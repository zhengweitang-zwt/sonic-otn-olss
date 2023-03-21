/**
 * Copyright (c) 2023 Alibaba Group Holding Limited
 * Copyright (c) 2023 Accelink Technologies Co., Ltd.
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

#include <fstream>
#include <iostream>
#include <string>
#include <inttypes.h>
#include <sys/time.h>
#include "timestamp.h"
#include "laiobjectorch.h"
#include "lai_serialize.h"
#include "linecardorch.h"
#include "flexcounterorch.h"
#include "converter.h"
#include "subscriberstatetable.h"
#include "tokenize.h"
#include "logger.h"
#include "consumerstatetable.h"
#include "notificationproducer.h"
#include "orchfsm.h"
#include "notifications.h"

using namespace std;
using namespace swss;

extern LinecardOrch *gLinecardOrch;
extern FlexCounterOrch *gFlexCounterOrch;
extern lai_object_id_t gLinecardId;

void LaiObjectOrch::localDataInit(DBConnector* db,
                                  lai_object_type_t obj_type,
                                  const vector<lai_attr_id_t>& cfg_attrs)
{
    SWSS_LOG_ENTER();

    m_objectName = lai_metadata_get_object_type_name(obj_type);
    m_stateDb = shared_ptr<DBConnector>(new DBConnector("STATE_DB", 0));
    m_countersDb = shared_ptr<DBConnector>(new DBConnector("COUNTERS_DB", 0));

    for (auto i : cfg_attrs)
    {
        auto meta = lai_metadata_get_attr_metadata(m_objectType, i);
        if (meta == NULL)
        {
            SWSS_LOG_ERROR("invalid attr, object=%s, attr=%d", m_objectName.c_str(), i);
            continue;
        }
        if (meta->ismandatoryoncreate == true)
        {
            m_mandatoryAttrs[meta->attridkebabname] = i;
        }
        if (meta->iscreateonly == true)
        {
            m_createonlyAttrs[meta->attridkebabname] = i;
        }
        else if (meta->iscreateandset == true || meta->issetonly == true)
        {
            m_createandsetAttrs[meta->attridkebabname] = i;
        }
        else
        {
            SWSS_LOG_ERROR("attr cannot be set, object=%s, attr=%d", m_objectName.c_str(), i);
            continue;
        }
        if (meta->isrecoverable == false)
        {
            m_irrecoverableAttrs[meta->attridkebabname] = i;
        }
    }
}

LaiObjectOrch::LaiObjectOrch(DBConnector* db,
    const vector<string>& table_names,
    lai_object_type_t obj_type,
    const vector<lai_attr_id_t>& cfg_attrs)
    : Orch(db, table_names), m_objectType(obj_type)
{
    SWSS_LOG_ENTER();

    localDataInit(db, obj_type, cfg_attrs);
}

LaiObjectOrch::LaiObjectOrch(DBConnector* db,
    vector<TableConnector> &connectors,
    lai_object_type_t obj_type,
    const vector<lai_attr_id_t>& cfg_attrs)
    : Orch(connectors), m_objectType(obj_type)
{
    SWSS_LOG_ENTER();

    localDataInit(db, obj_type, cfg_attrs);

    // 5-seconds timer
    // Currently, when pluggable lai-object is not present,
    // we use this timer to clear its data in counter and state db.
    auto interval = timespec { .tv_sec = 5, .tv_nsec = 0 };
    m_timer_5_sec = new SelectableTimer(interval);
    auto executor = new ExecutableTimer(m_timer_5_sec, this, "LAIOBJECT_TIMER");
    Orch::addExecutor(executor);
    m_timer_5_sec->start();
}

LaiObjectOrch::LaiObjectOrch(DBConnector *db,
                             const vector<string> &table_names,
                             lai_object_type_t obj_type,
                             const vector<lai_attr_id_t> &cfg_attrs,
                             const vector<string> &auxiliary_fields)
    : LaiObjectOrch(db, table_names, obj_type, cfg_attrs)
{
    for (auto i : auxiliary_fields)
    {
        m_auxiliaryFields.insert(i);
    }    
}

LaiObjectOrch::LaiObjectOrch(DBConnector *db,
                             vector<TableConnector> &connectors,
                             lai_object_type_t obj_type,
                             const vector<lai_attr_id_t> &cfg_attrs,
                             const vector<string> &auxiliary_fields)
    : LaiObjectOrch(db, connectors, obj_type, cfg_attrs)
{
    for (auto i : auxiliary_fields)
    {
        m_auxiliaryFields.insert(i);
    }
}

void LaiObjectOrch::doTask(NotificationConsumer& consumer)
{
    SWSS_LOG_ENTER();

    std::string op; 
    std::string data;
    lai_status_t status;
    std::vector<swss::FieldValueTuple> values;
    lai_object_id_t oid = LAI_NULL_OBJECT_ID;

    if (&consumer != m_notificationConsumer)
    {
        return;
    }

    if (OrchFSM::getState() != ORCH_STATE_WORK)
    {
        SWSS_LOG_ERROR("Orch isn't in working status");
        goto error;
    }

    consumer.pop(op, data, values);

    if (m_key2oid.find(data) == m_key2oid.end())
    {
        SWSS_LOG_ERROR("Failed to get oid, key=%s|%s", m_objectName.c_str(), data.c_str());
        goto error;
    }

    oid = m_key2oid[data];

    if (op == "set")
    {
        for (unsigned i = 0; i < values.size(); i++)
        {
            string &value = fvValue(values[i]);
            string &field = fvField(values[i]);

            if (m_irrecoverableAttrs.find(field) == m_irrecoverableAttrs.end())
            {
                SWSS_LOG_ERROR("Cannot use redis-channel to set recoverable attr, %s",
                               field.c_str());
                goto error;
            }
 
            status = setLaiObjectAttr(oid, field, value);
            if (status != LAI_STATUS_SUCCESS)
            {
                SWSS_LOG_ERROR("Failed to set attr, field=%s, value=%s, status=%d",
                    field.c_str(), value.c_str(), status);
                goto error;
            }
        }
        data = "SUCCESS";
        m_notificationProducer->send(op, data, values);

        return; 
    }
    else if (op == "get")
    {
        for (unsigned i = 0; i < values.size(); i++)
        {   
            string &value = fvValue(values[i]);
            string &field = fvField(values[i]);

            status = getLaiObjectAttr(oid, field, value);
            if (status != LAI_STATUS_SUCCESS)
            {
                SWSS_LOG_ERROR("Failed to get attr, field=%s, status=%d",
                    field.c_str(), status);
                goto error;
            }
         }
         data = "SUCCESS";
         m_notificationProducer->send(op, data, values);

         return;
    }

error:
    data = "FAILED";
    m_notificationProducer->send(op, data, values);

    return;
}

bool LaiObjectOrch::createLaiObject(const string &key)
{
    SWSS_LOG_ENTER();

    lai_status_t status;
    lai_attribute_t attr;
    vector<lai_attribute_t> attrs;
    lai_object_id_t oid;

    map<string, string> &createonly_attrs = m_key2createonlyAttrs[key];
    for (auto fv: createonly_attrs)
    {
        if (translateLaiObjectAttr(fv.first, fv.second, attr) == false)
        {
            SWSS_LOG_ERROR("Failed to translate attr, %s|%s",
                           m_objectName.c_str(), fv.first.c_str());
            continue;
        }
        attrs.push_back(attr); 
    }

    if (m_objectType == LAI_OBJECT_TYPE_APS)
    {
        attr.id = LAI_APS_ATTR_SWITCH_INFO_NOTIFY;
        attr.value.ptr = (void*)onApsSwitchInfoNotify;
        attrs.push_back(attr);

        attr.id = LAI_APS_ATTR_COLLECT_SWITCH_INFO;
        attr.value.booldata = true;
        attrs.push_back(attr);
    }

    status = m_createFunc(&oid, gLinecardId, static_cast<uint32_t>(attrs.size()), attrs.data());
    if (status != LAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to create %s|%s, rv=%d", m_objectName.c_str(), key.c_str(), status);
        return false;
    }
    SWSS_LOG_NOTICE("Create %s|%s oid:%" PRIx64, m_objectName.c_str(), key.c_str(), oid);

    m_key2oid[key] = oid;

    if (!setLaiObjectAttrs(key, m_key2createandsetAttrs[key]))
    {
        SWSS_LOG_ERROR("Failed to set fields, %s", key.c_str());
    }

    if (!syncStateTable(oid, key))
    {
        SWSS_LOG_ERROR("Failed to get object info, %s", key.c_str());
    }
    FieldValueTuple tuple(lai_serialize_object_id(oid), key);
    vector<FieldValueTuple> fields;
    fields.push_back(tuple);
    m_nameMapTable->set("", fields);

    setFlexCounter(oid, attrs);

    SWSS_LOG_NOTICE("Initialized %s", key.c_str());

    return true;
}

bool LaiObjectOrch::removeLaiObject(const lai_object_id_t oid)
{
    SWSS_LOG_ENTER();

    lai_status_t status = m_removeFunc(oid);
    if (status != LAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to remove %s|%" PRIx64 ", rv=%d", m_objectName.c_str(), oid, status);
        return false;
    }

    SWSS_LOG_NOTICE("Remove %s %" PRIx64, m_objectName.c_str(), oid);

    return true;
}

void LaiObjectOrch::publishOperationResult(string channel, lai_status_t status_code, string message) 
{
    swss::NotificationProducer notifications(m_stateDb.get(), channel);
    std::vector<swss::FieldValueTuple> entry;
    auto sent_clients = notifications.send(to_string(status_code), message, entry);
    SWSS_LOG_NOTICE("publishresult %d, %s to %ld client on channel %s", 
            status_code, message.c_str(), sent_clients, channel.c_str());
}

bool LaiObjectOrch::setLaiObjectAttrs(const string& key, map<string, string>& field_values, string operation_id)
{
    SWSS_LOG_ENTER();

    bool rv = true;

    if (OrchFSM::getState() != ORCH_STATE_WORK)
    {
        SWSS_LOG_ERROR("Orch isn't in working status");
        return false;
    }

    if (m_key2oid.find(key) == m_key2oid.end())
    {
        SWSS_LOG_ERROR("Failed to get oid, key=%s|%s", m_objectName.c_str(), key.c_str());
        return false;
    }

    string error_msg;
    lai_status_t status = LAI_STATUS_SUCCESS;

    for (auto fv : field_values)
    {
        string channel = fv.first + "-" + operation_id;

        SWSS_LOG_NOTICE("set field=%s value=%s", fv.first.c_str(), fv.second.c_str());

        status = setLaiObjectAttr(m_key2oid[key], fv.first, fv.second);
        if (status != LAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to set %s|%s %s to %s, status=%d",
                m_objectName.c_str(),
                key.c_str(),
                fv.first.c_str(),
                fv.second.c_str(),
                status);

            rv = false;
            error_msg = "Failed to set " + key + " " + fv.first + " to " + fv.second;
        }
        else
        {
            SWSS_LOG_NOTICE("Set %s|%s %s to %s",
                m_objectName.c_str(),
                key.c_str(),
                fv.first.c_str(),
                fv.second.c_str());

            if (m_needToCache.find(fv.first) != m_needToCache.end())
            {
                vector<FieldValueTuple> fvs;
                fvs.push_back(fv);
                m_stateTable->set(key, fvs);
            }
            error_msg = "Set " + key + " " + fv.first + " to " + fv.second;
        }

        publishOperationResult(channel, status, error_msg);
    }

    return rv;
}

bool LaiObjectOrch::translateLaiObjectAttr(
    _In_ const string &field,
    _In_ const string &value,
    _Out_ lai_attribute_t &attr)
{
    if (m_createandsetAttrs.find(field) != m_createandsetAttrs.end())
    {
        attr.id = m_createandsetAttrs[field];
    }
    else if (m_createonlyAttrs.find(field) != m_createonlyAttrs.end())
    {
        attr.id = m_createonlyAttrs[field];
    }
    else
    {
        SWSS_LOG_ERROR("Unrecognized attr, %s|%s", m_objectName.c_str(), field.c_str());
        return false;
    }
    
    auto meta = lai_metadata_get_attr_metadata(m_objectType, attr.id);
    if (meta == NULL)
    {
        SWSS_LOG_THROW("Unable to get %s metadata, attr=%d", m_objectName.c_str(), attr.id);
    }

    try
    {
        lai_deserialize_attr_value(value, *meta, attr);
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Unrecongnized attr value, %s|%s|%s",
                       m_objectName.c_str(), field.c_str(), value.c_str());
        return false;
    }

    return true;
}

lai_status_t LaiObjectOrch::setLaiObjectAttr(
    lai_object_id_t oid,
    const string &field,
    const string &value)
{
    SWSS_LOG_ENTER();

    lai_status_t status;
    lai_attribute_t attr;

    if (m_createandsetAttrs.find(field) == m_createandsetAttrs.end())
    {
        SWSS_LOG_ERROR("Unsupported attr, %s|%s", m_objectName.c_str(), field.c_str());
        return LAI_STATUS_FAILURE;
    }

    attr.id = m_createandsetAttrs[field];

    if (translateLaiObjectAttr(field, value, attr) == false)
    {
        SWSS_LOG_ERROR("Failed to translate attr, %s|%s",
                       m_objectName.c_str(), field.c_str());
        return LAI_STATUS_FAILURE;
    }

    status = m_setFunc(oid, &attr);
    if (status != LAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to set %s attr, field=%s, value=%s, status=%d",
                       m_objectName.c_str(), field.c_str(), value.c_str(), status);
        return status;
    }

    SWSS_LOG_NOTICE("Set %s attr, pid:%" PRIx64 " field=%s, value=%s",
                     m_objectName.c_str(), oid, field.c_str(), value.c_str());

    return LAI_STATUS_SUCCESS;
}

lai_status_t LaiObjectOrch::getLaiObjectAttr(lai_object_id_t oid, const string &field, string &value)
{
    SWSS_LOG_ENTER();

    lai_status_t status;
    lai_attribute_t attr;
    if (m_readonlyAttrs.find(field) == m_readonlyAttrs.end())
    {
        SWSS_LOG_ERROR("Unsupported attr, %s|%s", m_objectName.c_str(), field.c_str());
        return LAI_STATUS_FAILURE;
    }
    attr.id = m_readonlyAttrs[field];

    status = m_getFunc(oid, 1, &attr);
    if (status != LAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to get %s attr, field=%s, status=%d",
                       m_objectName.c_str(), field.c_str(), status);
        return status;
    }
    auto meta = lai_metadata_get_attr_metadata(m_objectType, attr.id);
    if (meta == NULL)
    {   
        SWSS_LOG_ERROR("Unable to get %s metadata, attr=%d", m_objectName.c_str(), attr.id);
        return LAI_STATUS_FAILURE; 
    }

    try
    {
        value = lai_serialize_attr_value(*meta, attr, false, true);
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to serialize attr value, %s|%s|%s",
                       m_objectName.c_str(), field.c_str(), value.c_str());
        return LAI_STATUS_FAILURE;
    }
    SWSS_LOG_NOTICE("Get %s attr successed, pid:%" PRIx64 " field=%s, value=%s",
                    m_objectName.c_str(), oid, field.c_str(), value.c_str());

    return LAI_STATUS_SUCCESS;
}

bool LaiObjectOrch::syncStateTable(lai_object_id_t oid, const string &key)
{
    SWSS_LOG_ENTER();

    vector<FieldValueTuple> fvs;

    if (m_key2auxiliaryFvs.find(key) != m_key2auxiliaryFvs.end())
    {
        for (auto &fv : m_key2auxiliaryFvs[key])
        {
            fvs.push_back(fv);
        }
    }
    m_stateTable->set(key, fvs);

    return true;
}

void LaiObjectOrch::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    if (OrchFSM::getState() <= ORCH_STATE_READY)
    {
        return;
    }

    if (!gFlexCounterOrch->checkFlexCounterInit())
    {
        return;
    }

    if (consumer.getDbName() == "STATE_DB")
    {
        doStateTask(consumer);
        return;
    }

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        auto &t = it->second;

        string key = kfvKey(t);
        string op = kfvOp(t);

        SWSS_LOG_NOTICE("doTask: Table = %s, key = %s, op = %s", m_objectName.c_str(), key.c_str(), op.c_str());

        if (key == "ConfigDone")
        {
            if (m_config_state != CONFIG_MISSING)
            {
                it = consumer.m_toSync.erase(it);
                continue;
            }
            m_config_state = CONFIG_RECEIVED;
            for (auto i : kfvFieldsValues(t))
            {
                if (fvField(i) == "count")
                {
                    m_count = to_uint<uint32_t>(fvValue(i));
                }
            }
        }

        if (op == SET_COMMAND)
        {
            int index = -1;
            string operation_id = "";
            map<string, string> createonly_attrs;
            map<string, string> createandset_attrs;
            vector<FieldValueTuple> auxiliary_fv; 
            for (auto i : kfvFieldsValues(t))
            {
                if (fvField(i) == "index")
                {
                    index = (int)stoul(fvValue(i));
                }
                if (fvField(i) == "operation-id")
                {
                    operation_id = fvValue(i);
                }
                else if (m_createonlyAttrs.find(fvField(i)) != m_createonlyAttrs.end())
                {
                    createonly_attrs[fvField(i)] = fvValue(i);
                }
                else if (m_createandsetAttrs.find(fvField(i)) != m_createandsetAttrs.end())
                {
                    createandset_attrs[fvField(i)] = fvValue(i);
                }
                else if (m_auxiliaryFields.find(fvField(i)) != m_auxiliaryFields.end())
                {
                    auxiliary_fv.push_back(i);
                }
            }

            if (index != -1)
            {
                m_keys.insert(key);
                m_key2createandsetAttrs[key] = createandset_attrs;
                m_key2createonlyAttrs[key] = createonly_attrs;
                m_key2auxiliaryFvs[key] = auxiliary_fv;
            }
            if (m_config_state == CONFIG_RECEIVED || m_config_state == CONFIG_DONE)
            {
                for (auto it = m_key2oid.begin(); it != m_key2oid.end();)
                {
                    if (m_keys.find(it->first) == m_keys.end())
                    {
                        if (removeLaiObject(it->second) != LAI_STATUS_SUCCESS)
                        {
                            SWSS_LOG_ERROR("Failed to remove object");
                        }
                        it = m_key2oid.erase(it);
                    }
                    else
                    {
                        it++;
                    }
                }

                for (auto it = m_keys.begin(); it != m_keys.end(); it++)
                {
                    if (m_key2oid.find(*it) == m_key2oid.end())
                    {
                        if (!createLaiObject(*it))
                        {
                            SWSS_LOG_THROW("Failed to create object");
                        }
                    }
                }
                if (m_key2oid.size() == m_count)
                {
                    m_count = 0;
                    SWSS_LOG_NOTICE("Finish initialize %s", m_objectName.c_str()); 
                    gLinecardOrch->incConfigNum();
                }
                m_config_state = CONFIG_DONE;
            }
            if (m_config_state != CONFIG_DONE)
            {
                it++;
                continue;
            }
            if (key == "ConfigDone")
            {
                it = consumer.m_toSync.erase(it);
                continue;
            }
            if (setLaiObjectAttrs(key, createandset_attrs, operation_id) == false)
            {
                it = consumer.m_toSync.erase(it);  
                continue;
            }
        }
        else if (op == DEL_COMMAND)
        {
            SWSS_LOG_NOTICE("Deleting %s", key.c_str());
        }
        else
        {
            SWSS_LOG_ERROR("Unknown operation type %s", op.c_str());
        }
        it = consumer.m_toSync.erase(it);
    }
}

void LaiObjectOrch::clearCountersTable(string key)
{
    SWSS_LOG_ENTER();

    string pattern = m_countersTable + ":" + key + "*";

    SWSS_LOG_NOTICE("clear counter-table pattern=%s", pattern.c_str());
    auto keys = m_countersDb->keys(pattern);
    for (auto k : keys)
    {
        SWSS_LOG_NOTICE("DEL key=%s", k.c_str());
        m_countersDb->del(k);
    }
}

void LaiObjectOrch::clearStateTable(string key)
{
    SWSS_LOG_ENTER();

    string pattern = m_stateTable->getTableName() + "|" + key + "*";

    SWSS_LOG_NOTICE("clear state-table pattern=%s", pattern.c_str());
    auto keys = m_stateDb->keys(pattern);
    for (auto k : keys)
    {
        SWSS_LOG_NOTICE("DEL key=%s", k.c_str());
        m_stateDb->del(k);
    }
}

void LaiObjectOrch::doStateTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        auto &t = it->second;

        string key = kfvKey(t);
        string op = kfvOp(t);

        bool has_present_field = false;
        string present_value;

        SWSS_LOG_INFO("%s, key = %s, op = %s", m_objectName.c_str(),
                      key.c_str(), op.c_str());

        if (m_key2oid.find(key) == m_key2oid.end())
        {
            it = consumer.m_toSync.erase(it);
            continue;
        }

        for (auto i : kfvFieldsValues(t))
        {
            if (fvField(i) == "present")
            {
                has_present_field = true;
                present_value = fvValue(i);
                break;
            }
        }
        if (has_present_field == false)
        {
            it = consumer.m_toSync.erase(it);
            continue;
        }

        lai_object_id_t id = m_key2oid[key];

        string present;

        if (m_key2present.find(key) != m_key2present.end())
        {
            present = m_key2present[key];
        }

        if (present_value != present)
        {
            if (present_value == "PRESENT")
            {
                SWSS_LOG_NOTICE("setCounterIdList 0x%lx, key = %s", id, key.c_str());
                vector<lai_attribute_t> attrs;
                setFlexCounter(id, attrs);
            }
            else if (present_value == "NOT_PRESENT")
            {
                SWSS_LOG_NOTICE("clearCounterIdList 0x%lx, key = %s", id, key.c_str());
                clearFlexCounter(id, key);
                m_preClearKeys.insert(key);
            }

            doSubobjectStateTask(key, present_value);
            m_key2present[key] = present_value;
        }

        it = consumer.m_toSync.erase(it);
    }
}

void LaiObjectOrch::doTask(SelectableTimer &timer)
{
    SWSS_LOG_ENTER();

    if (timer.getFd() == m_timer_5_sec->getFd())
    {
        for (auto &key : m_readyToClearKeys)
        {
            clearCountersTable(key);
            clearStateTable(key);
        }
        m_readyToClearKeys.clear();

        for (auto &key : m_preClearKeys)
        {
            m_readyToClearKeys.insert(key);
        }
        m_preClearKeys.clear();
    }
}

