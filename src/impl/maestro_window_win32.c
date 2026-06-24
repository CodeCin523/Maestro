#include "maestro_window.h"


#if HARP_PLATFORM_WINDOWS

#include <windowsx.h>


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

void window_pump_messages(MaestroWindowHandler *h) {
    MSG msg;
    while(PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}


/* ================================================================================ */
/*  WINDOW HANDLER                                                                  */
/* ================================================================================ */

LRESULT CALLBACK win32_process_message(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param) {
    switch(msg) {
        case WM_ERASEBKGND:
            // Notify the OS that erasing will be handled by the application to prevent flicker.
            return 1;
        case WM_CLOSE:
            // TODO: Fire an event for the application to quit.
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE: {
            // RECT r;
            // GetClientRect(hwnd, &r);
            // uint32_t width = r.width - r.left;
            // uint32_t height = r.bottom - r.top;

            // TODO: Fire an event for window resize.
        } break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            // uint8_t pressed = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
            // TODO: input processing
        } break;
        case WM_MOUSEMOVE: {
            // int32_t x_pos = GET_X_LPARAM(l_param);
            // int32_t y_pos = GET_Y_LPARAM(y_param);
            // TODO: input processing
        } break;
        case WM_MOUSEWHEEL: {
            // int32_t z_delta = GET_WHEEL_DELTA_WPARAM(w_param);
            // if(z_delta != 0) {
            //     // Flatten the input to an SO-independent
            //     z_delta = (z_delta < 0) ? -1 : 1;
            // }
            // TODO: input processing
        } break;
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP: {
            // uint8_t pressed = msg == WM_LBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_RBUTTONDOWN;
            // TODO: input processing
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

    if(HARP_FAILED(core_handler->get_handler(
        core_handler,
        &HARP_DEPENDENCY(MAESTRO_LOGGER_HANDLER_NAME, 0, UINT32_MAX),
        (HarpHandlerBase **)&handler->logger)))
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
        0, 0, handler->h_instance, 0);
    
    if(handle == 0) {
        MAESTRO_LOG_FATAL(handler->logger, base->name, "Window creation failed");
        return HARP_RESULT_FAILED;
    } else {
        handler->hwnd = handle;
    }

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

    return HARP_RESULT_OK;
}


#endif /* HARP_PLATFORM_WINDOWS */