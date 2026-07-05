#ifndef MAESTRO_WINDOW_H
#define MAESTRO_WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <harp/harp.h>
#include <harp/utils/harp_version.h>

#include <vulkan/vulkan_core.h>


/* ================================================================================ */
/*  TYPEDEF                                                                         */
/* ================================================================================ */

typedef uint16_t MaestroKey;
enum {
    MAESTRO_KEY_A, MAESTRO_KEY_B, MAESTRO_KEY_C, MAESTRO_KEY_D,
    MAESTRO_KEY_E, MAESTRO_KEY_F, MAESTRO_KEY_G, MAESTRO_KEY_H,
    MAESTRO_KEY_I, MAESTRO_KEY_J, MAESTRO_KEY_K, MAESTRO_KEY_L,
    MAESTRO_KEY_M, MAESTRO_KEY_N, MAESTRO_KEY_O, MAESTRO_KEY_P,
    MAESTRO_KEY_Q, MAESTRO_KEY_R, MAESTRO_KEY_S, MAESTRO_KEY_T,
    MAESTRO_KEY_U, MAESTRO_KEY_V, MAESTRO_KEY_W, MAESTRO_KEY_X,
    MAESTRO_KEY_Y, MAESTRO_KEY_Z,

    MAESTRO_KEY_0, MAESTRO_KEY_1, MAESTRO_KEY_2, MAESTRO_KEY_3,
    MAESTRO_KEY_4, MAESTRO_KEY_5, MAESTRO_KEY_6, MAESTRO_KEY_7,
    MAESTRO_KEY_8, MAESTRO_KEY_9,

    MAESTRO_KEY_F1,  MAESTRO_KEY_F2,  MAESTRO_KEY_F3,  MAESTRO_KEY_F4,
    MAESTRO_KEY_F5,  MAESTRO_KEY_F6,  MAESTRO_KEY_F7,  MAESTRO_KEY_F8,
    MAESTRO_KEY_F9,  MAESTRO_KEY_F10, MAESTRO_KEY_F11, MAESTRO_KEY_F12,

    MAESTRO_KEY_UP, MAESTRO_KEY_DOWN, MAESTRO_KEY_LEFT, MAESTRO_KEY_RIGHT,
    MAESTRO_KEY_HOME, MAESTRO_KEY_END,
    MAESTRO_KEY_PAGE_UP, MAESTRO_KEY_PAGE_DOWN,
    MAESTRO_KEY_INSERT, MAESTRO_KEY_DELETE,

    MAESTRO_KEY_SPACE, MAESTRO_KEY_ENTER, MAESTRO_KEY_ESCAPE,
    MAESTRO_KEY_TAB,   MAESTRO_KEY_BACKSPACE,

    MAESTRO_KEY_LEFT_SHIFT,  MAESTRO_KEY_RIGHT_SHIFT,
    MAESTRO_KEY_LEFT_CTRL,   MAESTRO_KEY_RIGHT_CTRL,
    MAESTRO_KEY_LEFT_ALT,    MAESTRO_KEY_RIGHT_ALT,

    MAESTRO_KEY_COUNT
};

// Per-key byte layout: bit 0 = pressed this frame, bit 1 = pressed previous frame
typedef uint8_t MaestroInputBits;
enum {
    MAESTRO_INPUT_CURRENT  = 1 << 0,
    MAESTRO_INPUT_PREVIOUS = 1 << 1,
};

// Mouse button bits — current state in bits 0-4, previous state in bits 5-9
typedef uint16_t MaestroMouseBits;
enum {
    MAESTRO_MOUSE_LEFT    = 1 << 0,
    MAESTRO_MOUSE_RIGHT   = 1 << 1,
    MAESTRO_MOUSE_MIDDLE  = 1 << 2,
    MAESTRO_MOUSE_BACK    = 1 << 3,  // side thumb button (back / browser-back)
    MAESTRO_MOUSE_FORWARD = 1 << 4,  // side thumb button (forward / browser-forward)
};

// Window state flags packed into a single byte
typedef uint8_t MaestroWindowFlags;
enum {
    MAESTRO_WINDOW_CLOSE_REQUESTED = 1 << 0,  // close requested by OS or user
    MAESTRO_WINDOW_MINIMIZED       = 1 << 1,
    MAESTRO_WINDOW_FOCUSED         = 1 << 2,
    MAESTRO_WINDOW_FULLSCREEN      = 1 << 3,  // managed by set_fullscreen
    MAESTRO_WINDOW_MOUSE_CAPTURED  = 1 << 4,  // managed by set_mouse_capture; the cursor is always hidden while captured
    MAESTRO_WINDOW_CURSOR_HIDDEN   = 1 << 5,  // visibility preference, managed by set_cursor_visible; applied while not captured
    MAESTRO_WINDOW_RESIZED         = 1 << 6,  // set for one pump cycle when size changes
};

typedef struct MaestroWindowCreator MaestroWindowCreator;
typedef struct MaestroWindowHandler MaestroWindowHandler;


/* ================================================================================ */
/*  HANDLER                                                                         */
/* ================================================================================ */

#define MAESTRO_WINDOW_HANDLER_NAME "MaestroWindowHandler"
#define MAESTRO_WINDOW_HANDLER_VERSION HARP_MAKE_VERSION(1,3,0)

struct MaestroWindowCreator {
    HarpCreatorBase _base;

    const char *title;
    uint32_t    width;
    uint32_t    height;
};

struct MaestroWindowHandler {
    HarpHandlerBase _base;

    void (*pump_messages)(MaestroWindowHandler *h);
    void (*set_mouse_capture)(MaestroWindowHandler *h, uint8_t captured);
    void (*set_cursor_visible)(MaestroWindowHandler *h, uint8_t visible);
    void (*set_title)(MaestroWindowHandler *h, const char *title);
    void (*set_title_extension)(MaestroWindowHandler *h, const char *extension);
    void (*set_size)(MaestroWindowHandler *h, uint32_t width, uint32_t height);
    void (*set_position)(MaestroWindowHandler *h, int32_t x, int32_t y);
    void (*set_fullscreen)(MaestroWindowHandler *h, uint8_t fullscreen);
    void (*request_attention)(MaestroWindowHandler *h);
    void (*get_vulkan_extensions)(MaestroWindowHandler *h, uint32_t *out_count, const char **out_extensions);
    HarpResult (*create_vulkan_surface)(MaestroWindowHandler *h, VkInstance instance, VkSurfaceKHR *out_surface);

    MaestroInputBits keys[MAESTRO_KEY_COUNT];
    MaestroMouseBits mouse_buttons;

    int32_t mouse_x;
    int32_t mouse_y;
    int32_t prev_mouse_x;
    int32_t prev_mouse_y;
    int32_t scroll;     // vertical scroll delta this pump; positive = up / forward
    int32_t scroll_x;   // horizontal scroll delta this pump; positive = right

    MaestroWindowFlags flags;
    uint32_t width;
    uint32_t height;
};


/* ================================================================================ */
/*  INPUT HELPERS                                                                   */
/* ================================================================================ */

#define MAESTRO_KEY_DOWN(h, k)     (!!(((h)->keys[k]) & MAESTRO_INPUT_CURRENT))
#define MAESTRO_KEY_UP(h, k)        (!(((h)->keys[k]) & MAESTRO_INPUT_CURRENT))
#define MAESTRO_KEY_WAS_DOWN(h, k) (!!(((h)->keys[k]) & MAESTRO_INPUT_PREVIOUS))
#define MAESTRO_KEY_WAS_UP(h, k)    (!(((h)->keys[k]) & MAESTRO_INPUT_PREVIOUS))

#define MAESTRO_MOUSE_DOWN(h, btn)     (!!(((h)->mouse_buttons) & (btn)))
#define MAESTRO_MOUSE_UP(h, btn)        (!(((h)->mouse_buttons) & (btn)))
#define MAESTRO_MOUSE_WAS_DOWN(h, btn) (!!(((h)->mouse_buttons) & ((MaestroMouseBits)((btn) << 5))))
#define MAESTRO_MOUSE_WAS_UP(h, btn)    (!(((h)->mouse_buttons) & ((MaestroMouseBits)((btn) << 5))))

#define MAESTRO_MOUSE_X_NORM(h)  ((float)(h)->mouse_x / (float)(h)->width)
#define MAESTRO_MOUSE_Y_NORM(h)  ((float)(h)->mouse_y / (float)(h)->height)
// In captured mode: displacement from window center. In normal mode: frame-to-frame delta.
#define MAESTRO_MOUSE_DELTA_X(h) ((h)->mouse_x - (h)->prev_mouse_x)
#define MAESTRO_MOUSE_DELTA_Y(h) ((h)->mouse_y - (h)->prev_mouse_y)

// Transition helpers — true only on the frame the state changes.
#define MAESTRO_KEY_JUST_PRESSED(h, k)       (MAESTRO_KEY_WAS_UP(h, k)      && MAESTRO_KEY_DOWN(h, k))
#define MAESTRO_KEY_JUST_RELEASED(h, k)      (MAESTRO_KEY_WAS_DOWN(h, k)    && MAESTRO_KEY_UP(h, k))
#define MAESTRO_MOUSE_JUST_PRESSED(h, btn)   (MAESTRO_MOUSE_WAS_UP(h, btn)  && MAESTRO_MOUSE_DOWN(h, btn))
#define MAESTRO_MOUSE_JUST_RELEASED(h, btn)  (MAESTRO_MOUSE_WAS_DOWN(h, btn)&& MAESTRO_MOUSE_UP(h, btn))

// Scroll helpers
#define MAESTRO_MOUSE_SCROLLED(h)   ((h)->scroll   != 0)
#define MAESTRO_MOUSE_SCROLLED_X(h) ((h)->scroll_x != 0)


/* ================================================================================ */
/*  WINDOW STATE HELPERS                                                            */
/* ================================================================================ */

#define MAESTRO_WINDOW_IS_CLOSING(h)        (!!(((h)->flags) & MAESTRO_WINDOW_CLOSE_REQUESTED))
#define MAESTRO_WINDOW_IS_MINIMIZED(h)      (!!(((h)->flags) & MAESTRO_WINDOW_MINIMIZED))
#define MAESTRO_WINDOW_IS_FOCUSED(h)        (!!(((h)->flags) & MAESTRO_WINDOW_FOCUSED))
#define MAESTRO_WINDOW_IS_FULLSCREEN(h)     (!!(((h)->flags) & MAESTRO_WINDOW_FULLSCREEN))
#define MAESTRO_WINDOW_IS_MOUSE_CAPTURED(h) (!!(((h)->flags) & MAESTRO_WINDOW_MOUSE_CAPTURED))
#define MAESTRO_WINDOW_IS_CURSOR_HIDDEN(h)  (!!(((h)->flags) & MAESTRO_WINDOW_CURSOR_HIDDEN))
#define MAESTRO_WINDOW_WAS_RESIZED(h)       (!!(((h)->flags) & MAESTRO_WINDOW_RESIZED))


#ifdef __cplusplus
}
#endif

#endif /* MAESTRO_WINDOW_H */
