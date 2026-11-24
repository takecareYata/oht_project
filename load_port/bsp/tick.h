/**
 * @file    tick.h
 * @brief   [L1 BSP] SysTick 1ms 시간 기준
 * @details HAL_GetTick() 대체. 모든 타임아웃·주기 판단의 기준 시간.
 */
#ifndef TICK_H
#define TICK_H

#include <stdint.h>

void     Tick_Init_Systick(void);
uint32_t Tick_Get_Ms(void);

#endif /* TICK_H */
