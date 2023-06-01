extern "C" {
#include "lai.h"
}
#include <vector>
#include <inttypes.h>

#include "dbconnector.h"
#include "logger.h"
#include "notifications.h"
#include "orchfsm.h"
#include "notificationproducer.h"
#include "lai_serialize.h"

using namespace std;
using namespace swss;

void onLinecardAlarmNotify()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("Alarm notify");
}

void onApsSwitchInfoNotify()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("Switch Info notify");
}

extern int gSlotId;
void onLinecardActive()
{
#if 0
    DBConnector stateDb("STATE_DB", 0);
    NotificationProducer linecard_state(&stateDb, "PACKET_UPGRADE");

    std::vector<FieldValueTuple> fvs;
    fvs.push_back({ "slot", std::to_string(gSlotId) });
    auto sent_clients = linecard_state.send("auto", "auto", fvs);
    SWSS_LOG_NOTICE("onLinecardActive %ld client ok, SlotId %d", sent_clients, gSlotId);
    //other things to do after linecard initialization.
#endif
}

void onLinecardStateChange(
        _In_ lai_object_id_t linecard_id,
        _In_ lai_oper_status_t linecard_oper_status)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("linecard state change oper_status=%d", linecard_oper_status);

    if (linecard_oper_status == LAI_OPER_STATUS_ACTIVE)
    {
        OrchFSM::setState(ORCH_STATE_WORK);

        onLinecardActive();
    }
    else if (linecard_oper_status == LAI_OPER_STATUS_INACTIVE)
    {
        OrchFSM::setState(ORCH_STATE_PAUSE);
    }
}

void onOcmSpectrumPowerNotify(
        _In_ lai_object_id_t linecard_id,
        _In_ lai_object_id_t ocm_id,
        _In_ lai_spectrum_power_t ocm_result)
{
    SWSS_LOG_ENTER();

    DBConnector appl_db("APPL_DB", 0);
    DBConnector counters_db("COUNTERS_DB", 0);

    NotificationProducer notify(&appl_db, OCM_REPLY);

    std::string strVid = lai_serialize_object_id(ocm_id);

    auto key = counters_db.hget(COUNTERS_OCM_NAME_MAP, strVid);
    if (key == nullptr)
    {
        return;
    }

    SWSS_LOG_DEBUG("orch receive ocm notification, %s", key->c_str());

    std::string op("SUCCESS");
    std::string data(*key);
    std::vector<swss::FieldValueTuple> values;

    notify.send(op, data, values);
}

void onOtdrResultNotify(
        _In_ lai_object_id_t linecard_id,
        _In_ lai_object_id_t otdr_id,
        _In_ lai_otdr_result_t otdr_result)
{
    SWSS_LOG_ENTER();
}

