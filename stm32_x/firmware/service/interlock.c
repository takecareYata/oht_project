/* ============================================================================
 * interlock.c — [보드1] Z 동작 허용 조건 (L3)
 *
 * X가 움직이는 중이거나 정거장에 없으면 Z를 내리지 않는다.
 * 동작 중에도 pick.c가 매 루프 다시 확인한다.
 * ============================================================================ */
#include "control.h"
/* command를 지금 실행해도 되는가. remote = Jetson이 전달한 X 보드 상태 비트.
 *   Z_UP          : X 정지(bit1)
 *   Z_DOWN / PICK / PLACE : X 정지(bit1) + 정거장 도착(bit2)
 *   OPEN / CLOSE  : 위 조건 + Z 하단 센서 감지 */
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
