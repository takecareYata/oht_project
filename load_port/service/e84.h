/**
 * @file    e84.h
 * @brief   [L3 Service] E84 Passive FSM
 * @details CSC : Load Port / CSU : E84 Passive
 *          FOUP 없음 -> 적재(L_REQ), FOUP 있음 -> 반출(U_REQ)
 */
#ifndef E84_H
#define E84_H

#include <stdint.h>
#include <stdbool.h>

/* ---- 단계별 타임아웃 (임시값, Z축 실측 후 재결정) ---- */
#define E84_TP1_MS      (2000U)    /* L_REQ/U_REQ ON  -> TR_REQ ON */
#define E84_TP2_MS      (2000U)    /* READY ON        -> BUSY ON */
#define E84_TP3_MS      (30000U)   /* BUSY ON         -> 안착/제거 판정 */
#define E84_TP4_MS      (30000U)   /* L_REQ/U_REQ OFF -> BUSY OFF, TR_REQ OFF, COMPT ON */
#define E84_TP5_MS      (2000U)    /* READY OFF       -> VALID, CS_0, COMPT OFF */

#define E84_SAMPLE_MS   (1U)       /* 입력 샘플 주기 (2회 연속 동일 시 반영) */

/* ---- Fault 복구 (Jetson 중앙제어 RECOVER 방식에 맞춤) ----
 * Jetson RECOVER : TScontrol_Set_Recovery() -> TSe84 IDLE -> E84 출력 5개 모두 LOW
 * Load Port      : Fault 상태에서 Active 입력 5개가 모두 OFF로 1초 이상 유지 + 센서 현재 판정 정상
 *                  -> Fault 해제 -> HO_AVBL ON -> IDLE
 * Jetson은 다음 핸드셰이크 전에 HO_AVBL = 1을 확인해야 한다. (LP 복구까지 약 1초) */
#define E84_RECOVER_MS  (1000U)

typedef enum
{
    E84_STATE_IDLE = 0,
    E84_STATE_WAIT_TR_REQ,
    E84_STATE_WAIT_BUSY,
    E84_STATE_WAIT_TRANSFER,
    E84_STATE_WAIT_COMPLETE,
    E84_STATE_WAIT_VALID_OFF,
    E84_STATE_FAULT
} e84State;

typedef enum
{
    E84_MODE_NONE = 0,
    E84_MODE_LOAD,
    E84_MODE_UNLOAD
} e84Mode;

void     E84_Init_Pio(void);
void     E84_Get_Input(void);
void     E84_Run_Fsm(void);
void     E84_Set_Output(uint8_t outMask, bool on);
bool     E84_Get_Timeout(void);
e84State E84_Get_State(void);
e84Mode  E84_Get_Mode(void);

#endif /* E84_H */
