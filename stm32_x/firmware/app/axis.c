#include "control.h"
static control_error_t axis_error;
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
void axis_Set_Stop(void)
{
    xmotion_Set_Stop();
    axis_error = CONTROL_OK;
}
control_state_t axis_Get_State(void)
{
    return (axis_error != CONTROL_OK) ? CONTROL_ERROR : xmotion_Get_State();
}
control_error_t axis_Get_Error(void)
{
    return (axis_error != CONTROL_OK) ? axis_error : xmotion_Get_Error();
}
