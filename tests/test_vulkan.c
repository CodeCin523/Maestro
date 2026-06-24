#include <harp/harp_core.h>

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

static HarpRuntime                  *g_runtime  = NULL;
static HarpCoreHandler              *g_core     = NULL;
static MaestroLoggerHandler         *g_logger   = NULL;
static MaestroWindowHandler         *g_window   = NULL;
static MaestroVulkanInstanceHandler *g_instance = NULL;


/* ========================================================= */
/* SETUP                                                      */
/* ========================================================= */

static void test_runtime_init(char *argv0) {
    TEST_MARKER("RUNTIME", "INIT");

    HarpRuntimeCreator creator = { .argv0 = argv0 };

    assert_harp(
        harp_initialize((const HarpCreatorBase *)&creator, &g_runtime),
        "Failed to init Harp runtime"
    );

    HarpDependencyDesc dep = {
        .name        = HARP_CORE_HANDLER_NAME,
        .min_version = 0,
        .max_version = UINT32_MAX
    };

    HarpHandlerBase *core_base = NULL;

    assert_harp(
        harp_runtime_get_handler(g_runtime, &dep, &core_base),
        "Failed to get core handler"
    );

    g_core = (HarpCoreHandler *)core_base;

    TEST_MARKER("RUNTIME", "INIT_DONE");
}

extern HarpResult maestro_register(HarpCoreHandler *);

static void test_maestro_register(void) {
    TEST_MARKER("MAESTRO", "REGISTER");

    assert_harp(
        maestro_register(g_core),
        "Failed to register Maestro package"
    );

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

static uint32_t      g_ext_count      = 0;
static const char  **g_extensions     = NULL;

static void test_window_get_vulkan_extensions(void) {
    TEST_MARKER("WINDOW", "GET_VULKAN_EXTENSIONS");

    /* first call: count only */
    g_window->get_vulkan_extensions(g_window, &g_ext_count, NULL);

    assert(g_ext_count > 0);
    printf("    window requires %u Vulkan extension(s)\n", g_ext_count);

    /* second call: fill */
    g_extensions = malloc(g_ext_count * sizeof(const char *));
    assert(g_extensions != NULL);

    g_window->get_vulkan_extensions(g_window, &g_ext_count, g_extensions);

    for(uint32_t i = 0; i < g_ext_count; ++i) {
        assert(g_extensions[i] != NULL);
        assert(g_extensions[i][0] != '\0');
        printf("    ext[%u] = %s\n", i, g_extensions[i]);
    }

    TEST_MARKER("WINDOW", "GET_VULKAN_EXTENSIONS_DONE");
}


/* ========================================================= */
/* VULKAN INSTANCE                                            */
/* ========================================================= */

static void test_vulkan_instance_get_handler(void) {
    TEST_MARKER("VULKAN_INSTANCE", "GET_HANDLER");

    HarpDependencyDesc dep = HARP_DEPENDENCY(MAESTRO_VULKAN_INSTANCE_HANDLER_NAME, 0, UINT32_MAX);
    HarpHandlerBase *base = NULL;

    assert_harp(
        g_core->get_handler(g_core, &dep, &base),
        "Failed to get Vulkan instance handler"
    );

    g_instance = (MaestroVulkanInstanceHandler *)base;

    assert(g_instance != NULL);
    assert(!(base->status & HARP_STATUS_FLAG_VALID));

    TEST_MARKER("VULKAN_INSTANCE", "GET_HANDLER_DONE");
}

static void test_vulkan_instance_init(void) {
    TEST_MARKER("VULKAN_INSTANCE", "INIT");

    MaestroVulkanInstanceCreator creator = {
        ._base            = { .kind = 0, .flags = 0 },
        .app_name         = "MaestroVulkanTest",
        .app_version      = HARP_MAKE_VERSION(1, 0, 0),
        .extensions       = g_extensions,
        .extension_count  = g_ext_count,
        .enable_validation = 1
    };

    HarpResult res = g_core->handler_initialize(
        g_core,
        MAESTRO_VULKAN_INSTANCE_HANDLER_NAME,
        (const HarpCreatorBase *)&creator
    );

    if(res != HARP_RESULT_OK) {
        printf("    [SKIP] No Vulkan-capable GPU or Vulkan not available (code=%d)\n", res);
        free(g_extensions);
        g_extensions = NULL;
        g_instance   = NULL;
        return;
    }

    HarpHandlerBase *base = (HarpHandlerBase *)g_instance;

    assert(base->status & HARP_STATUS_FLAG_VALID);
    assert(g_instance->instance != VK_NULL_HANDLE);
    assert(g_instance->pfn_default_device_score != NULL);

    MaestroVulkanInstanceHandlerImpl *impl = HARP_HANDLER_AS(MaestroVulkanInstanceHandlerImpl, base);
    printf("    physical device count: %u\n", impl->device_count);
    assert(impl->device_count > 0);

    TEST_MARKER("VULKAN_INSTANCE", "INIT_DONE");
}


/* ========================================================= */
/* VULKAN DEVICE ACTOR                                        */
/* ========================================================= */

static HarpActorBase *g_device_actor = NULL;

static void test_vulkan_device_create_default(void) {
    if(!g_instance) return;

    TEST_MARKER("VULKAN_DEVICE", "CREATE_DEFAULT");

    MaestroVulkanInstanceHandlerImpl *inst_impl =
        HARP_HANDLER_AS(MaestroVulkanInstanceHandlerImpl, (HarpHandlerBase *)g_instance);

    uint32_t count_before = inst_impl->device_count;

    HarpCreatorBase creator = { .flags = HARP_CREATOR_FLAG_DEFAULT_CREATOR };

    assert_harp(
        g_core->actor_create(g_core, MAESTRO_VULKAN_DEVICE_ACTOR_NAME, &creator, &g_device_actor),
        "Failed to create Vulkan device actor"
    );

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);

    assert(actor->physical_device != VK_NULL_HANDLE);
    assert(actor->device          != VK_NULL_HANDLE);

    /* device should have been removed from the instance pool */
    assert(inst_impl->device_count == count_before - 1);
    printf("    instance pool: %u -> %u\n", count_before, inst_impl->device_count);

    TEST_MARKER("VULKAN_DEVICE", "CREATE_DEFAULT_DONE");
}

static void test_vulkan_device_create_with_queues(void) {
    if(!g_instance) return;

    TEST_MARKER("VULKAN_DEVICE", "CREATE_WITH_QUEUES");

    MaestroVulkanInstanceHandlerImpl *inst_impl =
        HARP_HANDLER_AS(MaestroVulkanInstanceHandlerImpl, (HarpHandlerBase *)g_instance);

    if(inst_impl->device_count == 0) {
        printf("    [SKIP] No remaining devices to claim\n");
        TEST_MARKER("VULKAN_DEVICE", "CREATE_WITH_QUEUES_DONE");
        return;
    }

    HarpActorBase *second_actor = NULL;

    MaestroVulkanDeviceCreator creator = {
        ._base            = { .kind = 0, .flags = 0 },
        .pfn_score        = NULL,
        .request_compute  = 1,
        .request_transfer = 1
    };

    assert_harp(
        g_core->actor_create(
            g_core,
            MAESTRO_VULKAN_DEVICE_ACTOR_NAME,
            (const HarpCreatorBase *)&creator,
            &second_actor
        ),
        "Failed to create second Vulkan device actor with queues"
    );

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, second_actor);
    MaestroVulkanDeviceActorImpl *impl = HARP_ACTOR_AS(MaestroVulkanDeviceActorImpl, second_actor);

    assert(actor->physical_device != VK_NULL_HANDLE);
    assert(actor->device          != VK_NULL_HANDLE);

    printf("    graphics family: %u\n", impl->graphics_family);
    printf("    compute  family: %u\n", impl->compute_family);
    printf("    transfer family: %u\n", impl->transfer_family);

    assert_harp(
        g_core->actor_destroy(g_core, MAESTRO_VULKAN_DEVICE_ACTOR_NAME, second_actor),
        "Failed to destroy second device actor"
    );

    /* physical device should be returned to the pool */
    assert(inst_impl->device_count > 0);

    TEST_MARKER("VULKAN_DEVICE", "CREATE_WITH_QUEUES_DONE");
}

static int32_t always_zero_score(VkPhysicalDevice device) {
    (void)device;
    return 0;
}

static void test_vulkan_device_create_no_suitable(void) {
    if(!g_instance) return;

    TEST_MARKER("VULKAN_DEVICE", "CREATE_NO_SUITABLE");

    HarpActorBase *actor = NULL;

    MaestroVulkanDeviceCreator creator = {
        ._base     = { .kind = 0, .flags = 0 },
        .pfn_score = always_zero_score,
    };

    HarpResult res = g_core->actor_create(
        g_core,
        MAESTRO_VULKAN_DEVICE_ACTOR_NAME,
        (const HarpCreatorBase *)&creator,
        &actor
    );

    /* must fail — no device will score above 0 */
    assert(res != HARP_RESULT_OK);
    assert(actor == NULL);

    TEST_MARKER("VULKAN_DEVICE", "CREATE_NO_SUITABLE_DONE");
}

static void test_vulkan_device_destroy(void) {
    if(!g_instance || !g_device_actor) return;

    TEST_MARKER("VULKAN_DEVICE", "DESTROY");

    MaestroVulkanInstanceHandlerImpl *inst_impl =
        HARP_HANDLER_AS(MaestroVulkanInstanceHandlerImpl, (HarpHandlerBase *)g_instance);

    uint32_t count_before = inst_impl->device_count;

    assert_harp(
        g_core->actor_destroy(g_core, MAESTRO_VULKAN_DEVICE_ACTOR_NAME, g_device_actor),
        "Failed to destroy device actor"
    );

    /* physical device should be returned to the pool */
    assert(inst_impl->device_count == count_before + 1);
    printf("    instance pool: %u -> %u\n", count_before, inst_impl->device_count);

    g_device_actor = NULL;

    TEST_MARKER("VULKAN_DEVICE", "DESTROY_DONE");
}


/* ========================================================= */
/* TEARDOWN                                                   */
/* ========================================================= */

static void test_vulkan_instance_term(void) {
    if(!g_instance) return;

    TEST_MARKER("VULKAN_INSTANCE", "TERM");

    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_VULKAN_INSTANCE_HANDLER_NAME),
        "Failed to terminate Vulkan instance handler"
    );

    HarpHandlerBase *base = (HarpHandlerBase *)g_instance;
    assert(!(base->status & HARP_STATUS_FLAG_VALID));
    assert(g_instance->instance == VK_NULL_HANDLE);

    TEST_MARKER("VULKAN_INSTANCE", "TERM_DONE");
}

static void test_window_term(void) {
    TEST_MARKER("WINDOW", "TERM");

    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_WINDOW_HANDLER_NAME),
        "Failed to terminate window"
    );

    TEST_MARKER("WINDOW", "TERM_DONE");
}

static void test_logger_term(void) {
    TEST_MARKER("LOGGER", "TERM");

    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_LOGGER_HANDLER_NAME),
        "Failed to terminate logger"
    );

    TEST_MARKER("LOGGER", "TERM_DONE");
}

static void test_runtime_term(void) {
    TEST_MARKER("RUNTIME", "TERM");

    assert_harp(harp_terminate(g_runtime), "Failed to terminate runtime");

    TEST_MARKER("RUNTIME", "TERM_DONE");
}


/* ========================================================= */
/* MAIN                                                       */
/* ========================================================= */

int main(int argc, char **argv) {
    printf("=== HARP / MAESTRO VULKAN TEST ===\n");

    test_runtime_init(argv[0]);
    test_maestro_register();

    test_logger_init();
    test_window_init();

    test_window_get_vulkan_extensions();

    test_vulkan_instance_get_handler();
    test_vulkan_instance_init();

    test_vulkan_device_create_default();
    test_vulkan_device_create_with_queues();
    test_vulkan_device_create_no_suitable();
    test_vulkan_device_destroy();

    test_vulkan_instance_term();
    test_window_term();
    test_logger_term();

    free(g_extensions);

    test_runtime_term();

    printf("ALL TESTS PASSED\n");
    return 0;
}
