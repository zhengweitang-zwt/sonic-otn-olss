#pragma once

#include "laiobjectorch.h"

class InterfaceOrch: public LaiObjectOrch
{
public:
    InterfaceOrch(DBConnector *db, std::vector<TableConnector>& connectors);
    void setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs);
    void clearFlexCounter(lai_object_id_t id, string key);
};

