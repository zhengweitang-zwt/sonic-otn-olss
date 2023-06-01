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

#include "oaorch.h"
#include "flexcounterorch.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> oa_counter_ids_status;
extern std::unordered_set<std::string> oa_counter_ids_gauge;

vector<lai_attr_id_t> g_oa_cfg_attrs =
{
    LAI_OA_ATTR_ID,
    LAI_OA_ATTR_TARGET_GAIN,
    LAI_OA_ATTR_TARGET_GAIN_TILT,
    LAI_OA_ATTR_AMP_MODE,
    LAI_OA_ATTR_TARGET_OUTPUT_POWER,
    LAI_OA_ATTR_MAX_OUTPUT_POWER,
    LAI_OA_ATTR_ENABLED,
    LAI_OA_ATTR_FIBER_TYPE_PROFILE,
    LAI_OA_ATTR_WORKING_STATE,
    LAI_OA_ATTR_INPUT_LOS_THRESHOLD,
    LAI_OA_ATTR_INPUT_LOS_HYSTERESIS,
    LAI_OA_ATTR_OUTPUT_LOS_THRESHOLD,
    LAI_OA_ATTR_OUTPUT_LOS_HYSTERESIS,
    LAI_OA_ATTR_GAIN_LOW_THRESHOLD,
    LAI_OA_ATTR_GAIN_LOW_HYSTERESIS,
    LAI_OA_ATTR_INPUT_LOW_THRESHOLD,
    LAI_OA_ATTR_OUTPUT_LOW_THRESHOLD,
    LAI_OA_ATTR_LOS_ASE_DELAY,
    LAI_OA_ATTR_APR_NODE_ENABLE,
    LAI_OA_ATTR_APR_NODE_REFLECTION_THRESHOLD,
    LAI_OA_ATTR_APR_LINE_ENABLE,
    LAI_OA_ATTR_APR_LINE_VALID_LLDP,
};

vector<string> g_oa_auxiliary_fields =
{
    "name",
    "location",
    "component",
    "parent",
    "subcomponents",
};

OaOrch::OaOrch(DBConnector *db, const vector<string> &table_names)
    : LaiObjectOrch(db, table_names, LAI_OBJECT_TYPE_OA, g_oa_cfg_attrs, g_oa_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OA_TABLE_NAME));
    m_countersTable = COUNTERS_OA_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OA_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OA_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OA_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OA_REPLY);

    m_createFunc = lai_oa_api->create_oa;
    m_removeFunc = lai_oa_api->remove_oa;
    m_setFunc = lai_oa_api->set_oa_attribute;
    m_getFunc = lai_oa_api->get_oa_attribute;
}

void OaOrch::setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs)
{
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::OA_STATUS, oa_counter_ids_status);
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::OA_GAUGE, oa_counter_ids_gauge);
}

