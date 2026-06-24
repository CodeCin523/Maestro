#include <maestro/maestro.h>
#include "impl/maestro_logger.h"
#include "impl/maestro_window.h"
#include "impl/maestro_vulkan.h"

#include <harp/harp.h>

#include <harp/utils/harp_helpers.h>

#include <stdalign.h>


HarpResult maestro_register(HarpCoreHandler *core) {
    HarpResult res = HARP_RESULT_OK;


    static HarpHandlerDesc handler_desc = {
        .name               = MAESTRO_LOGGER_HANDLER_NAME,
        .version            = MAESTRO_LOGGER_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroLoggerHandlerImpl),
        .instance_alignment = alignof(MaestroLoggerHandlerImpl),
        .pfn_init           = init_logger,
        .pfn_term           = term_logger,
        .p_dependencies     = NULL,
        .dependency_count   = 0
    };

    res = core->register_handler(core, &handler_desc);
    if(HARP_FAILED(res))
        return res;

    HarpDependencyDesc dep_desc = HARP_DEPENDENCY(MAESTRO_LOGGER_HANDLER_NAME, 0, UINT32_MAX);
    HarpHandlerBase *handler_base = NULL;

    res = core->get_handler(core, &dep_desc, &handler_base);
    if(HARP_FAILED(res))
        return res;

    MaestroLoggerHandlerImpl *logger = HARP_HANDLER_AS(MaestroLoggerHandlerImpl, handler_base);

    logger->pub.log = logger_fallback_log;
    logger->pub.logf = logger_fallback_logf;
    logger->pub.flush = logger_flush;

    core->handler_set_serving(core, handler_base, 1);


    handler_desc = (HarpHandlerDesc) {
        .name               = MAESTRO_WINDOW_HANDLER_NAME,
        .version            = MAESTRO_WINDOW_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroWindowHandlerImpl),
        .instance_alignment = alignof(MaestroWindowHandlerImpl),
        .pfn_init           = init_window,
        .pfn_term           = term_window,
        .p_dependencies     = NULL,
        .dependency_count   = 0
    };

    res = core->register_handler(core, &handler_desc);
    if(HARP_FAILED(res))
        return res;

    dep_desc = HARP_DEPENDENCY(MAESTRO_WINDOW_HANDLER_NAME, 0, UINT32_MAX);

    res = core->get_handler(core, &dep_desc, &handler_base);
    if(HARP_FAILED(res))
        return res;

    MaestroWindowHandlerImpl *window = HARP_HANDLER_AS(MaestroWindowHandlerImpl, handler_base);

    window->pub.pump_messages = window_pump_messages;
    window->pub.get_vulkan_extensions = window_get_vulkan_extensions;

    core->handler_set_serving(core, handler_base, 1);


    static HarpHandlerDesc vulkan_instance_desc = {
        .name               = MAESTRO_VULKAN_INSTANCE_HANDLER_NAME,
        .version            = MAESTRO_VULKAN_INSTANCE_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroVulkanInstanceHandlerImpl),
        .instance_alignment = alignof(MaestroVulkanInstanceHandlerImpl),
        .pfn_init           = init_vulkan_instance,
        .pfn_term           = term_vulkan_instance,
        .p_dependencies     = NULL,
        .dependency_count   = 0
    };

    res = core->register_handler(core, &vulkan_instance_desc);
    if(HARP_FAILED(res))
        return res;

    static HarpActorDesc vulkan_device_desc = {
        .name               = MAESTRO_VULKAN_DEVICE_ACTOR_NAME,
        .version            = MAESTRO_VULKAN_DEVICE_ACTOR_VERSION,
        .instance_size      = sizeof(MaestroVulkanDeviceActorImpl),
        .instance_alignment = alignof(MaestroVulkanDeviceActorImpl),
        .pfn_create         = create_vulkan_device,
        .pfn_destroy        = destroy_vulkan_device,
        .parent_handler     = MAESTRO_VULKAN_INSTANCE_HANDLER_NAME
    };

    res = core->register_actor(core, &vulkan_device_desc);
    if(HARP_FAILED(res))
        return res;


    return HARP_RESULT_OK;
}

HarpResult harp_package_query(HarpPackageDesc **out_desc) {
    static HarpPackageDesc package_desc = {
        .name = MAESTRO_PACKAGE_NAME,
        .version = MAESTRO_PACKAGE_VERSION,

        .pfn_register = maestro_register,

        .p_dependencies = NULL,
        .dependency_count = 0
    };

    if(out_desc == NULL)
        return HARP_RESULT_MISSING_OUTPUT;

    *out_desc = &package_desc;

    return HARP_RESULT_OK;
}
