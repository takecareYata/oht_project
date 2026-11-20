#ifndef TSe84_H
#define TSe84_H

#include "main.h"

/* 아래 비트는 내부 신호 저장용이며 실제 GPIO 핀 번호가 아니다. */
#define TSe84_SIGNAL_NONE (0U)
#define TSe84_SIGNAL_VALID_MASK (0x01U)
#define TSe84_SIGNAL_CS_0_MASK (0x02U)
#define TSe84_SIGNAL_TR_REQ_MASK (0x04U)
#define TSe84_SIGNAL_BUSY_MASK (0x08U)
#define TSe84_SIGNAL_COMPT_MASK (0x10U)
#define TSe84_SIGNAL_L_REQ_MASK (0x20U)
#define TSe84_SIGNAL_READY_MASK (0x40U)
#define TSe84_OUTPUT_SIGNAL_MASK (TSe84_SIGNAL_VALID_MASK | \
                                 TSe84_SIGNAL_CS_0_MASK | \
                                 TSe84_SIGNAL_TR_REQ_MASK | \
                                 TSe84_SIGNAL_BUSY_MASK | \
                                 TSe84_SIGNAL_COMPT_MASK)
#define TSe84_INPUT_SIGNAL_MASK (TSe84_SIGNAL_L_REQ_MASK | \
                                TSe84_SIGNAL_READY_MASK)

typedef enum
{
    TSe84_STATE_IDLE = 0,
    TSe84_STATE_WAIT_L_REQ_ON,
    TSe84_STATE_WAIT_READY_ON,
    TSe84_STATE_READY,
    TSe84_STATE_BUSY,
    TSe84_STATE_WAIT_L_REQ_OFF,
    TSe84_STATE_WAIT_READY_OFF,
    TSe84_STATE_COMPLETE,
    TSe84_STATE_FAULT
} TSe84_state_t;

typedef struct
{
    uint32_t l_req_timeout_ms;
    uint32_t ready_timeout_ms;
    uint32_t transfer_timeout_ms;
    uint32_t complete_timeout_ms;
} TSe84_config_t;

typedef struct
{
    uint32_t input_signals;
    uint32_t output_signals;
} TSe84_signal_t;

/* DEV-PTHREAD-01/DEV-VOID-01: pthread_create에 등록하는 태스크 진입 함수이다. */
void * TSe84_Init_Task(void * argument);

void TSe84_Init_Communication(const TSe84_config_t * config);

/* PREPARE, BUSY, COMPLETE 단계의 인계 요청을 전달받는다. */
void TSe84_Set_Request(main_e84_request_t request);

/* 미구현 상태에서는 두 신호 필드를 모두 TSe84_SIGNAL_NONE으로 설정한다. */
void TSe84_Get_Signal(TSe84_signal_t * signal);

/* 핸드셰이크 타임아웃 등의 E84 오류를 설정한다. */
void TSe84_Set_Fault(main_fault_t fault);

#endif /* TSe84_H */
