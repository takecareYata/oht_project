#include "control.h"
/* TB6600 STEP/DIR driver: TIM1 CH1 (PA8) emits the PUL pulses in hardware,
 * PA9 is DIR, PC7 is ENA. Each TIM1 update interrupt marks one completed pulse.
 *
 * Position is a signed pulse count: + toward B, - toward A (CONFIG_X_REVERSE
 * only flips the DIR pin, not the count). With a goal set, the update
 * interrupt stops the timer on exactly the goal pulse. */
static volatile bool stepper_running;
static volatile bool stepper_forward;
static volatile bool stepper_has_goal;
static volatile int32_t stepper_position;
static volatile int32_t stepper_goal;
static void stepper_Set_Start(bool forward)
{
    /* Never change DIR while pulses are running: stop first, then set DIR.
     * The first rising PUL edge comes half a period after the timer starts,
     * which covers the TB6600 DIR/ENA setup time (>= 5 us). */
    stepper_running = false;
    bsp_Set_Step_Timer_Stop();
    stepper_forward = forward;
    bsp_Set_X_Direction(forward != (CONFIG_X_REVERSE != 0U));
    bsp_Set_X_Enable(true);
    stepper_running = true;
    bsp_Set_Step_Timer(CONFIG_X_START_HZ);
}
void stepper_Init_Driver(void)
{
    stepper_running = false;
    stepper_has_goal = false;
    stepper_position = 0;
    bsp_Set_Step_Timer_Stop();
    bsp_Set_X_Enable(false);
}
/* Run without a goal (sensor seek). */
void stepper_Set_Run(bool forward)
{
    stepper_has_goal = false;
    stepper_Set_Start(forward);
}
/* Run to an absolute pulse position and stop there. */
void stepper_Set_Goal(int32_t goal)
{
    if (goal == stepper_position)
    {
        stepper_Set_Stop();
        return;
    }
    stepper_goal = goal;
    stepper_has_goal = true;
    stepper_Set_Start(goal > stepper_position);
}
void stepper_Set_Stop(void)
{
    stepper_running = false;
    stepper_has_goal = false;
    bsp_Set_Step_Timer_Stop();
    if (CONFIG_X_HOLD_ON_STOP == 0U)
    {
        bsp_Set_X_Enable(false);
    }
}
void stepper_Set_Disable(void)
{
    stepper_Set_Stop();
    bsp_Set_X_Enable(false);
}
void stepper_Set_Rate(uint16_t hz)
{
    if (stepper_running)
    {
        bsp_Set_Step_Timer(hz);
    }
}
/* Called from the TIM1 update interrupt: one pulse finished. */
void stepper_Set_Tick(void)
{
    if (stepper_running)
    {
        stepper_position += stepper_forward ? 1 : -1;
        if (stepper_has_goal && (stepper_position == stepper_goal))
        {
            stepper_Set_Stop();
        }
    }
}
bool stepper_Get_Running(void) { return stepper_running; }
int32_t stepper_Get_Position(void) { return stepper_position; }
/* Single aligned 32-bit store: atomic against the tick interrupt on Cortex-M4. */
void stepper_Set_Position(int32_t position) { stepper_position = position; }
/* Pulses left to the goal; UINT32_MAX when running without a goal. */
uint32_t stepper_Get_Remaining(void)
{
    int32_t diff;
    if (!stepper_has_goal) { return UINT32_MAX; }
    diff = stepper_goal - stepper_position;
    return (uint32_t)((diff < 0) ? -diff : diff);
}
