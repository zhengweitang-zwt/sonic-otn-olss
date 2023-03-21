#pragma once

#include "laiobjectorch.h"

class AssignmentOrch: public LaiObjectOrch
{
public:
    AssignmentOrch(DBConnector *db, const vector<string> &table_names);
    void setFlexCounter(lai_object_id_t id, vector<lai_attribute_t> &attrs);
};

