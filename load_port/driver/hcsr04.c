/**
 * @file    hcsr04.c
 * @brief   [L2 Driver] HC-SR04 초음파 센서 드라이버 (레지스터 직접 제어)
 * @details 1) Hcsr04_Set_Trigger() : TRIG에 10us 펄스 출력 (측정 시작)
 *          2) TIM4_IRQHandler : ECHO 상승/하강 엣지 시각 캡처
 *          3) Hcsr04_Get_Distance() : 측정 완료 시 거리(mm) 반환
 *          TIM4 : TIMxCLK 96MHz / (PSC+1 = 96) = 1MHz, ARR 0xFFFF,
 *                 CH1 입력캡처(TI1), CC1P=1 + CC1NP=1 -> 양 엣지 캡처
 */
#include <stddef.h>
#include "device_driver.h"
#include "tick.h"
#include "hcsr04.h"

#define HCSR04_TRIG_PIN         (8U)        /* PA8 */
#define HCSR04_ECHO_PIN         (6U)        /* PB6 */
#define HCSR04_ECHO_AF          (2U)        /* AF2 = TIM4 */
#define HCSR04_BSRR_RESET_POS   (16U)
#define HCSR04_FIELD_MASK       (0x3U)
#define HCSR04_AF_MASK          (0xFU)
#define HCSR04_MODE_OUTPUT      (0x1U)
#define HCSR04_MODE_AF          (0x2U)

#define HCSR04_TIM_HZ           (1000000U)  /* 1 tick = 1us */
#define HCSR04_TIM_PSC          (((uint32_t)TIMXCLK / HCSR04_TIM_HZ) - 1U)
#define HCSR04_TIM_ARR          (0xFFFFU)

#define HCSR04_TRIG_US          (10U)       /* 트리거 펄스 폭 */
#define HCSR04_ECHO_TIMEOUT_MS  (40U)       /* 무반사 시 ECHO 최대 약 38ms */
#define HCSR04_MAX_ECHO_US      (30000U)    /* 약 5m 초과는 무반사로 취급 */
#define HCSR04_SOUND_NUM        (343U)      /* 거리[mm] = echo[us] * 343 / 2000 */
#define HCSR04_SOUND_DEN        (2000U)

typedef enum
{
    HCSR04_STEP_IDLE = 0,
    HCSR04_STEP_WAIT_RISE,
    HCSR04_STEP_WAIT_FALL,
    HCSR04_STEP_DONE
} hcsr04Step;

/* ISR과 공유하는 변수 */
static volatile hcsr04Step hcsr04StepState = HCSR04_STEP_IDLE;
static volatile uint16_t   hcsr04RiseCount = 0U;
static volatile uint16_t   hcsr04EchoUs    = 0U;

static uint32_t hcsr04TrigTick = 0U;

void Hcsr04_Init_Sensor(void)
{
    uint32_t shift;

    RCC->AHB1ENR |= (RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN);
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;

    /* PA8 : TRIG 출력, 초기 LOW */
    GPIOA->BSRR  = (1UL << (HCSR04_TRIG_PIN + HCSR04_BSRR_RESET_POS));
    shift        = HCSR04_TRIG_PIN * 2U;
    GPIOA->MODER = (GPIOA->MODER & ~(HCSR04_FIELD_MASK << shift)) | (HCSR04_MODE_OUTPUT << shift);
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(HCSR04_FIELD_MASK << shift));
    GPIOA->OTYPER &= ~(1UL << HCSR04_TRIG_PIN);

    /* PB6 : TIM4_CH1 (AF2), 풀업/풀다운 없음 (5V 입력은 FT핀, 분압 권장) */
    shift          = HCSR04_ECHO_PIN * 2U;
    GPIOB->MODER   = (GPIOB->MODER & ~(HCSR04_FIELD_MASK << shift)) | (HCSR04_MODE_AF << shift);
    GPIOB->PUPDR   = (GPIOB->PUPDR & ~(HCSR04_FIELD_MASK << shift));
    shift          = HCSR04_ECHO_PIN * 4U;
    GPIOB->AFR[0]  = (GPIOB->AFR[0] & ~(HCSR04_AF_MASK << shift)) | (HCSR04_ECHO_AF << shift);

    /* TIM4 : 1MHz 프리런 카운터 + CH1 양 엣지 입력캡처 */
    TIM4->CR1   = 0U;
    TIM4->PSC   = HCSR04_TIM_PSC;
    TIM4->ARR   = HCSR04_TIM_ARR;
    TIM4->CCER  = 0U;                                       /* 설정 전 캡처 비활성 */
    TIM4->CCMR1 = (TIM4->CCMR1 & ~(TIM_CCMR1_CC1S | TIM_CCMR1_IC1PSC | TIM_CCMR1_IC1F))
                | TIM_CCMR1_CC1S_0;                         /* CC1S = 01 : IC1 <- TI1 */
    TIM4->CCER  = TIM_CCER_CC1P | TIM_CCER_CC1NP | TIM_CCER_CC1E;
    TIM4->EGR   = TIM_EGR_UG;                               /* PSC 즉시 반영 */
    TIM4->SR    = 0U;
    TIM4->DIER  = TIM_DIER_CC1IE;

    hcsr04StepState = HCSR04_STEP_IDLE;

    NVIC_ClearPendingIRQ(TIM4_IRQn);
    NVIC_EnableIRQ(TIM4_IRQn);

    TIM4->CR1 = TIM_CR1_CEN;
}

/* 측정 시작 : TRIG 10us 펄스 (TIM4 카운터로 대기) */
void Hcsr04_Set_Trigger(void)
{
    uint16_t startCount;
    uint16_t nowCount;

    hcsr04StepState = HCSR04_STEP_WAIT_RISE;
    hcsr04TrigTick  = Tick_Get_Ms();

    GPIOA->BSRR = (1UL << HCSR04_TRIG_PIN);

    startCount = (uint16_t)TIM4->CNT;
    nowCount   = startCount;
    while ((uint16_t)(nowCount - startCount) < HCSR04_TRIG_US)
    {
        nowCount = (uint16_t)TIM4->CNT;
    }

    GPIOA->BSRR = (1UL << (HCSR04_TRIG_PIN + HCSR04_BSRR_RESET_POS));
}

/* 측정 결과 조회 : 결과는 한 번 읽으면 소멸 */
hcsr04Result Hcsr04_Get_Distance(uint16_t *pDistanceMm)
{
    hcsr04Result result = HCSR04_RESULT_NONE;
    uint32_t     echoUs;

    if (pDistanceMm != NULL)
    {
        if (hcsr04StepState == HCSR04_STEP_DONE)
        {
            echoUs          = (uint32_t)hcsr04EchoUs;
            hcsr04StepState = HCSR04_STEP_IDLE;

            if (echoUs >= HCSR04_MAX_ECHO_US)
            {
                result = HCSR04_RESULT_NO_ECHO;
            }
            else
            {
                *pDistanceMm = (uint16_t)((echoUs * HCSR04_SOUND_NUM) / HCSR04_SOUND_DEN);
                result       = HCSR04_RESULT_OK;
            }
        }
        else if ((hcsr04StepState == HCSR04_STEP_WAIT_RISE) ||
                 (hcsr04StepState == HCSR04_STEP_WAIT_FALL))
        {
            if ((Tick_Get_Ms() - hcsr04TrigTick) >= HCSR04_ECHO_TIMEOUT_MS)
            {
                hcsr04StepState = HCSR04_STEP_IDLE;
                result          = HCSR04_RESULT_NO_ECHO;
            }
        }
        else
        {
            /* IDLE : 진행 중인 측정 없음 */
        }
    }

    return result;
}

/* 벡터 테이블 지정 함수명이라 명명규칙 예외 */
void TIM4_IRQHandler(void)
{
    uint16_t captureCount;

    if ((TIM4->SR & TIM_SR_CC1IF) != 0U)
    {
        captureCount = (uint16_t)TIM4->CCR1;        /* CCR1 읽기 -> CC1IF 자동 클리어 */
        TIM4->SR     = ~(uint32_t)TIM_SR_CC1OF;     /* 오버캡처 플래그 클리어 */

        if ((GPIOB->IDR & (1UL << HCSR04_ECHO_PIN)) != 0U)
        {
            /* 상승 엣지 */
            if (hcsr04StepState == HCSR04_STEP_WAIT_RISE)
            {
                hcsr04RiseCount = captureCount;
                hcsr04StepState = HCSR04_STEP_WAIT_FALL;
            }
        }
        else
        {
            /* 하강 엣지 : 16bit 카운터 오버플로는 부호 없는 뺄셈으로 처리 */
            if (hcsr04StepState == HCSR04_STEP_WAIT_FALL)
            {
                hcsr04EchoUs    = (uint16_t)(captureCount - hcsr04RiseCount);
                hcsr04StepState = HCSR04_STEP_DONE;
            }
        }
    }

    NVIC_ClearPendingIRQ(TIM4_IRQn);
}
