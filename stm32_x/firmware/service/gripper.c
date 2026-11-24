/* ============================================================================
 * gripper.c — [보드1] 집게 동작 (L3)
 *
 * 28BYJ-48 스텝모터로 열고 닫는다. 위치 피드백은 없고, PC2 접촉 스위치로만 판단한다.
 *   닫기: 접촉 스위치가 눌릴 때까지 스텝 → 파지 완료
 *   열기: 스텝 0(전원 인가 시 열림 위치)까지 복귀 → 스위치 해제 확인
 * 움직이지 않을 때는 항상 코일 전류를 끈다 (기어가 위치 유지).
 * ============================================================================ */
#include "control.h"
/* 상태 변수 */
static control_state_t gripper_state;
static control_error_t gripper_error;
static bool gripper_close;      /* 진행 중 방향: true = 닫기 */
static bool gripper_holding;    /* 접촉 확인된 파지 상태 */
static bool gripper_opened;     /* 열기 완료 확인됨 (닫기 전 필수 조건) */
static uint32_t gripper_started; /* 동작 시작 시각 (시간 초과 판정) */
static uint32_t gripper_last;    /* 마지막 스텝 시각 */
/* 동작 종료 공통: 코일 off 후 결과 상태 기록. */
static void gripper_Set_Result(control_state_t state, control_error_t error)
{
    gripstep_Set_Release();
    gripper_state = state;
    if (error != CONTROL_OK) { gripper_error = error; }
}
/* 동작 시작. close = true면 닫기, false면 열기.
 * 닫기는 "열기 완료 확인" + "스위치 해제" 상태에서만 시작한다. 아니면 GRIP 고장. */
void gripper_Set_Target(bool close)
{
    gripper_error = CONTROL_OK;
    if (close && (!gripper_opened || sensor_Get_State(2U)))
    {
        gripper_Set_Result(CONTROL_ERROR, CONTROL_ERR_GRIP);
    }
    else
    {
        gripper_close = close;
        gripper_started = timer_Get_Tick_Ms();
        gripper_last = gripper_started;
        gripper_state = CONTROL_BUSY;
        gripper_holding = false;
        gripper_opened = false;
#if CONFIG_Z_TIMED != 0U
        if (!close) { sensor_Set_Virtual(2U, false); } /* 백도어: 가상 접촉 해제 */
#endif
    }
}
/* 매 루프: CONFIG_GRIP_STEP_MS마다 최대 1스텝 (밀린 스텝을 몰아서 밟지 않음).
 * 닫기: 접촉 스위치 감지 → 파지 완료 / 최대 스텝 또는 시간 초과 → GRIP 고장
 * 열기: 스텝 0 도달 → 스위치가 풀려 있으면 완료, 눌려 있으면 GRIP 고장 */
void gripper_Set_Update(void)
{
    uint32_t now = timer_Get_Tick_Ms();
    uint32_t elapsed = (uint32_t)(now - gripper_started);
    bool step_due = (uint32_t)(now - gripper_last) >= CONFIG_GRIP_STEP_MS;
    /* 파지 중 스위치가 풀림 = 물건을 놓침 → GRIP 고장 */
    if (gripper_holding && !sensor_Get_State(2U))
    {
        gripper_holding = false;
        gripper_Set_Result(CONTROL_ERROR, CONTROL_ERR_GRIP);
    }
    if (gripper_state == CONTROL_BUSY)
    {
        if (gripper_close)
        {
#if CONFIG_Z_TIMED == 1U
            /* Z 백도어(시간 모드): 정해진 스텝 수에 도달하면 접촉한 것으로 간주 */
            if (gripstep_Get_Position() >= (int32_t)CONFIG_GRIP_CLOSE_STEPS)
            {
                sensor_Set_Virtual(2U, true);
            }
#endif
            if (sensor_Get_State(2U))
            {
                gripper_holding = true;
                gripper_Set_Result(CONTROL_DONE, CONTROL_OK);
            }
            else if ((elapsed >= CONFIG_GRIP_TIMEOUT_MS) ||
                     (gripstep_Get_Position() >= (int32_t)CONFIG_GRIP_CLOSE_MAX_STEPS))
            {
                gripper_Set_Result(CONTROL_ERROR, CONTROL_ERR_GRIP);
            }
            else if (step_due)
            {
                gripper_last = now;
                gripstep_Set_Step(true);
            }
            else
            {
            }
        }
        else if (gripstep_Get_Position() <= 0)
        {
            if (sensor_Get_State(2U))
            {
                gripper_Set_Result(CONTROL_ERROR, CONTROL_ERR_GRIP);
            }
            else
            {
                gripper_opened = true;
                gripper_Set_Result(CONTROL_DONE, CONTROL_OK);
            }
        }
        else if (elapsed >= CONFIG_GRIP_TIMEOUT_MS)
        {
            gripper_Set_Result(CONTROL_ERROR, CONTROL_ERR_GRIP);
        }
        else if (step_due)
        {
            gripper_last = now;
            gripstep_Set_Step(false);
        }
        else
        {
        }
    }
}
/* 정지: 코일 off. 스텝 위치와 파지 여부는 유지한다. */
void gripper_Set_Stop(void)
{
    gripstep_Set_Release();
    gripper_state = CONTROL_STOPPED;
    gripper_opened = false; /* 다음 닫기 전에 다시 열어야 함 */
}
/* 집게 동작 상태. */
control_state_t gripper_Get_State(void)
{
    return gripper_state;
}
/* 마지막 고장 코드. */
control_error_t gripper_Get_Error(void)
{
    return gripper_error;
}
/* 파지 중인가 (PLACE 시작 조건). */
bool gripper_Get_Holding(void)
{
    return gripper_holding;
}
/* Z 백도어 버튼: 닫는 중이면 접촉한 것으로 처리. 열기는 스스로 끝나므로 무시. */
void gripper_Set_Confirm(void)
{
    if ((gripper_state == CONTROL_BUSY) && gripper_close) { sensor_Set_Virtual(2U, true); }
}
