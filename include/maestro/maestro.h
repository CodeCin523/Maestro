#ifndef MAESTRO_H
#define MAESTRO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <maestro/maestro_logger.h>

#include <harp/harp.h>

#undef HARP_UTILS_UNDEF
#include <harp/utils/harp_version.h>


#define MAESTRO_PACKAGE_NAME "Maestro"
#define MAESTRO_PACKAGE_VERSION HARP_MAKE_VERSION(1,0,0)


#define HARP_UTILS_UNDEF
#include <harp/utils/harp_version.h>
#undef HARP_UTILS_UNDEF

#ifdef __cplusplus
}
#endif

#endif
