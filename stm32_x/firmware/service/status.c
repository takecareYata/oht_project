#include "control.h"
static control_state_t status_state = CONTROL_STOPPED;
static control_error_t status_error = CONTROL_ERR_STOP;
static uint16_t status_seq, status_heartbeat;
static uint32_t status_last_heartbeat;
static uint8_t status_stage = 15U;
void status_Init_State(void)
{
    status_state = CONTROL_STOPPED;
    status_error = CONTROL_OK;
    status_seq = 0U;
    status_stage = 15U;
    status_heartbeat = 0U;
    status_last_heartbeat = timer_Get_Tick_Ms();
}
void status_Set_Stage(uint8_t stage) { status_stage = stage; }
uint8_t status_Get_Stage(void) { return status_stage; }
uint16_t status_Get_Heartbeat(void) { return status_heartbeat; }
void status_Set_Sequence(uint16_t seq)
{
    status_seq = seq;
}
uint16_t status_Get_Sequence(void)
{
    return status_seq;
}
void status_Set_State(control_state_t state, control_error_t error)
{
    status_state = state;
    status_error = error;
}
control_state_t status_Get_State(void)
{
    return status_state;
}
control_error_t status_Get_Error(void)
{
    return status_error;
}
void status_Set_Update(void)
{
    uint32_t now = timer_Get_Tick_Ms();
    if ((uint32_t)(now - status_last_heartbeat) >= 100U)
    {
        status_heartbeat++;
        status_last_heartbeat = now;
    }
    bsp_Set_Led(((timer_Get_Tick_Ms() / 250U) & 1U) != 0U);
}
