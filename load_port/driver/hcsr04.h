/**
 * @file    hcsr04.h
 * @brief   [L2 Driver] HC-SR04 초음파 센서 드라이버
 * @details CSC : Load Port / CSU : 센서·인터락 제어
 *          TRIG : PA8 GPIO 출력
 *          ECHO : PB6 TIM4_CH1 입력캡처 (AF2, 양 엣지, 1 tick = 1us)
 */
#ifndef HCSR04_H
#define HCSR04_H

#include <stdint.h>

typedef enum
{
    HCSR04_RESULT_NONE = 0,   /* 새 측정값 없음 (측정 안 함 / 측정 중) */
    HCSR04_RESULT_OK,         /* 거리 측정 완료 */
    HCSR04_RESULT_NO_ECHO     /* 에코 없음 또는 측정 범위 초과 */
} hcsr04Result;

void         Hcsr04_Init_Sensor(void);
void         Hcsr04_Set_Trigger(void);
hcsr04Result Hcsr04_Get_Distance(uint16_t *pDistanceMm);

#endif /* HCSR04_H */
