#include <harp/harp_api.h>

#include <maestro/maestro.h>
#include <maestro/maestro_logger.h>
#include <maestro/maestro_window.h>
#include <maestro/maestro_vulkan.h>

#include <harp/utils/harp_helpers.h>
#include <harp/utils/harp_version.h>

#include <impl/maestro_vulkan.h>

#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#if defined(_WIN32)
#include <windows.h>
#define test_sleep_ms(ms) Sleep(ms)
static f64 test_wall_seconds(void) {
    static LARGE_INTEGER freq = {0};
    if(freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (f64)now.QuadPart / (f64)freq.QuadPart;
}
#else
#include <unistd.h>
#include <time.h>
#define test_sleep_ms(ms) usleep((ms) * 1000)
static f64 test_wall_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (f64)ts.tv_sec + (f64)ts.tv_nsec / 1e9;
}
#endif


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

static HarpRuntime              *g_runtime      = NULL;
static HarpCoreHandler          *g_core         = NULL;
static MaestroLoggerHandler     *g_logger       = NULL;
static MaestroWindowHandler     *g_window       = NULL;
static MaestroVulkanCoreHandler *g_vk           = NULL;
static VkSurfaceKHR              g_surface      = VK_NULL_HANDLE;
static HarpActorBase            *g_device_actor = NULL;

static u32     g_ext_count  = 0;
static const char **g_extensions = NULL;


/* ========================================================= */
/* RUNTIME                                                    */
/* ========================================================= */

static void test_runtime_init(char *argv0) {
    TEST_MARKER("RUNTIME", "INIT");

    HarpRuntimeDesc desc = { .executable_path = argv0 };
    assert_harp(harp_initialize(&desc, &g_runtime), "harp_initialize");

    HarpHandlerBase *core_base = NULL;
    assert_harp(
        harp_runtime_get_handler(g_runtime,
            &HARP_DEPENDENCY(HARP_CORE_HANDLER_NAME, 0, UINT32_MAX),
            &core_base),
        "get core handler"
    );
    g_core = (HarpCoreHandler *)core_base;

    TEST_MARKER("RUNTIME", "INIT_DONE");
}

extern HarpResult maestro_register(HarpCoreHandler *);

static void test_maestro_register(void) {
    TEST_MARKER("MAESTRO", "REGISTER");
    assert_harp(maestro_register(g_core), "maestro_register");
    TEST_MARKER("MAESTRO", "REGISTER_DONE");
}


/* ========================================================= */
/* LOGGER                                                     */
/* ========================================================= */

static void test_logger_init(void) {
    TEST_MARKER("LOGGER", "INIT");

    HarpHandlerBase *base = NULL;
    assert_harp(
        g_core->get_handler(g_core, &HARP_DEPENDENCY(MAESTRO_LOGGER_HANDLER_NAME, 0, UINT32_MAX), &base),
        "get logger handler"
    );
    g_logger = (MaestroLoggerHandler *)base;

    assert_harp(
        g_core->handler_initialize(g_core, MAESTRO_LOGGER_HANDLER_NAME, &HARP_DEFAULT_CREATOR),
        "initialize logger"
    );

    TEST_MARKER("LOGGER", "INIT_DONE");
}


/* ========================================================= */
/* WINDOW                                                     */
/* ========================================================= */

static void test_window_init(void) {
    TEST_MARKER("WINDOW", "INIT");

    HarpHandlerBase *base = NULL;
    assert_harp(
        g_core->get_handler(g_core, &HARP_DEPENDENCY(MAESTRO_WINDOW_HANDLER_NAME, 0, UINT32_MAX), &base),
        "get window handler"
    );
    g_window = (MaestroWindowHandler *)base;

    MaestroWindowCreator creator = {
        ._base  = { .kind = 0, .flags = 0 },
        .title  = "Maestro Surface Test",
        .width  = 1280,
        .height = 720
    };

    assert_harp(
        g_core->handler_initialize(g_core, MAESTRO_WINDOW_HANDLER_NAME, (const HarpCreatorBase *)&creator),
        "initialize window"
    );

    g_window->get_vulkan_extensions(g_window, &g_ext_count, NULL);
    assert(g_ext_count > 0);

    g_extensions = malloc(g_ext_count * sizeof(const char *));
    assert(g_extensions != NULL);
    g_window->get_vulkan_extensions(g_window, &g_ext_count, g_extensions);

    printf("    window Vulkan extensions (%u):\n", g_ext_count);
    for(u32 i = 0; i < g_ext_count; ++i)
        printf("        %s\n", g_extensions[i]);

    TEST_MARKER("WINDOW", "INIT_DONE");
}


/* ========================================================= */
/* VULKAN CORE                                                */
/* ========================================================= */

static void test_vulkan_core_init(void) {
    TEST_MARKER("VULKAN_CORE", "INIT");

    HarpHandlerBase *base = NULL;
    assert_harp(
        g_core->get_handler(g_core, &HARP_DEPENDENCY(MAESTRO_VULKAN_CORE_HANDLER_NAME, 0, UINT32_MAX), &base),
        "get Vulkan core handler"
    );
    g_vk = (MaestroVulkanCoreHandler *)base;

    MaestroVulkanCoreCreator creator = {
        ._base             = { .kind = 0, .flags = 0 },
        .app_name          = "MaestroSurfaceTest",
        .app_version       = HARP_MAKE_VERSION(1, 0, 0),
        .extensions        = g_extensions,
        .extension_count   = g_ext_count,
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
    printf("    VkInstance: %p\n", (void *)g_vk->instance);

    TEST_MARKER("VULKAN_CORE", "INIT_DONE");
}


/* ========================================================= */
/* SURFACE                                                    */
/* ========================================================= */

static void test_surface_create(void) {
    if(!g_vk) return;

    TEST_MARKER("SURFACE", "CREATE");

    assert(g_window->create_vulkan_surface != NULL);
    assert_harp(
        g_window->create_vulkan_surface(g_window, g_vk->instance, &g_surface),
        "create Vulkan surface"
    );

    assert(g_surface != VK_NULL_HANDLE);
    printf("    VkSurfaceKHR: %p\n", (void *)g_surface);

    TEST_MARKER("SURFACE", "CREATE_DONE");
}


/* ========================================================= */
/* DEVICE WITH SURFACE                                        */
/* ========================================================= */

static void test_device_create(void) {
    if(!g_vk || g_surface == VK_NULL_HANDLE) return;

    TEST_MARKER("DEVICE", "CREATE");

    MaestroVulkanDeviceCreator creator = {
        ._base           = { .kind = 0, .flags = 0 },
        .pfn_score       = NULL,
        .features        = NULL,
        .extensions      = NULL,
        .extension_count = 0,
        .surface         = g_surface
    };

    assert_harp(
        g_core->actor_create(
            g_core,
            MAESTRO_VULKAN_DEVICE_ACTOR_NAME,
            (const HarpCreatorBase *)&creator,
            &g_device_actor
        ),
        "create Vulkan device actor"
    );

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);

    assert(actor->physical_device != VK_NULL_HANDLE);
    assert(actor->device          != VK_NULL_HANDLE);
    assert(actor->queue_count     >  0);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(actor->physical_device, &props);

    printf("    GPU              : %s\n", props.deviceName);
    printf("    Vulkan API       : %u.%u.%u\n",
        VK_VERSION_MAJOR(props.apiVersion),
        VK_VERSION_MINOR(props.apiVersion),
        VK_VERSION_PATCH(props.apiVersion));
    printf("    queues created   : %u\n", actor->queue_count);

    b8 found_present = 0;
    for(u32 i = 0; i < actor->queue_count; ++i) {
        MaestroVulkanQueue *q = &actor->queues[i];
        printf("    queues[%u]: family=%u flags=0x%x present=%u\n",
               i, q->family, q->flags, q->supports_present);
        assert(q->queue != VK_NULL_HANDLE);
        if(q->supports_present) found_present = 1;
    }

    assert(found_present);

    TEST_MARKER("DEVICE", "CREATE_DONE");
}


/* ========================================================= */
/* SURFACE QUERY                                              */
/* ========================================================= */

static const char *present_mode_str(VkPresentModeKHR mode) {
    switch(mode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:    return "IMMEDIATE";
        case VK_PRESENT_MODE_MAILBOX_KHR:      return "MAILBOX";
        case VK_PRESENT_MODE_FIFO_KHR:         return "FIFO";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
        default:                               return "OTHER";
    }
}

static void test_surface_query(void) {
    if(!g_device_actor || g_surface == VK_NULL_HANDLE) return;

    TEST_MARKER("SURFACE", "QUERY");

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(actor->physical_device, g_surface, &caps);

    printf("    surface capabilities:\n");
    printf("        min/max image count : %u / %u\n", caps.minImageCount, caps.maxImageCount);
    printf("        current extent      : %ux%u\n",   caps.currentExtent.width, caps.currentExtent.height);
    printf("        min/max extent      : %ux%u / %ux%u\n",
        caps.minImageExtent.width, caps.minImageExtent.height,
        caps.maxImageExtent.width, caps.maxImageExtent.height);

    u32 format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(actor->physical_device, g_surface, &format_count, NULL);
    assert(format_count > 0);

    VkSurfaceFormatKHR *formats = malloc(format_count * sizeof(VkSurfaceFormatKHR));
    assert(formats != NULL);
    vkGetPhysicalDeviceSurfaceFormatsKHR(actor->physical_device, g_surface, &format_count, formats);

    printf("    surface formats (%u):\n", format_count);
    for(u32 i = 0; i < format_count; ++i)
        printf("        format=%-4u  colorspace=%u\n", formats[i].format, formats[i].colorSpace);
    free(formats);

    u32 mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(actor->physical_device, g_surface, &mode_count, NULL);
    assert(mode_count > 0);

    VkPresentModeKHR *modes = malloc(mode_count * sizeof(VkPresentModeKHR));
    assert(modes != NULL);
    vkGetPhysicalDeviceSurfacePresentModesKHR(actor->physical_device, g_surface, &mode_count, modes);

    printf("    present modes (%u):\n", mode_count);
    for(u32 i = 0; i < mode_count; ++i)
        printf("        %s\n", present_mode_str(modes[i]));
    free(modes);

    TEST_MARKER("SURFACE", "QUERY_DONE");
}


/* ========================================================= */
/* STAY VISIBLE                                               */
/* ========================================================= */

static void test_window_stay_visible(f64 seconds) {
    TEST_MARKER("WINDOW", "STAY_VISIBLE");
    printf("    window + Vulkan active for ~%.1f seconds...\n", seconds);

    const int frame_ms = 16;
    f64 start = test_wall_seconds();

    while((test_wall_seconds() - start) < seconds) {
        g_window->pump_messages(g_window);
        test_sleep_ms(frame_ms);
    }

    TEST_MARKER("WINDOW", "STAY_VISIBLE_DONE");
}


/* ========================================================= */
/* TEARDOWN                                                   */
/* ========================================================= */

static void test_device_destroy(void) {
    if(!g_device_actor) return;

    TEST_MARKER("DEVICE", "DESTROY");
    assert_harp(
        g_core->actor_destroy(g_core, MAESTRO_VULKAN_DEVICE_ACTOR_NAME, g_device_actor),
        "destroy device actor"
    );
    g_device_actor = NULL;
    TEST_MARKER("DEVICE", "DESTROY_DONE");
}

static void test_surface_destroy(void) {
    if(!g_vk || g_surface == VK_NULL_HANDLE) return;

    TEST_MARKER("SURFACE", "DESTROY");
    vkDestroySurfaceKHR(g_vk->instance, g_surface, NULL);
    g_surface = VK_NULL_HANDLE;
    TEST_MARKER("SURFACE", "DESTROY_DONE");
}

static void test_vulkan_core_term(void) {
    if(!g_vk) return;

    TEST_MARKER("VULKAN_CORE", "TERM");
    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_VULKAN_CORE_HANDLER_NAME),
        "terminate Vulkan core handler"
    );
    TEST_MARKER("VULKAN_CORE", "TERM_DONE");
}

static void test_window_term(void) {
    TEST_MARKER("WINDOW", "TERM");
    assert_harp(g_core->handler_terminate(g_core, MAESTRO_WINDOW_HANDLER_NAME), "terminate window");
    TEST_MARKER("WINDOW", "TERM_DONE");
}

static void test_logger_term(void) {
    TEST_MARKER("LOGGER", "TERM");
    assert_harp(g_core->handler_terminate(g_core, MAESTRO_LOGGER_HANDLER_NAME), "terminate logger");
    TEST_MARKER("LOGGER", "TERM_DONE");
}

static void test_runtime_term(void) {
    TEST_MARKER("RUNTIME", "TERM");
    assert_harp(harp_terminate(g_runtime), "harp_terminate");
    TEST_MARKER("RUNTIME", "TERM_DONE");
}


/* ========================================================= */
/* MAIN                                                       */
/* ========================================================= */

int main(int argc, char **argv) {
    (void)argc;
    printf("=== HARP / MAESTRO SURFACE TEST ===\n");

    test_runtime_init(argv[0]);
    test_maestro_register();

    test_logger_init();
    test_window_init();

    test_vulkan_core_init();
    test_surface_create();
    test_device_create();

    test_surface_query();
    test_window_stay_visible(3.0);

    test_device_destroy();
    test_surface_destroy();

    test_vulkan_core_term();
    test_window_term();
    test_logger_term();

    free(g_extensions);

    test_runtime_term();

    printf("ALL TESTS PASSED\n");
    return 0;
}
