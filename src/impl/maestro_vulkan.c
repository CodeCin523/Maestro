#include "maestro_vulkan.h"

#include <maestro/maestro.h>

#include <harp/utils/harp_helpers.h>
#include <harp/utils/harp_platform.h>
#include <harp/utils/harp_build.h>

#include <stdlib.h>
#include <string.h>


/* ================================================================================ */
/*  CONSTANTS                                                                        */
/* ================================================================================ */

static const char *const VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};
static const uint32_t VALIDATION_LAYER_COUNT = sizeof(VALIDATION_LAYERS) / sizeof(VALIDATION_LAYERS[0]);


/* ================================================================================ */
/*  DEBUG MESSENGER                                                                  */
/* ================================================================================ */

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data,
    void *user_data
) {
    HARP_UNUSED(type);

    MaestroLoggerHandler *logger = (MaestroLoggerHandler *)user_data;

    if(severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        MAESTRO_LOG_ERROR(logger, "Vulkan", data->pMessage);
    else if(severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        MAESTRO_LOG_WARN(logger, "Vulkan", data->pMessage);
    else {
        MAESTRO_LOG_DEBUG(logger, "Vulkan", data->pMessage);
    }

    return VK_FALSE;
}

static void fill_debug_messenger_info(VkDebugUtilsMessengerCreateInfoEXT *info, MaestroLoggerHandler *logger) {
    *info = (VkDebugUtilsMessengerCreateInfoEXT){
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_messenger_callback,
        .pUserData       = logger
    };
}


/* ================================================================================ */
/*  DEFAULT SCORE                                                                    */
/* ================================================================================ */

int32_t default_device_score(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    switch(props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 1000;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 100;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 10;
        default:                                      return 0;
    }
}


/* ================================================================================ */
/*  VULKAN INSTANCE HANDLER                                                          */
/* ================================================================================ */

HarpResult init_vulkan_instance(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator_base) {
    MaestroVulkanInstanceCreator creator = {
        .app_name         = "MaestroApp",
        .app_version      = HARP_MAKE_VERSION(1, 0, 0),
        .extensions       = NULL,
        .extension_count  = 0,
        .enable_validation = HARP_DEBUG
    };
    if(!(creator_base->flags & HARP_CREATOR_FLAG_DEFAULT_CREATOR))
        creator = *(MaestroVulkanInstanceCreator *)creator_base;

    MaestroVulkanInstanceHandlerImpl *impl = HARP_HANDLER_AS(MaestroVulkanInstanceHandlerImpl, base);

    if(core_handler->get_handler(
        core_handler,
        &HARP_DEPENDENCY(MAESTRO_LOGGER_HANDLER_NAME, 0, UINT32_MAX),
        (HarpHandlerBase **)&impl->logger) != HARP_RESULT_OK)
            return HARP_RESULT_FAILED;

    uint8_t validation = creator.enable_validation;
#if HARP_DEBUG
    validation = 1;
#endif

    // Validation layer availability check
    if(validation) {
        uint32_t layer_count = 0;
        vkEnumerateInstanceLayerProperties(&layer_count, NULL);

        VkLayerProperties *available = malloc(layer_count * sizeof(VkLayerProperties));
        if(!available)
            return HARP_RESULT_OUT_OF_MEMORY;

        vkEnumerateInstanceLayerProperties(&layer_count, available);

        for(uint32_t i = 0; i < VALIDATION_LAYER_COUNT; ++i) {
            uint8_t found = 0;
            for(uint32_t j = 0; j < layer_count; ++j) {
                if(strcmp(VALIDATION_LAYERS[i], available[j].layerName) == 0) {
                    found = 1;
                    break;
                }
            }
            if(!found) {
                free(available);
                MAESTRO_LOG_FATAL(impl->logger, base->name, "Required validation layer not available");
                return HARP_RESULT_FAILED;
            }
        }

        free(available);
    }

    // Extension list: user extensions (expected to include surface extensions) + debug utils
    uint32_t ext_count = (uint32_t)creator.extension_count;
    const uint32_t max_extra = 1;

    const char **extensions = malloc((ext_count + max_extra) * sizeof(const char *));
    if(!extensions)
        return HARP_RESULT_OUT_OF_MEMORY;

    for(uint32_t i = 0; i < ext_count; ++i)
        extensions[i] = creator.extensions[i];

    if(validation)
        extensions[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

    VkApplicationInfo app_info = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = creator.app_name,
        .applicationVersion = VK_MAKE_VERSION(
            HARP_VERSION_MAJOR(creator.app_version),
            HARP_VERSION_MINOR(creator.app_version),
            HARP_VERSION_PATCH(creator.app_version)
        ),
        .pEngineName        = MAESTRO_PACKAGE_NAME,
        .engineVersion      = VK_MAKE_VERSION(
            HARP_VERSION_MAJOR(MAESTRO_PACKAGE_VERSION),
            HARP_VERSION_MINOR(MAESTRO_PACKAGE_VERSION),
            HARP_VERSION_PATCH(MAESTRO_PACKAGE_VERSION)
        ),
        .apiVersion         = VK_API_VERSION_1_3
    };

    VkDebugUtilsMessengerCreateInfoEXT debug_info = {0};
    if(validation)
        fill_debug_messenger_info(&debug_info, impl->logger);

    VkInstanceCreateInfo instance_info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = validation ? &debug_info : NULL,
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = validation ? VALIDATION_LAYER_COUNT : 0,
        .ppEnabledLayerNames     = validation ? VALIDATION_LAYERS : NULL,
        .enabledExtensionCount   = ext_count,
        .ppEnabledExtensionNames = extensions
    };

    VkResult res = vkCreateInstance(&instance_info, NULL, &impl->pub.instance);
    free(extensions);

    if(res != VK_SUCCESS) {
        MAESTRO_LOG_FATAL(impl->logger, base->name, "Failed to create Vulkan instance");
        return HARP_RESULT_FAILED;
    }

    // Debug messenger — reuse debug_info, already filled above
    if(validation) {
        PFN_vkCreateDebugUtilsMessengerEXT fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(impl->pub.instance, "vkCreateDebugUtilsMessengerEXT");

        if(!fn || fn(impl->pub.instance, &debug_info, NULL, &impl->pub.debug_messenger) != VK_SUCCESS) {
            MAESTRO_LOG_FATAL(impl->logger, base->name, "Failed to create debug messenger");
            vkDestroyInstance(impl->pub.instance, NULL);
            impl->pub.instance = VK_NULL_HANDLE;
            return HARP_RESULT_FAILED;
        }
    }

    // Enumerate and cache physical devices
    vkEnumeratePhysicalDevices(impl->pub.instance, &impl->device_count, NULL);
    if(impl->device_count == 0) {
        MAESTRO_LOG_FATAL(impl->logger, base->name, "No Vulkan-capable GPU found");
        vkDestroyInstance(impl->pub.instance, NULL);
        impl->pub.instance = VK_NULL_HANDLE;
        return HARP_RESULT_FAILED;
    }

    impl->devices = malloc(impl->device_count * sizeof(VkPhysicalDevice));
    if(!impl->devices) {
        vkDestroyInstance(impl->pub.instance, NULL);
        impl->pub.instance = VK_NULL_HANDLE;
        return HARP_RESULT_OUT_OF_MEMORY;
    }

    vkEnumeratePhysicalDevices(impl->pub.instance, &impl->device_count, impl->devices);

    impl->pub.pfn_default_device_score = default_device_score;

    MAESTRO_LOG_INFO(impl->logger, base->name, "Vulkan instance created");
    return HARP_RESULT_OK;
}

HarpResult term_vulkan_instance(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);

    MaestroVulkanInstanceHandlerImpl *impl = HARP_HANDLER_AS(MaestroVulkanInstanceHandlerImpl, base);

    free(impl->devices);
    impl->devices = NULL;
    impl->device_count = 0;

    if(impl->pub.debug_messenger != VK_NULL_HANDLE) {
        PFN_vkDestroyDebugUtilsMessengerEXT fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(impl->pub.instance, "vkDestroyDebugUtilsMessengerEXT");
        if(fn)
            fn(impl->pub.instance, impl->pub.debug_messenger, NULL);
        impl->pub.debug_messenger = VK_NULL_HANDLE;
    }

    if(impl->pub.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(impl->pub.instance, NULL);
        impl->pub.instance = VK_NULL_HANDLE;
    }

    return HARP_RESULT_OK;
}


/* ================================================================================ */
/*  VULKAN DEVICE ACTOR                                                              */
/* ================================================================================ */

HarpResult create_vulkan_device(HarpCoreHandler *core_handler, HarpActorBase *base, HarpCreatorBase *creator_base) {
    MaestroVulkanDeviceCreator creator = {
        .pfn_score        = NULL,
        .request_compute  = 0,
        .request_transfer = 0,
    };
    if(!(creator_base->flags & HARP_CREATOR_FLAG_DEFAULT_CREATOR))
        creator = *(MaestroVulkanDeviceCreator *)creator_base;

    MaestroVulkanDeviceActorImpl *impl = HARP_ACTOR_AS(MaestroVulkanDeviceActorImpl, base);

    HarpHandlerBase *handler_base = NULL;
    if(core_handler->get_handler(
        core_handler,
        &HARP_DEPENDENCY(MAESTRO_VULKAN_INSTANCE_HANDLER_NAME, 0, UINT32_MAX),
        &handler_base) != HARP_RESULT_OK)
            return HARP_RESULT_FAILED;

    MaestroVulkanInstanceHandlerImpl *instance = HARP_HANDLER_AS(MaestroVulkanInstanceHandlerImpl, handler_base);
    impl->logger = instance->logger;

    MaestroVulkanDeviceScorePfn score_fn = creator.pfn_score
        ? creator.pfn_score
        : instance->pub.pfn_default_device_score;

    // Score remaining physical devices, pick the best
    VkPhysicalDevice best_device = VK_NULL_HANDLE;
    int32_t best_score = 0;
    uint32_t best_index = 0;

    for(uint32_t i = 0; i < instance->device_count; ++i) {
        int32_t score = score_fn(instance->devices[i]);
        if(score > best_score) {
            best_score = score;
            best_device = instance->devices[i];
            best_index = i;
        }
    }

    if(best_device == VK_NULL_HANDLE) {
        MAESTRO_LOG_FATAL(impl->logger, base->name, "No suitable GPU available");
        return HARP_RESULT_FAILED;
    }

    // Remove from instance list: swap with last element
    instance->devices[best_index] = instance->devices[--instance->device_count];
    impl->pub.physical_device = best_device;

    // Find queue families
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(best_device, &family_count, NULL);

    VkQueueFamilyProperties *families = malloc(family_count * sizeof(VkQueueFamilyProperties));
    if(!families)
        return HARP_RESULT_OUT_OF_MEMORY;

    vkGetPhysicalDeviceQueueFamilyProperties(best_device, &family_count, families);

    uint32_t graphics_family = UINT32_MAX;
    uint32_t compute_family  = UINT32_MAX;
    uint32_t transfer_family = UINT32_MAX;

    for(uint32_t i = 0; i < family_count; ++i) {
        VkQueueFlags flags = families[i].queueFlags;

        if(graphics_family == UINT32_MAX && (flags & VK_QUEUE_GRAPHICS_BIT))
            graphics_family = i;

        // Prefer dedicated compute (no graphics bit)
        if(creator.request_compute && compute_family == UINT32_MAX
            && (flags & VK_QUEUE_COMPUTE_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT))
            compute_family = i;

        // Prefer dedicated transfer (no graphics, no compute)
        if(creator.request_transfer && transfer_family == UINT32_MAX
            && (flags & VK_QUEUE_TRANSFER_BIT)
            && !(flags & VK_QUEUE_GRAPHICS_BIT)
            && !(flags & VK_QUEUE_COMPUTE_BIT))
            transfer_family = i;
    }

    free(families);

    if(graphics_family == UINT32_MAX) {
        MAESTRO_LOG_FATAL(impl->logger, base->name, "No graphics queue family found");
        return HARP_RESULT_FAILED;
    }

    // Fallback to graphics family if no dedicated family found
    if(creator.request_compute  && compute_family  == UINT32_MAX) compute_family  = graphics_family;
    if(creator.request_transfer && transfer_family == UINT32_MAX) transfer_family = graphics_family;

    // Build unique queue create infos
    static const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_infos[3];
    uint32_t unique_families[3];
    uint32_t unique_count = 0;

    unique_families[unique_count] = graphics_family;
    queue_infos[unique_count++] = (VkDeviceQueueCreateInfo){
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphics_family,
        .queueCount       = 1,
        .pQueuePriorities = &priority
    };

    if(creator.request_compute && compute_family != graphics_family) {
        unique_families[unique_count] = compute_family;
        queue_infos[unique_count++] = (VkDeviceQueueCreateInfo){
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = compute_family,
            .queueCount       = 1,
            .pQueuePriorities = &priority
        };
    }

    if(creator.request_transfer) {
        uint8_t already = 0;
        for(uint32_t i = 0; i < unique_count; ++i)
            if(unique_families[i] == transfer_family) { already = 1; break; }

        if(!already) {
            unique_families[unique_count] = transfer_family;
            queue_infos[unique_count++] = (VkDeviceQueueCreateInfo){
                .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = transfer_family,
                .queueCount       = 1,
                .pQueuePriorities = &priority
            };
        }
    }

    VkDeviceCreateInfo device_info = {
        .sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = unique_count,
        .pQueueCreateInfos    = queue_infos,
    };

    if(vkCreateDevice(best_device, &device_info, NULL, &impl->pub.device) != VK_SUCCESS) {
        MAESTRO_LOG_FATAL(impl->logger, base->name, "Failed to create logical device");
        return HARP_RESULT_FAILED;
    }

    vkGetDeviceQueue(impl->pub.device, graphics_family, 0, &impl->graphics_queue);
    impl->graphics_family = graphics_family;

    if(creator.request_compute) {
        vkGetDeviceQueue(impl->pub.device, compute_family, 0, &impl->compute_queue);
        impl->compute_family = compute_family;
    }

    if(creator.request_transfer) {
        vkGetDeviceQueue(impl->pub.device, transfer_family, 0, &impl->transfer_queue);
        impl->transfer_family = transfer_family;
    }

    MAESTRO_LOG_INFO(impl->logger, base->name, "Vulkan device created");
    return HARP_RESULT_OK;
}

HarpResult destroy_vulkan_device(HarpCoreHandler *core_handler, HarpActorBase *base) {
    MaestroVulkanDeviceActorImpl *impl = HARP_ACTOR_AS(MaestroVulkanDeviceActorImpl, base);

    if(impl->pub.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(impl->pub.device);
        vkDestroyDevice(impl->pub.device, NULL);
        impl->pub.device = VK_NULL_HANDLE;
    }

    // Return physical device to instance pool
    HarpHandlerBase *handler_base = NULL;
    if(impl->pub.physical_device != VK_NULL_HANDLE
        && core_handler->get_handler(
            core_handler,
            &HARP_DEPENDENCY(MAESTRO_VULKAN_INSTANCE_HANDLER_NAME, 0, UINT32_MAX),
            &handler_base) == HARP_RESULT_OK)
    {
        MaestroVulkanInstanceHandlerImpl *instance = HARP_HANDLER_AS(MaestroVulkanInstanceHandlerImpl, handler_base);
        instance->devices[instance->device_count++] = impl->pub.physical_device;
        impl->pub.physical_device = VK_NULL_HANDLE;
    }

    impl->graphics_queue = VK_NULL_HANDLE;
    impl->compute_queue  = VK_NULL_HANDLE;
    impl->transfer_queue = VK_NULL_HANDLE;

    return HARP_RESULT_OK;
}
