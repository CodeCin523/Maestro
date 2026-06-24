#ifndef IMPL_MAESTRO_VULKAN_H
#define IMPL_MAESTRO_VULKAN_H

#include <maestro/maestro_vulkan.h>
#include <maestro/maestro_logger.h>


typedef struct MaestroVulkanInstanceHandlerImpl {
    MaestroVulkanInstanceHandler pub;

    MaestroLoggerHandler *logger;

    VkPhysicalDevice *devices;
    uint32_t device_count;
} MaestroVulkanInstanceHandlerImpl;

typedef struct MaestroVulkanDeviceActorImpl {
    MaestroVulkanDeviceActor pub;

    MaestroLoggerHandler *logger;

    VkQueue  graphics_queue;
    uint32_t graphics_family;

    VkQueue  compute_queue;
    uint32_t compute_family;

    VkQueue  transfer_queue;
    uint32_t transfer_family;
} MaestroVulkanDeviceActorImpl;


int32_t default_device_score(VkPhysicalDevice device);


HarpResult init_vulkan_instance(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_vulkan_instance(HarpCoreHandler *core_handler, HarpHandlerBase *base);


HarpResult create_vulkan_device(HarpCoreHandler *core_handler, HarpActorBase *base, HarpCreatorBase *creator);
HarpResult destroy_vulkan_device(HarpCoreHandler *core_handler, HarpActorBase *base);


#endif /* IMPL_MAESTRO_VULKAN_H */
