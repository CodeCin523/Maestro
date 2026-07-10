#include "impl/maestro_vulkan.h"

#include "maestro_globals.h"

#include <maestro/maestro.h>

#include <harp/utils/harp_helpers.h>
#include <harp/utils/harp_platform.h>
#include <harp/utils/harp_build.h>

#include <stdlib.h>
#include <string.h>


/* ================================================================================ */
/*  CONSTANTS                                                                       */
/* ================================================================================ */

static const char *const VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};
static const uint32_t VALIDATION_LAYER_COUNT = sizeof(VALIDATION_LAYERS) / sizeof(VALIDATION_LAYERS[0]);


/* ================================================================================ */
/*  DEBUG MESSENGER                                                                 */
/* ================================================================================ */

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data,
    void *user_data
) {
    HARP_UNUSED(type);
    HARP_UNUSED(user_data);

    if(severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        MAESTRO_LOG_ERROR(g_logger, "Vulkan", data->pMessage);
    else if(severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        MAESTRO_LOG_WARN(g_logger, "Vulkan", data->pMessage);
    else
        MAESTRO_LOG_DEBUG(g_logger, "Vulkan", data->pMessage);

    return VK_FALSE;
}

static void fill_debug_messenger_info(VkDebugUtilsMessengerCreateInfoEXT *info) {
    *info = (VkDebugUtilsMessengerCreateInfoEXT){
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_messenger_callback,
    };
}


/* ================================================================================ */
/*  VULKAN CORE HANDLER                                                             */
/* ================================================================================ */

HarpResult vulkan_create_buffer(
    MaestroVulkanDeviceActor *actor,
    VkDeviceSize              size,
    VkBufferUsageFlags        usage,
    VkMemoryPropertyFlags     mem_props,
    VkBuffer                 *out_buffer,
    VkDeviceMemory           *out_memory
) {
    HARP_CHECK_STATE(HARP_ACTOR_IS_VALID(actor), HARP_RESULT_INVALID_STATE);
    HARP_CHECK_ARG(out_buffer != NULL && out_memory != NULL, HARP_RESULT_MISSING_OUTPUT);

    *out_buffer = VK_NULL_HANDLE;
    *out_memory = VK_NULL_HANDLE;

    VkBufferCreateInfo buf_info = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if(vkCreateBuffer(actor->device, &buf_info, NULL, out_buffer) != VK_SUCCESS)
        return HARP_RESULT_FAILED;

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(actor->device, *out_buffer, &mem_req);

    VkPhysicalDeviceMemoryProperties mem_dev;
    vkGetPhysicalDeviceMemoryProperties(actor->physical_device, &mem_dev);

    uint32_t mem_type = UINT32_MAX;
    for(uint32_t i = 0; i < mem_dev.memoryTypeCount; ++i) {
        if((mem_req.memoryTypeBits & (1u << i)) &&
           (mem_dev.memoryTypes[i].propertyFlags & mem_props) == mem_props) {
            mem_type = i;
            break;
        }
    }

    if(mem_type == UINT32_MAX) {
        vkDestroyBuffer(actor->device, *out_buffer, NULL);
        *out_buffer = VK_NULL_HANDLE;
        return HARP_RESULT_FAILED;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mem_req.size,
        .memoryTypeIndex = mem_type
    };

    if(vkAllocateMemory(actor->device, &alloc_info, NULL, out_memory) != VK_SUCCESS) {
        vkDestroyBuffer(actor->device, *out_buffer, NULL);
        *out_buffer = VK_NULL_HANDLE;
        return HARP_RESULT_FAILED;
    }

    if(vkBindBufferMemory(actor->device, *out_buffer, *out_memory, 0) != VK_SUCCESS) {
        vkFreeMemory(actor->device, *out_memory, NULL);
        vkDestroyBuffer(actor->device, *out_buffer, NULL);
        *out_buffer = VK_NULL_HANDLE;
        *out_memory = VK_NULL_HANDLE;
        return HARP_RESULT_FAILED;
    }

    return HARP_RESULT_OK;
}

void vulkan_destroy_buffer(
    MaestroVulkanDeviceActor *actor,
    VkBuffer                  buffer,
    VkDeviceMemory            memory
) {
    if(!HARP_ACTOR_IS_VALID(actor)) return;

    if(memory != VK_NULL_HANDLE) vkFreeMemory(actor->device, memory, NULL);
    if(buffer != VK_NULL_HANDLE) vkDestroyBuffer(actor->device, buffer, NULL);
}

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

HarpResult init_vulkan_instance(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator_base) {
    MaestroVulkanCoreCreator creator = {
        .app_name          = "MaestroApp",
        .app_version       = HARP_MAKE_VERSION(1, 0, 0),
        .extensions        = NULL,
        .extension_count   = 0,
        .enable_validation = HARP_DEBUG
    };
    if(!(creator_base->flags & HARP_CREATOR_FLAG_DEFAULT_CREATOR))
        creator = *(MaestroVulkanCoreCreator *)creator_base;

    MaestroVulkanCoreHandlerImpl *impl = HARP_HANDLER_AS(MaestroVulkanCoreHandlerImpl, base);


    b8 validation = creator.enable_validation;
#if HARP_DEBUG
    validation = 1;
#endif

    if(validation) {
        uint32_t layer_count = 0;
        vkEnumerateInstanceLayerProperties(&layer_count, NULL);

        VkLayerProperties *available = malloc(layer_count * sizeof(VkLayerProperties));
        if(!available)
            return HARP_RESULT_OUT_OF_MEMORY;

        vkEnumerateInstanceLayerProperties(&layer_count, available);

        for(uint32_t i = 0; i < VALIDATION_LAYER_COUNT; ++i) {
            b8 found = 0;
            for(uint32_t j = 0; j < layer_count; ++j) {
                if(strcmp(VALIDATION_LAYERS[i], available[j].layerName) == 0) {
                    found = 1;
                    break;
                }
            }
            if(!found) {
                free(available);
                MAESTRO_LOG_FATAL(g_logger, base->name, "Required validation layer not available");
                return HARP_RESULT_FAILED;
            }
        }

        free(available);
    }

    uint32_t ext_count = creator.extension_count;
    const char **extensions = malloc((ext_count + 1) * sizeof(const char *));
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
        fill_debug_messenger_info(&debug_info);

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
        MAESTRO_LOG_FATAL(g_logger, base->name, "Failed to create Vulkan instance");
        return HARP_RESULT_FAILED;
    }

    if(validation) {
        PFN_vkCreateDebugUtilsMessengerEXT fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(impl->pub.instance, "vkCreateDebugUtilsMessengerEXT");

        if(!fn || fn(impl->pub.instance, &debug_info, NULL, &impl->pub.debug_messenger) != VK_SUCCESS) {
            MAESTRO_LOG_FATAL(g_logger, base->name, "Failed to create debug messenger");
            vkDestroyInstance(impl->pub.instance, NULL);
            impl->pub.instance = VK_NULL_HANDLE;
            return HARP_RESULT_FAILED;
        }
    }

    vkEnumeratePhysicalDevices(impl->pub.instance, &impl->device_count, NULL);
    if(impl->device_count == 0) {
        MAESTRO_LOG_FATAL(g_logger, base->name, "No Vulkan-capable GPU found");
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

    MAESTRO_LOG_INFO(g_logger, base->name, "Vulkan instance created");
    return HARP_RESULT_OK;
}

HarpResult term_vulkan_instance(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);

    MaestroVulkanCoreHandlerImpl *impl = HARP_HANDLER_AS(MaestroVulkanCoreHandlerImpl, base);

    free(impl->devices);
    impl->devices      = NULL;
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

HarpResult patch_vulkan_instance(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);
    MaestroVulkanCoreHandlerImpl *impl = HARP_HANDLER_AS(MaestroVulkanCoreHandlerImpl, base);

    // the messenger dispatches into the module that created it; recreate it
    // so validation output runs through this module's callback
    if(impl->pub.debug_messenger != VK_NULL_HANDLE) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(impl->pub.instance, "vkDestroyDebugUtilsMessengerEXT");
        PFN_vkCreateDebugUtilsMessengerEXT create_fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(impl->pub.instance, "vkCreateDebugUtilsMessengerEXT");

        if(destroy_fn && create_fn) {
            destroy_fn(impl->pub.instance, impl->pub.debug_messenger, NULL);
            impl->pub.debug_messenger = VK_NULL_HANDLE;

            VkDebugUtilsMessengerCreateInfoEXT debug_info;
            fill_debug_messenger_info(&debug_info);
            if(create_fn(impl->pub.instance, &debug_info, NULL, &impl->pub.debug_messenger) != VK_SUCCESS) {
                impl->pub.debug_messenger = VK_NULL_HANDLE;
                MAESTRO_LOG_WARN(g_logger, base->name, "Failed to recreate debug messenger after swap");
            }
        }
    }

    return HARP_RESULT_OK;
}



/* ================================================================================ */
/*  VULKAN DEVICE ACTOR                                                             */
/* ================================================================================ */

static int queue_priority(VkQueueFlags flags, b8 supports_present) {
    if((flags & VK_QUEUE_GRAPHICS_BIT) && supports_present) return 0;
    if(flags & VK_QUEUE_GRAPHICS_BIT)                       return 1;
    if(flags & VK_QUEUE_COMPUTE_BIT)                        return 2;
    return 3;
}

HarpResult create_vulkan_device(HarpCoreHandler *core_handler, HarpActorBase *base, HarpCreatorBase *creator_base) {
    MaestroVulkanDeviceCreator creator = {
        .pfn_score        = NULL,
        .features         = NULL,
        .extensions       = NULL,
        .extension_count  = 0,
        .surface          = VK_NULL_HANDLE,
    };
    if(!(creator_base->flags & HARP_CREATOR_FLAG_DEFAULT_CREATOR))
        creator = *(MaestroVulkanDeviceCreator *)creator_base;

    MaestroVulkanDeviceActor *impl = HARP_ACTOR_AS(MaestroVulkanDeviceActor, base);

    HarpHandlerBase *handler_base = NULL;
    if(core_handler->get_handler(
        core_handler,
        &HARP_DEPENDENCY(MAESTRO_VULKAN_CORE_HANDLER_NAME, 0, UINT32_MAX),
        &handler_base) != HARP_RESULT_OK)
            return HARP_RESULT_FAILED;

    MaestroVulkanCoreHandlerImpl *core = HARP_HANDLER_AS(MaestroVulkanCoreHandlerImpl, handler_base);

    MaestroVulkanDeviceScorePfn score_fn = creator.pfn_score
        ? creator.pfn_score
        : core->pub.pfn_default_device_score;

    /* Score and select the best physical device. */
    VkPhysicalDevice best_device = VK_NULL_HANDLE;
    int32_t  best_score = 0;
    uint32_t best_index = 0;

    for(uint32_t i = 0; i < core->device_count; ++i) {
        int32_t score = score_fn(core->devices[i]);
        if(score > best_score) {
            best_score = score;
            best_device = core->devices[i];
            best_index  = i;
        }
    }

    if(best_device == VK_NULL_HANDLE) {
        MAESTRO_LOG_FATAL(g_logger, base->name, "No suitable GPU available");
        return HARP_RESULT_FAILED;
    }

    /* Remove from pool: swap with last element. */
    core->devices[best_index] = core->devices[--core->device_count];
    impl->physical_device = best_device;

    /* Verify swapchain extension support when a surface is provided. */
    if(creator.surface != VK_NULL_HANDLE) {
        uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(best_device, NULL, &ext_count, NULL);

        VkExtensionProperties *exts = malloc(ext_count * sizeof(VkExtensionProperties));
        if(!exts)
            return HARP_RESULT_OUT_OF_MEMORY;

        vkEnumerateDeviceExtensionProperties(best_device, NULL, &ext_count, exts);

        b8 has_swapchain = 0;
        for(uint32_t i = 0; i < ext_count; ++i) {
            if(strcmp(exts[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                has_swapchain = 1;
                break;
            }
        }
        free(exts);

        if(!has_swapchain) {
            MAESTRO_LOG_FATAL(g_logger, base->name, "Device does not support VK_KHR_swapchain");
            return HARP_RESULT_FAILED;
        }
    }

    /* Enumerate queue families and collect one entry per useful family. */
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(best_device, &family_count, NULL);

    VkQueueFamilyProperties *families = malloc(family_count * sizeof(VkQueueFamilyProperties));
    if(!families)
        return HARP_RESULT_OUT_OF_MEMORY;

    vkGetPhysicalDeviceQueueFamilyProperties(best_device, &family_count, families);

    /* Temporary queue list, unsorted. */
    MaestroVulkanQueue tmp[MAESTRO_VULKAN_MAX_QUEUES];
    uint32_t tmp_count = 0;

    for(uint32_t i = 0; i < family_count && tmp_count < MAESTRO_VULKAN_MAX_QUEUES; ++i) {
        VkQueueFlags flags = families[i].queueFlags;

        if(!(flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)))
            continue;

        VkBool32 present = VK_FALSE;
        if(creator.surface != VK_NULL_HANDLE)
            vkGetPhysicalDeviceSurfaceSupportKHR(best_device, i, creator.surface, &present);

        tmp[tmp_count++] = (MaestroVulkanQueue){
            .queue            = VK_NULL_HANDLE,
            .family           = i,
            .flags            = flags,
            .supports_present = (b8)present
        };
    }

    free(families);

    /* Validate: at least one queue must support present when a surface is given. */
    if(creator.surface != VK_NULL_HANDLE) {
        b8 found_present = 0;
        for(uint32_t i = 0; i < tmp_count; ++i)
            if(tmp[i].supports_present) { found_present = 1; break; }

        if(!found_present) {
            MAESTRO_LOG_FATAL(g_logger, base->name, "No queue family supports present for this surface");
            return HARP_RESULT_FAILED;
        }
    }

    /* Insertion sort by priority: graphics+present > graphics > compute > transfer. */
    for(uint32_t i = 1; i < tmp_count; ++i) {
        MaestroVulkanQueue key = tmp[i];
        int key_prio = queue_priority(key.flags, key.supports_present);
        int32_t j = (int32_t)i - 1;
        while(j >= 0 && queue_priority(tmp[j].flags, tmp[j].supports_present) > key_prio) {
            tmp[j + 1] = tmp[j];
            --j;
        }
        tmp[j + 1] = key;
    }

    /* Build unique VkDeviceQueueCreateInfo array. */
    static const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_infos[MAESTRO_VULKAN_MAX_QUEUES];
    for(uint32_t i = 0; i < tmp_count; ++i) {
        queue_infos[i] = (VkDeviceQueueCreateInfo){
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = tmp[i].family,
            .queueCount       = 1,
            .pQueuePriorities = &priority
        };
    }

    /* Merge user extensions with VK_KHR_swapchain when needed. */
    uint32_t total_ext_count = creator.extension_count;
    b8       needs_swapchain = (creator.surface != VK_NULL_HANDLE);
    if(needs_swapchain) total_ext_count++;

    const char **all_extensions = NULL;
    if(total_ext_count > 0) {
        all_extensions = malloc(total_ext_count * sizeof(const char *));
        if(!all_extensions)
            return HARP_RESULT_OUT_OF_MEMORY;

        for(uint32_t i = 0; i < creator.extension_count; ++i)
            all_extensions[i] = creator.extensions[i];

        if(needs_swapchain)
            all_extensions[total_ext_count - 1] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    }

    VkDeviceCreateInfo device_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = creator.features,  /* VkPhysicalDeviceFeatures2 chain, or NULL */
        .queueCreateInfoCount    = tmp_count,
        .pQueueCreateInfos       = queue_infos,
        .enabledExtensionCount   = total_ext_count,
        .ppEnabledExtensionNames = all_extensions,
        .pEnabledFeatures        = NULL  /* must be NULL when using pNext features chain */
    };

    VkResult vk_res = vkCreateDevice(best_device, &device_info, NULL, &impl->device);
    free(all_extensions);

    if(vk_res != VK_SUCCESS) {
        MAESTRO_LOG_FATAL(g_logger, base->name, "Failed to create logical device");
        return HARP_RESULT_FAILED;
    }

    /* Retrieve queue handles and fill the public array. */
    impl->queue_count = tmp_count;
    for(uint32_t i = 0; i < tmp_count; ++i) {
        impl->queues[i] = tmp[i];
        vkGetDeviceQueue(impl->device, tmp[i].family, 0, &impl->queues[i].queue);
    }

    MAESTRO_LOG_INFO(g_logger, base->name, "Vulkan device created");
    return HARP_RESULT_OK;
}

HarpResult destroy_vulkan_device(HarpCoreHandler *core_handler, HarpActorBase *base) {
    MaestroVulkanDeviceActor *impl = HARP_ACTOR_AS(MaestroVulkanDeviceActor, base);

    if(impl->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(impl->device);
        vkDestroyDevice(impl->device, NULL);
        impl->device = VK_NULL_HANDLE;
    }

    /* Return physical device to the pool. */
    HarpHandlerBase *handler_base = NULL;
    if(impl->physical_device != VK_NULL_HANDLE
        && core_handler->get_handler(
            core_handler,
            &HARP_DEPENDENCY(MAESTRO_VULKAN_CORE_HANDLER_NAME, 0, UINT32_MAX),
            &handler_base) == HARP_RESULT_OK)
    {
        MaestroVulkanCoreHandlerImpl *core = HARP_HANDLER_AS(MaestroVulkanCoreHandlerImpl, handler_base);
        core->devices[core->device_count++] = impl->physical_device;
        impl->physical_device = VK_NULL_HANDLE;
    }

    impl->queue_count = 0;

    return HARP_RESULT_OK;
}

HarpResult patch_vulkan_device(HarpCoreHandler *core_handler, HarpActorBase *base) {
    HARP_UNUSED(core_handler);
    HARP_UNUSED(base);

    // the actor publishes only data (device, queues); buffer helpers live on
    // the core handler and were refreshed by swap-time registration
    return HARP_RESULT_OK;
}


/* ================================================================================ */
/*  SWAPCHAIN SELECTION                                                             */
/* ================================================================================ */

static VkSurfaceFormatKHR select_format_preferred(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface,
    VkFormat preferred)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, NULL);
    VkSurfaceFormatKHR *formats = malloc(count * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, formats);

    VkSurfaceFormatKHR result = formats[0];
    for(uint32_t i = 0; i < count; ++i) {
        if(formats[i].format == preferred &&
           formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            result = formats[i];
            break;
        }
    }
    free(formats);
    return result;
}

static VkPresentModeKHR select_present_mode_preferred(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface,
    VkPresentModeKHR preferred)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, NULL);
    VkPresentModeKHR *modes = malloc(count * sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, modes);

    VkPresentModeKHR result = VK_PRESENT_MODE_FIFO_KHR;
    for(uint32_t i = 0; i < count; ++i) {
        if(modes[i] == preferred) { result = preferred; break; }
    }
    free(modes);
    return result;
}


/* ================================================================================ */
/*  VULKAN SWAPCHAIN HANDLER                                                        */
/* ================================================================================ */

/* Creates a new swapchain and image views, replacing any previous one.
   Old resources are only destroyed after the new ones are fully ready. */
static HarpResult swapchain_build(
    MaestroVulkanSwapchainHandlerImpl *impl,
    MaestroVulkanDeviceActor *device,
    uint32_t width,
    uint32_t height,
    VkSurfaceFormatKHR format,
    VkPresentModeKHR present_mode)
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physical_device, impl->surface, &caps);

    uint32_t image_count = caps.minImageCount + 1;
    if(caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;

    VkExtent2D extent;
    if(caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width  = width  < caps.minImageExtent.width  ? caps.minImageExtent.width  :
                        width  > caps.maxImageExtent.width  ? caps.maxImageExtent.width  : width;
        extent.height = height < caps.minImageExtent.height ? caps.minImageExtent.height :
                        height > caps.maxImageExtent.height ? caps.maxImageExtent.height : height;
    }

    uint32_t graphics_family = UINT32_MAX;
    uint32_t present_family  = UINT32_MAX;
    for(uint32_t i = 0; i < device->queue_count; ++i) {
        if((device->queues[i].flags & VK_QUEUE_GRAPHICS_BIT) && graphics_family == UINT32_MAX)
            graphics_family = device->queues[i].family;
        if(device->queues[i].supports_present && present_family == UINT32_MAX)
            present_family = device->queues[i].family;
    }

    uint32_t queue_indices[2] = { graphics_family, present_family };
    b8 exclusive = (graphics_family == present_family);

    VkImageUsageFlags usage = impl->requested_usage
        ? impl->requested_usage
        : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if((usage & caps.supportedUsageFlags) != usage) {
        MAESTRO_LOGF_ERROR(g_logger, impl->pub._base.name,
            "unsupported swapchain image usage: requested=0x%x supported=0x%x",
            usage, caps.supportedUsageFlags);
        return HARP_RESULT_INVALID_ARGUMENTS;
    }

    /* The spec only guarantees that some composite alpha bit is supported. */
    static const VkCompositeAlphaFlagBitsKHR alpha_preference[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR
    };
    VkCompositeAlphaFlagBitsKHR composite_alpha = alpha_preference[0];
    for(uint32_t i = 0; i < sizeof(alpha_preference) / sizeof(alpha_preference[0]); ++i) {
        if(caps.supportedCompositeAlpha & alpha_preference[i]) {
            composite_alpha = alpha_preference[i];
            break;
        }
    }

    VkSwapchainCreateInfoKHR create_info = {
        .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface               = impl->surface,
        .minImageCount         = image_count,
        .imageFormat           = format.format,
        .imageColorSpace       = format.colorSpace,
        .imageExtent           = extent,
        .imageArrayLayers      = 1,
        .imageUsage            = usage,
        .imageSharingMode      = exclusive ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = exclusive ? 0 : 2,
        .pQueueFamilyIndices   = exclusive ? NULL : queue_indices,
        .preTransform          = caps.currentTransform,
        .compositeAlpha        = composite_alpha,
        .presentMode           = present_mode,
        .clipped               = VK_TRUE,
        .oldSwapchain          = impl->pub.swapchain
    };

    VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
    if(vkCreateSwapchainKHR(device->device, &create_info, NULL, &new_swapchain) != VK_SUCCESS)
        return HARP_RESULT_FAILED;

    uint32_t new_count = 0;
    vkGetSwapchainImagesKHR(device->device, new_swapchain, &new_count, NULL);

    VkImage *new_images = malloc(new_count * sizeof(VkImage));
    if(!new_images) {
        vkDestroySwapchainKHR(device->device, new_swapchain, NULL);
        return HARP_RESULT_OUT_OF_MEMORY;
    }
    vkGetSwapchainImagesKHR(device->device, new_swapchain, &new_count, new_images);

    VkImageView *new_views = malloc(new_count * sizeof(VkImageView));
    if(!new_views) {
        free(new_images);
        vkDestroySwapchainKHR(device->device, new_swapchain, NULL);
        return HARP_RESULT_OUT_OF_MEMORY;
    }

    for(uint32_t i = 0; i < new_count; ++i) {
        VkImageViewCreateInfo view_info = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = new_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = format.format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1
            }
        };
        if(vkCreateImageView(device->device, &view_info, NULL, &new_views[i]) != VK_SUCCESS) {
            for(uint32_t j = 0; j < i; ++j)
                vkDestroyImageView(device->device, new_views[j], NULL);
            free(new_views);
            free(new_images);
            vkDestroySwapchainKHR(device->device, new_swapchain, NULL);
            return HARP_RESULT_FAILED;
        }
    }

    /* New resources ready, destroy old ones. */
    for(uint32_t i = 0; i < impl->pub.image_count; ++i)
        vkDestroyImageView(device->device, impl->pub.views[i], NULL);
    free(impl->pub.views);
    free(impl->pub.images);
    if(impl->pub.swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device->device, impl->pub.swapchain, NULL);

    impl->pub.swapchain       = new_swapchain;
    impl->pub.images          = new_images;
    impl->pub.views           = new_views;
    impl->pub.image_count     = new_count;
    impl->pub.format          = format.format;
    impl->pub.color_space     = format.colorSpace;
    impl->pub.extent          = extent;
    impl->pub.present_mode    = present_mode;
    impl->pub.usage           = usage;
    impl->pub.composite_alpha = composite_alpha;

    return HARP_RESULT_OK;
}

HarpResult swapchain_acquire(
    MaestroVulkanSwapchainHandler *h,
    VkSemaphore signal_semaphore,
    uint32_t *out_image_index,
    b8 *out_suboptimal)
{
    HARP_CHECK_STATE(HARP_HANDLER_IS_VALID(h), HARP_RESULT_INVALID_STATE);
    MaestroVulkanSwapchainHandlerImpl *impl = (MaestroVulkanSwapchainHandlerImpl *)h;

    if(out_suboptimal)
        *out_suboptimal = 0;

    VkResult res = vkAcquireNextImageKHR(
        impl->device, h->swapchain, UINT64_MAX,
        signal_semaphore, VK_NULL_HANDLE, out_image_index);

    if(res == VK_SUBOPTIMAL_KHR) {
        if(out_suboptimal)
            *out_suboptimal = 1;
        return HARP_RESULT_OK;
    }
    if(res == VK_ERROR_OUT_OF_DATE_KHR)
        return HARP_RESULT_FAILED;
    if(res != VK_SUCCESS)
        return HARP_RESULT_CRITICAL_FAIL;
    return HARP_RESULT_OK;
}

HarpResult swapchain_present(
    MaestroVulkanSwapchainHandler *h,
    VkQueue queue,
    VkSemaphore wait_semaphore,
    uint32_t image_index,
    b8 *out_suboptimal)
{
    HARP_CHECK_STATE(HARP_HANDLER_IS_VALID(h), HARP_RESULT_INVALID_STATE);

    if(out_suboptimal)
        *out_suboptimal = 0;

    VkPresentInfoKHR info = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = wait_semaphore != VK_NULL_HANDLE ? 1 : 0,
        .pWaitSemaphores    = wait_semaphore != VK_NULL_HANDLE ? &wait_semaphore : NULL,
        .swapchainCount     = 1,
        .pSwapchains        = &h->swapchain,
        .pImageIndices      = &image_index,
        .pResults           = NULL
    };
    VkResult res = vkQueuePresentKHR(queue, &info);

    /* VK_SUBOPTIMAL_KHR is a success code: the image was presented. */
    if(res == VK_SUBOPTIMAL_KHR) {
        if(out_suboptimal)
            *out_suboptimal = 1;
        return HARP_RESULT_OK;
    }
    if(res == VK_ERROR_OUT_OF_DATE_KHR)
        return HARP_RESULT_FAILED;
    if(res != VK_SUCCESS)
        return HARP_RESULT_CRITICAL_FAIL;
    return HARP_RESULT_OK;
}

HarpResult swapchain_recreate(
    MaestroVulkanSwapchainHandler *h,
    MaestroVulkanDeviceActor *device,
    uint32_t width,
    uint32_t height,
    VkPresentModeKHR present_mode)
{
    HARP_CHECK_STATE(HARP_HANDLER_IS_VALID(h), HARP_RESULT_INVALID_STATE);
    MaestroVulkanSwapchainHandlerImpl *impl = (MaestroVulkanSwapchainHandlerImpl *)h;

    vkDeviceWaitIdle(device->device);

    /* Validate requested present mode is still supported; fall back to FIFO. */
    uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device->physical_device, impl->surface, &mode_count, NULL);
    VkPresentModeKHR *modes = malloc(mode_count * sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(device->physical_device, impl->surface, &mode_count, modes);

    VkPresentModeKHR chosen = VK_PRESENT_MODE_FIFO_KHR;
    for(uint32_t i = 0; i < mode_count; ++i) {
        if(modes[i] == present_mode) { chosen = present_mode; break; }
    }
    free(modes);

    VkSurfaceFormatKHR format = { h->format, h->color_space };
    return swapchain_build(impl, device, width, height, format, chosen);
}

HarpResult init_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator_base)
{
    MaestroVulkanSwapchainHandlerImpl *impl = (MaestroVulkanSwapchainHandlerImpl *)base;

    impl->pub.swapchain   = VK_NULL_HANDLE;
    impl->pub.images      = NULL;
    impl->pub.views       = NULL;
    impl->pub.image_count = 0;
    impl->surface         = VK_NULL_HANDLE;
    impl->device          = VK_NULL_HANDLE;
    impl->physical_device = VK_NULL_HANDLE;
    impl->requested_usage = 0;

    if(creator_base->flags & HARP_CREATOR_FLAG_DEFAULT_CREATOR)
        return HARP_RESULT_FAILED;

    MaestroVulkanSwapchainCreator *creator = (MaestroVulkanSwapchainCreator *)creator_base;
    MaestroVulkanDeviceActor *device = creator->device;
    VkSurfaceKHR surface             = creator->surface;
    uint32_t width                   = creator->width;
    uint32_t height                  = creator->height;
    VkSurfaceFormatKHR format        = select_format_preferred(device->physical_device, surface, creator->preferred_format);
    VkPresentModeKHR present_mode    = select_present_mode_preferred(device->physical_device, surface, creator->preferred_present_mode);

    impl->surface         = surface;
    impl->device          = device->device;
    impl->physical_device = device->physical_device;
    impl->requested_usage = creator->image_usage;

    HarpResult res = swapchain_build(impl, device, width, height, format, present_mode);
    if(res != HARP_RESULT_OK)
        return res;

    MAESTRO_LOGF_INFO(g_logger, base->name,
        "swapchain created: %ux%u  fmt=%u  mode=%u  images=%u  usage=0x%x  alpha=0x%x",
        impl->pub.extent.width, impl->pub.extent.height,
        impl->pub.format, impl->pub.present_mode, impl->pub.image_count,
        impl->pub.usage, impl->pub.composite_alpha);

    return HARP_RESULT_OK;
}
HarpResult term_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base)
{
    HARP_UNUSED(core_handler);
    MaestroVulkanSwapchainHandlerImpl *impl = (MaestroVulkanSwapchainHandlerImpl *)base;

    for(uint32_t i = 0; i < impl->pub.image_count; ++i)
        vkDestroyImageView(impl->device, impl->pub.views[i], NULL);

    free(impl->pub.views);
    free(impl->pub.images);
    impl->pub.views       = NULL;
    impl->pub.images      = NULL;
    impl->pub.image_count = 0;

    if(impl->pub.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(impl->device, impl->pub.swapchain, NULL);
        impl->pub.swapchain = VK_NULL_HANDLE;
    }

    return HARP_RESULT_OK;
}

HarpResult patch_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);
    HARP_UNUSED(base);

    // swap-time registration refreshed acquire/present/recreate; the rest is
    // Vulkan handles and data
    return HARP_RESULT_OK;
}
