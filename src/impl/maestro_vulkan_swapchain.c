#include "impl/maestro_vulkan_swapchain.h"

#include "maestro_globals.h"

#include <harp/utils/harp_helpers.h>

#include <stdlib.h>


/* ================================================================================ */
/*  SWAPCHAIN SELECTION                                                             */
/* ================================================================================ */

static VkSurfaceFormatKHR select_format_preferred(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface,
    VkFormat preferred)
{
    u32 count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, NULL);
    VkSurfaceFormatKHR *formats = malloc(count * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, formats);

    VkSurfaceFormatKHR result = formats[0];
    for(u32 i = 0; i < count; ++i) {
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
    u32 count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, NULL);
    VkPresentModeKHR *modes = malloc(count * sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, modes);

    VkPresentModeKHR result = VK_PRESENT_MODE_FIFO_KHR;
    for(u32 i = 0; i < count; ++i) {
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
    u32 width,
    u32 height,
    VkSurfaceFormatKHR format,
    VkPresentModeKHR present_mode)
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physical_device, impl->surface, &caps);

    u32 image_count = caps.minImageCount + 1;
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

    u32 graphics_family = UINT32_MAX;
    u32 present_family  = UINT32_MAX;
    for(u32 i = 0; i < device->queue_count; ++i) {
        if((device->queues[i].flags & VK_QUEUE_GRAPHICS_BIT) && graphics_family == UINT32_MAX)
            graphics_family = device->queues[i].family;
        if(device->queues[i].supports_present && present_family == UINT32_MAX)
            present_family = device->queues[i].family;
    }

    u32 queue_indices[2] = { graphics_family, present_family };
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
    for(u32 i = 0; i < sizeof(alpha_preference) / sizeof(alpha_preference[0]); ++i) {
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

    u32 new_count = 0;
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

    for(u32 i = 0; i < new_count; ++i) {
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
            for(u32 j = 0; j < i; ++j)
                vkDestroyImageView(device->device, new_views[j], NULL);
            free(new_views);
            free(new_images);
            vkDestroySwapchainKHR(device->device, new_swapchain, NULL);
            return HARP_RESULT_FAILED;
        }
    }

    /* One render-finished semaphore per image; present waits on them, so
       their lifetime is exactly the image array's. */
    VkSemaphore *new_sems = malloc(new_count * sizeof(VkSemaphore));
    if(!new_sems) {
        for(u32 i = 0; i < new_count; ++i)
            vkDestroyImageView(device->device, new_views[i], NULL);
        free(new_views);
        free(new_images);
        vkDestroySwapchainKHR(device->device, new_swapchain, NULL);
        return HARP_RESULT_OUT_OF_MEMORY;
    }
    VkSemaphoreCreateInfo sem_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for(u32 i = 0; i < new_count; ++i) {
        if(vkCreateSemaphore(device->device, &sem_info, NULL, &new_sems[i]) != VK_SUCCESS) {
            for(u32 j = 0; j < i; ++j)
                vkDestroySemaphore(device->device, new_sems[j], NULL);
            for(u32 j = 0; j < new_count; ++j)
                vkDestroyImageView(device->device, new_views[j], NULL);
            free(new_sems);
            free(new_views);
            free(new_images);
            vkDestroySwapchainKHR(device->device, new_swapchain, NULL);
            return HARP_RESULT_FAILED;
        }
    }

    /* New resources ready, destroy old ones. */
    for(u32 i = 0; i < impl->pub.image_count; ++i) {
        vkDestroyImageView(device->device, impl->pub.views[i], NULL);
        vkDestroySemaphore(device->device, impl->pub.render_sems[i], NULL);
    }
    free(impl->pub.views);
    free(impl->pub.images);
    free(impl->pub.render_sems);
    if(impl->pub.swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device->device, impl->pub.swapchain, NULL);

    impl->pub.swapchain       = new_swapchain;
    impl->pub.images          = new_images;
    impl->pub.views           = new_views;
    impl->pub.render_sems     = new_sems;
    impl->pub.image_count     = new_count;
    impl->pub.format          = format.format;
    impl->pub.color_space     = format.colorSpace;
    impl->pub.extent          = extent;
    impl->pub.present_mode    = present_mode;
    impl->pub.usage           = usage;
    impl->pub.composite_alpha = composite_alpha;

    impl->pub.generation++;
    impl->pub.needs_recreate = 0;

    return HARP_RESULT_OK;
}

HarpResult swapchain_acquire(
    MaestroVulkanSwapchainHandler *h,
    VkSemaphore signal_semaphore,
    u64 timeout_ns,
    u32 *out_image_index)
{
    HARP_CHECK_STATE(HARP_HANDLER_IS_VALID(h), HARP_RESULT_INVALID_STATE);
    MaestroVulkanSwapchainHandlerImpl *impl = (MaestroVulkanSwapchainHandlerImpl *)h;

    VkResult res = vkAcquireNextImageKHR(
        impl->device, h->swapchain, timeout_ns,
        signal_semaphore, VK_NULL_HANDLE, out_image_index);

    if(res == VK_TIMEOUT || res == VK_NOT_READY)
        return HARP_RESULT_INVALID_STATE; /* no image yet, retry */
    if(res == VK_SUBOPTIMAL_KHR) {
        h->needs_recreate = 1;
        return HARP_RESULT_OK;
    }
    if(res == VK_ERROR_OUT_OF_DATE_KHR) {
        h->needs_recreate = 1;
        return HARP_RESULT_FAILED;
    }
    if(res != VK_SUCCESS)
        return HARP_RESULT_CRITICAL_FAIL;
    return HARP_RESULT_OK;
}

HarpResult swapchain_present(
    MaestroVulkanSwapchainHandler *h,
    VkQueue queue,
    u32 image_index)
{
    HARP_CHECK_STATE(HARP_HANDLER_IS_VALID(h), HARP_RESULT_INVALID_STATE);
    HARP_CHECK_ARG(image_index < h->image_count, HARP_RESULT_INVALID_ARGUMENTS);

    if(queue == VK_NULL_HANDLE)
        queue = h->present_queue;

    VkPresentInfoKHR info = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &h->render_sems[image_index],
        .swapchainCount     = 1,
        .pSwapchains        = &h->swapchain,
        .pImageIndices      = &image_index,
        .pResults           = NULL
    };
    VkResult res = vkQueuePresentKHR(queue, &info);

    /* VK_SUBOPTIMAL_KHR is a success code: the image was presented. */
    if(res == VK_SUBOPTIMAL_KHR) {
        h->needs_recreate = 1;
        return HARP_RESULT_OK;
    }
    if(res == VK_ERROR_OUT_OF_DATE_KHR) {
        h->needs_recreate = 1;
        return HARP_RESULT_FAILED;
    }
    if(res != VK_SUCCESS)
        return HARP_RESULT_CRITICAL_FAIL;
    return HARP_RESULT_OK;
}

HarpResult swapchain_recreate(
    MaestroVulkanSwapchainHandler *h,
    u32 width,
    u32 height,
    VkPresentModeKHR present_mode)
{
    HARP_CHECK_STATE(HARP_HANDLER_IS_VALID(h), HARP_RESULT_INVALID_STATE);
    MaestroVulkanSwapchainHandlerImpl *impl = (MaestroVulkanSwapchainHandlerImpl *)h;
    MaestroVulkanDeviceActor *device = impl->device_actor;

    vkDeviceWaitIdle(device->device);

    /* Validate requested present mode is still supported; fall back to FIFO. */
    u32 mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device->physical_device, impl->surface, &mode_count, NULL);
    VkPresentModeKHR *modes = malloc(mode_count * sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(device->physical_device, impl->surface, &mode_count, modes);

    VkPresentModeKHR chosen = VK_PRESENT_MODE_FIFO_KHR;
    for(u32 i = 0; i < mode_count; ++i) {
        if(modes[i] == present_mode) { chosen = present_mode; break; }
    }
    free(modes);

    VkSurfaceFormatKHR format = { h->format, h->color_space };
    return swapchain_build(impl, device, width, height, format, chosen);
}

HarpResult swapchain_set_present_mode(MaestroVulkanSwapchainHandler *h, VkPresentModeKHR present_mode) {
    HARP_CHECK_STATE(HARP_HANDLER_IS_VALID(h), HARP_RESULT_INVALID_STATE);

    if(present_mode == h->present_mode)
        return HARP_RESULT_OK;
    return swapchain_recreate(h, h->extent.width, h->extent.height, present_mode);
}

HarpResult init_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator_base) {
    MaestroVulkanSwapchainHandlerImpl *impl = (MaestroVulkanSwapchainHandlerImpl *)base;

    impl->pub.swapchain   = VK_NULL_HANDLE;
    impl->pub.images      = NULL;
    impl->pub.views       = NULL;
    impl->pub.render_sems = NULL;
    impl->pub.image_count = 0;
    impl->pub.present_queue = VK_NULL_HANDLE;
    impl->pub.needs_recreate = 0;
    impl->pub.generation = 0;
    impl->device_actor    = NULL;
    impl->surface         = VK_NULL_HANDLE;
    impl->device          = VK_NULL_HANDLE;
    impl->physical_device = VK_NULL_HANDLE;
    impl->requested_usage = 0;

    if(creator_base->flags & HARP_CREATOR_FLAG_DEFAULT_CREATOR)
        return HARP_RESULT_FAILED;

    MaestroVulkanSwapchainCreator *creator = (MaestroVulkanSwapchainCreator *)creator_base;
    MaestroVulkanDeviceActor *device = creator->device;
    VkSurfaceKHR surface             = creator->surface;
    u32 width                   = creator->width;
    u32 height                  = creator->height;
    VkSurfaceFormatKHR format        = select_format_preferred(device->physical_device, surface, creator->preferred_format);
    VkPresentModeKHR present_mode    = select_present_mode_preferred(device->physical_device, surface, creator->preferred_present_mode);

    for(u32 i = 0; i < device->queue_count; ++i) {
        if(device->queues[i].supports_present) {
            impl->pub.present_queue = device->queues[i].queue;
            break;
        }
    }
    if(impl->pub.present_queue == VK_NULL_HANDLE) {
        MAESTRO_LOG_ERROR(g_logger, base->name, "no queue of the device supports present");
        return HARP_RESULT_INVALID_ARGUMENTS;
    }

    impl->device_actor = device;
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
HarpResult term_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);
    MaestroVulkanSwapchainHandlerImpl *impl = (MaestroVulkanSwapchainHandlerImpl *)base;

    if(impl->device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(impl->device);

    for(u32 i = 0; i < impl->pub.image_count; ++i) {
        vkDestroyImageView(impl->device, impl->pub.views[i], NULL);
        vkDestroySemaphore(impl->device, impl->pub.render_sems[i], NULL);
    }

    free(impl->pub.views);
    free(impl->pub.images);
    free(impl->pub.render_sems);
    impl->pub.views       = NULL;
    impl->pub.images      = NULL;
    impl->pub.render_sems = NULL;
    impl->pub.image_count = 0;

    if(impl->pub.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(impl->device, impl->pub.swapchain, NULL);
        impl->pub.swapchain = VK_NULL_HANDLE;
    }

    impl->pub.present_queue = VK_NULL_HANDLE;
    impl->pub.needs_recreate = 0;
    impl->device_actor = NULL;

    return HARP_RESULT_OK;
}

HarpResult patch_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);
    HARP_UNUSED(base);

    // swap-time registration refreshed acquire/present/recreate; the rest is
    // Vulkan handles and data
    return HARP_RESULT_OK;
}
