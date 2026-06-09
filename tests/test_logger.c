#include <harp/harp_core.h>

#include <maestro/maestro.h>
#include <maestro/maestro_logger.h>

#undef HARP_UTILS_UNDEF
#include <harp/utils/harp_api.h>
#include <harp/utils/harp_version.h>

// #include <maestro_package.c>
HarpResult maestro_register(HarpCoreApi *core);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
/* GLOBALS (test runtime state)                               */
/* ========================================================= */

static HarpRuntime *g_runtime = NULL;
static HarpCoreApi *g_core = NULL;
static MaestroLoggerApi *g_logger = NULL;

/* ========================================================= */
/* SETUP                                                      */
/* ========================================================= */

static void test_runtime_init(char *argv0) {
    TEST_MARKER("RUNTIME", "INIT");

    HarpRuntimeCreator creator = {
        .argv0 = argv0
    };

    assert_harp(
        harp_initialize((HarpCreatorBase*)&creator, &g_runtime),
        "Failed to init Harp runtime"
    );

    HarpApiBase *core_base = NULL;

    HarpDependencyDesc dep = {
        HARP_CORE_API_NAME,
        0,
        UINT32_MAX
    };

    assert_harp(
        harp_runtime_get_api(g_runtime, &dep, &core_base),
        "Failed to get core API"
    );

    g_core = (HarpCoreApi*)core_base;

    TEST_MARKER("RUNTIME", "INIT_DONE");
}

/* ========================================================= */
/* MAESTRO REGISTRATION (THIS IS YOUR INSERTION POINT)       */
/* ========================================================= */

static void test_maestro_register(void) {
    TEST_MARKER("MAESTRO", "REGISTER");

    /* this is where YOU plug your package registration */
    assert_harp(
        maestro_register(g_core),
        "Failed to register Maestro package"
    );

    TEST_MARKER("MAESTRO", "REGISTER_DONE");
}

static void test_logger_init_handler(void) {
    TEST_MARKER("LOGGER", "INIT_HANDLER");

    HarpCreatorBase creator = {.flags = HARP_CREATOR_FLAG_DEFAULT_CREATOR};

    assert_harp(
        g_core->handler_initialize(
            g_core,
            MAESTRO_LOGGER_HANDLER_NAME,
            &creator
        ),
        "Failed to initialize logger handler"
    );

    TEST_MARKER("LOGGER", "INIT_HANDLER_DONE");
}

/* ========================================================= */
/* LOGGER API FETCH                                           */
/* ========================================================= */

static void test_logger_get_api(void) {
    TEST_MARKER("LOGGER", "GET_API");

    HarpApiBase *api_base = NULL;
    HarpDependencyDesc dep = {
        MAESTRO_LOGGER_API_NAME,
        MAESTRO_LOGGER_API_VERSION,
        MAESTRO_LOGGER_API_VERSION
    };

    assert(g_core != NULL);
    assert_harp(
        g_core->get_api(g_core, &dep, &api_base),
        "Failed to get Maestro Logger API"
    );

    g_logger = (MaestroLoggerApi*)api_base;
if (!g_logger) {
    fprintf(stderr, "[LOGGER FATAL] g_logger is NULL\n");
    exit(1);
}

if (!g_logger->log_info) {
    fprintf(stderr, "[LOGGER FATAL] log_info is NULL\n");
    exit(1);
}

if (!g_logger->log_debug) {
    fprintf(stderr, "[LOGGER FATAL] log_debug is NULL\n");
    exit(1);
}

if (!g_logger->log_warning) {
    fprintf(stderr, "[LOGGER FATAL] log_warning is NULL\n");
    exit(1);
}

if (!g_logger->log_error) {
    fprintf(stderr, "[LOGGER FATAL] log_error is NULL\n");
    exit(1);
}

    TEST_MARKER("LOGGER", "GET_API_DONE");
}

/* ========================================================= */
/* TESTS                                                      */
/* ========================================================= */

static void test_basic_log(void) {
    TEST_MARKER("LOG", "BASIC");
if (!g_logger) {
    fprintf(stderr, "[LOGGER FATAL] g_logger is NULL\n");
    exit(1);
}

if (!g_logger->log_info) {
    fprintf(stderr, "[LOGGER FATAL] log_info is NULL\n");
    exit(1);
}

if (!g_logger->log_debug) {
    fprintf(stderr, "[LOGGER FATAL] log_debug is NULL\n");
    exit(1);
}

if (!g_logger->log_warning) {
    fprintf(stderr, "[LOGGER FATAL] log_warning is NULL\n");
    exit(1);
}

if (!g_logger->log_error) {
    fprintf(stderr, "[LOGGER FATAL] log_error is NULL\n");
    exit(1);
}

    g_logger->log_info(g_logger, NULL, "Hello from Maestro logger");

    TEST_MARKER("LOG", "BASIC_DONE");
}

static void test_named_log(void) {
    TEST_MARKER("LOG", "NAMED");

    g_logger->log_info(
        g_logger,
        "renderer",
        "Renderer subsystem initialized"
    );

    g_logger->log_warning(
        g_logger,
        "physics",
        "Physics running in fallback mode"
    );

    TEST_MARKER("LOG", "NAMED_DONE");
}

static void test_null_cases(void) {
    TEST_MARKER("LOG", "NULL");

    g_logger->log_error(g_logger, NULL, NULL);
    g_logger->log_info(g_logger, NULL, NULL);

    TEST_MARKER("LOG", "NULL_DONE");
}

/* ========================================================= */
/* TEARDOWN                                                  */
/* ========================================================= */

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

    /* insertion point for your package system */
    test_maestro_register();
    test_logger_init_handler();
    test_logger_get_api();

    /* actual logger validation */
    test_basic_log();
    test_named_log();
    test_null_cases();

    test_runtime_term();

    printf("ALL TESTS PASSED\n");
    return 0;
}