/**
 * @file    loadport.h
 * @brief   [L3 Service] 로드포트 FOUP 감지·안착 판정 및 클램프
 * @details CSC : Load Port / CSU : 로드포트, 센서·인터락 제어
 *          HC-SR04 측면 수평 측정 기준
 */
#ifndef LOADPORT_H
#define LOADPORT_H

#include <stdint.h>
#include <stdbool.h>

/* ---- 판정 기준 (임시값, 실측 후 수정) ---- */
#define LOADPORT_PRESENT_MM      (100U)   /* 이 거리 이하 : FOUP 있음 */
#define LOADPORT_EMPTY_MM        (200U)   /* 이 거리 이상 또는 무반사 : 비어 있음 */
#define LOADPORT_MEASURE_MS      (60U)    /* 측정 주기 */
#define LOADPORT_CONFIRM_COUNT   (3U)     /* 연속 동일 결과 횟수 */
#define LOADPORT_SEAT_SETTLE_MS  (1000U)  /* 안착 판정 : 거리 안정 유지 시간 */
#define LOADPORT_SEAT_TOL_MM     (10U)    /* 안착 판정 : 허용 거리 변화 (+-1cm) */

typedef enum
{
    LOADPORT_STATUS_UNKNOWN = 0,   /* 판정 전 또는 보류 */
    LOADPORT_STATUS_EMPTY,         /* 포트 비어 있음 */
    LOADPORT_STATUS_PRESENT        /* FOUP 있음 */
} loadportStatus;

void           Loadport_Init_Sensor(void);
void           Loadport_Run_Sensor(void);
loadportStatus Loadport_Get_Status(void);
bool           Loadport_Get_Seat(void);
void           Loadport_Set_Clamp(bool on);
bool           Loadport_Get_Clamp(void);

#endif /* LOADPORT_H */
