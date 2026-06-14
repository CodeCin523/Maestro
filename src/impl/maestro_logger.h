#ifndef MAESTRO_LOGGER_HANDLER_H
#define MAESTRO_LOGGER_HANDLER_H

#include <maestro/maestro_logger.h>

#include <time.h>


typedef struct MaestroLoggerHandlerImpl {
    MaestroLoggerHandler handler;
    
    char *p_buf;
    uint64_t buf_index;
    uint64_t buf_size;

    time_t last_time;
    char time[19];
} MaestroLoggerHandlerImpl;


void logger_fallback_log(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *msg);
void logger_fallback_logf(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *fmt, ...);

void logger_log(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *msg);
void logger_logf(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *fmt, ...);

void logger_flush(MaestroLoggerHandler *h);


HarpResult init_logger(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_logger(HarpCoreHandler *core_handler, HarpHandlerBase *base);


#endif /* MAESTRO_LOGGER_HANDLER_H */