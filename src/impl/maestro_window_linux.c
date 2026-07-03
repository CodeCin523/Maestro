#include "maestro_window.h"


#if HARP_PLATFORM_LINUX

#include <vulkan/vulkan_xcb.h>

#include <stdio.h>
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

static void window_apply_title(MaestroWindowHandlerImpl *impl) {
    char buf[512];
    if(impl->title_ext[0])
        snprintf(buf, sizeof(buf), "%s - %s", impl->title_base, impl->title_ext);
    else
        snprintf(buf, sizeof(buf), "%s", impl->title_base);

    uint32_t len = (uint32_t)strlen(buf);

    if(impl->net_wm_name != XCB_ATOM_NONE && impl->utf8_string != XCB_ATOM_NONE) {
        xcb_change_property(impl->connection, XCB_PROP_MODE_REPLACE, impl->window,
            impl->net_wm_name, impl->utf8_string, 8, len, buf);
    }
    xcb_change_property(impl->connection, XCB_PROP_MODE_REPLACE, impl->window,
        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, len, buf);
    xcb_flush(impl->connection);
}

// Creates a 1x1 depth-1 transparent pixmap cursor (invisible).
static xcb_cursor_t create_blank_cursor(MaestroWindowHandlerImpl *impl) {
    xcb_pixmap_t bm = xcb_generate_id(impl->connection);
    xcb_create_pixmap(impl->connection, 1, bm, impl->window, 1, 1);

    xcb_cursor_t cursor = xcb_generate_id(impl->connection);
    xcb_create_cursor(impl->connection, cursor, bm, bm,
        0, 0, 0, 0, 0, 0, 0, 0);

    xcb_free_pixmap(impl->connection, bm);
    return cursor;
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

HarpResult window_create_vulkan_surface(MaestroWindowHandler *h, VkInstance instance, VkSurfaceKHR *out_surface) {
    MaestroWindowHandlerImpl *window = HARP_HANDLER_AS(MaestroWindowHandlerImpl, h);
    *out_surface = VK_NULL_HANDLE;

    VkXcbSurfaceCreateInfoKHR create_info = {
        .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .connection = window->connection,
        .window     = window->window,
    };

    VkResult res = vkCreateXcbSurfaceKHR(instance, &create_info, NULL, out_surface);
    if(res != VK_SUCCESS) {
        *out_surface = VK_NULL_HANDLE;
        MAESTRO_LOG_FATAL(window->logger, window->pub._base.name, "Failed to create Vulkan surface");
        return HARP_RESULT_FAILED;
    }

    return HARP_RESULT_OK;
}

void window_pump_messages(MaestroWindowHandler *h) {
    MaestroWindowHandlerImpl *handler = (MaestroWindowHandlerImpl *)h;

    if(!handler->connection) return;

    h->flags    &= ~MAESTRO_WINDOW_RESIZED;
    h->scroll    = 0;
    h->scroll_x  = 0;

    for(uint32_t i = 0; i < MAESTRO_KEY_COUNT; ++i)
        h->keys[i] = (uint8_t)(((h->keys[i] & MAESTRO_INPUT_CURRENT) << 1) | handler->held[i]);

    h->mouse_buttons = (MaestroMouseBits)(((h->mouse_buttons & 0x1Fu) << 5) | handler->held_mouse);

    // In captured mode prev is always the center so DELTA reports displacement from center.
    if(h->flags & MAESTRO_WINDOW_MOUSE_CAPTURED) {
        h->prev_mouse_x = (int32_t)(h->width  / 2);
        h->prev_mouse_y = (int32_t)(h->height / 2);
    } else {
        h->prev_mouse_x = h->mouse_x;
        h->prev_mouse_y = h->mouse_y;
    }

    xcb_generic_event_t *event;
    while((event = xcb_poll_for_event(handler->connection)) != NULL) {
        switch(event->response_type & ~0x80) {
            case XCB_CLIENT_MESSAGE: {
                xcb_client_message_event_t *cm = (xcb_client_message_event_t *)event;
                if(cm->data.data32[0] == handler->wm_delete_win)
                    h->flags |= MAESTRO_WINDOW_CLOSE_REQUESTED;
            } break;

            case XCB_CONFIGURE_NOTIFY: {
                xcb_configure_notify_event_t *cfg = (xcb_configure_notify_event_t *)event;
                if(cfg->width != h->width || cfg->height != h->height) {
                    h->width  = cfg->width;
                    h->height = cfg->height;
                    h->flags |= MAESTRO_WINDOW_RESIZED;
                }
            } break;

            case XCB_FOCUS_IN:
                h->flags |= MAESTRO_WINDOW_FOCUSED;
                break;

            case XCB_FOCUS_OUT:
                h->flags &= ~MAESTRO_WINDOW_FOCUSED;
                break;

            case XCB_MAP_NOTIFY:
                h->flags &= ~MAESTRO_WINDOW_MINIMIZED;
                break;

            case XCB_UNMAP_NOTIFY:
                h->flags |= MAESTRO_WINDOW_MINIMIZED;
                break;

            case XCB_PROPERTY_NOTIFY: {
                xcb_property_notify_event_t *pn = (xcb_property_notify_event_t *)event;
                if(pn->atom != handler->net_wm_state) break;

                xcb_get_property_reply_t *reply = xcb_get_property_reply(
                    handler->connection,
                    xcb_get_property(handler->connection, 0, handler->window,
                        handler->net_wm_state, XCB_ATOM_ATOM, 0, 32),
                    NULL);
                if(!reply) break;

                xcb_atom_t *atoms = xcb_get_property_value(reply);
                uint32_t    count = (uint32_t)(xcb_get_property_value_length(reply) / sizeof(xcb_atom_t));
                uint8_t     fs    = 0;
                for(uint32_t i = 0; i < count; ++i) {
                    if(atoms[i] == handler->net_wm_state_fullscreen) { fs = 1; break; }
                }
                free(reply);

                if(fs) h->flags |=  MAESTRO_WINDOW_FULLSCREEN;
                else   h->flags &= ~MAESTRO_WINDOW_FULLSCREEN;
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
                if(handler->warp_skip) {
                    handler->warp_skip = 0;
                } else {
                    h->mouse_x = motion->event_x;
                    h->mouse_y = motion->event_y;
                }
            } break;

            case XCB_BUTTON_PRESS:
            case XCB_BUTTON_RELEASE: {
                xcb_button_press_event_t *bp = (xcb_button_press_event_t *)event;
                uint8_t pressed = (event->response_type & ~0x80) == XCB_BUTTON_PRESS;

                // Scroll wheel: only fires on press (no matching release event).
                if(pressed) {
                    switch(bp->detail) {
                        case 4: h->scroll   += 1;  break;  // up
                        case 5: h->scroll   -= 1;  break;  // down
                        case 6: h->scroll_x -= 1;  break;  // left
                        case 7: h->scroll_x += 1;  break;  // right
                        default: break;
                    }
                }

                uint8_t mask = 0;
                switch(bp->detail) {
                    case 1: mask = MAESTRO_MOUSE_LEFT;    break;
                    case 2: mask = MAESTRO_MOUSE_MIDDLE;  break;
                    case 3: mask = MAESTRO_MOUSE_RIGHT;   break;
                    case 8: mask = MAESTRO_MOUSE_BACK;    break;
                    case 9: mask = MAESTRO_MOUSE_FORWARD; break;
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

    // Warp to center once per pump when captured so the next pump's prev == center.
    if(h->flags & MAESTRO_WINDOW_MOUSE_CAPTURED) {
        xcb_warp_pointer(handler->connection, XCB_NONE, handler->window,
            0, 0, 0, 0,
            (int16_t)(h->width  / 2),
            (int16_t)(h->height / 2));
        handler->warp_skip = 1;
        xcb_flush(handler->connection);
    }
}

void window_set_mouse_capture(MaestroWindowHandler *h, uint8_t captured) {
    MaestroWindowHandlerImpl *impl = (MaestroWindowHandlerImpl *)h;

    if(captured) {
        if(impl->blank_cursor == XCB_CURSOR_NONE)
            impl->blank_cursor = create_blank_cursor(impl);

        // Grab and confine the pointer to the window. We pass blank_cursor here
        // so the grabbed cursor is invisible; set_cursor_visible manages this
        // independently, but the grab needs some cursor argument.
        xcb_grab_pointer(impl->connection, 1, impl->window,
            XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
            XCB_EVENT_MASK_POINTER_MOTION,
            XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
            impl->window,
            (h->flags & MAESTRO_WINDOW_CURSOR_HIDDEN) ? impl->blank_cursor : XCB_CURSOR_NONE,
            XCB_CURRENT_TIME);

        // Warp to center immediately so the first pump's prev is already centered.
        xcb_warp_pointer(impl->connection, XCB_NONE, impl->window,
            0, 0, 0, 0,
            (int16_t)(h->width  / 2),
            (int16_t)(h->height / 2));
        impl->warp_skip = 1;

        h->flags |= MAESTRO_WINDOW_MOUSE_CAPTURED;
        xcb_flush(impl->connection);
    } else {
        xcb_ungrab_pointer(impl->connection, XCB_CURRENT_TIME);

        // Restore cursor appearance to match the current visibility flag.
        uint32_t cursor = (h->flags & MAESTRO_WINDOW_CURSOR_HIDDEN)
            ? impl->blank_cursor
            : XCB_CURSOR_NONE;
        xcb_change_window_attributes(impl->connection, impl->window, XCB_CW_CURSOR, &cursor);

        impl->warp_skip = 0;
        h->flags &= ~MAESTRO_WINDOW_MOUSE_CAPTURED;
        xcb_flush(impl->connection);
    }
}

void window_set_cursor_visible(MaestroWindowHandler *h, uint8_t visible) {
    MaestroWindowHandlerImpl *impl = (MaestroWindowHandlerImpl *)h;

    if(visible) {
        h->flags &= ~MAESTRO_WINDOW_CURSOR_HIDDEN;
        // Only change the window cursor attribute when not captured; the grab
        // owns the cursor appearance while captured.
        if(!(h->flags & MAESTRO_WINDOW_MOUSE_CAPTURED)) {
            uint32_t cursor = XCB_CURSOR_NONE;
            xcb_change_window_attributes(impl->connection, impl->window, XCB_CW_CURSOR, &cursor);
        }
    } else {
        if(impl->blank_cursor == XCB_CURSOR_NONE)
            impl->blank_cursor = create_blank_cursor(impl);

        h->flags |= MAESTRO_WINDOW_CURSOR_HIDDEN;
        uint32_t cursor = impl->blank_cursor;
        xcb_change_window_attributes(impl->connection, impl->window, XCB_CW_CURSOR, &cursor);
    }
    xcb_flush(impl->connection);
}

void window_set_title(MaestroWindowHandler *h, const char *title) {
    MaestroWindowHandlerImpl *impl = (MaestroWindowHandlerImpl *)h;
    snprintf(impl->title_base, sizeof(impl->title_base), "%s", title);
    window_apply_title(impl);
}

void window_set_title_extension(MaestroWindowHandler *h, const char *extension) {
    MaestroWindowHandlerImpl *impl = (MaestroWindowHandlerImpl *)h;
    if(extension)
        snprintf(impl->title_ext, sizeof(impl->title_ext), "%s", extension);
    else
        impl->title_ext[0] = '\0';
    window_apply_title(impl);
}

void window_set_size(MaestroWindowHandler *h, uint32_t width, uint32_t height) {
    MaestroWindowHandlerImpl *impl = (MaestroWindowHandlerImpl *)h;
    uint32_t values[] = { width, height };
    xcb_configure_window(impl->connection, impl->window,
        XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
    xcb_flush(impl->connection);
    h->width  = width;
    h->height = height;
}

void window_set_position(MaestroWindowHandler *h, int32_t x, int32_t y) {
    MaestroWindowHandlerImpl *impl = (MaestroWindowHandlerImpl *)h;
    uint32_t values[] = { (uint32_t)x, (uint32_t)y };
    xcb_configure_window(impl->connection, impl->window,
        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
    xcb_flush(impl->connection);
}

void window_set_fullscreen(MaestroWindowHandler *h, uint8_t fullscreen) {
    MaestroWindowHandlerImpl *impl = (MaestroWindowHandlerImpl *)h;

    if(impl->net_wm_state == XCB_ATOM_NONE ||
       impl->net_wm_state_fullscreen == XCB_ATOM_NONE)
        return;

    xcb_client_message_event_t ev = { 0 };
    ev.response_type    = XCB_CLIENT_MESSAGE;
    ev.type             = impl->net_wm_state;
    ev.window           = impl->window;
    ev.format           = 32;
    ev.data.data32[0]   = fullscreen ? 1 : 0;  // _NET_WM_STATE_ADD / _NET_WM_STATE_REMOVE
    ev.data.data32[1]   = impl->net_wm_state_fullscreen;
    ev.data.data32[2]   = 0;
    ev.data.data32[3]   = 1;  // source: application

    xcb_send_event(impl->connection, 0, impl->screen->root,
        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
        (const char *)&ev);
    xcb_flush(impl->connection);
    // Flag is updated when the WM confirms via XCB_PROPERTY_NOTIFY in pump_messages.
}

void window_request_attention(MaestroWindowHandler *h) {
    MaestroWindowHandlerImpl *impl = (MaestroWindowHandlerImpl *)h;

    if(impl->net_wm_state == XCB_ATOM_NONE ||
       impl->net_wm_state_demands_attention == XCB_ATOM_NONE)
        return;

    xcb_client_message_event_t ev = { 0 };
    ev.response_type    = XCB_CLIENT_MESSAGE;
    ev.type             = impl->net_wm_state;
    ev.window           = impl->window;
    ev.format           = 32;
    ev.data.data32[0]   = 1;  // _NET_WM_STATE_ADD
    ev.data.data32[1]   = impl->net_wm_state_demands_attention;
    ev.data.data32[3]   = 1;  // source: application

    xcb_send_event(impl->connection, 0, impl->screen->root,
        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
        (const char *)&ev);
    xcb_flush(impl->connection);
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

    uint32_t event_mask =
        XCB_EVENT_MASK_KEY_PRESS |
        XCB_EVENT_MASK_KEY_RELEASE |
        XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_BUTTON_RELEASE |
        XCB_EVENT_MASK_POINTER_MOTION |
        XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_FOCUS_CHANGE |
        XCB_EVENT_MASK_PROPERTY_CHANGE;

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

    // Fetch atoms used throughout the lifetime of the window.
    handler->wm_protocols                  = get_atom(handler->connection, "WM_PROTOCOLS");
    handler->wm_delete_win                 = get_atom(handler->connection, "WM_DELETE_WINDOW");
    handler->net_wm_name                   = get_atom(handler->connection, "_NET_WM_NAME");
    handler->utf8_string                   = get_atom(handler->connection, "UTF8_STRING");
    handler->net_wm_state                  = get_atom(handler->connection, "_NET_WM_STATE");
    handler->net_wm_state_fullscreen       = get_atom(handler->connection, "_NET_WM_STATE_FULLSCREEN");
    handler->net_wm_state_demands_attention= get_atom(handler->connection, "_NET_WM_STATE_DEMANDS_ATTENTION");

    snprintf(handler->title_base, sizeof(handler->title_base), "%s", window_creator.title);
    handler->title_ext[0] = '\0';
    window_apply_title(handler);

    // Opt into WM_DELETE_WINDOW so the WM sends a close request instead of
    // killing the connection outright.
    if(handler->wm_protocols != XCB_ATOM_NONE && handler->wm_delete_win != XCB_ATOM_NONE) {
        xcb_change_property(
            handler->connection, XCB_PROP_MODE_REPLACE, handler->window,
            handler->wm_protocols, XCB_ATOM_ATOM, 32,
            1, &handler->wm_delete_win);
    }

    xcb_map_window(handler->connection, handler->window);
    xcb_flush(handler->connection);

    handler->blank_cursor           = XCB_CURSOR_NONE;
    handler->warp_skip              = 0;
    handler->pub.width              = window_creator.width;
    handler->pub.height             = window_creator.height;
    handler->pub.flags              = MAESTRO_WINDOW_FOCUSED;
    handler->pub.mouse_x            = 0;
    handler->pub.mouse_y            = 0;
    handler->pub.prev_mouse_x       = 0;
    handler->pub.prev_mouse_y       = 0;
    handler->pub.mouse_buttons      = 0;
    handler->pub.scroll             = 0;
    handler->pub.scroll_x           = 0;
    memset(handler->pub.keys, 0, sizeof(handler->pub.keys));
    memset(handler->held, 0, sizeof(handler->held));
    handler->held_mouse             = 0;

    return HARP_RESULT_OK;
}

HarpResult term_window(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);

    MaestroWindowHandlerImpl *handler = (MaestroWindowHandlerImpl *)base;

    if(handler->connection && handler->window) {
        if(handler->blank_cursor != XCB_CURSOR_NONE) {
            xcb_free_cursor(handler->connection, handler->blank_cursor);
            handler->blank_cursor = XCB_CURSOR_NONE;
        }
        xcb_destroy_window(handler->connection, handler->window);
        xcb_flush(handler->connection);
        handler->window = 0;
    }

    if(handler->display) {
        // XCloseDisplay also tears down the bridged XCB connection,
        // so there is nothing separate to free for `connection`.
        XCloseDisplay(handler->display);
        handler->display    = NULL;
        handler->connection = NULL;
    }

    return HARP_RESULT_OK;
}


#endif /* HARP_PLATFORM_LINUX */
