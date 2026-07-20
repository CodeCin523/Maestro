#ifndef IMPL_MAESTRO_VULKAN_CONDUCTOR_H
#define IMPL_MAESTRO_VULKAN_CONDUCTOR_H

#include <maestro/maestro_vulkan_conductor.h>

#include <threads.h>
#include <stdatomic.h>


#define SEQ_SENTINEL UINT32_MAX

typedef struct SeqQueueSlot SeqQueueSlot;
typedef struct SeqDeviceState SeqDeviceState;
typedef struct MaestroVulkanConductorHandlerImpl MaestroVulkanConductorHandlerImpl;

typedef struct SeqCueRecord {
    u32 next; /* next cue index in the slot FIFO, or SEQ_SENTINEL */
    _Atomic u32 state;
    u32 generation; /* never 0; generation 0 marks the null cue */

    SeqQueueSlot *slot;
    u64 value;

    VkCommandBuffer cmd;
    VkFence extra_fence;

    /* Dependencies are snapshotted to timeline/value pairs at submit time.
       This is sound because a held cue is never recycled (see alloc_cue),
       so a dep's slot and value are stable while its handle circulates. */
    VkSemaphore wait_sems[MAESTRO_VULKAN_CUE_MAX_WAITS];
    u64 wait_values[MAESTRO_VULKAN_CUE_MAX_WAITS];
    u32 wait_count;
    VkSemaphore bin_waits[MAESTRO_VULKAN_CUE_MAX_BINARY];
    VkPipelineStageFlags2 bin_wait_stages[MAESTRO_VULKAN_CUE_MAX_BINARY];
    u32 bin_wait_count;
    VkSemaphore bin_signals[MAESTRO_VULKAN_CUE_MAX_BINARY];
    u32 bin_signal_count;

    b8 abandoned; /* released before completion; reclaimable once complete */
} SeqCueRecord;

struct SeqQueueSlot {
    VkQueue queue;
    VkDevice device;
    u32 family;
    VkQueueFlags flags;
    mtx_t lock;
    u32 head;
    u32 tail;
    _Atomic u32 pending_count;
    VkSemaphore timeline;
    u64 next_value;
};

struct SeqDeviceState {
    MaestroVulkanDeviceActor *device;
    SeqQueueSlot slots[MAESTRO_VULKAN_MAX_QUEUES];
    u32 slot_count;
    SeqDeviceState *next; /* intrusive list, guarded by attach_lock */
};

struct MaestroVulkanRecorder {
    MaestroVulkanConductorHandlerImpl *seq;
    SeqDeviceState *dev;
    SeqQueueSlot *slot;
    VkCommandPool pool;
    VkCommandBuffer bufs[MAESTRO_VULKAN_RECORDER_MAX_BUFS];
    u32 buf_count;
    u32 used;
    u64 last_value;
};

struct MaestroVulkanConductorHandlerImpl {
    MaestroVulkanConductorHandler pub;

    SeqDeviceState *device_list;
    mtx_t attach_lock;

    SeqCueRecord *cues; /* heap array of cue_capacity, allocated at init */
    u32 *free_list;
    u32 free_top;
    u32 cue_capacity;
    mtx_t pool_lock;
};


HarpResult init_vulkan_conductor(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_vulkan_conductor(HarpCoreHandler *core_handler, HarpHandlerBase *base);
HarpResult patch_vulkan_conductor(HarpCoreHandler *core_handler, HarpHandlerBase *base);

HarpResult conductor_open_recorder(MaestroVulkanConductorHandler *h, MaestroVulkanDeviceActor *device, MaestroVulkanQueueKind kind, MaestroVulkanRecorder **out);
HarpResult conductor_close_recorder(MaestroVulkanRecorder *rec);
VkCommandBuffer conductor_record(MaestroVulkanRecorder *rec);
MaestroVulkanCue conductor_submit(MaestroVulkanRecorder *rec, const MaestroVulkanSubmitDesc *desc);
HarpResult conductor_reset_recorder(MaestroVulkanRecorder *rec);
HarpResult conductor_flush(MaestroVulkanRecorder *rec);
HarpResult conductor_conduct(MaestroVulkanConductorHandler *h);
MaestroVulkanCueState conductor_cue_state(MaestroVulkanConductorHandler *h, MaestroVulkanCue cue);
b8 conductor_cue_done(MaestroVulkanConductorHandler *h, MaestroVulkanCue cue);
HarpResult conductor_cue_wait(MaestroVulkanConductorHandler *h, MaestroVulkanCue cue, u64 timeout_ns);
void conductor_cue_release(MaestroVulkanConductorHandler *h, MaestroVulkanCue cue);


#endif /* IMPL_MAESTRO_VULKAN_CONDUCTOR_H */
