#include <harp/harp_api.h>

#include <maestro/maestro.h>
#include <maestro/maestro_logger.h>
#include <maestro/maestro_window.h>
#include <maestro/maestro_vulkan.h>

#include <harp/utils/harp_helpers.h>
#include <harp/utils/harp_version.h>

#include <impl/maestro_vulkan.h>

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

static HarpRuntime             *g_runtime = NULL;
static HarpCoreHandler         *g_core    = NULL;
static MaestroLoggerHandler    *g_logger  = NULL;
static MaestroWindowHandler    *g_window  = NULL;
static MaestroVulkanCoreHandler *g_vk     = NULL;


/* ========================================================= */
/* SETUP                                                      */
/* ========================================================= */

static void test_runtime_init(char *argv0) {
    TEST_MARKER("RUNTIME", "INIT");

    HarpRuntimeDesc desc = { .executable_path = argv0 };

    assert_harp(harp_initialize(&desc, &g_runtime), "Failed to init Harp runtime");

    HarpDependencyDesc dep = {
        .name        = HARP_CORE_HANDLER_NAME,
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


/* ========================================================= */
/* LOGGER + WINDOW INIT                                       */
/* ========================================================= */

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

static void test_window_init(void) {
    TEST_MARKER("WINDOW", "INIT");

    HarpDependencyDesc dep = HARP_DEPENDENCY(MAESTRO_WINDOW_HANDLER_NAME, 0, UINT32_MAX);
    HarpHandlerBase *base = NULL;

    assert_harp(g_core->get_handler(g_core, &dep, &base), "Failed to get window");
    g_window = (MaestroWindowHandler *)base;
    assert(g_window->get_vulkan_extensions != NULL);

    HarpCreatorBase creator = { .flags = HARP_CREATOR_FLAG_DEFAULT_CREATOR };
    assert_harp(
        g_core->handler_initialize(g_core, MAESTRO_WINDOW_HANDLER_NAME, &creator),
        "Failed to initialize window"
    );

    TEST_MARKER("WINDOW", "INIT_DONE");
}


/* ========================================================= */
/* WINDOW — GET VULKAN EXTENSIONS                             */
/* ========================================================= */

static u32     g_ext_count  = 0;
static const char **g_extensions = NULL;

static void test_window_get_vulkan_extensions(void) {
    TEST_MARKER("WINDOW", "GET_VULKAN_EXTENSIONS");

    g_window->get_vulkan_extensions(g_window, &g_ext_count, NULL);
    assert(g_ext_count > 0);
    printf("    window requires %u Vulkan extension(s)\n", g_ext_count);

    g_extensions = malloc(g_ext_count * sizeof(const char *));
    assert(g_extensions != NULL);
    g_window->get_vulkan_extensions(g_window, &g_ext_count, g_extensions);

    for(u32 i = 0; i < g_ext_count; ++i) {
        assert(g_extensions[i] != NULL);
        assert(g_extensions[i][0] != '\0');
        printf("    ext[%u] = %s\n", i, g_extensions[i]);
    }

    TEST_MARKER("WINDOW", "GET_VULKAN_EXTENSIONS_DONE");
}


/* ========================================================= */
/* VULKAN CORE HANDLER                                        */
/* ========================================================= */

static void test_vulkan_core_get_handler(void) {
    TEST_MARKER("VULKAN_CORE", "GET_HANDLER");

    HarpDependencyDesc dep = HARP_DEPENDENCY(MAESTRO_VULKAN_CORE_HANDLER_NAME, 0, UINT32_MAX);
    HarpHandlerBase *base = NULL;

    assert_harp(g_core->get_handler(g_core, &dep, &base), "Failed to get Vulkan core handler");

    g_vk = (MaestroVulkanCoreHandler *)base;
    assert(g_vk != NULL);
    assert(!(base->status & HARP_STATUS_FLAG_VALID));
    assert(g_vk->create_buffer  != NULL);
    assert(g_vk->destroy_buffer != NULL);

    TEST_MARKER("VULKAN_CORE", "GET_HANDLER_DONE");
}

static void test_vulkan_core_init(void) {
    TEST_MARKER("VULKAN_CORE", "INIT");

    MaestroVulkanCoreCreator creator = {
        ._base            = { .kind = 0, .flags = 0 },
        .app_name         = "MaestroVulkanTest",
        .app_version      = HARP_MAKE_VERSION(1, 0, 0),
        .extensions       = g_extensions,
        .extension_count  = g_ext_count,
        .enable_validation = 1
    };

    HarpResult res = g_core->handler_initialize(
        g_core,
        MAESTRO_VULKAN_CORE_HANDLER_NAME,
        (const HarpCreatorBase *)&creator
    );

    if(res != HARP_RESULT_OK) {
        printf("    [SKIP] Vulkan not available (code=%d)\n", res);
        free(g_extensions);
        g_extensions = NULL;
        g_vk         = NULL;
        return;
    }

    HarpHandlerBase *base = (HarpHandlerBase *)g_vk;
    assert(base->status & HARP_STATUS_FLAG_VALID);
    assert(g_vk->instance != VK_NULL_HANDLE);
    assert(g_vk->pfn_default_device_score != NULL);

    MaestroVulkanCoreHandlerImpl *impl = HARP_HANDLER_AS(MaestroVulkanCoreHandlerImpl, base);
    printf("    physical device count: %u\n", impl->device_count);
    assert(impl->device_count > 0);

    TEST_MARKER("VULKAN_CORE", "INIT_DONE");
}


/* ========================================================= */
/* VULKAN DEVICE ACTOR                                        */
/* ========================================================= */

static HarpActorBase *g_device_actor = NULL;

static void test_vulkan_device_create_default(void) {
    if(!g_vk) return;

    TEST_MARKER("VULKAN_DEVICE", "CREATE_DEFAULT");

    MaestroVulkanCoreHandlerImpl *core_impl =
        HARP_HANDLER_AS(MaestroVulkanCoreHandlerImpl, (HarpHandlerBase *)g_vk);

    u32 count_before = core_impl->device_count;

    HarpCreatorBase creator = { .flags = HARP_CREATOR_FLAG_DEFAULT_CREATOR };

    assert_harp(
        g_core->actor_create(g_core, MAESTRO_VULKAN_DEVICE_ACTOR_NAME, &creator, &g_device_actor),
        "Failed to create Vulkan device actor"
    );

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);

    assert(actor->physical_device != VK_NULL_HANDLE);
    assert(actor->device          != VK_NULL_HANDLE);
    assert(actor->queue_count     >  0);
    assert(core_impl->device_count == count_before - 1);

    printf("    instance pool: %u -> %u\n", count_before, core_impl->device_count);
    printf("    queues created: %u\n", actor->queue_count);

    for(u32 i = 0; i < actor->queue_count; ++i) {
        MaestroVulkanQueue *q = &actor->queues[i];
        printf("    queues[%u]: family=%u flags=0x%x present=%u\n",
               i, q->family, q->flags, q->supports_present);
        assert(q->queue != VK_NULL_HANDLE);
    }

    /* First queue must be a graphics queue. */
    assert(actor->queues[0].flags & VK_QUEUE_GRAPHICS_BIT);

    TEST_MARKER("VULKAN_DEVICE", "CREATE_DEFAULT_DONE");
}

static void test_vulkan_device_create_with_features(void) {
    if(!g_vk) return;

    MaestroVulkanCoreHandlerImpl *core_impl =
        HARP_HANDLER_AS(MaestroVulkanCoreHandlerImpl, (HarpHandlerBase *)g_vk);

    if(core_impl->device_count == 0) {
        printf("    [SKIP] No remaining devices\n");
        return;
    }

    TEST_MARKER("VULKAN_DEVICE", "CREATE_WITH_FEATURES");

    HarpActorBase *second_actor = NULL;

    /* Request sampler anisotropy as a concrete feature. */
    VkPhysicalDeviceFeatures base_features = { .samplerAnisotropy = VK_TRUE };
    VkPhysicalDeviceFeatures2 features2 = {
        .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext    = NULL,
        .features = base_features
    };

    MaestroVulkanDeviceCreator creator = {
        ._base           = { .kind = 0, .flags = 0 },
        .pfn_score       = NULL,
        .features        = &features2,
        .extensions      = NULL,
        .extension_count = 0,
        .surface         = VK_NULL_HANDLE
    };

    assert_harp(
        g_core->actor_create(
            g_core, MAESTRO_VULKAN_DEVICE_ACTOR_NAME,
            (const HarpCreatorBase *)&creator, &second_actor
        ),
        "Failed to create device with features"
    );

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, second_actor);
    assert(actor->device != VK_NULL_HANDLE);
    assert(actor->queue_count > 0);

    printf("    created device with samplerAnisotropy, queues: %u\n", actor->queue_count);
    for(u32 i = 0; i < actor->queue_count; ++i) {
        MaestroVulkanQueue *q = &actor->queues[i];
        printf("    queues[%u]: family=%u flags=0x%x present=%u\n",
               i, q->family, q->flags, q->supports_present);
    }

    assert_harp(
        g_core->actor_destroy(g_core, MAESTRO_VULKAN_DEVICE_ACTOR_NAME, second_actor),
        "Failed to destroy second device actor"
    );

    assert(core_impl->device_count > 0);

    TEST_MARKER("VULKAN_DEVICE", "CREATE_WITH_FEATURES_DONE");
}

static i32 always_zero_score(VkPhysicalDevice device) {
    (void)device;
    return 0;
}

static void test_vulkan_device_create_no_suitable(void) {
    if(!g_vk) return;

    TEST_MARKER("VULKAN_DEVICE", "CREATE_NO_SUITABLE");

    HarpActorBase *actor = NULL;
    MaestroVulkanDeviceCreator creator = {
        ._base     = { .kind = 0, .flags = 0 },
        .pfn_score = always_zero_score
    };

    HarpResult res = g_core->actor_create(
        g_core, MAESTRO_VULKAN_DEVICE_ACTOR_NAME,
        (const HarpCreatorBase *)&creator, &actor
    );

    assert(res != HARP_RESULT_OK);
    assert(actor == NULL);

    TEST_MARKER("VULKAN_DEVICE", "CREATE_NO_SUITABLE_DONE");
}

static void test_vulkan_device_create_buffer(void) {
    if(!g_vk || !g_device_actor) return;

    TEST_MARKER("VULKAN_DEVICE", "CREATE_BUFFER");

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);

    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;

    assert_harp(
        g_vk->create_buffer(
            actor,
            256,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &buffer, &memory
        ),
        "Failed to create staging buffer"
    );

    assert(buffer != VK_NULL_HANDLE);
    assert(memory != VK_NULL_HANDLE);

    /* Map, write, unmap. */
    void *mapped = NULL;
    assert(vkMapMemory(actor->device, memory, 0, 256, 0, &mapped) == VK_SUCCESS);
    assert(mapped != NULL);
    ((u8 *)mapped)[0] = 0xAB;
    vkUnmapMemory(actor->device, memory);

    g_vk->destroy_buffer(actor, buffer, memory);
    printf("    256-byte staging buffer created and destroyed\n");

    TEST_MARKER("VULKAN_DEVICE", "CREATE_BUFFER_DONE");
}

static void test_vulkan_device_destroy(void) {
    if(!g_vk || !g_device_actor) return;

    TEST_MARKER("VULKAN_DEVICE", "DESTROY");

    MaestroVulkanCoreHandlerImpl *core_impl =
        HARP_HANDLER_AS(MaestroVulkanCoreHandlerImpl, (HarpHandlerBase *)g_vk);

    u32 count_before = core_impl->device_count;

    assert_harp(
        g_core->actor_destroy(g_core, MAESTRO_VULKAN_DEVICE_ACTOR_NAME, g_device_actor),
        "Failed to destroy device actor"
    );

    assert(core_impl->device_count == count_before + 1);
    printf("    instance pool: %u -> %u\n", count_before, core_impl->device_count);

    g_device_actor = NULL;

    TEST_MARKER("VULKAN_DEVICE", "DESTROY_DONE");
}


/* ========================================================= */
/* TEARDOWN                                                   */
/* ========================================================= */

static void test_vulkan_core_term(void) {
    if(!g_vk) return;

    TEST_MARKER("VULKAN_CORE", "TERM");

    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_VULKAN_CORE_HANDLER_NAME),
        "Failed to terminate Vulkan core handler"
    );

    HarpHandlerBase *base = (HarpHandlerBase *)g_vk;
    assert(!(base->status & HARP_STATUS_FLAG_VALID));
    assert(g_vk->instance == VK_NULL_HANDLE);

    TEST_MARKER("VULKAN_CORE", "TERM_DONE");
}

static void test_window_term(void) {
    TEST_MARKER("WINDOW", "TERM");
    assert_harp(g_core->handler_terminate(g_core, MAESTRO_WINDOW_HANDLER_NAME), "Failed to terminate window");
    TEST_MARKER("WINDOW", "TERM_DONE");
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
    printf("=== HARP / MAESTRO VULKAN TEST ===\n");

    test_runtime_init(argv[0]);
    test_maestro_register();

    test_logger_init();
    test_window_init();
    test_window_get_vulkan_extensions();

    test_vulkan_core_get_handler();
    test_vulkan_core_init();

    test_vulkan_device_create_default();
    test_vulkan_device_create_with_features();
    test_vulkan_device_create_no_suitable();
    test_vulkan_device_create_buffer();
    test_vulkan_device_destroy();

    test_vulkan_core_term();
    test_window_term();
    test_logger_term();

    free(g_extensions);

    test_runtime_term();

    printf("ALL TESTS PASSED\n");
    return 0;
}
