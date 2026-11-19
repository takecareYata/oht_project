#include "TScontrol.h"
#include "TSspi.h"
#include "TSe84.h"
#include <pthread.h>
#include <stddef.h>

/* ==================== 큐와 중앙 FSM 상태 ==================== */
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
static main_status_t TScontrol_status = { main_STATE_INIT, main_AXIS_STATE_UNKNOWN, main_AXIS_STATE_UNKNOWN, main_E84_EVENT_NONE, main_FAULT_NONE };
static main_command_t TScontrol_command = main_COMMAND_NONE;
static bool TScontrol_handover_axis_complete;
static bool TScontrol_load_request_off;
static bool TScontrol_stop_confirmed[2];
static bool TScontrol_axis_armed[2];
static main_axis_command_t TScontrol_move = { .axis = main_AXIS_NONE, .command = main_AXIS_COMMAND_NONE };

static bool TScontrol_Get_Event(TScontrol_event_t * event);
static bool TScontrol_Get_Notice(TScontrol_notice_t * notice);
static void TScontrol_Set_Event(const TScontrol_event_t * event);
static void TScontrol_Set_Notice(TScontrol_notice_kind_t kind, main_command_result_t result);
static void TScontrol_Parse(const TScontrol_event_t * event);
static void TScontrol_Parse_Command(main_command_t command);
static void TScontrol_Parse_Axis(const TScontrol_event_t * event);
static void TScontrol_Parse_E84(main_e84_event_t event);
static void TScontrol_Set_Transition(main_state_t state);
static void TScontrol_Set_Fault_Internal(main_fault_t fault);
static void TScontrol_Set_Stop(void);
static void TScontrol_Set_Recovery(void);
static void TScontrol_Set_Axis_Command(main_axis_t axis, main_axis_command_code_t code, uint8_t arg);
static void TScontrol_Set_Handover_Complete(void);
static bool TScontrol_Is_Ready(void);

/* ==================== 1. 태스크: 이벤트 하나를 받아 종류별로 처리 ==================== */
void * TScontrol_Init_Task(void * argument)
{
    TScontrol_event_t event;
    (void)argument;
    TScontrol_Init_Fsm();
    while (TScontrol_Get_Event(&event) == true)
    {
        if (pthread_mutex_lock(&TScontrol_state_mutex) != 0)
        {
            break;
        }
        TScontrol_Parse(&event);
        (void)pthread_mutex_unlock(&TScontrol_state_mutex);
    }
    return NULL;
}

static void TScontrol_Parse(const TScontrol_event_t * event)
{
    switch (event->kind)
    {
        case TScontrol_EVENT_COMMAND:
            TScontrol_Parse_Command(event->command);
            break;
        case TScontrol_EVENT_AXIS:
            TScontrol_Parse_Axis(event);
            break;
        case TScontrol_EVENT_E84:
            TScontrol_Parse_E84(event->e84_event);
            break;
        case TScontrol_EVENT_FAULT:
            TScontrol_Set_Fault_Internal(event->fault);
            break;
        case TScontrol_EVENT_INIT:
            if (TScontrol_status.state == main_STATE_INIT)
            {
                TScontrol_Set_Recovery();
            }
            break;
        case TScontrol_EVENT_STATE:
            /* 외부 상태 설정으로 복구 절차나 고장 해제를 우회하지 못한다. */
            if (event->state != main_STATE_IDLE)
            {
                break;
            }
            if (TScontrol_status.state == main_STATE_COMPLETE)
            {
                TScontrol_Set_Transition(main_STATE_IDLE);
            }
            break;
        default:
            break;
    }
}

/* ==================== 2. START / STOP / 명시적 RECOVER ==================== */
static void TScontrol_Parse_Command(main_command_t command)
{
    if (command == main_COMMAND_STOP)
    {
        TScontrol_Set_Notice(TScontrol_NOTICE_COMMAND, main_COMMAND_RESULT_ACCEPTED);
        TScontrol_Set_Fault_Internal(main_FAULT_STOP);
        return;
    }
    if (command == main_COMMAND_RECOVER)
    {
        switch (TScontrol_status.state)
        {
            case main_STATE_INIT:
            case main_STATE_IDLE:
            case main_STATE_COMPLETE:
            case main_STATE_FAULT:
                TScontrol_command = command;
                TScontrol_Set_Notice(TScontrol_NOTICE_COMMAND, main_COMMAND_RESULT_ACCEPTED);
                TScontrol_Set_Recovery();
                return;
            case main_STATE_WAFER_PICKUP:
            case main_STATE_X_TRANSFER:
            case main_STATE_E84_PREPARE:
            case main_STATE_WAFER_HANDOVER:
            case main_STATE_E84_COMPLETE:
            case main_STATE_Z_RETURN:
            case main_STATE_STOPPING:
            case main_STATE_ARMING_Z:
            case main_STATE_ARMING_X:
            case main_STATE_RECOVER_Z:
            case main_STATE_RECOVER_X:
            default:
                TScontrol_Set_Notice(TScontrol_NOTICE_COMMAND, main_COMMAND_RESULT_REJECTED);
                return;
        }
    }
    if (command == main_COMMAND_START)
    {
        if (TScontrol_Is_Ready() == true)
        {
            TScontrol_command = command;
            TScontrol_status.e84_event = main_E84_EVENT_NONE;
            TScontrol_Set_Notice(TScontrol_NOTICE_COMMAND, main_COMMAND_RESULT_ACCEPTED);
            TScontrol_Set_Transition(main_STATE_WAFER_PICKUP);
            return;
        }
    }
    TScontrol_Set_Notice(TScontrol_NOTICE_COMMAND, main_COMMAND_RESULT_REJECTED);
}

static void TScontrol_Set_Recovery(void)
{
    TScontrol_stop_confirmed[0] = false;
    TScontrol_stop_confirmed[1] = false;
    TScontrol_axis_armed[0] = false;
    TScontrol_axis_armed[1] = false;
    TScontrol_move.command = main_AXIS_COMMAND_NONE;
    TScontrol_status.e84_event = main_E84_EVENT_NONE;
    /* 기존 고장은 양축 RECOVER 완료 후에만 해제한다. */
    TSspi_Set_Recovery();
    TScontrol_Set_Transition(main_STATE_STOPPING);
}

/* ==================== 3. 축 이벤트: 접수가 아니라 실제 완료에서만 전이 ==================== */
static void TScontrol_Parse_Axis(const TScontrol_event_t * event)
{
    const main_axis_status_t * status = &event->axis_status;
    size_t index = (event->axis == main_AXIS_X) ? 0U : 1U;
    TScontrol_axis_armed[index] = status->armed;
    if (event->axis == main_AXIS_X)
    {
        TScontrol_status.x_axis_state = status->state;
    }
    else
    {
        TScontrol_status.z_axis_state = status->state;
    }

    /* STOP은 FAULT 상태로도 확인된다. ARM 전의 이전 FAULT는 정상 복구 대상이다. */
    if (TScontrol_status.state == main_STATE_STOPPING)
    {
        if (event->axis_event != main_AXIS_EVENT_STOPPED)
        {
            return;
        }
        if (status->command != main_AXIS_COMMAND_STOP)
        {
            return;
        }
        TScontrol_stop_confirmed[index] = true;
        if (TScontrol_stop_confirmed[0] == true)
        {
            if (TScontrol_stop_confirmed[1] == true)
            {
                TScontrol_Set_Transition(main_STATE_ARMING_Z);
            }
        }
        return;
    }
    if (TScontrol_status.state == main_STATE_ARMING_Z)
    {
        if (event->axis_event != main_AXIS_EVENT_ARMED)
        {
            return;
        }
        if (event->axis != main_AXIS_Z)
        {
            return;
        }
        if (status->command != main_AXIS_COMMAND_ARM)
        {
            return;
        }
        TScontrol_Set_Transition(main_STATE_ARMING_X);
        return;
    }
    if (TScontrol_status.state == main_STATE_ARMING_X)
    {
        if (event->axis_event != main_AXIS_EVENT_ARMED)
        {
            return;
        }
        if (event->axis != main_AXIS_X)
        {
            return;
        }
        if (status->command != main_AXIS_COMMAND_ARM)
        {
            return;
        }
        TScontrol_Set_Transition(main_STATE_RECOVER_Z);
        return;
    }
    if (TScontrol_status.state == main_STATE_FAULT)
    {
        return;
    }
    if (status->fault != main_FAULT_NONE)
    {
        TScontrol_Set_Fault_Internal(main_FAULT_SPI_COMMUNICATION);
        return;
    }
    if (status->state == main_AXIS_STATE_FAULT)
    {
        TScontrol_Set_Fault_Internal(main_FAULT_SPI_COMMUNICATION);
        return;
    }
    if (status->heartbeat_status == main_HEARTBEAT_LOST)
    {
        TScontrol_Set_Fault_Internal(main_FAULT_SPI_COMMUNICATION);
        return;
    }
    if (event->axis_event != main_AXIS_EVENT_COMPLETE)
    {
        return;
    }
    if (status->state != main_AXIS_STATE_COMPLETE)
    {
        return;
    }
    if (TScontrol_move.command == main_AXIS_COMMAND_NONE)
    {
        return;
    }
    if (status->command != TScontrol_move.command)
    {
        return;
    }
    if (event->axis != TScontrol_move.axis)
    {
        return;
    }
    /* seq와 CRC는 TSspi에서 확인했다. position은 표시용이므로 비교하지 않는다. */
    TScontrol_move.command = main_AXIS_COMMAND_NONE;
    switch (TScontrol_status.state)
    {
        case main_STATE_RECOVER_Z:
            TScontrol_Set_Transition(main_STATE_RECOVER_X);
            break;
        case main_STATE_RECOVER_X:
            TScontrol_status.fault = main_FAULT_NONE;
            TScontrol_Set_Transition(main_STATE_IDLE);
            if (TScontrol_status.state == main_STATE_IDLE)
            {
                if (TScontrol_command == main_COMMAND_RECOVER)
                {
                    TScontrol_Set_Notice(TScontrol_NOTICE_COMPLETE, main_COMMAND_RESULT_NONE);
                    TScontrol_command = main_COMMAND_NONE;
                }
            }
            break;
        case main_STATE_WAFER_PICKUP:
            TScontrol_Set_Transition(main_STATE_X_TRANSFER);
            break;
        case main_STATE_X_TRANSFER:
            TScontrol_Set_Transition(main_STATE_E84_PREPARE);
            break;
        case main_STATE_WAFER_HANDOVER:
            TScontrol_handover_axis_complete = true;
            TScontrol_Set_Handover_Complete();
            break;
        case main_STATE_INIT:
        case main_STATE_IDLE:
        case main_STATE_E84_PREPARE:
        case main_STATE_E84_COMPLETE:
        case main_STATE_Z_RETURN:
        case main_STATE_COMPLETE:
        case main_STATE_FAULT:
        case main_STATE_STOPPING:
        case main_STATE_ARMING_Z:
        case main_STATE_ARMING_X:
        default:
            break;
    }
}

/* ==================== 4. E84 이벤트와 상태별 명령 ==================== */
static void TScontrol_Parse_E84(main_e84_event_t event)
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
                /* PLACE 자체에 상승이 포함되므로 별도 Z_RETURN을 보내지 않는다. */
                TScontrol_Set_Transition(main_STATE_COMPLETE);
            }
            break;
        case main_E84_EVENT_NONE:
        default:
            break;
    }
}

static void TScontrol_Set_Handover_Complete(void)
{
    if (TScontrol_handover_axis_complete == true)
    {
        if (TScontrol_load_request_off == true)
        {
            TScontrol_Set_Transition(main_STATE_E84_COMPLETE);
        }
    }
}

static void TScontrol_Set_Transition(main_state_t state)
{
    TScontrol_status.state = state;
    switch (state)
    {
        case main_STATE_ARMING_Z:
            TScontrol_Set_Axis_Command(main_AXIS_Z, main_AXIS_COMMAND_ARM, 0U);
            break;
        case main_STATE_ARMING_X:
            TScontrol_Set_Axis_Command(main_AXIS_X, main_AXIS_COMMAND_ARM, 0U);
            break;
        case main_STATE_RECOVER_Z:
            TScontrol_Set_Axis_Command(main_AXIS_Z, main_AXIS_COMMAND_RECOVER, 0U);
            break;
        case main_STATE_RECOVER_X:
            TScontrol_Set_Axis_Command(main_AXIS_X, main_AXIS_COMMAND_RECOVER, 0U);
            break;
        case main_STATE_WAFER_PICKUP:
            TScontrol_Set_Axis_Command(main_AXIS_Z, main_AXIS_COMMAND_PICK, 0U);
            break;
        case main_STATE_X_TRANSFER:
            TScontrol_Set_Axis_Command(main_AXIS_X, main_AXIS_COMMAND_X_MOVE, TScontrol_TRANSFER_STATION);
            break;
        case main_STATE_E84_PREPARE:
            TSe84_Set_Request(main_E84_REQUEST_PREPARE);
            break;
        case main_STATE_WAFER_HANDOVER:
            TScontrol_handover_axis_complete = false;
            TScontrol_load_request_off = false;
            TSe84_Set_Request(main_E84_REQUEST_BUSY);
            TScontrol_Set_Axis_Command(main_AXIS_Z, main_AXIS_COMMAND_PLACE, 0U);
            break;
        case main_STATE_E84_COMPLETE:
            TSe84_Set_Request(main_E84_REQUEST_COMPLETE);
            break;
        case main_STATE_INIT:
        case main_STATE_IDLE:
        case main_STATE_Z_RETURN:
        case main_STATE_COMPLETE:
        case main_STATE_FAULT:
        case main_STATE_STOPPING:
        default:
            break;
    }
    TScontrol_Set_Notice(TScontrol_NOTICE_STATUS, main_COMMAND_RESULT_NONE);
    if (TScontrol_status.state == main_STATE_COMPLETE)
    {
        if (TScontrol_status.fault == main_FAULT_NONE)
        {
            TScontrol_Set_Notice(TScontrol_NOTICE_COMPLETE, main_COMMAND_RESULT_NONE);
            TScontrol_command = main_COMMAND_NONE;
        }
    }
}

static void TScontrol_Set_Axis_Command(main_axis_t axis, main_axis_command_code_t code, uint8_t arg)
{
    const main_axis_command_t command = { .axis = axis, .command = code, .arg = arg };
    TScontrol_move = command;
    TSspi_Set_Axis_Command(&command);
}

static void TScontrol_Set_Stop(void)
{
    const main_axis_command_t x = { .axis = main_AXIS_X, .command = main_AXIS_COMMAND_STOP };
    const main_axis_command_t z = { .axis = main_AXIS_Z, .command = main_AXIS_COMMAND_STOP };
    TSspi_Set_Axis_Command(&x);
    TSspi_Set_Axis_Command(&z);
    TScontrol_move.command = main_AXIS_COMMAND_NONE;
    TScontrol_axis_armed[0] = false;
    TScontrol_axis_armed[1] = false;
}

static void TScontrol_Set_Fault_Internal(main_fault_t fault)
{
    TScontrol_Set_Stop();
    if (TScontrol_status.state != main_STATE_FAULT)
    {
        TScontrol_status.fault = fault;
        TScontrol_Set_Transition(main_STATE_FAULT);
        TScontrol_Set_Notice(TScontrol_NOTICE_ALARM, main_COMMAND_RESULT_NONE);
        TScontrol_command = main_COMMAND_NONE;
    }
}

static bool TScontrol_Is_Ready(void)
{
    main_heartbeat_status_t x = main_HEARTBEAT_UNKNOWN;
    main_heartbeat_status_t z = main_HEARTBEAT_UNKNOWN;
    main_heartbeat_status_t e84 = main_HEARTBEAT_UNKNOWN;
    TSspi_Get_Heartbeat_Status(main_AXIS_X, &x);
    TSspi_Get_Heartbeat_Status(main_AXIS_Z, &z);
    TSe84_Get_Heartbeat_Status(&e84);
    if (TScontrol_status.fault != main_FAULT_NONE)
    {
        return false;
    }
    if (TScontrol_status.state != main_STATE_IDLE)
    {
        if (TScontrol_status.state != main_STATE_COMPLETE)
        {
            return false;
        }
    }
    if (TScontrol_axis_armed[0] == false)
    {
        return false;
    }
    if (TScontrol_axis_armed[1] == false)
    {
        return false;
    }
    if (TScontrol_status.x_axis_state != main_AXIS_STATE_IDLE)
    {
        if (TScontrol_status.x_axis_state != main_AXIS_STATE_COMPLETE)
        {
            return false;
        }
    }
    if (TScontrol_status.z_axis_state != main_AXIS_STATE_IDLE)
    {
        if (TScontrol_status.z_axis_state != main_AXIS_STATE_COMPLETE)
        {
            return false;
        }
    }
    if (x != main_HEARTBEAT_NORMAL)
    {
        return false;
    }
    if (z != main_HEARTBEAT_NORMAL)
    {
        return false;
    }
    return e84 == main_HEARTBEAT_NORMAL;
}

/* ==================== 5. 외부 CSU 연결: 입력 큐와 TCP 알림 ==================== */
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
            *permitted = TScontrol_Is_Ready();
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

void TScontrol_Set_Axis_Event(main_axis_t axis, main_axis_event_t event, const main_axis_status_t * status)
{
    TScontrol_event_t input = { 0 };

    if (status == NULL)
    {
        return;
    }
    if (axis != status->axis)
    {
        return;
    }
    if (axis != main_AXIS_X)
    {
        if (axis != main_AXIS_Z)
        {
            return;
        }
    }
    input.kind = TScontrol_EVENT_AXIS;
    input.axis = axis;
    input.axis_event = event;
    input.axis_status = *status;
    TScontrol_Set_Event(&input);
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
    if ((fault >= main_FAULT_INTERLOCK) && (fault <= main_FAULT_STOP))
    {
        event.kind = TScontrol_EVENT_FAULT;
        event.fault = fault;
        TScontrol_Set_Event(&event);
    }
}

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

/* ==================== 6. 큐 보조 함수: 데이터 보관과 스레드 동기화 ==================== */
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
    while (TScontrol_event_count == 0U)
    {
        if (TScontrol_event_overflow != false)
        {
            break;
        }
        if (ret != 0)
        {
            break;
        }
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

static void TScontrol_Set_Notice(TScontrol_notice_kind_t kind, main_command_result_t result)
{
    TScontrol_notice_t notice = { 0 };
    bool overflow = false;
    notice.kind = kind;
    notice.status = TScontrol_status;
    notice.result = result;
    notice.command = TScontrol_command;
    notice.fault = TScontrol_status.fault;
    if (pthread_mutex_lock(&TScontrol_queue_mutex) == 0)
    {
        if (TScontrol_notice_count < TScontrol_QUEUE_SIZE)
        {
            TScontrol_notices[(TScontrol_notice_head + TScontrol_notice_count) % TScontrol_QUEUE_SIZE] = notice;
            ++TScontrol_notice_count;
        }
        else
        {
            /* 통보 유실 시 추가 운전을 막고 TCP 복구 후 알람을 보고한다. */
            TScontrol_notice_overflow = true;
            overflow = true;
            TScontrol_status.fault = main_FAULT_INTERLOCK;
            TScontrol_status.state = main_STATE_FAULT;
            TScontrol_move.command = main_AXIS_COMMAND_NONE;
            TScontrol_command = main_COMMAND_NONE;
        }
        (void)pthread_mutex_unlock(&TScontrol_queue_mutex);
    }
    if (overflow == true)
    {
        TScontrol_Set_Stop();
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
