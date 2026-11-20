#include "stm32f411xe.h"
#include "control.h"
/* Reset starts on HSI 16 MHz; SystemInit keeps the reset clock configuration. */
static void bsp_Set_Mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode, uint32_t af)
{
    port->MODER = (port->MODER & ~(3UL << (pin * 2U))) | (mode << (pin * 2U));
    port->AFR[pin / 8U] =
        (port->AFR[pin / 8U] & ~(15UL << ((pin % 8U) * 4U))) | (af << ((pin % 8U) * 4U));
}
void bsp_Init_Board(void)
{
    uint32_t pin;
    RCC->AHB1ENR |=
        RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_DMA1EN;
    RCC->APB1ENR |=
        RCC_APB1ENR_TIM3EN | RCC_APB1ENR_TIM4EN | RCC_APB1ENR_SPI2EN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    __DSB();
    /* Board 1: PA0/1 Z L298N IN1/IN2. Board 2: TB6600 on PA8 PUL, PA9 DIR, PC7 ENA;
     * PA0/1/4/6 unused, held low. Avoid PA2/3 (Nucleo ST-Link VCP). */
    GPIOA->BSRR = 0x0053UL << 16U;
    GPIOB->BSRR = 3UL << 16U;
    bsp_Set_Mode(GPIOA, 0U, 1U, 0U);
    bsp_Set_Mode(GPIOA, 1U, 1U, 0U);
    bsp_Set_Mode(GPIOA, 4U, 1U, 0U);
    bsp_Set_Mode(GPIOA, 6U, 1U, 0U);
    bsp_Set_Mode(GPIOA, 5U, 1U, 0U);
    for (pin = 0U; pin < 2U; pin++)
    {
        bsp_Set_Mode(GPIOB, pin, 1U, 0U);
    }
    for (pin = 0U; pin < 3U; pin++)
    {
        bsp_Set_Mode(GPIOC, pin, 0U, 0U);
        GPIOC->PUPDR = (GPIOC->PUPDR & ~(3UL << (pin * 2U))) | (1UL << (pin * 2U));
    }
#if BOARD_ID == 1
    /* PB0 AF2 = TIM3_CH3, 20 kHz, 0..800 period ticks. */
    bsp_Set_Mode(GPIOB, 0U, 2U, 2U);
    TIM3->PSC = 0U;
    TIM3->ARR = 799U;
    TIM3->CCR3 = 0U;
    TIM3->CCMR2 = (6UL << 4U) | TIM_CCMR2_OC3PE;
    TIM3->CCER = TIM_CCER_CC3E;
    TIM3->EGR = TIM_EGR_UG;
    /* PB6 AF2 = TIM4_CH1, 1 us timer ticks, 20 ms servo frame. Initially no pulse. */
    bsp_Set_Mode(GPIOB, 6U, 2U, 2U);
    TIM4->PSC = 15U;
    TIM4->ARR = 19999U;
    TIM4->CCR1 = 0U;
    TIM4->CCMR1 = (6UL << 4U) | TIM_CCMR1_OC1PE;
    TIM4->CCER = TIM_CCER_CC1E;
    TIM4->EGR = TIM_EGR_UG;
    TIM4->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
#else
    /* TB6600: PA8 AF1 = TIM1_CH1 -> PUL, PA9 GPIO -> DIR, PC7 GPIO -> ENA.
     * TIM1 (APB2 = 16 MHz) runs at 1 MHz. CH1 is forced low until a move
     * starts, so PUL is driven low (not floating) at idle. ENA starts high
     * (driver off) until stepper_Init_Driver/Set_Run. PA0/1/4/6, PB0/1 unused. */
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    __DSB();
    GPIOA->BSRR = 1UL << (9U + 16U);
    bsp_Set_Mode(GPIOA, 9U, 1U, 0U);
    GPIOC->BSRR = 1UL << 7U;
    bsp_Set_Mode(GPIOC, 7U, 1U, 0U);
    bsp_Set_Mode(GPIOA, 8U, 2U, 1U);
    TIM1->PSC = 15U;
    TIM1->ARR = 19999U;
    TIM1->CCMR1 = 4UL << 4U;
    TIM1->CCER = TIM_CCER_CC1E;
    TIM1->BDTR = TIM_BDTR_MOE; /* advanced timer: outputs stay off without MOE */
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0U;
    NVIC_SetPriority(TIM1_UP_TIM10_IRQn, 2U);
    NVIC_EnableIRQ(TIM1_UP_TIM10_IRQn);
#endif
    (void)SysTick_Config(CONFIG_CLOCK_HZ / 1000U);
    NVIC_SetPriority(SysTick_IRQn, 3U);
    /* IWDG ~1 second at nominal LSI. Reload only after a full main-loop pass. */
    IWDG->KR = 0xCCCCU;
    IWDG->KR = 0x5555U;
    IWDG->PR = 4U;
    IWDG->RLR = 499U;
    while (IWDG->SR != 0U)
    {
    }
    IWDG->KR = 0xAAAAU;
}
uint8_t bsp_Get_Sensors(void)
{
    return (uint8_t)(GPIOC->IDR & 7U);
}
void bsp_Set_X_Direction(bool forward)
{
    GPIOA->BSRR = forward ? (1UL << 9U) : (1UL << (9U + 16U));
}
void bsp_Set_X_Enable(bool enabled)
{
    /* TB6600 ENA is active-high DISABLE: PC7 low = driver on, high = motor free.
     * With ENA left unconnected the driver is always on. */
    GPIOC->BSRR = enabled ? (1UL << (7U + 16U)) : (1UL << 7U);
}
void bsp_Set_Z_Output(bool down, uint16_t duty)
{
    TIM3->CCR3 = 0U;
    TIM3->EGR = TIM_EGR_UG;
    if (duty == 0U)
    {
        GPIOA->BSRR = 3UL << 16U;
    }
    else
    {
        uint32_t value = down ? 2U : 1U;
        GPIOA->BSRR = ((3UL & ~value) << 16U) | value;
        TIM3->CCR3 = ((uint32_t)duty * 800U) / 1000U;
    }
}
void bsp_Set_Servo_Pulse(uint16_t pulse)
{
    TIM4->CCR1 = pulse;
}
void bsp_Set_Step_Timer(uint16_t hz)
{
    /* TIM1 is 16-bit at 1 MHz: ARR <= 65535 -> minimum 16 Hz. */
    if ((hz >= 16U) && (hz <= CONFIG_X_MAX_PULSE_HZ))
    {
        /* PWM mode 2: PUL low for the first half of each period, high for the
         * second half. 50% duty gives >= 25 us pulse width at 20 kHz. The update
         * event (end of period) is the falling edge, i.e. one finished pulse.
         * ARR/CCR1 are preloaded, so rate changes apply at a period boundary. */
        uint32_t arr = (1000000UL / hz) - 1U;
        TIM1->CR1 |= TIM_CR1_ARPE;
        TIM1->ARR = arr;
        TIM1->CCR1 = (arr + 1U) / 2U;
        if ((TIM1->CR1 & TIM_CR1_CEN) == 0U)
        {
            TIM1->CNT = 0U;
            TIM1->CCMR1 = (7UL << 4U) | TIM_CCMR1_OC1PE;
            TIM1->CCER = TIM_CCER_CC1E;
            TIM1->BDTR = TIM_BDTR_MOE;
            TIM1->EGR = TIM_EGR_UG;
            TIM1->SR = 0U;
            TIM1->DIER = TIM_DIER_UIE;
            TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
        }
    }
}
void bsp_Set_Step_Timer_Stop(void)
{
    TIM1->DIER = 0U;
    TIM1->CR1 = 0U;
    /* Force PUL low immediately (OC1M = force inactive). */
    TIM1->CCMR1 = 4UL << 4U;
    TIM1->SR = 0U;
    NVIC_ClearPendingIRQ(TIM1_UP_TIM10_IRQn);
}
void TIM1_UP_TIM10_IRQHandler(void)
{
    if ((TIM1->SR & TIM_SR_UIF) != 0U)
    {
        TIM1->SR = ~TIM_SR_UIF;
        stepper_Set_Tick();
    }
}
void bsp_Set_Led(bool on)
{
    GPIOA->BSRR = on ? (1UL << 5U) : (1UL << 21U);
}
void bsp_Set_Watchdog(void)
{
    IWDG->KR = 0xAAAAU;
}
