#include <fstream>
#include <iostream>
#include <string>
#include <inttypes.h>
#include <sys/time.h>
#include "timestamp.h"
#include "orch.h"

#include "subscriberstatetable.h"
#include "tokenize.h"
#include "logger.h"
#include "consumerstatetable.h"

using namespace swss;
using namespace std;

extern int gBatchSize;

Orch::Orch(DBConnector *db, const string tableName, int pri)
{
    addConsumer(db, tableName, pri);
}

Orch::Orch(DBConnector *db, const vector<string> &tableNames)
{
    for (auto it : tableNames)
    {
        addConsumer(db, it, default_orch_pri);
    }
}

Orch::Orch(DBConnector *db, const vector<table_name_with_pri_t> &tableNames_with_pri)
{
    for (const auto& it : tableNames_with_pri)
    {
        addConsumer(db, it.first, it.second);
    }
}

Orch::Orch(const vector<TableConnector>& tables)
{
    for (auto it : tables)
    {
        addConsumer(it.first, it.second);
    }
}

Orch::~Orch()
{
}

vector<Selectable *> Orch::getSelectables()
{
    vector<Selectable *> selectables;
    for (auto& it : m_consumerMap)
    {
        selectables.push_back(it.second.get());
    }
    return selectables;
}

void Consumer::addToSync(const KeyOpFieldsValuesTuple &entry)
{
    SWSS_LOG_ENTER();


    string key = kfvKey(entry);
    string op  = kfvOp(entry);

    /*
    * m_toSync is a multimap which will allow one key with multiple values,
    * Also, the order of the key-value pairs whose keys compare equivalent
    * is the order of insertion and does not change. (since C++11)
    */

    /* If a new task comes we directly put it into getConsumerTable().m_toSync map */
    if (m_toSync.find(key) == m_toSync.end())
    {
        m_toSync.emplace(key, entry);
    }

    /* if a DEL task comes, we overwrite the old key */
    else if (op == DEL_COMMAND)
    {
        m_toSync.erase(key);
        m_toSync.emplace(key, entry);
    }
    else
    {
        /*
        * Now we are trying to add the key-value with SET.
        * We maintain maximum two values per key.
        * In case there is one key-value, it should be DEL or SET
        * In case there are two key-value pairs, it should be DEL then SET
        * The code logic is following:
        * We iterate the values with the key, we skip the value with DEL and then
        * check if that was the only one (I,E, the iter pointer now points to end or next key),
        * in such case, we insert the key-value with SET.
        * If there was a SET already (I,E, the pointer still points to the same key), we combine the kfv.
        */
        auto ret = m_toSync.equal_range(key);
        auto iter = ret.first;
        for (; iter != ret.second; ++iter)
        {
            auto old_op = kfvOp(iter->second);
            if (old_op == SET_COMMAND)
                break;
        }
        if (iter == ret.second)
        {
            m_toSync.emplace(key, entry);
        }
        else
        {
            KeyOpFieldsValuesTuple existing_data = iter->second;

            auto new_values = kfvFieldsValues(entry);
            auto existing_values = kfvFieldsValues(existing_data);


            for (auto it : new_values)
            {
                string field = fvField(it);
                string value = fvValue(it);

                auto iu = existing_values.begin();
                while (iu != existing_values.end())
                {
                    string ofield = fvField(*iu);
                    if (field == ofield)
                        iu = existing_values.erase(iu);
                    else
                        iu++;
                }
                existing_values.push_back(FieldValueTuple(field, value));
            }
            iter->second = KeyOpFieldsValuesTuple(key, op, existing_values);
        }
    }

}

size_t Consumer::addToSync(const std::deque<KeyOpFieldsValuesTuple> &entries)
{
    SWSS_LOG_ENTER();

    for (auto& entry: entries)
    {
        addToSync(entry);
    }

    return entries.size();
}

// TODO: Table should be const
size_t Consumer::refillToSync(Table* table)
{
    std::deque<KeyOpFieldsValuesTuple> entries;
    vector<string> keys;
    table->getKeys(keys);
    for (const auto &key: keys)
    {
        KeyOpFieldsValuesTuple kco;

        kfvKey(kco) = key;
        kfvOp(kco) = SET_COMMAND;

        if (!table->get(key, kfvFieldsValues(kco)))
        {
            continue;
        }
        entries.push_back(kco);
    }

    return addToSync(entries);
}

size_t Consumer::refillToSync()
{
    ConsumerTableBase *consumerTable = getConsumerTable();

    auto subTable = dynamic_cast<SubscriberStateTable *>(consumerTable);
    if (subTable != NULL)
    {
        std::deque<KeyOpFieldsValuesTuple> entries;
        subTable->pops(entries);
        return addToSync(entries);
    }
    else
    {
        // consumerTable is either ConsumerStateTable or ConsumerTable
        auto db = consumerTable->getDbConnector();
        string tableName = consumerTable->getTableName();
        auto table = Table(db, tableName);
        return refillToSync(&table);
    }
}

void Consumer::execute()
{
    SWSS_LOG_ENTER();

    std::deque<KeyOpFieldsValuesTuple> entries;
    getConsumerTable()->pops(entries);

    addToSync(entries);

    drain();
}

void Consumer::drain()
{
    if (!m_toSync.empty())
        m_orch->doTask(*this);
}

string Consumer::dumpTuple(const KeyOpFieldsValuesTuple &tuple)
{
    string s = getTableName() + getConsumerTable()->getTableNameSeparator() + kfvKey(tuple)
               + "|" + kfvOp(tuple);
    for (auto i = kfvFieldsValues(tuple).begin(); i != kfvFieldsValues(tuple).end(); i++)
    {
        s += "|" + fvField(*i) + ":" + fvValue(*i);
    }

    return s;
}

void Consumer::dumpPendingTasks(vector<string> &ts)
{
    for (auto &tm : m_toSync)
    {
        KeyOpFieldsValuesTuple& tuple = tm.second;

        string s = dumpTuple(tuple);

        ts.push_back(s);
    }
}

size_t Orch::addExistingData(const string& tableName)
{
    auto consumer = dynamic_cast<Consumer *>(getExecutor(tableName));
    if (consumer == NULL)
    {
        SWSS_LOG_ERROR("No consumer %s in Orch", tableName.c_str());
        return 0;
    }

    return consumer->refillToSync();
}

// TODO: Table should be const
size_t Orch::addExistingData(Table *table)
{
    string tableName = table->getTableName();
    Consumer* consumer = dynamic_cast<Consumer *>(getExecutor(tableName));
    if (consumer == NULL)
    {
        SWSS_LOG_ERROR("No consumer %s in Orch", tableName.c_str());
        return 0;
    }

    return consumer->refillToSync(table);
}

bool Orch::bake()
{
    SWSS_LOG_ENTER();

    for (auto &it : m_consumerMap)
    {
        string executorName = it.first;
        auto executor = it.second;
        auto consumer = dynamic_cast<Consumer *>(executor.get());
        if (consumer == NULL)
        {
            continue;
        }

        size_t refilled = consumer->refillToSync();
        SWSS_LOG_NOTICE("Add warm input: %s, %zd", executorName.c_str(), refilled);
    }

    return true;
}

/*
- Validates reference has proper format which is [table_name:object_name]
- validates table_name exists
- validates object with object_name exists

- Special case:
- Deem reference format [] as valid, and return true. But in such a case,
- both type_name and object_name are cleared to empty strings as an
- indication to the caller of the special case
*/
bool Orch::parseReference(type_map &type_maps, string &ref_in, string &type_name, string &object_name)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_DEBUG("input:%s", ref_in.c_str());
    if (ref_in.size() < 2)
    {
        SWSS_LOG_ERROR("invalid reference received:%s\n", ref_in.c_str());
        return false;
    }
    if ((ref_in[0] != ref_start) || (ref_in[ref_in.size()-1] != ref_end))
    {
        SWSS_LOG_ERROR("malformed reference:%s. Must be surrounded by [ ]\n", ref_in.c_str());
        return false;
    }
    if (ref_in.size() == 2)
    {
        // value set by user is "[]"
        // Deem it as a valid format
        // clear both type_name and object_name
        // as an indication to the caller that
        // such a case has been encountered
        type_name.clear();
        object_name.clear();
        return true;
    }
    string ref_content = ref_in.substr(1, ref_in.size() - 2);
    vector<string> tokens;
    tokens = tokenize(ref_content, delimiter);
    if (tokens.size() != 2)
    {
        tokens = tokenize(ref_content, config_db_key_delimiter);
        if (tokens.size() != 2)
        {
            SWSS_LOG_ERROR("malformed reference:%s. Must contain 2 tokens\n", ref_content.c_str());
            return false;
        }
    }
    auto type_it = type_maps.find(tokens[0]);
    if (type_it == type_maps.end())
    {
        SWSS_LOG_ERROR("not recognized type:%s\n", tokens[0].c_str());
        return false;
    }
    auto obj_map = type_maps[tokens[0]];
    auto obj_it = obj_map->find(tokens[1]);
    if (obj_it == obj_map->end())
    {
        SWSS_LOG_INFO("map:%s does not contain object with name:%s\n", tokens[0].c_str(), tokens[1].c_str());
        return false;
    }
    type_name = tokens[0];
    object_name = tokens[1];
    SWSS_LOG_DEBUG("parsed: type_name:%s, object_name:%s", type_name.c_str(), object_name.c_str());
    return true;
}

ref_resolve_status Orch::resolveFieldRefValue(
    type_map &type_maps,
    const string &field_name,
    KeyOpFieldsValuesTuple &tuple,
    otai_object_id_t &otai_object,
    string &referenced_object_name)
{
    SWSS_LOG_ENTER();

    bool hit = false;
    for (auto i = kfvFieldsValues(tuple).begin(); i != kfvFieldsValues(tuple).end(); i++)
    {
        SWSS_LOG_DEBUG("field:%s, value:%s", fvField(*i).c_str(), fvValue(*i).c_str());
        if (fvField(*i) == field_name)
        {
            if (hit)
            {
                SWSS_LOG_ERROR("Multiple same fields %s", field_name.c_str());
                return ref_resolve_status::multiple_instances;
            }
            string ref_type_name, object_name;
            if (!parseReference(type_maps, fvValue(*i), ref_type_name, object_name))
            {
                return ref_resolve_status::not_resolved;
            }
            else if (ref_type_name.empty() && object_name.empty())
            {
                return ref_resolve_status::empty;
            }
            otai_object = (*(type_maps[ref_type_name]))[object_name].m_otaiObjectId;
            referenced_object_name = ref_type_name + delimiter + object_name;
            hit = true;
        }
    }
    if (!hit)
    {
        return ref_resolve_status::field_not_found;
    }

    return ref_resolve_status::success;
}

void Orch::removeMeFromObjsReferencedByMe(
    type_map &type_maps,
    const string &table,
    const string &obj_name,
    const string &field,
    const string &old_referenced_obj_name)
{
    vector<string> objects = tokenize(old_referenced_obj_name, list_item_delimiter);
    for (auto &obj : objects)
    {
        // obj_name references token
        auto tokens = tokenize(obj, delimiter);
        auto &referenced_table = tokens[0];
        auto &ref_obj_name = tokens[1];
        auto &old_referenced_obj = (*type_maps[referenced_table])[ref_obj_name];
        old_referenced_obj.m_objsDependingOnMe.erase(obj_name);
        SWSS_LOG_INFO("Obj %s.%s Field %s: Remove reference to %s %s (now %s)",
                      table.c_str(), obj_name.c_str(), field.c_str(),
                      referenced_table.c_str(), ref_obj_name.c_str(),
                      to_string(old_referenced_obj.m_objsDependingOnMe.size()).c_str());
    }
}

void Orch::setObjectReference(
    type_map &type_maps,
    const string &table,
    const string &obj_name,
    const string &field,
    const string &referenced_obj)
{
    auto &obj = (*type_maps[table])[obj_name];
    auto field_ref = obj.m_objsReferencingByMe.find(field);

    if (field_ref != obj.m_objsReferencingByMe.end())
        removeMeFromObjsReferencedByMe(type_maps, table, obj_name, field, field_ref->second);

    obj.m_objsReferencingByMe[field] = referenced_obj;

    // Add the reference to the new object being referenced
    vector<string> objs = tokenize(referenced_obj, list_item_delimiter);
    for (auto &obj : objs)
    {
        auto tokens = tokenize(obj, delimiter);
        auto &referenced_table = tokens[0];
        auto &referenced_obj_name = tokens[1];
        auto &new_obj_being_referenced = (*type_maps[referenced_table])[referenced_obj_name];
        new_obj_being_referenced.m_objsDependingOnMe.insert(obj_name);
        SWSS_LOG_INFO("Obj %s.%s Field %s: Add reference to %s %s (now %s)",
                      table.c_str(), obj_name.c_str(), field.c_str(),
                      referenced_table.c_str(), referenced_obj_name.c_str(),
                      to_string(new_obj_being_referenced.m_objsDependingOnMe.size()).c_str());
    }
}

void Orch::removeObject(
    type_map &type_maps,
    const string &table,
    const string &obj_name)
{
    auto &obj = (*type_maps[table])[obj_name];

    for (auto field_ref : obj.m_objsReferencingByMe)
    {
        removeMeFromObjsReferencedByMe(type_maps, table, obj_name, field_ref.first, field_ref.second);
    }

    // Update the field store
    (*type_maps[table]).erase(obj_name);
    SWSS_LOG_INFO("Obj %s:%s is removed from store", table.c_str(), obj_name.c_str());
}

bool Orch::isObjectBeingReferenced(
    type_map &type_maps,
    const string &table,
    const string &obj_name)
{
    return !(*type_maps[table])[obj_name].m_objsDependingOnMe.empty();
}

string Orch::objectReferenceInfo(
    type_map &type_maps,
    const string &table,
    const string &obj_name)
{
    auto &objsDependingSet = (*type_maps[table])[obj_name].m_objsDependingOnMe;
    for (auto &depObjName : objsDependingSet)
    {
        string hint = table + " " + obj_name + " one object: " + depObjName;
        hint += " reference count: " + to_string(objsDependingSet.size());
        return hint;
    }
    return "reference count: 0";
}

void Orch::doTask()
{
    for (auto &it : m_consumerMap)
    {
        it.second->drain();
    }
}

void Orch::dumpPendingTasks(vector<string> &ts)
{
    for (auto &it : m_consumerMap)
    {
        Consumer* consumer = dynamic_cast<Consumer *>(it.second.get());
        if (consumer == NULL)
        {
            SWSS_LOG_DEBUG("Executor is not a Consumer");
            continue;
        }

        consumer->dumpPendingTasks(ts);
    }
}

string Orch::dumpTuple(Consumer &consumer, const KeyOpFieldsValuesTuple &tuple)
{
    string s = consumer.dumpTuple(tuple);
    return s;
}

ref_resolve_status Orch::resolveFieldRefArray(
    type_map &type_maps,
    const string &field_name,
    KeyOpFieldsValuesTuple &tuple,
    vector<otai_object_id_t> &otai_object_arr,
    string &object_name_list)
{
    // example: [BUFFER_PROFILE_TABLE:e_port.profile0],[BUFFER_PROFILE_TABLE:e_port.profile1]
    SWSS_LOG_ENTER();
    size_t count = 0;
    otai_object_arr.clear();
    for (auto i = kfvFieldsValues(tuple).begin(); i != kfvFieldsValues(tuple).end(); i++)
    {
        if (fvField(*i) == field_name)
        {
            if (count > 1)
            {
                SWSS_LOG_ERROR("Singleton field with name:%s must have only 1 instance, actual count:%zd\n", field_name.c_str(), count);
                return ref_resolve_status::multiple_instances;
            }
            string ref_type_name, object_name;
            string list = fvValue(*i);
            vector<string> list_items;
            if (list.find(list_item_delimiter) != string::npos)
            {
                list_items = tokenize(list, list_item_delimiter);
            }
            else
            {
                list_items.push_back(list);
            }
            for (size_t ind = 0; ind < list_items.size(); ind++)
            {
                if (!parseReference(type_maps, list_items[ind], ref_type_name, object_name))
                {
                    SWSS_LOG_ERROR("Failed to parse profile reference:%s\n", list_items[ind].c_str());
                    return ref_resolve_status::not_resolved;
                }
                otai_object_id_t otai_obj = (*(type_maps[ref_type_name]))[object_name].m_otaiObjectId;
                SWSS_LOG_DEBUG("Resolved to otai_object:0x%" PRIx64 ", type:%s, name:%s", otai_obj, ref_type_name.c_str(), object_name.c_str());
                otai_object_arr.push_back(otai_obj);
                if (!object_name_list.empty())
                    object_name_list += list_item_delimiter;
                object_name_list += ref_type_name + delimiter + object_name;
            }
            count++;
        }
    }
    if (0 == count)
    {
        SWSS_LOG_NOTICE("field with name:%s not found\n", field_name.c_str());
        return ref_resolve_status::field_not_found;
    }
    return ref_resolve_status::success;
}

bool Orch::parseIndexRange(const string &input, otai_uint32_t &range_low, otai_uint32_t &range_high)
{
    SWSS_LOG_ENTER();
    SWSS_LOG_DEBUG("input:%s", input.c_str());
    if (input.find(range_specifier) != string::npos)
    {
        vector<string> range_values;
        range_values = tokenize(input, range_specifier);
        if (range_values.size() != 2)
        {
            SWSS_LOG_ERROR("malformed index range in:%s. Must contain 2 tokens\n", input.c_str());
            return false;
        }
        range_low = (uint32_t)stoul(range_values[0]);
        range_high = (uint32_t)stoul(range_values[1]);
        if (range_low >= range_high)
        {
            SWSS_LOG_ERROR("malformed index range in:%s. left value must be less than right value.\n", input.c_str());
            return false;
        }
    }
    else
    {
        range_low = range_high = (uint32_t)stoul(input);
    }
    SWSS_LOG_DEBUG("resulting range:%d-%d", range_low, range_high);
    return true;
}

void Orch::addConsumer(DBConnector *db, string tableName, int pri)
{
	SWSS_LOG_NOTICE("add consumer dbname is %s table name is %s",db->getDbName().c_str(),tableName.c_str());
    if (db->getDbName() == "CONFIG_DB" || db->getDbName() == "STATE_DB")
    {
        addExecutor(new Consumer(new SubscriberStateTable(db, tableName, TableConsumable::DEFAULT_POP_BATCH_SIZE, pri), this, tableName));
    }
    else
    {
        addExecutor(new Consumer(new ConsumerStateTable(db, tableName, gBatchSize, pri), this, tableName));
    }
}

void Orch::addExecutor(Executor* executor)
{
    auto inserted = m_consumerMap.emplace(std::piecewise_construct,
            std::forward_as_tuple(executor->getName()),
            std::forward_as_tuple(executor));

    // If there is duplication of executorName in m_consumerMap, logic error
    if (!inserted.second)
    {
        SWSS_LOG_THROW("Duplicated executorName in m_consumerMap: %s", executor->getName().c_str());
    }
}

Executor *Orch::getExecutor(string executorName)
{
    auto it = m_consumerMap.find(executorName);
    if (it != m_consumerMap.end())
    {
        return it->second.get();
    }

    return NULL;
}

void Orch2::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        bool erase_from_queue = true;
        try
        {
            request_.parse(it->second);
            auto table_name = consumer.getTableName();
            request_.setTableName(table_name);

            auto op = request_.getOperation();
            if (op == SET_COMMAND)
            {
                erase_from_queue = addOperation(request_);
            }
            else if (op == DEL_COMMAND)
            {
                erase_from_queue = delOperation(request_);
            }
            else
            {
                SWSS_LOG_ERROR("Wrong operation. Check RequestParser: %s", op.c_str());
            }
        }
        catch (const std::invalid_argument& e)
        {
            SWSS_LOG_ERROR("Parse error: %s", e.what());
        }
        catch (const std::logic_error& e)
        {
            SWSS_LOG_ERROR("Logic error: %s", e.what());
        }
        catch (const std::exception& e)
        {
            SWSS_LOG_ERROR("Exception was catched in the request parser: %s", e.what());
        }
        catch (...)
        {
            SWSS_LOG_ERROR("Unknown exception was catched in the request parser");
        }
        request_.clear();

        if (erase_from_queue)
        {
            it = consumer.m_toSync.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

