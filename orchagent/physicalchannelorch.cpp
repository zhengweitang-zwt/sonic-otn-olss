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

#include "physicalchannelorch.h"
#include "flexcounterorch.h"
#include "lai_serialize.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> physicalch_counter_ids_counter;
extern std::unordered_set<std::string> physicalch_counter_ids_gauge;
extern std::unordered_set<std::string> physicalch_counter_ids_status;

vector<lai_attr_id_t> g_physicalchannel_cfg_attrs =
{
    LAI_PHYSICALCHANNEL_ATTR_PORT_TYPE,
    LAI_PHYSICALCHANNEL_ATTR_PORT_ID,
    LAI_PHYSICALCHANNEL_ATTR_LANE_ID,
};

PhysicalChannelOrch::PhysicalChannelOrch(DBConnector *db, std::vector<TableConnector>& connectors)
    : LaiObjectOrch(db, connectors, LAI_OBJECT_TYPE_PHYSICALCHANNEL, g_physicalchannel_cfg_attrs)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_TRANSCEIVER_TABLE_NAME));
    m_countersTable = COUNTERS_TRANSCEIVER_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_TRANSCEIVER_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, TRANSCEIVER_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, TRANSCEIVER_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, TRANSCEIVER_REPLY);

    m_createFunc = lai_physicalchannel_api->create_physicalchannel;
    m_removeFunc = lai_physicalchannel_api->remove_physicalchannel;
    m_setFunc = lai_physicalchannel_api->set_physicalchannel_attribute;
    m_getFunc = lai_physicalchannel_api->get_physicalchannel_attribute;
}

void PhysicalChannelOrch::setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs)
{
    gFlexCounterOrch->getCounterGroup()->setCounterIdList(id, CounterType::PHYSICALCH_COUNTER, physicalch_counter_ids_counter);
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::PHYSICALCH_GAUGE, physicalch_counter_ids_gauge);
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::PHYSICALCH_STATUS, physicalch_counter_ids_status);
}

void PhysicalChannelOrch::clearFlexCounter(lai_object_id_t id, string key)
{
    gFlexCounterOrch->getCounterGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getGaugeGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getStatusGroup()->clearCounterIdList(id);
}
