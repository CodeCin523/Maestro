#include "maestro_window.h"


#if HARP_PLATFORM_WINDOWS

#include <vulkan/vulkan_win32.h>

#include <windowsx.h>
#include <stdio.h>
#include <string.h>


/* ================================================================================ */
/*  KEY MAPPING                                                                     */
/* ================================================================================ */

static MaestroKey win32_vk_to_maestro(UINT vk, uint8_t extended, uint8_t scan) {
    if(vk >= 'A' && vk <= 'Z')      return (MaestroKey)(MAESTRO_KEY_A  + (vk - 'A'));
    if(vk >= '0' && vk <= '9')      return (MaestroKey)(MAESTRO_KEY_0  + (vk - '0'));
    if(vk >= VK_F1 && vk <= VK_F12) return (MaestroKey)(MAESTRO_KEY_F1 + (vk - VK_F1));

    switch(vk) {
        case VK_SPACE:   return MAESTRO_KEY_SPACE;
        case VK_RETURN:  return MAESTRO_KEY_ENTER;
        case VK_ESCAPE:  return MAESTRO_KEY_ESCAPE;
        case VK_TAB:     return MAESTRO_KEY_TAB;
        case VK_BACK:    return MAESTRO_KEY_BACKSPACE;
        case VK_DELETE:  return MAESTRO_KEY_DELETE;
        case VK_INSERT:  return MAESTRO_KEY_INSERT;
        case VK_HOME:    return MAESTRO_KEY_HOME;
        case VK_END:     return MAESTRO_KEY_END;
        case VK_PRIOR:   return MAESTRO_KEY_PAGE_UP;
        case VK_NEXT:    return MAESTRO_KEY_PAGE_DOWN;
        case VK_UP:      return MAESTRO_KEY_UP;
        case VK_DOWN:    return MAESTRO_KEY_DOWN;
        case VK_LEFT:    return MAESTRO_KEY_LEFT;
        case VK_RIGHT:   return MAESTRO_KEY_RIGHT;
        case VK_SHIFT:   return scan == 0x36 ? MAESTRO_KEY_RIGHT_SHIFT : MAESTRO_KEY_LEFT_SHIFT;
        case VK_CONTROL: return extended     ? MAESTRO_KEY_RIGHT_CTRL  : MAESTRO_KEY_LEFT_CTRL;
        case VK_MENU:    return extended     ? MAESTRO_KEY_RIGHT_ALT   : MAESTRO_KEY_LEFT_ALT;
        default:         return MAESTRO_KEY_COUNT;
    }
}


/* ================================================================================ */
/*  WINDOW FUNCTIONS                                                                */
/* ================================================================================ */

static const char *const WINDOW_VULKAN_EXTENSIONS[] = {
    "VK_KHR_surface",
    "VK_KHR_win32_surface"
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

    VkWin32SurfaceCreateInfoKHR create_info = {
        .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = window->h_instance,
        .hwnd      = window->hwnd,
    };

    VkResult res = vkCreateWin32SurfaceKHR(instance, &create_info, NULL, out_surface);
    if(res != VK_SUCCESS) {
        *out_surface = VK_NULL_HANDLE;
        MAESTRO_LOG_FATAL(window->logger, window->pub._base.name, "Failed to create Vulkan surface");
        return HARP_RESULT_FAILED;
    }

    return HARP_RESULT_OK;
}

void window_pump_messages(MaestroWindowHandler *h) {
    MaestroWindowHandlerImpl *handler = (MaestroWindowHandlerImpl *)h;

    if(!handler->hwnd) return;

    for(uint32_t i = 0; i < MAESTRO_KEY_COUNT; ++i)
        h->keys[i] = (uint8_t)(((h->keys[i] & MAESTRO_INPUT_CURRENT) << 1) | handler->held[i]);

    h->flags    &= ~MAESTRO_WINDOW_RESIZED;
    h->scroll    = 0;
    h->scroll_x  = 0;

    h->mouse_buttons = (MaestroMouseBits)(((h->mouse_buttons & 0x1Fu) << 5) | handler->held_mouse);

    // In captured mode prev is always the center so DELTA reports the movement
    // gathered during this pump. mouse_x starts at the center too; a stale
    // value would otherwise repeat the previous delta on quiet frames.
    if(h->flags & MAESTRO_WINDOW_MOUSE_CAPTURED) {
        h->prev_mouse_x = (int32_t)(h->width  / 2);
        h->prev_mouse_y = (int32_t)(h->height / 2);
        h->mouse_x = h->prev_mouse_x;
        h->mouse_y = h->prev_mouse_y;
    } else {
        h->prev_mouse_x = h->mouse_x;
        h->prev_mouse_y = h->mouse_y;
    }

    MSG msg;
    while(PeekMessageA(&msg, handler->hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    // With Raw Input, captured mouse_x/y come from the accumulated WM_INPUT
    // deltas. Then warp back to center so the cursor stays inside the clip
    // rect. Skipped while unfocused, capture is suspended then.
    if((h->flags & MAESTRO_WINDOW_MOUSE_CAPTURED) && (h->flags & MAESTRO_WINDOW_FOCUSED)) {
        if(handler->raw_input) {
            h->mouse_x = h->prev_mouse_x + handler->raw_accum_x;
            h->mouse_y = h->prev_mouse_y + handler->raw_accum_y;
            handler->raw_accum_x = 0;
            handler->raw_accum_y = 0;
        }
        POINT center = { (LONG)(h->width / 2), (LONG)(h->height / 2) };
        ClientToScreen(handler->hwnd, &center);
        SetCursorPos(center.x, center.y);
    }
}


/* ================================================================================ */
/*  WINDOW PROCEDURE                                                                */
/* ================================================================================ */

// Applies clip + capture + center warp. Used when enabling capture and when
// focus returns while capture is still requested.
static void win32_apply_mouse_capture(MaestroWindowHandlerImpl *impl) {
    SetCapture(impl->hwnd);

    RECT rect;
    GetClientRect(impl->hwnd, &rect);
    MapWindowPoints(impl->hwnd, NULL, (POINT *)&rect, 2);
    ClipCursor(&rect);

    POINT center = { (LONG)(impl->pub.width / 2), (LONG)(impl->pub.height / 2) };
    ClientToScreen(impl->hwnd, &center);
    SetCursorPos(center.x, center.y);
    impl->pub.mouse_x      = (int32_t)(impl->pub.width  / 2);
    impl->pub.mouse_y      = (int32_t)(impl->pub.height / 2);
    impl->pub.prev_mouse_x = impl->pub.mouse_x;
    impl->pub.prev_mouse_y = impl->pub.mouse_y;

    impl->raw_accum_x  = 0;
    impl->raw_accum_y  = 0;
    impl->has_last_abs = 0;

    // The cursor is always hidden while captured, regardless of the
    // visibility preference; WM_SETCURSOR keeps it hidden on hover.
    SetCursor(NULL);
}

LRESULT CALLBACK win32_process_message(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param) {
    if(msg == WM_NCCREATE) {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)l_param;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcA(hwnd, msg, w_param, l_param);
    }

    MaestroWindowHandlerImpl *handler = (MaestroWindowHandlerImpl *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch(msg) {
        case WM_ERASEBKGND:
            // Notify the OS that erasing will be handled by the application to prevent flicker.
            return 1;
        case WM_CLOSE:
            if(handler) handler->pub.flags |= MAESTRO_WINDOW_CLOSE_REQUESTED;
            return 0;
        case WM_SIZE: {
            if(handler) {
                if(w_param == SIZE_MINIMIZED) {
                    handler->pub.flags |= MAESTRO_WINDOW_MINIMIZED;
                } else {
                    uint32_t new_w = LOWORD(l_param);
                    uint32_t new_h = HIWORD(l_param);
                    handler->pub.flags &= ~MAESTRO_WINDOW_MINIMIZED;
                    if(new_w != handler->pub.width || new_h != handler->pub.height) {
                        handler->pub.width  = new_w;
                        handler->pub.height = new_h;
                        handler->pub.flags |= MAESTRO_WINDOW_RESIZED;
                    }
                }
            }
        } break;
        case WM_SETFOCUS:
            if(handler) {
                handler->pub.flags |= MAESTRO_WINDOW_FOCUSED;
                // Capture is a persistent request; restore it when focus returns.
                if(handler->pub.flags & MAESTRO_WINDOW_MOUSE_CAPTURED)
                    win32_apply_mouse_capture(handler);
            }
            break;
        case WM_KILLFOCUS:
            if(handler) {
                handler->pub.flags &= ~MAESTRO_WINDOW_FOCUSED;
                // Suspend capture on focus loss to avoid trapping the cursor.
                // The CAPTURED flag stays set so WM_SETFOCUS can restore it.
                if(handler->pub.flags & MAESTRO_WINDOW_MOUSE_CAPTURED) {
                    ClipCursor(NULL);
                    ReleaseCapture();
                }
            }
            break;
        case WM_SETCURSOR:
            // Suppress the default cursor when hidden by preference or while
            // captured; capture always hides the cursor.
            if(handler &&
               (handler->pub.flags & (MAESTRO_WINDOW_CURSOR_HIDDEN | MAESTRO_WINDOW_MOUSE_CAPTURED)) &&
               LOWORD(l_param) == HTCLIENT) {
                SetCursor(NULL);
                return TRUE;
            }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            if(!handler) break;
            uint8_t    pressed  = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
            uint8_t    extended = (uint8_t)((l_param >> 24) & 1);
            uint8_t    scan     = (uint8_t)((l_param >> 16) & 0xFF);
            MaestroKey key      = win32_vk_to_maestro((UINT)w_param, extended, scan);
            if(key < MAESTRO_KEY_COUNT) {
                if(pressed) { handler->held[key] = 1; handler->pub.keys[key] |=  MAESTRO_INPUT_CURRENT; }
                else        { handler->held[key] = 0; handler->pub.keys[key] &= ~MAESTRO_INPUT_CURRENT; }
            }
        } break;
        case WM_MOUSEMOVE: {
            if(!handler) break;
            // While captured with Raw Input available the WM_INPUT deltas
            // below drive mouse_x/y; positions include pointer acceleration
            // and would double count.
            if(!(handler->pub.flags & MAESTRO_WINDOW_MOUSE_CAPTURED) || !handler->raw_input) {
                handler->pub.mouse_x = GET_X_LPARAM(l_param);
                handler->pub.mouse_y = GET_Y_LPARAM(l_param);
            }
        } break;
        case WM_INPUT: {
            if(!handler) break;
            if(!(handler->pub.flags & MAESTRO_WINDOW_MOUSE_CAPTURED) ||
               !(handler->pub.flags & MAESTRO_WINDOW_FOCUSED))
                break;

            RAWINPUT raw;
            UINT size = sizeof(raw);
            if(GetRawInputData((HRAWINPUT)l_param, RID_INPUT, &raw, &size,
                               sizeof(RAWINPUTHEADER)) == (UINT)-1)
                break;
            if(raw.header.dwType != RIM_TYPEMOUSE)
                break;

            if(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
                // Absolute devices (tablets, RDP): normalized 0..65535 over
                // the screen; derive deltas from consecutive positions.
                uint8_t virt = (raw.data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
                int32_t sw = GetSystemMetrics(virt ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
                int32_t sh = GetSystemMetrics(virt ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
                int32_t ax = MulDiv(raw.data.mouse.lLastX, sw, 65535);
                int32_t ay = MulDiv(raw.data.mouse.lLastY, sh, 65535);
                if(handler->has_last_abs) {
                    handler->raw_accum_x += ax - handler->last_abs_x;
                    handler->raw_accum_y += ay - handler->last_abs_y;
                }
                handler->last_abs_x   = ax;
                handler->last_abs_y   = ay;
                handler->has_last_abs = 1;
            } else {
                handler->raw_accum_x += raw.data.mouse.lLastX;
                handler->raw_accum_y += raw.data.mouse.lLastY;
            }
        } break;
        case WM_MOUSEWHEEL:
            if(handler)
                handler->pub.scroll += GET_WHEEL_DELTA_WPARAM(w_param) / WHEEL_DELTA;
            break;
        case WM_MOUSEHWHEEL:
            if(handler)
                handler->pub.scroll_x += GET_WHEEL_DELTA_WPARAM(w_param) / WHEEL_DELTA;
            break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: {
            if(!handler) break;
            uint8_t pressed = (msg == WM_XBUTTONDOWN);
            uint8_t mask    = (HIWORD(w_param) == XBUTTON1) ? MAESTRO_MOUSE_BACK : MAESTRO_MOUSE_FORWARD;
            if(pressed) { handler->held_mouse |= mask;  handler->pub.mouse_buttons |=  mask; }
            else        { handler->held_mouse &= ~mask; handler->pub.mouse_buttons &= ~mask; }
        } break;
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP: {
            if(!handler) break;
            uint8_t pressed = (msg == WM_LBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_RBUTTONDOWN);
            uint8_t mask    = 0;
            if     (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) mask = MAESTRO_MOUSE_LEFT;
            else if(msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) mask = MAESTRO_MOUSE_RIGHT;
            else if(msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP) mask = MAESTRO_MOUSE_MIDDLE;
            if(pressed) { handler->held_mouse |= mask;  handler->pub.mouse_buttons |=  mask; }
            else        { handler->held_mouse &= ~mask; handler->pub.mouse_buttons &= ~mask; }
        } break;
    }

    return DefWindowProcA(hwnd, msg, w_param, l_param);
}


/* ================================================================================ */
/*  SET FUNCTIONS                                                                   */
/* ================================================================================ */

void window_set_mouse_capture(MaestroWindowHandler *h, uint8_t captured) {
    MaestroWindowHandlerImpl *impl = (MaestroWindowHandlerImpl *)h;

    if(captured) {
        win32_apply_mouse_capture(impl);
        h->flags |= MAESTRO_WINDOW_MOUSE_CAPTURED;
    } else {
        ClipCursor(NULL);
        ReleaseCapture();
        h->flags &= ~MAESTRO_WINDOW_MOUSE_CAPTURED;

        // Restore the remembered visibility preference.
        SetCursor((h->flags & MAESTRO_WINDOW_CURSOR_HIDDEN)
            ? NULL
            : LoadCursorA(NULL, IDC_ARROW));
    }
}

void window_set_cursor_visible(MaestroWindowHandler *h, uint8_t visible) {
    // The flag stores the visibility preference. While captured the cursor
    // stays hidden; the preference is applied again on release.
    if(visible) h->flags &= ~MAESTRO_WINDOW_CURSOR_HIDDEN;
    else        h->flags |=  MAESTRO_WINDOW_CURSOR_HIDDEN;

    if(!(h->flags & MAESTRO_WINDOW_MOUSE_CAPTURED))
        SetCursor(visible ? LoadCursorA(NULL, IDC_ARROW) : NULL);
    // WM_SETCURSOR handles subsequent cursor hover events based on the flags.
}

static void window_apply_title(MaestroWindowHandlerImpl *impl) {
    char buf[512];
    if(impl->title_ext[0])
        snprintf(buf, sizeof(buf), "%s - %s", impl->title_base, impl->title_ext);
    else
        snprintf(buf, sizeof(buf), "%s", impl->title_base);
    SetWindowTextA(impl->hwnd, buf);
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
    DWORD style    = (DWORD)GetWindowLongA(impl->hwnd, GWL_STYLE);
    DWORD ex_style = (DWORD)GetWindowLongA(impl->hwnd, GWL_EXSTYLE);
    RECT  rect     = { 0, 0, (LONG)width, (LONG)height };
    AdjustWindowRectEx(&rect, style, FALSE, ex_style);
    SetWindowPos(impl->hwnd, NULL, 0, 0,
        rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    h->width  = width;
    h->height = height;
}

void window_set_position(MaestroWindowHandler *h, int32_t x, int32_t y) {
    MaestroWindowHandlerImpl *impl = (MaestroWindowHandlerImpl *)h;
    SetWindowPos(impl->hwnd, NULL, x, y, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void window_set_fullscreen(MaestroWindowHandler *h, uint8_t fullscreen) {
    MaestroWindowHandlerImpl *impl = (MaestroWindowHandlerImpl *)h;

    if(fullscreen) {
        impl->saved_placement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(impl->hwnd, &impl->saved_placement);

        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(MonitorFromWindow(impl->hwnd, MONITOR_DEFAULTTONEAREST), &mi);

        SetWindowLongA(impl->hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(impl->hwnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right  - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED);

        h->flags |= MAESTRO_WINDOW_FULLSCREEN;
    } else {
        DWORD style = WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION |
                      WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME;
        SetWindowLongA(impl->hwnd, GWL_STYLE, style);
        SetWindowPlacement(impl->hwnd, &impl->saved_placement);
        SetWindowPos(impl->hwnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        h->flags &= ~MAESTRO_WINDOW_FULLSCREEN;
    }
}

void window_request_attention(MaestroWindowHandler *h) {
    MaestroWindowHandlerImpl *impl = (MaestroWindowHandlerImpl *)h;
    FLASHWINFO fi = {
        .cbSize    = sizeof(FLASHWINFO),
        .hwnd      = impl->hwnd,
        .dwFlags   = FLASHW_TRAY | FLASHW_TIMERNOFG,
        .uCount    = 3,
        .dwTimeout = 0
    };
    FlashWindowEx(&fi);
}


/* ================================================================================ */
/*  INIT / TERM                                                                     */
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

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    handler->h_instance = GetModuleHandleA(0);

    HICON icon = LoadIcon(handler->h_instance, IDI_APPLICATION);
    WNDCLASSA wc = {0};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = win32_process_message;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = handler->h_instance;
    wc.hIcon = icon;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = "MaestroWindowClass";

    if(!RegisterClassA(&wc)) {
        MAESTRO_LOG_FATAL(handler->logger, base->name, "Window registration failed");
        return HARP_RESULT_FAILED;
    }

    int client_x = CW_USEDEFAULT;
    int client_y = CW_USEDEFAULT;
    int client_width  = (int)window_creator.width;
    int client_height = (int)window_creator.height;

    int window_x = client_x;
    int window_y = client_y;
    int window_width  = client_width;
    int window_height = client_height;

    DWORD window_style    = WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION;
    DWORD window_ex_style = WS_EX_APPWINDOW;

    window_style |= WS_MAXIMIZEBOX;
    window_style |= WS_MINIMIZEBOX;
    window_style |= WS_THICKFRAME;

    RECT border_rect = {0, 0, 0, 0};
    AdjustWindowRectEx(&border_rect, window_style, 0, window_ex_style);

    window_x += border_rect.left;
    window_y += border_rect.top;
    window_width  += border_rect.right  - border_rect.left;
    window_height += border_rect.bottom - border_rect.top;

    HWND handle = CreateWindowExA(
        window_ex_style, "MaestroWindowClass", window_creator.title,
        window_style, window_x, window_y, window_width, window_height,
        0, 0, handler->h_instance, handler);

    if(handle == 0) {
        MAESTRO_LOG_FATAL(handler->logger, base->name, "Window creation failed");
        return HARP_RESULT_FAILED;
    }

    handler->hwnd = handle;

    snprintf(handler->title_base, sizeof(handler->title_base), "%s", window_creator.title);
    handler->title_ext[0]               = '\0';
    memset(&handler->saved_placement, 0, sizeof(handler->saved_placement));

    // Raw Input: unaccelerated per-device deltas for captured mode. Delivered
    // only while the application is in the foreground, which matches the
    // focus-suspended capture. Without it captured deltas fall back to
    // WM_MOUSEMOVE positions, which include pointer acceleration.
    handler->raw_accum_x  = 0;
    handler->raw_accum_y  = 0;
    handler->last_abs_x   = 0;
    handler->last_abs_y   = 0;
    handler->has_last_abs = 0;

    RAWINPUTDEVICE rid = {
        .usUsagePage = 0x01,  /* generic desktop */
        .usUsage     = 0x02,  /* mouse           */
        .dwFlags     = 0,
        .hwndTarget  = handler->hwnd
    };
    handler->raw_input = RegisterRawInputDevices(&rid, 1, sizeof(rid)) ? 1 : 0;
    if(!handler->raw_input)
        MAESTRO_LOG_WARN(handler->logger, base->name,
            "Raw Input unavailable, captured mouse deltas fall back to absolute positions");

    handler->pub.width                  = (uint32_t)client_width;
    handler->pub.height                 = (uint32_t)client_height;
    handler->pub.flags                  = MAESTRO_WINDOW_FOCUSED;
    handler->pub.mouse_x                = 0;
    handler->pub.mouse_y                = 0;
    handler->pub.prev_mouse_x           = 0;
    handler->pub.prev_mouse_y           = 0;
    handler->pub.mouse_buttons          = 0;
    handler->pub.scroll                 = 0;
    handler->pub.scroll_x               = 0;
    memset(handler->pub.keys, 0, sizeof(handler->pub.keys));
    memset(handler->held, 0, sizeof(handler->held));
    handler->held_mouse = 0;

    uint32_t should_activate = 1;
    int32_t show_window_command_flags = should_activate ? SW_SHOW : SW_SHOWNOACTIVE;
    ShowWindow(handler->hwnd, show_window_command_flags);

    return HARP_RESULT_OK;
}

HarpResult term_window(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);
    MaestroWindowHandlerImpl *handler = (MaestroWindowHandlerImpl *)base;

    if(handler->hwnd) {
        DestroyWindow(handler->hwnd);
        handler->hwnd = 0;
    }

    if(handler->h_instance) {
        UnregisterClassA("MaestroWindowClass", handler->h_instance);
    }

    return HARP_RESULT_OK;
}


#endif /* HARP_PLATFORM_WINDOWS */
