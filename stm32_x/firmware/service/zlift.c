#include "control.h"
static control_state_t zlift_state;
static control_error_t zlift_error;
static uint8_t zlift_target;
static uint32_t zlift_started;
static void zlift_Set_Fault(control_error_t error)
{
    position_Set_Stop();
    linact_Set_Stop();
    zlift_state = CONTROL_ERROR;
    zlift_error = error;
}
void zlift_Set_Target(uint8_t target)
{
    if (target > 1U)
    {
        zlift_Set_Fault(CONTROL_ERR_COMMAND);
    }
    else if (sensor_Get_State(0U) && sensor_Get_State(1U))
    {
        zlift_Set_Fault(CONTROL_ERR_SENSOR);
    }
    else
    {
        zlift_target = target;
        zlift_error = CONTROL_OK;
        if (sensor_Get_State(target))
        {
            position_Set_Stop();
            linact_Set_Stop();
            zlift_state = CONTROL_DONE;
        }
        else
        {
            zlift_started = timer_Get_Tick_Ms();
            position_Set_Motion(target);
            linact_Set_Run(target != 0U);
            zlift_state = CONTROL_BUSY;
        }
    }
}
void zlift_Set_Update(void)
{
    uint32_t elapsed = (uint32_t)(timer_Get_Tick_Ms() - zlift_started);
    if (zlift_state == CONTROL_BUSY)
    {
        if (sensor_Get_State(0U) && sensor_Get_State(1U))
        {
            zlift_Set_Fault(CONTROL_ERR_SENSOR);
        }
        else if (sensor_Get_State(zlift_target))
        {
            position_Set_Stop();
            linact_Set_Stop();
            zlift_state = CONTROL_DONE;
        }
        else if (elapsed >= CONFIG_MOVE_TIMEOUT_MS)
        {
            zlift_Set_Fault(CONTROL_ERR_TIMEOUT);
        }
        else if ((elapsed >= CONFIG_RELEASE_TIMEOUT_MS) &&
                 sensor_Get_State((uint8_t)(1U - zlift_target)))
        {
            zlift_Set_Fault(CONTROL_ERR_SENSOR);
        }
        else
        {
        }
    }
}
void zlift_Set_Stop(void)
{
    position_Set_Stop();
    linact_Set_Stop();
    zlift_state = CONTROL_STOPPED;
}
control_state_t zlift_Get_State(void)
{
    return zlift_state;
}
control_error_t zlift_Get_Error(void)
{
    return zlift_error;
}
