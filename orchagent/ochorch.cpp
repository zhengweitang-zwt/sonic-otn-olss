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

#include "ochorch.h"
#include "flexcounterorch.h"

extern FlexCounterOrch* gFlexCounterOrch;

using namespace std;

extern FlexCounterOrch* gFlexCounterOrch;
extern std::unordered_set<std::string> opticalch_counter_ids_status;
extern std::unordered_set<std::string> opticalch_counter_ids_gauge;
extern std::unordered_set<std::string> opticalch_counter_ids_counter;

vector<lai_attr_id_t> g_och_cfg_attrs =
{
    LAI_OCH_ATTR_PORT_TYPE,
    LAI_OCH_ATTR_PORT_ID,
    LAI_OCH_ATTR_OPERATIONAL_MODE,
    LAI_OCH_ATTR_FREQUENCY,
    LAI_OCH_ATTR_TARGET_OUTPUT_POWER
};

vector<string> g_och_auxiliary_fields =
{
    "parent",
    "line-port",
};

OchOrch::OchOrch(DBConnector *db, std::vector<TableConnector>& connectors)
    : LaiObjectOrch(db, connectors, LAI_OBJECT_TYPE_OCH, g_och_cfg_attrs, g_och_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OCH_TABLE_NAME));
    m_countersTable = COUNTERS_OCH_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OCH_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OCH_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OCH_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OCH_REPLY);

    m_needToCache.insert("frequency");
    m_needToCache.insert("target-output-power");

    m_createFunc = lai_och_api->create_och;
    m_removeFunc = lai_och_api->remove_och;
    m_setFunc = lai_och_api->set_och_attribute;
    m_getFunc = lai_och_api->get_och_attribute;
}

void OchOrch::setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs)
{
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::OPTICALCH_STATUS, opticalch_counter_ids_status);
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::OPTICALCH_GAUGE, opticalch_counter_ids_gauge);
    gFlexCounterOrch->getCounterGroup()->setCounterIdList(id, CounterType::OPTICALCH_COUNTER, opticalch_counter_ids_counter);
}

void OchOrch::clearFlexCounter(lai_object_id_t id, string key)
{
    gFlexCounterOrch->getCounterGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getGaugeGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getStatusGroup()->clearCounterIdList(id);
}

