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

#include "apsportorch.h"
#include "flexcounterorch.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> apsport_counter_ids_status;
extern std::unordered_set<std::string> apsport_counter_ids_gauge;

vector<lai_attr_id_t> g_apsport_cfg_attrs =
{
    LAI_APSPORT_ATTR_ID,
    LAI_APSPORT_ATTR_PORT_TYPE,
    LAI_APSPORT_ATTR_ENABLED,
    LAI_APSPORT_ATTR_TARGET_ATTENUATION,
    LAI_APSPORT_ATTR_POWER_LOW_THRESHOLD,
};

ApsportOrch::ApsportOrch(DBConnector *db, const vector<string> &table_names)
    : LaiObjectOrch(db, table_names, LAI_OBJECT_TYPE_APSPORT, g_apsport_cfg_attrs)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_APSPORT_TABLE_NAME));
    m_countersTable = COUNTERS_APSPORT_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_APSPORT_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, APSPORT_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, APSPORT_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, APSPORT_REPLY);

    m_createFunc = lai_apsport_api->create_apsport;
    m_removeFunc = lai_apsport_api->remove_apsport;
    m_setFunc = lai_apsport_api->set_apsport_attribute;
    m_getFunc = lai_apsport_api->get_apsport_attribute;
}

void ApsportOrch::setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs)
{
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::APSPORT_STATUS, apsport_counter_ids_status);
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::APSPORT_GAUGE, apsport_counter_ids_gauge);
}

