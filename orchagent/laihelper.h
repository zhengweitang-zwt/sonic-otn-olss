#pragma once

#include <string>

extern lai_linecard_api_t*              lai_linecard_api;
extern lai_port_api_t*                  lai_port_api;
extern lai_transceiver_api_t*           lai_transceiver_api;
extern lai_physicalchannel_api_t*       lai_physicalchannel_api;
extern lai_otn_api_t*                   lai_otn_api;
extern lai_och_api_t*                   lai_och_api;
extern lai_logicalchannel_api_t*        lai_logicalchannel_api;
extern lai_lldp_api_t*                  lai_lldp_api;
extern lai_assignment_api_t*            lai_assignment_api;
extern lai_ethernet_api_t*              lai_ethernet_api;
extern lai_interface_api_t*             lai_interface_api;
extern lai_oa_api_t*                    lai_oa_api;
extern lai_osc_api_t*                   lai_osc_api;
extern lai_aps_api_t*                   lai_aps_api;
extern lai_apsport_api_t*               lai_apsport_api;
extern lai_attenuator_api_t*            lai_attenuator_api;
extern lai_ocm_api_t*                   lai_ocm_api;
extern lai_otdr_api_t*                  lai_otdr_api;

void initLaiApi();
void initLaiRedis(const std::string &record_location, const std::string &record_filename);
