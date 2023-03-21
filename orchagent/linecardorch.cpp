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

#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <lairedis.h>
#include "linecardorch.h"
#include "flexcounterorch.h"
#include "lai_serialize.h"
#include "notifier.h"
#include "notificationproducer.h"
#include "notifications.h"
#include "orchfsm.h"
#include "subscriberstatetable.h"
#include "laihelper.h"
#include "timestamp.h"
#include "converter.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> linecard_counter_ids_status;
extern std::unordered_set<std::string> linecard_counter_ids_gauge;
extern std::unordered_set<std::string> linecard_counter_ids_counter;
extern int gSlotId;
extern string lairedis_rec_filename;
extern bool gSyncMode;
extern lai_redis_communication_mode_t gRedisCommunicationMode;
extern FlexCounterOrch* gFlexCounterOrch;

vector<lai_attr_id_t> g_linecard_cfg_attrs =
{
    LAI_LINECARD_ATTR_LINECARD_TYPE,
    LAI_LINECARD_ATTR_BOARD_MODE,
    LAI_LINECARD_ATTR_RESET,
    LAI_LINECARD_ATTR_HOSTNAME,
    LAI_LINECARD_ATTR_BAUD_RATE,
    LAI_LINECARD_ATTR_COLLECT_LINECARD_LOG,
    LAI_LINECARD_ATTR_HOST_IP,
    LAI_LINECARD_ATTR_USER_NAME,
    LAI_LINECARD_ATTR_USER_PASSWORD,
    LAI_LINECARD_ATTR_UPGRADE_FILE_NAME,
    LAI_LINECARD_ATTR_UPGRADE_FILE_PATH,
    LAI_LINECARD_ATTR_UPGRADE_DOWNLOAD,
    LAI_LINECARD_ATTR_UPGRADE_AUTO,
    LAI_LINECARD_ATTR_UPGRADE_COMMIT,
    LAI_LINECARD_ATTR_UPGRADE_COMMIT_PAUSE,
    LAI_LINECARD_ATTR_UPGRADE_COMMIT_RESUME,
    LAI_LINECARD_ATTR_UPGRADE_ROLLBACK,
    LAI_LINECARD_ATTR_UPGRADE_REBOOT,
    LAI_LINECARD_ATTR_LED_MODE,
    LAI_LINECARD_ATTR_LED_FLASH_INTERVAL,
};

vector<lai_attr_id_t> g_linecard_state_attrs = 
{
    LAI_LINECARD_ATTR_UPGRADE_STATE,
};

LinecardOrch::LinecardOrch(DBConnector *db, std::vector<TableConnector>& connectors)
    : LaiObjectOrch(db, connectors, LAI_OBJECT_TYPE_LINECARD, g_linecard_cfg_attrs)
{
    SWSS_LOG_ENTER();

    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_LINECARD_TABLE_NAME));
    m_countersTable = COUNTERS_LINECARD_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_LINECARD_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, LINECARD_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, LINECARD_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, LINECARD_REPLY);

    std::string oper_status;
    std::string linecard_key = "LINECARD-1-" + std::to_string(gSlotId);
    if (m_stateTable->hget(linecard_key, "oper-status", oper_status))
    {   
        if (oper_status == "ACTIVE")
        {   
            SWSS_LOG_NOTICE("Orchagent go into ready state");
            OrchFSM::setState(ORCH_STATE_READY);
        }   
    }

    m_config_total_num = 0;
    m_config_total_num_inited = false;
    m_config_num = 0;

    m_setFunc = lai_linecard_api->set_linecard_attribute;
    m_getFunc = lai_linecard_api->get_linecard_attribute;

    for (auto i : g_linecard_state_attrs)
    {
        auto meta = lai_metadata_get_attr_metadata(LAI_OBJECT_TYPE_LINECARD, i);
        if (meta == NULL)
        {   
            SWSS_LOG_ERROR("invalid attr, object=LINECARD, attr=%d", i);
            continue;
        }
        if (meta->isreadonly == true)
        {
            m_readonlyAttrs[meta->attridkebabname] = i;
        }
    }

    // 1-second timer
    // Currently, we use it to check if linecard is active or not per second.
    auto interval = timespec { .tv_sec = 1, .tv_nsec = 0 };
    m_timer_1_sec = new SelectableTimer(interval);
    auto executor = new ExecutableTimer(m_timer_1_sec, this, "LINECARD_TIMER");
    Orch::addExecutor(executor);
    m_timer_1_sec->start();

    m_orch_state = ORCH_STATE_NOT_READY;
}

void LinecardOrch::initConfigTotalNum(int num)
{
    SWSS_LOG_ENTER();

    if (num < 0)
    {   
        SWSS_LOG_ERROR("Invalid config total number, num=%d", num);
        return;
    }   
    m_config_total_num = num;
    m_config_total_num_inited = true;

    SWSS_LOG_NOTICE("Config total number=%d", num);

    if (m_config_num >= m_config_total_num)
    {   
        stopPreConfigProc();
        onLinecardActive();
    }
}

void LinecardOrch::incConfigNum()
{
    SWSS_LOG_ENTER();

    m_config_num++;

    if (!m_config_total_num_inited)
    {   
        return;
    }   

    if (m_config_num >= m_config_total_num)
    {   
        stopPreConfigProc();
        onLinecardActive();
    }
}

void LinecardOrch::stopPreConfigProc()
{
    SWSS_LOG_ENTER();

    lai_status_t status;
    lai_attribute_t attr;

    SWSS_LOG_NOTICE("Pre-Config Finished.");

    attr.id = LAI_LINECARD_ATTR_STOP_PRE_CONFIGURATION;
    attr.value.booldata = true;
    status = lai_linecard_api->set_linecard_attribute(gLinecardId, &attr);
    if (status != LAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to notify Lai pre-config finish %d", status);
    }
}

void LinecardOrch::doAppLinecardTableTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();

    if (OrchFSM::getState() == ORCH_STATE_NOT_READY)
    {
        return;
    }

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        auto t = it->second;
        auto op = kfvOp(t);
        string key = kfvKey(t);

        if (op == SET_COMMAND)
        {
            string operation_id;

            map<string, string> create_attrs;
            map<string, string> set_attrs;

            for (auto i : kfvFieldsValues(t))
            {
                if (fvField(i) == "operation-id")
                {
                    operation_id = fvValue(i);
                }
                else if (fvField(i) == "object-count")
                {
                    initConfigTotalNum(stoi(fvValue(i)));
                }
                else if (fvField(i) == "board-mode")
                {
                    create_attrs[fvField(i)] = fvValue(i);
                }
                else if (m_createandsetAttrs.find(fvField(i)) != m_createandsetAttrs.end())
                {
                    set_attrs[fvField(i)] = fvValue(i);
                }
                if (m_mandatoryAttrs.find(fvField(i)) != m_mandatoryAttrs.end())
                {
                    create_attrs[fvField(i)] = fvValue(i);
                }
            }
            if (OrchFSM::getState() == ORCH_STATE_READY)
            {
                createLinecard(key, create_attrs);
            }
            if (OrchFSM::getState() == ORCH_STATE_WORK)
            {
                setLaiObjectAttrs(key, set_attrs, operation_id);
            }
            it = consumer.m_toSync.erase(it);
        }
        else
        {
            SWSS_LOG_WARN("Unsupported operation %s ", op.c_str());
            it = consumer.m_toSync.erase(it);
        }
    }
}

void LinecardOrch::doLinecardStateTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();

    if (OrchFSM::getState() != ORCH_STATE_NOT_READY)
    {   
        return;
    }

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        auto t = it->second;
        auto op = kfvOp(t);
        for (auto i : kfvFieldsValues(t))
        {
            if (fvField(i) == "oper-status")
            {
                if (strcmp(fvValue(i).c_str(), "ACTIVE") == 0)
                {
                    SWSS_LOG_NOTICE("Linecard is active.");
                    OrchFSM::setState(ORCH_STATE_READY);
                }
                else
                {
                    SWSS_LOG_NOTICE("Linecard is inactive.");
                }
            }
        }
        it = consumer.m_toSync.erase(it);
    }
}

void LinecardOrch::doTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();

    const string& table_name = consumer.getTableName();

    if (table_name == APP_LINECARD_TABLE_NAME)
    {
        doAppLinecardTableTask(consumer);
    }
    else if (table_name == STATE_LINECARD_TABLE_NAME)
    {
        doLinecardStateTask(consumer);
    }
}

void LinecardOrch::createLinecard(
    const string& key,
    const map<string, string>& create_attrs)
{
    SWSS_LOG_ENTER();

    string record_location = "/var/log/swss";
    lai_attribute_t attr;
    vector<lai_attribute_t> attrs;
    lai_status_t status;
    lai_linecard_type_t linecard_type;
    bool is_linecard_type_existed = false;
    bool is_board_mode_existed = false;
    lai_linecard_board_mode_t board_mode = LAI_LINECARD_BOARD_MODE_L1_400G_CA_100GE;
 
    initLaiRedis(record_location, lairedis_rec_filename);

    for (auto fv: create_attrs)
    {
        if (translateLaiObjectAttr(fv.first, fv.second, attr) == false)
        {
            SWSS_LOG_ERROR("Failed to translate linecard attr, %s", fv.first.c_str());
            continue;
        }
        if (attr.id == LAI_LINECARD_ATTR_LINECARD_TYPE)
        {
            is_linecard_type_existed = true;
            linecard_type = (lai_linecard_type_t)(attr.value.s32); 
        }
        else if (attr.id == LAI_LINECARD_ATTR_BOARD_MODE)
        {
            is_board_mode_existed = true;
            board_mode = (lai_linecard_board_mode_t)(attr.value.s32);
            continue;
        }
        attrs.push_back(attr);
    }
    if (is_linecard_type_existed)
    {
        gFlexCounterOrch->initCounterTable(linecard_type);
    }

    attr.id = LAI_LINECARD_ATTR_LINECARD_ALARM_NOTIFY;
    attr.value.ptr = (void*)onLinecardAlarmNotify;
    attrs.push_back(attr);

    attr.id = LAI_LINECARD_ATTR_LINECARD_STATE_CHANGE_NOTIFY;
    attr.value.ptr = (void*)onLinecardStateChange;
    attrs.push_back(attr);

    attr.id = LAI_LINECARD_ATTR_COLLECT_LINECARD_ALARM;
    attr.value.booldata = true;
    attrs.push_back(attr);

    if (gSyncMode)
    {   
        SWSS_LOG_WARN("sync mode is depreacated, use -z param");

        gRedisCommunicationMode = LAI_REDIS_COMMUNICATION_MODE_REDIS_SYNC;
    }   

    attr.id = LAI_REDIS_LINECARD_ATTR_REDIS_COMMUNICATION_MODE;
    attr.value.s32 = gRedisCommunicationMode;

    lai_linecard_api->set_linecard_attribute(gLinecardId, &attr);

    status = lai_linecard_api->create_linecard(&gLinecardId, (uint32_t)attrs.size(), attrs.data());
    if (status != LAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to create a linecard, rv:%d", status);
        exit(EXIT_FAILURE);
    }
    SWSS_LOG_NOTICE("Create a linecard, id:%" PRIu64, gLinecardId);

    m_key2oid[key] = gLinecardId;

    attr.id = LAI_LINECARD_ATTR_START_PRE_CONFIGURATION;
    attr.value.booldata = true;
    status = lai_linecard_api->set_linecard_attribute(gLinecardId, &attr);
    if (status != LAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to notify Lai start pre-config %d", status);
    }

    if (is_board_mode_existed)
    {
        setBoardMode(board_mode);
    }

    FieldValueTuple tuple(lai_serialize_object_id(gLinecardId), key);
    vector<FieldValueTuple> fields;
    fields.push_back(tuple);
    m_nameMapTable->set("", fields);

    setFlexCounter(gLinecardId);

    OrchFSM::setState(ORCH_STATE_WORK);
}

void LinecardOrch::setBoardMode(lai_linecard_board_mode_t mode)
{
    SWSS_LOG_ENTER();

    int wait_count = 0;
    lai_attribute_t attr;
    lai_status_t status;

    attr.id = LAI_LINECARD_ATTR_BOARD_MODE;
    status = lai_linecard_api->get_linecard_attribute(gLinecardId, 1, &attr);
    if (status == LAI_STATUS_SUCCESS && attr.value.s32 == mode)
    {
        SWSS_LOG_DEBUG("Linecard and maincard have a same board-mode, %d", mode);
        return;
    }

    SWSS_LOG_NOTICE("Begin to set board-mode %d", mode);

    attr.value.s32 = mode;
    status = lai_linecard_api->set_linecard_attribute(gLinecardId, &attr);
    if (status != LAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to set board-mode status=%d, mode=%d",
                       status, mode);
        return;
    }

    do
    {
        wait_count++;
        this_thread::sleep_for(chrono::milliseconds(1000));
        status = lai_linecard_api->get_linecard_attribute(gLinecardId, 1, &attr);
        if (status != LAI_STATUS_SUCCESS)
        {
            continue;
        }
        auto meta = lai_metadata_get_attr_metadata(LAI_OBJECT_TYPE_LINECARD, attr.id);
        SWSS_LOG_DEBUG("board-mode = %s", lai_serialize_attr_value(*meta, attr, false, true).c_str());
        if (attr.value.s32 == mode)
        {
            break;
        }
    } while (wait_count < 10 * 60); /* 10 minutes is enough for P230C to change its boardmode */

    SWSS_LOG_NOTICE("The end of setting board-mode");
}

void LinecardOrch::setFlexCounter(lai_object_id_t id)
{
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::LINECARD_GAUGE, linecard_counter_ids_gauge);
    gFlexCounterOrch->getCounterGroup()->setCounterIdList(id, CounterType::LINECARD_COUNTER, linecard_counter_ids_counter);
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::LINECARD_STATUS, linecard_counter_ids_status);
}

void LinecardOrch::doTask(SelectableTimer &timer)
{
    SWSS_LOG_ENTER();

    if (timer.getFd() == m_timer_1_sec->getFd())
    {
        OrchState current_state = OrchFSM::getState();
        if (m_orch_state != current_state && current_state == ORCH_STATE_PAUSE)
        {
            clearLinecardData(); 
        }
        m_orch_state = current_state;
    }
}

void LinecardOrch::clearLinecardData()
{
    SWSS_LOG_ENTER();

    vector<string> counter_tables =
    {
        COUNTERS_LINECARD_TABLE_NAME,
        COUNTERS_PORT_TABLE_NAME,
        COUNTERS_TRANSCEIVER_TABLE_NAME,
        COUNTERS_LOGICALCHANNEL_TABLE_NAME,
        COUNTERS_OTN_TABLE_NAME,
        COUNTERS_ETHERNET_TABLE_NAME,
        COUNTERS_PHYSICALCHANNEL_TABLE_NAME,
        COUNTERS_OCH_TABLE_NAME,
        COUNTERS_LLDP_TABLE_NAME,
        COUNTERS_ASSIGNMENT_TABLE_NAME,
        COUNTERS_INTERFACE_TABLE_NAME,
        COUNTERS_OA_TABLE_NAME,
        COUNTERS_OSC_TABLE_NAME,
        COUNTERS_APS_TABLE_NAME,
        COUNTERS_APSPORT_TABLE_NAME,
        COUNTERS_ATTENUATOR_TABLE_NAME,
    };

    for (auto &table : counter_tables)
    {
        string pattern = table + "*";
        auto keys = m_countersDb->keys(pattern);
        for (auto k : keys)
        {
            SWSS_LOG_NOTICE("clear counter-table %s", k.c_str());
            m_countersDb->del(k);
        }
    }

    vector<string> state_tables =
    {
        STATE_PORT_TABLE_NAME,
        STATE_TRANSCEIVER_TABLE_NAME,
        STATE_LOGICALCHANNEL_TABLE_NAME,
        STATE_OTN_TABLE_NAME,
        STATE_ETHERNET_TABLE_NAME,
        STATE_PHYSICALCHANNEL_TABLE_NAME,
        STATE_OCH_TABLE_NAME,
        STATE_LLDP_TABLE_NAME,
        STATE_ASSIGNMENT_TABLE_NAME,
        STATE_INTERFACE_TABLE_NAME,
        STATE_OA_TABLE_NAME,
        STATE_OSC_TABLE_NAME,
        STATE_APS_TABLE_NAME,
        STATE_APSPORT_TABLE_NAME,
        STATE_ATTENUATOR_TABLE_NAME,
    };

    for (auto &table : state_tables)
    {
        string pattern = table + "*";
        auto keys = m_stateDb->keys(pattern);
        for (auto k : keys)
        {
            SWSS_LOG_NOTICE("clear state-table %s", k.c_str());
            m_stateDb->del(k);
        }
    }

    string linecard_key = "LINECARD|LINECARD-1-" + to_string(gSlotId);
    auto hash = m_stateDb->hgetall(linecard_key);
    for (auto &kv: hash)
    {
        const string &field = kv.first;
        if (field == "oper-status" ||
            field == "power-admin-state" ||
            field == "slot-status" ||
            field == "empty" ||
            field == "part-no" ||
            field == "serial-no" ||
            field == "hardware-version" ||
            field == "mfg-date" ||
            field == "removable")
        {
            continue;
        }
        SWSS_LOG_NOTICE("clear state-table %s %s", linecard_key.c_str(), field.c_str());
        m_stateDb->hdel(linecard_key, field);
    } 
}

