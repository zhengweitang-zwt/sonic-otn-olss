extern "C" {

#include "lai.h"
#include "laistatus.h"
}

#include <inttypes.h>
#include <string.h>
#include <fstream>
#include <map>
#include <logger.h>
#include <lairedis.h>
#include <set>
#include <tuple>
#include <vector>
#include <linux/limits.h>
#include <net/if.h>
#include "timestamp.h"
#include "lai_serialize.h"
#include "laihelper.h"
#include "orch.h"

using namespace std;
using namespace swss;

#define _STR(s) #s
#define STR(s) _STR(s)

#define CONTEXT_CFG_FILE "/usr/share/sonic/hwsku/context_config.json"
#define LAI_REDIS_SYNC_OPERATION_RESPONSE_TIMEOUT (480*1000)

// hwinfo = "INTERFACE_NAME/PHY ID", mii_ioctl_data->phy_id is a __u16
#define HWINFO_MAX_SIZE IFNAMSIZ + 1 + 5

/* Initialize all global api pointers */
lai_linecard_api_t*              lai_linecard_api;
lai_port_api_t*                  lai_port_api;
lai_transceiver_api_t*           lai_transceiver_api;
lai_physicalchannel_api_t*       lai_physicalchannel_api;
lai_otn_api_t*                   lai_otn_api;
lai_och_api_t*                   lai_och_api;
lai_logicalchannel_api_t*        lai_logicalchannel_api;
lai_lldp_api_t*                  lai_lldp_api;
lai_assignment_api_t*            lai_assignment_api;
lai_ethernet_api_t*              lai_ethernet_api;
lai_interface_api_t*             lai_interface_api;
lai_oa_api_t*                    lai_oa_api;
lai_osc_api_t*                   lai_osc_api;
lai_aps_api_t*                   lai_aps_api;
lai_apsport_api_t*               lai_apsport_api;
lai_attenuator_api_t*            lai_attenuator_api;

extern lai_object_id_t gLinecardId;
extern bool gLairedisRecord;
extern bool gSwssRecord;
extern ofstream gRecordOfs;
extern string gRecordFile;

map<string, string> gProfileMap;

const char *test_profile_get_value (
    _In_ lai_linecard_profile_id_t profile_id,
    _In_ const char *variable)
{
    SWSS_LOG_ENTER();

    auto it = gProfileMap.find(variable);

    if (it == gProfileMap.end())
        return NULL;
    return it->second.c_str();
}

int test_profile_get_next_value (
    _In_ lai_linecard_profile_id_t profile_id,
    _Out_ const char **variable,
    _Out_ const char **value)
{
    SWSS_LOG_ENTER();

    static auto it = gProfileMap.begin();

    if (value == NULL)
    {
        // Restarts enumeration
        it = gProfileMap.begin();
    }
    else if (it == gProfileMap.end())
    {
        return -1;
    }
    else
    {
        *variable = it->first.c_str();
        *value = it->second.c_str();
        it++;
    }

    if (it != gProfileMap.end())
        return 0;
    else
        return -1;
}

const lai_service_method_table_t test_services = {
    test_profile_get_value,
    test_profile_get_next_value
};

void initLaiApi()
{
    SWSS_LOG_ENTER();

    if (ifstream(CONTEXT_CFG_FILE))
    {
        SWSS_LOG_NOTICE("Context config file %s exists", CONTEXT_CFG_FILE);
        gProfileMap[LAI_REDIS_KEY_CONTEXT_CONFIG] = CONTEXT_CFG_FILE;
    }

    lai_api_initialize(0, (const lai_service_method_table_t *)&test_services);

    lai_api_query(LAI_API_LINECARD,               (void **)&lai_linecard_api);
    lai_api_query(LAI_API_PORT,                   (void **)&lai_port_api);
    lai_api_query(LAI_API_TRANSCEIVER,            (void **)&lai_transceiver_api);
    lai_api_query(LAI_API_PHYSICALCHANNEL,        (void **)&lai_physicalchannel_api);
    lai_api_query(LAI_API_OTN,                    (void **)&lai_otn_api);
    lai_api_query(LAI_API_OCH,                    (void **)&lai_och_api);
    lai_api_query(LAI_API_LOGICALCHANNEL,         (void **)&lai_logicalchannel_api);
    lai_api_query(LAI_API_LLDP,                   (void **)&lai_lldp_api);
    lai_api_query(LAI_API_ASSIGNMENT,             (void **)&lai_assignment_api);
    lai_api_query(LAI_API_ETHERNET,               (void **)&lai_ethernet_api);
    lai_api_query(LAI_API_INTERFACE,              (void **)&lai_interface_api);
    lai_api_query(LAI_API_OA,                     (void **)&lai_oa_api);
    lai_api_query(LAI_API_OSC,                    (void **)&lai_osc_api);
    lai_api_query(LAI_API_APS,                    (void **)&lai_aps_api);
    lai_api_query(LAI_API_APSPORT,                (void **)&lai_apsport_api);
    lai_api_query(LAI_API_ATTENUATOR,             (void **)&lai_attenuator_api);

    lai_log_set(LAI_API_LINECARD,                 LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_PORT,                     LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_TRANSCEIVER,              LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_PHYSICALCHANNEL,          LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_OTN,                      LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_OCH,                      LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_LOGICALCHANNEL,           LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_LLDP,                     LAI_LOG_LEVEL_CRITICAL);
    lai_log_set(LAI_API_ASSIGNMENT,               LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_ETHERNET,                 LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_INTERFACE,                LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_OA,                       LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_OSC,                      LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_APS,                      LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_APSPORT,                  LAI_LOG_LEVEL_WARN);
    lai_log_set(LAI_API_ATTENUATOR,               LAI_LOG_LEVEL_WARN);
}

void initLaiRedis(const string &record_location, const std::string &record_filename)
{
    /**
     * NOTE: Notice that all Redis attributes here are using LAI_NULL_OBJECT_ID
     * as the linecard ID, because those operations don't require actual linecard
     * to be performed, and they should be executed before creating linecard.
     */

    lai_attribute_t attr;
    lai_status_t status;

    /* set recording dir before enable recording */

    if (gLairedisRecord)
    {
        attr.id = LAI_REDIS_LINECARD_ATTR_RECORDING_OUTPUT_DIR;
        attr.value.s8list.count = (uint32_t)record_location.size();
        attr.value.s8list.list = (int8_t*)const_cast<char *>(record_location.c_str());

        status = lai_linecard_api->set_linecard_attribute(gLinecardId, &attr);
        if (status != LAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to set LAI Redis recording output folder to %s, rv:%d",
                record_location.c_str(), status);
            exit(EXIT_FAILURE);
        }

        attr.id = LAI_REDIS_LINECARD_ATTR_RECORDING_FILENAME;
        attr.value.s8list.count = (uint32_t)record_filename.size();
        attr.value.s8list.list = (int8_t*)const_cast<char *>(record_filename.c_str());

        status = lai_linecard_api->set_linecard_attribute(gLinecardId, &attr);
        if (status != LAI_STATUS_SUCCESS)
        {   
            SWSS_LOG_ERROR("Failed to set LAI Redis recording logfile to %s, rv:%d",
                record_filename.c_str(), status);
            exit(EXIT_FAILURE);
        } 

    }

    /* Disable/enable LAI Redis recording */

    attr.id = LAI_REDIS_LINECARD_ATTR_RECORD;
    attr.value.booldata = gLairedisRecord;

    status = lai_linecard_api->set_linecard_attribute(gLinecardId, &attr);
    if (status != LAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to %s LAI Redis recording, rv:%d",
            gLairedisRecord ? "enable" : "disable", status);
        exit(EXIT_FAILURE);
    }

    attr.id = LAI_REDIS_LINECARD_ATTR_USE_PIPELINE;
    attr.value.booldata = true;

    status = lai_linecard_api->set_linecard_attribute(gLinecardId, &attr);
    if (status != LAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to enable redis pipeline, rv:%d", status);
        exit(EXIT_FAILURE);
    }
    SWSS_LOG_NOTICE("Enable redis pipeline");
}

