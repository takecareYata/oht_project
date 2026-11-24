/**
 * @file    exception.c
 * @brief   [L1 BSP] 미사용 예외/인터럽트 기본 처리 (1601.DMA_EX 프레임워크 기반)
 * @details crt0.s의 벡터 테이블에서 구현되지 않은 핸들러는 모두 여기로 온다.
 *          UART를 사용하지 않으므로 printf 출력 없이 정지한다.
 *          (디버거로 SCB->ICSR 하위 9비트를 보면 예외 번호 확인 가능)
 *          사용 중인 핸들러 : SysTick_Handler (tick.c), TIM4_IRQHandler (hcsr04.c)
 */
#include "device_driver.h"

void _Invalid_ISR(void)
{
    for (;;)
    {
    }
}
