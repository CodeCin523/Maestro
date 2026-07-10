#ifndef IMPL_MAESTRO_PATH_H
#define IMPL_MAESTRO_PATH_H

#include <maestro/maestro_path.h>

#include <harp/utils/harp_platform.h>


usize path_make(MaestroPathHandler *h, MaestroPathBase base, char *buf, usize buf_size, const char *relative);
usize path_makef(MaestroPathHandler *h, MaestroPathBase base, char *buf, usize buf_size, const char *fmt, ...);

HarpResult path_info(MaestroPathHandler *h, const char *path, MaestroPathInfo *out_info);
HarpResult path_enumerate(MaestroPathHandler *h, const char *path, MaestroPathEntryFlags filter, u32 *count, MaestroPathEntry *entries);


HarpResult init_path(HarpCoreHandler *core_handler, HarpHandlerBase *base, HarpCreatorBase *creator);
HarpResult term_path(HarpCoreHandler *core_handler, HarpHandlerBase *base);
HarpResult patch_path(HarpCoreHandler *core_handler, HarpHandlerBase *base);


#endif /* IMPL_MAESTRO_PATH_H */
