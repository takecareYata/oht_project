/* ============================================================================
 * xmotion.c — [보드2] X축 이동 (L3)
 *
 * 두 가지 이동 방식:
 *   목표 이동: 원점이 잡혀 있으면 스텝 0(A) 또는 TRAVEL_STEPS(B)까지 가감속 이동.
 *              TIM1 인터럽트가 목표 펄스에서 정확히 멈춘다.
 *   탐색 이동: 원점이 없으면(HOME_MODE 1, 센서를 아직 못 봄) 저속으로 목표 센서까지.
 * 어느 방식이든 목표 센서가 감지되면 멈추고(리밋 스위치 역할), A/B 센서는 위치를 보정한다.
 * ============================================================================ */
#include "control.h"
/* 상태 변수 */
static control_state_t xmotion_state;
static control_error_t xmotion_error;
static uint8_t xmotion_target;          /* 0 = A, 1 = B */
static uint32_t xmotion_started;        /* 이동 시작 시각 */
static uint32_t xmotion_ramp;           /* 마지막 가감속 갱신 시각 */
static uint16_t xmotion_rate;           /* 현재 펄스 주파수 Hz */
static bool xmotion_referenced;         /* 절대 위치를 알고 있는가 (원점 잡힘) */
static bool xmotion_seek;               /* true = 센서 탐색 이동 */
static int32_t xmotion_start_position;  /* 이동 시작 위치 (탐색 거리 제한용) */
/* 고장: 정지 후 고장 코드 기록. */
static void xmotion_Set_Fault(control_error_t error)
{
    position_Set_Stop();
    stepper_Set_Stop();
    xmotion_error = error;
    xmotion_state = CONTROL_ERROR;
}
/* 완료: 정지 후 DONE. */
static void xmotion_Set_Done(void)
{
    position_Set_Stop();
    stepper_Set_Stop();
    xmotion_state = CONTROL_DONE;
}
/* 끝 센서로 절대 위치 보정: A = 0, B = CONFIG_X_TRAVEL_STEPS. */
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
/* 현재 속도에서 시작 속도까지 감속하는 데 필요한 펄스 수 (v² - v0² = 2as). */
static uint32_t xmotion_Get_Brake(uint16_t rate)
{
    const uint64_t accel = ((uint64_t)CONFIG_X_ACCEL_HZ * 1000U) / CONFIG_X_ACCEL_INTERVAL_MS;
    uint64_t v = rate, v0 = CONFIG_X_START_HZ;
    uint64_t brake = (v > v0) ? ((v * v - v0 * v0) / (2U * accel)) : 0U;
    /* 다음 가감속 갱신까지 지나갈 펄스도 더한다 */
    return (uint32_t)(brake + (v * CONFIG_X_ACCEL_INTERVAL_MS) / 1000U + 1U);
}
/* CONFIG_X_ACCEL_INTERVAL_MS(20 ms)마다 호출:
 * 남은 펄스가 감속 거리 이하면 감속, 아니면 최고 속도까지 가속. */
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
/* 부팅 시: HOME_MODE 0(센서 없음)이면 현재 위치를 A(0)로 간주. */
void xmotion_Init_State(void)
{
    xmotion_state = CONTROL_STOPPED;
    xmotion_error = CONTROL_OK;
    xmotion_referenced = (CONFIG_X_HOME_MODE == 0U);
    xmotion_seek = false;
}
/* 절대 위치를 알고 있는가. */
bool xmotion_Get_Referenced(void) { return xmotion_referenced; }
/* 이동 시작. 이미 목표 센서 위거나 목표 위치면 즉시 완료.
 * 원점이 잡혀 있으면 목표 이동(가감속), 아니면 센서 탐색 이동. */
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
/* 매 루프: 완료·고장 판정과 가감속.
 * 완료: 목표 센서 감지 / 목표 펄스 도달(인터럽트가 이미 정지)
 * 고장: 양 끝 센서 동시(SENSOR), 시간 초과(TIMEOUT), 출발 센서가 안 풀림(SENSOR),
 *       탐색 중 전체 거리 + 여유를 넘음(SENSOR) */
void xmotion_Set_Update(void)
{
    uint32_t now = timer_Get_Tick_Ms();
    /* 위치 보정은 정지 중(또는 아래 도착 시)에만: 센서를 떠나는 동안에는 센서가 아직
     * 감지 상태라 보정하면 카운트가 센서 위치에 묶여 버린다. */
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
            /* 목표 펄스 도달: TIM1 인터럽트가 이미 타이머를 멈춤 */
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
            /* 전체 거리보다 더 갔는데 센서를 못 찾음 */
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
/* 정지 (위치 유지). */
void xmotion_Set_Stop(void)
{
    position_Set_Stop();
    stepper_Set_Stop();
    xmotion_state = CONTROL_STOPPED;
}
/* X 이동 상태. */
control_state_t xmotion_Get_State(void)
{
    return xmotion_state;
}
/* 마지막 고장 코드. */
control_error_t xmotion_Get_Error(void)
{
    return xmotion_error;
}
