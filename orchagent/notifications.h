#pragma once

extern "C" {
#include "lai.h"
}

void onApsSwitchInfoNotify();
void onLinecardAlarmNotify();
void onLinecardActive();
void onLinecardStateChange(_In_ lai_object_id_t linecard_id,
                           _In_ lai_oper_status_t linecard_oper_status);

