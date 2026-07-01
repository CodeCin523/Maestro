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

typedef struct MaestroVulkanInstanceCreator MaestroVulkanInstanceCreator;
typedef struct MaestroVulkanInstanceHandler MaestroVulkanInstanceHandler;

typedef struct MaestroVulkanDeviceCreator MaestroVulkanDeviceCreator;
typedef struct MaestroVulkanDeviceActor MaestroVulkanDeviceActor;

typedef int32_t (*MaestroVulkanDeviceScorePfn)(VkPhysicalDevice);


/* ================================================================================ */
/*  Handlers                                                                        */
/* ================================================================================ */

#define MAESTRO_VULKAN_INSTANCE_HANDLER_NAME "MaestroVulkanInstanceHandler"
#define MAESTRO_VULKAN_INSTANCE_HANDLER_VERSION HARP_MAKE_VERSION(1,0,0)

struct MaestroVulkanInstanceCreator {
    HarpCreatorBase _base;

    const char *app_name;
    HarpVersion app_version;

    const char **extensions;
    uint64_t extension_count;

    uint8_t enable_validation;
};

struct MaestroVulkanInstanceHandler {
    HarpHandlerBase _base;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;

    MaestroVulkanDeviceScorePfn pfn_default_device_score;
};

#define MAESTRO_VULKAN_DEVICE_ACTOR_NAME "MaestroVulkanDeviceActor"
#define MAESTRO_VULKAN_DEVICE_ACTOR_VERSION HARP_MAKE_VERSION(1,1,0)

struct MaestroVulkanDeviceCreator {
    HarpCreatorBase _base;

    MaestroVulkanDeviceScorePfn pfn_score;

    uint8_t request_compute;
    uint8_t request_transfer;

    VkSurfaceKHR surface;
};

struct MaestroVulkanDeviceActor {
    HarpActorBase _base;

    VkPhysicalDevice physical_device;
    VkDevice device;

    uint32_t present_family;
};


#ifdef __cplusplus
}
#endif

#endif /* MAESTRO_VULKAN_H */