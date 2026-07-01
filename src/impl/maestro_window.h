#ifndef IMPL_MAESTRO_WINDOW_H
#define IMPL_MAESTRO_WINDOW_H

#include <maestro/maestro_logger.h>
#include <maestro/maestro_window.h>

#include <harp/utils/harp_helpers.h>
#include <harp/utils/harp_platform.h>

#if HARP_PLATFORM_WINDOWS
#include <windows.h>
#elif HARP_PLATFORM_LINUX
#include <xcb/xcb.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#endif


typedef struct MaestroWindowHandlerImpl {
    MaestroWindowHandler pub;

    MaestroLoggerHandler *logger;

    /* ground-truth held state, persists across frames; keys[] is derived from it  */
    uint8_t held[MAESTRO_KEY_COUNT];
    uint8_t held_mouse;

#if HARP_PLATFORM_WINDOWS
    HINSTANCE h_instance;
    HWND hwnd;
#elif HARP_PLATFORM_LINUX
    Display *display;
    xcb_connection_t *connection;
    xcb_window_t window;
    xcb_screen_t *screen;
    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_win;
#endif

} MaestroWindowHandlerImpl;


void window_pump_messages(MaestroWindowHandler *h);
void window_get_vulkan_extensions(MaestroWindowHandler *h, uint32_t *out_count, const char **out_extensions);
HarpResult window_create_vulkan_surface(MaestroWindowHandler *h, VkInstance instance, VkSurfaceKHR *out_surface);

HarpResult init_window(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_window(HarpCoreHandler *core_handler, HarpHandlerBase *base);


#endif /* IMPL_MAESTRO_WINDOW_H */
