#include <maestro/maestro.h>
#include "impl/maestro_logger.h"

#include <harp/harp.h>

#include <harp/utils/harp_api.h>
#include <harp/utils/harp_version.h>

#include <stdalign.h>


HarpResult maestro_register(HarpCoreApi *core) {
    HarpResult res = HARP_RESULT_OK;
    HarpDependencyDesc dep_desc = {0};
    HarpHandlerDesc handler_desc = {0};
    HarpHandlerBase *handler_base = NULL;
    HarpApiDesc api_desc = {0};
    HarpApiBase *api_base = NULL;
    

    handler_desc.name = MAESTRO_LOGGER_HANDLER_NAME;
    handler_desc.version = MAESTRO_LOGGER_HANDLER_VERSION;
    handler_desc.instance_size = sizeof(MaestroLoggerHandler);
    handler_desc.instance_alignment = alignof(MaestroLoggerHandler);
    handler_desc.pfn_init = init_logger;
    handler_desc.pfn_term = term_logger;
    handler_desc.p_dependencies = NULL;
    handler_desc.dependency_count = 0;

    res = core->register_handler(core, &handler_desc);
    if(res != HARP_RESULT_OK)
        return res;
    
    api_desc.name = MAESTRO_LOGGER_API_NAME;
    api_desc.version = MAESTRO_LOGGER_API_VERSION;
    api_desc.instance_size = sizeof(MaestroLoggerApiImpl);
    api_desc.instance_alignment = alignof(MaestroLoggerApiImpl);

    res = core->register_api(core, &api_desc, &api_base); 
    if(res != HARP_RESULT_OK)
        return res;

        
    dep_desc.name = MAESTRO_LOGGER_HANDLER_NAME;
    dep_desc.min_version = 0;
    dep_desc.max_version = UINT32_MAX;
    res = core->get_handler(core, &dep_desc, &handler_base);
    if(res != HARP_RESULT_OK)
        return res;

    MaestroLoggerApiImpl *logger = HARP_API_AS(MaestroLoggerApiImpl, api_base);

    logger->logger_handler = (MaestroLoggerHandler *) handler_base;

    logger->logger_api.log_info = log_info;
    logger->logger_api.log_debug = log_debug;
    logger->logger_api.log_warning = log_warning;
    logger->logger_api.log_error = log_error;

    api_base->available = 1;


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
