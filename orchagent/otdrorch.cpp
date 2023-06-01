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

#include "otdrorch.h"
#include "flexcounterorch.h"
#include "orchfsm.h"
#include "notifications.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> otdr_counter_ids_status;

vector<lai_attr_id_t> g_otdr_cfg_attrs =
{
    LAI_OTDR_ATTR_ID,
    LAI_OTDR_ATTR_REFRACTIVE_INDEX,
    LAI_OTDR_ATTR_BACKSCATTER_INDEX,
    LAI_OTDR_ATTR_REFLECTION_THRESHOLD,
    LAI_OTDR_ATTR_SPLICE_LOSS_THRESHOLD,
    LAI_OTDR_ATTR_END_OF_FIBER_THRESHOLD,
    LAI_OTDR_ATTR_DISTANCE_RANGE,
    LAI_OTDR_ATTR_PULSE_WIDTH,
    LAI_OTDR_ATTR_AVERAGE_TIME,
    LAI_OTDR_ATTR_OUTPUT_FREQUENCY,
    LAI_OTDR_ATTR_SCAN,
};

vector<string> g_otdr_auxiliary_fields =
{
    "name",
    "parent-port",
    "parent",
    "enable",
    "start-time",
    "period",
};

OtdrOrch::OtdrOrch(DBConnector *db, const vector<string> &table_names)
    : LaiObjectOrch(db, table_names, LAI_OBJECT_TYPE_OTDR, g_otdr_cfg_attrs, g_otdr_auxiliary_fields)
{
    SWSS_LOG_ENTER();
 
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTDR_TABLE_NAME));
    m_countersTable = COUNTERS_OTDR_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTDR_NAME_MAP));
 
    m_notificationConsumer = new NotificationConsumer(db, OTDR_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTDR_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTDR_REPLY);
 
    m_createFunc = lai_otdr_api->create_otdr;
    m_removeFunc = lai_otdr_api->remove_otdr;
    m_setFunc = lai_otdr_api->set_otdr_attribute;
    m_getFunc = lai_otdr_api->get_otdr_attribute;
}

void OtdrOrch::setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs)
{
    SWSS_LOG_ENTER();
 
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::OTDR_STATUS, otdr_counter_ids_status);
}

void OtdrOrch::doTask(swss::NotificationConsumer &consumer)
{
    SWSS_LOG_ENTER();
 
    if (&consumer != m_notificationConsumer)
    {
        return;
    }
 
    std::string data;
    std::string op;
    std::vector<swss::FieldValueTuple> values;
 
    consumer.pop(op, data, values);
 
    std::string op_ret;
 
    if (OrchFSM::getState() != ORCH_STATE_WORK)
    {
        op_ret = "UNAVAILABLE";
        m_notificationProducer->send(op_ret, data, values);
 
        return;
    }
 
    if (m_key2oid.find(data) == m_key2oid.end())
    {
        op_ret = "FAILED";
        m_notificationProducer->send(op_ret, data, values);
 
        return;
    }
 
    bool success = false;
 
    lai_object_id_t oid = m_key2oid[data];

    lai_status_t status;
 
    if (op == "set")
    {
        for (unsigned i = 0; i < values.size(); i++)
        {
            std::string &value = fvValue(values[i]);
            std::string &field = fvField(values[i]);
 
            if (m_irrecoverableAttrs.find(field) == m_irrecoverableAttrs.end())
            {
                success = false;
                break;
            }
 
            if (m_createandsetAttrs.find(field) == m_createandsetAttrs.end())
            {
                success = false;
                break;
            }
 
            lai_attribute_t attr;
            attr.id = m_createandsetAttrs[field];
 
            if (translateLaiObjectAttr(field, value, attr) == false)
            {
                success = false;
                break;
            }

            try
            { 
                status = m_setFunc(oid, &attr);

                if (status == LAI_STATUS_SUCCESS)
                {
                    success = true;
                    op_ret = "SUCCESS";
                }
                else
                {
                    success = false;
                    break;
                }
            }
            catch (const runtime_error &e)
            {
                SWSS_LOG_WARN("OTDR set %s failed", field.c_str());
                success = false;
                break;
            }

        }
    }
 
    if (success)
    {
        m_notificationProducer->send(op_ret, data, values);
    }
    else
    {
        op_ret = "FAILED";
        m_notificationProducer->send(op_ret, data, values);
    }
 
    return;
}

void OtdrOrch::setSelfProcessAttrs(const string &key,
                                   vector<FieldValueTuple> &auxiliary_fv,
                                   string operation_id)
{
    for (auto fv: auxiliary_fv)
    {
        if (fvField(fv) != "name" &&
            fvField(fv) != "enable" &&
            fvField(fv) != "start-time" &&
            fvField(fv) != "period" &&
            fvField(fv) != "parent-port")
        {
            continue;
        }

        m_stateTable->hset(key, fvField(fv), fvValue(fv));

        string channel = fvField(fv) + "-" + operation_id;
        string error_msg = "Set " + key + " " + fvField(fv) + " to " + fvValue(fv);
        publishOperationResult(channel, 0, error_msg);
    }
}

