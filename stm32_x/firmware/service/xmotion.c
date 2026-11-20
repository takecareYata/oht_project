#include "control.h"
/* X motion. Two move kinds:
 *  - goal move : position is referenced -> run to step 0 (A) or TRAVEL_STEPS (B)
 *                with ramp up / ramp down; the tick interrupt stops on the goal.
 *  - seek move : position not referenced (HOME_MODE 1 before a sensor was seen)
 *                -> run at CONFIG_X_HOME_HZ until the target sensor.
 * In both kinds a detected target sensor ends the move (limit switch) and every
 * detected A/B sensor re-references the position. */
static control_state_t xmotion_state;
static control_error_t xmotion_error;
static uint8_t xmotion_target;
static uint32_t xmotion_started;
static uint32_t xmotion_ramp;
static uint16_t xmotion_rate;
static bool xmotion_referenced;
static bool xmotion_seek;
static int32_t xmotion_start_position;
static void xmotion_Set_Fault(control_error_t error)
{
    position_Set_Stop();
    stepper_Set_Stop();
    xmotion_error = error;
    xmotion_state = CONTROL_ERROR;
}
static void xmotion_Set_Done(void)
{
    position_Set_Stop();
    stepper_Set_Stop();
    xmotion_state = CONTROL_DONE;
}
/* A sensor fixes the absolute position: A = 0, B = TRAVEL_STEPS. */
static void xmotion_Set_Reference(void)
{
    bool a, b;
    if (!sensor_Get_Ready()) { return; }
    a = sensor_Get_State(0U);
    b = sensor_Get_State(1U);
    if (a && !b)
    {
        stepper_Set_Position(0);
        xmotion_referenced = true;
    }
    else if (b && !a)
    {
        stepper_Set_Position((int32_t)CONFIG_X_TRAVEL_STEPS);
        xmotion_referenced = true;
    }
    else
    {
    }
}
/* Pulses needed to ramp from rate down to CONFIG_X_START_HZ. */
static uint32_t xmotion_Get_Brake(uint16_t rate)
{
    const uint64_t accel = ((uint64_t)CONFIG_X_ACCEL_HZ * 1000U) / CONFIG_X_ACCEL_INTERVAL_MS;
    uint64_t v = rate, v0 = CONFIG_X_START_HZ;
    uint64_t brake = (v > v0) ? ((v * v - v0 * v0) / (2U * accel)) : 0U;
    /* plus the pulses that pass before the next ramp update */
    return (uint32_t)(brake + (v * CONFIG_X_ACCEL_INTERVAL_MS) / 1000U + 1U);
}
static void xmotion_Set_Ramp(void)
{
    uint16_t rate = xmotion_rate;
    if (xmotion_seek) { return; }
    if (stepper_Get_Remaining() <= xmotion_Get_Brake(rate))
    {
        rate = (rate > CONFIG_X_START_HZ + CONFIG_X_ACCEL_HZ) ? (uint16_t)(rate - CONFIG_X_ACCEL_HZ)
                                                             : (uint16_t)CONFIG_X_START_HZ;
    }
    else if (rate < CONFIG_X_STEP_HZ)
    {
        rate = (rate + CONFIG_X_ACCEL_HZ > CONFIG_X_STEP_HZ) ? (uint16_t)CONFIG_X_STEP_HZ
                                                           : (uint16_t)(rate + CONFIG_X_ACCEL_HZ);
    }
    else
    {
    }
    if (rate != xmotion_rate)
    {
        xmotion_rate = rate;
        stepper_Set_Rate(rate);
    }
}
void xmotion_Init_State(void)
{
    xmotion_state = CONTROL_STOPPED;
    xmotion_error = CONTROL_OK;
    xmotion_referenced = (CONFIG_X_HOME_MODE == 0U);
    xmotion_seek = false;
}
bool xmotion_Get_Referenced(void) { return xmotion_referenced; }
void xmotion_Set_Target(uint8_t target)
{
    if (target > 1U)
    {
        xmotion_Set_Fault(CONTROL_ERR_COMMAND);
    }
    else if (sensor_Get_State(0U) && sensor_Get_State(1U))
    {
        xmotion_Set_Fault(CONTROL_ERR_SENSOR);
    }
    else
    {
        xmotion_target = target;
        xmotion_error = CONTROL_OK;
        xmotion_Set_Reference();
        if (sensor_Get_State(target))
        {
            xmotion_Set_Done();
            return;
        }
        xmotion_seek = !xmotion_referenced;
        xmotion_started = timer_Get_Tick_Ms();
        xmotion_ramp = xmotion_started;
        xmotion_start_position = stepper_Get_Position();
        position_Set_Motion(target);
        if (xmotion_seek)
        {
            xmotion_rate = CONFIG_X_HOME_HZ;
            stepper_Set_Run(target != 0U);
            if (CONFIG_X_HOME_HZ != CONFIG_X_START_HZ) { stepper_Set_Rate(CONFIG_X_HOME_HZ); }
        }
        else
        {
            const int32_t goal = (target != 0U) ? (int32_t)CONFIG_X_TRAVEL_STEPS : 0;
            if (stepper_Get_Position() == goal)
            {
                xmotion_Set_Done();
                return;
            }
            xmotion_rate = CONFIG_X_START_HZ;
            stepper_Set_Goal(goal);
        }
        xmotion_state = CONTROL_BUSY;
    }
}
void xmotion_Set_Update(void)
{
    uint32_t now = timer_Get_Tick_Ms();
    /* Re-reference only while stationary (or on arrival below): while leaving
     * a sensor it is still active and would pin the count to its edge. */
    if (xmotion_state != CONTROL_BUSY) { xmotion_Set_Reference(); }
    if (xmotion_state == CONTROL_BUSY)
    {
        int32_t moved = stepper_Get_Position() - xmotion_start_position;
        if (moved < 0) { moved = -moved; }
        if (sensor_Get_State(0U) && sensor_Get_State(1U))
        {
            xmotion_Set_Fault(CONTROL_ERR_SENSOR);
        }
        else if (sensor_Get_State(xmotion_target))
        {
            xmotion_Set_Done();
            xmotion_Set_Reference();
        }
        else if (!xmotion_seek && !stepper_Get_Running())
        {
            /* goal pulse reached: the tick interrupt already stopped the timer */
            xmotion_Set_Done();
        }
        else if ((uint32_t)(now - xmotion_started) >= CONFIG_MOVE_TIMEOUT_MS)
        {
            xmotion_Set_Fault(CONTROL_ERR_TIMEOUT);
        }
        else if (((uint32_t)(now - xmotion_started) >= CONFIG_RELEASE_TIMEOUT_MS) &&
                 sensor_Get_State((uint8_t)(1U - xmotion_target)))
        {
            xmotion_Set_Fault(CONTROL_ERR_SENSOR);
        }
        else if (xmotion_seek &&
                 ((uint32_t)moved > (CONFIG_X_TRAVEL_STEPS * (100UL + CONFIG_X_OVERRUN_PERCENT)) / 100UL))
        {
            /* went farther than the whole travel without finding the sensor */
            xmotion_Set_Fault(CONTROL_ERR_SENSOR);
        }
        else if ((uint32_t)(now - xmotion_ramp) >= CONFIG_X_ACCEL_INTERVAL_MS)
        {
            xmotion_ramp = now;
            xmotion_Set_Ramp();
        }
        else
        {
        }
    }
}
void xmotion_Set_Stop(void)
{
    position_Set_Stop();
    stepper_Set_Stop();
    xmotion_state = CONTROL_STOPPED;
}
control_state_t xmotion_Get_State(void)
{
    return xmotion_state;
}
control_error_t xmotion_Get_Error(void)
{
    return xmotion_error;
}
