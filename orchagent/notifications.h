#pragma once

extern "C" {
#include "lai.h"
}

void onApsSwitchInfoNotify();
void onLinecardAlarmNotify();
void onLinecardActive();
void onLinecardStateChange(_In_ lai_object_id_t linecard_id,
                           _In_ lai_oper_status_t linecard_oper_status);
void onOcmSpectrumPowerNotify(_In_ lai_object_id_t linecard_id,
                              _In_ lai_object_id_t ocm_id,
                              _In_ lai_spectrum_power_t ocm_result);

void onOtdrResultNotify(_In_ lai_object_id_t linecard_id,
                        _In_ lai_object_id_t ocm_id,
                        _In_ lai_otdr_result_t otdr_result);

