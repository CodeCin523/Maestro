#include <harp/harp_api.h>

#include <maestro/maestro.h>
#include <maestro/maestro_logger.h>

#include <harp/utils/harp_version.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

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
static MaestroLoggerHandler *g_logger  = NULL;

/* ========================================================= */
/* SETUP                                                      */
/* ========================================================= */

static void test_runtime_init(char *argv0) {
    TEST_MARKER("RUNTIME", "INIT");

    HarpRuntimeDesc desc = {
        .executable_path = argv0
    };

    assert_harp(
        harp_initialize(&desc, &g_runtime),
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
/* FALLBACK PHASE — before handler_initialize                 */
/* ========================================================= */

static void test_logger_get_handler(void) {
    TEST_MARKER("LOGGER", "GET_HANDLER");

    HarpDependencyDesc dep = {
        .name        = MAESTRO_LOGGER_HANDLER_NAME,
        .min_version = 0,
        .max_version = UINT32_MAX
    };

    HarpHandlerBase *handler_base = NULL;

    assert_harp(
        g_core->get_handler(g_core, &dep, &handler_base),
        "Failed to get Maestro Logger handler"
    );

    g_logger = (MaestroLoggerHandler *)handler_base;

    assert(g_logger != NULL);
    assert(g_logger->log  != NULL);
    assert(g_logger->logf != NULL);
    assert(g_logger->flush != NULL);

    /* handler is AVAILABLE but not yet VALID */
    assert(handler_base->status & HARP_STATUS_FLAG_SERVING);
    assert(!(handler_base->status & HARP_STATUS_FLAG_VALID));

    TEST_MARKER("LOGGER", "GET_HANDLER_DONE");
}

static void test_logger_fallback(void) {
    TEST_MARKER("LOGGER", "FALLBACK");

    /* these go through printf fallbacks — output should appear unformatted */
    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_INFO,  NULL,       "fallback info");
    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_DEBUG, "system",   "fallback debug");
    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_WARN,  "physics",  "fallback warning");
    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_ERROR, "renderer", "fallback error");

    g_logger->logf(g_logger, MAESTRO_LOGGER_LEVEL_INFO, "test", "fallback logf %d + %d = %d", 1, 2, 3);

    TEST_MARKER("LOGGER", "FALLBACK_DONE");
}

/* ========================================================= */
/* INITIALIZATION                                             */
/* ========================================================= */

static void test_logger_init_handler(void) {
    TEST_MARKER("LOGGER", "INIT_HANDLER");

    HarpCreatorBase creator = {
        .flags = HARP_CREATOR_FLAG_DEFAULT_CREATOR
    };

    assert_harp(
        g_core->handler_initialize(
            g_core,
            MAESTRO_LOGGER_HANDLER_NAME,
            &creator
        ),
        "Failed to initialize logger handler"
    );

    HarpHandlerBase *handler_base = (HarpHandlerBase *)g_logger;

    /* handler is now both AVAILABLE and VALID */
    assert(handler_base->status & HARP_STATUS_FLAG_SERVING);
    assert(handler_base->status & HARP_STATUS_FLAG_VALID);

    /* function pointers must still be populated */
    assert(g_logger->log   != NULL);
    assert(g_logger->logf  != NULL);
    assert(g_logger->flush != NULL);

    TEST_MARKER("LOGGER", "INIT_HANDLER_DONE");
}

/* ========================================================= */
/* REAL IMPLEMENTATION TESTS                                  */
/* ========================================================= */

static void test_basic_log(void) {
    TEST_MARKER("LOG", "BASIC");

    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_INFO,  NULL, "Hello from Maestro logger");
    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_DEBUG, NULL, "debug message");
    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_WARN,  NULL, "warning message");

    TEST_MARKER("LOG", "BASIC_DONE");
}

static void test_named_log(void) {
    TEST_MARKER("LOG", "NAMED");

    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_INFO,  "renderer", "Renderer subsystem initialized");
    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_WARN,  "physics",  "Physics running in fallback mode");
    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_DEBUG, "audio",    "Audio buffer size: 4096");

    TEST_MARKER("LOG", "NAMED_DONE");
}

static void test_formatted_log(void) {
    TEST_MARKER("LOG", "FORMATTED");

    g_logger->logf(g_logger, MAESTRO_LOGGER_LEVEL_INFO,  "math",   "result: %d", 42);
    g_logger->logf(g_logger, MAESTRO_LOGGER_LEVEL_DEBUG, "memory", "allocated %zu bytes at %p", (usize)1024, (void*)0xDEAD);
    g_logger->logf(g_logger, MAESTRO_LOGGER_LEVEL_WARN,  NULL,     "frame time: %.2f ms", 16.67f);

    TEST_MARKER("LOG", "FORMATTED_DONE");
}

static void test_error_autoflushed(void) {
    TEST_MARKER("LOG", "ERROR_AUTOFLUSH");

    /* errors should trigger an automatic flush */
    g_logger->log(g_logger,  MAESTRO_LOGGER_LEVEL_ERROR, "core",   "something went wrong");
    g_logger->logf(g_logger, MAESTRO_LOGGER_LEVEL_FATAL, "core",   "fatal: code %d", 99);

    TEST_MARKER("LOG", "ERROR_AUTOFLUSH_DONE");
}

static void test_explicit_flush(void) {
    TEST_MARKER("LOG", "FLUSH");

    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_INFO, "test", "about to flush");
    g_logger->flush(g_logger);

    /* a second flush on an empty buffer should be safe */
    g_logger->flush(g_logger);

    TEST_MARKER("LOG", "FLUSH_DONE");
}

static void test_null_cases(void) {
    TEST_MARKER("LOG", "NULL");

    /* null msg should be handled gracefully */
    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_INFO,  NULL, NULL);
    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_ERROR, NULL, NULL);

    /* null fmt in logf should be handled gracefully */
    g_logger->logf(g_logger, MAESTRO_LOGGER_LEVEL_INFO, NULL, NULL);

    TEST_MARKER("LOG", "NULL_DONE");
}

/* ========================================================= */
/* TEARDOWN                                                   */
/* ========================================================= */

static void test_logger_term_handler(void) {
    TEST_MARKER("LOGGER", "TERM_HANDLER");

    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_LOGGER_HANDLER_NAME),
        "Failed to terminate logger handler"
    );

    HarpHandlerBase *handler_base = (HarpHandlerBase *)g_logger;

    /* after termination: AVAILABLE (fallbacks rewired) but not VALID */
    assert(handler_base->status & HARP_STATUS_FLAG_SERVING);
    assert(!(handler_base->status & HARP_STATUS_FLAG_VALID));

    /* fallbacks should be callable again */
    g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_INFO, "test", "post-terminate fallback");

    TEST_MARKER("LOGGER", "TERM_HANDLER_DONE");
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
/* MAIN                                                       */
/* ========================================================= */

int main(int argc, char **argv) {
    printf("=== HARP / MAESTRO LOGGER TEST ===\n");

    test_runtime_init(argv[0]);
    test_maestro_register();

    /* verify fallback behavior before initialization */
    test_logger_get_handler();
    test_logger_fallback();

    /* initialize and verify state transition */
    test_logger_init_handler();

    /* real implementation tests */
    test_basic_log();
    test_named_log();
    test_formatted_log();
    test_error_autoflushed();
    test_explicit_flush();
    test_null_cases();

    /* verify fallback restored after termination */
    test_logger_term_handler();

    test_runtime_term();

    printf("ALL TESTS PASSED\n");
    return 0;
}