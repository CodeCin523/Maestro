#ifndef MAESTRO_VULKAN_SWAPCHAIN_H
#define MAESTRO_VULKAN_SWAPCHAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <maestro/maestro_vulkan.h>


/* ================================================================================ */
/*  TYPEDEF                                                                         */
/* ================================================================================ */

typedef struct MaestroVulkanSwapchainCreator MaestroVulkanSwapchainCreator;
typedef struct MaestroVulkanSwapchainHandler MaestroVulkanSwapchainHandler;


/* ================================================================================ */
/*  SWAPCHAIN HANDLER                                                               */
/* ================================================================================ */

#define MAESTRO_VULKAN_SWAPCHAIN_HANDLER_NAME "MaestroVulkanSwapchainHandler"
#define MAESTRO_VULKAN_SWAPCHAIN_HANDLER_VERSION HARP_MAKE_VERSION(3,0,0)

struct MaestroVulkanSwapchainCreator {
    HarpCreatorBase _base;

    MaestroVulkanDeviceActor *device;
    VkSurfaceKHR surface;
    u32 width;
    u32 height;

    VkFormat preferred_format;               /* falls back to first available */
    VkPresentModeKHR preferred_present_mode; /* falls back to FIFO            */

    /* 0 = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT. Init fails with
       HARP_RESULT_INVALID_ARGUMENTS if the surface does not support
       every requested bit; the granted value is published as `usage`. */
    VkImageUsageFlags image_usage;
};

/* acquire/present result contract:
     HARP_RESULT_OK            proceed; needs_recreate may have been set if
                               the swapchain became suboptimal, recreate at a
                               convenient point (e.g. end of frame)
     HARP_RESULT_INVALID_STATE acquire timed out, no image was acquired, retry
     HARP_RESULT_FAILED        swapchain is out of date, no image was
                               acquired / presented; needs_recreate was set,
                               recreate and retry
     HARP_RESULT_CRITICAL_FAIL unrecoverable (e.g. device lost), do not retry */
struct MaestroVulkanSwapchainHandler {
    HarpHandlerBase _base;

    /* signal_semaphore is signalled when the image is ready to render into.
       timeout_ns = UINT64_MAX blocks until an image is available. */
    HarpResult (*acquire)(MaestroVulkanSwapchainHandler *h, VkSemaphore signal_semaphore, u64 timeout_ns, u32 *out_image_index);
    /* Waits on render_sems[image_index]: the submission that rendered to the
       image must signal that semaphore. queue VK_NULL_HANDLE presents on the
       default present_queue. */
    HarpResult (*present)(MaestroVulkanSwapchainHandler *h, VkQueue queue, u32 image_index);
    /* Call on window resize or once needs_recreate is set. Waits for the
       device to idle, then rebuilds swapchain, views and render_sems. */
    HarpResult (*recreate)(MaestroVulkanSwapchainHandler *h, u32 width, u32 height, VkPresentModeKHR present_mode);
    /* Rebuilds with the current extent, e.g. a runtime vsync toggle.
       Unsupported modes fall back to FIFO. */
    HarpResult (*set_present_mode)(MaestroVulkanSwapchainHandler *h, VkPresentModeKHR present_mode);

    VkSwapchainKHR swapchain;
    VkFormat format;
    VkColorSpaceKHR color_space;
    VkExtent2D extent;
    VkPresentModeKHR present_mode;
    VkImageUsageFlags usage;
    VkCompositeAlphaFlagBitsKHR composite_alpha;

    VkQueue present_queue; /* first queue of the device with present support */

    b8 needs_recreate; /* set by acquire/present on suboptimal or out of date,
                          cleared by recreate; callers may also set it */
    u32 generation;    /* bumped on every successful (re)build; compare it to
                          know when dependent resources (depth targets, ...)
                          must be rebuilt */

    VkImage *images;
    VkImageView *views;
    VkSemaphore *render_sems; /* one per image, waited on by present */
    u32 image_count;
};


#ifdef __cplusplus
}
#endif

#endif /* MAESTRO_VULKAN_SWAPCHAIN_H */
