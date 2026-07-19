#ifndef IMPL_MAESTRO_VULKAN_SWAPCHAIN_H
#define IMPL_MAESTRO_VULKAN_SWAPCHAIN_H

#include <maestro/maestro_vulkan_swapchain.h>


typedef struct MaestroVulkanSwapchainHandlerImpl {
    MaestroVulkanSwapchainHandler pub;
    MaestroVulkanDeviceActor *device_actor; /* bound at init, outlives the swapchain */
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkSurfaceKHR surface;
    VkImageUsageFlags requested_usage; /* as passed to the creator, 0 = default */
} MaestroVulkanSwapchainHandlerImpl;


HarpResult swapchain_acquire(MaestroVulkanSwapchainHandler *h, VkSemaphore signal_semaphore, u64 timeout_ns, u32 *out_image_index);
HarpResult swapchain_present(MaestroVulkanSwapchainHandler *h, VkQueue queue, u32 image_index);
HarpResult swapchain_recreate(MaestroVulkanSwapchainHandler *h, u32 width, u32 height, VkPresentModeKHR present_mode);
HarpResult swapchain_set_present_mode(MaestroVulkanSwapchainHandler *h, VkPresentModeKHR present_mode);

HarpResult init_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base);
HarpResult patch_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base);


#endif /* IMPL_MAESTRO_VULKAN_SWAPCHAIN_H */
