#ifndef MAESTRO_GLOBALS_H
#define MAESTRO_GLOBALS_H

#include <maestro/maestro_logger.h>
#include <maestro/maestro_path.h>
#include <maestro/maestro_vulkan.h>
#include <maestro/maestro_window.h>

#include <harp/harp.h>


/* ================================================================================ */
/*  HANDLERS                                                                        */
/* ================================================================================ */

extern const MaestroLoggerHandler *g_logger;
extern const MaestroPathHandler *g_path;
extern const MaestroVulkanCoreHandler *g_vulkan_core;
extern const MaestroVulkanSwapchainHandler *g_vulkan_swapchain;
extern const MaestroWindowHandler *g_window;


/* ================================================================================ */
/*  NAMES                                                                           */
/* ================================================================================ */

extern const char * const g_logger_name;
extern const char * const g_path_name;
extern const char * const g_window_name;
extern const char * const g_vulkan_core_name;
extern const char * const g_vulkan_device_name;
extern const char * const g_vulkan_swapchain_name;


#endif /* MAESTRO_GLOBALS_H */