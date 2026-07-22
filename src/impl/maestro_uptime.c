#include "impl/maestro_uptime.h"

#include <harp/utils/harp_helpers.h>


HarpResult uptime_get_uptime_ns(const MaestroUptimeHandler *h, MaestroUptimeNs *out_time) {
    if(h == NULL || out_time == NULL)
        return HARP_RESULT_INVALID_ARGUMENTS;

    const MaestroUptimeHandlerImpl *impl = (const MaestroUptimeHandlerImpl *)h;

#if HARP_PLATFORM_WINDOWS
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    out_time->ns = ((u64)(now.QuadPart - impl->start_time.QuadPart) * 1000000000ULL) / (u64)impl->frequency.QuadPart;
#elif HARP_PLATFORM_LINUX
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    u64 now_ns = (u64)now.tv_sec * 1000000000ULL + (u64)now.tv_nsec;
    u64 start_ns = (u64)impl->start_time.tv_sec * 1000000000ULL + (u64)impl->start_time.tv_nsec;
    out_time->ns = now_ns - start_ns;
#endif

    return HARP_RESULT_OK;
}


HarpResult init_uptime(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator) {
    HARP_UNUSED(core_handler);
    HARP_UNUSED(creator);

    MaestroUptimeHandlerImpl *impl = (MaestroUptimeHandlerImpl *)base;

#if HARP_PLATFORM_WINDOWS
    QueryPerformanceFrequency(&impl->frequency);
    QueryPerformanceCounter(&impl->start_time);
#elif HARP_PLATFORM_LINUX
    clock_gettime(CLOCK_MONOTONIC, &impl->start_time);
#endif

    return HARP_RESULT_OK;
}

HarpResult term_uptime(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);
    HARP_UNUSED(base);
    return HARP_RESULT_OK;
}

/* Nothing to migrate: start_time/frequency survive a hot-swap patch
   unchanged, same as every other handler in this package. */
HarpResult patch_uptime(HarpCoreHandler *core_handler, HarpHandlerBase *base) {
    HARP_UNUSED(core_handler);
    HARP_UNUSED(base);
    return HARP_RESULT_OK;
}
