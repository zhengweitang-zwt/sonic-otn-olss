#pragma once

#include "laiobjectorch.h"

using namespace std;
using namespace swss;

class LogicalChannelOrch: public LaiObjectOrch
{
public:
    LogicalChannelOrch(DBConnector *db, std::vector<TableConnector>& connectors);
    void setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs);
    void clearFlexCounter(lai_object_id_t id, string key);
};

