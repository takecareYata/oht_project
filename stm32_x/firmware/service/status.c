/* ============================================================================
 * status.c — 상태 보고 값 (L3)
 *
 * state, fault, stage, op_seq, heartbeat와 상태 LED를 관리한다.
 * heartbeat는 메인 루프가 돌고 있다는 증거 (NOP 횟수와 무관하게 100 ms마다 +1).
 * ============================================================================ */
#include "control.h"
static control_state_t status_state = CONTROL_STOPPED; /* 응답 state */
static control_error_t status_error = CONTROL_ERR_STOP; /* 응답 fault */
static uint16_t status_seq, status_heartbeat; /* op_seq(마지막 수락 작업 seq), heartbeat */
static uint32_t status_last_heartbeat;
static uint8_t status_stage = 15U; /* 응답 stage (15 = BOOT, ARM 전) */
/* 부팅 시: UNKNOWN, fault 0, stage 15(BOOT). */
void status_Init_State(void)
{
    status_state = CONTROL_STOPPED;
    status_error = CONTROL_OK;
    status_seq = 0U;
    status_stage = 15U;
    status_heartbeat = 0U;
    status_last_heartbeat = timer_Get_Tick_Ms();
}
/* stage: 0 NONE, 1 X→A, 2 X→B, 3 Z→상단, 4~7 PICK, 8~10 PLACE, 15 BOOT */
void status_Set_Stage(uint8_t stage) { status_stage = stage; }
uint8_t status_Get_Stage(void) { return status_stage; }
uint16_t status_Get_Heartbeat(void) { return status_heartbeat; }
/* op_seq: 수락한 동작 명령의 seq (ARM/STOP/NOP에는 유지). */
void status_Set_Sequence(uint16_t seq)
{
    status_seq = seq;
}
uint16_t status_Get_Sequence(void)
{
    return status_seq;
}
/* 전체 상태와 고장 코드 설정. */
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
/* 매 루프: 100 ms마다 heartbeat 증가, LED 깜빡임. */
void status_Set_Update(void)
{
    uint32_t now = timer_Get_Tick_Ms();
    if ((uint32_t)(now - status_last_heartbeat) >= 100U)
    {
        status_heartbeat++;
        status_last_heartbeat = now;
    }
#if ((BOARD_ID == 1) && (CONFIG_Z_TIMED != 0U)) || \
    ((BOARD_ID == 2) && (CONFIG_X_STATION_STEPS != 0U))
    bsp_Set_Led(((now / 100U) & 1U) != 0U); /* 빠른 깜빡임(100 ms) = 백도어 빌드 */
#else
    bsp_Set_Led(((now / 250U) & 1U) != 0U);
#endif
}
