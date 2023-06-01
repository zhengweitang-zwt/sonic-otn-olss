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

#include "oscorch.h"
#include "flexcounterorch.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> osc_counter_ids_status;
extern std::unordered_set<std::string> osc_counter_ids_gauge;

vector<lai_attr_id_t> g_osc_cfg_attrs =
{
    LAI_OSC_ATTR_ID,
    LAI_OSC_ATTR_ENABLED,
    LAI_OSC_ATTR_RX_LOW_THRESHOLD,
    LAI_OSC_ATTR_RX_HIGH_THRESHOLD,
    LAI_OSC_ATTR_TX_LOW_THRESHOLD,
};

vector<string> g_osc_auxiliary_fields =
{
    "name",
    "location",
    "interface",
    "parent",
};

OscOrch::OscOrch(DBConnector *db, const vector<string> &table_names)
    : LaiObjectOrch(db, table_names, LAI_OBJECT_TYPE_OSC, g_osc_cfg_attrs, g_osc_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OSC_TABLE_NAME));
    m_countersTable = COUNTERS_OSC_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OSC_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OSC_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OSC_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OSC_REPLY);

    m_createFunc = lai_osc_api->create_osc;
    m_removeFunc = lai_osc_api->remove_osc;
    m_setFunc = lai_osc_api->set_osc_attribute;
    m_getFunc = lai_osc_api->get_osc_attribute;
}

void OscOrch::setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs)
{
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::OSC_STATUS, osc_counter_ids_status);
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::OSC_GAUGE, osc_counter_ids_gauge);
}

