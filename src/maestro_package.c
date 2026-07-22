#include <maestro/maestro.h>

#include <harp/harp.h>
#include <harp/harp_ext.h>
#include <harp/utils/harp_helpers.h>

#include "maestro_globals.h"
#include "impl/maestro_logger.h"
#include "impl/maestro_path.h"
#include "impl/maestro_uptime.h"
#include "impl/maestro_window.h"
#include "impl/maestro_vulkan.h"
#include "impl/maestro_vulkan_conductor.h"
#include "impl/maestro_vulkan_swapchain.h"

#include <stdalign.h>


/* ================================================================================ */
/*  GLOBALS                                                                         */
/* ================================================================================ */

MaestroLoggerHandler *g_logger                    = NULL;
MaestroPathHandler *g_path                        = NULL;
MaestroUptimeHandler *g_uptime                    = NULL;
MaestroVulkanCoreHandler *g_vulkan_core           = NULL;
MaestroVulkanSwapchainHandler *g_vulkan_swapchain = NULL;
MaestroVulkanConductorHandler *g_vulkan_conductor = NULL;
MaestroWindowHandler *g_window                    = NULL;

const char * const g_logger_name            = MAESTRO_LOGGER_HANDLER_NAME;
const char * const g_path_name              = MAESTRO_PATH_HANDLER_NAME;
const char * const g_uptime_name            = MAESTRO_UPTIME_HANDLER_NAME;
const char * const g_window_name            = MAESTRO_WINDOW_HANDLER_NAME;
const char * const g_vulkan_core_name       = MAESTRO_VULKAN_CORE_HANDLER_NAME;
const char * const g_vulkan_device_name     = MAESTRO_VULKAN_DEVICE_ACTOR_NAME;
const char * const g_vulkan_swapchain_name  = MAESTRO_VULKAN_SWAPCHAIN_HANDLER_NAME;
const char * const g_vulkan_conductor_name  = MAESTRO_VULKAN_CONDUCTOR_HANDLER_NAME;


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

    // HARP LOGGER
    {
        HarpDependencyDesc ext_dep = HARP_DEPENDENCY(HARP_EXTENSION_HANDLER_NAME, HARP_EXTENSION_HANDLER_VERSION, UINT32_MAX);
        HarpHandlerBase *ext_base = NULL;

        // older harp runtimes have no extension handler; harp then keeps its own sink
        if(core->get_handler(core, &ext_dep, &ext_base) == HARP_RESULT_OK) {
            HarpExtensionHandler *ext = (HarpExtensionHandler *)ext_base;

            HarpLoggerDesc harp_logger_desc = {
                .user = logger,
                .pfn_log = logger_harp_log,
                .pfn_flush = logger_harp_flush
            };

            ext->set_logger(ext, &harp_logger_desc);
        }
    }

    // PATH_HANDLER
    handler_desc = (HarpHandlerDesc) {
        .name               = g_path_name,
        .version            = MAESTRO_PATH_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroPathHandler),
        .instance_alignment = alignof(MaestroPathHandler),
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

    // UPTIME_HANDLER
    handler_desc = (HarpHandlerDesc) {
        .name               = g_uptime_name,
        .version            = MAESTRO_UPTIME_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroUptimeHandlerImpl),
        .instance_alignment = alignof(MaestroUptimeHandlerImpl),
        .pfn_init           = init_uptime,
        .pfn_term           = term_uptime,
        .pfn_patch          = patch_uptime,
        .p_dependencies     = NULL,
        .dependency_count   = 0
    };

    res = core->register_handler(core, &handler_desc, &handler_base);
    if(res != HARP_RESULT_OK)
        return res;

    MaestroUptimeHandler *uptime = HARP_HANDLER_AS(MaestroUptimeHandler, handler_base);

    uptime->get_uptime_ns = uptime_get_uptime_ns;

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
    vulkan_core->create_buffer_unbound = vulkan_create_buffer_unbound;
    vulkan_core->bind_buffer = vulkan_bind_buffer;
    vulkan_core->destroy_buffer = vulkan_destroy_buffer;
    vulkan_core->create_image_unbound = vulkan_create_image_unbound;
    vulkan_core->finish_image = vulkan_finish_image;
    vulkan_core->destroy_image = vulkan_destroy_image;
    vulkan_core->find_memory_type = vulkan_find_memory_type;
    vulkan_core->alloc_memory = vulkan_alloc_memory;
    vulkan_core->free_memory = vulkan_free_memory;

    core->handler_set_serving(core, handler_base, 1);

    // VULKAN_DEVICE_ACTOR
    actor_desc = (HarpActorDesc) {
        .name               = g_vulkan_device_name,
        .version            = MAESTRO_VULKAN_DEVICE_ACTOR_VERSION,
        .instance_size      = sizeof(MaestroVulkanDeviceActor),
        .instance_alignment = alignof(MaestroVulkanDeviceActor),
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

    swapchain->acquire = swapchain_acquire;
    swapchain->present = swapchain_present;
    swapchain->recreate = swapchain_recreate;
    swapchain->set_present_mode = swapchain_set_present_mode;

    core->handler_set_serving(core, handler_base, 1);

    // VULKAN_CONDUCTOR_HANDLER
    HarpDependencyDesc conductor_deps[] = {
        {g_vulkan_core_name, 0, UINT32_MAX}
    };

    handler_desc = (HarpHandlerDesc) {
        .name               = g_vulkan_conductor_name,
        .version            = MAESTRO_VULKAN_CONDUCTOR_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroVulkanConductorHandlerImpl),
        .instance_alignment = alignof(MaestroVulkanConductorHandlerImpl),
        .pfn_init           = init_vulkan_conductor,
        .pfn_term           = term_vulkan_conductor,
        .pfn_patch          = patch_vulkan_conductor,
        .p_dependencies     = conductor_deps,
        .dependency_count   = 1
    };

    res = core->register_handler(core, &handler_desc, &handler_base);
    if(res != HARP_RESULT_OK)
        return res;

    MaestroVulkanConductorHandler *conductor = HARP_HANDLER_AS(MaestroVulkanConductorHandler, handler_base);

    conductor->open_recorder = conductor_open_recorder;
    conductor->close_recorder = conductor_close_recorder;
    conductor->record = conductor_record;
    conductor->submit = conductor_submit;
    conductor->reset_recorder = conductor_reset_recorder;
    conductor->flush = conductor_flush;
    conductor->conduct = conductor_conduct;
    conductor->cue_state = conductor_cue_state;
    conductor->cue_done = conductor_cue_done;
    conductor->cue_wait = conductor_cue_wait;
    conductor->cue_release = conductor_cue_release;

    core->handler_set_serving(core, handler_base, 1);


    // SAVES GLOBAL
    g_logger = logger;
    g_path = path;
    g_uptime = uptime;
    g_vulkan_core = vulkan_core;
    g_vulkan_swapchain = swapchain;
    g_vulkan_conductor = conductor;
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
