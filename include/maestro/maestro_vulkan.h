#ifndef MAESTRO_VULKAN_H
#define MAESTRO_VULKAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <harp/harp.h>
#include <harp/utils/harp_version.h>

#include <vulkan/vulkan.h>


/* ================================================================================ */
/*  TYPEDEF                                                                         */
/* ================================================================================ */

typedef struct MaestroVulkanCoreCreator MaestroVulkanCoreCreator;
typedef struct MaestroVulkanCoreHandler MaestroVulkanCoreHandler;

typedef struct MaestroVulkanDeviceCreator MaestroVulkanDeviceCreator;
typedef struct MaestroVulkanDeviceActor MaestroVulkanDeviceActor;

typedef struct MaestroVulkanSwapchainCreator MaestroVulkanSwapchainCreator;
typedef struct MaestroVulkanSwapchainHandler MaestroVulkanSwapchainHandler;

typedef i32 (*MaestroVulkanDeviceScorePfn)(VkPhysicalDevice);

#define MAESTRO_VULKAN_MAX_QUEUES 8

/* One entry per distinct queue family Maestro created a queue for.
   Sorted by priority: graphics+present first, then graphics, then
   compute-only, then transfer-only. */
typedef struct MaestroVulkanQueue {
    VkQueue      queue;
    u32     family;
    VkQueueFlags flags;
    b8           supports_present;
} MaestroVulkanQueue;


/* ================================================================================ */
/*  CORE HANDLER                                                                    */
/* ================================================================================ */

#define MAESTRO_VULKAN_CORE_HANDLER_NAME "MaestroVulkanCoreHandler"
#define MAESTRO_VULKAN_CORE_HANDLER_VERSION HARP_MAKE_VERSION(3,0,0)

struct MaestroVulkanCoreCreator {
    HarpCreatorBase _base;

    const char *app_name;
    HarpVersion app_version;

    const char **extensions;
    u32 extension_count;

    b8 enable_validation;
};

typedef struct MaestroVulkanImageDesc {
    VkFormat format;
    VkExtent3D extent;
    VkImageType type; /* pass VK_IMAGE_TYPE_2D etc; 0 is 1D */
    u32 mip_levels; /* 0 defaults to 1 */
    u32 array_layers; /* 0 defaults to 1 */
    VkImageUsageFlags usage;
    VkImageTiling tiling; /* 0 is OPTIMAL */
    VkSampleCountFlagBits samples; /* 0 defaults to 1 */
    b8 make_view; /* finish_image builds the default view */
} MaestroVulkanImageDesc;

typedef struct MaestroVulkanImage {
    VkImage image;
    VkImageView view; /* VK_NULL_HANDLE when make_view was false */
} MaestroVulkanImage;


struct MaestroVulkanCoreHandler {
    HarpHandlerBase _base;

    HarpResult (*create_buffer_unbound)(MaestroVulkanDeviceActor *device, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *out_buffer, VkMemoryRequirements *out_reqs);
    HarpResult (*bind_buffer)(MaestroVulkanDeviceActor *device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset);
    void (*destroy_buffer)(MaestroVulkanDeviceActor *device, VkBuffer buffer);

    HarpResult (*create_image_unbound)(MaestroVulkanDeviceActor *device, const MaestroVulkanImageDesc *desc, VkImage *out_image, VkMemoryRequirements *out_reqs);
    HarpResult (*finish_image)(MaestroVulkanDeviceActor *device, const MaestroVulkanImageDesc *desc, VkImage image, VkDeviceMemory memory, VkDeviceSize offset, MaestroVulkanImage *out);
    void (*destroy_image)(MaestroVulkanDeviceActor *device, MaestroVulkanImage *image);

    /* Memory helpers for the caller's own allocator. Maestro never calls these
       itself; they only simplify writing an allocator and may go unused. */
    u32 (*find_memory_type)(MaestroVulkanDeviceActor *device, u32 type_bits, VkMemoryPropertyFlags props);
    HarpResult (*alloc_memory)(MaestroVulkanDeviceActor *device, VkDeviceSize size, u32 memory_type_index, VkDeviceMemory *out);
    void (*free_memory)(MaestroVulkanDeviceActor *device, VkDeviceMemory memory);

    MaestroVulkanDeviceScorePfn pfn_default_device_score;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
};


/* ================================================================================ */
/*  DEVICE ACTOR                                                                    */
/* ================================================================================ */

#define MAESTRO_VULKAN_DEVICE_ACTOR_NAME "MaestroVulkanDeviceActor"
#define MAESTRO_VULKAN_DEVICE_ACTOR_VERSION HARP_MAKE_VERSION(2,0,0)

struct MaestroVulkanDeviceCreator {
    HarpCreatorBase _base;

    MaestroVulkanDeviceScorePfn pfn_score;

    /* NULL = no features requested. Use VkPhysicalDeviceFeatures2 to chain
       Vulkan 1.1+ feature structs via pNext. Passed directly to vkCreateDevice. */
    const VkPhysicalDeviceFeatures2 *features;

    const char **extensions;
    u32     extension_count;

    VkSurfaceKHR surface;
};

struct MaestroVulkanDeviceActor {
    HarpActorBase _base;

    VkPhysicalDevice physical_device;
    VkDevice device;

    /* Queues Maestro created for this device, sorted by priority.
       Iterate to find the queue that matches your needs via flags / supports_present. */
    MaestroVulkanQueue queues[MAESTRO_VULKAN_MAX_QUEUES];
    u32 queue_count;
};


/* ================================================================================ */
/*  SWAPCHAIN HANDLER                                                               */
/* ================================================================================ */

#define MAESTRO_VULKAN_SWAPCHAIN_HANDLER_NAME "MaestroVulkanSwapchainHandler"
#define MAESTRO_VULKAN_SWAPCHAIN_HANDLER_VERSION HARP_MAKE_VERSION(2,0,0)

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
     HARP_RESULT_OK            proceed; if *out_suboptimal was set to 1 the
                               swapchain still works but should be recreated
                               when convenient (e.g. end of frame)
     HARP_RESULT_FAILED        swapchain is out of date, no image was
                               acquired / presented, recreate and retry
     HARP_RESULT_CRITICAL_FAIL unrecoverable (e.g. device lost), do not retry */
struct MaestroVulkanSwapchainHandler {
    HarpHandlerBase _base;

    /* signal_semaphore is signalled when the image is ready to render into.
       out_suboptimal may be NULL if the caller does not care. */
    HarpResult (*acquire)(MaestroVulkanSwapchainHandler *h, VkSemaphore signal_semaphore, u32 *out_image_index, b8 *out_suboptimal);
    /* wait_semaphore is waited on before presenting (render-finished semaphore).
       out_suboptimal may be NULL if the caller does not care. */
    HarpResult (*present)(MaestroVulkanSwapchainHandler *h, VkQueue queue, VkSemaphore wait_semaphore, u32 image_index, b8 *out_suboptimal);
    /* Call on window resize, after acquire/present returns HARP_RESULT_FAILED,
       or at a convenient point after one of them reported suboptimal. */
    HarpResult (*recreate)(MaestroVulkanSwapchainHandler *h, MaestroVulkanDeviceActor *device, u32 width, u32 height, VkPresentModeKHR present_mode);

    VkSwapchainKHR swapchain;
    VkFormat format;
    VkColorSpaceKHR color_space;
    VkExtent2D extent;
    VkPresentModeKHR present_mode;
    VkImageUsageFlags usage;
    VkCompositeAlphaFlagBitsKHR composite_alpha;

    VkImage *images;
    VkImageView *views;
    u32 image_count;
};


#ifdef __cplusplus
}
#endif

#endif /* MAESTRO_VULKAN_H */
