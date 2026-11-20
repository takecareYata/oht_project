#include "control.h"
static uint8_t sensor_candidate;
static uint8_t sensor_stable;
static uint32_t sensor_since[3];
static uint32_t sensor_started;
void sensor_Init_State(void)
{
    uint8_t i;
    sensor_candidate = 0U;
    sensor_stable = 0U;
    sensor_started = timer_Get_Tick_Ms();
    for (i = 0U; i < 3U; i++)
    {
        sensor_since[i] = sensor_started;
    }
}
void sensor_Set_Update(void)
{
    uint8_t raw = bsp_Get_Sensors();
    uint8_t i;
    uint32_t now = timer_Get_Tick_Ms();
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
        { /* Await stable input. */
        }
    }
}
bool sensor_Get_State(uint8_t index)
{
    return (index < 3U) && ((sensor_stable & (1U << index)) != 0U);
}
bool sensor_Get_Ready(void)
{
    return (uint32_t)(timer_Get_Tick_Ms() - sensor_started) >= (2U * CONFIG_SENSOR_DEBOUNCE_MS);
}
uint8_t sensor_Get_Bits(void)
{
    return sensor_stable;
}
