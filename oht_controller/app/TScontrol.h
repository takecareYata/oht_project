#ifndef TScontrol_H
#define TScontrol_H

#include "main.h"
#include "TStcp.h"

/* ==================== 설비별 설정: 단위/위치는 STM32 사양 확정 후 지정 ==================== */
#ifndef TScontrol_POSITIONS_CONFIGURED
#define TScontrol_POSITIONS_CONFIGURED (0)
#endif
#ifndef TScontrol_PICKUP_POSITION
#define TScontrol_PICKUP_POSITION (0)
#endif
#ifndef TScontrol_TRANSFER_POSITION
#define TScontrol_TRANSFER_POSITION (0)
#endif
#ifndef TScontrol_HANDOVER_POSITION
#define TScontrol_HANDOVER_POSITION (0)
#endif
#ifndef TScontrol_RETURN_POSITION
#define TScontrol_RETURN_POSITION (0)
#endif
#define TScontrol_QUEUE_SIZE (64U)

/* ==================== 태스크 간 입력 이벤트 및 TCP 알림 큐 ==================== */
typedef enum
{
    TScontrol_EVENT_INIT = 0,
    TScontrol_EVENT_COMMAND,
    TScontrol_EVENT_STATE,
    TScontrol_EVENT_AXIS,
    TScontrol_EVENT_E84,
    TScontrol_EVENT_FAULT
} TScontrol_event_kind_t;

typedef struct
{
    TScontrol_event_kind_t kind;
    main_command_t command;
    main_state_t state;
    main_axis_t axis;
    main_axis_event_t axis_event;
    main_axis_status_t axis_status;
    main_e84_event_t e84_event;
    main_fault_t fault;
} TScontrol_event_t;

typedef enum
{
    TScontrol_NOTICE_STATUS = 0,
    TScontrol_NOTICE_COMMAND,
    TScontrol_NOTICE_COMPLETE,
    TScontrol_NOTICE_ALARM
} TScontrol_notice_kind_t;

typedef struct
{
    TScontrol_notice_kind_t kind;
    main_status_t status;
    main_command_result_t result;
    main_command_t command;
    main_fault_t fault;
} TScontrol_notice_t;

/* DEV-PTHREAD-01/DEV-VOID-01: pthread_create에 등록하는 태스크 진입 함수이다. */
void * TScontrol_Init_Task(void * argument);

/* 대기/완료 상태에서 FSM 초기화 요청을 큐에 전달한다. FAULT는 해제하지 않는다. */
void TScontrol_Init_Fsm(void);

/* GUI에서 수신한 START 명령을 전달받는다. */
void TScontrol_Set_Command(main_command_t command);

/* 운전 가능 여부를 확인한다. */
void TScontrol_Get_Interlock(bool * permitted);

/* 픽업, X축 이송, 인계, Z축 복귀 등의 FSM 상태를 설정한다. */
void TScontrol_Set_State(main_state_t state);

/* SPI CSU에서 축별 ACK, 상태 및 완료 이벤트를 전달받는다. */
void TScontrol_Set_Axis_Event(main_axis_t axis, main_axis_event_t event,
                              const main_axis_status_t * status);

/* E84 CSU에서 인계 준비 및 완료 이벤트를 전달받는다. */
void TScontrol_Set_E84_Event(main_e84_event_t event);

/* 인터록, SPI 통신, E84 Heartbeat 및 타임아웃 오류를 전달받는다. */
void TScontrol_Set_Fault(main_fault_t fault);

/* TCP 태스크 전용: FSM 알림 큐를 비우며 기존 TCP 송신 API를 호출한다. */
void TScontrol_Process_Notifications(void);

#endif /* TScontrol_H */
