#ifndef IMPL_MAESTRO_VULKAN_H
#define IMPL_MAESTRO_VULKAN_H

#include <maestro/maestro_vulkan.h>


typedef struct MaestroVulkanCoreHandlerImpl {
    MaestroVulkanCoreHandler pub;

    VkPhysicalDevice *devices;
    u32 device_count;
} MaestroVulkanCoreHandlerImpl;

typedef struct MaestroVulkanSwapchainHandlerImpl {
    MaestroVulkanSwapchainHandler pub;
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkSurfaceKHR surface;
    VkImageUsageFlags requested_usage; /* as passed to the creator, 0 = default */
} MaestroVulkanSwapchainHandlerImpl;


i32 default_device_score(VkPhysicalDevice device);

HarpResult vulkan_create_buffer_unbound(MaestroVulkanDeviceActor *device, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *out_buffer, VkMemoryRequirements *out_reqs);
HarpResult vulkan_bind_buffer(MaestroVulkanDeviceActor *device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset);
void vulkan_destroy_buffer(MaestroVulkanDeviceActor *device, VkBuffer buffer);

HarpResult vulkan_create_image_unbound(MaestroVulkanDeviceActor *device, const MaestroVulkanImageDesc *desc, VkImage *out_image, VkMemoryRequirements *out_reqs);
HarpResult vulkan_finish_image(MaestroVulkanDeviceActor *device, const MaestroVulkanImageDesc *desc, VkImage image, VkDeviceMemory memory, VkDeviceSize offset, MaestroVulkanImage *out);
void vulkan_destroy_image(MaestroVulkanDeviceActor *device, MaestroVulkanImage *image);

u32 vulkan_find_memory_type(MaestroVulkanDeviceActor *device, u32 type_bits, VkMemoryPropertyFlags props);
HarpResult vulkan_alloc_memory(MaestroVulkanDeviceActor *device, VkDeviceSize size, u32 memory_type_index, VkDeviceMemory *out);
void vulkan_free_memory(MaestroVulkanDeviceActor *device, VkDeviceMemory memory);


HarpResult init_vulkan_instance(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_vulkan_instance(HarpCoreHandler *core_handler, HarpHandlerBase *base);
HarpResult patch_vulkan_instance(HarpCoreHandler *core_handler, HarpHandlerBase *base);


HarpResult create_vulkan_device(HarpCoreHandler *core_handler, HarpActorBase *base, HarpCreatorBase *creator);
HarpResult destroy_vulkan_device(HarpCoreHandler *core_handler, HarpActorBase *base);
HarpResult patch_vulkan_device(HarpCoreHandler *core_handler, HarpActorBase *base);


HarpResult swapchain_acquire(MaestroVulkanSwapchainHandler *h, VkSemaphore signal_semaphore, u32 *out_image_index, b8 *out_suboptimal);
HarpResult swapchain_present(MaestroVulkanSwapchainHandler *h, VkQueue queue, VkSemaphore wait_semaphore, u32 image_index, b8 *out_suboptimal);
HarpResult swapchain_recreate(MaestroVulkanSwapchainHandler *h, MaestroVulkanDeviceActor *device, u32 width, u32 height, VkPresentModeKHR present_mode);

HarpResult init_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base);
HarpResult patch_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base);


#endif /* IMPL_MAESTRO_VULKAN_H */
