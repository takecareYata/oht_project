/* ============================================================================
 * zlift.c — [보드1] Z축 상승/하강 (L3)
 *
 * 리니어 액추에이터를 상단(PC0)/하단(PC1) 센서까지 움직인다.
 * 위치 제어는 없고 두 끝 위치만 다룬다. 완료는 센서 감지로만 판정한다.
 * (Z 백도어 빌드에서는 시간 또는 B1 버튼이 센서를 대신한다)
 * ============================================================================ */
#include "control.h"
static control_state_t zlift_state;
static control_error_t zlift_error;
static uint8_t zlift_target;   /* 0 = 상단(상승), 1 = 하단(하강) */
static uint32_t zlift_started; /* 이동 시작 시각 */
/* 고장: 모터 정지 후 고장 코드 기록. */
static void zlift_Set_Fault(control_error_t error)
{
    position_Set_Stop();
    linact_Set_Stop();
    zlift_state = CONTROL_ERROR;
    zlift_error = error;
}
/* 이동 시작. target 0 = 상승, 1 = 하강. 이미 목표 센서가 감지되면 즉시 완료. */
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
#if CONFIG_Z_TIMED != 0U
            /* Z 백도어: 끝에서 떠나므로 이전 가상 상단/하단 센서를 지운다 */
            sensor_Set_Virtual(0U, false);
            sensor_Set_Virtual(1U, false);
#endif
            zlift_started = timer_Get_Tick_Ms();
            position_Set_Motion(target);
            linact_Set_Run(target != 0U);
            zlift_state = CONTROL_BUSY;
        }
    }
}
/* 매 루프 판정 (위에서부터 우선):
 *   양 끝 센서 동시 → SENSOR / 목표 센서 감지 → 완료 / 15초 초과 → TIMEOUT
 *   1.5초 지나도 출발 센서가 계속 감지 → SENSOR (실제로 안 움직임) */
void zlift_Set_Update(void)
{
    uint32_t elapsed = (uint32_t)(timer_Get_Tick_Ms() - zlift_started);
    if (zlift_state == CONTROL_BUSY)
    {
#if CONFIG_Z_TIMED == 1U
        /* Z 백도어(시간 모드): 구동 시간이 다 되면 끝 센서가 감지된 것으로 간주 */
        if (elapsed >= ((zlift_target == 0U) ? CONFIG_Z_UP_MS : CONFIG_Z_DOWN_MS))
        {
            sensor_Set_Virtual(zlift_target, true);
        }
#endif
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
/* 정지 (모터 출력 off). */
void zlift_Set_Stop(void)
{
    position_Set_Stop();
    linact_Set_Stop();
    zlift_state = CONTROL_STOPPED;
}
/* Z 이동 상태. */
control_state_t zlift_Get_State(void)
{
    return zlift_state;
}
/* 마지막 고장 코드. */
control_error_t zlift_Get_Error(void)
{
    return zlift_error;
}
/* Z 백도어 버튼: 이동 중이면 도착한 것으로 처리 (바로 다음 zlift_Set_Update에서 완료). */
void zlift_Set_Confirm(void)
{
    if (zlift_state == CONTROL_BUSY) { sensor_Set_Virtual(zlift_target, true); }
}
