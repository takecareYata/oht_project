#include "control.h"

static volatile uint32_t timer_tick;

/* Cortex-M vector name is an intentional naming-rule exception. */
void SysTick_Handler(void)
{
    timer_tick++;
}

uint32_t timer_Get_Tick_Ms(void)
{
    return timer_tick;
}
