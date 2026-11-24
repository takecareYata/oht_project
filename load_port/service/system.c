/**
 * @file    system.c
 * @brief   [L3 Service] Fault 관리 및 Watchdog
 * @details - Fault : 최초 원인 1개를 래치(latch)
 *          - 추후 결정 : Fault 복구 방법 (System_Clear_Fault 호출 위치),
 *                        IWDG 사용 여부 (아래 Watchdog 함수 내용)
 */
#include "system.h"

static systemError systemFaultCode = SYSTEM_ERR_NONE;

/* 추후 결정 : IWDG 사용 시 ioc에서 IWDG 활성화 후 내용 추가 */
void System_Init_Watchdog(void)
{
    /* 예) DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP; */
}

/* 추후 결정 : IWDG 사용 시 IWDG->KR = 0xAAAAU; */
void System_Set_Watchdog(void)
{
}

/* Fault 래치 : 이미 Fault 상태면 최초 원인 유지 */
void System_Set_Fault(systemError errorCode)
{
    if (systemFaultCode == SYSTEM_ERR_NONE)
    {
        systemFaultCode = errorCode;
    }
}

systemError System_Get_Fault(void)
{
    return systemFaultCode;
}

/* 추후 결정 : 호출 위치 미정 (현재는 MCU 리셋으로만 Fault 해제) */
void System_Clear_Fault(void)
{
    systemFaultCode = SYSTEM_ERR_NONE;
}
