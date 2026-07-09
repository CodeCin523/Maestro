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

    char title_base[256];
    char title_ext[256];

    /* ground-truth held state, persists across frames; keys[] is derived from it  */
    uint8_t held[MAESTRO_KEY_COUNT];
    uint8_t held_mouse;

#if HARP_PLATFORM_WINDOWS
    HINSTANCE h_instance;
    HWND hwnd;
    WINDOWPLACEMENT saved_placement;

    /* Raw Input: per-device deltas for captured mode, free of pointer
       acceleration. raw_input is 0 when registration failed. last_abs
       tracks absolute-mode devices (tablets, RDP) to derive deltas. */
    uint8_t raw_input;
    int32_t raw_accum_x;
    int32_t raw_accum_y;
    int32_t last_abs_x;
    int32_t last_abs_y;
    uint8_t has_last_abs;
#elif HARP_PLATFORM_LINUX
    Display *display;
    xcb_connection_t *connection;
    xcb_window_t window;
    xcb_screen_t *screen;
    xcb_cursor_t blank_cursor;

    /* XInput2 raw motion: per-device deltas for captured mode, independent of
       the cursor position. opcode is 0 when the extension is unavailable. */
    uint8_t xi2_opcode;
    double  raw_accum_x;
    double  raw_accum_y;

    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_win;
    xcb_atom_t net_wm_name;
    xcb_atom_t utf8_string;
    xcb_atom_t net_wm_state;
    xcb_atom_t net_wm_state_fullscreen;
    xcb_atom_t net_wm_state_demands_attention;
#endif

} MaestroWindowHandlerImpl;


void window_pump_messages(MaestroWindowHandler *h);
void window_set_mouse_capture(MaestroWindowHandler *h, uint8_t captured);
void window_set_cursor_visible(MaestroWindowHandler *h, uint8_t visible);
void window_set_title(MaestroWindowHandler *h, const char *title);
void window_set_title_extension(MaestroWindowHandler *h, const char *extension);
void window_set_size(MaestroWindowHandler *h, uint32_t width, uint32_t height);
void window_set_position(MaestroWindowHandler *h, int32_t x, int32_t y);
void window_set_fullscreen(MaestroWindowHandler *h, uint8_t fullscreen);
void window_request_attention(MaestroWindowHandler *h);
void window_get_vulkan_extensions(MaestroWindowHandler *h, uint32_t *out_count, const char **out_extensions);
HarpResult window_create_vulkan_surface(MaestroWindowHandler *h, VkInstance instance, VkSurfaceKHR *out_surface);

HarpResult init_window(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_window(HarpCoreHandler *core_handler, HarpHandlerBase *base);
HarpResult patch_window(HarpCoreHandler *core_handler, HarpHandlerBase *base);


#endif /* IMPL_MAESTRO_WINDOW_H */
