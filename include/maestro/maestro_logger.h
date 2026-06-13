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

typedef struct MaestroLoggerCreator MaestroLoggerCreator;
typedef struct MaestroLoggerHandler MaestroLoggerHandler;


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

    void (*log_info)(MaestroLoggerHandler *h, const HarpName name, const char *msg);
    void (*log_debug)(MaestroLoggerHandler *h, const HarpName name, const char *msg);
    void (*log_warning)(MaestroLoggerHandler *h, const HarpName name, const char *msg);
    void (*log_error)(MaestroLoggerHandler *h, const HarpName name, const char *msg);
};


#ifdef __cplusplus
}
#endif

#endif /* MAESTRO_LOGGER_H */