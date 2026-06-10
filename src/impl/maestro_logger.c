#include "maestro_logger.h"

#include <harp/utils/harp_platform.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#if HARP_PLATFORM_LINUX
    #include <unistd.h>
#elif HARP_PLATFORM_WINDOWS
    #include <windows.h>
    #include <fileapi.h>
#else
    #error Unsupported system.
#endif


/* ================================================================================ */
/*  LOGGER API                                                                      */
/* ================================================================================ */

static inline size_t allowed_len(size_t len_msg, size_t len_required, size_t bufSize) {
    size_t allowed = bufSize - (len_required - len_msg);
    return len_msg > allowed? allowed : len_msg;
}

static inline uint64_t format_date(char *buf, uint64_t index, struct tm *local) {
    uint16_t    tYear   = local->tm_year + 1900,
                tDay    = local->tm_yday + 1;
    uint8_t     tSec    = local->tm_sec,
                tMin    = local->tm_min,
                tHour   = local->tm_hour;
        
    buf[index++] = '[';
    buf[index++] = (tYear / 1000) % 10 + '0';
    buf[index++] = (tYear / 100) % 10 + '0';
    buf[index++] = (tYear / 10) % 10 + '0';
    buf[index++] = (tYear) % 10 + '0';
    buf[index++] = '-';
    buf[index++] = (tDay / 100) % 10 + '0';
    buf[index++] = (tDay / 10) % 10 + '0';
    buf[index++] = (tDay) % 10 + '0';
    buf[index++] = ' ';
    buf[index++] = (tHour / 10) % 10 + '0';
    buf[index++] = (tHour) % 10 + '0';
    buf[index++] = ':';
    buf[index++] = (tMin / 10) % 10 + '0';
    buf[index++] = (tMin) % 10 + '0';
    buf[index++] = ':';
    buf[index++] = (tSec / 10) % 10 + '0';
    buf[index++] = (tSec) % 10 + '0';
    buf[index++] = ']';

    return index;
}
static inline uint64_t format_level(char *buf, uint64_t index, uint8_t level) {
    static const char LOG_LEVEL[][3] = {
        "MSG", "DBG", "WRN", "ERR"
    };

    const char *level_str = LOG_LEVEL[level % 4];

    buf[index++] = '[';
    buf[index++] = level_str[0];
    buf[index++] = level_str[1];
    buf[index++] = level_str[2];
    buf[index++] = ']';

    return index;
}


static inline void maestro_log_flush(MaestroLoggerHandler *handler) {
    if(handler == NULL || handler->p_buf == NULL)
        return;

#if HARP_PLATFORM_LINUX
    write(STDOUT_FILENO, handler->p_buf, handler->buf_index);
#elif HARP_PLATFORM_WINDOWS
    HANDLE std_out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    
    WriteConsoleA(
        std_out,
        handler->p_buf,
        (DWORD)handler->buf_index,
        &written,
        NULL
    );
#endif

    handler->buf_index = 0;
}
static inline void maestro_log(MaestroLoggerHandler *handler, const HarpName name, const char *msg, uint8_t level) {
    if(handler == NULL || handler->p_buf == NULL || msg == NULL)
        return;

    size_t len_msg = strlen(msg);

    size_t len_name = 0;
    if(name != NULL)
        len_name = strlen(name);

    size_t len_required =
        19 +             /* timestamp */
        6 +              /* level */
        len_msg + 3 +    /* " - " */
        1;               /* '\n' */

    if(name != NULL)
        len_required += len_name + 3; /* " [name]" */

     if(handler->buf_index + len_required > handler->buf_size)
        maestro_log_flush(handler);

    char *buf = handler->p_buf;
    uint64_t index = handler->buf_index;

    len_msg = allowed_len(len_msg, len_required, handler->buf_size - index);

    { // time
        time_t now;
        time(&now);
        struct tm *local = localtime(&now);

        index = format_date(buf, index, local);
    }
    { // level
        buf[index++] = ' ';
        index = format_level(buf, index, level);
    }
    if(name != NULL) { // name
        buf[index++] = ' ';
        buf[index++] = '[';

        memcpy(&buf[index], name, len_name);
        index += len_name;

        buf[index++] = ']';
    }
    { // message
        buf[index++] = ' ';
        buf[index++] = '-';
        buf[index++] = ' ';
        memcpy(&buf[index], msg, len_msg);
        index += len_msg;
        buf[index++] = '\n';
    }

    handler->buf_index = index;
}


void log_info(MaestroLoggerApi *api, const HarpName name, const char *msg) {
    if(api == NULL)
        return;

    MaestroLoggerApiImpl *api_impl = (MaestroLoggerApiImpl *) api;
    maestro_log(api_impl->logger_handler, name, msg, 0);
}
void log_debug(MaestroLoggerApi *api, const HarpName name, const char *msg) {
    if(api == NULL)
        return;

    MaestroLoggerApiImpl *api_impl = (MaestroLoggerApiImpl *) api;
    maestro_log(api_impl->logger_handler, name, msg, 1);
}
void log_warning(MaestroLoggerApi *api, const HarpName name, const char *msg) {
    if(api == NULL)
        return;

    MaestroLoggerApiImpl *api_impl = (MaestroLoggerApiImpl *) api;
    maestro_log(api_impl->logger_handler, name, msg, 2);
}
void log_error(MaestroLoggerApi *api, const HarpName name, const char *msg) {
    if(api == NULL)
        return;

    MaestroLoggerApiImpl *api_impl = (MaestroLoggerApiImpl *) api;
    maestro_log(api_impl->logger_handler, name, msg, 3);
    maestro_log_flush(api_impl->logger_handler);
}


/* ================================================================================ */
/*  LOGGER HANDLER                                                                  */
/* ================================================================================ */

HarpResult init_logger(HarpCoreApi *api, HarpHandlerBase *base, HarpCreatorBase *creator) {
    // the arguments are guaranteed to be valid by harp.

    MaestroLoggerCreator logger_creator = {
        .buffer_size = 8192
    };
    if(!(creator->flags & HARP_CREATOR_FLAG_DEFAULT_CREATOR)) {
        // harp do not guaranteed the size of the creator if we are in default creator mode
        logger_creator = *(MaestroLoggerCreator *)creator;
    }

    MaestroLoggerHandler *handler = (MaestroLoggerHandler *)base;
    
    void *tmp = malloc(logger_creator.buffer_size);
    if(tmp == NULL)
        return HARP_RESULT_OUT_OF_MEMORY;

    handler->p_buf = tmp;
    handler->buf_index = 0;
    handler->buf_size = logger_creator.buffer_size;

    return HARP_RESULT_OK;
}
HarpResult term_logger(HarpCoreApi *api, HarpHandlerBase *base) {
    // harp guarenty we are in the correct state to clear, but it do not guarenty it was correctly clean if it fail init
    MaestroLoggerHandler *handler = (MaestroLoggerHandler *)base;

    if(handler->p_buf != NULL)
        free(handler->p_buf);

    handler->p_buf = NULL;
    handler->buf_size = 0;
    handler->buf_index = 0;
    
    return HARP_RESULT_OK; // a cleanup should never fail.
}