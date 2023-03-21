#pragma once

#include <string>
#include <vector>
#include <map>
#include <tuple>
#include "orch.h"
#include "laihelper.h"
#include "notifier.h"
#include "notificationproducer.h"
#include "notifications.h"
#include "timer.h"

using namespace std;
using namespace swss;

extern lai_object_id_t gLinecardId;

typedef lai_status_t (*CreateObjectFunc)(
        lai_object_id_t *oid,
        lai_object_id_t linecard_id,
        uint32_t attr_count,
        const lai_attribute_t *attr_list);

typedef lai_status_t (*RemoveObjectFunc)(
        lai_object_id_t oid);

typedef lai_status_t (*SetObjectAttrFunc)(
        lai_object_id_t oid,
        const lai_attribute_t *attr);

typedef lai_status_t (*GetObjectAttrFunc)(
        lai_object_id_t oid,
        uint32_t attr_count,
        lai_attribute_t *attr_list);

typedef enum _ConfigState_E
{
    CONFIG_MISSING = 0,
    CONFIG_RECEIVED,
    CONFIG_DONE,
} ConfigState_E;

class LaiObjectOrch: public Orch
{
public:
    LaiObjectOrch(DBConnector *db, const vector<string> &table_names): Orch(db, table_names) {}
    LaiObjectOrch(DBConnector *db,
                  const vector<string> &table_names,
                  lai_object_type_t obj_type,
                  const vector<lai_attr_id_t> &cfg_attrs);
    LaiObjectOrch(DBConnector *db,
                  const vector<string> &table_names,
                  lai_object_type_t obj_type,
                  const vector<lai_attr_id_t> &cfg_attrs,
                  const vector<string> &auxiliary_fields);
    LaiObjectOrch(DBConnector *db,
                  vector<TableConnector> &connectors,
                  lai_object_type_t obj_type,
                  const vector<lai_attr_id_t> &cfg_attrs);
    LaiObjectOrch(DBConnector *db,
                  vector<TableConnector> &connectors,
                  lai_object_type_t obj_type,
                  const vector<lai_attr_id_t> &cfg_attrs,
                  const vector<string> &auxiliary_fields);
    void localDataInit(DBConnector* db,
                       lai_object_type_t obj_type,
                       const vector<lai_attr_id_t>& cfg_attrs);

    void doTask(Consumer &consumer);
    void doTask(SelectableTimer &timer);
    void doStateTask(Consumer &consumer);
    bool createLaiObject(const string &key);
    bool removeLaiObject(const lai_object_id_t oid);
    bool syncStateTable(lai_object_id_t oid, const string &key);
    bool setLaiObjectAttrs(const string &key, map<string, string> &field_values, string operation_id="");
    lai_status_t setLaiObjectAttr(lai_object_id_t oid, const string &field, const string &value);
    lai_status_t getLaiObjectAttr(lai_object_id_t oid, const string &field, string &value);
    virtual void setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs){};
    virtual void clearFlexCounter(lai_object_id_t id, string key){};
    virtual void doSubobjectStateTask(const string &key, const string &present){};
    void clearCountersTable(string key);
    void clearStateTable(string key);
    void publishOperationResult(string operation_id, int status_code, string message);
    bool translateLaiObjectAttr(_In_ const string &field,
                                _In_ const string &value,
                                _Out_ lai_attribute_t &attr);

protected:
    shared_ptr<DBConnector> m_stateDb;
    unique_ptr<Table> m_stateTable;

    shared_ptr<DBConnector> m_countersDb;
    string m_countersTable;
    unique_ptr<Table> m_nameMapTable;

    CreateObjectFunc m_createFunc;
    RemoveObjectFunc m_removeFunc;
    SetObjectAttrFunc m_setFunc;
    GetObjectAttrFunc m_getFunc;

    lai_object_type_t m_objectType;

    string m_objectName;
    map<string, lai_attr_id_t> m_createandsetAttrs;
    map<string, lai_attr_id_t> m_createonlyAttrs;
    map<string, lai_attr_id_t> m_irrecoverableAttrs;
    map<string, lai_attr_id_t> m_mandatoryAttrs;
    map<string, lai_attr_id_t> m_readonlyAttrs;

    ConfigState_E m_config_state = CONFIG_MISSING;
    uint32_t m_count;
    set<string> m_keys;
    map<string, lai_object_id_t> m_key2oid;
    map<string, map<string, string>> m_key2createonlyAttrs;
    map<string, map<string, string>> m_key2createandsetAttrs;
    map<string, string> m_key2present;

    set<string> m_auxiliaryFields;
    map<string, vector<FieldValueTuple>> m_key2auxiliaryFvs;

    set<string> m_needToCache;

    void doTask(NotificationConsumer& consumer);
    NotificationConsumer *m_notificationConsumer;
    NotificationProducer *m_notificationProducer;

    SelectableTimer *m_timer_5_sec;
    SelectableTimer *m_timer_1_sec;

    set<string> m_preClearKeys;
    set<string> m_readyToClearKeys;
};

