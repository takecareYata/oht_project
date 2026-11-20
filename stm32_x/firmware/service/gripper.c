#include "control.h"
static control_state_t gripper_state;
static control_error_t gripper_error;
static bool gripper_close;
static bool gripper_holding;
static bool gripper_opened;
static uint16_t gripper_pulse = CONFIG_SERVO_OPEN_US;
static uint32_t gripper_started;
static uint32_t gripper_last;
void gripper_Set_Target(bool close)
{
    gripper_error = CONTROL_OK;
    if (close && (!gripper_opened || sensor_Get_State(2U)))
    {
        gripper_state = CONTROL_ERROR;
        gripper_error = CONTROL_ERR_GRIP;
    }
    else
    {
        gripper_close = close;
        gripper_started = timer_Get_Tick_Ms();
        gripper_last = gripper_started;
        gripper_state = CONTROL_BUSY;
        gripper_holding = false;
        gripper_opened = false;
        if (!close)
        {
            gripper_pulse = CONFIG_SERVO_OPEN_US;
            servo_Set_Pulse(gripper_pulse);
        }
    }
}
void gripper_Set_Update(void)
{
    uint32_t now = timer_Get_Tick_Ms();
    uint32_t elapsed = (uint32_t)(now - gripper_started);
    if (gripper_holding && !sensor_Get_State(2U))
    {
        gripper_holding = false;
        gripper_state = CONTROL_ERROR;
        gripper_error = CONTROL_ERR_GRIP;
    }
    if (gripper_state == CONTROL_BUSY)
    {
        if (gripper_close)
        {
            if (sensor_Get_State(2U))
            {
                gripper_holding = true;
                gripper_state = CONTROL_DONE;
            }
            else if (elapsed >= CONFIG_GRIP_TIMEOUT_MS)
            {
                gripper_state = CONTROL_ERROR;
                gripper_error = CONTROL_ERR_GRIP;
            }
            else if ((uint32_t)(now - gripper_last) >= CONFIG_SERVO_PERIOD_MS)
            {
                gripper_last = now;
                if (gripper_pulse < CONFIG_SERVO_CLOSED_US)
                {
                    gripper_pulse = (uint16_t)(gripper_pulse + CONFIG_SERVO_INCREMENT_US);
                    if (gripper_pulse > CONFIG_SERVO_CLOSED_US)
                    {
                        gripper_pulse = CONFIG_SERVO_CLOSED_US;
                    }
                    servo_Set_Pulse(gripper_pulse);
                }
            }
            else
            {
            }
        }
        else if (elapsed >= CONFIG_SERVO_SETTLE_MS)
        {
            if (sensor_Get_State(2U))
            {
                gripper_state = CONTROL_ERROR;
                gripper_error = CONTROL_ERR_GRIP;
            }
            else
            {
                gripper_state = CONTROL_DONE;
                gripper_opened = true;
            }
        }
        else
        {
        }
    }
}
void gripper_Set_Stop(void)
{
    gripper_state = CONTROL_STOPPED;
    gripper_opened = false; /* Retain pulse and holding observation. */
}
control_state_t gripper_Get_State(void)
{
    return gripper_state;
}
control_error_t gripper_Get_Error(void)
{
    return gripper_error;
}
bool gripper_Get_Holding(void)
{
    return gripper_holding;
}
