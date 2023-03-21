#pragma once

#include "laiobjectorch.h"

class ApsportOrch: public LaiObjectOrch
{
public:
    ApsportOrch(DBConnector *db, const vector<string> &table_names);
    void setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs);
};

