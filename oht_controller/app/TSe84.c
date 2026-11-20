#include "TSe84.h"

#include <stddef.h>

void * TSe84_Init_Task(void * argument)
{
    /* 설정값 0은 미설정 상태이다. 신호 제어는 아직 구현하지 않는다. */
    const TSe84_config_t config =
    {
        .l_req_timeout_ms = 0U,
        .ready_timeout_ms = 0U,
        .transfer_timeout_ms = 0U,
        .complete_timeout_ms = 0U
    };

    (void)argument;
    TSe84_Init_Communication(&config);

    /* TODO: E84 태스크의 반복 처리와 종료 처리를 구현한다. */
    return NULL;
}

/*
 * 구현 시 반영할 그림의 인계 순서:
 * VALID/CS_0 ON -> L_REQ ON -> TR_REQ ON -> READY ON -> 준비 이벤트
 * -> BUSY ON -> Z축 하강/웨이퍼 인계 -> L_REQ OFF
 * -> BUSY/TR_REQ OFF -> COMPT ON -> READY OFF
 * -> COMPT/VALID/CS_0 OFF -> 완료 이벤트.
 * 실제 신호 제어, 시간 측정 및 오류 통보는 아직 구현하지 않는다.
 */
void TSe84_Init_Communication(const TSe84_config_t * config)
{
    (void)config;
    
}

void TSe84_Set_Request(main_e84_request_t request)
{
    (void)request;
}

void TSe84_Get_Signal(TSe84_signal_t * signal)
{
    if (signal != NULL)
    {
        signal->input_signals = TSe84_SIGNAL_NONE;
        signal->output_signals = TSe84_SIGNAL_NONE;
    }
}

void TSe84_Set_Fault(main_fault_t fault)
{
    (void)fault;
}
