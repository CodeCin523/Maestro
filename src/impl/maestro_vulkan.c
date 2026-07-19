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
static const u32 VALIDATION_LAYER_COUNT = sizeof(VALIDATION_LAYERS) / sizeof(VALIDATION_LAYERS[0]);


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

static VkImageAspectFlags image_aspect_for_format(VkFormat format) {
    switch(format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

static VkImageViewType image_view_type(VkImageType type, u32 layers) {
    switch(type) {
        case VK_IMAGE_TYPE_1D:
            return layers > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
        case VK_IMAGE_TYPE_3D:
            return VK_IMAGE_VIEW_TYPE_3D;
        case VK_IMAGE_TYPE_2D:
        default:
            return layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    }
}

HarpResult vulkan_create_buffer_unbound(
    MaestroVulkanDeviceActor *actor,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer *out_buffer,
    VkMemoryRequirements *out_reqs
) {
    HARP_CHECK_STATE(HARP_ACTOR_IS_VALID(actor), HARP_RESULT_INVALID_STATE);
    HARP_CHECK_ARG(out_buffer != NULL && out_reqs != NULL, HARP_RESULT_MISSING_OUTPUT);

    *out_buffer = VK_NULL_HANDLE;

    VkBufferCreateInfo buf_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if(vkCreateBuffer(actor->device, &buf_info, NULL, out_buffer) != VK_SUCCESS)
        return HARP_RESULT_FAILED;

    vkGetBufferMemoryRequirements(actor->device, *out_buffer, out_reqs);
    return HARP_RESULT_OK;
}

HarpResult vulkan_bind_buffer(
    MaestroVulkanDeviceActor *actor,
    VkBuffer buffer,
    VkDeviceMemory memory,
    VkDeviceSize offset
) {
    HARP_CHECK_STATE(HARP_ACTOR_IS_VALID(actor), HARP_RESULT_INVALID_STATE);
    HARP_CHECK_ARG(buffer != VK_NULL_HANDLE && memory != VK_NULL_HANDLE, HARP_RESULT_INVALID_ARGUMENTS);

    if(vkBindBufferMemory(actor->device, buffer, memory, offset) != VK_SUCCESS)
        return HARP_RESULT_FAILED;

    return HARP_RESULT_OK;
}

void vulkan_destroy_buffer(
    MaestroVulkanDeviceActor *actor,
    VkBuffer buffer
) {
    if(!HARP_ACTOR_IS_VALID(actor)) return;
    if(buffer != VK_NULL_HANDLE) vkDestroyBuffer(actor->device, buffer, NULL);
}

HarpResult vulkan_create_image_unbound(
    MaestroVulkanDeviceActor *actor,
    const MaestroVulkanImageDesc *desc,
    VkImage *out_image,
    VkMemoryRequirements *out_reqs
) {
    HARP_CHECK_STATE(HARP_ACTOR_IS_VALID(actor), HARP_RESULT_INVALID_STATE);
    HARP_CHECK_ARG(desc != NULL, HARP_RESULT_INVALID_ARGUMENTS);
    HARP_CHECK_ARG(out_image != NULL && out_reqs != NULL, HARP_RESULT_MISSING_OUTPUT);

    *out_image = VK_NULL_HANDLE;

    VkImageCreateInfo img_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = desc->type,
        .format = desc->format,
        .extent = desc->extent,
        .mipLevels = desc->mip_levels ? desc->mip_levels : 1,
        .arrayLayers = desc->array_layers ? desc->array_layers : 1,
        .samples = desc->samples ? desc->samples : VK_SAMPLE_COUNT_1_BIT,
        .tiling = desc->tiling,
        .usage = desc->usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    if(vkCreateImage(actor->device, &img_info, NULL, out_image) != VK_SUCCESS)
        return HARP_RESULT_FAILED;

    vkGetImageMemoryRequirements(actor->device, *out_image, out_reqs);
    return HARP_RESULT_OK;
}

HarpResult vulkan_finish_image(
    MaestroVulkanDeviceActor *actor,
    const MaestroVulkanImageDesc *desc,
    VkImage image,
    VkDeviceMemory memory,
    VkDeviceSize offset,
    MaestroVulkanImage *out
) {
    HARP_CHECK_STATE(HARP_ACTOR_IS_VALID(actor), HARP_RESULT_INVALID_STATE);
    HARP_CHECK_ARG(desc != NULL, HARP_RESULT_INVALID_ARGUMENTS);
    HARP_CHECK_ARG(image != VK_NULL_HANDLE && memory != VK_NULL_HANDLE, HARP_RESULT_INVALID_ARGUMENTS);
    HARP_CHECK_ARG(out != NULL, HARP_RESULT_MISSING_OUTPUT);

    out->image = VK_NULL_HANDLE;
    out->view = VK_NULL_HANDLE;

    if(vkBindImageMemory(actor->device, image, memory, offset) != VK_SUCCESS)
        return HARP_RESULT_FAILED;

    if(desc->make_view) {
        u32 layers = desc->array_layers ? desc->array_layers : 1;

        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = image_view_type(desc->type, layers),
            .format = desc->format,
            .subresourceRange = {
                .aspectMask = image_aspect_for_format(desc->format),
                .baseMipLevel = 0,
                .levelCount = desc->mip_levels ? desc->mip_levels : 1,
                .baseArrayLayer = 0,
                .layerCount = layers
            }
        };

        if(vkCreateImageView(actor->device, &view_info, NULL, &out->view) != VK_SUCCESS)
            return HARP_RESULT_FAILED;
    }

    out->image = image;
    return HARP_RESULT_OK;
}

void vulkan_destroy_image(
    MaestroVulkanDeviceActor *actor,
    MaestroVulkanImage *image
) {
    if(!HARP_ACTOR_IS_VALID(actor)) return;
    if(image == NULL) return;

    if(image->view != VK_NULL_HANDLE) vkDestroyImageView(actor->device, image->view, NULL);
    if(image->image != VK_NULL_HANDLE) vkDestroyImage(actor->device, image->image, NULL);
    image->view = VK_NULL_HANDLE;
    image->image = VK_NULL_HANDLE;
}

u32 vulkan_find_memory_type(
    MaestroVulkanDeviceActor *actor,
    u32 type_bits,
    VkMemoryPropertyFlags props
) {
    HARP_CHECK_STATE(HARP_ACTOR_IS_VALID(actor), UINT32_MAX);

    VkPhysicalDeviceMemoryProperties mem_dev;
    vkGetPhysicalDeviceMemoryProperties(actor->physical_device, &mem_dev);

    for(u32 i = 0; i < mem_dev.memoryTypeCount; ++i) {
        if((type_bits & (1u << i)) &&
           (mem_dev.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return UINT32_MAX;
}

HarpResult vulkan_alloc_memory(
    MaestroVulkanDeviceActor *actor,
    VkDeviceSize size,
    u32 memory_type_index,
    VkMemoryAllocateFlags flags,
    VkDeviceMemory *out
) {
    HARP_CHECK_STATE(HARP_ACTOR_IS_VALID(actor), HARP_RESULT_INVALID_STATE);
    HARP_CHECK_ARG(out != NULL, HARP_RESULT_MISSING_OUTPUT);
    HARP_CHECK_ARG(memory_type_index != UINT32_MAX, HARP_RESULT_INVALID_ARGUMENTS);

    *out = VK_NULL_HANDLE;

    VkMemoryAllocateFlagsInfo flags_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = flags
    };

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = flags ? &flags_info : NULL,
        .allocationSize = size,
        .memoryTypeIndex = memory_type_index
    };

    if(vkAllocateMemory(actor->device, &alloc_info, NULL, out) != VK_SUCCESS)
        return HARP_RESULT_FAILED;

    return HARP_RESULT_OK;
}

void vulkan_free_memory(
    MaestroVulkanDeviceActor *actor,
    VkDeviceMemory memory
) {
    if(!HARP_ACTOR_IS_VALID(actor)) return;
    if(memory != VK_NULL_HANDLE) vkFreeMemory(actor->device, memory, NULL);
}

i32 default_device_score(VkPhysicalDevice device) {
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
        u32 layer_count = 0;
        vkEnumerateInstanceLayerProperties(&layer_count, NULL);

        VkLayerProperties *available = malloc(layer_count * sizeof(VkLayerProperties));
        if(!available)
            return HARP_RESULT_OUT_OF_MEMORY;

        vkEnumerateInstanceLayerProperties(&layer_count, available);

        for(u32 i = 0; i < VALIDATION_LAYER_COUNT; ++i) {
            b8 found = 0;
            for(u32 j = 0; j < layer_count; ++j) {
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

    u32 ext_count = creator.extension_count;
    const char **extensions = malloc((ext_count + 1) * sizeof(const char *));
    if(!extensions)
        return HARP_RESULT_OUT_OF_MEMORY;

    for(u32 i = 0; i < ext_count; ++i)
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
    i32  best_score = 0;
    u32 best_index = 0;

    for(u32 i = 0; i < core->device_count; ++i) {
        i32 score = score_fn(core->devices[i]);
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
        u32 ext_count = 0;
        vkEnumerateDeviceExtensionProperties(best_device, NULL, &ext_count, NULL);

        VkExtensionProperties *exts = malloc(ext_count * sizeof(VkExtensionProperties));
        if(!exts)
            return HARP_RESULT_OUT_OF_MEMORY;

        vkEnumerateDeviceExtensionProperties(best_device, NULL, &ext_count, exts);

        b8 has_swapchain = 0;
        for(u32 i = 0; i < ext_count; ++i) {
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
    u32 family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(best_device, &family_count, NULL);

    VkQueueFamilyProperties *families = malloc(family_count * sizeof(VkQueueFamilyProperties));
    if(!families)
        return HARP_RESULT_OUT_OF_MEMORY;

    vkGetPhysicalDeviceQueueFamilyProperties(best_device, &family_count, families);

    /* Temporary queue list, unsorted. */
    MaestroVulkanQueue tmp[MAESTRO_VULKAN_MAX_QUEUES];
    u32 tmp_count = 0;

    for(u32 i = 0; i < family_count && tmp_count < MAESTRO_VULKAN_MAX_QUEUES; ++i) {
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
        for(u32 i = 0; i < tmp_count; ++i)
            if(tmp[i].supports_present) { found_present = 1; break; }

        if(!found_present) {
            MAESTRO_LOG_FATAL(g_logger, base->name, "No queue family supports present for this surface");
            return HARP_RESULT_FAILED;
        }
    }

    /* Insertion sort by priority: graphics+present > graphics > compute > transfer. */
    for(u32 i = 1; i < tmp_count; ++i) {
        MaestroVulkanQueue key = tmp[i];
        int key_prio = queue_priority(key.flags, key.supports_present);
        i32 j = (i32)i - 1;
        while(j >= 0 && queue_priority(tmp[j].flags, tmp[j].supports_present) > key_prio) {
            tmp[j + 1] = tmp[j];
            --j;
        }
        tmp[j + 1] = key;
    }

    /* Build unique VkDeviceQueueCreateInfo array. */
    static const f32 priority = 1.0f;
    VkDeviceQueueCreateInfo queue_infos[MAESTRO_VULKAN_MAX_QUEUES];
    for(u32 i = 0; i < tmp_count; ++i) {
        queue_infos[i] = (VkDeviceQueueCreateInfo){
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = tmp[i].family,
            .queueCount       = 1,
            .pQueuePriorities = &priority
        };
    }

    /* Merge user extensions with VK_KHR_swapchain when needed. */
    u32 total_ext_count = creator.extension_count;
    b8       needs_swapchain = (creator.surface != VK_NULL_HANDLE);
    if(needs_swapchain) total_ext_count++;

    const char **all_extensions = NULL;
    if(total_ext_count > 0) {
        all_extensions = malloc(total_ext_count * sizeof(const char *));
        if(!all_extensions)
            return HARP_RESULT_OUT_OF_MEMORY;

        for(u32 i = 0; i < creator.extension_count; ++i)
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
    for(u32 i = 0; i < tmp_count; ++i) {
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
