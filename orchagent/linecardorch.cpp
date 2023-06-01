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
    bool is_board_mode_existed = false;
    string board_mode;
 
    initLaiRedis(record_location, lairedis_rec_filename);

    for (auto fv: create_attrs)
    {
        if (translateLaiObjectAttr(fv.first, fv.second, attr) == false)
        {
            SWSS_LOG_ERROR("Failed to translate linecard attr, %s", fv.first.c_str());
            continue;
        }
        if (attr.id == LAI_LINECARD_ATTR_BOARD_MODE)
        {
            is_board_mode_existed = true;
            board_mode = (attr.value.chardata);
            continue;
        }
        attrs.push_back(attr);
    }

    gFlexCounterOrch->initCounterTable();

    attr.id = LAI_LINECARD_ATTR_LINECARD_ALARM_NOTIFY;
    attr.value.ptr = (void*)onLinecardAlarmNotify;
    attrs.push_back(attr);

    attr.id = LAI_LINECARD_ATTR_LINECARD_STATE_CHANGE_NOTIFY;
    attr.value.ptr = (void*)onLinecardStateChange;
    attrs.push_back(attr);

    attr.id = LAI_LINECARD_ATTR_COLLECT_LINECARD_ALARM;
    attr.value.booldata = true;
    attrs.push_back(attr);

    attr.id = LAI_LINECARD_ATTR_LINECARD_OCM_SPECTRUM_POWER_NOTIFY;
    attr.value.ptr = (void*)onOcmSpectrumPowerNotify;
    attrs.push_back(attr);

    attr.id = LAI_LINECARD_ATTR_LINECARD_OTDR_RESULT_NOTIFY;
    attr.value.ptr = (void*)onOtdrResultNotify;
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

    m_vid2NameTable->set("", fields);

    setFlexCounter(gLinecardId);

    OrchFSM::setState(ORCH_STATE_WORK);
}

void LinecardOrch::setBoardMode(std::string mode)
{
    SWSS_LOG_ENTER();

    int wait_count = 0;
    lai_attribute_t attr;
    lai_status_t status;

    attr.id = LAI_LINECARD_ATTR_BOARD_MODE;
    memset(attr.value.chardata, 0, sizeof(attr.value.chardata));
    status = lai_linecard_api->get_linecard_attribute(gLinecardId, 1, &attr);
    if (status == LAI_STATUS_SUCCESS && mode == attr.value.chardata)
    {
        SWSS_LOG_DEBUG("Linecard and maincard have a same board-mode, %s", mode.c_str());
        return;
    }

    SWSS_LOG_NOTICE("Begin to set board-mode %s", mode.c_str());

    memset(attr.value.chardata, 0, sizeof(attr.value.chardata));
    strncpy(attr.value.chardata, mode.c_str(), sizeof(attr.value.chardata) - 1);
    status = lai_linecard_api->set_linecard_attribute(gLinecardId, &attr);
    if (status != LAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to set board-mode status=%d, mode=%s",
                       status, mode.c_str());
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
        SWSS_LOG_DEBUG("board-mode = %s", attr.value.chardata);
        if (mode == attr.value.chardata)
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

