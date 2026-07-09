#include <maestro/maestro.h>

#include <harp/harp.h>
#include <harp/utils/harp_helpers.h>

#include "maestro_globals.h"
#include "impl/maestro_logger.h"
#include "impl/maestro_path.h"
#include "impl/maestro_window.h"
#include "impl/maestro_vulkan.h"

#include <stdalign.h>


/* ================================================================================ */
/*  GLOBALS                                                                         */
/* ================================================================================ */

const MaestroLoggerHandler *g_logger                    = NULL;
const MaestroPathHandler *g_path                        = NULL;
const MaestroVulkanCoreHandler *g_vulkan_core           = NULL;
const MaestroVulkanSwapchainHandler *g_vulkan_swapchain = NULL;
const MaestroWindowHandler *g_window                    = NULL;

const char * const g_logger_name            = MAESTRO_LOGGER_HANDLER_NAME;
const char * const g_path_name              = MAESTRO_PATH_HANDLER_NAME;
const char * const g_window_name            = MAESTRO_WINDOW_HANDLER_NAME;
const char * const g_vulkan_core_name       = MAESTRO_VULKAN_CORE_HANDLER_NAME;
const char * const g_vulkan_device_name     = MAESTRO_VULKAN_DEVICE_ACTOR_NAME;
const char * const g_vulkan_swapchain_name  = MAESTRO_VULKAN_SWAPCHAIN_HANDLER_NAME;


/* ================================================================================ */
/*  PACKAGE REGISTER                                                                */
/* ================================================================================ */

HarpResult maestro_register(HarpCoreHandler *core) {
    HarpResult res                  = HARP_RESULT_OK;
    HarpHandlerBase *handler_base   = NULL;
    HarpHandlerDesc handler_desc    = {0};
    HarpActorDesc actor_desc        = {0};


    // LOGGER_HANDLER
    handler_desc = (HarpHandlerDesc) {
        .name               = g_logger_name,
        .version            = MAESTRO_LOGGER_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroLoggerHandlerImpl),
        .instance_alignment = alignof(MaestroLoggerHandlerImpl),
        .pfn_init           = init_logger,
        .pfn_term           = term_logger,
        .pfn_patch          = patch_logger,
        .p_dependencies     = NULL,
        .dependency_count   = 0
    };

    res = core->register_handler(core, &handler_desc, &handler_base);
    if(res != HARP_RESULT_OK)
        return res;

    MaestroLoggerHandler *logger = HARP_HANDLER_AS(MaestroLoggerHandler, handler_base);

    logger->log   = logger_fallback_log;
    logger->logf  = logger_fallback_logf;
    logger->flush = logger_flush;

    core->handler_set_serving(core, handler_base, 1);

    // PATH_HANDLER
    handler_desc = (HarpHandlerDesc) {
        .name               = g_path_name,
        .version            = MAESTRO_PATH_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroPathHandlerImpl),
        .instance_alignment = alignof(MaestroPathHandlerImpl),
        .pfn_init           = init_path,
        .pfn_term           = term_path,
        .pfn_patch          = patch_path,
        .p_dependencies     = NULL,
        .dependency_count   = 0
    };

    res = core->register_handler(core, &handler_desc, &handler_base);
    if(res != HARP_RESULT_OK)
        return res;

    MaestroPathHandler *path = HARP_HANDLER_AS(MaestroPathHandler, handler_base);

    path->make      = path_make;
    path->makef     = path_makef;
    path->info      = path_info;
    path->enumerate = path_enumerate;

    core->handler_set_serving(core, handler_base, 1);

    // WINDOW_HANDLER
    handler_desc = (HarpHandlerDesc) {
        .name               = g_window_name,
        .version            = MAESTRO_WINDOW_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroWindowHandlerImpl),
        .instance_alignment = alignof(MaestroWindowHandlerImpl),
        .pfn_init           = init_window,
        .pfn_term           = term_window,
        .pfn_patch          = patch_window,
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

    // VULKAN_CORE_HANDLER
    handler_desc = (HarpHandlerDesc) {
        .name               = g_vulkan_core_name,
        .version            = MAESTRO_VULKAN_CORE_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroVulkanCoreHandlerImpl),
        .instance_alignment = alignof(MaestroVulkanCoreHandlerImpl),
        .pfn_init           = init_vulkan_instance,
        .pfn_term           = term_vulkan_instance,
        .pfn_patch          = patch_vulkan_instance,
        .p_dependencies     = NULL,
        .dependency_count   = 0
    };

    res = core->register_handler(core, &handler_desc, &handler_base);
    if(res != HARP_RESULT_OK)
        return res;

    MaestroVulkanCoreHandler *vulkan_core = HARP_HANDLER_AS(MaestroVulkanCoreHandler, handler_base);

    vulkan_core->pfn_default_device_score = default_device_score;
    vulkan_core->create_buffer            = vulkan_create_buffer;
    vulkan_core->destroy_buffer           = vulkan_destroy_buffer;

    core->handler_set_serving(core, handler_base, 1);

    // VULKAN_DEVICE_ACTOR
    actor_desc = (HarpActorDesc) {
        .name               = g_vulkan_device_name,
        .version            = MAESTRO_VULKAN_DEVICE_ACTOR_VERSION,
        .instance_size      = sizeof(MaestroVulkanDeviceActorImpl),
        .instance_alignment = alignof(MaestroVulkanDeviceActorImpl),
        .pfn_create         = create_vulkan_device,
        .pfn_destroy        = destroy_vulkan_device,
        .pfn_patch          = patch_vulkan_device,
        .parent_handler     = HARP_DEPENDENCY(g_vulkan_core_name, 0, UINT32_MAX)
    };

    res = core->register_actor(core, &actor_desc);
    if(res != HARP_RESULT_OK)
        return res;

    // VULKAN_SWAPCHAIN_HANDLER
    HarpDependencyDesc swapchain_deps[] = {
        {g_window_name,      0, UINT32_MAX},
        {g_vulkan_core_name, 0, UINT32_MAX}
    };

    handler_desc = (HarpHandlerDesc) {
        .name               = g_vulkan_swapchain_name,
        .version            = MAESTRO_VULKAN_SWAPCHAIN_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroVulkanSwapchainHandlerImpl),
        .instance_alignment = alignof(MaestroVulkanSwapchainHandlerImpl),
        .pfn_init           = init_vulkan_swapchain,
        .pfn_term           = term_vulkan_swapchain,
        .pfn_patch          = patch_vulkan_swapchain,
        .p_dependencies     = swapchain_deps,
        .dependency_count   = 2
    };

    res = core->register_handler(core, &handler_desc, &handler_base);
    if(res != HARP_RESULT_OK)
        return res;

    MaestroVulkanSwapchainHandler *swapchain = HARP_HANDLER_AS(MaestroVulkanSwapchainHandler, handler_base);

    swapchain->acquire  = swapchain_acquire;
    swapchain->present  = swapchain_present;
    swapchain->recreate = swapchain_recreate;

    core->handler_set_serving(core, handler_base, 1);


    // SAVES GLOBAL
    g_logger = logger;
    g_path = path;
    g_vulkan_core = vulkan_core;
    g_vulkan_swapchain = swapchain;
    g_window = window;


    return HARP_RESULT_OK;
}

HarpResult harp_package_query(HarpPackageDesc **out_desc) {
    static HarpPackageDesc package_desc = {
        .name    = MAESTRO_PACKAGE_NAME,
        .version = MAESTRO_PACKAGE_VERSION,

        .pfn_register = maestro_register,

        .p_dependencies   = NULL,
        .dependency_count = 0
    };

    if(out_desc == NULL)
        return HARP_RESULT_MISSING_OUTPUT;

    *out_desc = &package_desc;
    return HARP_RESULT_OK;
}
