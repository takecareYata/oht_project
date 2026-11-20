#include "stm32f411xe.h"
#include "config.h"

extern const uint32_t g_pfnVectors[];
uint32_t SystemCoreClock = CONFIG_CLOCK_HZ;

/* Called by ST startup BEFORE .data/.bss initialization. Do not access RAM globals.
 * Hardware reset selects HSI 16 MHz; the project keeps that clock configuration.
 * This is project code, not ST's system_stm32f4xx.c template.
 */
void SystemInit(void)
{
    SCB->VTOR = (uint32_t)g_pfnVectors;
    __DSB();
    __ISB();
}

/* Newlib __libc_init_array calls this hook before walking .init_array.
 * No legacy .init functions are needed by this C-only firmware.
 */
void _init(void)
{
}

static void system_Set_Fault(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    __DSB();
    GPIOB->BSRR = 3UL << 16U;
    GPIOB->MODER = (GPIOB->MODER & ~15UL) | 5UL;
    NVIC_SystemReset();
}

/* Override ST weak core exception handlers to retain bridge inhibit/reset. */
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
