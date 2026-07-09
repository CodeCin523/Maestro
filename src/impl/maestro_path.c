#include "impl/maestro_path.h"

#include "maestro_globals.h"

#include <harp/utils/harp_helpers.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HARP_PLATFORM_WINDOWS
#include <windows.h>
#elif HARP_PLATFORM_LINUX
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#endif


#if HARP_PLATFORM_WINDOWS
#define PATH_SEP '\\'
#define PATH_IS_SEP(c) ((c) == '/' || (c) == '\\')  // tolerate native separators in relative
#else
#define PATH_SEP '/'
#define PATH_IS_SEP(c) ((c) == '/')
#endif


/* ================================================================================ */
/*  MAKE                                                                            */
/* ================================================================================ */

static size_t join_normalize(const char *base, const char *relative, char *buf, size_t buf_size) {
    size_t base_len = strlen(base);
    if(base_len + 1 > buf_size)
        return 0;
    memcpy(buf, base, base_len);

    size_t len = base_len;
    const char *s = relative ? relative : "";

    while(*s) {
        while(PATH_IS_SEP(*s))
            ++s;

        const char *seg = s;
        while(*s && !PATH_IS_SEP(*s))
            ++s;
        size_t seg_len = (size_t)(s - seg);

        if(seg_len == 0)
            break;
        if(seg_len == 1 && seg[0] == '.')
            continue;

        if(seg_len == 2 && seg[0] == '.' && seg[1] == '.') {
            if(len == base_len)
                return 0;
            while(len > base_len && buf[len - 1] != PATH_SEP)
                --len;
            if(len > base_len)
                --len;
            continue;
        }

        if(len + 1 + seg_len + 1 > buf_size)
            return 0;
        if(len == 0 || buf[len - 1] != PATH_SEP)
            buf[len++] = PATH_SEP;
        memcpy(buf + len, seg, seg_len);
        len += seg_len;
    }

    buf[len] = '\0';
    return len;
}

size_t path_make(MaestroPathHandler *h, MaestroPathBase base, char *buf, size_t buf_size, const char *relative) {
    if(!HARP_HANDLER_IS_VALID(h) || !buf || buf_size == 0 || base >= MAESTRO_PATH_BASE_COUNT)
        return 0;

    return join_normalize(h->bases[base], relative, buf, buf_size);
}
size_t path_makef(MaestroPathHandler *h, MaestroPathBase base, char *buf, size_t buf_size, const char *fmt, ...) {
    if(!fmt)
        return path_make(h, base, buf, buf_size, NULL);

    char relative[MAESTRO_PATH_MAX];

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(relative, sizeof(relative), fmt, args);
    va_end(args);

    if(written < 0 || (size_t)written >= sizeof(relative))
        return 0;

    return path_make(h, base, buf, buf_size, relative);
}


/* ================================================================================ */
/*  INFO / ENUMERATE                                                                */
/* ================================================================================ */

#if HARP_PLATFORM_LINUX

HarpResult path_info(MaestroPathHandler *h, const char *path, MaestroPathInfo *out_info) {
    HARP_CHECK_STATE(HARP_HANDLER_IS_VALID(h), HARP_RESULT_INVALID_STATE);
    HARP_CHECK_ARG(path != NULL, HARP_RESULT_INVALID_ARGUMENTS);
    HARP_CHECK_ARG(out_info != NULL, HARP_RESULT_MISSING_OUTPUT);

    struct stat st;
    if(stat(path, &st) != 0)
        return HARP_RESULT_FAILED;

    if(S_ISDIR(st.st_mode)) {
        out_info->flags = MAESTRO_PATH_ENTRY_DIR;
        out_info->size  = 0;
    } else {
        out_info->flags = S_ISREG(st.st_mode) ? MAESTRO_PATH_ENTRY_FILE : MAESTRO_PATH_ENTRY_OTHER;
        out_info->size  = (uint64_t)st.st_size;
    }
    out_info->mtime = (int64_t)st.st_mtime;

    return HARP_RESULT_OK;
}

HarpResult path_enumerate(MaestroPathHandler *h, const char *path, MaestroPathEntryFlags filter, uint32_t *count, MaestroPathEntry *entries) {
    HARP_CHECK_STATE(HARP_HANDLER_IS_VALID(h), HARP_RESULT_INVALID_STATE);
    HARP_CHECK_ARG(path != NULL, HARP_RESULT_INVALID_ARGUMENTS);
    HARP_CHECK_ARG(count != NULL, HARP_RESULT_MISSING_OUTPUT);

    DIR *dir = opendir(path);
    if(!dir)
        return (errno == ENOENT || errno == ENOTDIR) ? HARP_RESULT_NAME_NOT_FOUND : HARP_RESULT_FAILED;

    uint32_t capacity = entries ? *count : 0;
    uint32_t matched  = 0;

    struct dirent *ent;
    while((ent = readdir(dir)) != NULL) {
        if(entries && matched == capacity)
            break;

        if(ent->d_name[0] == '.' && (ent->d_name[1] == '\0' ||
           (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;

        MaestroPathEntryFlags flags;
        switch(ent->d_type) {
            case DT_REG: flags = MAESTRO_PATH_ENTRY_FILE; break;
            case DT_DIR: flags = MAESTRO_PATH_ENTRY_DIR;  break;
            default: {
                // DT_UNKNOWN or symlink: resolve with a follow-links stat
                struct stat st;
                if(fstatat(dirfd(dir), ent->d_name, &st, 0) != 0)
                    flags = MAESTRO_PATH_ENTRY_OTHER;
                else if(S_ISDIR(st.st_mode))
                    flags = MAESTRO_PATH_ENTRY_DIR;
                else if(S_ISREG(st.st_mode))
                    flags = MAESTRO_PATH_ENTRY_FILE;
                else
                    flags = MAESTRO_PATH_ENTRY_OTHER;
            } break;
        }

        if(!(flags & filter))
            continue;

        if(entries) {
            snprintf(entries[matched].name, sizeof(entries[matched].name), "%s", ent->d_name);
            entries[matched].flags = flags;
        }
        ++matched;
    }

    closedir(dir);

    *count = matched;
    return HARP_RESULT_OK;
}

static int mkdir_p(const char *path) {
    char buf[MAESTRO_PATH_MAX];
    size_t len = strlen(path);
    if(len + 1 > sizeof(buf))
        return -1;
    memcpy(buf, path, len + 1);

    for(size_t i = 1; i < len; ++i) {
        if(buf[i] != PATH_SEP)
            continue;
        buf[i] = '\0';
        if(mkdir(buf, 0755) != 0 && errno != EEXIST)
            return -1;
        buf[i] = PATH_SEP;
    }
    if(mkdir(buf, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

// "<env or HOME fallback>/<app_name>"; fallback_dir as is when neither is set
static int resolve_user_base(char *out, size_t out_size,
                             const char *env_name, const char *home_suffix,
                             const char *app_name, const char *fallback_dir) {
    const char *env = getenv(env_name);
    int written;

    if(env && env[0]) {
        written = snprintf(out, out_size, "%s/%s", env, app_name);
    } else {
        const char *home = getenv("HOME");
        if(home && home[0])
            written = snprintf(out, out_size, "%s/%s/%s", home, home_suffix, app_name);
        else
            written = snprintf(out, out_size, "%s", fallback_dir);
    }

    return (written < 0 || (size_t)written >= out_size) ? -1 : 0;
}

#elif HARP_PLATFORM_WINDOWS

// FILETIME is 100 ns ticks since 1601-01-01; unix epoch is 11644473600 s later.
static int64_t filetime_to_unix(FILETIME ft) {
    uint64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (int64_t)(ticks / 10000000ULL) - 11644473600LL;
}

HarpResult path_info(MaestroPathHandler *h, const char *path, MaestroPathInfo *out_info) {
    HARP_CHECK_STATE(HARP_HANDLER_IS_VALID(h), HARP_RESULT_INVALID_STATE);
    HARP_CHECK_ARG(path != NULL, HARP_RESULT_INVALID_ARGUMENTS);
    HARP_CHECK_ARG(out_info != NULL, HARP_RESULT_MISSING_OUTPUT);

    WIN32_FILE_ATTRIBUTE_DATA data;
    if(!GetFileAttributesExA(path, GetFileExInfoStandard, &data))
        return HARP_RESULT_FAILED;

    if(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        out_info->flags = MAESTRO_PATH_ENTRY_DIR;
        out_info->size  = 0;
    } else {
        out_info->flags = MAESTRO_PATH_ENTRY_FILE;
        out_info->size  = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    }
    out_info->mtime = filetime_to_unix(data.ftLastWriteTime);

    return HARP_RESULT_OK;
}

HarpResult path_enumerate(MaestroPathHandler *h, const char *path,
                          MaestroPathEntryFlags filter,
                          uint32_t *count, MaestroPathEntry *entries) {
    HARP_CHECK_ARG(path != NULL, HARP_RESULT_INVALID_ARGUMENTS);
    HARP_CHECK_ARG(count != NULL, HARP_RESULT_MISSING_OUTPUT);

    char pattern[MAESTRO_PATH_MAX];
    int written = snprintf(pattern, sizeof(pattern), "%s\\*", path);
    if(written < 0 || (size_t)written >= sizeof(pattern))
        return HARP_RESULT_INVALID_ARGUMENTS;

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if(find == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        return (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            ? HARP_RESULT_NAME_NOT_FOUND : HARP_RESULT_FAILED;
    }

    uint32_t capacity = entries ? *count : 0;
    uint32_t matched  = 0;

    do {
        if(entries && matched == capacity)
            break;

        if(data.cFileName[0] == '.' && (data.cFileName[1] == '\0' ||
           (data.cFileName[1] == '.' && data.cFileName[2] == '\0')))
            continue;

        MaestroPathEntryFlags flags = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            ? MAESTRO_PATH_ENTRY_DIR : MAESTRO_PATH_ENTRY_FILE;

        if(!(flags & filter))
            continue;

        if(entries) {
            snprintf(entries[matched].name, sizeof(entries[matched].name), "%s", data.cFileName);
            entries[matched].flags = flags;
        }
        ++matched;
    } while(FindNextFileA(find, &data));

    FindClose(find);

    *count = matched;
    return HARP_RESULT_OK;
}

static int mkdir_p(const char *path) {
    char buf[MAESTRO_PATH_MAX];
    size_t len = strlen(path);
    if(len + 1 > sizeof(buf))
        return -1;
    memcpy(buf, path, len + 1);

    // skip the drive root ("C:\")
    size_t start = (len >= 3 && buf[1] == ':' && buf[2] == PATH_SEP) ? 3 : 1;

    for(size_t i = start; i < len; ++i) {
        if(buf[i] != PATH_SEP)
            continue;
        buf[i] = '\0';
        if(!CreateDirectoryA(buf, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            return -1;
        buf[i] = PATH_SEP;
    }
    if(!CreateDirectoryA(buf, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return -1;
    return 0;
}

static int resolve_user_base(char *out, size_t out_size,
                             const char *env_name, const char *home_suffix,
                             const char *app_name, const char *fallback_dir) {
    HARP_UNUSED(home_suffix);

    const char *env = getenv(env_name);
    int written;

    if(env && env[0])
        written = snprintf(out, out_size, "%s\\%s", env, app_name);
    else
        written = snprintf(out, out_size, "%s", fallback_dir);

    return (written < 0 || (size_t)written >= out_size) ? -1 : 0;
}

#endif


/* ================================================================================ */
/*  PATH HANDLER                                                                    */
/* ================================================================================ */

// rejects truncation, strips trailing separators (a bare root keeps its one)
static int set_base(MaestroPathHandler *pub, MaestroPathBase base, const char *src) {
    size_t len = strlen(src);
    if(len == 0 || len + 1 > MAESTRO_PATH_MAX)
        return -1;

    memcpy(pub->bases[base], src, len + 1);
    while(len > 1 && PATH_IS_SEP(pub->bases[base][len - 1]))
        pub->bases[base][--len] = '\0';
    return 0;
}

HarpResult init_path(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator) {
    MaestroPathCreator path_creator = {
        .app_name     = "maestro",
        .default_path = NULL
    };
    if(!(creator->flags & HARP_CREATOR_FLAG_DEFAULT_CREATOR)) {
        path_creator = *(MaestroPathCreator *)creator;
        if(!path_creator.app_name || !path_creator.app_name[0])
            path_creator.app_name = "maestro";
    }

    MaestroPathHandler *handler = (MaestroPathHandler *) base;


    const char *exe_dir = NULL;
    const char *cwd_dir = NULL;
    if(core_handler->get_executable_directory(core_handler, &exe_dir) != HARP_RESULT_OK || !exe_dir) {
        MAESTRO_LOG_FATAL(g_logger, base->name, "Failed to query executable directory");
        return HARP_RESULT_FAILED;
    }
    if(core_handler->get_working_directory(core_handler, &cwd_dir) != HARP_RESULT_OK || !cwd_dir) {
        MAESTRO_LOG_FATAL(g_logger, base->name, "Failed to query working directory");
        return HARP_RESULT_FAILED;
    }

    if(set_base(handler, MAESTRO_PATH_BASE_EXE, exe_dir) != 0 ||
       set_base(handler, MAESTRO_PATH_BASE_CWD, cwd_dir) != 0 ||
       set_base(handler, MAESTRO_PATH_BASE_DEFAULT,
                path_creator.default_path ? path_creator.default_path : exe_dir) != 0) {
        MAESTRO_LOG_FATAL(g_logger, base->name, "Base directory exceeds MAESTRO_PATH_MAX");
        return HARP_RESULT_FAILED;
    }

    char user_dir[MAESTRO_PATH_MAX];
    const struct {
        MaestroPathBase base;
        const char *env_name;
        const char *home_suffix;
    } user_bases[] = {
#if HARP_PLATFORM_WINDOWS
        {MAESTRO_PATH_BASE_CONFIG, "APPDATA",         NULL},
        {MAESTRO_PATH_BASE_SAVE,   "LOCALAPPDATA",    NULL},
#else
        {MAESTRO_PATH_BASE_CONFIG, "XDG_CONFIG_HOME", ".config"},
        {MAESTRO_PATH_BASE_SAVE,   "XDG_DATA_HOME",   ".local/share"},
#endif
    };

    for(size_t i = 0; i < sizeof(user_bases) / sizeof(user_bases[0]); ++i) {
        if(resolve_user_base(user_dir, sizeof(user_dir),
                             user_bases[i].env_name, user_bases[i].home_suffix,
                             path_creator.app_name, handler->bases[MAESTRO_PATH_BASE_EXE]) != 0 ||
           set_base(handler, user_bases[i].base, user_dir) != 0) {
            MAESTRO_LOG_FATAL(g_logger, base->name, "User directory exceeds MAESTRO_PATH_MAX");
            return HARP_RESULT_FAILED;
        }

        if(mkdir_p(handler->bases[user_bases[i].base]) != 0)
            MAESTRO_LOGF_WARN(g_logger, base->name,
                "Failed to create directory '%s'", handler->bases[user_bases[i].base]);
    }

    return HARP_RESULT_OK;
}

HarpResult term_path(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);

    MaestroPathHandler *handler = (MaestroPathHandler *)base;

    memset(handler->bases, 0, sizeof(handler->bases));

    return HARP_RESULT_OK;
}

HarpResult patch_path(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);
    HARP_UNUSED(base);

    // swap-time registration already refreshed the function pointers and
    // nothing outside the module points at path code
    return HARP_RESULT_OK;
}
