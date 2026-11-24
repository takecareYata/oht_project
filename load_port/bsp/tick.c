/**
 * @file    tick.c
 * @brief   [L1 BSP] SysTick 1ms 시간 기준
 * @details SysTick 클럭 = HCLK(96MHz), 1ms마다 인터럽트로 카운터 증가.
 *          32bit 카운터는 약 49.7일 후 0으로 돌아가며,
 *          경과 시간은 (현재 - 시작) 부호 없는 뺄셈으로 계산하므로 문제없다.
 */
#include "device_driver.h"
#include "tick.h"

#define TICK_HZ             (1000U)
#define TICK_RELOAD         (((uint32_t)HCLK / TICK_HZ) - 1U)

static volatile uint32_t tickMs = 0U;

void Tick_Init_Systick(void)
{
    tickMs = 0U;

    SysTick->CTRL = 0U;
    SysTick->LOAD = TICK_RELOAD;
    SysTick->VAL  = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |   /* 프로세서 클럭(HCLK) */
                    SysTick_CTRL_TICKINT_Msk   |   /* 인터럽트 사용 */
                    SysTick_CTRL_ENABLE_Msk;
}

uint32_t Tick_Get_Ms(void)
{
    return tickMs;
}

/* 벡터 테이블 지정 함수명이라 명명규칙 예외 */
void SysTick_Handler(void)
{
    tickMs++;
}
