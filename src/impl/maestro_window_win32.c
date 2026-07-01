#include "maestro_window.h"


#if HARP_PLATFORM_WINDOWS

#include <vulkan/vulkan_win32.h>

#include <windowsx.h>
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

    h->mouse_buttons = (uint8_t)(((h->mouse_buttons & 0x07u) << 3) | handler->held_mouse);
    h->prev_mouse_x  = h->mouse_x;
    h->prev_mouse_y  = h->mouse_y;

    MSG msg;
    while(PeekMessageA(&msg, handler->hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}


/* ================================================================================ */
/*  WINDOW HANDLER                                                                  */
/* ================================================================================ */

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
            if(handler) handler->pub.should_close = 1;
            return 0;
        case WM_SIZE: {
            if(handler) {
                if(w_param == SIZE_MINIMIZED) {
                    handler->pub.is_minimized = 1;
                } else {
                    handler->pub.is_minimized = 0;
                    handler->pub.width  = LOWORD(l_param);
                    handler->pub.height = HIWORD(l_param);
                }
            }
        } break;
        case WM_SETFOCUS:
            if(handler) handler->pub.is_focused = 1;
            break;
        case WM_KILLFOCUS:
            if(handler) handler->pub.is_focused = 0;
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
            handler->pub.mouse_x = GET_X_LPARAM(l_param);
            handler->pub.mouse_y = GET_Y_LPARAM(l_param);
        } break;
        case WM_MOUSEWHEEL: {
            // TODO: scroll input
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

    handler->h_instance = GetModuleHandleA(0);

    HICON icon = LoadIcon(handler->h_instance, IDI_APPLICATION);
    WNDCLASSA wc = {0};
    wc.style = CS_DBLCLKS; // Get double-clicks
    wc.lpfnWndProc = win32_process_message;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = handler->h_instance;
    wc.hIcon = icon;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); // NULL; // Manage the cursor manually
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

    // Obtain the size of the border.
    RECT border_rect = {0, 0, 0, 0};
    AdjustWindowRectEx(&border_rect, window_style, 0, window_ex_style);

    // The border rectangle is negative.
    window_x += border_rect.left;
    window_y += border_rect.top;

    // Grow by the size of the OS border.
    window_width += border_rect.right - border_rect.left;
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

    handler->pub.width         = (uint32_t)client_width;
    handler->pub.height        = (uint32_t)client_height;
    handler->pub.should_close  = 0;
    handler->pub.is_minimized  = 0;
    handler->pub.is_focused    = 1;
    handler->pub.mouse_x       = 0;
    handler->pub.mouse_y       = 0;
    handler->pub.prev_mouse_x  = 0;
    handler->pub.prev_mouse_y  = 0;
    handler->pub.mouse_buttons = 0;
    memset(handler->pub.keys, 0, sizeof(handler->pub.keys));
    memset(handler->held, 0, sizeof(handler->held));
    handler->held_mouse = 0;

    uint32_t should_activate = 1; // If the window should not accept input, this should be false.
    int32_t show_window_command_flags = should_activate ? SW_SHOW : SW_SHOWNOACTIVE;
    // If initially minimized, use SW_MINIMIZE : SW_SHOWMINNOACTIVE
    // If initially maximized, use SW_SHOWMAXIMIZED : SW_MAXIMIZE
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