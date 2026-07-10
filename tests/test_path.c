#include <harp/harp_api.h>

#include <maestro/maestro.h>
#include <maestro/maestro_logger.h>
#include <maestro/maestro_path.h>

#include <harp/utils/harp_platform.h>
#include <harp/utils/harp_version.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if HARP_PLATFORM_WINDOWS
#include <direct.h>
#define TEST_MKDIR(p) _mkdir(p)
#define TEST_RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define TEST_MKDIR(p) mkdir(p, 0755)
#define TEST_RMDIR(p) rmdir(p)
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

static HarpRuntime        *g_runtime = NULL;
static HarpCoreHandler    *g_core    = NULL;
static MaestroPathHandler *g_path    = NULL;

/* Playground directory created under CWD, removed at the end. */
#define TEST_DIR_NAME "maestro_path_test_dir"
static char g_test_dir[MAESTRO_PATH_MAX];

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

extern HarpResult maestro_register(HarpCoreHandler *);

static void test_maestro_register(void) {
    TEST_MARKER("MAESTRO", "REGISTER");

    assert_harp(
        maestro_register(g_core),
        "Failed to register Maestro package"
    );

    TEST_MARKER("MAESTRO", "REGISTER_DONE");
}

static void test_path_init_handler(void) {
    TEST_MARKER("PATH", "INIT_HANDLER");

    HarpDependencyDesc dep = {
        .name        = MAESTRO_PATH_HANDLER_NAME,
        .min_version = 0,
        .max_version = UINT32_MAX
    };

    HarpHandlerBase *handler_base = NULL;

    assert_harp(
        g_core->get_handler(g_core, &dep, &handler_base),
        "Failed to get Maestro Path handler"
    );

    g_path = (MaestroPathHandler *)handler_base;

    assert(g_path->make      != NULL);
    assert(g_path->makef     != NULL);
    assert(g_path->info      != NULL);
    assert(g_path->enumerate != NULL);

    MaestroPathCreator creator = {
        .app_name     = "maestro_path_test",
        .default_path = NULL
    };

    assert_harp(
        g_core->handler_initialize(g_core, MAESTRO_PATH_HANDLER_NAME, &creator._base),
        "Failed to initialize path handler"
    );

    assert(handler_base->status & HARP_STATUS_FLAG_VALID);

    TEST_MARKER("PATH", "INIT_HANDLER_DONE");
}

/* ========================================================= */
/* BASES                                                      */
/* ========================================================= */

static void test_bases(void) {
    TEST_MARKER("PATH", "BASES");

    for(MaestroPathBase b = 0; b < MAESTRO_PATH_BASE_COUNT; ++b) {
        const char *base = g_path->bases[b];
        assert(base[0] != '\0');
        /* no trailing separator */
        usize len = strlen(base);
        if(len > 1)
            assert(base[len - 1] != '/' && base[len - 1] != '\\');
        printf("  base[%u] = %s\n", b, base);
    }

    /* NULL default_path in the creator makes DEFAULT the exe directory */
    assert(strcmp(g_path->bases[MAESTRO_PATH_BASE_DEFAULT],
                  g_path->bases[MAESTRO_PATH_BASE_EXE]) == 0);

    /* CONFIG and SAVE end with the app name and were created on disk */
    const char *config = g_path->bases[MAESTRO_PATH_BASE_CONFIG];
    assert(strlen(config) > strlen("maestro_path_test"));
    assert(strcmp(config + strlen(config) - strlen("maestro_path_test"),
                  "maestro_path_test") == 0);

    MaestroPathInfo info = {0};
    assert_harp(g_path->info(g_path, config, &info), "CONFIG dir was not created");
    assert(info.flags & MAESTRO_PATH_ENTRY_DIR);
    assert_harp(g_path->info(g_path, g_path->bases[MAESTRO_PATH_BASE_SAVE], &info),
                "SAVE dir was not created");
    assert(info.flags & MAESTRO_PATH_ENTRY_DIR);

    TEST_MARKER("PATH", "BASES_DONE");
}

/* ========================================================= */
/* MAKE / MAKEF                                               */
/* ========================================================= */

static void test_make(void) {
    TEST_MARKER("PATH", "MAKE");

    const char *cwd = g_path->bases[MAESTRO_PATH_BASE_CWD];
    char buf[MAESTRO_PATH_MAX];
    char expect[MAESTRO_PATH_MAX];

    /* simple join */
    usize len = g_path->make(g_path, MAESTRO_PATH_BASE_CWD, buf, sizeof(buf), "assets/something.txt");
    assert(len > 0 && len == strlen(buf));
#if HARP_PLATFORM_WINDOWS
    snprintf(expect, sizeof(expect), "%s\\assets\\something.txt", cwd);
#else
    snprintf(expect, sizeof(expect), "%s/assets/something.txt", cwd);
#endif
    assert(strcmp(buf, expect) == 0);

    /* NULL relative returns the base itself */
    len = g_path->make(g_path, MAESTRO_PATH_BASE_CWD, buf, sizeof(buf), NULL);
    assert(len == strlen(cwd) && strcmp(buf, cwd) == 0);

    /* "." and ".." are collapsed */
    len = g_path->make(g_path, MAESTRO_PATH_BASE_CWD, buf, sizeof(buf), "./a/../b//c");
#if HARP_PLATFORM_WINDOWS
    snprintf(expect, sizeof(expect), "%s\\b\\c", cwd);
#else
    snprintf(expect, sizeof(expect), "%s/b/c", cwd);
#endif
    assert(len > 0 && strcmp(buf, expect) == 0);

    /* escaping the base fails */
    assert(g_path->make(g_path, MAESTRO_PATH_BASE_CWD, buf, sizeof(buf), "../escape") == 0);
    assert(g_path->make(g_path, MAESTRO_PATH_BASE_CWD, buf, sizeof(buf), "a/../../escape") == 0);

    /* invalid base and too-small buffer fail */
    assert(g_path->make(g_path, MAESTRO_PATH_BASE_COUNT, buf, sizeof(buf), "x") == 0);
    assert(g_path->make(g_path, MAESTRO_PATH_BASE_CWD, buf, 4, "assets/x") == 0);

    /* formatted variant */
    len = g_path->makef(g_path, MAESTRO_PATH_BASE_CWD, buf, sizeof(buf),
                        "worldgen/%s_%d.json", "chunk", 42);
#if HARP_PLATFORM_WINDOWS
    snprintf(expect, sizeof(expect), "%s\\worldgen\\chunk_42.json", cwd);
#else
    snprintf(expect, sizeof(expect), "%s/worldgen/chunk_42.json", cwd);
#endif
    assert(len > 0 && strcmp(buf, expect) == 0);

    /* a formatted traversal is still rejected */
    assert(g_path->makef(g_path, MAESTRO_PATH_BASE_CWD, buf, sizeof(buf), "saves/%s", "../../etc") == 0);

    TEST_MARKER("PATH", "MAKE_DONE");
}

/* ========================================================= */
/* INFO / ENUMERATE                                           */
/* ========================================================= */

static void make_test_file(const char *dir, const char *name, const char *content) {
    char buf[MAESTRO_PATH_MAX];
    snprintf(buf, sizeof(buf), "%s/%s", dir, name);
    FILE *f = fopen(buf, "wb");
    assert(f != NULL);
    fputs(content, f);
    fclose(f);
}

static void remove_test_file(const char *dir, const char *name) {
    char buf[MAESTRO_PATH_MAX];
    snprintf(buf, sizeof(buf), "%s/%s", dir, name);
    remove(buf);
}

static void test_playground_setup(void) {
    TEST_MARKER("PATH", "PLAYGROUND_SETUP");

    usize len = g_path->make(g_path, MAESTRO_PATH_BASE_CWD,
                              g_test_dir, sizeof(g_test_dir), TEST_DIR_NAME);
    assert(len > 0);

    TEST_MKDIR(g_test_dir);

    char sub[MAESTRO_PATH_MAX];
    snprintf(sub, sizeof(sub), "%s/sub", g_test_dir);
    TEST_MKDIR(sub);

    make_test_file(g_test_dir, "a.txt", "aaaa");
    make_test_file(g_test_dir, "b.txt", "bb");

    TEST_MARKER("PATH", "PLAYGROUND_SETUP_DONE");
}

static void test_info(void) {
    TEST_MARKER("PATH", "INFO");

    char buf[MAESTRO_PATH_MAX];
    MaestroPathInfo info = {0};

    g_path->make(g_path, MAESTRO_PATH_BASE_CWD, buf, sizeof(buf), TEST_DIR_NAME "/a.txt");
    assert_harp(g_path->info(g_path, buf, &info), "info on a.txt failed");
    assert(info.flags == MAESTRO_PATH_ENTRY_FILE);
    assert(info.size == 4);
    assert(info.mtime > 0);

    assert_harp(g_path->info(g_path, g_test_dir, &info), "info on test dir failed");
    assert(info.flags == MAESTRO_PATH_ENTRY_DIR);
    assert(info.size == 0);

    /* nothing there */
    g_path->make(g_path, MAESTRO_PATH_BASE_CWD, buf, sizeof(buf), TEST_DIR_NAME "/missing.txt");
    assert(g_path->info(g_path, buf, &info) != HARP_RESULT_OK);

    /* argument validation */
    assert(g_path->info(g_path, NULL, &info) == HARP_RESULT_INVALID_ARGUMENTS);
    assert(g_path->info(g_path, buf, NULL) == HARP_RESULT_MISSING_OUTPUT);

    TEST_MARKER("PATH", "INFO_DONE");
}

static void test_enumerate(void) {
    TEST_MARKER("PATH", "ENUMERATE");

    u32 count = 0;

    /* count pass: 2 files */
    assert_harp(g_path->enumerate(g_path, g_test_dir, MAESTRO_PATH_ENTRY_FILE, &count, NULL),
                "count files failed");
    assert(count == 2);

    /* count pass: 1 directory */
    assert_harp(g_path->enumerate(g_path, g_test_dir, MAESTRO_PATH_ENTRY_DIR, &count, NULL),
                "count dirs failed");
    assert(count == 1);

    /* fill pass */
    MaestroPathEntry entries[8];
    count = 8;
    assert_harp(g_path->enumerate(g_path, g_test_dir,
                                  MAESTRO_PATH_ENTRY_FILE | MAESTRO_PATH_ENTRY_DIR,
                                  &count, entries),
                "fill pass failed");
    assert(count == 3);

    b8 seen_a = 0, seen_b = 0, seen_sub = 0;
    for(u32 i = 0; i < count; ++i) {
        if(strcmp(entries[i].name, "a.txt") == 0) {
            assert(entries[i].flags == MAESTRO_PATH_ENTRY_FILE);
            seen_a = 1;
        } else if(strcmp(entries[i].name, "b.txt") == 0) {
            assert(entries[i].flags == MAESTRO_PATH_ENTRY_FILE);
            seen_b = 1;
        } else if(strcmp(entries[i].name, "sub") == 0) {
            assert(entries[i].flags == MAESTRO_PATH_ENTRY_DIR);
            seen_sub = 1;
        }
    }
    assert(seen_a && seen_b && seen_sub);

    /* get_actors-style clamping: capacity 1 fills 1 and still returns OK */
    count = 1;
    assert_harp(g_path->enumerate(g_path, g_test_dir, MAESTRO_PATH_ENTRY_FILE, &count, entries),
                "clamped fill failed");
    assert(count == 1);

    /* enumerating a missing directory */
    char buf[MAESTRO_PATH_MAX];
    g_path->make(g_path, MAESTRO_PATH_BASE_CWD, buf, sizeof(buf), TEST_DIR_NAME "/missing");
    count = 0;
    assert(g_path->enumerate(g_path, buf, MAESTRO_PATH_ENTRY_FILE, &count, NULL)
           == HARP_RESULT_NAME_NOT_FOUND);

    TEST_MARKER("PATH", "ENUMERATE_DONE");
}

static void test_playground_teardown(void) {
    TEST_MARKER("PATH", "PLAYGROUND_TEARDOWN");

    remove_test_file(g_test_dir, "a.txt");
    remove_test_file(g_test_dir, "b.txt");

    char sub[MAESTRO_PATH_MAX];
    snprintf(sub, sizeof(sub), "%s/sub", g_test_dir);
    TEST_RMDIR(sub);
    TEST_RMDIR(g_test_dir);

    TEST_MARKER("PATH", "PLAYGROUND_TEARDOWN_DONE");
}

/* ========================================================= */
/* TEARDOWN                                                   */
/* ========================================================= */

static void test_path_term_handler(void) {
    TEST_MARKER("PATH", "TERM_HANDLER");

    /* drop the CONFIG/SAVE dirs init created; rmdir only removes them empty */
    TEST_RMDIR(g_path->bases[MAESTRO_PATH_BASE_CONFIG]);
    TEST_RMDIR(g_path->bases[MAESTRO_PATH_BASE_SAVE]);

    assert_harp(
        g_core->handler_terminate(g_core, MAESTRO_PATH_HANDLER_NAME),
        "Failed to terminate path handler"
    );

    HarpHandlerBase *handler_base = (HarpHandlerBase *)g_path;
    assert(!(handler_base->status & HARP_STATUS_FLAG_VALID));

    TEST_MARKER("PATH", "TERM_HANDLER_DONE");
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
    printf("=== HARP / MAESTRO PATH TEST ===\n");

    test_runtime_init(argv[0]);
    test_maestro_register();

    test_path_init_handler();

    test_bases();
    test_make();

    test_playground_setup();
    test_info();
    test_enumerate();
    test_playground_teardown();

    test_path_term_handler();
    test_runtime_term();

    printf("ALL TESTS PASSED\n");
    return 0;
}
