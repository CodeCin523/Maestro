#ifndef IMPL_MAESTRO_UPTIME_H
#define IMPL_MAESTRO_UPTIME_H

#include <maestro/maestro_uptime.h>

#include <harp/utils/harp_platform.h>

#if HARP_PLATFORM_WINDOWS
#include <windows.h>
#elif HARP_PLATFORM_LINUX
#include <time.h>
#endif


typedef struct MaestroUptimeHandlerImpl {
    MaestroUptimeHandler pub;

#if HARP_PLATFORM_WINDOWS
    LARGE_INTEGER start_time;
    LARGE_INTEGER frequency;
#elif HARP_PLATFORM_LINUX
    struct timespec start_time;
#endif
} MaestroUptimeHandlerImpl;


HarpResult uptime_get_uptime_ns(const MaestroUptimeHandler *h, MaestroUptimeNs *out_time);

HarpResult init_uptime(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_uptime(HarpCoreHandler *core_handler, HarpHandlerBase *base);
HarpResult patch_uptime(HarpCoreHandler *core_handler, HarpHandlerBase *base);


#endif /* IMPL_MAESTRO_UPTIME_H */
