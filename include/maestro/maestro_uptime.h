#ifndef MAESTRO_UPTIME_H
#define MAESTRO_UPTIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <harp/harp.h>
#include <harp/utils/harp_version.h>


/* ================================================================================ */
/*  TYPES                                                                           */
/* ================================================================================ */

typedef struct MaestroUptimeNs {
    u64 ns;
} MaestroUptimeNs;
typedef struct MaestroDeltaNs {
    u64 ns;
} MaestroDeltaNs;


#define MAESTRO_UPTIME_NS(raw) ((MaestroUptimeNs){ .ns = (raw) })
#define MAESTRO_DELTA_NS(raw)  ((MaestroDeltaNs){ .ns = (raw) })

#define MAESTRO_DELTA_SINCE(prev, now) \
    MAESTRO_DELTA_NS((now).ns > (prev).ns ? (now).ns - (prev).ns : 0)

#define MAESTRO_DELTA_CLAMP(dt, max_ns) \
    MAESTRO_DELTA_NS((dt).ns > (max_ns) ? (max_ns) : (dt).ns)

#define MAESTRO_DELTA_SUB_SATURATING(remaining_ns, dt) \
    ((remaining_ns) > (dt).ns ? (remaining_ns) - (dt).ns : 0)

#define MAESTRO_DELTA_TO_SEC_F64(dt) ((f64)(dt).ns * 1e-9)
#define MAESTRO_DELTA_TO_SEC_F32(dt) ((f32)MAESTRO_DELTA_TO_SEC_F64(dt))


/* ================================================================================ */
/*  HANDLER                                                                         */
/* ================================================================================ */

#define MAESTRO_UPTIME_HANDLER_NAME "MaestroUptimeHandler"
#define MAESTRO_UPTIME_HANDLER_VERSION HARP_MAKE_VERSION(1,0,0)

typedef struct MaestroUptimeHandler MaestroUptimeHandler;

struct MaestroUptimeHandler {
    HarpHandlerBase _base;

    /* Absolute, monotonic reading since this handler's own init. Two
       readings are only meaningful compared against each other (see
       MAESTRO_DELTA_SINCE) -- never assume anything about what the zero
       point corresponds to. */
    HarpResult (*get_uptime_ns)(const MaestroUptimeHandler *h, MaestroUptimeNs *out_time);
};


#ifdef __cplusplus
}
#endif

#endif /* MAESTRO_UPTIME_H */
