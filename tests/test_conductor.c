#include <harp/harp_api.h>

#include <maestro/maestro.h>
#include <maestro/maestro_logger.h>
#include <maestro/maestro_vulkan.h>
#include <maestro/maestro_vulkan_conductor.h>

#include <harp/utils/harp_helpers.h>
#include <harp/utils/harp_version.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>


/* ========================================================= */
/* TEST MACRO                                                 */
/* ========================================================= */

#define TEST_MARKER(name, phase) \
    printf("[TEST:%s:%s]\n", name, phase)

static void assert_harp(HarpResult r, const char *msg) {
    if(r != HARP_RESULT_OK) {
        printf("ASSERT_FAIL: %s (code=%d)\n", msg, r);
        exit(EXIT_FAILURE);
    }
}


/* ========================================================= */
/* GLOBALS                                                    */
/* ========================================================= */

static HarpRuntime *g_runtime = NULL;
static HarpCoreHandler *g_core = NULL;
static MaestroLoggerHandler *g_logger = NULL;
static MaestroVulkanCoreHandler *g_vk = NULL;
static MaestroVulkanConductorHandler *g_seq = NULL;
static HarpActorBase *g_device_actor = NULL;


/* ========================================================= */
/* SETUP                                                      */
/* ========================================================= */

static void test_runtime_init(char *argv0) {
    TEST_MARKER("RUNTIME", "INIT");

    HarpRuntimeDesc desc = { .executable_path = argv0 };
    assert_harp(harp_initialize(&desc, &g_runtime), "Failed to init Harp runtime");

    HarpDependencyDesc dep = {
        .name = HARP_CORE_HANDLER_NAME,
        .min_version = 0,
        .max_version = UINT32_MAX
    };

    HarpHandlerBase *core_base = NULL;
    assert_harp(harp_runtime_get_handler(g_runtime, &dep, &core_base), "Failed to get core handler");
    g_core = (HarpCoreHandler *)core_base;

    TEST_MARKER("RUNTIME", "INIT_DONE");
}

extern HarpResult maestro_register(HarpCoreHandler *);

static void test_maestro_register(void) {
    TEST_MARKER("MAESTRO", "REGISTER");
    assert_harp(maestro_register(g_core), "Failed to register Maestro package");
    TEST_MARKER("MAESTRO", "REGISTER_DONE");
}

static void test_logger_init(void) {
    TEST_MARKER("LOGGER", "INIT");

    HarpDependencyDesc dep = HARP_DEPENDENCY(MAESTRO_LOGGER_HANDLER_NAME, 0, UINT32_MAX);
    HarpHandlerBase *base = NULL;
    assert_harp(g_core->get_handler(g_core, &dep, &base), "Failed to get logger");
    g_logger = (MaestroLoggerHandler *)base;

    HarpCreatorBase creator = { .flags = HARP_CREATOR_FLAG_DEFAULT_CREATOR };
    assert_harp(
        g_core->handler_initialize(g_core, MAESTRO_LOGGER_HANDLER_NAME, &creator),
        "Failed to initialize logger"
    );

    TEST_MARKER("LOGGER", "INIT_DONE");
}


/* ========================================================= */
/* VULKAN CORE                                                */
/* ========================================================= */

static void test_vulkan_core_init(void) {
    TEST_MARKER("VULKAN_CORE", "INIT");

    HarpDependencyDesc dep = HARP_DEPENDENCY(MAESTRO_VULKAN_CORE_HANDLER_NAME, 0, UINT32_MAX);
    HarpHandlerBase *base = NULL;
    assert_harp(g_core->get_handler(g_core, &dep, &base), "Failed to get Vulkan core handler");
    g_vk = (MaestroVulkanCoreHandler *)base;

    MaestroVulkanCoreCreator creator = {
        ._base = { .kind = 0, .flags = 0 },
        .app_name = "MaestroConductorTest",
        .app_version = HARP_MAKE_VERSION(1, 0, 0),
        .extensions = NULL,
        .extension_count = 0,
        .enable_validation = 1
    };

    HarpResult res = g_core->handler_initialize(
        g_core,
        MAESTRO_VULKAN_CORE_HANDLER_NAME,
        (const HarpCreatorBase *)&creator
    );

    if(res != HARP_RESULT_OK) {
        printf("    [SKIP] Vulkan not available (code=%d)\n", res);
        g_vk = NULL;
        return;
    }

    assert(g_vk->instance != VK_NULL_HANDLE);
    TEST_MARKER("VULKAN_CORE", "INIT_DONE");
}

static void test_conductor_get_handler(void) {
    if(!g_vk) return;

    TEST_MARKER("CONDUCTOR", "GET_HANDLER");

    HarpDependencyDesc dep = HARP_DEPENDENCY(MAESTRO_VULKAN_CONDUCTOR_HANDLER_NAME, 0, UINT32_MAX);
    HarpHandlerBase *base = NULL;
    assert_harp(g_core->get_handler(g_core, &dep, &base), "Failed to get conductor handler");
    g_seq = (MaestroVulkanConductorHandler *)base;

    assert(g_seq->open_recorder != NULL);
    assert(g_seq->record != NULL);
    assert(g_seq->submit != NULL);
    assert(g_seq->conduct != NULL);
    assert(g_seq->cue_wait != NULL);
    assert(g_seq->cue_done != NULL);

    /* small pool so the exhaustion test can fill it */
    MaestroVulkanConductorCreator creator = {
        ._base = { .kind = 0, .flags = 0 },
        .cue_capacity = 4
    };
    assert_harp(
        g_core->handler_initialize(g_core, MAESTRO_VULKAN_CONDUCTOR_HANDLER_NAME,
            (const HarpCreatorBase *)&creator),
        "Failed to initialize conductor"
    );

    TEST_MARKER("CONDUCTOR", "GET_HANDLER_DONE");
}

static void test_device_create(void) {
    if(!g_vk || !g_seq) return;

    TEST_MARKER("CONDUCTOR", "DEVICE_CREATE");

    VkPhysicalDeviceVulkan13Features f13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE
    };
    VkPhysicalDeviceVulkan12Features f12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &f13,
        .timelineSemaphore = VK_TRUE
    };
    VkPhysicalDeviceFeatures2 f2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &f12
    };

    MaestroVulkanDeviceCreator creator = {
        ._base = { .kind = 0, .flags = 0 },
        .pfn_score = NULL,
        .features = &f2,
        .extensions = NULL,
        .extension_count = 0,
        .surface = VK_NULL_HANDLE
    };

    HarpResult res = g_core->actor_create(
        g_core,
        MAESTRO_VULKAN_DEVICE_ACTOR_NAME,
        (const HarpCreatorBase *)&creator,
        &g_device_actor
    );

    if(res != HARP_RESULT_OK) {
        printf("    [SKIP] device with timeline+sync2 unavailable (code=%d)\n", res);
        g_device_actor = NULL;
        return;
    }

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);
    printf("    device created with %u queue(s)\n", actor->queue_count);

    TEST_MARKER("CONDUCTOR", "DEVICE_CREATE_DONE");
}


/* ========================================================= */
/* CONDUCTOR                                                  */
/* ========================================================= */

static b8 make_host_buffer(MaestroVulkanDeviceActor *actor, VkDeviceSize size, VkBuffer *out_buf, VkDeviceMemory *out_mem) {
    VkMemoryRequirements reqs;
    if(g_vk->create_buffer_unbound(actor, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, out_buf, &reqs) != HARP_RESULT_OK)
        return 0;

    u32 type = g_vk->find_memory_type(
        actor, reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    if(type == UINT32_MAX)
        return 0;

    if(g_vk->alloc_memory(actor, reqs.size, type, 0, out_mem) != HARP_RESULT_OK)
        return 0;
    if(g_vk->bind_buffer(actor, *out_buf, *out_mem, 0) != HARP_RESULT_OK)
        return 0;

    return 1;
}

static void test_conductor_single(void) {
    if(!g_seq || !g_device_actor) return;

    TEST_MARKER("CONDUCTOR", "SINGLE");

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);

    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    assert(make_host_buffer(actor, 64, &buf, &mem));

    MaestroVulkanRecorder *rec = NULL;
    assert_harp(g_seq->open_recorder(g_seq, actor, MAESTRO_VULKAN_QUEUE_GRAPHICS, &rec), "open_recorder failed");

    VkCommandBuffer cmd = g_seq->record(rec);
    assert(cmd != VK_NULL_HANDLE);
    vkCmdFillBuffer(cmd, buf, 0, 64, 0xABABABABu);

    MaestroVulkanSubmitDesc desc = { .cmd = cmd };
    MaestroVulkanCue cue = g_seq->submit(rec, &desc);
    assert(cue.index != UINT32_MAX);

    /* Deferred: nothing reaches the GPU until conduct. */
    assert(g_seq->cue_state(g_seq, cue) == MAESTRO_VULKAN_CUE_RECORDED);

    assert_harp(g_seq->conduct(g_seq), "conduct failed");
    assert_harp(g_seq->cue_wait(g_seq, cue, UINT64_MAX), "cue_wait failed");
    assert(g_seq->cue_done(g_seq, cue));
    assert(g_seq->cue_state(g_seq, cue) == MAESTRO_VULKAN_CUE_DONE);

    void *mapped = NULL;
    assert(vkMapMemory(actor->device, mem, 0, 64, 0, &mapped) == VK_SUCCESS);
    for(u32 i = 0; i < 16; ++i)
        assert(((u32 *)mapped)[i] == 0xABABABABu);
    vkUnmapMemory(actor->device, mem);

    assert_harp(g_seq->reset_recorder(rec), "reset_recorder failed after completion");

    g_seq->cue_release(g_seq, cue);
    assert_harp(g_seq->close_recorder(rec), "close_recorder failed");
    g_vk->destroy_buffer(actor, buf);
    g_vk->free_memory(actor, mem);

    printf("    record -> submit -> conduct -> wait: buffer filled by GPU\n");
    TEST_MARKER("CONDUCTOR", "SINGLE_DONE");
}

static void test_conductor_dependency(void) {
    if(!g_seq || !g_device_actor) return;

    TEST_MARKER("CONDUCTOR", "DEPENDENCY");

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);

    VkBuffer buf_a = VK_NULL_HANDLE, buf_b = VK_NULL_HANDLE;
    VkDeviceMemory mem_a = VK_NULL_HANDLE, mem_b = VK_NULL_HANDLE;
    assert(make_host_buffer(actor, 64, &buf_a, &mem_a));
    assert(make_host_buffer(actor, 64, &buf_b, &mem_b));

    MaestroVulkanRecorder *rec = NULL;
    assert_harp(g_seq->open_recorder(g_seq, actor, MAESTRO_VULKAN_QUEUE_GRAPHICS, &rec), "open_recorder failed");

    VkCommandBuffer cmd_a = g_seq->record(rec);
    assert(cmd_a != VK_NULL_HANDLE);
    vkCmdFillBuffer(cmd_a, buf_a, 0, 64, 0x11111111u);
    MaestroVulkanSubmitDesc desc_a = { .cmd = cmd_a };
    MaestroVulkanCue cue_a = g_seq->submit(rec, &desc_a);
    assert(cue_a.index != UINT32_MAX);

    VkCommandBuffer cmd_b = g_seq->record(rec);
    assert(cmd_b != VK_NULL_HANDLE);
    vkCmdFillBuffer(cmd_b, buf_b, 0, 64, 0x22222222u);
    MaestroVulkanSubmitDesc desc_b = { .cmd = cmd_b, .waits = &cue_a, .wait_count = 1 };
    MaestroVulkanCue cue_b = g_seq->submit(rec, &desc_b);
    assert(cue_b.index != UINT32_MAX);

    assert_harp(g_seq->conduct(g_seq), "conduct failed");
    assert_harp(g_seq->cue_wait(g_seq, cue_b, UINT64_MAX), "cue_wait B failed");

    /* B waited on A, so B being done proves A's timeline reached A's value. */
    assert(g_seq->cue_done(g_seq, cue_a));
    assert(g_seq->cue_done(g_seq, cue_b));

    void *mapped = NULL;
    assert(vkMapMemory(actor->device, mem_a, 0, 64, 0, &mapped) == VK_SUCCESS);
    assert(((u32 *)mapped)[0] == 0x11111111u);
    vkUnmapMemory(actor->device, mem_a);
    assert(vkMapMemory(actor->device, mem_b, 0, 64, 0, &mapped) == VK_SUCCESS);
    assert(((u32 *)mapped)[0] == 0x22222222u);
    vkUnmapMemory(actor->device, mem_b);

    g_seq->cue_release(g_seq, cue_a);
    g_seq->cue_release(g_seq, cue_b);
    assert_harp(g_seq->close_recorder(rec), "close_recorder failed");
    g_vk->destroy_buffer(actor, buf_a);
    g_vk->free_memory(actor, mem_a);
    g_vk->destroy_buffer(actor, buf_b);
    g_vk->free_memory(actor, mem_b);

    printf("    dependency A <- B: both completed, values correct\n");
    TEST_MARKER("CONDUCTOR", "DEPENDENCY_DONE");
}

static void test_conductor_stale_cue(void) {
    if(!g_seq) return;

    TEST_MARKER("CONDUCTOR", "STALE_CUE");

    /* generation mismatch means completed-and-recycled: reads as done */
    MaestroVulkanCue bogus = { 0, 999999u };
    assert(g_seq->cue_state(g_seq, bogus) == MAESTRO_VULKAN_CUE_RETIRED);
    assert(g_seq->cue_done(g_seq, bogus) == 1);
    assert_harp(g_seq->cue_wait(g_seq, bogus, 0), "stale cue_wait should succeed");

    /* a zero-initialized cue is the null cue and also reads as done */
    MaestroVulkanCue null_cue = {0};
    assert(g_seq->cue_state(g_seq, null_cue) == MAESTRO_VULKAN_CUE_RETIRED);
    assert(g_seq->cue_done(g_seq, null_cue) == 1);
    assert_harp(g_seq->cue_wait(g_seq, null_cue, 0), "null cue_wait should succeed");

    /* an out-of-range index never existed: that is an error, not completion */
    MaestroVulkanCue oob = { UINT32_MAX, 0 };
    assert(g_seq->cue_state(g_seq, oob) == MAESTRO_VULKAN_CUE_RETIRED);
    assert(g_seq->cue_done(g_seq, oob) == 0);
    assert(g_seq->cue_wait(g_seq, oob, 0) != HARP_RESULT_OK);

    printf("    stale and null cues read as done, out-of-range errors\n");
    TEST_MARKER("CONDUCTOR", "STALE_CUE_DONE");
}

static void test_conductor_exhaustion(void) {
    if(!g_seq || !g_device_actor) return;

    TEST_MARKER("CONDUCTOR", "EXHAUSTION");

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);

    MaestroVulkanRecorder *rec = NULL;
    assert_harp(g_seq->open_recorder(g_seq, actor, MAESTRO_VULKAN_QUEUE_GRAPHICS, &rec), "open_recorder failed");

    /* fill the pool (cue_capacity = 4) with held cues */
    MaestroVulkanCue held[4];
    for(u32 i = 0; i < 4; ++i) {
        VkCommandBuffer cmd = g_seq->record(rec);
        assert(cmd != VK_NULL_HANDLE);
        MaestroVulkanSubmitDesc desc = { .cmd = cmd };
        held[i] = g_seq->submit(rec, &desc);
        assert(held[i].index != UINT32_MAX);
    }

    VkCommandBuffer extra = g_seq->record(rec);
    assert(extra != VK_NULL_HANDLE);
    MaestroVulkanSubmitDesc extra_desc = { .cmd = extra };

    /* pool is full of held cues: submit must fail */
    MaestroVulkanCue cue = g_seq->submit(rec, &extra_desc);
    assert(cue.index == UINT32_MAX);

    /* completing the work must not change that: held cues are never stolen */
    assert_harp(g_seq->conduct(g_seq), "conduct failed");
    for(u32 i = 0; i < 4; ++i)
        assert_harp(g_seq->cue_wait(g_seq, held[i], UINT64_MAX), "cue_wait failed");
    cue = g_seq->submit(rec, &extra_desc);
    assert(cue.index == UINT32_MAX);

    /* releasing one frees a slot, and the released handle reads as done */
    g_seq->cue_release(g_seq, held[0]);
    assert(g_seq->cue_done(g_seq, held[0]) == 1);
    cue = g_seq->submit(rec, &extra_desc);
    assert(cue.index != UINT32_MAX);

    /* no conduct here: cue_wait must flush the pending submission itself */
    assert_harp(g_seq->cue_wait(g_seq, cue, UINT64_MAX), "flush-on-wait failed");

    g_seq->cue_release(g_seq, cue);
    for(u32 i = 1; i < 4; ++i)
        g_seq->cue_release(g_seq, held[i]);
    assert_harp(g_seq->close_recorder(rec), "close_recorder failed");

    printf("    exhaustion: held cues survive, release frees, wait flushes\n");
    TEST_MARKER("CONDUCTOR", "EXHAUSTION_DONE");
}


/* ========================================================= */
/* TEARDOWN                                                   */
/* ========================================================= */

static void test_conductor_term(void) {
    if(!g_seq) return;

    TEST_MARKER("CONDUCTOR", "TERM");
    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_VULKAN_CONDUCTOR_HANDLER_NAME),
        "Failed to terminate conductor"
    );
    TEST_MARKER("CONDUCTOR", "TERM_DONE");
}

static void test_device_destroy(void) {
    if(!g_vk || !g_device_actor) return;

    TEST_MARKER("CONDUCTOR", "DEVICE_DESTROY");
    assert_harp(
        g_core->actor_destroy(g_core, MAESTRO_VULKAN_DEVICE_ACTOR_NAME, g_device_actor),
        "Failed to destroy device actor"
    );
    g_device_actor = NULL;
    TEST_MARKER("CONDUCTOR", "DEVICE_DESTROY_DONE");
}

static void test_vulkan_core_term(void) {
    if(!g_vk) return;

    TEST_MARKER("VULKAN_CORE", "TERM");
    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_VULKAN_CORE_HANDLER_NAME),
        "Failed to terminate Vulkan core handler"
    );
    TEST_MARKER("VULKAN_CORE", "TERM_DONE");
}

static void test_logger_term(void) {
    TEST_MARKER("LOGGER", "TERM");
    assert_harp(g_core->handler_terminate(g_core, MAESTRO_LOGGER_HANDLER_NAME), "Failed to terminate logger");
    TEST_MARKER("LOGGER", "TERM_DONE");
}

static void test_runtime_term(void) {
    TEST_MARKER("RUNTIME", "TERM");
    assert_harp(harp_terminate(g_runtime), "Failed to terminate runtime");
    TEST_MARKER("RUNTIME", "TERM_DONE");
}


/* ========================================================= */
/* MAIN                                                        */
/* ========================================================= */

int main(int argc, char **argv) {
    HARP_UNUSED(argc);
    printf("=== HARP / MAESTRO CONDUCTOR TEST ===\n");

    test_runtime_init(argv[0]);
    test_maestro_register();
    test_logger_init();

    test_vulkan_core_init();
    test_conductor_get_handler();
    test_device_create();

    test_conductor_single();
    test_conductor_dependency();
    test_conductor_stale_cue();
    test_conductor_exhaustion();

    test_conductor_term();
    test_device_destroy();
    test_vulkan_core_term();
    test_logger_term();
    test_runtime_term();

    printf("ALL TESTS PASSED\n");
    return 0;
}
