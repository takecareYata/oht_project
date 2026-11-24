/* ============================================================================
 * axis.c — [X 보드] X축 명령 계층 (L4)
 *
 * xmotion 위에 "Z가 위에 있을 때만 X가 움직인다" 인터록을 얹는다.
 * Z 상단 여부는 Jetson이 요청 프레임 remote bit0으로 전달한다.
 * ============================================================================ */
#include "control.h"
static control_error_t axis_error; /* 이 계층에서 낸 고장(INTERLOCK), 없으면 OK */
/* X 이동 시작. target 0 = A, 1 = B. remote bit0(Z 상단)이 없으면 INTERLOCK. */
void axis_Set_Command(uint8_t target, uint8_t remote)
{
    axis_error = CONTROL_OK;
    if ((remote & CONTROL_REMOTE_Z_UP) == 0U)
    {
        xmotion_Set_Stop();
        axis_error = CONTROL_ERR_INTERLOCK;
    }
    else
    {
        xmotion_Set_Target(target);
    }
}
/* 매 루프: 이동 중 Z 상단 비트가 사라지면 즉시 정지(INTERLOCK), 아니면 xmotion 진행. */
void axis_Set_Update(uint8_t remote)
{
    if ((xmotion_Get_State() == CONTROL_BUSY) && ((remote & CONTROL_REMOTE_Z_UP) == 0U))
    {
        xmotion_Set_Stop();
        axis_error = CONTROL_ERR_INTERLOCK;
    }
    else
    {
        xmotion_Set_Update();
    }
}
/* 정지하고 이 계층의 고장 기록을 지운다. */
void axis_Set_Stop(void)
{
    xmotion_Set_Stop();
    axis_error = CONTROL_OK;
}
/* 상태: 인터록 고장이 있으면 ERROR, 아니면 xmotion 상태. */
control_state_t axis_Get_State(void)
{
    return (axis_error != CONTROL_OK) ? CONTROL_ERROR : xmotion_Get_State();
}
/* 고장 코드: 인터록 고장 우선, 없으면 xmotion 고장. */
control_error_t axis_Get_Error(void)
{
    return (axis_error != CONTROL_OK) ? axis_error : xmotion_Get_Error();
}
