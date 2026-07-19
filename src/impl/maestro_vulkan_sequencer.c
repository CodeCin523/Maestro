#include "impl/maestro_vulkan_sequencer.h"

#include "maestro_globals.h"

#include <harp/utils/harp_helpers.h>

#include <stdlib.h>


/* Cues per vkQueueSubmit2 call when flushing; a cue carrying an extra_fence
   also terminates its batch since the fence is a per-call parameter. */
#define SEQ_FLUSH_BATCH 16


/* ================================================================================ */
/*  HELPERS                                                                          */
/* ================================================================================ */

static MaestroVulkanSequencerHandlerImpl *impl_of(MaestroVulkanSequencerHandler *h) {
    return (MaestroVulkanSequencerHandlerImpl *)h;
}

static VkResult create_timeline(VkDevice device, VkSemaphore *out) {
    VkSemaphoreTypeCreateInfo type_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0
    };
    VkSemaphoreCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &type_info
    };
    return vkCreateSemaphore(device, &info, NULL, out);
}

/* Prefers a dedicated queue: compute without graphics, transfer without
   graphics or compute. Falls back to the first queue with the capability. */
static u32 select_queue_index(MaestroVulkanDeviceActor *device, MaestroVulkanQueueKind kind) {
    VkQueueFlags need;
    switch(kind) {
        case MAESTRO_VULKAN_QUEUE_COMPUTE:
            need = VK_QUEUE_COMPUTE_BIT;
            break;
        case MAESTRO_VULKAN_QUEUE_TRANSFER:
            need = VK_QUEUE_TRANSFER_BIT;
            break;
        case MAESTRO_VULKAN_QUEUE_GRAPHICS:
        default:
            need = VK_QUEUE_GRAPHICS_BIT;
            break;
    }

    u32 fallback = SEQ_SENTINEL;
    for(u32 i = 0; i < device->queue_count; ++i) {
        VkQueueFlags f = device->queues[i].flags;
        if(!(f & need)) continue;
        if(fallback == SEQ_SENTINEL) fallback = i;

        b8 dedicated = 1;
        if(kind != MAESTRO_VULKAN_QUEUE_GRAPHICS && (f & VK_QUEUE_GRAPHICS_BIT)) dedicated = 0;
        if(kind == MAESTRO_VULKAN_QUEUE_TRANSFER && (f & VK_QUEUE_COMPUTE_BIT)) dedicated = 0;
        if(dedicated) return i;
    }
    return fallback;
}

static SeqDeviceState *attach_device(MaestroVulkanSequencerHandlerImpl *impl, MaestroVulkanDeviceActor *device) {
    mtx_lock(&impl->attach_lock);

    for(SeqDeviceState *ds = impl->device_list; ds != NULL; ds = ds->next) {
        if(ds->device == device) {
            mtx_unlock(&impl->attach_lock);
            return ds;
        }
    }

    SeqDeviceState *ds = malloc(sizeof(*ds));
    if(ds == NULL) {
        mtx_unlock(&impl->attach_lock);
        return NULL;
    }

    ds->device = device;
    ds->slot_count = device->queue_count > MAESTRO_VULKAN_MAX_QUEUES
        ? MAESTRO_VULKAN_MAX_QUEUES
        : device->queue_count;

    for(u32 i = 0; i < ds->slot_count; ++i) {
        SeqQueueSlot *s = &ds->slots[i];
        s->queue = device->queues[i].queue;
        s->device = device->device;
        s->family = device->queues[i].family;
        s->flags = device->queues[i].flags;
        s->head = SEQ_SENTINEL;
        s->tail = SEQ_SENTINEL;
        atomic_store(&s->pending_count, 0);
        s->next_value = 0;
        s->timeline = VK_NULL_HANDLE;
        mtx_init(&s->lock, mtx_plain);
        if(create_timeline(device->device, &s->timeline) != VK_SUCCESS) {
            MAESTRO_LOGF_ERROR(g_logger, impl->pub._base.name,
                "failed to create timeline for queue family %u", s->family);
            /* A slot without a timeline cannot track anything: fail the attach
               instead of leaving a slot that would signal a null semaphore. */
            for(u32 j = 0; j <= i; ++j) {
                if(ds->slots[j].timeline != VK_NULL_HANDLE)
                    vkDestroySemaphore(device->device, ds->slots[j].timeline, NULL);
                mtx_destroy(&ds->slots[j].lock);
            }
            free(ds);
            mtx_unlock(&impl->attach_lock);
            return NULL;
        }
    }

    ds->next = impl->device_list;
    impl->device_list = ds;
    mtx_unlock(&impl->attach_lock);
    return ds;
}

/* pool_lock must be held. Returns a cue index marked OPEN, or SEQ_SENTINEL.
   Only free-listed or abandoned-and-complete cues are taken: a handle the
   application still holds is never recycled out from under it. */
static u32 alloc_cue(MaestroVulkanSequencerHandlerImpl *impl) {
    u32 idx = SEQ_SENTINEL;

    if(impl->free_top > 0) {
        idx = impl->free_list[--impl->free_top];
    } else {
        for(u32 i = 0; i < impl->cue_capacity; ++i) {
            SeqCueRecord *c = &impl->cues[i];
            if(!c->abandoned) continue;
            u32 st = atomic_load(&c->state);
            if(st == MAESTRO_VULKAN_CUE_DONE || st == MAESTRO_VULKAN_CUE_FAILED) {
                idx = i;
                break;
            }
            if(st == MAESTRO_VULKAN_CUE_IN_FLIGHT) {
                u64 v = 0;
                vkGetSemaphoreCounterValue(c->slot->device, c->slot->timeline, &v);
                if(v >= c->value) {
                    idx = i;
                    break;
                }
            }
        }
        if(idx == SEQ_SENTINEL) return SEQ_SENTINEL;
    }

    SeqCueRecord *c = &impl->cues[idx];
    c->abandoned = 0;
    if(++c->generation == 0) c->generation = 1; /* generation 0 marks the null cue */
    atomic_store(&c->state, MAESTRO_VULKAN_CUE_OPEN);
    return idx;
}

static void fifo_push(SeqQueueSlot *slot, SeqCueRecord *cues, u32 idx) {
    cues[idx].next = SEQ_SENTINEL;
    if(slot->head == SEQ_SENTINEL) slot->head = idx;
    else cues[slot->tail].next = idx;
    slot->tail = idx;
}

/* Batches the slot FIFO into vkQueueSubmit2 calls of up to SEQ_FLUSH_BATCH
   submissions. Submission order matches FIFO order, which matches monotonic
   timeline values. On a failed call the batch and everything queued behind
   it are marked FAILED and the timeline is host-signaled past the gap so
   waiters unblock; the FAILED state is how they learn the work never ran. */
static HarpResult flush_slot(MaestroVulkanSequencerHandlerImpl *impl, SeqQueueSlot *slot) {
    if(atomic_load(&slot->pending_count) == 0) return HARP_RESULT_OK;

    mtx_lock(&slot->lock);

    VkSubmitInfo2 submits[SEQ_FLUSH_BATCH];
    VkCommandBufferSubmitInfo cmd_infos[SEQ_FLUSH_BATCH];
    VkSemaphoreSubmitInfo waits[SEQ_FLUSH_BATCH][MAESTRO_VULKAN_CUE_MAX_WAITS + MAESTRO_VULKAN_CUE_MAX_BINARY];
    VkSemaphoreSubmitInfo signals[SEQ_FLUSH_BATCH][1 + MAESTRO_VULKAN_CUE_MAX_BINARY];
    u32 batch[SEQ_FLUSH_BATCH];

    HarpResult result = HARP_RESULT_OK;
    u32 idx = slot->head;
    while(idx != SEQ_SENTINEL) {
        u32 n = 0;
        VkFence fence = VK_NULL_HANDLE;
        while(idx != SEQ_SENTINEL && n < SEQ_FLUSH_BATCH) {
            SeqCueRecord *cue = &impl->cues[idx];

            u32 wc = 0;
            for(u32 i = 0; i < cue->wait_count; ++i) {
                waits[n][wc++] = (VkSemaphoreSubmitInfo){
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                    .semaphore = cue->wait_sems[i],
                    .value = cue->wait_values[i],
                    .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
                };
            }
            for(u32 i = 0; i < cue->bin_wait_count; ++i) {
                waits[n][wc++] = (VkSemaphoreSubmitInfo){
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                    .semaphore = cue->bin_waits[i],
                    .value = 0,
                    .stageMask = cue->bin_wait_stages[i]
                };
            }

            u32 sc = 0;
            signals[n][sc++] = (VkSemaphoreSubmitInfo){
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = slot->timeline,
                .value = cue->value,
                .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
            };
            for(u32 i = 0; i < cue->bin_signal_count; ++i) {
                signals[n][sc++] = (VkSemaphoreSubmitInfo){
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                    .semaphore = cue->bin_signals[i],
                    .value = 0,
                    .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
                };
            }

            cmd_infos[n] = (VkCommandBufferSubmitInfo){
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = cue->cmd
            };
            submits[n] = (VkSubmitInfo2){
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                .waitSemaphoreInfoCount = wc,
                .pWaitSemaphoreInfos = waits[n],
                .commandBufferInfoCount = 1,
                .pCommandBufferInfos = &cmd_infos[n],
                .signalSemaphoreInfoCount = sc,
                .pSignalSemaphoreInfos = signals[n]
            };
            batch[n] = idx;
            fence = cue->extra_fence;
            n++;
            idx = cue->next;
            if(fence != VK_NULL_HANDLE) break;
        }

        if(vkQueueSubmit2(slot->queue, n, submits, fence) == VK_SUCCESS) {
            for(u32 i = 0; i < n; ++i)
                atomic_store(&impl->cues[batch[i]].state, MAESTRO_VULKAN_CUE_IN_FLIGHT);
        } else {
            u64 base = impl->cues[batch[0]].value - 1;
            u64 tail = 0;
            for(u32 i = 0; i < n; ++i) {
                atomic_store(&impl->cues[batch[i]].state, MAESTRO_VULKAN_CUE_FAILED);
                tail = impl->cues[batch[i]].value;
            }
            for(; idx != SEQ_SENTINEL; idx = impl->cues[idx].next) {
                atomic_store(&impl->cues[idx].state, MAESTRO_VULKAN_CUE_FAILED);
                tail = impl->cues[idx].value;
            }
            MAESTRO_LOGF_ERROR(g_logger, impl->pub._base.name,
                "vkQueueSubmit2 failed on family %u; cue values %llu..%llu retired as FAILED",
                slot->family, (unsigned long long)(base + 1), (unsigned long long)tail);

            /* Host-signaling must stay behind pending GPU signals, so wait
               for the values submitted before the failure to land first. */
            VkSemaphoreWaitInfo wi = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                .semaphoreCount = 1,
                .pSemaphores = &slot->timeline,
                .pValues = &base
            };
            if(vkWaitSemaphores(slot->device, &wi, UINT64_MAX) == VK_SUCCESS) {
                VkSemaphoreSignalInfo si = {
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
                    .semaphore = slot->timeline,
                    .value = tail
                };
                vkSignalSemaphore(slot->device, &si);
            }
            result = HARP_RESULT_FAILED;
            break;
        }
    }

    slot->head = SEQ_SENTINEL;
    slot->tail = SEQ_SENTINEL;
    atomic_store(&slot->pending_count, 0);
    mtx_unlock(&slot->lock);
    return result;
}

static HarpResult flush_all(MaestroVulkanSequencerHandlerImpl *impl) {
    HarpResult result = HARP_RESULT_OK;
    mtx_lock(&impl->attach_lock);
    for(SeqDeviceState *ds = impl->device_list; ds != NULL; ds = ds->next) {
        for(u32 s = 0; s < ds->slot_count; ++s) {
            HarpResult r = flush_slot(impl, &ds->slots[s]);
            if(r != HARP_RESULT_OK) result = r;
        }
    }
    mtx_unlock(&impl->attach_lock);
    return result;
}


/* ================================================================================ */
/*  RECORDERS                                                                        */
/* ================================================================================ */

HarpResult seq_open_recorder(MaestroVulkanSequencerHandler *h, MaestroVulkanDeviceActor *device, MaestroVulkanQueueKind kind, MaestroVulkanRecorder **out) {
    HARP_CHECK_STATE(HARP_HANDLER_IS_VALID(h), HARP_RESULT_INVALID_STATE);
    HARP_CHECK_ARG(HARP_ACTOR_IS_VALID(device), HARP_RESULT_INVALID_ARGUMENTS);
    HARP_CHECK_ARG(out != NULL, HARP_RESULT_MISSING_OUTPUT);

    *out = NULL;
    MaestroVulkanSequencerHandlerImpl *impl = impl_of(h);

    SeqDeviceState *ds = attach_device(impl, device);
    if(ds == NULL) return HARP_RESULT_FAILED;

    u32 slot_idx = select_queue_index(device, kind);
    if(slot_idx == SEQ_SENTINEL || slot_idx >= ds->slot_count) return HARP_RESULT_FAILED;
    SeqQueueSlot *slot = &ds->slots[slot_idx];

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = slot->family
    };
    VkCommandPool pool;
    if(vkCreateCommandPool(device->device, &pool_info, NULL, &pool) != VK_SUCCESS)
        return HARP_RESULT_FAILED;

    MaestroVulkanRecorder *rec = malloc(sizeof(*rec));
    if(rec == NULL) {
        vkDestroyCommandPool(device->device, pool, NULL);
        return HARP_RESULT_OUT_OF_MEMORY;
    }
    rec->seq = impl;
    rec->dev = ds;
    rec->slot = slot;
    rec->pool = pool;
    rec->buf_count = 0;
    rec->used = 0;
    rec->last_value = 0;

    *out = rec;
    return HARP_RESULT_OK;
}

HarpResult seq_close_recorder(MaestroVulkanRecorder *rec) {
    if(rec == NULL) return HARP_RESULT_INVALID_STATE;

    if(rec->last_value > 0 && rec->slot->timeline != VK_NULL_HANDLE) {
        VkSemaphoreWaitInfo wi = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .semaphoreCount = 1,
            .pSemaphores = &rec->slot->timeline,
            .pValues = &rec->last_value
        };
        vkWaitSemaphores(rec->slot->device, &wi, UINT64_MAX);
    }

    vkDestroyCommandPool(rec->slot->device, rec->pool, NULL);
    free(rec);
    return HARP_RESULT_OK;
}

VkCommandBuffer seq_record(MaestroVulkanRecorder *rec) {
    if(rec == NULL) return VK_NULL_HANDLE;

    VkCommandBuffer cmd;
    if(rec->used < rec->buf_count) {
        cmd = rec->bufs[rec->used];
    } else {
        if(rec->buf_count >= MAESTRO_VULKAN_RECORDER_MAX_BUFS) return VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo ai = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = rec->pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        if(vkAllocateCommandBuffers(rec->slot->device, &ai, &cmd) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        rec->bufs[rec->buf_count++] = cmd;
    }

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    if(vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) return VK_NULL_HANDLE;
    rec->used++;
    return cmd;
}

MaestroVulkanCue seq_submit(MaestroVulkanRecorder *rec, const MaestroVulkanSubmitDesc *desc) {
    MaestroVulkanCue invalid = { SEQ_SENTINEL, 0 };
    if(rec == NULL || desc == NULL || desc->cmd == VK_NULL_HANDLE) return invalid;

    MaestroVulkanSequencerHandlerImpl *impl = rec->seq;

    /* A truncated dependency would be a silent sync bug, so oversized or
       inconsistent lists are rejected outright. */
    if(desc->wait_count > MAESTRO_VULKAN_CUE_MAX_WAITS ||
       desc->binary_wait_count > MAESTRO_VULKAN_CUE_MAX_BINARY ||
       desc->binary_signal_count > MAESTRO_VULKAN_CUE_MAX_BINARY) {
        MAESTRO_LOG_ERROR(g_logger, impl->pub._base.name,
            "submit rejected: too many waits or signals");
        return invalid;
    }
    if((desc->wait_count > 0 && desc->waits == NULL) ||
       (desc->binary_wait_count > 0 && desc->binary_waits == NULL) ||
       (desc->binary_signal_count > 0 && desc->binary_signals == NULL)) {
        MAESTRO_LOG_ERROR(g_logger, impl->pub._base.name,
            "submit rejected: wait/signal count given without an array");
        return invalid;
    }

    VkSemaphore wait_sems[MAESTRO_VULKAN_CUE_MAX_WAITS];
    u64 wait_values[MAESTRO_VULKAN_CUE_MAX_WAITS];
    u32 wait_count = 0;

    mtx_lock(&impl->pool_lock);

    for(u32 i = 0; i < desc->wait_count; ++i) {
        MaestroVulkanCue dep = desc->waits[i];
        if(dep.index >= impl->cue_capacity) {
            mtx_unlock(&impl->pool_lock);
            MAESTRO_LOG_ERROR(g_logger, impl->pub._base.name,
                "submit rejected: dependency cue never existed");
            return invalid;
        }
        SeqCueRecord *d = &impl->cues[dep.index];
        if(d->generation != dep.gen) continue; /* completed and recycled */
        if(d->slot->device != rec->slot->device) {
            mtx_unlock(&impl->pool_lock);
            MAESTRO_LOG_ERROR(g_logger, impl->pub._base.name,
                "submit rejected: cross-device cue dependency is not supported");
            return invalid;
        }
        wait_sems[wait_count] = d->slot->timeline;
        wait_values[wait_count] = d->value;
        wait_count++;
    }

    u32 idx = alloc_cue(impl);
    if(idx == SEQ_SENTINEL) {
        mtx_unlock(&impl->pool_lock);
        MAESTRO_LOG_ERROR(g_logger, impl->pub._base.name, "cue pool exhausted");
        return invalid;
    }
    SeqCueRecord *cue = &impl->cues[idx];
    u32 gen = cue->generation;
    mtx_unlock(&impl->pool_lock);

    if(vkEndCommandBuffer(desc->cmd) != VK_SUCCESS) {
        mtx_lock(&impl->pool_lock);
        atomic_store(&cue->state, MAESTRO_VULKAN_CUE_RETIRED);
        if(impl->free_top < impl->cue_capacity)
            impl->free_list[impl->free_top++] = idx;
        mtx_unlock(&impl->pool_lock);
        return invalid;
    }

    cue->slot = rec->slot;
    cue->cmd = desc->cmd;
    cue->extra_fence = desc->extra_fence;

    cue->wait_count = wait_count;
    for(u32 i = 0; i < wait_count; ++i) {
        cue->wait_sems[i] = wait_sems[i];
        cue->wait_values[i] = wait_values[i];
    }

    cue->bin_wait_count = desc->binary_wait_count;
    for(u32 i = 0; i < cue->bin_wait_count; ++i) {
        cue->bin_waits[i] = desc->binary_waits[i];
        cue->bin_wait_stages[i] = desc->binary_wait_stages
            ? desc->binary_wait_stages[i] : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }

    cue->bin_signal_count = desc->binary_signal_count;
    for(u32 i = 0; i < cue->bin_signal_count; ++i)
        cue->bin_signals[i] = desc->binary_signals[i];

    SeqQueueSlot *slot = rec->slot;
    mtx_lock(&slot->lock);
    cue->value = ++slot->next_value;
    atomic_store(&cue->state, MAESTRO_VULKAN_CUE_RECORDED);
    fifo_push(slot, impl->cues, idx);
    atomic_fetch_add(&slot->pending_count, 1);
    mtx_unlock(&slot->lock);

    rec->last_value = cue->value;
    return (MaestroVulkanCue){ idx, gen };
}

HarpResult seq_reset_recorder(MaestroVulkanRecorder *rec) {
    if(rec == NULL) return HARP_RESULT_INVALID_STATE;

    if(rec->last_value > 0) {
        u64 v = 0;
        vkGetSemaphoreCounterValue(rec->slot->device, rec->slot->timeline, &v);
        if(v < rec->last_value) return HARP_RESULT_INVALID_STATE; /* still in flight, retry */
    }

    vkResetCommandPool(rec->slot->device, rec->pool, 0);
    rec->used = 0;
    return HARP_RESULT_OK;
}

HarpResult seq_flush(MaestroVulkanRecorder *rec) {
    if(rec == NULL) return HARP_RESULT_INVALID_STATE;
    return flush_slot(rec->seq, rec->slot);
}

HarpResult seq_conduct(MaestroVulkanSequencerHandler *h) {
    HARP_CHECK_STATE(HARP_HANDLER_IS_VALID(h), HARP_RESULT_INVALID_STATE);
    return flush_all(impl_of(h));
}


/* ================================================================================ */
/*  CUES                                                                             */
/* ================================================================================ */

MaestroVulkanCueState seq_cue_state(MaestroVulkanSequencerHandler *h, MaestroVulkanCue cue) {
    if(!HARP_HANDLER_IS_VALID(h)) return MAESTRO_VULKAN_CUE_RETIRED;
    MaestroVulkanSequencerHandlerImpl *impl = impl_of(h);
    if(cue.index >= impl->cue_capacity) return MAESTRO_VULKAN_CUE_RETIRED;
    SeqCueRecord *c = &impl->cues[cue.index];
    if(c->generation != cue.gen) return MAESTRO_VULKAN_CUE_RETIRED;

    u32 st = atomic_load(&c->state);
    if(st == MAESTRO_VULKAN_CUE_IN_FLIGHT) {
        u64 v = 0;
        vkGetSemaphoreCounterValue(c->slot->device, c->slot->timeline, &v);
        if(v >= c->value) {
            atomic_store(&c->state, MAESTRO_VULKAN_CUE_DONE);
            st = MAESTRO_VULKAN_CUE_DONE;
        }
    }
    return (MaestroVulkanCueState)st;
}

b8 seq_cue_done(MaestroVulkanSequencerHandler *h, MaestroVulkanCue cue) {
    if(!HARP_HANDLER_IS_VALID(h)) return 0;
    MaestroVulkanSequencerHandlerImpl *impl = impl_of(h);
    if(cue.index >= impl->cue_capacity) return 0;
    SeqCueRecord *c = &impl->cues[cue.index];
    if(c->generation != cue.gen) return 1; /* stale: completed and recycled */

    u32 st = atomic_load(&c->state);
    if(st == MAESTRO_VULKAN_CUE_DONE || st == MAESTRO_VULKAN_CUE_FAILED) return 1;
    if(st != MAESTRO_VULKAN_CUE_IN_FLIGHT) return 0;

    u64 v = 0;
    vkGetSemaphoreCounterValue(c->slot->device, c->slot->timeline, &v);
    if(v >= c->value) {
        atomic_store(&c->state, MAESTRO_VULKAN_CUE_DONE);
        return 1;
    }
    return 0;
}

HarpResult seq_cue_wait(MaestroVulkanSequencerHandler *h, MaestroVulkanCue cue, u64 timeout_ns) {
    HARP_CHECK_STATE(HARP_HANDLER_IS_VALID(h), HARP_RESULT_INVALID_STATE);
    MaestroVulkanSequencerHandlerImpl *impl = impl_of(h);
    if(cue.index >= impl->cue_capacity) return HARP_RESULT_INVALID_ARGUMENTS;
    SeqCueRecord *c = &impl->cues[cue.index];
    if(c->generation != cue.gen) return HARP_RESULT_OK; /* stale: completed and recycled */

    /* Flush-on-wait: an unflushed cue can never signal, and its dependencies
       may sit unflushed on other slots, so push everything through first. */
    if(atomic_load(&c->state) < MAESTRO_VULKAN_CUE_IN_FLIGHT)
        flush_all(impl);

    if(atomic_load(&c->state) == MAESTRO_VULKAN_CUE_FAILED)
        return HARP_RESULT_FAILED;

    SeqQueueSlot *slot = c->slot;
    VkSemaphoreWaitInfo wi = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &slot->timeline,
        .pValues = &c->value
    };
    VkResult r = vkWaitSemaphores(slot->device, &wi, timeout_ns);
    if(r == VK_TIMEOUT) return HARP_RESULT_INVALID_STATE; /* not done yet, retry */
    if(r != VK_SUCCESS) return HARP_RESULT_FAILED;

    /* The timeline is repaired past failed submissions, so the wait can
       succeed for work that never ran; the state keeps the truth. */
    if(atomic_load(&c->state) == MAESTRO_VULKAN_CUE_FAILED)
        return HARP_RESULT_FAILED;
    atomic_store(&c->state, MAESTRO_VULKAN_CUE_DONE);
    return HARP_RESULT_OK;
}

void seq_cue_release(MaestroVulkanSequencerHandler *h, MaestroVulkanCue cue) {
    if(!HARP_HANDLER_IS_VALID(h)) return;
    MaestroVulkanSequencerHandlerImpl *impl = impl_of(h);
    if(cue.index >= impl->cue_capacity) return;
    SeqCueRecord *c = &impl->cues[cue.index];
    if(c->generation != cue.gen) return;

    u32 st = atomic_load(&c->state);
    b8 done = (st == MAESTRO_VULKAN_CUE_DONE || st == MAESTRO_VULKAN_CUE_FAILED);
    if(!done && st == MAESTRO_VULKAN_CUE_IN_FLIGHT) {
        u64 v = 0;
        vkGetSemaphoreCounterValue(c->slot->device, c->slot->timeline, &v);
        if(v >= c->value) {
            atomic_store(&c->state, MAESTRO_VULKAN_CUE_DONE);
            done = 1;
        }
    }

    if(done) {
        mtx_lock(&impl->pool_lock);
        if(c->generation == cue.gen) { /* re-check: lose a concurrent release race */
            if(++c->generation == 0) c->generation = 1;
            if(impl->free_top < impl->cue_capacity)
                impl->free_list[impl->free_top++] = cue.index;
        }
        mtx_unlock(&impl->pool_lock);
    } else {
        c->abandoned = 1;
    }
}


/* ================================================================================ */
/*  LIFECYCLE                                                                        */
/* ================================================================================ */

HarpResult init_vulkan_sequencer(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator) {
    HARP_UNUSED(core_handler);
    MaestroVulkanSequencerHandlerImpl *impl = (MaestroVulkanSequencerHandlerImpl *)base;

    u32 capacity = MAESTRO_VULKAN_MAX_CUES;
    if(creator != NULL && !(creator->flags & HARP_CREATOR_FLAG_DEFAULT_CREATOR)) {
        const MaestroVulkanSequencerCreator *c = (const MaestroVulkanSequencerCreator *)creator;
        if(c->cue_capacity > 0) capacity = c->cue_capacity;
    }

    mtx_init(&impl->attach_lock, mtx_plain);
    mtx_init(&impl->pool_lock, mtx_plain);

    /* Allocated once here, off the hot path, so the pool does not bloat the
       handler arena instance. Survives a hot-swap patch via the reused impl. */
    impl->cue_capacity = capacity;
    impl->cues = calloc(capacity, sizeof(*impl->cues));
    impl->free_list = calloc(capacity, sizeof(*impl->free_list));
    if(impl->cues == NULL || impl->free_list == NULL) {
        free(impl->cues);
        free(impl->free_list);
        impl->cues = NULL;
        impl->free_list = NULL;
        mtx_destroy(&impl->attach_lock);
        mtx_destroy(&impl->pool_lock);
        return HARP_RESULT_OUT_OF_MEMORY;
    }

    impl->device_list = NULL;
    impl->free_top = capacity;
    for(u32 i = 0; i < capacity; ++i) {
        impl->free_list[i] = i;
        impl->cues[i].generation = 1; /* 0 is reserved for the null cue */
        impl->cues[i].next = SEQ_SENTINEL;
        impl->cues[i].slot = NULL;
        impl->cues[i].value = 0;
        impl->cues[i].abandoned = 0;
        atomic_store(&impl->cues[i].state, MAESTRO_VULKAN_CUE_RETIRED);
    }
    return HARP_RESULT_OK;
}

HarpResult term_vulkan_sequencer(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);
    MaestroVulkanSequencerHandlerImpl *impl = (MaestroVulkanSequencerHandlerImpl *)base;

    SeqDeviceState *ds = impl->device_list;
    while(ds != NULL) {
        SeqDeviceState *next = ds->next;
        b8 dev_ok = HARP_ACTOR_IS_VALID(ds->device);
        if(dev_ok) vkDeviceWaitIdle(ds->device->device);

        for(u32 s = 0; s < ds->slot_count; ++s) {
            SeqQueueSlot *slot = &ds->slots[s];
            if(dev_ok && slot->timeline != VK_NULL_HANDLE)
                vkDestroySemaphore(slot->device, slot->timeline, NULL);
            mtx_destroy(&slot->lock);
        }
        free(ds);
        ds = next;
    }
    impl->device_list = NULL;

    free(impl->cues);
    free(impl->free_list);
    impl->cues = NULL;
    impl->free_list = NULL;

    mtx_destroy(&impl->attach_lock);
    mtx_destroy(&impl->pool_lock);
    return HARP_RESULT_OK;
}

HarpResult patch_vulkan_sequencer(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);
    HARP_UNUSED(base);
    return HARP_RESULT_OK;
}
