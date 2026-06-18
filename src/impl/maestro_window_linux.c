#include "maestro_window.h"


#if HARP_PLATFORM_LINUX

#include <stdlib.h>
#include <string.h>


/* ================================================================================ */
/*  HELPERS                                                                         */
/* ================================================================================ */

// Fetches an X atom by name. XCB intern_atom is async; this blocks for the
// reply, which is fine since it only happens during init.
static xcb_atom_t get_atom(xcb_connection_t *conn, const char *name) {
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(
        conn, 0, (uint16_t)strlen(name), name);

    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);
    if(!reply) return XCB_ATOM_NONE;

    xcb_atom_t atom = reply->atom;
    free(reply);
    return atom;
}


/* ================================================================================ */
/*  WINDOW FUNCTIONS                                                                */
/* ================================================================================ */

void window_pump_messages(MaestroWindowHandler *h) {
    MaestroWindowHandlerImpl *handler = (MaestroWindowHandlerImpl *)h;

    if(!handler->connection) return;

    xcb_generic_event_t *event;
    while((event = xcb_poll_for_event(handler->connection)) != 0) {
        switch(event->response_type & ~0x80) {
            case XCB_CLIENT_MESSAGE: {
                xcb_client_message_event_t *cm = (xcb_client_message_event_t *)event;

                if(cm->data.data32[0] == handler->wm_delete_win) {
                    // TODO: Fire an event for the application to quit.
                }
            } break;

            case XCB_CONFIGURE_NOTIFY: {
                // xcb_configure_notify_event_t *cfg = (xcb_configure_notify_event_t *)event;
                // uint32_t width = cfg->width;
                // uint32_t height = cfg->height;

                // TODO: Fire an event for window resize.
            } break;

            case XCB_KEY_PRESS:
            case XCB_KEY_RELEASE: {
                // xcb_key_press_event_t *kp = (xcb_key_press_event_t *)event;
                // uint8_t pressed = (event->response_type & ~0x80) == XCB_KEY_PRESS;
                // TODO: input processing
            } break;

            case XCB_MOTION_NOTIFY: {
                // xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)event;
                // int32_t x_pos = motion->event_x;
                // int32_t y_pos = motion->event_y;
                // TODO: input processing
            } break;

            case XCB_BUTTON_PRESS:
            case XCB_BUTTON_RELEASE: {
                // xcb_button_press_event_t *bp = (xcb_button_press_event_t *)event;
                // uint8_t pressed = (event->response_type & ~0x80) == XCB_BUTTON_PRESS;
                // TODO: input processing
            } break;

            default:
                break;
        }

        free(event);
    }
}


/* ================================================================================ */
/*  WINDOW HANDLER                                                                  */
/* ================================================================================ */

HarpResult init_window(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator) {
    MaestroWindowCreator window_creator = {
        .title = "MaestroWindow",
        .width = 1280,
        .height = 720
    };
    if(!(creator->flags & HARP_CREATOR_FLAG_DEFAULT_CREATOR)) {
        window_creator = *(MaestroWindowCreator *)creator;
    }

    MaestroWindowHandlerImpl *handler = (MaestroWindowHandlerImpl *)base;

    if(core_handler->get_handler(
        core_handler,
        &(HarpDependencyDesc){MAESTRO_LOGGER_HANDLER_NAME, 0, UINT32_MAX},
        (HarpHandlerBase **) &handler->logger) != HARP_RESULT_OK)
            return HARP_RESULT_FAILED;

    // Open the X display via Xlib, then bridge to an XCB connection.
    // This works identically under native X11 and under XWayland --
    // the compositor presents itself as a regular X server either way.
    handler->display = XOpenDisplay(NULL);
    if(!handler->display) {
        MAESTRO_LOG_FATAL(handler->logger, base->name, "Failed to open X display");
        return HARP_RESULT_FAILED;
    }

    // Hand X event processing fully to XCB rather than Xlib.
    XSetEventQueueOwner(handler->display, XCBOwnsEventQueue);

    handler->connection = XGetXCBConnection(handler->display);
    if(!handler->connection || xcb_connection_has_error(handler->connection)) {
        MAESTRO_LOG_FATAL(handler->logger, base->name, "Failed to get XCB connection");
        XCloseDisplay(handler->display);
        handler->display = NULL;
        return HARP_RESULT_FAILED;
    }

    int default_screen_num = DefaultScreen(handler->display);

    const xcb_setup_t *setup = xcb_get_setup(handler->connection);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    for(int i = 0; i < default_screen_num; ++i) {
        xcb_screen_next(&it);
    }
    handler->screen = it.data;

    handler->window = xcb_generate_id(handler->connection);

    // Events we care about now, plus the ones the pump already has
    // switch cases for so we don't need to touch this again later.
    uint32_t event_mask =
        XCB_EVENT_MASK_KEY_PRESS |
        XCB_EVENT_MASK_KEY_RELEASE |
        XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_BUTTON_RELEASE |
        XCB_EVENT_MASK_POINTER_MOTION |
        XCB_EVENT_MASK_STRUCTURE_NOTIFY;

    uint32_t value_list[] = { handler->screen->black_pixel, event_mask };
    uint32_t value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;

    xcb_create_window(
        handler->connection,
        XCB_COPY_FROM_PARENT,
        handler->window,
        handler->screen->root,
        0, 0,
        (uint16_t)window_creator.width, (uint16_t)window_creator.height,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        handler->screen->root_visual,
        value_mask, value_list);

    // Window title.
    xcb_change_property(
        handler->connection, XCB_PROP_MODE_REPLACE, handler->window,
        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
        (uint32_t)strlen(window_creator.title), window_creator.title);

    // Hint the window manager to give us a normal decorated, resizable
    // frame (title bar + close/min/max), matching the Win32 behavior of
    // WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME.
    // Most window managers default to exactly this for a plain
    // INPUT_OUTPUT window with no overrides, so no MOTIF_WM_HINTS or
    // override-redirect tweaking is needed here.

    // Opt into WM_DELETE_WINDOW so the WM sends us a close request via
    // a client message instead of just killing the connection outright.
    handler->wm_protocols = get_atom(handler->connection, "WM_PROTOCOLS");
    handler->wm_delete_win = get_atom(handler->connection, "WM_DELETE_WINDOW");

    if(handler->wm_protocols != XCB_ATOM_NONE && handler->wm_delete_win != XCB_ATOM_NONE) {
        xcb_change_property(
            handler->connection, XCB_PROP_MODE_REPLACE, handler->window,
            handler->wm_protocols, XCB_ATOM_ATOM, 32,
            1, &handler->wm_delete_win);
    }

    xcb_map_window(handler->connection, handler->window);
    xcb_flush(handler->connection);

    return HARP_RESULT_OK;
}

HarpResult term_window(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    (void)core_handler;

    MaestroWindowHandlerImpl *handler = (MaestroWindowHandlerImpl *)base;

    if(handler->connection && handler->window) {
        xcb_destroy_window(handler->connection, handler->window);
        xcb_flush(handler->connection);
        handler->window = 0;
    }

    if(handler->display) {
        // XCloseDisplay also tears down the bridged XCB connection,
        // so there is nothing separate to free for `connection`.
        XCloseDisplay(handler->display);
        handler->display = NULL;
        handler->connection = NULL;
    }

    return HARP_RESULT_OK;
}


#endif /* HARP_PLATFORM_LINUX */
