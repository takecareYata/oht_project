/* ============================================================================
 * timer.c — ms 시간 기준 (L2)
 *
 * 모든 시간 판정(디바운스, 시간 초과, 스텝 주기)은 이 틱을 쓴다.
 * ============================================================================ */
#include "control.h"

static volatile uint32_t timer_tick; /* 부팅 후 경과 ms (약 49.7일마다 0으로 돌아감) */

/* SysTick 인터럽트(1 ms). 함수 이름은 Cortex-M 벡터 이름이라 명명 규칙 예외. */
void SysTick_Handler(void)
{
    timer_tick++;
}

/* 현재 ms 틱. 경과 시간은 항상 (uint32_t)(now - start)로 계산해 랩어라운드에 안전하게. */
uint32_t timer_Get_Tick_Ms(void)
{
    return timer_tick;
}
