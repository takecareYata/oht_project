/**
 * @file    system.c
 * @brief   [L3 Service] Fault 관리 및 Watchdog
 * @details - Fault : 최초 원인 1개를 래치(latch)
 *          - Fault 해제 : e84.c FAULT 상태에서 입력 전체 OFF 1초 유지 시 호출 (Jetson RECOVER)
 *          - 추후 결정 : IWDG 사용 여부 (아래 Watchdog 함수 내용)
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

/* Fault 해제 : e84.c 복구 요청 처리에서 호출 (MCU 리셋으로도 해제됨) */
void System_Clear_Fault(void)
{
    systemFaultCode = SYSTEM_ERR_NONE;
}
