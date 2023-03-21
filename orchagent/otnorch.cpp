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

#include "otnorch.h"
#include "flexcounterorch.h"

using namespace std;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> otn_counter_ids_counter;
extern std::unordered_set<std::string> otn_counter_ids_gauge;
extern std::unordered_set<std::string> otn_counter_ids_status;

vector<lai_attr_id_t> g_otn_cfg_attrs =
{
    LAI_OTN_ATTR_CHANNEL_ID,
    LAI_OTN_ATTR_TTI_MSG_EXPECTED,
    LAI_OTN_ATTR_TTI_MSG_TRANSMIT,
    LAI_OTN_ATTR_TTI_MSG_AUTO,
    LAI_OTN_ATTR_DELAY_MEASUREMENT_ENABLED,
    LAI_OTN_ATTR_DELAY_MEASUREMENT_MODE,
    LAI_OTN_ATTR_MAINTENANCE,
};

vector<string> g_otn_auxiliary_fields =
{
    "index",
};

OtnOrch::OtnOrch(DBConnector *db, std::vector<TableConnector>& connectors)
    : LaiObjectOrch(db, connectors, LAI_OBJECT_TYPE_OTN, g_otn_cfg_attrs, g_otn_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTN_TABLE_NAME));
    m_countersTable = COUNTERS_OTN_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTN_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OTN_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTN_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTN_REPLY);

    m_createFunc = lai_otn_api->create_otn;
    m_removeFunc = lai_otn_api->remove_otn;
    m_setFunc = lai_otn_api->set_otn_attribute;
    m_getFunc = lai_otn_api->get_otn_attribute;
}

void OtnOrch::setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs)
{            
    gFlexCounterOrch->getCounterGroup()->setCounterIdList(id, CounterType::OTN_COUNTER, otn_counter_ids_counter);
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::OTN_GAUGE, otn_counter_ids_gauge);
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::OTN_STATUS, otn_counter_ids_status);
}

void OtnOrch::clearFlexCounter(lai_object_id_t id, string key)
{
    gFlexCounterOrch->getCounterGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getGaugeGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getStatusGroup()->clearCounterIdList(id);
}

