#include "control.h"
bool interlock_Get_Permit(uint8_t command, uint8_t remote)
{
    bool stationary = (remote & (CONTROL_REMOTE_X_STOPPED | CONTROL_REMOTE_X_STATION)) ==
                      (CONTROL_REMOTE_X_STOPPED | CONTROL_REMOTE_X_STATION);
    bool permit = false;
    if (command == CONTROL_CMD_Z_UP)
    {
        permit = (remote & CONTROL_REMOTE_X_STOPPED) != 0U;
    }
    else if ((command == CONTROL_CMD_Z_DOWN) || (command == CONTROL_CMD_PICK) ||
             (command == CONTROL_CMD_PLACE))
    {
        permit = stationary;
    }
    else if ((command == CONTROL_CMD_OPEN) || (command == CONTROL_CMD_CLOSE))
    {
        permit = stationary && sensor_Get_State(1U);
    }
    else
    {
    }
    return permit;
}
