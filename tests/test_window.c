#include <harp/harp_core.h>

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

    HarpRuntimeCreator creator = {
        .argv0 = argv0
    };

    assert_harp(
        harp_initialize((const HarpCreatorBase *)&creator, &g_runtime),
        "Failed to init Harp runtime"
    );

    HarpDependencyDesc dep = {
        .name        = HARP_CORE_HANDLER_NAME,
        .min_version = 0,
        .max_version = UINT32_MAX
    };

    HarpHandlerBase *core_base = NULL;

    assert_harp(
        harp_runtime_get_handler(g_runtime, &dep, &core_base),
        "Failed to get core handler"
    );

    g_core = (HarpCoreHandler *)core_base;

    TEST_MARKER("RUNTIME", "INIT_DONE");
}

/* ========================================================= */
/* MAESTRO REGISTRATION                                       */
/* ========================================================= */

extern HarpResult maestro_register(HarpCoreHandler *);

static void test_maestro_register(void) {
    TEST_MARKER("MAESTRO", "REGISTER");

    assert_harp(
        maestro_register(g_core),
        "Failed to register Maestro package"
    );

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

    assert_harp(
        g_core->get_handler(g_core, &dep, &handler_base),
        "Failed to get Maestro Window handler"
    );

    g_window = (MaestroWindowHandler *)handler_base;

    assert(g_window != NULL);
    assert(g_window->pump_messages != NULL);

    /* handler is SERVING but not yet VALID -- window does not exist yet */
    assert(handler_base->status & HARP_STATUS_FLAG_SERVING);
    assert(!(handler_base->status & HARP_STATUS_FLAG_VALID));

    TEST_MARKER("WINDOW", "GET_HANDLER_DONE");
}

/* ========================================================= */
/* INITIALIZATION                                              */
/* ========================================================= */

static void test_window_init_handler_default(void) {
    TEST_MARKER("WINDOW", "INIT_HANDLER_DEFAULT");

    /* default creator: backend falls back to its built-in title/size */
    HarpCreatorBase creator = {
        .flags = HARP_CREATOR_FLAG_DEFAULT_CREATOR
    };

    assert_harp(
        g_core->handler_initialize(
            g_core,
            MAESTRO_WINDOW_HANDLER_NAME,
            &creator
        ),
        "Failed to initialize window handler with default creator"
    );

    HarpHandlerBase *handler_base = (HarpHandlerBase *)g_window;

    /* handler is now both SERVING and VALID -- a real window exists */
    assert(handler_base->status & HARP_STATUS_FLAG_SERVING);
    assert(handler_base->status & HARP_STATUS_FLAG_VALID);

    assert(g_window->pump_messages != NULL);

    TEST_MARKER("WINDOW", "INIT_HANDLER_DEFAULT_DONE");
}

/* ========================================================= */
/* MESSAGE PUMP                                                */
/* ========================================================= */

static void test_window_pump_messages(void) {
    TEST_MARKER("WINDOW", "PUMP_MESSAGES");

    /* should drain whatever the WM/X server has queued (map notify,
       expose, etc.) without blocking and without crashing on event
       types the pump doesn't handle yet */
    for (int i = 0; i < 10; ++i) {
        g_window->pump_messages(g_window);
    }

    TEST_MARKER("WINDOW", "PUMP_MESSAGES_DONE");
}

/* Keeps the window alive and responsive for `seconds`, pumping at roughly
   60Hz so the WM/compositor never considers it unresponsive. This is the
   part that actually lets you SEE the window on screen, instead of it
   opening and closing within the same frame. */
static void test_window_stay_visible(double seconds) {
    TEST_MARKER("WINDOW", "STAY_VISIBLE");
    printf("    window should be visible on screen for ~%.1f seconds...\n", seconds);

    const int frame_ms = 16; /* ~60Hz */
    double start = test_wall_seconds();

    while ((test_wall_seconds() - start) < seconds) {
        g_window->pump_messages(g_window);
        test_sleep_ms(frame_ms);
    }

    TEST_MARKER("WINDOW", "STAY_VISIBLE_DONE");
}

/* ========================================================= */
/* TEARDOWN + RE-INIT — verify the window can be torn down     */
/* and stood back up cleanly (relevant for hot-reload safety)  */
/* ========================================================= */

static void test_window_term_handler(void) {
    TEST_MARKER("WINDOW", "TERM_HANDLER");

    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_WINDOW_HANDLER_NAME),
        "Failed to terminate window handler"
    );

    HarpHandlerBase *handler_base = (HarpHandlerBase *)g_window;

    /* after termination: no longer VALID -- window destroyed */
    assert(!(handler_base->status & HARP_STATUS_FLAG_VALID));

    TEST_MARKER("WINDOW", "TERM_HANDLER_DONE");
}

static void test_window_reinit_with_custom_creator(void) {
    TEST_MARKER("WINDOW", "REINIT_CUSTOM");

    MaestroWindowCreator creator = {
        ._base = { .kind = 0, .flags = 0 }, /* not HARP_CREATOR_FLAG_DEFAULT_CREATOR */
        .title  = "Harp Test Window",
        .width  = 640,
        .height = 480
    };

    assert_harp(
        g_core->handler_initialize(
            g_core,
            MAESTRO_WINDOW_HANDLER_NAME,
            (const HarpCreatorBase *)&creator
        ),
        "Failed to re-initialize window handler with custom creator"
    );

    HarpHandlerBase *handler_base = (HarpHandlerBase *)g_window;

    assert(handler_base->status & HARP_STATUS_FLAG_SERVING);
    assert(handler_base->status & HARP_STATUS_FLAG_VALID);

    /* pump should still work fine on the new window */
    g_window->pump_messages(g_window);

    TEST_MARKER("WINDOW", "REINIT_CUSTOM_DONE");
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

    assert_harp(
        harp_terminate(g_runtime),
        "Failed to terminate runtime"
    );

    TEST_MARKER("RUNTIME", "TERM_DONE");
}

/* ========================================================= */
/* MAIN                                                        */
/* ========================================================= */

int main(int argc, char **argv) {
    printf("=== HARP / MAESTRO WINDOW TEST ===\n");

    test_runtime_init(argv[0]);
    test_maestro_register();

    /* verify state before initialization */
    test_window_get_handler();

    /* initialize with the backend's default title/size */
    test_window_init_handler_default();

    /* pump should be safe to call repeatedly and not block */
    test_window_pump_messages();

    /* keep it on screen long enough to actually look at it */
    test_window_stay_visible(3.0);

    /* tear down, then bring it back with explicit size/title --
       exercises the same init/term path a hot-reload would take */
    test_window_term_handler();
    test_window_reinit_with_custom_creator();
    test_window_pump_messages();

    /* keep the resized/retitled window visible too */
    test_window_stay_visible(3.0);

    test_window_final_term();
    test_runtime_term();

    printf("ALL TESTS PASSED\n");
    return 0;
}
