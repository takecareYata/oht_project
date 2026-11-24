#ifndef TSe84_H
#define TSe84_H

#include "main.h"

/* 0이면 진단 큐와 출력 스레드를 빌드에서 제외한다. */
#ifndef TSe84_DEBUG_ENABLED
#define TSe84_DEBUG_ENABLED (1)
#endif
#if (TSe84_DEBUG_ENABLED != 0) && (TSe84_DEBUG_ENABLED != 1)
#error "TSe84_DEBUG_ENABLED must be 0 or 1"
#endif
#if TSe84_DEBUG_ENABLED
#define TSe84_DEBUG_QUEUE_SIZE (64U)
#define TSe84_DEBUG_TEXT_SIZE (256U)
#endif

/* Jetson 40핀 헤더의 E84 GPIO와 테스트 코드의 TA1~TA4 설정이다. */
#define TSe84_GPIO_DEVICE "/dev/gpiochip0"
#define TSe84_GPIO_LINE_COUNT (5U)
#define TSe84_POLL_MS (10U)
#define TSe84_SETTLE_MS (100U)
#define TSe84_TA1_MS (2000U)
#define TSe84_TA2_MS (2000U)
#define TSe84_TA3_MS (35000U)
#define TSe84_TA4_MS (2000U)

/* 아래 비트는 내부 신호 저장용이며 실제 GPIO 핀 번호가 아니다. */
#define TSe84_SIGNAL_NONE (0U)
#define TSe84_SIGNAL_VALID_MASK (0x01U)
#define TSe84_SIGNAL_CS_0_MASK (0x02U)
#define TSe84_SIGNAL_TR_REQ_MASK (0x04U)
#define TSe84_SIGNAL_BUSY_MASK (0x08U)
#define TSe84_SIGNAL_COMPT_MASK (0x10U)
#define TSe84_SIGNAL_L_REQ_MASK (0x20U)
#define TSe84_SIGNAL_READY_MASK (0x40U)
#define TSe84_SIGNAL_U_REQ_MASK (0x80U)
#define TSe84_SIGNAL_HO_AVBL_MASK (0x100U)
#define TSe84_SIGNAL_ES_MASK (0x200U)
#define TSe84_OUTPUT_SIGNAL_MASK (TSe84_SIGNAL_VALID_MASK | \
                                 TSe84_SIGNAL_CS_0_MASK | \
                                 TSe84_SIGNAL_TR_REQ_MASK | \
                                 TSe84_SIGNAL_BUSY_MASK | \
                                 TSe84_SIGNAL_COMPT_MASK)
#define TSe84_INPUT_SIGNAL_MASK (TSe84_SIGNAL_L_REQ_MASK | \
                                 TSe84_SIGNAL_READY_MASK | \
                                 TSe84_SIGNAL_U_REQ_MASK | \
                                 TSe84_SIGNAL_HO_AVBL_MASK | \
                                 TSe84_SIGNAL_ES_MASK)

typedef enum
{
    TSe84_STATE_IDLE = 0,
    TSe84_STATE_PREPARE,
    TSe84_STATE_WAIT_L_REQ_ON,
    TSe84_STATE_WAIT_READY_ON,
    TSe84_STATE_READY,
    TSe84_STATE_WAIT_L_REQ_OFF,
    TSe84_STATE_WAIT_COMPLETE_REQUEST,
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

#if TSe84_DEBUG_ENABLED
typedef struct
{
    const char * message;      /* 정적 수명 문자열만 저장한다. */
    const char * detail_label;
    uint32_t detail;
    TSe84_state_t state;
    TSe84_signal_t signals;
    bool inputs_valid;
    bool outputs_valid;
} TSe84_debug_entry_t;
#endif

/* DEV-PTHREAD-01/DEV-VOID-01: pthread_create에 등록하는 태스크 진입 함수이다. */
void * TSe84_Init_Task(void * argument);

void TSe84_Init_Communication(const TSe84_config_t * config);

/* BUSY는 GPIO 설정 성공 후 true를 반환한다. NONE은 출력을 내리고 인계를 취소한다. */
bool TSe84_Set_Request(main_e84_request_t request);

/* 마지막으로 읽은 입력과 마지막으로 설정한 출력을 복사한다. */
void TSe84_Get_Signal(TSe84_signal_t * signal);

/* 핸드셰이크 타임아웃 등의 E84 오류를 설정한다. */
void TSe84_Set_Fault(main_fault_t fault);

#endif /* TSe84_H */
