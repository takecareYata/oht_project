/* ============================================================================
 * bsp.c — 보드 지원 계층 (L1)
 *
 * 핀맵, GPIO 모드, 타이머(TIM1), SysTick, 워치독, LED를 담당한다.
 * 레지스터를 직접 다루는 코드는 대부분 이 파일과 spislave.c에 있다.
 * 핀 배치를 바꿀 때는 이 파일과 README 핀맵 표를 함께 고친다.
 * ============================================================================ */
#include "stm32f411xe.h"
#include "control.h"
/* GPIO 핀 하나의 모드(입력/출력/AF/아날로그)와 AF 번호 설정. */
static void bsp_Set_Mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode, uint32_t af)
{
    port->MODER = (port->MODER & ~(3UL << (pin * 2U))) | (mode << (pin * 2U));
    port->AFR[pin / 8U] =
        (port->AFR[pin / 8U] & ~(15UL << ((pin % 8U) * 4U))) | (af << ((pin % 8U) * 4U));
}
/* 부팅 시 1회: 클록 enable, 핀맵 설정(출력은 모두 Low = 모터 off로 시작),
 * 보드별 드라이버 핀, SysTick 1 ms, 워치독(IWDG) 시작. */
void bsp_Init_Board(void)
{
    uint32_t pin;
    RCC->AHB1ENR |=
        RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_DMA1EN;
    RCC->APB1ENR |=
        RCC_APB1ENR_SPI2EN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    __DSB();
    /* 공통: PA0/1/4/6, PB0/1을 출력 Low로 고정 (보드1은 PA0/1 = L298N IN1/IN2, PB0 = ENA).
     * PA2/3은 Nucleo ST-Link 가상 COM 포트라 사용하지 않는다. */
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
    /* [보드1] Z L298N: PA0 IN1, PA1 IN2, PB0 ENA. 위에서 이미 출력 Low로 설정됨.
     * 온/오프 제어만 하고 PWM은 쓰지 않는다. */
    /* [보드1] 집게 ULN2003 IN1~IN4 = PA8(D7), PA9(D8), PA10(D2), PB4(D5).
     * 푸시풀 출력, 부팅 시 모두 Low(코일 off). PB4는 리셋 기본값인 JTAG 기능에서 빠지지만
     * SWD(PA13/PA14)에는 영향 없음. */
    GPIOA->BSRR = 7UL << (8U + 16U);
    GPIOB->BSRR = 1UL << (4U + 16U);
    bsp_Set_Mode(GPIOA, 8U, 1U, 0U);
    bsp_Set_Mode(GPIOA, 9U, 1U, 0U);
    bsp_Set_Mode(GPIOA, 10U, 1U, 0U);
    bsp_Set_Mode(GPIOB, 4U, 1U, 0U);
    /* [보드1] Nucleo 파란 버튼 B1 = PC13 (누르면 Low). Z 백도어 "단계 완료" 버튼. */
    bsp_Set_Mode(GPIOC, 13U, 0U, 0U);
    GPIOC->PUPDR = (GPIOC->PUPDR & ~(3UL << 26U)) | (1UL << 26U);
#else
    /* [보드2] TB6600: PA8 = TIM1_CH1(AF1) → PUL, PA9 → DIR, PC7 → ENA.
     * TIM1(APB2 16 MHz)을 1 MHz로 분주. 이동 전에는 CH1을 강제 Low로 두어
     * PUL이 떠 있지 않게 한다. ENA는 High(드라이버 off)로 시작. */
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
    TIM1->BDTR = TIM_BDTR_MOE; /* 고급 타이머라 MOE가 없으면 출력이 나가지 않음 */
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0U;
    NVIC_SetPriority(TIM1_UP_TIM10_IRQn, 2U);
    NVIC_EnableIRQ(TIM1_UP_TIM10_IRQn);
#endif
    (void)SysTick_Config(CONFIG_CLOCK_HZ / 1000U);
    NVIC_SetPriority(SysTick_IRQn, 3U);
    /* 워치독(IWDG) 약 1초 (LSI 편차 있음). 메인 루프 한 바퀴가 끝날 때만 갱신한다. */
    IWDG->KR = 0xCCCCU;
    IWDG->KR = 0x5555U;
    IWDG->PR = 4U;
    IWDG->RLR = 499U;
    while (IWDG->SR != 0U)
    {
    }
    IWDG->KR = 0xAAAAU;
}
/* 센서 입력 PC0~PC2 원시값 (bit0~bit2). 극성 변환은 sensor.c에서. */
uint8_t bsp_Get_Sensors(void)
{
    return (uint8_t)(GPIOC->IDR & 7U);
}
/* B1 버튼(PC13)이 눌렸으면 true. */
bool bsp_Get_Button(void)
{
    return (GPIOC->IDR & (1UL << 13U)) == 0U;
}
/* [보드2] TB6600 DIR(PA9) 출력. */
void bsp_Set_X_Direction(bool forward)
{
    GPIOA->BSRR = forward ? (1UL << 9U) : (1UL << (9U + 16U));
}
/* [보드2] TB6600 ENA(PC7) 출력. */
void bsp_Set_X_Enable(bool enabled)
{
    /* TB6600 ENA는 High = 해제(모터 풀림), Low = 구동. 미연결이면 항상 구동 상태. */
    GPIOC->BSRR = enabled ? (1UL << (7U + 16U)) : (1UL << 7U);
}
/* [보드1] Z L298N 온/오프. ENA(PB0)를 먼저 끄고 → 방향 설정 → ENA 켬.
 * 정지 = ENA Low + IN1 = IN2 = Low (모터 자유 회전, 기계적 제동 없음). */
void bsp_Set_Z_Output(bool down, bool run)
{
#if BOARD_ID == 1
    GPIOB->BSRR = 1UL << (0U + 16U);
    if (!run)
    {
        GPIOA->BSRR = 3UL << 16U;
    }
    else
    {
        uint32_t value = down ? 2U : 1U;
        GPIOA->BSRR = ((3UL & ~value) << 16U) | value;
        GPIOB->BSRR = 1UL << 0U;
    }
#else
    (void)down;
    (void)run;
#endif
}
/* [보드1] 집게 코일 출력. coils bit0~bit3 = ULN2003 IN1~IN4, 1 = 코일 on.
 * 보드2에서는 같은 핀이 X축용이므로 아무것도 하지 않는다. */
void bsp_Set_Grip_Phase(uint8_t coils)
{
#if BOARD_ID == 1
    GPIOA->BSRR = (((coils & 1U) != 0U) ? (1UL << 8U) : (1UL << (8U + 16U))) |
                  (((coils & 2U) != 0U) ? (1UL << 9U) : (1UL << (9U + 16U))) |
                  (((coils & 4U) != 0U) ? (1UL << 10U) : (1UL << (10U + 16U)));
    GPIOB->BSRR = ((coils & 8U) != 0U) ? (1UL << 4U) : (1UL << (4U + 16U));
#else
    (void)coils;
#endif
}
/* [보드2] STEP 펄스 주파수 설정. 정지 상태면 타이머를 시작한다. */
void bsp_Set_Step_Timer(uint16_t hz)
{
    /* TIM1은 16비트, 1 MHz 기준: ARR ≤ 65535 → 최소 16 Hz. */
    if ((hz >= 16U) && (hz <= CONFIG_X_MAX_PULSE_HZ))
    {
        /* PWM 모드 2: 주기 앞 절반 Low, 뒤 절반 High (듀티 50%, 20 kHz에서도 펄스 폭 ≥ 25 µs).
         * 업데이트 이벤트(주기 끝) = 하강 엣지 = 펄스 1개 완료 → 인터럽트에서 카운트.
         * ARR/CCR1은 프리로드되므로 주파수 변경은 주기 경계에서 적용된다. */
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
/* [보드2] STEP 펄스 즉시 정지. */
void bsp_Set_Step_Timer_Stop(void)
{
    TIM1->DIER = 0U;
    TIM1->CR1 = 0U;
    /* PUL을 즉시 Low로 강제 (OC1M = force inactive). */
    TIM1->CCMR1 = 4UL << 4U;
    TIM1->SR = 0U;
    NVIC_ClearPendingIRQ(TIM1_UP_TIM10_IRQn);
}
/* [보드2] TIM1 업데이트 인터럽트: STEP 펄스 1개 완료마다 호출. */
void TIM1_UP_TIM10_IRQHandler(void)
{
    if ((TIM1->SR & TIM_SR_UIF) != 0U)
    {
        TIM1->SR = ~TIM_SR_UIF;
        stepper_Set_Tick();
    }
}
/* 상태 LED(PA5) 출력. */
void bsp_Set_Led(bool on)
{
    GPIOA->BSRR = on ? (1UL << 5U) : (1UL << 21U);
}
/* 워치독 갱신(kick). 1초 안에 다시 부르지 않으면 리셋. */
void bsp_Set_Watchdog(void)
{
    IWDG->KR = 0xAAAAU;
}
