extern "C" {
#include "lai.h"
}
#include <vector>

#include "dbconnector.h"
#include "logger.h"
#include "notifications.h"
#include "orchfsm.h"
#include "notificationproducer.h"

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

void onLinecardStateChange(_In_ lai_object_id_t linecard_id,
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


