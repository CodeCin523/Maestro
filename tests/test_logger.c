#include <harp/harp_core.h>

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

    TEST_MARKER("LOGGER", "INIT_HANDLER_DONE");
}

/* ========================================================= */
/* LOGGER HANDLER FETCH                                       */
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
    assert(g_logger->log_info    != NULL);
    assert(g_logger->log_debug   != NULL);
    assert(g_logger->log_warning != NULL);
    assert(g_logger->log_error   != NULL);

    TEST_MARKER("LOGGER", "GET_HANDLER_DONE");
}

/* ========================================================= */
/* TESTS                                                      */
/* ========================================================= */

static void test_basic_log(void) {
    TEST_MARKER("LOG", "BASIC");

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
/* TEARDOWN                                                   */
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
    test_maestro_register();
    test_logger_init_handler();
    test_logger_get_handler();

    test_basic_log();
    test_named_log();
    test_null_cases();

    test_runtime_term();

    printf("ALL TESTS PASSED\n");
    return 0;
}