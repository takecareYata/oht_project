#include "control.h"
/* Diagnostic only. No motion or interlock decisions depend on this estimate. */
static int32_t position_value;
static uint8_t position_source, position_target;
static bool position_moving;
static uint32_t position_last, position_fraction;
static uint32_t position_Get_Counter(void)
{
    return timer_Get_Tick_Ms();
}
void position_Init_State(void)
{
    position_value = 0;
    position_source = 0U;
    position_moving = false;
    position_fraction = 0U;
    position_last = position_Get_Counter();
}
#if BOARD_ID == 2
/* X: the step counter is the position (0 = A, CONFIG_X_TRAVEL_STEPS = B),
 * reported on the 0..1000 scale. pos_src 2 at a sensor, 1 from the step count
 * once referenced, 0 when unknown or sensors conflict. */
void position_Set_Update(void)
{
    int64_t scaled;
    if (!sensor_Get_Ready() || (sensor_Get_State(0U) && sensor_Get_State(1U)))
    { position_source = 0U; return; }
    if (sensor_Get_State(0U) || sensor_Get_State(1U))
    {
        position_value = sensor_Get_State(0U) ? 0 : 1000;
        position_source = 2U;
        return;
    }
    if (!xmotion_Get_Referenced()) { position_source = 0U; return; }
    scaled = ((int64_t)stepper_Get_Position() * 1000) / (int64_t)CONFIG_X_TRAVEL_STEPS;
    position_value = (scaled < 1) ? 1 : ((scaled > 999) ? 999 : (int32_t)scaled);
    position_source = 1U;
}
#else
void position_Set_Update(void)
{
    uint32_t now = position_Get_Counter();
    uint32_t elapsed = now - position_last;
    position_last = now;
    if (!sensor_Get_Ready() || (sensor_Get_State(0U) && sensor_Get_State(1U)))
    { position_source = 0U; return; }
    if (sensor_Get_State(0U) || sensor_Get_State(1U))
    {
        position_value = sensor_Get_State(0U) ? 0 : 1000;
        position_source = 2U;
        position_fraction = 0U;
    }
    else if (position_source != 0U)
    {
        if (position_moving)
        {
#if BOARD_ID == 2
            const uint32_t travel = CONFIG_X_TRAVEL_STEPS;
#else
            const uint32_t travel = CONFIG_Z_TRAVEL_MS;
#endif
            uint64_t scaled = (uint64_t)elapsed * 1000U + position_fraction;
            uint32_t delta = (scaled / travel > 1000U) ? 1000U : (uint32_t)(scaled / travel);
            position_fraction = (uint32_t)(scaled % travel);
            position_value += (position_target == 0U) ? -(int32_t)delta : (int32_t)delta;
        }
        if (position_value < 1) { position_value = 1; }
        if (position_value > 999) { position_value = 999; }
        position_source = 1U;
    }
}
#endif
void position_Set_Motion(uint8_t target)
{
    position_Set_Update();
    position_target = target;
    position_moving = true;
    position_fraction = 0U;
}
void position_Set_Stop(void)
{
    position_Set_Update();
    position_moving = false;
}
int32_t position_Get_Value(void) { return position_value; }
uint8_t position_Get_Source(void) { return position_source; }
