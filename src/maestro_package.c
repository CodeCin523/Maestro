#include <maestro/maestro.h>
#include "impl/maestro_logger.h"

#include <harp/harp.h>

#include <harp/utils/harp_api.h>
#include <harp/utils/harp_version.h>

#include <stdalign.h>


HarpResult maestro_register(HarpCoreHandler *core) {
    HarpResult res = HARP_RESULT_OK;

    static HarpHandlerDesc handler_desc = {
        .name               = MAESTRO_LOGGER_HANDLER_NAME,
        .version            = MAESTRO_LOGGER_HANDLER_VERSION,
        .instance_size      = sizeof(MaestroLoggerHandlerImpl),
        .instance_alignment = alignof(MaestroLoggerHandlerImpl),
        .pfn_init           = init_logger,
        .pfn_term           = term_logger,
        .p_dependencies     = NULL,
        .dependency_count   = 0
    };

    res = core->register_handler(core, &handler_desc);
    if(res != HARP_RESULT_OK)
        return res;

    HarpDependencyDesc dep_desc = {
        .name        = MAESTRO_LOGGER_HANDLER_NAME,
        .min_version = 0,
        .max_version = UINT32_MAX
    };

    HarpHandlerBase *handler_base = NULL;

    res = core->get_handler(core, &dep_desc, &handler_base);
    if(res != HARP_RESULT_OK)
        return res;

    MaestroLoggerHandlerImpl *logger = (MaestroLoggerHandlerImpl *)handler_base;

    logger->handler.log_info    = log_info;
    logger->handler.log_debug   = log_debug;
    logger->handler.log_warning = log_warning;
    logger->handler.log_error   = log_error;

    handler_base->status |= HARP_STATUS_FLAG_AVAILABLE;

    return HARP_RESULT_OK;
}

HarpResult harp_package_query(HarpPackageDesc **out_desc) {
    static HarpPackageDesc package_desc = {
        .name = MAESTRO_PACKAGE_NAME,
        .version = MAESTRO_PACKAGE_VERSION,

        .pfn_register = maestro_register,

        .p_dependencies = NULL,
        .dependency_count = 0
    };

    if(out_desc == NULL)
        return HARP_RESULT_MISSING_OUTPUT;

    *out_desc = &package_desc;

    return HARP_RESULT_OK;
}
