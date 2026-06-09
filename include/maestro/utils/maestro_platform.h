#ifndef UTILS_MAESTRO_OS_H
#define UTILS_MAESTRO_OS_H

#endif /* UTILS_MAESTRO_OS_H */


/* ================================================================================ */
/*  MACROS                                                                          */
/* ================================================================================ */

#if defined(_WIN32) || defined(_WIN64)
    #define MAESTRO_PLATFORM_WINDOWS 1
#elif defined(__linux__)
    #define MAESTRO_PLATFORM_LINUX 1
#else
    #error Unsupported platform
#endif


/* ================================================================================ */
/*  UTILS_UNDEF                                                                     */
/* ================================================================================ */

#ifdef MAESTRO_UTILS_UNDEF

#undef MAESTRO_PLATFORM_WINDOWS
#undef MAESTRO_PLATFORM_LINUX

#endif /* MAESTRO_UTILS_UNDEF */