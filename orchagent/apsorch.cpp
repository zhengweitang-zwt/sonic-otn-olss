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

#include "apsorch.h"
#include "flexcounterorch.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> aps_counter_ids_status;

vector<lai_attr_id_t> g_aps_cfg_attrs =
{
    LAI_APS_ATTR_ID,
    LAI_APS_ATTR_TYPE,
    LAI_APS_ATTR_NAME,
    LAI_APS_ATTR_REVERTIVE,
    LAI_APS_ATTR_WAIT_TO_RESTORE_TIME,
    LAI_APS_ATTR_HOLD_OFF_TIME,
    LAI_APS_ATTR_PRIMARY_SWITCH_THRESHOLD,
    LAI_APS_ATTR_PRIMARY_SWITCH_HYSTERESIS,
    LAI_APS_ATTR_SECONDARY_SWITCH_THRESHOLD,
    LAI_APS_ATTR_RELATIVE_SWITCH_THRESHOLD,
    LAI_APS_ATTR_RELATIVE_SWITCH_THRESHOLD_OFFSET,
    LAI_APS_ATTR_FORCE_TO_PORT,
    LAI_APS_ATTR_ALARM_HYSTERESIS,
    LAI_APS_ATTR_COLLECT_SWITCH_INFO,
    LAI_APS_ATTR_ACTIVE_PATH,
};

vector<string> g_aps_auxiliary_fields =
{
    "location",
    "parent",
    "subcomponents",
};

ApsOrch::ApsOrch(DBConnector *db, const vector<string> &table_names)
    : LaiObjectOrch(db, table_names, LAI_OBJECT_TYPE_APS, g_aps_cfg_attrs, g_aps_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_APS_TABLE_NAME));
    m_countersTable = COUNTERS_APS_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_APS_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, APS_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, APS_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, APS_REPLY);

    m_createFunc = lai_aps_api->create_aps;
    m_removeFunc = lai_aps_api->remove_aps;
    m_setFunc = lai_aps_api->set_aps_attribute;
    m_getFunc = lai_aps_api->get_aps_attribute;
}

void ApsOrch::setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs)
{
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::APS_STATUS, aps_counter_ids_status);
}

