/**
 * @file    pio.c
 * @brief   [L2 Driver] E84 PIO GPIO 입출력 드라이버 (레지스터 직접 제어)
 * @details 극성 : Active-High (ON = 3.3V, OFF = 0V)
 *          입력 : PC0~PC4 Input + Pull-down (선이 빠지면 OFF로 읽힘)
 *          출력 : PC5~PC9 Output Push-Pull, Low speed, 초기값 LOW
 */
#include "device_driver.h"
#include "pio.h"

#define PIO_IN_PIN_POS      (0U)        /* 입력 시작 핀 : PC0 */
#define PIO_OUT_PIN_POS     (5U)        /* 출력 시작 핀 : PC5 */
#define PIO_PIN_COUNT       (5U)
#define PIO_IN_MASK         (0x1FU)
#define PIO_BSRR_RESET_POS  (16U)

#define PIO_MODE_INPUT      (0x0U)
#define PIO_MODE_OUTPUT     (0x1U)
#define PIO_PUPD_NONE       (0x0U)
#define PIO_PUPD_DOWN       (0x2U)
#define PIO_FIELD_MASK      (0x3U)

static uint8_t pioOutState = 0U;

/* GPIOC 클럭 ON, PC0~PC4 입력(풀다운), PC5~PC9 출력(LOW) */
void Pio_Init_Gpio(void)
{
    uint32_t pinNum;
    uint32_t shift;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    /* 출력 모드로 바꾸기 전에 출력값을 먼저 LOW로 */
    GPIOC->BSRR = ((uint32_t)PIO_OUT_ALL << (PIO_OUT_PIN_POS + PIO_BSRR_RESET_POS));

    for (pinNum = 0U; pinNum < PIO_PIN_COUNT; pinNum++)
    {
        /* 입력 핀 */
        shift = (PIO_IN_PIN_POS + pinNum) * 2U;
        GPIOC->MODER   = (GPIOC->MODER   & ~(PIO_FIELD_MASK << shift)) | (PIO_MODE_INPUT << shift);
        GPIOC->PUPDR   = (GPIOC->PUPDR   & ~(PIO_FIELD_MASK << shift)) | (PIO_PUPD_DOWN  << shift);

        /* 출력 핀 */
        shift = (PIO_OUT_PIN_POS + pinNum) * 2U;
        GPIOC->MODER   = (GPIOC->MODER   & ~(PIO_FIELD_MASK << shift)) | (PIO_MODE_OUTPUT << shift);
        GPIOC->PUPDR   = (GPIOC->PUPDR   & ~(PIO_FIELD_MASK << shift)) | (PIO_PUPD_NONE   << shift);
        GPIOC->OSPEEDR = (GPIOC->OSPEEDR & ~(PIO_FIELD_MASK << shift));
        GPIOC->OTYPER  = (GPIOC->OTYPER  & ~(1UL << (PIO_OUT_PIN_POS + pinNum)));
    }

    pioOutState = 0U;
}

/* 입력 5핀을 읽어 비트마스크로 반환 (bit = 1 : ON) */
uint8_t Pio_Get_Input(void)
{
    return (uint8_t)((GPIOC->IDR >> PIO_IN_PIN_POS) & PIO_IN_MASK);
}

/* outMask에 포함된 출력 핀을 ON/OFF (BSRR 사용 : 다른 핀에 영향 없음) */
void Pio_Set_Output(uint8_t outMask, bool on)
{
    uint32_t pinBits = ((uint32_t)outMask & PIO_OUT_ALL) << PIO_OUT_PIN_POS;

    if (on == true)
    {
        GPIOC->BSRR = pinBits;
        pioOutState = (uint8_t)(pioOutState | outMask);
    }
    else
    {
        GPIOC->BSRR = pinBits << PIO_BSRR_RESET_POS;
        pioOutState = (uint8_t)(pioOutState & (uint8_t)(~outMask));
    }
}

/* 현재 출력 상태 비트마스크 반환 (디버깅용) */
uint8_t Pio_Get_Output(void)
{
    return pioOutState;
}
