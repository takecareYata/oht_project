#include "TScontrol.h"
#include "TSspi.h"
#include "TSe84.h"

#include <pthread.h>
#include <stddef.h>

/* ==================== 내부 큐 및 FSM 상태 ==================== */
static pthread_mutex_t TScontrol_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t TScontrol_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t TScontrol_event_ready = PTHREAD_COND_INITIALIZER;
static TScontrol_event_t TScontrol_events[TScontrol_QUEUE_SIZE];
static size_t TScontrol_event_head;
static size_t TScontrol_event_count;
static bool TScontrol_event_overflow;
static TScontrol_notice_t TScontrol_notices[TScontrol_QUEUE_SIZE];
static size_t TScontrol_notice_head;
static size_t TScontrol_notice_count;
static bool TScontrol_notice_overflow;
static main_status_t TScontrol_status =
{
    main_STATE_IDLE, main_AXIS_STATE_UNKNOWN, main_AXIS_STATE_UNKNOWN,
    main_E84_EVENT_NONE, main_FAULT_NONE
};
static main_command_t TScontrol_command = main_COMMAND_NONE;
static bool TScontrol_handover_axis_complete;
static bool TScontrol_load_request_off;
static main_axis_t TScontrol_expected_axis = main_AXIS_NONE;
static int32_t TScontrol_expected_position;
static bool TScontrol_axis_pending;

static bool TScontrol_Get_Event(TScontrol_event_t * event);
static bool TScontrol_Get_Notice(TScontrol_notice_t * notice);
static void TScontrol_Set_Process_Command(main_command_t command);
static void TScontrol_Set_Event(const TScontrol_event_t * event);
static void TScontrol_Set_Notice(TScontrol_notice_kind_t kind,main_command_result_t result);
static void TScontrol_Set_Transition(main_state_t state);
static void TScontrol_Set_Fault_Internal(main_fault_t fault);
static void TScontrol_Get_Interlock_Internal(bool * permitted);
static void TScontrol_Set_Axis_Move(main_axis_t axis, int32_t position);
static void TScontrol_Set_Process_Event(const TScontrol_event_t * event);
static void TScontrol_Set_Process_Axis(const TScontrol_event_t * event);
static void TScontrol_Set_Process_E84(main_e84_event_t event);
static void TScontrol_Set_Handover_Complete(void);

/* ==================== 태스크 실행 ==================== */
void * TScontrol_Init_Task(void * argument)
{
    TScontrol_event_t event;
    const main_axis_command_t x_status =
        { main_AXIS_X, main_AXIS_COMMAND_GET_STATUS, 0 };
    const main_axis_command_t z_status =
        { main_AXIS_Z, main_AXIS_COMMAND_GET_STATUS, 0 };

    (void)argument;
    TSspi_Set_Axis_Command(&x_status);
    TSspi_Set_Axis_Command(&z_status);

    /* 이벤트를 하나 받아 처리한다. 큐 대기는 아래 보조 함수에서 수행한다. */
    while (TScontrol_Get_Event(&event) == true)
    {
        if (pthread_mutex_lock(&TScontrol_state_mutex) != 0)
        {
            break;
        }
        TScontrol_Set_Process_Event(&event);
        (void)pthread_mutex_unlock(&TScontrol_state_mutex);
    }
    return NULL;
}

/* ==================== 외부 CSU 입력: 이벤트를 큐에 전달 ==================== */
void TScontrol_Init_Fsm(void)
{
    TScontrol_event_t event = { 0 };
    event.kind = TScontrol_EVENT_INIT;
    TScontrol_Set_Event(&event);
}

void TScontrol_Set_Command(main_command_t command)
{
    TScontrol_event_t event = { 0 };
    event.kind = TScontrol_EVENT_COMMAND;
    event.command = command;
    TScontrol_Set_Event(&event);
}

void TScontrol_Get_Interlock(bool * permitted)
{
    if (permitted != NULL)
    {
        *permitted = false;
        if (pthread_mutex_lock(&TScontrol_state_mutex) == 0)
        {
            TScontrol_Get_Interlock_Internal(permitted);
            (void)pthread_mutex_unlock(&TScontrol_state_mutex);
        }
    }
}

void TScontrol_Set_State(main_state_t state)
{
    TScontrol_event_t event = { 0 };
    event.kind = TScontrol_EVENT_STATE;
    event.state = state;
    TScontrol_Set_Event(&event);
}

void TScontrol_Set_Axis_Event(main_axis_t axis, main_axis_event_t event,
                              const main_axis_status_t * status)
{
    if ((status != NULL) && (axis == status->axis) &&
        ((axis == main_AXIS_X) || (axis == main_AXIS_Z)))
    {
        TScontrol_event_t input = { 0 };
        input.kind = TScontrol_EVENT_AXIS;
        input.axis = axis;
        input.axis_event = event;
        input.axis_status = *status;
        TScontrol_Set_Event(&input);
    }
}

void TScontrol_Set_E84_Event(main_e84_event_t event)
{
    TScontrol_event_t input = { 0 };
    input.kind = TScontrol_EVENT_E84;
    input.e84_event = event;
    TScontrol_Set_Event(&input);
}

void TScontrol_Set_Fault(main_fault_t fault)
{
    TScontrol_event_t event = { 0 };
    if ((fault >= main_FAULT_INTERLOCK) && (fault <= main_FAULT_E84_TIMEOUT))
    {
        event.kind = TScontrol_EVENT_FAULT;
        event.fault = fault;
        TScontrol_Set_Event(&event);
    }
}

/* ==================== 이벤트 분기: 종류별 처리 함수 호출 ==================== */
static void TScontrol_Set_Process_Event(const TScontrol_event_t * event)
{
    /* TStcp_Parse와 동일하게 종류를 확인하고 담당 함수를 호출한다. */
    switch (event->kind)
    {
        case TScontrol_EVENT_COMMAND:
            TScontrol_Set_Process_Command(event->command);
            break;
        case TScontrol_EVENT_AXIS:
            TScontrol_Set_Process_Axis(event);
            break;
        case TScontrol_EVENT_E84:
            TScontrol_Set_Process_E84(event->e84_event);
            break;
        case TScontrol_EVENT_FAULT:
            TScontrol_Set_Fault_Internal(event->fault);
            break;
        case TScontrol_EVENT_INIT:
        case TScontrol_EVENT_STATE:
            /* 대기/완료 상태만 초기화한다. 동작 중 상태 변경과 오류 해제는 막는다. */
            if (((TScontrol_status.state == main_STATE_IDLE) ||
                 (TScontrol_status.state == main_STATE_COMPLETE)) &&
                ((event->kind == TScontrol_EVENT_INIT) || (event->state == main_STATE_IDLE)))
            {
                TScontrol_command = main_COMMAND_NONE;
                TScontrol_Set_Transition(main_STATE_IDLE);
            }
            break;
        default:
            break;
    }
}

/* ==================== 명령 및 완료 이벤트 처리 ==================== */
static void TScontrol_Set_Process_Command(main_command_t command)
{
    bool permitted = false;

    TScontrol_Get_Interlock_Internal(&permitted);
    if ((command == main_COMMAND_START) && (permitted == true))
    {
        TScontrol_command = command;
        TScontrol_status.e84_event = main_E84_EVENT_NONE;
        TScontrol_Set_Notice(TScontrol_NOTICE_COMMAND, main_COMMAND_RESULT_ACCEPTED);
        TScontrol_Set_Transition(main_STATE_WAFER_PICKUP);
    }
    else
    {
        TScontrol_Set_Notice(TScontrol_NOTICE_COMMAND, main_COMMAND_RESULT_REJECTED);
        /* 운전 중 중복 명령은 거부만 한다. 시작 시 인터록 불만족은 FAULT이다. */
        if ((command == main_COMMAND_START) &&
            ((TScontrol_status.state == main_STATE_IDLE) ||
             (TScontrol_status.state == main_STATE_COMPLETE)))
        {
            TScontrol_Set_Fault_Internal(main_FAULT_INTERLOCK);
        }
    }
}

static void TScontrol_Set_Process_Axis(const TScontrol_event_t * event)
{
    const main_axis_status_t * status = &event->axis_status;

    if (event->axis == main_AXIS_X)
    {
        TScontrol_status.x_axis_state = status->state;
    }
    else
    {
        TScontrol_status.z_axis_state = status->state;
    }

    /* 축 오류가 있으면 다음 단계로 진행하지 않는다. */
    if ((status->fault != main_FAULT_NONE) || (status->state == main_AXIS_STATE_FAULT) ||
        (status->heartbeat_status == main_HEARTBEAT_LOST))
    {
        TScontrol_Set_Fault_Internal(main_FAULT_SPI_COMMUNICATION);
        return;
    }
    /* 요청한 축이 목표 위치에 도착한 완료 이벤트만 사용한다. */
    if ((event->axis_event != main_AXIS_EVENT_COMPLETE) ||
        (status->state != main_AXIS_STATE_COMPLETE) || (TScontrol_axis_pending == false) ||
        (event->axis != TScontrol_expected_axis) ||
        (status->current_position != TScontrol_expected_position))
    {
        return;
    }
    TScontrol_axis_pending = false;

    switch (TScontrol_status.state)
    {
        case main_STATE_WAFER_PICKUP:       /* 픽업 완료 -> X축 이송 */
            TScontrol_Set_Transition(main_STATE_X_TRANSFER);
            break;
        case main_STATE_X_TRANSFER:         /* 이송 완료 -> E84 준비 */
            TScontrol_Set_Transition(main_STATE_E84_PREPARE);
            break;
        case main_STATE_WAFER_HANDOVER:     /* 인계 축 완료 -> L_REQ OFF도 확인 */
            TScontrol_handover_axis_complete = true;
            TScontrol_Set_Handover_Complete();
            break;
        case main_STATE_Z_RETURN:           /* 복귀 완료 -> 작업 완료 */
            TScontrol_Set_Transition(main_STATE_COMPLETE);
            break;
        case main_STATE_INIT:
        case main_STATE_IDLE:
        case main_STATE_E84_PREPARE:
        case main_STATE_E84_COMPLETE:
        case main_STATE_COMPLETE:
        case main_STATE_FAULT:
        default:
            break;
    }
}

static void TScontrol_Set_Process_E84(main_e84_event_t event)
{
    switch (event)
    {
        case main_E84_EVENT_READY:
            if (TScontrol_status.state == main_STATE_E84_PREPARE)
            {
                TScontrol_status.e84_event = event;
                TScontrol_Set_Transition(main_STATE_WAFER_HANDOVER);
            }
            break;
        case main_E84_EVENT_LOAD_REQUEST_OFF:
            if (TScontrol_status.state == main_STATE_WAFER_HANDOVER)
            {
                TScontrol_status.e84_event = event;
                TScontrol_load_request_off = true;
                TScontrol_Set_Handover_Complete();
            }
            break;
        case main_E84_EVENT_COMPLETE:
            if (TScontrol_status.state == main_STATE_E84_COMPLETE)
            {
                TScontrol_status.e84_event = event;
                TScontrol_Set_Transition(main_STATE_Z_RETURN);
            }
            break;
        case main_E84_EVENT_NONE:
        default:
            break;
    }
}

static void TScontrol_Set_Handover_Complete(void)
{
    /* Z축 완료와 L_REQ OFF가 도착하는 순서는 서로 달라도 된다. */
    if ((TScontrol_handover_axis_complete == true) &&
        (TScontrol_load_request_off == true))
    {
        TScontrol_Set_Transition(main_STATE_E84_COMPLETE);
    }
}

/* ==================== 상태 변경: 상태별 실행 동작 ==================== */
static void TScontrol_Set_Transition(main_state_t state)
{
    if ((TScontrol_status.fault != main_FAULT_NONE) && (state != main_STATE_FAULT))
    {
        return;
    }
    TScontrol_status.state = state;
    switch (state)
    {
        case main_STATE_WAFER_PICKUP:
            TScontrol_Set_Axis_Move(main_AXIS_Z, TScontrol_PICKUP_POSITION);
            break;
        case main_STATE_X_TRANSFER:
            TScontrol_Set_Axis_Move(main_AXIS_X, TScontrol_TRANSFER_POSITION);
            break;
        case main_STATE_E84_PREPARE:
            TSe84_Set_Request(main_E84_REQUEST_PREPARE);
            break;
        case main_STATE_WAFER_HANDOVER:
            TScontrol_handover_axis_complete = false;
            TScontrol_load_request_off = false;
            TSe84_Set_Request(main_E84_REQUEST_BUSY);
            TScontrol_Set_Axis_Move(main_AXIS_Z, TScontrol_HANDOVER_POSITION);
            break;
        case main_STATE_E84_COMPLETE:
            TSe84_Set_Request(main_E84_REQUEST_COMPLETE);
            break;
        case main_STATE_Z_RETURN:
            TScontrol_Set_Axis_Move(main_AXIS_Z, TScontrol_RETURN_POSITION);
            break;
        case main_STATE_INIT:
        case main_STATE_IDLE:
        case main_STATE_COMPLETE:
        case main_STATE_FAULT:
        default:
            break;
    }
    TScontrol_Set_Notice(TScontrol_NOTICE_STATUS, main_COMMAND_RESULT_NONE);
    if ((state == main_STATE_COMPLETE) && (TScontrol_status.fault == main_FAULT_NONE))
    {
        TScontrol_Set_Notice(TScontrol_NOTICE_COMPLETE, main_COMMAND_RESULT_NONE);
        TScontrol_command = main_COMMAND_NONE;
    }
}

static void TScontrol_Set_Axis_Move(main_axis_t axis, int32_t position)
{
    const main_axis_command_t command = { axis, main_AXIS_COMMAND_MOVE, position };
    if (axis == main_AXIS_X)
    {
        TScontrol_status.x_axis_state = main_AXIS_STATE_MOVING;
    }
    else
    {
        TScontrol_status.z_axis_state = main_AXIS_STATE_MOVING;
    }
    TScontrol_expected_axis = axis;
    TScontrol_expected_position = position;
    TScontrol_axis_pending = true;
    TSspi_Set_Axis_Command(&command);
}

static void TScontrol_Set_Fault_Internal(main_fault_t fault)
{
    /* 최초 오류를 유지한다. 자동 복귀 및 동작 중 초기화는 허용하지 않는다. */
    if (TScontrol_status.state != main_STATE_FAULT)
    {
        TScontrol_axis_pending = false;
        TScontrol_status.fault = fault;
        TScontrol_Set_Transition(main_STATE_FAULT);
        TScontrol_Set_Notice(TScontrol_NOTICE_ALARM, main_COMMAND_RESULT_NONE);
        TScontrol_command = main_COMMAND_NONE;
        /* 축 정지 API가 없어 실제 정지 명령은 보내지 않는다. */
    }
}

static void TScontrol_Get_Interlock_Internal(bool * permitted)
{
    main_heartbeat_status_t x = main_HEARTBEAT_UNKNOWN;
    main_heartbeat_status_t z = main_HEARTBEAT_UNKNOWN;
    main_heartbeat_status_t e84 = main_HEARTBEAT_UNKNOWN;

    TSspi_Get_Heartbeat_Status(main_AXIS_X, &x);
    TSspi_Get_Heartbeat_Status(main_AXIS_Z, &z);
    TSe84_Get_Heartbeat_Status(&e84);
    
    *permitted = (TScontrol_POSITIONS_CONFIGURED == 1) &&
        ((TScontrol_status.state == main_STATE_IDLE) ||
         (TScontrol_status.state == main_STATE_COMPLETE)) &&
        (TScontrol_status.fault == main_FAULT_NONE) &&
        ((TScontrol_status.x_axis_state == main_AXIS_STATE_IDLE) ||
         (TScontrol_status.x_axis_state == main_AXIS_STATE_COMPLETE)) &&
        ((TScontrol_status.z_axis_state == main_AXIS_STATE_IDLE) ||
         (TScontrol_status.z_axis_state == main_AXIS_STATE_COMPLETE)) &&
        (x == main_HEARTBEAT_NORMAL) && (z == main_HEARTBEAT_NORMAL) &&
        (e84 == main_HEARTBEAT_NORMAL);
}

/* ==================== TCP 알림 분기: TCP 태스크에서만 송신 ==================== */
void TScontrol_Process_Notifications(void)
{
    TScontrol_notice_t notice;
    TStcp_result_t result;
    size_t count;

    /* TCP 태스크에서만 호출한다. 큐에서 하나씩 꺼내 종류에 맞게 송신한다. */
    for (count = 0U; count < TScontrol_QUEUE_SIZE; ++count)
    {
        if (TScontrol_Get_Notice(&notice) == false)
        {
            break;
        }
        switch (notice.kind)
        {
            case TScontrol_NOTICE_STATUS:
                TStcp_Set_Status(&notice.status);
                break;
            case TScontrol_NOTICE_COMMAND:
                TStcp_Set_Command_Result(notice.result);
                break;
            case TScontrol_NOTICE_COMPLETE:
                TStcp_Set_Complete(notice.command);
                break;
            case TScontrol_NOTICE_ALARM:
                TStcp_Set_Alarm(notice.fault);
                break;
            default:
                break;
        }
        TStcp_Get_Result(&result);
        if (result != TStcp_RESULT_OK)
        {
            /* 실패 프레임의 자동 재전송은 하지 않는다. */
            break;
        }
    }
}

/* ==================== 큐 보조 함수: 스레드 동기화와 데이터 보관 ==================== */
static void TScontrol_Set_Event(const TScontrol_event_t * event)
{
    if (pthread_mutex_lock(&TScontrol_queue_mutex) == 0)
    {
        if (TScontrol_event_count < TScontrol_QUEUE_SIZE)
        {
            TScontrol_events[(TScontrol_event_head + TScontrol_event_count) % TScontrol_QUEUE_SIZE] = *event;
            ++TScontrol_event_count;
        }
        else
        {
            TScontrol_event_overflow = true;
        }
        (void)pthread_cond_signal(&TScontrol_event_ready);
        (void)pthread_mutex_unlock(&TScontrol_queue_mutex);
    }
}

static bool TScontrol_Get_Event(TScontrol_event_t * event)
{
    int ret;

    if (pthread_mutex_lock(&TScontrol_queue_mutex) != 0)
    {
        return false;
    }
    ret = 0;
    while ((TScontrol_event_count == 0U) && (TScontrol_event_overflow == false) && (ret == 0))
    {
        ret = pthread_cond_wait(&TScontrol_event_ready, &TScontrol_queue_mutex);
    }
    if (ret == 0)
    {
        if (TScontrol_event_overflow == true)
        {
            /* 이벤트가 유실되면 대기 이벤트를 지우고 FAULT를 전달한다. */
            TScontrol_event_overflow = false;
            TScontrol_event_head = 0U;
            TScontrol_event_count = 0U;
            *event = (TScontrol_event_t){ 0 };
            event->kind = TScontrol_EVENT_FAULT;
            event->fault = main_FAULT_INTERLOCK;
        }
        else
        {
            *event = TScontrol_events[TScontrol_event_head];
            TScontrol_event_head = (TScontrol_event_head + 1U) % TScontrol_QUEUE_SIZE;
            --TScontrol_event_count;
        }
    }
    (void)pthread_mutex_unlock(&TScontrol_queue_mutex);
    return ret == 0;
}

static void TScontrol_Set_Notice(TScontrol_notice_kind_t kind,
                                 main_command_result_t result)
{
    TScontrol_notice_t notice = { 0 };
    notice.kind = kind;
    notice.status = TScontrol_status;
    notice.result = result;
    notice.command = TScontrol_command;
    notice.fault = TScontrol_status.fault;
    if (pthread_mutex_lock(&TScontrol_queue_mutex) == 0)
    {
        if (TScontrol_notice_count < TScontrol_QUEUE_SIZE)
        {
            TScontrol_notices[(TScontrol_notice_head + TScontrol_notice_count) %
                              TScontrol_QUEUE_SIZE] = notice;
            ++TScontrol_notice_count;
        }
        else
        {
            /* 통보 유실 시 추가 운전을 막고 TCP 복구 후 알람을 보고한다. */
            TScontrol_notice_overflow = true;
            TScontrol_status.fault = main_FAULT_INTERLOCK;
            TScontrol_status.state = main_STATE_FAULT;
            TScontrol_axis_pending = false;
            TScontrol_command = main_COMMAND_NONE;
        }
        (void)pthread_mutex_unlock(&TScontrol_queue_mutex);
    }
}

static bool TScontrol_Get_Notice(TScontrol_notice_t * notice)
{
    bool available = false;

    if (pthread_mutex_lock(&TScontrol_queue_mutex) == 0)
    {
        if (TScontrol_notice_count > 0U)
        {
            *notice = TScontrol_notices[TScontrol_notice_head];
            TScontrol_notice_head = (TScontrol_notice_head + 1U) % TScontrol_QUEUE_SIZE;
            --TScontrol_notice_count;
            available = true;
        }
        else if (TScontrol_notice_overflow == true)
        {
            *notice = (TScontrol_notice_t){ 0 };
            notice->kind = TScontrol_NOTICE_ALARM;
            notice->fault = main_FAULT_INTERLOCK;
            TScontrol_notice_overflow = false;
            available = true;
        }
        else
        {
            /* 알림 없음. */
        }
        (void)pthread_mutex_unlock(&TScontrol_queue_mutex);
    }
    return available;
}
