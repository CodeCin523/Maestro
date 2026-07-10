#include <harp/harp_api.h>

#include <maestro/maestro.h>
#include <maestro/maestro_logger.h>

#include <harp/utils/harp_version.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

/* ========================================================= */
/* CONFIG                                                     */
/* ========================================================= */

#define PERF_ITERATIONS         100000
#define PERF_ITERATIONS_FMT     10000
#define PERF_ITERATIONS_ERROR   100

#define PERF_RESULT_MAX         16

/* ========================================================= */
/* RESULTS                                                    */
/* ========================================================= */

typedef struct {
    const char *label;
    u64    ns;
    u64    iterations;
} PerfResult;

static PerfResult g_results[PERF_RESULT_MAX];
static u32   g_result_count = 0;

static void perf_record(const char *label, u64 ns, u64 iterations) {
    if(g_result_count >= PERF_RESULT_MAX)
        return;

    g_results[g_result_count].label      = label;
    g_results[g_result_count].ns         = ns;
    g_results[g_result_count].iterations = iterations;

    ++g_result_count;
}

static void perf_print_all(void) {
    fprintf(stderr, "%-44s  %12s  %12s\n", "benchmark", "total (ms)", "per op (ns)");
    fprintf(stderr, "%-44s  %12s  %12s\n",
        "--------------------------------------------",
        "----------",
        "-----------"
    );

    for(u32 i = 0; i < g_result_count; ++i) {
        PerfResult *r = &g_results[i];

        f64 total_ms  = (f64)r->ns / 1e6;
        f64 per_op_ns = (f64)r->ns / (f64)r->iterations;

        fprintf(stderr, "  %-42s  %10.3f ms  %8.2f ns\n", r->label, total_ms, per_op_ns);
    }
}

/* ========================================================= */
/* TIMING                                                     */
/* ========================================================= */

static inline u64 perf_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ULL + (u64)ts.tv_nsec;
}

/* ========================================================= */
/* HELPERS                                                    */
/* ========================================================= */

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

static void setup(char *argv0) {
    HarpRuntimeDesc desc = { .executable_path = argv0 };

    assert_harp(
        harp_initialize(&desc, &g_runtime),
        "harp_initialize"
    );

    HarpDependencyDesc core_dep = {
        .name        = HARP_CORE_HANDLER_NAME,
        .min_version = 0,
        .max_version = UINT32_MAX
    };

    HarpHandlerBase *core_base = NULL;

    assert_harp(
        harp_runtime_get_handler(g_runtime, &core_dep, &core_base),
        "get core handler"
    );

    g_core = (HarpCoreHandler *)core_base;

    extern HarpResult maestro_register(HarpCoreHandler *);

    assert_harp(
        maestro_register(g_core),
        "maestro_register"
    );

    HarpDependencyDesc logger_dep = {
        .name        = MAESTRO_LOGGER_HANDLER_NAME,
        .min_version = 0,
        .max_version = UINT32_MAX
    };

    HarpHandlerBase *logger_base = NULL;

    assert_harp(
        g_core->get_handler(g_core, &logger_dep, &logger_base),
        "get logger handler"
    );

    g_logger = (MaestroLoggerHandler *)logger_base;
}

static void setup_real_logger(void) {
    HarpCreatorBase creator = {
        .flags = HARP_CREATOR_FLAG_DEFAULT_CREATOR
    };

    assert_harp(
        g_core->handler_initialize(
            g_core,
            MAESTRO_LOGGER_HANDLER_NAME,
            &creator
        ),
        "handler_initialize"
    );
}

static void teardown_real_logger(void) {
    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_LOGGER_HANDLER_NAME),
        "handler_terminate"
    );
}

static void teardown(void) {
    assert_harp(
        harp_terminate(g_runtime),
        "harp_terminate"
    );
}

/* ========================================================= */
/* BENCHMARKS — FALLBACK                                      */
/* ========================================================= */

static void bench_fallback_log_no_name(void) {
    u64 t0 = perf_now_ns();

    for(u64 i = 0; i < PERF_ITERATIONS; ++i)
        g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_INFO, NULL, "hello world");

    fflush(stdout);

    perf_record("fallback log (no name)", perf_now_ns() - t0, PERF_ITERATIONS);
}

static void bench_fallback_log_named(void) {
    u64 t0 = perf_now_ns();

    for(u64 i = 0; i < PERF_ITERATIONS; ++i)
        g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_INFO, "renderer", "hello world");

    fflush(stdout);

    perf_record("fallback log (named)", perf_now_ns() - t0, PERF_ITERATIONS);
}

static void bench_fallback_logf(void) {
    u64 t0 = perf_now_ns();

    for(u64 i = 0; i < PERF_ITERATIONS_FMT; ++i)
        g_logger->logf(g_logger, MAESTRO_LOGGER_LEVEL_INFO, "system", "frame %llu dt=%.4f", (unsigned long long)i, 0.016f);

    fflush(stdout);

    perf_record("fallback logf (named, fmt)", perf_now_ns() - t0, PERF_ITERATIONS_FMT);
}

static void bench_fallback_warn(void) {
    u64 t0 = perf_now_ns();

    for(u64 i = 0; i < PERF_ITERATIONS; ++i)
        g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_WARN, "physics", "something is off");

    fflush(stdout);
    
    perf_record("fallback log WARN (named)", perf_now_ns() - t0, PERF_ITERATIONS);
}

/* ========================================================= */
/* BENCHMARKS — REAL IMPLEMENTATION                           */
/* ========================================================= */

static void bench_real_log_no_name(void) {
    u64 t0 = perf_now_ns();

    for(u64 i = 0; i < PERF_ITERATIONS; ++i)
        g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_INFO, NULL, "hello world");

    g_logger->flush(g_logger);

    perf_record("buffered log (no name)", perf_now_ns() - t0, PERF_ITERATIONS);
}

static void bench_real_log_named(void) {
    u64 t0 = perf_now_ns();

    for(u64 i = 0; i < PERF_ITERATIONS; ++i)
        g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_INFO, "renderer", "hello world");

    g_logger->flush(g_logger);

    perf_record("buffered log (named)", perf_now_ns() - t0, PERF_ITERATIONS);
}

static void bench_real_logf(void) {
    u64 t0 = perf_now_ns();

    for(u64 i = 0; i < PERF_ITERATIONS_FMT; ++i)
        g_logger->logf(g_logger, MAESTRO_LOGGER_LEVEL_INFO, "system", "frame %llu dt=%.4f", (unsigned long long)i, 0.016f);

    g_logger->flush(g_logger);

    perf_record("buffered logf (named, fmt)", perf_now_ns() - t0, PERF_ITERATIONS_FMT);
}

static void bench_real_warn(void) {
    u64 t0 = perf_now_ns();

    for(u64 i = 0; i < PERF_ITERATIONS; ++i)
        g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_WARN, "physics", "something is off");

    g_logger->flush(g_logger);

    perf_record("buffered log WARN (named)", perf_now_ns() - t0, PERF_ITERATIONS);
}

static void bench_real_error_autoflush(void) {
    u64 t0 = perf_now_ns();

    for(u64 i = 0; i < PERF_ITERATIONS_ERROR; ++i)
        g_logger->log(g_logger, MAESTRO_LOGGER_LEVEL_ERROR, "core", "critical failure");

    perf_record("buffered log ERROR (autoflush each)", perf_now_ns() - t0, PERF_ITERATIONS_ERROR);
}

/* ========================================================= */
/* MAIN                                                       */
/* ========================================================= */

int main(int argc, char **argv) {
    printf("=== MAESTRO LOGGER PERFORMANCE TEST ===\n\n");
    printf("  iterations (log):   %d\n", PERF_ITERATIONS);
    printf("  iterations (logf):  %d\n", PERF_ITERATIONS_FMT);
    printf("  iterations (error): %d\n\n", PERF_ITERATIONS_ERROR);

    setup(argv[0]);

    printf("running fallback benchmarks...\n");
    bench_fallback_log_no_name();
    bench_fallback_log_named();
    bench_fallback_logf();
    bench_fallback_warn();

    setup_real_logger();

    printf("running buffered benchmarks...\n\n");
    bench_real_log_no_name();
    bench_real_log_named();
    bench_real_logf();
    bench_real_warn();
    bench_real_error_autoflush();

    teardown_real_logger();
    teardown();

    printf("\n--- FALLBACK (printf) ---\n");
    perf_print_all();

    return 0;
}