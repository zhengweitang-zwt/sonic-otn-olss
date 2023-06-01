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

#include "attenuatororch.h"
#include "flexcounterorch.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> attenuator_counter_ids_status;
extern std::unordered_set<std::string> attenuator_counter_ids_gauge;

vector<lai_attr_id_t> g_attenuator_cfg_attrs =
{
    LAI_ATTENUATOR_ATTR_ID,
    LAI_ATTENUATOR_ATTR_ATTENUATION_MODE,
    LAI_ATTENUATOR_ATTR_TARGET_OUTPUT_POWER,
    LAI_ATTENUATOR_ATTR_ATTENUATION,
    LAI_ATTENUATOR_ATTR_ENABLED,
};

vector<string> g_attenuator_auxiliary_fields =
{
    "name",
    "component",
};

AttenuatorOrch::AttenuatorOrch(DBConnector *db, const vector<string> &table_names)
    : LaiObjectOrch(db, table_names, LAI_OBJECT_TYPE_ATTENUATOR, g_attenuator_cfg_attrs, g_attenuator_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_ATTENUATOR_TABLE_NAME));
    m_countersTable = COUNTERS_ATTENUATOR_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_ATTENUATOR_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, ATTENUATOR_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, ATTENUATOR_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, ATTENUATOR_REPLY);

    m_createFunc = lai_attenuator_api->create_attenuator;
    m_removeFunc = lai_attenuator_api->remove_attenuator;
    m_setFunc = lai_attenuator_api->set_attenuator_attribute;
    m_getFunc = lai_attenuator_api->get_attenuator_attribute;
}

void AttenuatorOrch::setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs)
{
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::ATTENUATOR_STATUS, attenuator_counter_ids_status);
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::ATTENUATOR_GAUGE, attenuator_counter_ids_gauge);
}

