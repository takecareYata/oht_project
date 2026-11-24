/**
 * @file    device_driver.h
 * @brief   [L1 BSP] 공통 헤더 (1601.DMA_EX 프레임워크 기반, Load Port용으로 정리)
 * @details 레지스터 정의(stm32f4xx.h), 클럭 설정값(option.h), 비트 매크로(macro.h)
 */
#ifndef DEVICE_DRIVER_H
#define DEVICE_DRIVER_H

#include "stm32f4xx.h"
#include "option.h"
#include "macro.h"
#include "malloc.h"

/* clock.c : HSI 16MHz -> PLL 96MHz (HCLK 96MHz, PCLK1 48MHz, TIMxCLK 96MHz) */
extern void Clock_Init(void);

#endif /* DEVICE_DRIVER_H */
