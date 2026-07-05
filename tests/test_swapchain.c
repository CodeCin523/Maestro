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

static void assert_vk(VkResult r, const char *msg) {
    if(r != VK_SUCCESS) {
        printf("ASSERT_FAIL: %s (VkResult=%d)\n", msg, r);
        exit(EXIT_FAILURE);
    }
}


/* ========================================================= */
/* GLOBALS                                                    */
/* ========================================================= */

static HarpRuntime                   *g_runtime      = NULL;
static HarpCoreHandler               *g_core         = NULL;
static MaestroLoggerHandler          *g_logger       = NULL;
static MaestroWindowHandler          *g_window       = NULL;
static MaestroVulkanCoreHandler      *g_vk           = NULL;
static VkSurfaceKHR                   g_surface      = VK_NULL_HANDLE;
static HarpActorBase                 *g_device_actor = NULL;
static MaestroVulkanSwapchainHandler *g_swapchain    = NULL;

static uint32_t     g_ext_count  = 0;
static const char **g_extensions = NULL;

/* Frame objects shared by the acquire/present tests. */
static VkSemaphore   g_sem_acquire = VK_NULL_HANDLE;
static VkSemaphore   g_sem_render  = VK_NULL_HANDLE;
static VkFence       g_fence       = VK_NULL_HANDLE;
static VkCommandPool g_cmd_pool    = VK_NULL_HANDLE;


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
/* LOGGER + WINDOW                                            */
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
        .title  = "Maestro Swapchain Test",
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

    TEST_MARKER("WINDOW", "INIT_DONE");
}


/* ========================================================= */
/* VULKAN CORE + SURFACE + DEVICE                             */
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
        .app_name          = "MaestroSwapchainTest",
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

    TEST_MARKER("VULKAN_CORE", "INIT_DONE");
}

static void test_surface_create(void) {
    if(!g_vk) return;

    TEST_MARKER("SURFACE", "CREATE");

    assert_harp(
        g_window->create_vulkan_surface(g_window, g_vk->instance, &g_surface),
        "create Vulkan surface"
    );
    assert(g_surface != VK_NULL_HANDLE);

    TEST_MARKER("SURFACE", "CREATE_DONE");
}

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
    assert(actor->device != VK_NULL_HANDLE);
    assert(actor->queue_count > 0);
    assert(actor->queues[0].flags & VK_QUEUE_GRAPHICS_BIT);

    TEST_MARKER("DEVICE", "CREATE_DONE");
}


/* ========================================================= */
/* SWAPCHAIN GET HANDLER                                      */
/* ========================================================= */

static void test_swapchain_get_handler(void) {
    TEST_MARKER("SWAPCHAIN", "GET_HANDLER");

    HarpHandlerBase *base = NULL;
    assert_harp(
        g_core->get_handler(g_core, &HARP_DEPENDENCY(MAESTRO_VULKAN_SWAPCHAIN_HANDLER_NAME, 0, UINT32_MAX), &base),
        "get swapchain handler"
    );
    g_swapchain = (MaestroVulkanSwapchainHandler *)base;

    assert(!(base->status & HARP_STATUS_FLAG_VALID));
    assert(g_swapchain->acquire  != NULL);
    assert(g_swapchain->present  != NULL);
    assert(g_swapchain->recreate != NULL);

    /* Calls on an uninitialized handler must fail cleanly. */
    uint32_t image_index = 0;
    assert(g_swapchain->acquire(g_swapchain, VK_NULL_HANDLE, &image_index) == HARP_RESULT_INVALID_STATE);
    assert(g_swapchain->present(g_swapchain, VK_NULL_HANDLE, VK_NULL_HANDLE, 0) == HARP_RESULT_INVALID_STATE);
    assert(g_swapchain->recreate(g_swapchain, NULL, 0, 0, VK_PRESENT_MODE_FIFO_KHR) == HARP_RESULT_INVALID_STATE);

    TEST_MARKER("SWAPCHAIN", "GET_HANDLER_DONE");
}


/* ========================================================= */
/* SWAPCHAIN INIT                                             */
/* ========================================================= */

static void test_swapchain_init_default_rejected(void) {
    if(!g_vk || !g_device_actor) return;

    TEST_MARKER("SWAPCHAIN", "INIT_DEFAULT_REJECTED");

    /* The swapchain needs a device and surface; the default creator
       carries neither and must be rejected. */
    HarpResult res = g_core->handler_initialize(
        g_core,
        MAESTRO_VULKAN_SWAPCHAIN_HANDLER_NAME,
        &HARP_DEFAULT_CREATOR
    );

    assert(res != HARP_RESULT_OK);
    assert(!(((HarpHandlerBase *)g_swapchain)->status & HARP_STATUS_FLAG_VALID));

    TEST_MARKER("SWAPCHAIN", "INIT_DEFAULT_REJECTED_DONE");
}

static void test_swapchain_init(void) {
    if(!g_vk || !g_device_actor) return;

    TEST_MARKER("SWAPCHAIN", "INIT");

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);

    MaestroVulkanSwapchainCreator creator = {
        ._base                  = { .kind = 0, .flags = 0 },
        .device                 = actor,
        .surface                = g_surface,
        .width                  = 1280,
        .height                 = 720,
        .preferred_format       = VK_FORMAT_B8G8R8A8_SRGB,
        .preferred_present_mode = VK_PRESENT_MODE_MAILBOX_KHR
    };

    assert_harp(
        g_core->handler_initialize(
            g_core,
            MAESTRO_VULKAN_SWAPCHAIN_HANDLER_NAME,
            (const HarpCreatorBase *)&creator
        ),
        "initialize swapchain"
    );

    HarpHandlerBase *base = (HarpHandlerBase *)g_swapchain;
    assert(base->status & HARP_STATUS_FLAG_VALID);

    assert(g_swapchain->swapchain != VK_NULL_HANDLE);
    assert(g_swapchain->image_count > 0);
    assert(g_swapchain->images != NULL);
    assert(g_swapchain->views  != NULL);
    assert(g_swapchain->format != VK_FORMAT_UNDEFINED);
    assert(g_swapchain->extent.width  > 0);
    assert(g_swapchain->extent.height > 0);

    for(uint32_t i = 0; i < g_swapchain->image_count; ++i) {
        assert(g_swapchain->images[i] != VK_NULL_HANDLE);
        assert(g_swapchain->views[i]  != VK_NULL_HANDLE);
    }

    /* Preferred mode or the guaranteed FIFO fallback. */
    assert(g_swapchain->present_mode == VK_PRESENT_MODE_MAILBOX_KHR ||
           g_swapchain->present_mode == VK_PRESENT_MODE_FIFO_KHR);

    MaestroVulkanSwapchainHandlerImpl *impl = HARP_HANDLER_AS(MaestroVulkanSwapchainHandlerImpl, base);
    assert(impl->device          == actor->device);
    assert(impl->physical_device == actor->physical_device);
    assert(impl->surface         == g_surface);

    printf("    swapchain: %ux%u  fmt=%u  mode=%u  images=%u\n",
        g_swapchain->extent.width, g_swapchain->extent.height,
        g_swapchain->format, g_swapchain->present_mode, g_swapchain->image_count);

    TEST_MARKER("SWAPCHAIN", "INIT_DONE");
}


/* ========================================================= */
/* SWAPCHAIN ACQUIRE AND PRESENT                              */
/* ========================================================= */

static void frame_objects_create(void) {
    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);

    VkSemaphoreCreateInfo sem_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    assert_vk(vkCreateSemaphore(actor->device, &sem_info, NULL, &g_sem_acquire), "create acquire semaphore");
    assert_vk(vkCreateSemaphore(actor->device, &sem_info, NULL, &g_sem_render),  "create render semaphore");

    VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    assert_vk(vkCreateFence(actor->device, &fence_info, NULL, &g_fence), "create fence");

    VkCommandPoolCreateInfo pool_info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = actor->queues[0].family
    };
    assert_vk(vkCreateCommandPool(actor->device, &pool_info, NULL, &g_cmd_pool), "create command pool");
}

static void frame_objects_destroy(void) {
    if(!g_device_actor) return;

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);
    vkDeviceWaitIdle(actor->device);

    if(g_cmd_pool    != VK_NULL_HANDLE) vkDestroyCommandPool(actor->device, g_cmd_pool, NULL);
    if(g_fence       != VK_NULL_HANDLE) vkDestroyFence(actor->device, g_fence, NULL);
    if(g_sem_render  != VK_NULL_HANDLE) vkDestroySemaphore(actor->device, g_sem_render, NULL);
    if(g_sem_acquire != VK_NULL_HANDLE) vkDestroySemaphore(actor->device, g_sem_acquire, NULL);

    g_cmd_pool    = VK_NULL_HANDLE;
    g_fence       = VK_NULL_HANDLE;
    g_sem_render  = VK_NULL_HANDLE;
    g_sem_acquire = VK_NULL_HANDLE;
}

/* Acquire an image, transition it to PRESENT_SRC via a one-shot command
   buffer, and present it. This is a minimal but valid full frame. */
static void test_swapchain_frame(const char *phase) {
    if(!g_vk || !g_device_actor) return;
    if(!(((HarpHandlerBase *)g_swapchain)->status & HARP_STATUS_FLAG_VALID)) return;

    TEST_MARKER("SWAPCHAIN", phase);

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);

    VkQueue present_queue = VK_NULL_HANDLE;
    for(uint32_t i = 0; i < actor->queue_count; ++i) {
        if(actor->queues[i].supports_present) {
            present_queue = actor->queues[i].queue;
            break;
        }
    }
    assert(present_queue != VK_NULL_HANDLE);

    uint32_t image_index = UINT32_MAX;
    assert_harp(
        g_swapchain->acquire(g_swapchain, g_sem_acquire, &image_index),
        "acquire swapchain image"
    );
    assert(image_index < g_swapchain->image_count);
    printf("    acquired image %u / %u\n", image_index, g_swapchain->image_count);

    VkCommandBufferAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = g_cmd_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    assert_vk(vkAllocateCommandBuffers(actor->device, &alloc_info, &cmd), "allocate command buffer");

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    assert_vk(vkBeginCommandBuffer(cmd, &begin_info), "begin command buffer");

    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = 0,
        .dstAccessMask       = 0,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = g_swapchain->images[image_index],
        .subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        }
    };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    assert_vk(vkEndCommandBuffer(cmd), "end command buffer");

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkSubmitInfo submit_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &g_sem_acquire,
        .pWaitDstStageMask    = &wait_stage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &g_sem_render
    };
    assert_vk(vkQueueSubmit(actor->queues[0].queue, 1, &submit_info, g_fence), "submit transition");

    assert_harp(
        g_swapchain->present(g_swapchain, present_queue, g_sem_render, image_index),
        "present swapchain image"
    );

    assert_vk(vkWaitForFences(actor->device, 1, &g_fence, VK_TRUE, UINT64_MAX), "wait fence");
    assert_vk(vkResetFences(actor->device, 1, &g_fence), "reset fence");
    vkDeviceWaitIdle(actor->device);
    vkFreeCommandBuffers(actor->device, g_cmd_pool, 1, &cmd);

    printf("    presented image %u\n", image_index);

    TEST_MARKER("SWAPCHAIN", phase);
}


/* ========================================================= */
/* SWAPCHAIN RECREATE                                         */
/* ========================================================= */

static void test_swapchain_recreate(void) {
    if(!g_vk || !g_device_actor) return;
    if(!(((HarpHandlerBase *)g_swapchain)->status & HARP_STATUS_FLAG_VALID)) return;

    TEST_MARKER("SWAPCHAIN", "RECREATE");

    MaestroVulkanDeviceActor *actor = HARP_ACTOR_AS(MaestroVulkanDeviceActor, g_device_actor);

    VkSwapchainKHR old_swapchain = g_swapchain->swapchain;
    VkFormat       old_format    = g_swapchain->format;

    assert_harp(
        g_swapchain->recreate(g_swapchain, actor, 800, 600, VK_PRESENT_MODE_FIFO_KHR),
        "recreate swapchain"
    );

    assert(g_swapchain->swapchain != VK_NULL_HANDLE);
    assert(g_swapchain->swapchain != old_swapchain);
    assert(g_swapchain->image_count > 0);
    assert(g_swapchain->images != NULL);
    assert(g_swapchain->views  != NULL);
    /* Format is carried over, FIFO is always supported. */
    assert(g_swapchain->format == old_format);
    assert(g_swapchain->present_mode == VK_PRESENT_MODE_FIFO_KHR);
    assert(g_swapchain->extent.width  > 0);
    assert(g_swapchain->extent.height > 0);

    for(uint32_t i = 0; i < g_swapchain->image_count; ++i) {
        assert(g_swapchain->images[i] != VK_NULL_HANDLE);
        assert(g_swapchain->views[i]  != VK_NULL_HANDLE);
    }

    /* An unsupported present mode must fall back to FIFO. */
    assert_harp(
        g_swapchain->recreate(g_swapchain, actor, 800, 600, (VkPresentModeKHR)0x7FFFFFFF),
        "recreate with bogus present mode"
    );
    assert(g_swapchain->present_mode == VK_PRESENT_MODE_FIFO_KHR);

    printf("    recreated: %ux%u  mode=%u  images=%u\n",
        g_swapchain->extent.width, g_swapchain->extent.height,
        g_swapchain->present_mode, g_swapchain->image_count);

    TEST_MARKER("SWAPCHAIN", "RECREATE_DONE");
}


/* ========================================================= */
/* TEARDOWN                                                   */
/* ========================================================= */

static void test_swapchain_term(void) {
    if(!g_vk || !g_device_actor) return;
    if(!(((HarpHandlerBase *)g_swapchain)->status & HARP_STATUS_FLAG_VALID)) return;

    TEST_MARKER("SWAPCHAIN", "TERM");

    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_VULKAN_SWAPCHAIN_HANDLER_NAME),
        "terminate swapchain handler"
    );

    HarpHandlerBase *base = (HarpHandlerBase *)g_swapchain;
    assert(!(base->status & HARP_STATUS_FLAG_VALID));
    assert(g_swapchain->swapchain   == VK_NULL_HANDLE);
    assert(g_swapchain->images      == NULL);
    assert(g_swapchain->views       == NULL);
    assert(g_swapchain->image_count == 0);

    TEST_MARKER("SWAPCHAIN", "TERM_DONE");
}

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
    printf("=== HARP / MAESTRO SWAPCHAIN TEST ===\n");

    test_runtime_init(argv[0]);
    test_maestro_register();

    test_logger_init();
    test_window_init();

    test_vulkan_core_init();
    test_surface_create();
    test_device_create();

    test_swapchain_get_handler();
    test_swapchain_init_default_rejected();
    test_swapchain_init();

    if(g_vk && g_device_actor) {
        frame_objects_create();
        test_swapchain_frame("FRAME");
        test_swapchain_recreate();
        test_swapchain_frame("FRAME_AFTER_RECREATE");
        frame_objects_destroy();
    }

    test_swapchain_term();
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
