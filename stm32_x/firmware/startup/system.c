/* ============================================================================
 * system.c — 시작 코드 보조
 *
 * 부팅 순서: Reset_Handler(ST startup .s) → SystemInit → .data 복사/.bss 0 초기화
 *           → __libc_init_array → main
 * 치명적 예외(NMI/HardFault 등)가 나면 모터 출력을 끄고 리셋한다.
 * ============================================================================ */
#include "stm32f411xe.h"
#include "config.h"

extern const uint32_t g_pfnVectors[];
uint32_t SystemCoreClock = CONFIG_CLOCK_HZ; /* CMSIS 표준 변수: 코어 클록 Hz */

/* ST startup이 .data/.bss 초기화 "전에" 호출한다. 그래서 RAM 전역 변수에 접근하면 안 된다.
 * 리셋 기본 클록(HSI 16 MHz)을 그대로 쓰고, 벡터 테이블 주소만 설정한다.
 * (ST의 system_stm32f4xx.c 템플릿이 아닌 프로젝트 코드) */
void SystemInit(void)
{
    SCB->VTOR = (uint32_t)g_pfnVectors;
    __DSB();
    __ISB();
}

/* Newlib의 __libc_init_array가 .init_array 실행 전에 부르는 훅.
 * C 전용 펌웨어라 할 일이 없다. */
void _init(void)
{
}

/* 치명적 예외 공통 처리: Z L298N ENA(PB0/PB1)를 Low로 내려 모터를 끈 뒤 리셋. */
static void system_Set_Fault(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    __DSB();
    GPIOB->BSRR = 3UL << 16U;
    GPIOB->MODER = (GPIOB->MODER & ~15UL) | 5UL;
    NVIC_SystemReset();
}

/* ST의 weak 예외 핸들러를 대체: 예외 발생 시 모터 출력 차단 후 리셋한다. */
void NMI_Handler(void)
{
    system_Set_Fault();
}
void HardFault_Handler(void)
{
    system_Set_Fault();
}
void MemManage_Handler(void)
{
    system_Set_Fault();
}
void BusFault_Handler(void)
{
    system_Set_Fault();
}
void UsageFault_Handler(void)
{
    system_Set_Fault();
}
