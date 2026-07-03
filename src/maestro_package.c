#include <maestro/maestro.h>

#include "impl/maestro_logger.h"
#include "impl/maestro_window.h"
#include "impl/maestro_vulkan.h"

#include "maestro_names.h"

#include <harp/harp.h>

#include <harp/utils/harp_helpers.h>

#include <stdalign.h>


/* ================================================================================ */
/*  NAMES                                                                           */
/* ================================================================================ */

const char * const maestro_name_logger = MAESTRO_LOGGER_HANDLER_NAME;
const char * const maestro_name_window = MAESTRO_WINDOW_HANDLER_NAME;
const char * const maestro_name_vulkan_instance = MAESTRO_VULKAN_INSTANCE_HANDLER_NAME;
const char * const maestro_name_vulkan_device = MAESTRO_VULKAN_DEVICE_ACTOR_NAME;


/* ================================================================================ */
/*  PACKAGE REGISTER                                                                */
/* ================================================================================ */

HarpResult maestro_register(HarpCoreHandler *core) {
    HarpResult res                  = HARP_RESULT_OK;
    HarpHandlerBase *handler_base   = NULL;
    HarpHandlerDesc handler_desc    = {0};
    HarpActorDesc actor_desc        = {0};

    HarpDependencyDesc deps[] = {
        {maestro_name_logger,          0, UINT32_MAX},
        {maestro_name_window,          0, UINT32_MAX},
        {maestro_name_vulkan_instance, 0, UINT32_MAX}
    };

    // LOGGER_HANDLER
    handler_desc = (HarpHandlerDesc) {
        .name               = maestro_name_logger,
        .version            = MAESTRO_LOGGER_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroLoggerHandlerImpl),
        .instance_alignment = alignof(MaestroLoggerHandlerImpl),
        .pfn_init           = init_logger,
        .pfn_term           = term_logger,
        .p_dependencies     = NULL,
        .dependency_count   = 0
    };

    res = core->register_handler(core, &handler_desc, &handler_base);
    if(res != HARP_RESULT_OK)
        return res;

    MaestroLoggerHandler *logger = HARP_HANDLER_AS(MaestroLoggerHandler, handler_base);

    logger->log = logger_fallback_log;
    logger->logf = logger_fallback_logf;
    logger->flush = logger_flush;

    core->handler_set_serving(core, handler_base, 1);
    
    // WINDOW_HANDLER
    handler_desc = (HarpHandlerDesc) {
        .name               = maestro_name_window,
        .version            = MAESTRO_WINDOW_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroWindowHandlerImpl),
        .instance_alignment = alignof(MaestroWindowHandlerImpl),
        .pfn_init           = init_window,
        .pfn_term           = term_window,
        .p_dependencies     = NULL,
        .dependency_count   = 0
    };

    res = core->register_handler(core, &handler_desc, &handler_base);
    if(res != HARP_RESULT_OK)
        return res;

    MaestroWindowHandler *window = HARP_HANDLER_AS(MaestroWindowHandler, handler_base);

    window->pump_messages         = window_pump_messages;
    window->set_mouse_capture     = window_set_mouse_capture;
    window->set_cursor_visible    = window_set_cursor_visible;
    window->set_title             = window_set_title;
    window->set_title_extension   = window_set_title_extension;
    window->set_size              = window_set_size;
    window->set_position          = window_set_position;
    window->set_fullscreen        = window_set_fullscreen;
    window->request_attention     = window_request_attention;
    window->get_vulkan_extensions = window_get_vulkan_extensions;
    window->create_vulkan_surface = window_create_vulkan_surface;

    core->handler_set_serving(core, handler_base, 1);

    // VULKAN_INSTANCE_HANDLER
    handler_desc = (HarpHandlerDesc) {
        .name               = maestro_name_vulkan_instance,
        .version            = MAESTRO_VULKAN_INSTANCE_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroVulkanInstanceHandlerImpl),
        .instance_alignment = alignof(MaestroVulkanInstanceHandlerImpl),
        .pfn_init           = init_vulkan_instance,
        .pfn_term           = term_vulkan_instance,
        .p_dependencies     = NULL,
        .dependency_count   = 0
    };

    res = core->register_handler(core, &handler_desc, &handler_base);
    if(res != HARP_RESULT_OK)
        return res;

    MaestroVulkanInstanceHandler *vulkan_instance = HARP_HANDLER_AS(MaestroVulkanInstanceHandler, handler_base);

    vulkan_instance->pfn_default_device_score = default_device_score;

    core->handler_set_serving(core, handler_base, 1);
    
    // VULKAN_DEVICE_ACTOR
    actor_desc = (HarpActorDesc) {
        .name = maestro_name_vulkan_device,
        .version = MAESTRO_VULKAN_DEVICE_ACTOR_VERSION,
        .instance_size = sizeof(MaestroVulkanDeviceActorImpl),
        .instance_alignment = alignof(MaestroVulkanDeviceActorImpl),
        .pfn_create = create_vulkan_device,
        .pfn_destroy = destroy_vulkan_device,
        .parent_handler = deps[2]
    };

    res = core->register_actor(core, &actor_desc);
    if(res != HARP_RESULT_OK)
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
