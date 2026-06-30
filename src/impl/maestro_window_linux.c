#include "maestro_window.h"


#if HARP_PLATFORM_LINUX

#include <stdlib.h>
#include <string.h>


/* ================================================================================ */
/*  KEY MAPPING                                                                     */
/* ================================================================================ */

static MaestroKey keysym_to_maestro(KeySym sym) {
    if(sym >= XK_a && sym <= XK_z)    return (MaestroKey)(MAESTRO_KEY_A  + (sym - XK_a));
    if(sym >= XK_A && sym <= XK_Z)    return (MaestroKey)(MAESTRO_KEY_A  + (sym - XK_A));
    if(sym >= XK_0 && sym <= XK_9)    return (MaestroKey)(MAESTRO_KEY_0  + (sym - XK_0));
    if(sym >= XK_F1 && sym <= XK_F12) return (MaestroKey)(MAESTRO_KEY_F1 + (sym - XK_F1));

    switch(sym) {
        case XK_space:     return MAESTRO_KEY_SPACE;
        case XK_Return:    return MAESTRO_KEY_ENTER;
        case XK_Escape:    return MAESTRO_KEY_ESCAPE;
        case XK_Tab:       return MAESTRO_KEY_TAB;
        case XK_BackSpace: return MAESTRO_KEY_BACKSPACE;
        case XK_Delete:    return MAESTRO_KEY_DELETE;
        case XK_Insert:    return MAESTRO_KEY_INSERT;
        case XK_Home:      return MAESTRO_KEY_HOME;
        case XK_End:       return MAESTRO_KEY_END;
        case XK_Page_Up:   return MAESTRO_KEY_PAGE_UP;
        case XK_Page_Down: return MAESTRO_KEY_PAGE_DOWN;
        case XK_Up:        return MAESTRO_KEY_UP;
        case XK_Down:      return MAESTRO_KEY_DOWN;
        case XK_Left:      return MAESTRO_KEY_LEFT;
        case XK_Right:     return MAESTRO_KEY_RIGHT;
        case XK_Shift_L:   return MAESTRO_KEY_LEFT_SHIFT;
        case XK_Shift_R:   return MAESTRO_KEY_RIGHT_SHIFT;
        case XK_Control_L: return MAESTRO_KEY_LEFT_CTRL;
        case XK_Control_R: return MAESTRO_KEY_RIGHT_CTRL;
        case XK_Alt_L:     return MAESTRO_KEY_LEFT_ALT;
        case XK_Alt_R:     return MAESTRO_KEY_RIGHT_ALT;
        default:           return MAESTRO_KEY_COUNT;
    }
}


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

static const char *const WINDOW_VULKAN_EXTENSIONS[] = {
    "VK_KHR_surface",
    "VK_KHR_xcb_surface"
};
static const uint32_t WINDOW_VULKAN_EXTENSION_COUNT = 2;

void window_get_vulkan_extensions(MaestroWindowHandler *h, uint32_t *out_count, const char **out_extensions) {
    HARP_UNUSED(h);
    *out_count = WINDOW_VULKAN_EXTENSION_COUNT;
    if(out_extensions) {
        for(uint32_t i = 0; i < WINDOW_VULKAN_EXTENSION_COUNT; ++i)
            out_extensions[i] = WINDOW_VULKAN_EXTENSIONS[i];
    }
}

void window_pump_messages(MaestroWindowHandler *h) {
    MaestroWindowHandlerImpl *handler = (MaestroWindowHandlerImpl *)h;

    if(!handler->connection) return;

    for(uint32_t i = 0; i < MAESTRO_KEY_COUNT; ++i)
        h->keys[i] = (uint8_t)(((h->keys[i] & MAESTRO_INPUT_CURRENT) << 1) | handler->held[i]);

    h->mouse_buttons = (uint8_t)(((h->mouse_buttons & 0x07u) << 3) | handler->held_mouse);
    h->prev_mouse_x  = h->mouse_x;
    h->prev_mouse_y  = h->mouse_y;

    xcb_generic_event_t *event;
    while((event = xcb_poll_for_event(handler->connection)) != NULL) {
        switch(event->response_type & ~0x80) {
            case XCB_CLIENT_MESSAGE: {
                xcb_client_message_event_t *cm = (xcb_client_message_event_t *)event;
                if(cm->data.data32[0] == handler->wm_delete_win)
                    h->should_close = 1;
            } break;

            case XCB_CONFIGURE_NOTIFY: {
                xcb_configure_notify_event_t *cfg = (xcb_configure_notify_event_t *)event;
                h->width  = cfg->width;
                h->height = cfg->height;
            } break;

            case XCB_KEY_PRESS:
            case XCB_KEY_RELEASE: {
                xcb_key_press_event_t *kp = (xcb_key_press_event_t *)event;
                uint8_t pressed = (event->response_type & ~0x80) == XCB_KEY_PRESS;
                KeySym sym = XkbKeycodeToKeysym(handler->display, kp->detail, 0, 0);
                MaestroKey key = keysym_to_maestro(sym);
                if(key < MAESTRO_KEY_COUNT) {
                    if(pressed) { handler->held[key] = 1; h->keys[key] |=  MAESTRO_INPUT_CURRENT; }
                    else        { handler->held[key] = 0; h->keys[key] &= ~MAESTRO_INPUT_CURRENT; }
                }
            } break;

            case XCB_MOTION_NOTIFY: {
                xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)event;
                h->mouse_x = motion->event_x;
                h->mouse_y = motion->event_y;
            } break;

            case XCB_BUTTON_PRESS:
            case XCB_BUTTON_RELEASE: {
                xcb_button_press_event_t *bp = (xcb_button_press_event_t *)event;
                uint8_t pressed = (event->response_type & ~0x80) == XCB_BUTTON_PRESS;
                uint8_t mask = 0;
                switch(bp->detail) {
                    case 1: mask = MAESTRO_MOUSE_LEFT;   break;
                    case 2: mask = MAESTRO_MOUSE_MIDDLE; break;
                    case 3: mask = MAESTRO_MOUSE_RIGHT;  break;
                    default: break;
                }
                if(mask) {
                    if(pressed) { handler->held_mouse |= mask;  h->mouse_buttons |=  mask; }
                    else        { handler->held_mouse &= ~mask; h->mouse_buttons &= ~mask; }
                }
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
        &HARP_DEPENDENCY(MAESTRO_LOGGER_HANDLER_NAME, 0, UINT32_MAX),
        (HarpHandlerBase **)&handler->logger) != HARP_RESULT_OK)
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

    handler->pub.width         = window_creator.width;
    handler->pub.height        = window_creator.height;
    handler->pub.should_close  = 0;
    handler->pub.mouse_x       = 0;
    handler->pub.mouse_y       = 0;
    handler->pub.prev_mouse_x  = 0;
    handler->pub.prev_mouse_y  = 0;
    handler->pub.mouse_buttons = 0;
    memset(handler->pub.keys, 0, sizeof(handler->pub.keys));
    memset(handler->held, 0, sizeof(handler->held));
    handler->held_mouse = 0;

    return HARP_RESULT_OK;
}

HarpResult term_window(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);

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
