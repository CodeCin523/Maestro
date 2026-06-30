#ifndef MAESTRO_WINDOW_H
#define MAESTRO_WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <harp/harp.h>
#include <harp/utils/harp_version.h>


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

typedef struct MaestroWindowCreator MaestroWindowCreator;
typedef struct MaestroWindowHandler MaestroWindowHandler;


/* ================================================================================ */
/*  INPUT STATE BITS                                                                */
/* ================================================================================ */

// Per-key byte layout: bit 0 = pressed this frame, bit 1 = pressed previous frame
#define MAESTRO_INPUT_CURRENT  0x01u
#define MAESTRO_INPUT_PREVIOUS 0x02u

// Mouse button masks — current state in bits 0-2, previous state in bits 3-5
#define MAESTRO_MOUSE_LEFT   0x01u
#define MAESTRO_MOUSE_RIGHT  0x02u
#define MAESTRO_MOUSE_MIDDLE 0x04u


/* ================================================================================ */
/*  HANDLER                                                                         */
/* ================================================================================ */

#define MAESTRO_WINDOW_HANDLER_NAME    "MaestroWindowHandler"
#define MAESTRO_WINDOW_HANDLER_VERSION HARP_MAKE_VERSION(1,1,0)

struct MaestroWindowCreator {
    HarpCreatorBase _base;

    const char *title;
    uint32_t    width;
    uint32_t    height;
};

struct MaestroWindowHandler {
    HarpHandlerBase _base;

    void (*pump_messages)(MaestroWindowHandler *h);
    void (*get_vulkan_extensions)(MaestroWindowHandler *h, uint32_t *out_count, const char **out_extensions);

    // keys[k]: bit 0 = pressed this frame, bit 1 = pressed previous frame
    uint8_t  keys[MAESTRO_KEY_COUNT];

    // bits 0-2: current L/R/M buttons; bits 3-5: previous L/R/M buttons
    uint8_t  mouse_buttons;

    int32_t  mouse_x;
    int32_t  mouse_y;
    int32_t  prev_mouse_x;
    int32_t  prev_mouse_y;

    uint8_t  should_close;
    uint8_t  is_minimized;
    uint8_t  is_focused;
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
#define MAESTRO_MOUSE_WAS_DOWN(h, btn) (!!(((h)->mouse_buttons) & ((uint8_t)((btn) << 3))))
#define MAESTRO_MOUSE_WAS_UP(h, btn)    (!(((h)->mouse_buttons) & ((uint8_t)((btn) << 3))))

#define MAESTRO_MOUSE_X_NORM(h)  ((float)(h)->mouse_x / (float)(h)->width)
#define MAESTRO_MOUSE_Y_NORM(h)  ((float)(h)->mouse_y / (float)(h)->height)
#define MAESTRO_MOUSE_DELTA_X(h) ((h)->mouse_x - (h)->prev_mouse_x)
#define MAESTRO_MOUSE_DELTA_Y(h) ((h)->mouse_y - (h)->prev_mouse_y)


#ifdef __cplusplus
}
#endif

#endif /* MAESTRO_WINDOW_H */
