#ifndef MAESTRO_LOGGER_API_H
#define MAESTRO_LOGGER_API_H

#include <maestro/maestro_logger.h>

#include <time.h>


typedef struct MaestroLoggerHandlerImpl {
    MaestroLoggerHandler logger_handler;
    time_t last_time;
    char time[19];
} MaestroLoggerHandlerImpl;

typedef struct MaestroLoggerApiImpl {
    MaestroLoggerApi logger_api;
    MaestroLoggerHandlerImpl *logger_handler;
} MaestroLoggerApiImpl;


void log_info(MaestroLoggerApi *api, const HarpName name, const char *msg);
void log_debug(MaestroLoggerApi *api, const HarpName name, const char *msg);
void log_warning(MaestroLoggerApi *api, const HarpName name, const char *msg);
void log_error(MaestroLoggerApi *api, const HarpName name, const char *msg);


HarpResult init_logger(HarpCoreApi *api, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_logger(HarpCoreApi *api, HarpHandlerBase *base);


#endif /* MAESTRO_LOGGER_API_H */