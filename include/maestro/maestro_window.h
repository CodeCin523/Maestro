#ifndef MAESTRO_WINDOW_H
#define MAESTRO_WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <harp/harp.h>
#include <harp/utils/harp_version.h>


/* ================================================================================ */
/*  TYPEDEF                                                                         */
/* ================================================================================ */

typedef struct MaestroWindowCreator MaestroWindowCreator;
typedef struct MaestroWindowHandler MaestroWindowHandler;


/* ================================================================================ */
/*  Handlers                                                                        */
/* ================================================================================ */

#define MAESTRO_WINDOW_HANDLER_NAME "MaestroWindowHandler"
#define MAESTRO_WINDOW_HANDLER_VERSION HARP_MAKE_VERSION(1,0,0)

struct MaestroWindowCreator {
    HarpCreatorBase _base;

    const char *title;

    uint32_t width;
    uint32_t height;
};

struct MaestroWindowHandler {
    HarpHandlerBase _base;

    void (*pump_messages)(MaestroWindowHandler *h);
};


#ifdef __cplusplus
}
#endif

#endif /* MAESTRO_WINDOW_H */