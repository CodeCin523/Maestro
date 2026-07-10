#ifndef IMPL_MAESTRO_LOGGER_H
#define IMPL_MAESTRO_LOGGER_H

#include <maestro/maestro_logger.h>

#include <harp/harp_ext.h>

#include <time.h>


typedef struct MaestroLoggerHandlerImpl {
    MaestroLoggerHandler pub;
    
    char *p_buf;
    u64 buf_index;
    u64 buf_size;

    time_t last_time;
    char time[19];
} MaestroLoggerHandlerImpl;


void logger_fallback_log(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *msg);
void logger_fallback_logf(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *fmt, ...);

void logger_log(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *msg);
void logger_logf(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *fmt, ...);

void logger_flush(MaestroLoggerHandler *h);

void logger_harp_log(void *user, HarpLogLevel level, const char *msg);
void logger_harp_flush(void *user);


HarpResult init_logger(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_logger(HarpCoreHandler *core_handler, HarpHandlerBase *base);
HarpResult patch_logger(HarpCoreHandler *core_handler, HarpHandlerBase *base);


#endif /* IMPL_MAESTRO_LOGGER_H */