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

typedef int32_t (*MaestroVulkanDeviceScorePfn)(VkPhysicalDevice);

#define MAESTRO_VULKAN_MAX_QUEUES 8

/* One entry per distinct queue family Maestro created a queue for.
   Sorted by priority: graphics+present first, then graphics, then
   compute-only, then transfer-only. */
typedef struct MaestroVulkanQueue {
    VkQueue      queue;
    uint32_t     family;
    VkQueueFlags flags;
    uint8_t      supports_present;
} MaestroVulkanQueue;


/* ================================================================================ */
/*  CORE HANDLER                                                                    */
/* ================================================================================ */

#define MAESTRO_VULKAN_CORE_HANDLER_NAME "MaestroVulkanCoreHandler"
#define MAESTRO_VULKAN_CORE_HANDLER_VERSION HARP_MAKE_VERSION(2,0,0)

struct MaestroVulkanCoreCreator {
    HarpCreatorBase _base;

    const char *app_name;
    HarpVersion app_version;

    const char **extensions;
    uint32_t extension_count;

    uint8_t enable_validation;
};

struct MaestroVulkanCoreHandler {
    HarpHandlerBase _base;

    /* mem_props: e.g. VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT */
    HarpResult (*create_buffer)(MaestroVulkanDeviceActor *device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_props, VkBuffer *out_buffer, VkDeviceMemory *out_memory);
    void (*destroy_buffer)(MaestroVulkanDeviceActor *device, VkBuffer buffer, VkDeviceMemory memory);

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
    uint32_t     extension_count;

    VkSurfaceKHR surface;
};

struct MaestroVulkanDeviceActor {
    HarpActorBase _base;

    VkPhysicalDevice physical_device;
    VkDevice device;

    /* Queues Maestro created for this device, sorted by priority.
       Iterate to find the queue that matches your needs via flags / supports_present. */
    MaestroVulkanQueue queues[MAESTRO_VULKAN_MAX_QUEUES];
    uint32_t queue_count;
};


#ifdef __cplusplus
}
#endif

#endif /* MAESTRO_VULKAN_H */
