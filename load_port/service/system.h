/**
 * @file    system.h
 * @brief   [L3 Service] Fault 관리 및 Watchdog
 * @details CSC : Load Port / CSU : 상태·Fault
 *          Heartbeat 기능은 제외됨 (RTOS 미사용)
 */
#ifndef SYSTEM_H
#define SYSTEM_H

typedef enum
{
    SYSTEM_ERR_NONE = 0,
    SYSTEM_ERR_TP1_TIMEOUT,     /* L_REQ/U_REQ ON -> TR_REQ ON 대기 초과 */
    SYSTEM_ERR_TP2_TIMEOUT,     /* READY ON -> BUSY ON 대기 초과 */
    SYSTEM_ERR_TP3_TIMEOUT,     /* BUSY ON -> 안착/제거 판정 대기 초과 */
    SYSTEM_ERR_TP4_TIMEOUT,     /* L_REQ/U_REQ OFF -> COMPT ON 대기 초과 */
    SYSTEM_ERR_TP5_TIMEOUT,     /* READY OFF -> VALID OFF 대기 초과 */
    SYSTEM_ERR_SEQUENCE         /* 신호 순서 위반 (VALID/CS_0/TR_REQ/BUSY 이탈) */
} systemError;

void        System_Init_Watchdog(void);
void        System_Set_Watchdog(void);
void        System_Set_Fault(systemError errorCode);
systemError System_Get_Fault(void);
void        System_Clear_Fault(void);

#endif /* SYSTEM_H */
