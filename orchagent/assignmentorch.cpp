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

#include "assignmentorch.h"
#include "flexcounterorch.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch* gFlexCounterOrch;
extern std::unordered_set<std::string> assignment_counter_ids_status;

vector<lai_attr_id_t> g_assignment_cfg_attrs =
{
    LAI_ASSIGNMENT_ATTR_CHANNEL_ID,
    LAI_ASSIGNMENT_ATTR_ID,
};

vector<string> g_assignment_auxiliary_fields =
{
    "assignment-type",
    "logical-channel",
    "index",
};

AssignmentOrch::AssignmentOrch(DBConnector* db, const vector<string>& table_names)
    : LaiObjectOrch(db, table_names, LAI_OBJECT_TYPE_ASSIGNMENT, g_assignment_cfg_attrs, g_assignment_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_ASSIGNMENT_TABLE_NAME));
    m_countersTable = COUNTERS_ASSIGNMENT_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_ASSIGNMENT_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, ASSIGNMENT_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, ASSIGNMENT_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, ASSIGNMENT_REPLY);

    m_createFunc = lai_assignment_api->create_assignment;
    m_removeFunc = lai_assignment_api->remove_assignment;
    m_setFunc = lai_assignment_api->set_assignment_attribute;
    m_getFunc = lai_assignment_api->get_assignment_attribute;
}

void AssignmentOrch::setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs)
{
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::ASSIGNMENT_STATUS, assignment_counter_ids_status);
}

