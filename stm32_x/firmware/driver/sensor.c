/* ============================================================================
 * sensor.c — 센서 입력 디바운스 (L2)
 *
 * 입력 3개: PC0, PC1, PC2 (내부 풀업, Low = 감지).
 *   보드1: PC0 = Z 상단, PC1 = Z 하단, PC2 = 집게 접촉 스위치
 *   보드2: PC0 = A(Home), PC1 = B(Load Port), PC2 미사용
 * Z 백도어 빌드에서는 가상 센서와 B1 버튼도 여기서 처리한다.
 * ============================================================================ */
#include "control.h"
static uint8_t sensor_candidate;  /* 최근 원시값 (극성 변환 후, 1 = 감지) */
static uint8_t sensor_stable;     /* 디바운스가 끝난 확정값 */
static uint32_t sensor_since[3];  /* 입력별로 값이 마지막으로 바뀐 시각 */
static uint32_t sensor_started;   /* 초기화 시각 (준비 판정용) */
/* Z 백도어 가상 센서: 시간/버튼으로 "감지"를 대신 켠 비트. 실제 센서와 OR로 합친다. */
static uint8_t sensor_virtual;
#if CONFIG_Z_TIMED != 0U
/* B1 버튼: 디바운스된 상태 + "눌림 이벤트". 이벤트는 루프 한 바퀴 동안만 유효해서
 * 아무것도 움직이지 않을 때 누른 것은 저장되지 않고 버려진다. */
static bool sensor_button_candidate, sensor_button_stable, sensor_button_event;
static uint32_t sensor_button_since;
#endif
/* 부팅 시: 모든 입력 "미감지"로 시작. */
void sensor_Init_State(void)
{
    uint8_t i;
    sensor_candidate = 0U;
    sensor_stable = 0U;
    sensor_virtual = 0U;
    sensor_started = timer_Get_Tick_Ms();
#if CONFIG_Z_TIMED != 0U
    sensor_button_candidate = false;
    sensor_button_stable = false;
    sensor_button_event = false;
    sensor_button_since = sensor_started;
#endif
    for (i = 0U; i < 3U; i++)
    {
        sensor_since[i] = sensor_started;
    }
}
/* 매 루프: 입력별 디바운스. 같은 값이 CONFIG_SENSOR_DEBOUNCE_MS(10 ms) 유지되면 확정. */
void sensor_Set_Update(void)
{
    uint8_t raw = bsp_Get_Sensors();
    uint8_t i;
    uint32_t now = timer_Get_Tick_Ms();
    /* Low = 감지 → 내부적으로 1 = 감지가 되도록 뒤집는다 */
    if (CONFIG_SENSOR_ACTIVE_LOW != 0U)
    {
        raw = (uint8_t)(~raw) & 7U;
    }
    for (i = 0U; i < 3U; i++)
    {
        uint8_t mask = (uint8_t)(1U << i);
        if ((raw & mask) != (sensor_candidate & mask))
        {
            sensor_candidate = (uint8_t)((sensor_candidate & ~mask) | (raw & mask));
            sensor_since[i] = now;
        }
        else if ((uint32_t)(now - sensor_since[i]) >= CONFIG_SENSOR_DEBOUNCE_MS)
        {
            sensor_stable = (uint8_t)((sensor_stable & ~mask) | (raw & mask));
        }
        else
        { /* 아직 흔들리는 중: 대기 */
        }
    }
#if CONFIG_Z_TIMED != 0U
    /* B1 버튼 디바운스 (CONFIG_BUTTON_DEBOUNCE_MS), 눌리는 순간에만 이벤트 1회 */
    {
        bool pressed = bsp_Get_Button();
        sensor_button_event = false;
        if (pressed != sensor_button_candidate)
        {
            sensor_button_candidate = pressed;
            sensor_button_since = now;
        }
        else if ((pressed != sensor_button_stable) &&
                 ((uint32_t)(now - sensor_button_since) >= CONFIG_BUTTON_DEBOUNCE_MS))
        {
            sensor_button_stable = pressed;
            sensor_button_event = pressed;
        }
        else
        {
        }
    }
#endif
}
/* 입력 index(0 = PC0, 1 = PC1, 2 = PC2)가 감지 상태인가. 실제 센서 OR 가상 센서. */
bool sensor_Get_State(uint8_t index)
{
    return (index < 3U) && (((sensor_stable | sensor_virtual) & (1U << index)) != 0U);
}
/* Z 백도어: 가상 센서 비트 켜기/끄기. */
void sensor_Set_Virtual(uint8_t index, bool active)
{
    if (index < 3U)
    {
        uint8_t mask = (uint8_t)(1U << index);
        sensor_virtual = active ? (uint8_t)(sensor_virtual | mask) : (uint8_t)(sensor_virtual & ~mask);
    }
}
/* 부팅 후 디바운스 2회분(20 ms)이 지나 센서 값을 믿을 수 있는가. */
bool sensor_Get_Ready(void)
{
    return (uint32_t)(timer_Get_Tick_Ms() - sensor_started) >= (2U * CONFIG_SENSOR_DEBOUNCE_MS);
}
/* 이번 루프에 B1이 새로 눌렸는가. 백도어 빌드가 아니면 항상 false. */
bool sensor_Get_Button_Event(void)
{
#if CONFIG_Z_TIMED != 0U
    return sensor_button_event;
#else
    return false;
#endif
}
/* 응답 프레임 sensors 바이트용 (bit0~2, 가상 센서 포함). */
uint8_t sensor_Get_Bits(void)
{
    return (uint8_t)(sensor_stable | sensor_virtual);
}
