#ifndef MAESTRO_LOGGER_H
#define MAESTRO_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <harp/harp.h>
#include <harp/utils/harp_version.h>


/* ================================================================================ */
/*  TYPEDEF                                                                         */
/* ================================================================================ */

typedef uint8_t MaestroLoggerLevel;
enum {
    MAESTRO_LOGGER_LEVEL_INFO = 0,
    MAESTRO_LOGGER_LEVEL_WARN = 1,
    MAESTRO_LOGGER_LEVEL_DEBUG = 2,
    MAESTRO_LOGGER_LEVEL_ERROR = 3,
    MAESTRO_LOGGER_LEVEL_FATAL = 4,
};

typedef struct MaestroLoggerCreator MaestroLoggerCreator;
typedef struct MaestroLoggerHandler MaestroLoggerHandler;


/* ================================================================================ */
/*  Handlers                                                                        */
/* ================================================================================ */

#define MAESTRO_LOGGER_HANDLER_NAME "MaestroLoggerHandler"
#define MAESTRO_LOGGER_HANDLER_VERSION HARP_MAKE_VERSION(2,0,0)

struct MaestroLoggerCreator {
    HarpCreatorBase _base;

    size_t buffer_size;
};

struct MaestroLoggerHandler {
    HarpHandlerBase _base;

    void (*log)(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *msg);
    void (*logf)(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *fmt, ...);

    void (*flush)(MaestroLoggerHandler *h);
};


/* ================================================================================ */
/*  HELPER MACRO                                                                    */
/* ================================================================================ */

#define MAESTRO_LOGGER_INFO_ENABLED 1
#define MAESTRO_LOGGER_WARN_ENABLED 1
#define MAESTRO_LOGGER_DEBUG_ENABLED 1
#define MAESTRO_LOGGER_ERROR_ENABLED 1
#define MAESTRO_LOGGER_FATAL_ENABLED 1


#if MAESTRO_LOGGER_INFO_ENABLED
#define MAESTRO_LOG_INFO(h, name, msg) ((h)->log((h), MAESTRO_LOGGER_LEVEL_INFO, (name), (msg)))
#define MAESTRO_LOGF_INFO(h, name, fmt, ...) ((h)->logf((h), MAESTRO_LOGGER_LEVEL_INFO, (name), (fmt), ##__VA_ARGS__))
#else
#define MAESTRO_LOG_INFO(h, name, msg)
#define MAESTRO_LOGF_INFO(h, name, fmt, ...)
#endif

#if MAESTRO_LOGGER_WARN_ENABLED
#define MAESTRO_LOG_WARN(h, name, msg) ((h)->log((h), MAESTRO_LOGGER_LEVEL_WARN, (name), (msg)))
#define MAESTRO_LOGF_WARN(h, name, fmt, ...) ((h)->logf((h), MAESTRO_LOGGER_LEVEL_WARN, (name), (fmt), ##__VA_ARGS__))
#else
#define MAESTRO_LOG_WARN(h, name, msg)
#define MAESTRO_LOGF_WARN(h, name, fmt, ...)
#endif

#if MAESTRO_LOGGER_DEBUG_ENABLED
#define MAESTRO_LOG_DEBUG(h, name, msg) ((h)->log((h), MAESTRO_LOGGER_LEVEL_DEBUG, (name), (msg)))
#define MAESTRO_LOGF_DEBUG(h, name, fmt, ...) ((h)->logf((h), MAESTRO_LOGGER_LEVEL_DEBUG, (name), (fmt), ##__VA_ARGS__))
#else
#define MAESTRO_LOG_DEBUG(h, name, msg)
#define MAESTRO_LOGF_DEBUG(h, name, fmt, ...)
#endif

#if MAESTRO_LOGGER_ERROR_ENABLED
#define MAESTRO_LOG_ERROR(h, name, msg) ((h)->log((h), MAESTRO_LOGGER_LEVEL_ERROR, (name), (msg)))
#define MAESTRO_LOGF_ERROR(h, name, fmt, ...) ((h)->logf((h), MAESTRO_LOGGER_LEVEL_ERROR, (name), (fmt), ##__VA_ARGS__))
#else
#define MAESTRO_LOG_ERROR(h, name, msg)
#define MAESTRO_LOGF_ERROR(h, name, fmt, ...)
#endif

#if MAESTRO_LOGGER_FATAL_ENABLED
#define MAESTRO_LOG_FATAL(h, name, msg) ((h)->log((h), MAESTRO_LOGGER_LEVEL_FATAL, (name), (msg)))
#define MAESTRO_LOGF_FATAL(h, name, fmt, ...) ((h)->logf((h), MAESTRO_LOGGER_LEVEL_FATAL, (name), (fmt), ##__VA_ARGS__))
#else
#define MAESTRO_LOG_FATAL(h, name, msg)
#define MAESTRO_LOGF_FATAL(h, name, fmt, ...)
#endif


#ifdef __cplusplus
}
#endif

#endif /* MAESTRO_LOGGER_H */