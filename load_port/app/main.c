/**
 * @file    main.c
 * @brief   [L4 Application] Load Port(STM32③) 초기화 및 메인 루프
 * @details CSCI : OHT-Load Port 웨이퍼 이송 시스템
 *          CSC  : Load Port
 *          보드 : NUCLEO-F411RE, 레지스터 직접 제어 (1601.DMA_EX 프레임워크)
 *          RTOS 미사용, Heartbeat 제외, UART 미사용
 *
 *          계층 구조
 *            L4 Application : main.c
 *            L3 Service     : e84.c, loadport.c, system.c
 *            L2 Driver      : pio.c, hcsr04.c
 *            L1 BSP         : tick.c, clock.c, exception.c, runtime.c,
 *                             crt0.s, system_stm32f4xx.c, CMSIS 헤더
 */
#include "device_driver.h"
#include "tick.h"
#include "e84.h"
#include "loadport.h"
#include "system.h"

#define MAIN_CPACR_FPU_FULL     ((0x3UL << 20) | (0x3UL << 22))   /* CP10, CP11 Full access */

static void Main_Init_System(void);

/* crt0.s에서 호출하는 진입 함수 (프레임워크 지정 이름이라 명명규칙 예외) */
void Main(void)
{
    Main_Init_System();

    E84_Init_Pio();            /* E84 핀 설정 -> 출력 OFF -> HO_AVBL, ES ON */
    Loadport_Init_Sensor();    /* HC-SR04 + TIM4 입력캡처 시작, 첫 측정 */
    System_Init_Watchdog();    /* 추후 결정 : 현재 빈 함수 */

    for (;;)
    {
        Loadport_Run_Sensor();   /* 60ms 주기 초음파 측정·판정 */
        E84_Get_Input();         /* 1ms 주기 입력 샘플 + 디바운스 */
        E84_Run_Fsm();           /* E84 Passive FSM */
        System_Set_Watchdog();   /* 추후 결정 : 현재 빈 함수 */
    }
}

/* FPU 허용(hard-float ABI) -> 96MHz 클럭 -> 1ms SysTick */
static void Main_Init_System(void)
{
    SCB->CPACR |= MAIN_CPACR_FPU_FULL;
    Clock_Init();
    Tick_Init_Systick();
}
