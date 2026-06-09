#ifndef MAESTRO_LOGGER_H
#define MAESTRO_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <harp/harp.h>

#undef HARP_UTILS_UNDEF
#include <harp/utils/harp_version.h>


/* ================================================================================ */
/*  TYPEDEF                                                                         */
/* ================================================================================ */

typedef struct MaestroLoggerCreator MaestroLoggerCreator;
typedef struct MaestroLoggerHandler MaestroLoggerHandler;
typedef struct MaestroLoggerApi MaestroLoggerApi;


/* ================================================================================ */
/*  Handlers                                                                        */
/* ================================================================================ */

#define MAESTRO_LOGGER_HANDLER_NAME "MaestroLoggerHandler"
#define MAESTRO_LOGGER_HANDLER_VERSION HARP_MAKE_VERSION(1,0,0)

struct MaestroLoggerCreator {
    HarpCreatorBase _base;

    size_t buffer_size;
};

struct MaestroLoggerHandler {
    HarpHandlerBase _base;

    char *p_buf;
    uint64_t buf_index;
    uint64_t buf_size;
};


/* ================================================================================ */
/*  Application Programming Interface                                               */
/* ================================================================================ */

#define MAESTRO_LOGGER_API_NAME "MaestroLoggerApi"
#define MAESTRO_LOGGER_API_VERSION HARP_MAKE_VERSION(1,0,0)

struct MaestroLoggerApi {
    HarpApiBase _base;

    void (*log_info)(MaestroLoggerApi *api, const HarpName name, const char *msg);
    void (*log_debug)(MaestroLoggerApi *api, const HarpName name, const char *msg);
    void (*log_warning)(MaestroLoggerApi *api, const HarpName name, const char *msg);
    void (*log_error)(MaestroLoggerApi *api, const HarpName name, const char *msg);
};


#define HARP_UTILS_UNDEF
#include <harp/utils/harp_version.h>
#undef HARP_UTILS_UNDEF

#ifdef __cplusplus
}
#endif

#endif /* MAESTRO_LOGGER_H */