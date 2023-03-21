/**
 * Copyright (c) 2023 Alibaba Group Holding Limited
 *
 *    Licensed under the Apache License, Version 2.0 (the "License"); you may
 *    not use this file except in compliance with the License. You may obtain
 *    a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 *    THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR
 *    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 *    LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 *    FOR A PARTICULAR PURPOSE, MERCHANTABILITY OR NON-INFRINGEMENT.
 *
 *    See the Apache Version 2.0 License for specific language governing
 *    permissions and limitations under the License.
 *
 */

#include <getopt.h>
#include <iostream>
#include <string>
#include "configsync.h"
#include <thread>
#include <chrono>

using namespace std;
using namespace swss;

void usage()
{
    cout << "Usage: configsyncd" << endl;
    cout << "       this program will exit if configDB does not contain that info" << endl;
}

int main(int argc, char **argv)
{
    Logger::linkToDbNative("configsyncd");
    SWSS_LOG_ENTER();

    int opt;

    while ((opt = getopt(argc, argv, "v:h")) != -1 )
    {
        switch (opt)
        {
        case 'h':
            usage();
            return 1;
        default: /* '?' */
            usage();
            return EXIT_FAILURE;
        }
    }

    ConfigSync *apsport_sync = new ConfigSync("apsportsync", CFG_APSPORT_TABLE_NAME, APP_APSPORT_TABLE_NAME);
    ConfigSync *aps_sync = new ConfigSync("apssync", CFG_APS_TABLE_NAME, APP_APS_TABLE_NAME);
    ConfigSync *assignment_sync = new ConfigSync("assignmentsync", CFG_ASSIGNMENT_TABLE_NAME, APP_ASSIGNMENT_TABLE_NAME);
    ConfigSync *attenuator_sync = new ConfigSync("attenuatorsync", CFG_ATTENUATOR_TABLE_NAME, APP_ATTENUATOR_TABLE_NAME);
    ConfigSync *ethernet_sync = new ConfigSync("ethernetsync", CFG_ETHERNET_TABLE_NAME, APP_ETHERNET_TABLE_NAME);
    ConfigSync *interface_sync = new ConfigSync("interfacesync", CFG_INTERFACE_TABLE_NAME, APP_INTERFACE_TABLE_NAME);
    ConfigSync *lldp_sync = new ConfigSync("lldpsync", CFG_LLDP_TABLE_NAME, APP_LLDP_TABLE_NAME);
    ConfigSync *logicalchannel_sync = new ConfigSync("logicalchannelsync", CFG_LOGICALCHANNEL_TABLE_NAME, APP_LOGICALCHANNEL_TABLE_NAME);
    ConfigSync *oa_sync = new ConfigSync("oasync", CFG_OA_TABLE_NAME, APP_OA_TABLE_NAME);
    ConfigSync *och_sync = new ConfigSync("ochsync", CFG_OCH_TABLE_NAME, APP_OCH_TABLE_NAME);
    ConfigSync *osc_sync = new ConfigSync("oscsync", CFG_OSC_TABLE_NAME, APP_OSC_TABLE_NAME);
    ConfigSync *otn_sync = new ConfigSync("otnsync", CFG_OTN_TABLE_NAME, APP_OTN_TABLE_NAME);
    ConfigSync *physicalchannel_sync = new ConfigSync("physicalchannelsync", CFG_PHYSICALCHANNEL_TABLE_NAME, APP_PHYSICALCHANNEL_TABLE_NAME);
    ConfigSync *port_sync = new ConfigSync("portsync", CFG_PORT_TABLE_NAME, APP_PORT_TABLE_NAME);
    ConfigSync *transceiver_sync = new ConfigSync("transceiversync", CFG_TRANSCEIVER_TABLE_NAME, APP_TRANSCEIVER_TABLE_NAME);

    vector<ConfigSync *>sync_list  {
        apsport_sync,
        aps_sync,
        assignment_sync,
        attenuator_sync,
        ethernet_sync,
        interface_sync,
        lldp_sync,
        logicalchannel_sync,
        oa_sync,
        och_sync,
        osc_sync,
        otn_sync,
        physicalchannel_sync,
        port_sync,
        transceiver_sync
    };

    try {
        Select s;

        vector<ConfigSync *> objects;

        for (ConfigSync * sync : sync_list) {
            if (sync->handleConfigFromConfigDB()) {
                objects.push_back(sync);
                s.addSelectable(sync->getSelectable());
            }
        }

        if (objects.size() == 0) {
            goto exit;
        }

        DBConnector appl_db("APPL_DB", 0);
        Table linecard_table(&appl_db, APP_LINECARD_TABLE_NAME);
        ProducerStateTable linecard_pst(&appl_db, APP_LINECARD_TABLE_NAME);
        vector<FieldValueTuple> attrs;
        attrs.push_back(FieldValueTuple("object-count", to_string(objects.size())));

        vector<string> linecard_key;
        linecard_table.getKeys(linecard_key);
        while (linecard_key.size() == 0)
        {   
            linecard_table.getKeys(linecard_key);
            SWSS_LOG_NOTICE("Waiting for Linecard...");
            this_thread::sleep_for(chrono::milliseconds(1000));
        }
        for (const auto& value : linecard_key)
        {
            SWSS_LOG_NOTICE("Set object-count, key=%s, count=%d", value.c_str(), static_cast<int>(objects.size()));
            linecard_pst.set(value, attrs);
        }
 
        while (true) {
            Selectable *temps;
            int ret;
            ret = s.select(&temps, DEFAULT_SELECT_TIMEOUT);

            if (ret == Select::ERROR) {
                cerr << "Error had been returned in select" << endl;
                continue;
            } else if (ret == Select::TIMEOUT) {
                continue;
            } else if (ret != Select::OBJECT) {
                SWSS_LOG_ERROR("Unknown return value from Select %d", ret);
                continue;
            }
            for (ConfigSync *o : objects) {
                o->doTask(temps);
            }
        }
    } catch (const std::exception& e) {
        cerr << "Exception \"" << e.what() << "\" was thrown in daemon" << endl;
    } catch (...) {   
        cerr << "Exception was thrown in daemon" << endl;
    }

exit:
    for (ConfigSync * sync : sync_list) {
        delete sync;
    }

    return 0;
}

