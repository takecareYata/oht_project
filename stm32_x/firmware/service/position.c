/* ============================================================================
 * position.c — 진단용 위치 표시 (L3)
 *
 * 현재 위치를 0~1000(‰)으로 계산해 응답에 싣는다. 0 = A·Z 상단, 1000 = B·Z 하단.
 * 진단·GUI 표시 전용: 모션 완료나 인터록 판단에는 이 값을 쓰지 않는다.
 * ============================================================================ */
#include "control.h"
/* position_value: 0~1000, position_source(pos_src): 2 = 센서 확인, 1 = 추정, 0 = 모름 */
static int32_t position_value;
static uint8_t position_source, position_target;
static bool position_moving;
static uint32_t position_last, position_fraction; /* 직전 계산 시각 / 나눗셈 나머지 누적 */
/* 시간 기준 (ms). */
static uint32_t position_Get_Counter(void)
{
    return timer_Get_Tick_Ms();
}
/* 부팅 시: 위치 모름(pos_src 0)으로 시작. */
void position_Init_State(void)
{
    position_value = 0;
    position_source = 0U;
    position_moving = false;
    position_fraction = 0U;
    position_last = position_Get_Counter();
}
#if BOARD_ID == 2
/* [보드2] X: 스텝 카운터가 곧 위치 (0 = A, CONFIG_X_TRAVEL_STEPS = B)를 0~1000으로 환산.
 * pos_src: 끝 센서 위 = 2, 원점 잡힌 뒤 스텝 기준 = 1, 모름·센서 모순 = 0. */
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
/* [보드1] Z: 이동 시간으로 위치를 추정한다 (CONFIG_Z_TRAVEL_MS = 전체 스트로크 시간).
 * 부팅 후 끝 센서를 한 번 본 뒤부터 추정을 시작하고, 끝 센서에서 0/1000으로 보정. */
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
            /* 경과 ms × 1000 / 전체 시간. 나머지는 position_fraction에 모아 오차 누적 방지.
             * (이 블록은 보드1 전용이라 아래 #if는 항상 CONFIG_Z_TRAVEL_MS를 고른다) */
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
/* zlift가 이동 시작 시 호출: 방향 기록, 시간 적분 시작. */
void position_Set_Motion(uint8_t target)
{
    position_Set_Update();
    position_target = target;
    position_moving = true;
    position_fraction = 0U;
}
/* zlift가 정지·도착·고장 시 호출: 시간 적분 중지. */
void position_Set_Stop(void)
{
    position_Set_Update();
    position_moving = false;
}
/* 위치 0~1000 (응답 byte12~15). */
int32_t position_Get_Value(void) { return position_value; }
/* 위치 출처 pos_src (응답 byte16). */
uint8_t position_Get_Source(void) { return position_source; }
