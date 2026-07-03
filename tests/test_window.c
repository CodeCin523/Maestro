#include <harp/harp_api.h>

#include <maestro/maestro.h>
#include <maestro/maestro_window.h>

#include <harp/utils/harp_version.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#if defined(_WIN32)
#include <windows.h>
#define test_sleep_ms(ms) Sleep(ms)
static double test_wall_seconds(void) {
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}
#else
#include <unistd.h>
#include <time.h>
#define test_sleep_ms(ms) usleep((ms) * 1000)
static double test_wall_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
#endif

/* ========================================================= */
/* TEST MACRO                                                 */
/* ========================================================= */

#define TEST_MARKER(name, phase) \
    printf("[TEST:%s:%s]\n", name, phase)

static void assert_harp(HarpResult r, const char *msg) {
    if (r != HARP_RESULT_OK) {
        printf("ASSERT_FAIL: %s (code=%d)\n", msg, r);
        exit(EXIT_FAILURE);
    }
}

/* ========================================================= */
/* GLOBALS                                                    */
/* ========================================================= */

static HarpRuntime          *g_runtime = NULL;
static HarpCoreHandler      *g_core    = NULL;
static MaestroWindowHandler *g_window  = NULL;

/* ========================================================= */
/* SETUP                                                      */
/* ========================================================= */

static void test_runtime_init(char *argv0) {
    TEST_MARKER("RUNTIME", "INIT");

    HarpRuntimeDesc desc = { .executable_path = argv0 };
    assert_harp(harp_initialize(&desc, &g_runtime), "Failed to init Harp runtime");

    HarpDependencyDesc dep = {
        .name        = HARP_CORE_HANDLER_NAME,
        .min_version = 0,
        .max_version = UINT32_MAX
    };
    HarpHandlerBase *core_base = NULL;
    assert_harp(harp_runtime_get_handler(g_runtime, &dep, &core_base), "Failed to get core handler");
    g_core = (HarpCoreHandler *)core_base;

    TEST_MARKER("RUNTIME", "INIT_DONE");
}

/* ========================================================= */
/* MAESTRO REGISTRATION                                       */
/* ========================================================= */

extern HarpResult maestro_register(HarpCoreHandler *);

static void test_maestro_register(void) {
    TEST_MARKER("MAESTRO", "REGISTER");
    assert_harp(maestro_register(g_core), "Failed to register Maestro package");
    TEST_MARKER("MAESTRO", "REGISTER_DONE");
}

/* ========================================================= */
/* GET HANDLER — before handler_initialize                    */
/* ========================================================= */

static void test_window_get_handler(void) {
    TEST_MARKER("WINDOW", "GET_HANDLER");

    HarpDependencyDesc dep = {
        .name        = MAESTRO_WINDOW_HANDLER_NAME,
        .min_version = 0,
        .max_version = UINT32_MAX
    };
    HarpHandlerBase *handler_base = NULL;
    assert_harp(g_core->get_handler(g_core, &dep, &handler_base), "Failed to get Maestro Window handler");

    g_window = (MaestroWindowHandler *)handler_base;
    assert(g_window != NULL);
    assert(g_window->pump_messages      != NULL);
    assert(g_window->set_mouse_capture  != NULL);
    assert(g_window->set_cursor_visible != NULL);
    assert(g_window->set_title          != NULL);
    assert(g_window->set_title_extension!= NULL);
    assert(g_window->set_size           != NULL);
    assert(g_window->set_position       != NULL);
    assert(g_window->set_fullscreen     != NULL);
    assert(g_window->request_attention  != NULL);

    assert(handler_base->status & HARP_STATUS_FLAG_SERVING);
    assert(!(handler_base->status & HARP_STATUS_FLAG_VALID));

    TEST_MARKER("WINDOW", "GET_HANDLER_DONE");
}

/* ========================================================= */
/* INITIALIZATION                                              */
/* ========================================================= */

static void test_window_init_handler_default(void) {
    TEST_MARKER("WINDOW", "INIT_HANDLER_DEFAULT");

    HarpCreatorBase creator = { .flags = HARP_CREATOR_FLAG_DEFAULT_CREATOR };
    assert_harp(
        g_core->handler_initialize(g_core, MAESTRO_WINDOW_HANDLER_NAME, &creator),
        "Failed to initialize window handler with default creator"
    );

    HarpHandlerBase *handler_base = (HarpHandlerBase *)g_window;
    assert(handler_base->status & HARP_STATUS_FLAG_SERVING);
    assert(handler_base->status & HARP_STATUS_FLAG_VALID);
    assert(g_window->pump_messages != NULL);

    TEST_MARKER("WINDOW", "INIT_HANDLER_DEFAULT_DONE");
}

/* ========================================================= */
/* BASIC PUMP                                                 */
/* ========================================================= */

static void test_window_pump_messages(void) {
    TEST_MARKER("WINDOW", "PUMP_MESSAGES");
    for (int i = 0; i < 10; ++i)
        g_window->pump_messages(g_window);
    TEST_MARKER("WINDOW", "PUMP_MESSAGES_DONE");
}

static void test_window_stay_visible(double seconds) {
    TEST_MARKER("WINDOW", "STAY_VISIBLE");
    printf("    window visible for ~%.1f seconds...\n", seconds);

    const int frame_ms = 16;
    double start = test_wall_seconds();
    while ((test_wall_seconds() - start) < seconds) {
        g_window->pump_messages(g_window);
        test_sleep_ms(frame_ms);
    }

    TEST_MARKER("WINDOW", "STAY_VISIBLE_DONE");
}

/* ========================================================= */
/* API TESTS — title, size, position, attention               */
/* ========================================================= */

static void test_window_api(void) {
    TEST_MARKER("WINDOW", "API");

    /* --- Title -------------------------------------------------------
       Format must always be "{base}" or "{base} - {ext}".
       The ' - ' separator is added by the implementation; the caller
       passes only the extension text.                                  */

    g_window->set_title(g_window, "Maestro Test");
    test_window_stay_visible(0.5);
    printf("    title should read: 'Maestro Test'\n");

    g_window->set_title_extension(g_window, "Phase A");
    test_window_stay_visible(0.5);
    printf("    title should read: 'Maestro Test - Phase A'\n");

    g_window->set_title_extension(g_window, NULL);
    test_window_stay_visible(0.5);
    printf("    title should read: 'Maestro Test' (extension cleared)\n");

    /* Extension set, then base changed — extension must survive. */
    g_window->set_title_extension(g_window, "Persistent Ext");
    g_window->set_title(g_window, "New Base");
    test_window_stay_visible(0.5);
    printf("    title should read: 'New Base - Persistent Ext'\n");

    /* Reset to a clean title for the rest of the test. */
    g_window->set_title_extension(g_window, NULL);
    g_window->set_title(g_window, "Maestro Test");

    /* --- Size -------------------------------------------------------- */

    g_window->set_size(g_window, 800, 600);
    /* Pump to let the WM process the resize request. */
    for(int i = 0; i < 5; ++i) {
        g_window->pump_messages(g_window);
        test_sleep_ms(16);
    }
    assert(g_window->width  == 800);
    assert(g_window->height == 600);
    printf("    set_size(800, 600): width=%u height=%u\n", g_window->width, g_window->height);

    /* --- Position ---------------------------------------------------- */

    g_window->set_position(g_window, 100, 100);
    test_window_stay_visible(0.5);
    printf("    window should have moved to roughly (100, 100)\n");

    /* --- Attention --------------------------------------------------- */

    g_window->request_attention(g_window);
    printf("    taskbar / dock should flash once\n");
    test_window_stay_visible(1.0);

    TEST_MARKER("WINDOW", "API_DONE");
}

/* ========================================================= */
/* TEARDOWN + RE-INIT                                         */
/* ========================================================= */

static void test_window_term_handler(void) {
    TEST_MARKER("WINDOW", "TERM_HANDLER");
    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_WINDOW_HANDLER_NAME),
        "Failed to terminate window handler"
    );
    assert(!(((HarpHandlerBase *)g_window)->status & HARP_STATUS_FLAG_VALID));
    TEST_MARKER("WINDOW", "TERM_HANDLER_DONE");
}

static void test_window_reinit_with_custom_creator(void) {
    TEST_MARKER("WINDOW", "REINIT_CUSTOM");

    MaestroWindowCreator creator = {
        ._base = { .kind = 0, .flags = 0 },
        .title  = "Harp Test Window",
        .width  = 800,
        .height = 600
    };
    assert_harp(
        g_core->handler_initialize(g_core, MAESTRO_WINDOW_HANDLER_NAME, (const HarpCreatorBase *)&creator),
        "Failed to re-initialize window handler with custom creator"
    );

    HarpHandlerBase *handler_base = (HarpHandlerBase *)g_window;
    assert(handler_base->status & HARP_STATUS_FLAG_SERVING);
    assert(handler_base->status & HARP_STATUS_FLAG_VALID);
    assert(g_window->width  == 800);
    assert(g_window->height == 600);

    g_window->pump_messages(g_window);
    TEST_MARKER("WINDOW", "REINIT_CUSTOM_DONE");
}

/* ========================================================= */
/* INTERACTIVE LOOP                                           */
/* ========================================================= */

static const char *key_name(MaestroKey k) {
    static const char *letters[] = {
        "A","B","C","D","E","F","G","H","I","J","K","L","M",
        "N","O","P","Q","R","S","T","U","V","W","X","Y","Z"
    };
    static const char *digits[] = { "0","1","2","3","4","5","6","7","8","9" };
    static const char *fn[]     = {
        "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"
    };

    if(k >= MAESTRO_KEY_A  && k <= MAESTRO_KEY_Z)   return letters[k - MAESTRO_KEY_A];
    if(k >= MAESTRO_KEY_0  && k <= MAESTRO_KEY_9)   return digits[k - MAESTRO_KEY_0];
    if(k >= MAESTRO_KEY_F1 && k <= MAESTRO_KEY_F12) return fn[k - MAESTRO_KEY_F1];

    switch(k) {
        case MAESTRO_KEY_SPACE:       return "SPACE";
        case MAESTRO_KEY_ENTER:       return "ENTER";
        case MAESTRO_KEY_ESCAPE:      return "ESCAPE";
        case MAESTRO_KEY_TAB:         return "TAB";
        case MAESTRO_KEY_BACKSPACE:   return "BACKSPACE";
        case MAESTRO_KEY_DELETE:      return "DELETE";
        case MAESTRO_KEY_INSERT:      return "INSERT";
        case MAESTRO_KEY_HOME:        return "HOME";
        case MAESTRO_KEY_END:         return "END";
        case MAESTRO_KEY_PAGE_UP:     return "PAGE_UP";
        case MAESTRO_KEY_PAGE_DOWN:   return "PAGE_DOWN";
        case MAESTRO_KEY_UP:          return "UP";
        case MAESTRO_KEY_DOWN:        return "DOWN";
        case MAESTRO_KEY_LEFT:        return "LEFT";
        case MAESTRO_KEY_RIGHT:       return "RIGHT";
        case MAESTRO_KEY_LEFT_SHIFT:  return "LEFT_SHIFT";
        case MAESTRO_KEY_RIGHT_SHIFT: return "RIGHT_SHIFT";
        case MAESTRO_KEY_LEFT_CTRL:   return "LEFT_CTRL";
        case MAESTRO_KEY_RIGHT_CTRL:  return "RIGHT_CTRL";
        case MAESTRO_KEY_LEFT_ALT:    return "LEFT_ALT";
        case MAESTRO_KEY_RIGHT_ALT:   return "RIGHT_ALT";
        default:                      return "?";
    }
}

/* Update the title extension to reflect current window state. */
static void update_state_title(void) {
    uint8_t captured = MAESTRO_WINDOW_IS_MOUSE_CAPTURED(g_window);
    uint8_t hidden   = MAESTRO_WINDOW_IS_CURSOR_HIDDEN(g_window);
    uint8_t fs       = MAESTRO_WINDOW_IS_FULLSCREEN(g_window);

    if(!captured && !hidden && !fs) {
        g_window->set_title_extension(g_window, NULL);
        return;
    }

    char buf[128];
    int  pos = 0;
    if(captured) pos += snprintf(buf + pos, (int)sizeof(buf) - pos, "%sCaptured", pos ? " | " : "");
    if(hidden)   pos += snprintf(buf + pos, (int)sizeof(buf) - pos, "%sCursor Hidden", pos ? " | " : "");
    if(fs)               snprintf(buf + pos, (int)sizeof(buf) - pos, "%sFullscreen",  pos ? " | " : "");

    g_window->set_title_extension(g_window, buf);
}

static void test_window_run_until_close(void) {
    TEST_MARKER("WINDOW", "RUN_UNTIL_CLOSE");

    printf("\n  === INTERACTIVE INPUT TEST ===\n");
    printf("  Controls:\n");
    printf("    C          — toggle mouse capture (ESC releases)\n");
    printf("    H          — toggle cursor visibility\n");
    printf("    F          — toggle fullscreen\n");
    printf("    R          — request attention (flash taskbar)\n");
    printf("    ESC        — release capture (or close if not captured)\n");
    printf("    close btn  — end test\n");
    printf("  Try: all keys, all mouse buttons, scroll wheel,\n");
    printf("       resize the window, minimize/restore, alt-tab.\n\n");

    const int frame_ms = 16;

    uint8_t prev_minimized = MAESTRO_WINDOW_IS_MINIMIZED(g_window);
    uint8_t prev_focused   = MAESTRO_WINDOW_IS_FOCUSED(g_window);

    while(!MAESTRO_WINDOW_IS_CLOSING(g_window)) {
        g_window->pump_messages(g_window);

        /* ---- Window state changes ----------------------------------- */

        if(MAESTRO_WINDOW_WAS_RESIZED(g_window))
            printf("  WINDOW RESIZED   %ux%u\n", g_window->width, g_window->height);

        uint8_t minimized = MAESTRO_WINDOW_IS_MINIMIZED(g_window);
        uint8_t focused   = MAESTRO_WINDOW_IS_FOCUSED(g_window);
        if(minimized != prev_minimized) {
            printf("  WINDOW %s\n", minimized ? "MINIMIZED" : "RESTORED");
            prev_minimized = minimized;
        }
        if(focused != prev_focused) {
            printf("  WINDOW %s\n", focused ? "FOCUSED" : "UNFOCUSED");
            prev_focused = focused;
        }

        /* ---- Hotkeys ------------------------------------------------ */

        if(MAESTRO_KEY_JUST_PRESSED(g_window, MAESTRO_KEY_C)) {
            uint8_t captured = !MAESTRO_WINDOW_IS_MOUSE_CAPTURED(g_window);
            g_window->set_mouse_capture(g_window, captured);
            printf("  CAPTURE          %s\n", captured ? "ON" : "OFF");
            update_state_title();
        }
        if(MAESTRO_KEY_JUST_PRESSED(g_window, MAESTRO_KEY_H)) {
            uint8_t visible = MAESTRO_WINDOW_IS_CURSOR_HIDDEN(g_window);  /* toggle */
            g_window->set_cursor_visible(g_window, visible);
            printf("  CURSOR           %s\n", visible ? "VISIBLE" : "HIDDEN");
            update_state_title();
        }
        if(MAESTRO_KEY_JUST_PRESSED(g_window, MAESTRO_KEY_F)) {
            uint8_t fs = !MAESTRO_WINDOW_IS_FULLSCREEN(g_window);
            g_window->set_fullscreen(g_window, fs);
            printf("  FULLSCREEN       %s\n", fs ? "ON" : "OFF");
            update_state_title();
        }
        if(MAESTRO_KEY_JUST_PRESSED(g_window, MAESTRO_KEY_R)) {
            g_window->request_attention(g_window);
            printf("  ATTENTION        requested\n");
        }
        if(MAESTRO_KEY_JUST_PRESSED(g_window, MAESTRO_KEY_ESCAPE)) {
            if(MAESTRO_WINDOW_IS_MOUSE_CAPTURED(g_window)) {
                g_window->set_mouse_capture(g_window, 0);
                printf("  CAPTURE          OFF (ESC)\n");
                update_state_title();
            }
        }

        /* ---- All key events ----------------------------------------- */

        for(MaestroKey k = 0; k < MAESTRO_KEY_COUNT; ++k) {
            if(MAESTRO_KEY_JUST_PRESSED(g_window, k))
                printf("  KEY PRESS        %s\n", key_name(k));
            if(MAESTRO_KEY_JUST_RELEASED(g_window, k))
                printf("  KEY RELEASE      %s\n", key_name(k));
        }

        /* ---- Mouse buttons (all 5) ---------------------------------- */

        if(MAESTRO_MOUSE_JUST_PRESSED(g_window,  MAESTRO_MOUSE_LEFT))
            printf("  MOUSE LEFT       PRESS   (%d, %d)\n", g_window->mouse_x, g_window->mouse_y);
        if(MAESTRO_MOUSE_JUST_RELEASED(g_window, MAESTRO_MOUSE_LEFT))
            printf("  MOUSE LEFT       RELEASE (%d, %d)\n", g_window->mouse_x, g_window->mouse_y);

        if(MAESTRO_MOUSE_JUST_PRESSED(g_window,  MAESTRO_MOUSE_RIGHT))
            printf("  MOUSE RIGHT      PRESS   (%d, %d)\n", g_window->mouse_x, g_window->mouse_y);
        if(MAESTRO_MOUSE_JUST_RELEASED(g_window, MAESTRO_MOUSE_RIGHT))
            printf("  MOUSE RIGHT      RELEASE (%d, %d)\n", g_window->mouse_x, g_window->mouse_y);

        if(MAESTRO_MOUSE_JUST_PRESSED(g_window,  MAESTRO_MOUSE_MIDDLE))
            printf("  MOUSE MIDDLE     PRESS   (%d, %d)\n", g_window->mouse_x, g_window->mouse_y);
        if(MAESTRO_MOUSE_JUST_RELEASED(g_window, MAESTRO_MOUSE_MIDDLE))
            printf("  MOUSE MIDDLE     RELEASE (%d, %d)\n", g_window->mouse_x, g_window->mouse_y);

        if(MAESTRO_MOUSE_JUST_PRESSED(g_window,  MAESTRO_MOUSE_BACK))
            printf("  MOUSE BACK       PRESS\n");
        if(MAESTRO_MOUSE_JUST_RELEASED(g_window, MAESTRO_MOUSE_BACK))
            printf("  MOUSE BACK       RELEASE\n");

        if(MAESTRO_MOUSE_JUST_PRESSED(g_window,  MAESTRO_MOUSE_FORWARD))
            printf("  MOUSE FORWARD    PRESS\n");
        if(MAESTRO_MOUSE_JUST_RELEASED(g_window, MAESTRO_MOUSE_FORWARD))
            printf("  MOUSE FORWARD    RELEASE\n");

        /* ---- Scroll ------------------------------------------------- */

        if(MAESTRO_MOUSE_SCROLLED(g_window))
            printf("  SCROLL           %+d\n", g_window->scroll);
        if(MAESTRO_MOUSE_SCROLLED_X(g_window))
            printf("  SCROLL X         %+d\n", g_window->scroll_x);

        /* ---- Mouse movement ----------------------------------------- */

        if(MAESTRO_WINDOW_IS_MOUSE_CAPTURED(g_window)) {
            int dx = MAESTRO_MOUSE_DELTA_X(g_window);
            int dy = MAESTRO_MOUSE_DELTA_Y(g_window);
            if(dx || dy)
                printf("  MOUSE DELTA      (%+d, %+d)\n", dx, dy);
        } else {
            if(g_window->mouse_x != g_window->prev_mouse_x ||
               g_window->mouse_y != g_window->prev_mouse_y)
                printf("  MOUSE MOVE       (%d, %d)\n", g_window->mouse_x, g_window->mouse_y);
        }

        test_sleep_ms(frame_ms);
    }

    /* Clean up any active modes before teardown. */
    if(MAESTRO_WINDOW_IS_MOUSE_CAPTURED(g_window))
        g_window->set_mouse_capture(g_window, 0);
    if(MAESTRO_WINDOW_IS_CURSOR_HIDDEN(g_window))
        g_window->set_cursor_visible(g_window, 1);
    if(MAESTRO_WINDOW_IS_FULLSCREEN(g_window))
        g_window->set_fullscreen(g_window, 0);

    TEST_MARKER("WINDOW", "RUN_UNTIL_CLOSE_DONE");
}

/* ========================================================= */
/* FINAL TEARDOWN                                              */
/* ========================================================= */

static void test_window_final_term(void) {
    TEST_MARKER("WINDOW", "FINAL_TERM");
    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_WINDOW_HANDLER_NAME),
        "Failed to terminate window handler at end of test"
    );
    TEST_MARKER("WINDOW", "FINAL_TERM_DONE");
}

static void test_runtime_term(void) {
    TEST_MARKER("RUNTIME", "TERM");
    assert_harp(harp_terminate(g_runtime), "Failed to terminate runtime");
    TEST_MARKER("RUNTIME", "TERM_DONE");
}

/* ========================================================= */
/* MAIN                                                        */
/* ========================================================= */

int main(int argc, char **argv) {
    printf("=== HARP / MAESTRO WINDOW TEST ===\n");

    test_runtime_init(argv[0]);
    test_maestro_register();

    test_window_get_handler();
    test_window_init_handler_default();
    test_window_pump_messages();
    test_window_stay_visible(2.0);

    /* API tests: title format, size, position, attention. */
    test_window_api();

    /* Tear down and bring back with explicit dimensions. */
    test_window_term_handler();
    test_window_reinit_with_custom_creator();
    test_window_pump_messages();

    /* Full interactive test — runs until the window is closed. */
    test_window_run_until_close();

    test_window_final_term();
    test_runtime_term();

    printf("ALL TESTS PASSED\n");
    return 0;
}
