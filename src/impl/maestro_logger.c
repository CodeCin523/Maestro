#include "impl/maestro_logger.h"

#include <harp/utils/harp_helpers.h>
#include <harp/utils/harp_platform.h>

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>


#if HARP_PLATFORM_LINUX
    #include <unistd.h>
#elif HARP_PLATFORM_WINDOWS
    #include <windows.h>
#else
    #error Unsupported system.
#endif


/* ================================================================================ */
/*  CONSTANTS                                                                       */
/* ================================================================================ */

#define LOGGER_TIME_SIZE 19
#define LOGGER_LEVEL_SIZE 5
#define LOGGER_FIXED_SIZE (LOGGER_TIME_SIZE + 1 + LOGGER_LEVEL_SIZE + 1 + 3)

#define LOGGER_MIN_FREE_SPACE 128


/* ================================================================================ */
/*  LEVELS                                                                          */
/* ================================================================================ */

static const char LOGGER_LEVEL_STR[5][4] = {
    "MSG\0",
    "WRN\0",
    "DBG\0",
    "ERR\0",
    "FTL\0",
};


/* ================================================================================ */
/*  FORMATTERS                                                                      */
/* ================================================================================ */

static inline u64 logger_write_timestamp(
    MaestroLoggerHandlerImpl *impl,
    char *buf,
    u64 index
) {
    time_t now;
    time(&now);

    if(now != impl->last_time) {
        impl->last_time = now;

        struct tm *t = localtime(&now);

        u16 year = t->tm_year + 1900;
        u16 day  = t->tm_yday + 1;

        char *tmp = impl->time;

        tmp[0]  = '[';

        tmp[1]  = (year / 1000) % 10 + '0';
        tmp[2]  = (year / 100) % 10 + '0';
        tmp[3]  = (year / 10) % 10 + '0';
        tmp[4]  = (year) % 10 + '0';

        tmp[5]  = '-';

        tmp[6]  = (day / 100) % 10 + '0';
        tmp[7]  = (day / 10) % 10 + '0';
        tmp[8]  = (day) % 10 + '0';

        tmp[9]  = ' ';

        tmp[10] = (t->tm_hour / 10) % 10 + '0';
        tmp[11] = (t->tm_hour) % 10 + '0';

        tmp[12] = ':';

        tmp[13] = (t->tm_min / 10) % 10 + '0';
        tmp[14] = (t->tm_min) % 10 + '0';

        tmp[15] = ':';

        tmp[16] = (t->tm_sec / 10) % 10 + '0';
        tmp[17] = (t->tm_sec) % 10 + '0';

        tmp[18] = ']';
    }

    memcpy(&buf[index], impl->time, LOGGER_TIME_SIZE);
    return index + LOGGER_TIME_SIZE;
}

static inline u64 logger_write_level(
    char *buf,
    u64 index,
    MaestroLoggerLevel level
) {
    const char *lvl = LOGGER_LEVEL_STR[level];

    buf[index++] = ' ';
    buf[index++] = '[';
    buf[index++] = lvl[0];
    buf[index++] = lvl[1];
    buf[index++] = lvl[2];
    buf[index++] = ']';

    return index;
}

static inline u64 logger_write_prefix(
    MaestroLoggerHandlerImpl *impl,
    char *buf,
    u64 index,
    MaestroLoggerLevel level,
    const HarpName name
) {
    index = logger_write_timestamp(impl, buf, index);
    index = logger_write_level(buf, index, level);

    buf[index++] = ' ';

    if(name) {
        usize len = strlen(name);

        buf[index++] = '[';
        memcpy(&buf[index], name, len);
        index += len;
        buf[index++] = ']';
    }

    buf[index++] = ' ';
    buf[index++] = '-';
    buf[index++] = ' ';

    return index;
}


/* ================================================================================ */
/*  LOG FUNCTIONS                                                                   */
/* ================================================================================ */

void logger_fallback_log(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *msg) {
    MaestroLoggerHandlerImpl *impl = HARP_HANDLER_AS(MaestroLoggerHandlerImpl, h);
    if(!impl)
        return;
    const char *lvl = LOGGER_LEVEL_STR[level];

    char ts[LOGGER_TIME_SIZE + 1] = {0};
    logger_write_timestamp(impl, ts, 0);

    if(name)
        printf("%s [%s] [%s] - %s\n", ts, lvl, name, msg);
    else
        printf("%s [%s] - %s\n", ts, lvl, msg);
}
void logger_fallback_logf(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *fmt, ...) {
    MaestroLoggerHandlerImpl *impl = HARP_HANDLER_AS(MaestroLoggerHandlerImpl, h);
    if(!impl)
        return;
    const char *lvl = LOGGER_LEVEL_STR[level];

    char ts[LOGGER_TIME_SIZE + 1] = {0};
    logger_write_timestamp(impl, ts, 0);

    printf("%s [%s]", ts, lvl);

    if(name)
        printf(" [%s]", name);

    printf(" - ");

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
}


void logger_log(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *msg) {
    MaestroLoggerHandlerImpl *impl = HARP_HANDLER_AS(MaestroLoggerHandlerImpl, h);
    if(!HARP_HANDLER_IS_VALID(impl) || !msg)
        return;

    char *buf = impl->p_buf;
    u64 i = impl->buf_index;

    if(impl->buf_size - i < LOGGER_FIXED_SIZE + strlen(msg) + LOGGER_MIN_FREE_SPACE) {
        logger_flush(h);
        i = 0;
    }

    i = logger_write_prefix(impl, buf, i, level, name);

    usize len = strlen(msg);
    usize cap = impl->buf_size - i;

    if(len >= cap)
        len = cap - 1;

    memcpy(&buf[i], msg, len);
    i += len;

    buf[i++] = '\n';

    impl->buf_index = i;

    if(level >= MAESTRO_LOGGER_LEVEL_ERROR)
        logger_flush(h);
}
void logger_logf(MaestroLoggerHandler *h, const MaestroLoggerLevel level, const HarpName name, const char *fmt, ...) {
    MaestroLoggerHandlerImpl *impl = HARP_HANDLER_AS(MaestroLoggerHandlerImpl, h);
    if(!HARP_HANDLER_IS_VALID(impl) || !fmt)
        return;

    char *buf = impl->p_buf;
    u64 i = impl->buf_index;

    if(impl->buf_size - i < LOGGER_FIXED_SIZE + LOGGER_MIN_FREE_SPACE) {
        logger_flush(h);
        i = 0;
    }

    i = logger_write_prefix(impl, buf, i, level, name);

    usize cap = impl->buf_size - i;

    va_list args;
    va_start(args, fmt);

    int written = vsnprintf(&buf[i], cap, fmt, args);

    va_end(args);

    if(written > 0) {
        if((usize)written >= cap)
            i = impl->buf_size - 1;
        else
            i += (usize)written;
    }

    buf[i++] = '\n';

    impl->buf_index = i;

    if(level >= MAESTRO_LOGGER_LEVEL_ERROR)
        logger_flush(h);
}

void logger_flush(MaestroLoggerHandler *h) {
    MaestroLoggerHandlerImpl *impl = HARP_HANDLER_AS(MaestroLoggerHandlerImpl, h);
    if(!HARP_HANDLER_IS_VALID(impl))
        return;

#if HARP_PLATFORM_LINUX
    write(STDOUT_FILENO, impl->p_buf, impl->buf_index);
#elif HARP_PLATFORM_WINDOWS
    DWORD written;
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE),
        impl->p_buf,
        (DWORD)impl->buf_index,
        &written,
        NULL
    );
#endif

    impl->buf_index = 0;
}


/* ================================================================================ */
/*  HARP LOGGER                                                                     */
/* ================================================================================ */

// harp messages are written as harp gives them, without the maestro prefix
void logger_harp_log(void *user, HarpLogLevel level, const char *msg) {
    MaestroLoggerHandler *h = (MaestroLoggerHandler *)user;
    MaestroLoggerHandlerImpl *impl = HARP_HANDLER_AS(MaestroLoggerHandlerImpl, h);

    // harp logs before init_logger ran and after term_logger; no buffer then
    if(!HARP_HANDLER_IS_VALID(impl)) {
        printf("%s\n", msg);
        return;
    }

    char *buf = impl->p_buf;
    u64 i = impl->buf_index;

    usize len = strlen(msg);

    if(impl->buf_size - i < len + LOGGER_MIN_FREE_SPACE) {
        logger_flush(h);
        i = 0;
    }

    usize cap = impl->buf_size - i;

    if(len >= cap)
        len = cap - 1;

    memcpy(&buf[i], msg, len);
    i += len;

    buf[i++] = '\n';

    impl->buf_index = i;

    if(level >= HARP_LOG_LEVEL_ERROR)
        logger_flush(h);
}
void logger_harp_flush(void *user) {
    logger_flush((MaestroLoggerHandler *)user);
}


/* ================================================================================ */
/*  LOGGER HANDLER                                                                  */
/* ================================================================================ */

HarpResult init_logger(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator) {
    // the arguments are guaranteed to be valid by harp.

    MaestroLoggerCreator logger_creator = {
        .buffer_size = 8192
    };
    if(!(creator->flags & HARP_CREATOR_FLAG_DEFAULT_CREATOR)) {
        // harp do not guaranteed the size of the creator if we are in default creator mode
        logger_creator = *(MaestroLoggerCreator *)creator;
    }

    MaestroLoggerHandlerImpl *handler = (MaestroLoggerHandlerImpl *)base;
    
    void *tmp = malloc(logger_creator.buffer_size);
    if(tmp == NULL)
        return HARP_RESULT_OUT_OF_MEMORY;

    handler->p_buf = tmp;
    handler->buf_size = logger_creator.buffer_size;
    handler->buf_index = 0;
    handler->last_time = 0;

    core_handler->handler_set_serving(core_handler, base, 0);

    handler->pub.log = logger_log;
    handler->pub.logf = logger_logf;

    core_handler->handler_set_serving(core_handler, base, 1);

    return HARP_RESULT_OK;
}
HarpResult term_logger(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    // harp guarenty we are in the correct state to clear, but it do not guarenty it was correctly clean if it fail init
    MaestroLoggerHandlerImpl *handler = (MaestroLoggerHandlerImpl *)base;

    core_handler->handler_set_serving(core_handler, base, 0);

    handler->pub.log = logger_fallback_log;
    handler->pub.logf = logger_fallback_logf;

    core_handler->handler_set_serving(core_handler, base, 1);

    if(handler->p_buf != NULL) {
        logger_flush((MaestroLoggerHandler *)base);
        free(handler->p_buf);
    }

    handler->p_buf = NULL;
    handler->buf_size = 0;
    handler->buf_index = 0;
    handler->last_time = 0;
    
    return HARP_RESULT_OK; // a cleanup should never fail.
}
HarpResult patch_logger(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);
    MaestroLoggerHandlerImpl *handler = (MaestroLoggerHandlerImpl *)base;

    // swap-time registration rewired the fallbacks; restore the real
    // implementation when the handler is initialized
    if(HARP_STATUS_IS_VALID(base->status)) {
        handler->pub.log  = logger_log;
        handler->pub.logf = logger_logf;
    } else {
        handler->pub.log  = logger_fallback_log;
        handler->pub.logf = logger_fallback_logf;
    }
    handler->pub.flush = logger_flush;

    return HARP_RESULT_OK;
}