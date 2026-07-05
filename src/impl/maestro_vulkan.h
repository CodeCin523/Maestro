#ifndef IMPL_MAESTRO_VULKAN_H
#define IMPL_MAESTRO_VULKAN_H

#include <maestro/maestro_vulkan.h>
#include <maestro/maestro_logger.h>


typedef struct MaestroVulkanCoreHandlerImpl {
    MaestroVulkanCoreHandler pub;

    MaestroLoggerHandler *logger;

    VkPhysicalDevice *devices;
    uint32_t device_count;
} MaestroVulkanCoreHandlerImpl;

typedef struct MaestroVulkanDeviceActorImpl {
    MaestroVulkanDeviceActor pub;

    MaestroLoggerHandler *logger;
} MaestroVulkanDeviceActorImpl;

typedef struct MaestroVulkanSwapchainHandlerImpl {
    MaestroVulkanSwapchainHandler pub;
    MaestroLoggerHandler *logger;
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkSurfaceKHR surface;
} MaestroVulkanSwapchainHandlerImpl;


int32_t default_device_score(VkPhysicalDevice device);

HarpResult vulkan_create_buffer(MaestroVulkanDeviceActor *device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_props, VkBuffer *out_buffer, VkDeviceMemory *out_memory);
void vulkan_destroy_buffer(MaestroVulkanDeviceActor *device, VkBuffer buffer, VkDeviceMemory memory);


HarpResult init_vulkan_instance(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_vulkan_instance(HarpCoreHandler *core_handler, HarpHandlerBase *base);


HarpResult create_vulkan_device(HarpCoreHandler *core_handler, HarpActorBase *base, HarpCreatorBase *creator);
HarpResult destroy_vulkan_device(HarpCoreHandler *core_handler, HarpActorBase *base);


HarpResult swapchain_acquire(MaestroVulkanSwapchainHandler *h, VkSemaphore signal_semaphore, uint32_t *out_image_index);
HarpResult swapchain_present(MaestroVulkanSwapchainHandler *h, VkQueue queue, VkSemaphore wait_semaphore, uint32_t image_index);
HarpResult swapchain_recreate(MaestroVulkanSwapchainHandler *h, MaestroVulkanDeviceActor *device, uint32_t width, uint32_t height, VkPresentModeKHR present_mode);

HarpResult init_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_vulkan_swapchain(HarpCoreHandler *core_handler, HarpHandlerBase *base);


#endif /* IMPL_MAESTRO_VULKAN_H */
