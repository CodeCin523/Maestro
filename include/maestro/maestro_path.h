#ifndef MAESTRO_PATH_H
#define MAESTRO_PATH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <harp/harp.h>
#include <harp/utils/harp_version.h>


/* ================================================================================ */
/*  TYPEDEF                                                                         */
/* ================================================================================ */

// Longest path the handler will produce, including the NUL terminator.
#define MAESTRO_PATH_MAX 512

// 255 is the practical name limit on both Linux and Windows.
#define MAESTRO_PATH_NAME_MAX 256

typedef u8 MaestroPathBase;
enum {
    MAESTRO_PATH_BASE_DEFAULT = 0,  // creator-provided root; falls back to the exe directory
    MAESTRO_PATH_BASE_EXE,          // directory of the running executable
    MAESTRO_PATH_BASE_CONFIG,       // XDG_CONFIG_HOME/<app> on Linux, %APPDATA%\<app> on Windows
    MAESTRO_PATH_BASE_SAVE,         // XDG_DATA_HOME/<app> on Linux, %LOCALAPPDATA%\<app> on Windows
    MAESTRO_PATH_BASE_CWD,          // working directory at init time
    MAESTRO_PATH_BASE_COUNT
};

typedef u8 MaestroPathEntryFlags;
enum {
    MAESTRO_PATH_ENTRY_FILE  = 1 << 0,
    MAESTRO_PATH_ENTRY_DIR   = 1 << 1,
    MAESTRO_PATH_ENTRY_OTHER = 1 << 2,  // socket, device, broken symlink, ...
};

typedef struct MaestroPathInfo {
    u64 size;   // bytes; 0 for directories
    i64  mtime;  // last modification, unix seconds
    MaestroPathEntryFlags flags;
} MaestroPathInfo;

typedef struct MaestroPathEntry {
    char name[MAESTRO_PATH_NAME_MAX];
    MaestroPathEntryFlags flags;
} MaestroPathEntry;

typedef struct MaestroPathCreator MaestroPathCreator;
typedef struct MaestroPathHandler MaestroPathHandler;


/* ================================================================================ */
/*  HANDLER                                                                         */
/* ================================================================================ */

#define MAESTRO_PATH_HANDLER_NAME "MaestroPathHandler"
#define MAESTRO_PATH_HANDLER_VERSION HARP_MAKE_VERSION(1,0,0)

struct MaestroPathCreator {
    HarpCreatorBase _base;

    const char *app_name;      // names the CONFIG and SAVE subdirectories
    const char *default_path;  // absolute root for BASE_DEFAULT; NULL = exe directory
};

struct MaestroPathHandler {
    HarpHandlerBase _base;

    // Joins bases[base] + relative into buf; relative uses forward slashes,
    // "." and ".." collapse. Returns the length written (excluding the NUL),
    // 0 if buf is too small or relative escapes the base.
    usize (*make)(MaestroPathHandler *h, MaestroPathBase base, char *buf, usize buf_size, const char *relative);
    usize (*makef)(MaestroPathHandler *h, MaestroPathBase base, char *buf, usize buf_size, const char *fmt, ...);

    // stat. Symlinks are followed; fails if nothing exists at path.
    HarpResult (*info)(MaestroPathHandler *h, const char *path, MaestroPathInfo *out_info);

    // get_actors-style two-call enumeration of the entries whose flags
    // intersect filter. "." and ".." are never reported; order is unspecified.
    HarpResult (*enumerate)(MaestroPathHandler *h, const char *path, MaestroPathEntryFlags filter, u32 *count, MaestroPathEntry *entries);

    // Resolved at init, absolute, NUL-terminated, no trailing separator.
    // CONFIG and SAVE are created on disk during init if missing.
    char bases[MAESTRO_PATH_BASE_COUNT][MAESTRO_PATH_MAX];
};


#ifdef __cplusplus
}
#endif

#endif /* MAESTRO_PATH_H */
