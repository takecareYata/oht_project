#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "TSspi.h"
#include "TScontrol.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/spi/spidev.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* ==================== 전용 상태와 공유 큐 ==================== */
static TSspi_axis_context_t TSspi_axes[TSspi_AXIS_COUNT] = { { .device = -1, .axis = main_AXIS_X }, { .device = -1, .axis = main_AXIS_Z } };
static TSspi_config_t TSspi_config;
static bool TSspi_initialized;
static pthread_mutex_t TSspi_mutex = PTHREAD_MUTEX_INITIALIZER;
static main_axis_command_t TSspi_commands[TSspi_QUEUE_SIZE];
static size_t TSspi_command_head;
static size_t TSspi_command_count;
static TSspi_snapshot_t TSspi_snapshots[TSspi_AXIS_COUNT];
static main_axis_t TSspi_target_axis = main_AXIS_NONE;
static uint8_t TSspi_stop_mask;
static bool TSspi_recovery_requested;
static bool TSspi_blocked = true;

static bool TSspi_Get_Time(uint64_t * now);
static void TSspi_Set_Delay(uint32_t us);
static size_t TSspi_Get_Index(main_axis_t axis);
static bool TSspi_Get_Command(main_axis_command_t * command);
static void TSspi_Set_Stop_Requests(void);
static void TSspi_Set_Stop_All(void);
static void TSspi_Set_Fault(TSspi_axis_context_t * axis);
static void TSspi_Set_Open(TSspi_axis_context_t * axis, uint64_t now);
static void TSspi_Set_Poll(TSspi_axis_context_t * axis);
static void TSspi_Parse_Command(const main_axis_command_t * command);
static bool TSspi_Get_Wire_Command(const main_axis_command_t * command, uint8_t * cmd);
static void TSspi_Set_Start(TSspi_axis_context_t * axis, main_axis_command_code_t command, uint8_t cmd, uint8_t arg, uint64_t now);
static bool TSspi_Set_Exchange(TSspi_axis_context_t * axis, const TSspi_request_t * request);
static bool TSspi_Set_Transfer(TSspi_axis_context_t * axis, const TSspi_request_t * request, uint8_t * rx);
static uint8_t TSspi_Get_Remote(main_axis_t axis);
static bool TSspi_Check_Timeout(TSspi_axis_context_t * axis, uint64_t now);
static void TSspi_Set_Publish(TSspi_axis_context_t * axis, main_axis_event_t event, main_axis_command_code_t command);
static void TSspi_Set_Receipt(TSspi_axis_context_t * axis, const TSspi_response_t * response);
static void TSspi_Set_Request(const TSspi_request_t * frame, uint8_t * bytes);
static bool TSspi_Get_Response(const uint8_t * bytes, TSspi_response_t * frame);
static bool TSspi_Check_Response(const TSspi_response_t * frame);
static void TSspi_Set_U16(uint16_t value, uint8_t * bytes);
static uint16_t TSspi_Get_U16(const uint8_t * bytes);
static void TSspi_Set_U32(uint32_t value, uint8_t * bytes);
static uint32_t TSspi_Get_U32(const uint8_t * bytes);

/* ==================== 1. 명령 하나 처리 / 양축 NOP 조회 ==================== */
void * TSspi_Init_Task(void * argument)
{
    const TSspi_config_t config = { TSspi_POLL_MS, TSspi_HEARTBEAT_TIMEOUT_MS, TSspi_RESPONSE_TIMEOUT_MS, TSspi_MOVE_TIMEOUT_MS, TSspi_WORK_TIMEOUT_MS };
    main_axis_command_t command;
    uint64_t now;
    (void)argument;
    TSspi_Init_Communication(&config);
    if (TSspi_initialized == false)
    {
        TSspi_Set_Stop_All();
        TScontrol_Set_Fault(main_FAULT_SPI_COMMUNICATION);
        return NULL;
    }
    while (TSspi_Get_Time(&now) == true)
    {
        TSspi_Set_Stop_Requests();
        TSspi_Set_Poll(&TSspi_axes[0]);
        TSspi_Set_Poll(&TSspi_axes[1]);
        if (TSspi_Get_Command(&command) == true)
        {
            TSspi_Parse_Command(&command);
        }
        TSspi_Set_Delay(TSspi_LOOP_MS * 1000U);
    }
    TScontrol_Set_Fault(main_FAULT_SPI_COMMUNICATION);
    return NULL;
}

void TSspi_Init_Communication(const TSspi_config_t * config)
{
    uint64_t now;
    TSspi_initialized = false;
    if (config == NULL)
    {
        return;
    }
    if (config->heartbeat_period_ms == 0U)
    {
        return;
    }
    if (config->response_timeout_ms <= config->heartbeat_period_ms)
    {
        return;
    }
    if (config->heartbeat_timeout_ms <= config->heartbeat_period_ms)
    {
        return;
    }
    if (config->move_timeout_ms <= config->response_timeout_ms)
    {
        return;
    }
    if (config->work_timeout_ms <= config->response_timeout_ms)
    {
        return;
    }
    if (TSspi_Get_Time(&now) == false)
    {
        return;
    }
    TSspi_config = *config;
    TSspi_initialized = true;
    TSspi_Set_Open(&TSspi_axes[0], now);
    TSspi_Set_Open(&TSspi_axes[1], now);
}

/* ==================== 2. 응답: 수락 번호와 실제 작업 번호를 따로 확인 ==================== */
void TSspi_Parse(const TSspi_response_t * response)
{
    TSspi_axis_context_t * axis;
    uint64_t now;
    size_t index;
    if (response == NULL)
    {
        return;
    }
    if (TSspi_Check_Response(response) == false)
    {
        return;
    }
    index = TSspi_Get_Index((main_axis_t)response->axis);
    if (index == TSspi_AXIS_COUNT)
    {
        return;
    }
    axis = &TSspi_axes[index];
    if (TSspi_Get_Time(&now) == false)
    {
        TSspi_Set_Fault(axis);
        return;
    }
    if (TSspi_Check_Timeout(axis, now) == false)
    {
        return;
    }
    /* ARM 이후 BOOT가 보이면 MCU 재부팅이다. 이전 작업을 이어가지 않는다. */
    if (axis->armed == true)
    {
        if (((response->relay >> 3U) & 0x0FU) == TSspi_BOOT_STAGE)
        {
            TSspi_Set_Fault(axis);
            return;
        }
    }
    if (axis->synced == false)
    {
        axis->seq = response->ack_seq;
        axis->synced = true;
    }
    axis->valid_response = true;
    axis->last_response_ms = now;
    if (axis->heartbeat_seen == false)
    {
        axis->heartbeat_seen = true;
        axis->heartbeat_changed_ms = now;
    }
    else if (response->heartbeat != axis->status.heartbeat_counter)
    {
        axis->heartbeat_changed_ms = now;
        axis->status.heartbeat_status = main_HEARTBEAT_NORMAL;
    }
    axis->status.axis = axis->axis;
    axis->status.state = (main_axis_state_t)response->state;
    axis->status.current_position = response->position;
    axis->status.heartbeat_counter = response->heartbeat;
    axis->status.ack_seq = response->ack_seq;
    axis->status.op_seq = response->op_seq;
    axis->status.receipt = response->receipt;
    axis->status.device_fault = response->fault;
    axis->status.sensors = response->sensors;
    axis->status.relay = response->relay;
    axis->status.stage = (uint8_t)((response->relay >> 3U) & 0x0FU);
    axis->status.position_source = response->pos_src;
    axis->status.link_valid = (response->relay & TSspi_LINK_MASK) != 0U;
    axis->status.fault = main_FAULT_NONE;
    if (response->fault != 0U)
    {
        axis->status.fault = main_FAULT_SPI_COMMUNICATION;
    }
    if (response->state == TSspi_STATE_FAULT)
    {
        axis->status.fault = main_FAULT_SPI_COMMUNICATION;
    }

    /* STOP/ARM 대기 중 이전 FAULT 응답은 허용한다. 현재 요청의 receipt를 기다린다. */
    if (axis->pending != TSspi_PENDING_NONE)
    {
        TSspi_Set_Receipt(axis, response);
    }
    else if (axis->armed == true)
    {
        if (axis->status.fault != main_FAULT_NONE)
        {
            TSspi_Set_Fault(axis);
            return;
        }
    }
    if (axis->device != TSspi_INVALID_DEVICE)
    {
        TSspi_Set_Publish(axis, main_AXIS_EVENT_STATUS, main_AXIS_COMMAND_NONE);
    }
}

static void TSspi_Set_Receipt(TSspi_axis_context_t * axis, const TSspi_response_t * response)
{
    main_axis_command_code_t command = axis->command;
    if (axis->armed == true)
    {
        if (axis->status.fault != main_FAULT_NONE)
        {
            TSspi_Set_Fault(axis);
            return;
        }
    }
    if (response->ack_seq != axis->request.seq)
    {
        return;
    }
    if (response->receipt != 0U)
    {
        TSspi_Set_Fault(axis);
        return;
    }
    switch (command)
    {
        case main_AXIS_COMMAND_STOP:
            if (response->state != TSspi_STATE_UNKNOWN)
            {
                if (response->state != TSspi_STATE_FAULT)
                {
                    return;
                }
            }
            axis->pending = TSspi_PENDING_NONE;
            axis->stop_required = false;
            axis->stopped = true;
            axis->armed = false;
            TSspi_Set_Publish(axis, main_AXIS_EVENT_STOPPED, command);
            break;
        case main_AXIS_COMMAND_ARM:
            if (response->state != TSspi_STATE_IDLE)
            {
                return;
            }
            if (response->fault != 0U)
            {
                return;
            }
            axis->pending = TSspi_PENDING_NONE;
            axis->armed = true;
            axis->fault_reported = false;
            if (TSspi_axes[0].armed == true)
            {
                if (TSspi_axes[1].armed == true)
                {
                    if (pthread_mutex_lock(&TSspi_mutex) == 0)
                    {
                        if (TSspi_stop_mask == 0U)
                        {
                            TSspi_blocked = false;
                        }
                        (void)pthread_mutex_unlock(&TSspi_mutex);
                    }
                }
            }
            TSspi_Set_Publish(axis, main_AXIS_EVENT_ARMED, command);
            break;
        case main_AXIS_COMMAND_RECOVER:
        case main_AXIS_COMMAND_X_MOVE:
        case main_AXIS_COMMAND_PICK:
        case main_AXIS_COMMAND_PLACE:
            if (axis->pending == TSspi_PENDING_RECEIPT)
            {
                axis->pending = TSspi_PENDING_COMPLETE;
                TSspi_Set_Publish(axis, main_AXIS_EVENT_ACK, command);
            }
            if (response->op_seq != axis->request.seq)
            {
                return;
            }
            if (response->state != TSspi_STATE_COMPLETE)
            {
                return;
            }
            if (response->fault != 0U)
            {
                return;
            }
            axis->pending = TSspi_PENDING_NONE;
            TSspi_Set_Publish(axis, main_AXIS_EVENT_COMPLETE, command);
            break;
        case main_AXIS_COMMAND_NONE:
        case main_AXIS_COMMAND_GET_STATUS:
        default:
            break;
    }
}

/* ==================== 3. 명령 시작 / STOP 우선 처리 ==================== */
static void TSspi_Parse_Command(const main_axis_command_t * command)
{
    TSspi_axis_context_t * axis;
    uint8_t cmd;
    uint64_t now;
    size_t index = TSspi_Get_Index(command->axis);
    if (index == TSspi_AXIS_COUNT)
    {
        return;
    }
    axis = &TSspi_axes[index];
    if (command->command == main_AXIS_COMMAND_GET_STATUS)
    {
        axis->last_poll_ms = 0U;
        return;
    }
    if (TSspi_Get_Wire_Command(command, &cmd) == false)
    {
        TSspi_Set_Fault(axis);
        return;
    }
    if (axis->device == TSspi_INVALID_DEVICE)
    {
        TSspi_Set_Fault(axis);
        return;
    }
    if (axis->synced == false)
    {
        TSspi_Set_Fault(axis);
        return;
    }
    if (axis->pending != TSspi_PENDING_NONE)
    {
        TSspi_Set_Fault(axis);
        return;
    }
    if (command->command == main_AXIS_COMMAND_ARM)
    {
        if (axis->stopped == false)
        {
            TSspi_Set_Fault(axis);
            return;
        }
        if (axis->stop_required == true)
        {
            TSspi_Set_Fault(axis);
            return;
        }
    }
    else
    {
        if (axis->armed == false)
        {
            TSspi_Set_Fault(axis);
            return;
        }
        if (pthread_mutex_lock(&TSspi_mutex) != 0)
        {
            return;
        }
        if (TSspi_blocked == true)
        {
            (void)pthread_mutex_unlock(&TSspi_mutex);
            TSspi_Set_Fault(axis);
            return;
        }
        (void)pthread_mutex_unlock(&TSspi_mutex);
    }
    if (TSspi_Get_Time(&now) == false)
    {
        TSspi_Set_Fault(axis);
        return;
    }
    TSspi_Set_Start(axis, command->command, cmd, command->arg, now);
}

static void TSspi_Set_Start(TSspi_axis_context_t * axis, main_axis_command_code_t command, uint8_t cmd, uint8_t arg, uint64_t now)
{
    axis->seq = (axis->seq == UINT16_MAX) ? 1U : (uint16_t)(axis->seq + 1U);
    axis->request = (TSspi_request_t){ .cmd = cmd, .seq = axis->seq, .arg = arg };
    axis->command = command;
    axis->pending = TSspi_PENDING_RECEIPT;
    axis->status.acknowledged = false;
    axis->command_ms = now;
    if (command == main_AXIS_COMMAND_STOP)
    {
        axis->armed = false;
    }
    if (command != main_AXIS_COMMAND_ARM)
    {
        axis->stopped = false;
    }
    if (TSspi_Set_Exchange(axis, &axis->request) == false)
    {
        TSspi_Set_Fault(axis);
    }
}

static void TSspi_Set_Stop_Requests(void)
{
    uint8_t mask;
    bool recovery;
    size_t index;
    if (pthread_mutex_lock(&TSspi_mutex) != 0)
    {
        return;
    }
    mask = TSspi_stop_mask;
    recovery = TSspi_recovery_requested;
    TSspi_stop_mask = 0U;
    TSspi_recovery_requested = false;
    (void)pthread_mutex_unlock(&TSspi_mutex);
    for (index = 0U; index < TSspi_AXIS_COUNT; ++index)
    {
        if (recovery == true)
        {
            TSspi_axes[index].fault_reported = false;
            TSspi_axes[index].pending = TSspi_PENDING_NONE;
            TSspi_axes[index].stop_required = false;
        }
        if ((mask & (uint8_t)(1U << index)) != 0U)
        {
            if (TSspi_axes[index].stop_required == false)
            {
                TSspi_axes[index].pending = TSspi_PENDING_NONE;
                TSspi_axes[index].stop_required = true;
            }
        }
    }
}

static void TSspi_Set_Poll(TSspi_axis_context_t * axis)
{
    const TSspi_request_t nop = { .cmd = TSspi_CMD_NOP };
    uint64_t now;
    if (TSspi_Get_Time(&now) == false)
    {
        TSspi_Set_Fault(axis);
        return;
    }
    if (axis->device == TSspi_INVALID_DEVICE)
    {
        if ((now - axis->reopen_ms) >= TSspi_REOPEN_MS)
        {
            TSspi_Set_Open(axis, now);
        }
        return;
    }
    if ((now - axis->last_poll_ms) < TSspi_config.heartbeat_period_ms)
    {
        return;
    }
    axis->last_poll_ms = now;
    if (axis->stop_required == true)
    {
        if (axis->synced == true)
        {
            if (axis->command != main_AXIS_COMMAND_STOP)
            {
                axis->pending = TSspi_PENDING_NONE;
            }
            if (axis->pending == TSspi_PENDING_NONE)
            {
                TSspi_Set_Start(axis, main_AXIS_COMMAND_STOP, TSspi_CMD_STOP, 0U, now);
            }
            else
            {
                /* STOP만 같은 seq로 반복 가능. 일반 작업은 NOP로만 확인한다. */
                if (TSspi_Set_Exchange(axis, &axis->request) == false)
                {
                    TSspi_Set_Fault(axis);
                }
            }
        }
        else
        {
            if (TSspi_Set_Exchange(axis, &nop) == false)
            {
                /* 응답이 깨져도 STOP 송신은 시도한다. MCU는 STOP의 seq 검사를 우회한다.
                 * 확인 응답 없이는 stopped로 판단하지 않는다. */
                TSspi_Set_Start(axis, main_AXIS_COMMAND_STOP, TSspi_CMD_STOP, 0U, now);
            }
        }
    }
    else
    {
        if (TSspi_Set_Exchange(axis, &nop) == false)
        {
            if (axis->pending == TSspi_PENDING_RECEIPT)
            {
                TSspi_Set_Fault(axis);
            }
        }
    }
    if (TSspi_Get_Time(&now) == true)
    {
        (void)TSspi_Check_Timeout(axis, now);
    }
}

static bool TSspi_Check_Timeout(TSspi_axis_context_t * axis, uint64_t now)
{
    uint32_t limit;
    if (axis->device == TSspi_INVALID_DEVICE)
    {
        return false;
    }
    if ((now - axis->last_response_ms) >= TSspi_config.response_timeout_ms)
    {
        TSspi_Set_Fault(axis);
        return false;
    }
    if ((now - axis->heartbeat_changed_ms) >= TSspi_config.heartbeat_timeout_ms)
    {
        TSspi_Set_Fault(axis);
        return false;
    }
    if (axis->pending == TSspi_PENDING_NONE)
    {
        return true;
    }
    limit = TSspi_config.response_timeout_ms;
    if (axis->pending == TSspi_PENDING_COMPLETE)
    {
        limit = TSspi_config.move_timeout_ms;
        if (axis->command == main_AXIS_COMMAND_PICK)
        {
            limit = TSspi_config.work_timeout_ms;
        }
        if (axis->command == main_AXIS_COMMAND_PLACE)
        {
            limit = TSspi_config.work_timeout_ms;
        }
    }
    if ((now - axis->command_ms) >= limit)
    {
        TSspi_Set_Fault(axis);
        return false;
    }
    return true;
}

static void TSspi_Set_Fault(TSspi_axis_context_t * axis)
{
    axis->armed = false;
    axis->synced = false;
    axis->valid_response = false;
    axis->pending = TSspi_PENDING_NONE;
    axis->status.axis = axis->axis;
    axis->status.state = main_AXIS_STATE_FAULT;
    axis->status.heartbeat_status = main_HEARTBEAT_LOST;
    axis->status.fault = main_FAULT_SPI_COMMUNICATION;
    TSspi_Set_Stop_All();
    TSspi_Set_Device_Status(&axis->status);
    if (axis->fault_reported == false)
    {
        axis->fault_reported = true;
        TScontrol_Set_Fault(main_FAULT_SPI_COMMUNICATION);
    }
    if (axis->device != TSspi_INVALID_DEVICE)
    {
        (void)close(axis->device);
        axis->device = TSspi_INVALID_DEVICE;
    }
    (void)TSspi_Get_Time(&axis->reopen_ms);
}

/* ==================== 4. 외부 태스크용 큐와 캐시 ==================== */
void TSspi_Set_Axis_Command(const main_axis_command_t * command)
{
    size_t index;
    bool rejected = false;
    uint8_t wire;
    if (command == NULL)
    {
        return;
    }
    index = TSspi_Get_Index(command->axis);
    if (index == TSspi_AXIS_COUNT)
    {
        return;
    }
    if (command->command != main_AXIS_COMMAND_STOP)
    {
        if (command->command != main_AXIS_COMMAND_GET_STATUS)
        {
            if (TSspi_Get_Wire_Command(command, &wire) == false)
            {
                TSspi_Set_Stop_All();
                TScontrol_Set_Fault(main_FAULT_SPI_COMMUNICATION);
                return;
            }
        }
    }
    if (pthread_mutex_lock(&TSspi_mutex) != 0)
    {
        TScontrol_Set_Fault(main_FAULT_SPI_COMMUNICATION);
        return;
    }
    if (command->command == main_AXIS_COMMAND_STOP)
    {
        TSspi_blocked = true;
        TSspi_stop_mask |= (uint8_t)(1U << index);
        TSspi_command_head = 0U;
        TSspi_command_count = 0U;
    }
    else if (TSspi_command_count == TSspi_QUEUE_SIZE)
    {
        rejected = true;
    }
    else
    {
        TSspi_commands[(TSspi_command_head + TSspi_command_count) % TSspi_QUEUE_SIZE] = *command;
        ++TSspi_command_count;
    }
    (void)pthread_mutex_unlock(&TSspi_mutex);
    if (rejected == true)
    {
        TSspi_Set_Stop_All();
        TScontrol_Set_Fault(main_FAULT_SPI_COMMUNICATION);
    }
}

void TSspi_Set_Recovery(void)
{
    if (pthread_mutex_lock(&TSspi_mutex) != 0)
    {
        return;
    }
    TSspi_stop_mask = TSspi_STOP_ALL_MASK;
    TSspi_recovery_requested = true;
    TSspi_blocked = true;
    TSspi_command_head = 0U;
    TSspi_command_count = 0U;
    (void)pthread_mutex_unlock(&TSspi_mutex);
}

static void TSspi_Set_Stop_All(void)
{
    if (pthread_mutex_lock(&TSspi_mutex) != 0)
    {
        return;
    }
    TSspi_stop_mask = TSspi_STOP_ALL_MASK;
    TSspi_blocked = true;
    TSspi_command_head = 0U;
    TSspi_command_count = 0U;
    (void)pthread_mutex_unlock(&TSspi_mutex);
}

void TSspi_Get_Target_Axis(main_axis_t * axis)
{
    if (axis == NULL)
    {
        return;
    }
    *axis = main_AXIS_NONE;
    if (pthread_mutex_lock(&TSspi_mutex) != 0)
    {
        return;
    }
    *axis = TSspi_target_axis;
    (void)pthread_mutex_unlock(&TSspi_mutex);
}

void TSspi_Set_Device_Status(const main_axis_status_t * status)
{
    size_t index;
    if (status == NULL)
    {
        return;
    }
    index = TSspi_Get_Index(status->axis);
    if (index == TSspi_AXIS_COUNT)
    {
        return;
    }
    if (pthread_mutex_lock(&TSspi_mutex) != 0)
    {
        return;
    }
    TSspi_snapshots[index].status = *status;
    TSspi_snapshots[index].status.armed = TSspi_axes[index].armed;
    TSspi_snapshots[index].heartbeat_changed_ms = TSspi_axes[index].heartbeat_changed_ms;
    TSspi_snapshots[index].heartbeat_timeout_ms = TSspi_config.heartbeat_timeout_ms;
    (void)pthread_mutex_unlock(&TSspi_mutex);
}

void TSspi_Get_Heartbeat_Status(main_axis_t axis, main_heartbeat_status_t * status)
{
    TSspi_snapshot_t snapshot;
    size_t index = TSspi_Get_Index(axis);
    uint64_t now;
    if (status == NULL)
    {
        return;
    }
    *status = main_HEARTBEAT_UNKNOWN;
    if (index == TSspi_AXIS_COUNT)
    {
        return;
    }
    if (pthread_mutex_lock(&TSspi_mutex) != 0)
    {
        return;
    }
    snapshot = TSspi_snapshots[index];
    (void)pthread_mutex_unlock(&TSspi_mutex);
    *status = snapshot.status.heartbeat_status;
    if (snapshot.heartbeat_timeout_ms == 0U)
    {
        return;
    }
    if (TSspi_Get_Time(&now) == false)
    {
        *status = main_HEARTBEAT_LOST;
        return;
    }
    if ((now - snapshot.heartbeat_changed_ms) >= snapshot.heartbeat_timeout_ms)
    {
        *status = main_HEARTBEAT_LOST;
    }
}

static void TSspi_Set_Publish(TSspi_axis_context_t * axis, main_axis_event_t event, main_axis_command_code_t command)
{
    axis->status.command = command;
    axis->status.armed = axis->armed;
    axis->status.acknowledged = false;
    if (axis->pending == TSspi_PENDING_COMPLETE)
    {
        axis->status.acknowledged = true;
    }
    if (event == main_AXIS_EVENT_ARMED)
    {
        axis->status.acknowledged = true;
    }
    if (event == main_AXIS_EVENT_STOPPED)
    {
        axis->status.acknowledged = true;
    }
    if (event == main_AXIS_EVENT_COMPLETE)
    {
        axis->status.acknowledged = true;
    }
    TSspi_Set_Device_Status(&axis->status);
    TScontrol_Set_Axis_Event(axis->axis, event, &axis->status);
}

/* ==================== 5. Linux 장치와 2단계 교환 ==================== */
static void TSspi_Set_Open(TSspi_axis_context_t * axis, uint64_t now)
{
    const char * path = (axis->axis == main_AXIS_X) ? TSspi_X_DEVICE : TSspi_Z_DEVICE;
    const uint8_t mode = TSspi_MODE;
    const uint8_t bits = TSspi_BITS_PER_WORD;
    const uint8_t lsb_first = 0U;
    const uint32_t speed = TSspi_SPEED_HZ;
    axis->reopen_ms = now;
    if (axis->device != TSspi_INVALID_DEVICE)
    {
        return;
    }
    axis->device = open(path, O_RDWR | O_CLOEXEC);
    if (axis->device == TSspi_INVALID_DEVICE)
    {
        TSspi_Set_Fault(axis);
        return;
    }
    if (ioctl(axis->device, SPI_IOC_WR_MODE, &mode) < 0)
    {
        TSspi_Set_Fault(axis);
        return;
    }
    if (ioctl(axis->device, SPI_IOC_WR_LSB_FIRST, &lsb_first) < 0)
    {
        TSspi_Set_Fault(axis);
        return;
    }
    if (ioctl(axis->device, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0)
    {
        TSspi_Set_Fault(axis);
        return;
    }
    if (ioctl(axis->device, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
    {
        TSspi_Set_Fault(axis);
        return;
    }
    axis->synced = false;
    axis->armed = false;
    axis->stopped = false;
    axis->valid_response = false;
    axis->heartbeat_seen = false;
    axis->pending = TSspi_PENDING_NONE;
    axis->last_response_ms = now;
    axis->heartbeat_changed_ms = now;
    axis->last_poll_ms = 0U;
    axis->status = (main_axis_status_t){ .axis = axis->axis, .state = main_AXIS_STATE_UNKNOWN, .heartbeat_status = main_HEARTBEAT_UNKNOWN };
    TSspi_Set_Device_Status(&axis->status);
}

static bool TSspi_Set_Exchange(TSspi_axis_context_t * axis, const TSspi_request_t * request)
{
    const TSspi_request_t nop = { .cmd = TSspi_CMD_NOP };
    TSspi_response_t response;
    uint8_t rx[TSspi_FRAME_SIZE];
    if (pthread_mutex_lock(&TSspi_mutex) == 0)
    {
        TSspi_target_axis = axis->axis;
        (void)pthread_mutex_unlock(&TSspi_mutex);
    }
    if (TSspi_Set_Transfer(axis, request, rx) == false)
    {
        return false;
    }
    TSspi_Set_Delay(TSspi_RESPONSE_DELAY_US);
    if (TSspi_Set_Transfer(axis, &nop, rx) == false)
    {
        return false;
    }
    if (TSspi_Get_Response(rx, &response) == false)
    {
        return false;
    }
    if (response.axis != (uint8_t)axis->axis)
    {
        return false;
    }
    TSspi_Parse(&response);
    return true;
}

static bool TSspi_Set_Transfer(TSspi_axis_context_t * axis, const TSspi_request_t * frame, uint8_t * rx)
{
    TSspi_request_t request = *frame;
    uint8_t tx[TSspi_FRAME_SIZE];
    const struct spi_ioc_transfer transfer = { .tx_buf = (uint64_t)(uintptr_t)tx, .rx_buf = (uint64_t)(uintptr_t)rx, .len = TSspi_FRAME_SIZE, .speed_hz = TSspi_SPEED_HZ, .bits_per_word = TSspi_BITS_PER_WORD };
    request.magic = TSspi_REQUEST_MAGIC;
    request.version = TSspi_VERSION;
    request.axis = (uint8_t)axis->axis;
    request.remote = TSspi_Get_Remote(axis->axis);
    TSspi_Set_Request(&request, tx);
    return ioctl(axis->device, SPI_IOC_MESSAGE(1), &transfer) == (int)TSspi_FRAME_SIZE;
}

static uint8_t TSspi_Get_Remote(main_axis_t axis)
{
    size_t other = (axis == main_AXIS_X) ? 1U : 0U;
    uint8_t mask = (axis == main_AXIS_X) ? 0x01U : 0x06U;
    uint64_t now;
    if (TSspi_axes[other].valid_response == false)
    {
        return 0U;
    }
    if (TSspi_axes[other].status.link_valid == false)
    {
        return 0U;
    }
    if (TSspi_Get_Time(&now) == false)
    {
        return 0U;
    }
    if ((now - TSspi_axes[other].last_response_ms) > TSspi_REMOTE_TIMEOUT_MS)
    {
        return 0U;
    }
    return TSspi_axes[other].status.relay & mask;
}

/* ==================== 6. 보조 함수 ==================== */
static bool TSspi_Get_Wire_Command(const main_axis_command_t * command, uint8_t * cmd)
{
    if (command->target_position != 0)
    {
        return false;
    }
    if (command->option != 0U)
    {
        return false;
    }
    if (command->command != main_AXIS_COMMAND_X_MOVE)
    {
        if (command->arg != 0U)
        {
            return false;
        }
    }
    switch (command->command)
    {
        case main_AXIS_COMMAND_ARM:
            *cmd = TSspi_CMD_ARM;
            break;
        case main_AXIS_COMMAND_STOP:
            *cmd = TSspi_CMD_STOP;
            break;
        case main_AXIS_COMMAND_RECOVER:
            *cmd = TSspi_CMD_RECOVER;
            break;
        case main_AXIS_COMMAND_X_MOVE:
            if (command->axis != main_AXIS_X)
            {
                return false;
            }
            if (command->arg >= TSspi_STATION_COUNT)
            {
                return false;
            }
            *cmd = TSspi_CMD_X_MOVE;
            break;
        case main_AXIS_COMMAND_PICK:
            if (command->axis != main_AXIS_Z)
            {
                return false;
            }
            *cmd = TSspi_CMD_PICK;
            break;
        case main_AXIS_COMMAND_PLACE:
            if (command->axis != main_AXIS_Z)
            {
                return false;
            }
            *cmd = TSspi_CMD_PLACE;
            break;
        case main_AXIS_COMMAND_NONE:
        case main_AXIS_COMMAND_GET_STATUS:
        default: return false;
    }
    return true;
}

static bool TSspi_Get_Command(main_axis_command_t * command)
{
    bool available = false;
    if (pthread_mutex_lock(&TSspi_mutex) != 0)
    {
        return false;
    }
    if (TSspi_stop_mask == 0U)
    {
        if (TSspi_command_count > 0U)
        {
            *command = TSspi_commands[TSspi_command_head];
            TSspi_command_head = (TSspi_command_head + 1U) % TSspi_QUEUE_SIZE;
            --TSspi_command_count;
            available = true;
        }
    }
    (void)pthread_mutex_unlock(&TSspi_mutex);
    return available;
}

static size_t TSspi_Get_Index(main_axis_t axis)
{
    if (axis == main_AXIS_X)
    {
        return 0U;
    }
    if (axis == main_AXIS_Z)
    {
        return 1U;
    }
    return TSspi_AXIS_COUNT;
}

static bool TSspi_Get_Time(uint64_t * now)
{
    struct timespec stamp;
    if (clock_gettime(CLOCK_MONOTONIC, &stamp) != 0)
    {
        return false;
    }
    if (stamp.tv_sec < 0)
    {
        return false;
    }
    *now = ((uint64_t)stamp.tv_sec * 1000U) + ((uint64_t)stamp.tv_nsec / 1000000U);
    return true;
}

static void TSspi_Set_Delay(uint32_t us)
{
    struct timespec delay = { .tv_sec = (time_t)(us / 1000000U), .tv_nsec = (long)((us % 1000000U) * 1000U) };
    struct timespec remaining;
    while (nanosleep(&delay, &remaining) != 0)
    {
        if (errno != EINTR)
        {
            break;
        }
        delay = remaining;
    }
}

/* ==================== 7. 명시적인 wire 변환과 CRC ==================== */
static void TSspi_Set_Request(const TSspi_request_t * frame, uint8_t * bytes)
{
    size_t i;
    bytes[0] = frame->magic;
    bytes[1] = frame->version;
    bytes[2] = frame->axis;
    bytes[3] = frame->cmd;
    TSspi_Set_U16(frame->seq, &bytes[4]);
    bytes[6] = frame->arg;
    bytes[7] = frame->remote;
    TSspi_Set_U32((uint32_t)frame->target, &bytes[8]);
    TSspi_Set_U16(frame->option, &bytes[12]);
    for (i = 0U; i < 8U; ++i) { bytes[14U + i] = 0U; }
    TSspi_Set_U16(TSspi_Get_Crc(bytes, TSspi_CRC_OFFSET), &bytes[22]);
}

static bool TSspi_Get_Response(const uint8_t * bytes, TSspi_response_t * frame)
{
    uint32_t position = TSspi_Get_U32(&bytes[12]);
    frame->magic = bytes[0];
    frame->version = bytes[1];
    frame->axis = bytes[2];
    frame->state = bytes[3];
    frame->ack_seq = TSspi_Get_U16(&bytes[4]);
    frame->receipt = bytes[6];
    frame->fault = bytes[7];
    frame->sensors = bytes[8];
    frame->relay = bytes[9];
    frame->op_seq = TSspi_Get_U16(&bytes[10]);
    frame->position = (position <= (uint32_t)INT32_MAX) ? (int32_t)position : (-((int32_t)(UINT32_MAX - position)) - 1);
    frame->pos_src = bytes[16];
    frame->reserved0 = bytes[17];
    frame->heartbeat = TSspi_Get_U16(&bytes[18]);
    frame->reserved1 = TSspi_Get_U16(&bytes[20]);
    frame->crc = TSspi_Get_U16(&bytes[22]);
    if (frame->crc != TSspi_Get_Crc(bytes, TSspi_CRC_OFFSET))
    {
        return false;
    }
    return TSspi_Check_Response(frame);
}

static bool TSspi_Check_Response(const TSspi_response_t * frame)
{
    if (frame->magic != TSspi_RESPONSE_MAGIC)
    {
        return false;
    }
    if (frame->version != TSspi_VERSION)
    {
        return false;
    }
    if (frame->axis < 1U)
    {
        return false;
    }
    if (frame->axis > 2U)
    {
        return false;
    }
    if (frame->reserved0 != 0U)
    {
        return false;
    }
    if (frame->reserved1 != 0U)
    {
        return false;
    }
    if (frame->state > TSspi_STATE_FAULT)
    {
        return false;
    }
    if (frame->receipt > TSspi_ERROR_TRANSPORT)
    {
        return false;
    }
    if (frame->fault > TSspi_ERROR_TRANSPORT)
    {
        return false;
    }
    if (frame->pos_src > 3U)
    {
        return false;
    }
    if ((frame->sensors & 0xF8U) != 0U)
    {
        return false;
    }
    /* pos_src=0이면 위치값을 해석하지 않는다. 완료에는 위치를 사용하지 않는다. */
    if (frame->pos_src != 0U)
    {
        if (frame->position < 0)
        {
            return false;
        }
        if (frame->position > 1000)
        {
            return false;
        }
    }
    if (frame->pos_src == 1U)
    {
        if (frame->position == 0)
        {
            return false;
        }
        if (frame->position == 1000)
        {
            return false;
        }
    }
    if (frame->pos_src == 2U)
    {
        if (frame->position != 0)
        {
            if (frame->position != 1000)
            {
                return false;
            }
        }
    }
    return true;
}

uint16_t TSspi_Get_Crc(const uint8_t * bytes, size_t length)
{
    uint16_t crc = 0xFFFFU;
    size_t i;
    uint32_t bit;
    if (bytes == NULL)
    {
        return 0U;
    }
    for (i = 0U; i < length; ++i)
    {
        crc ^= (uint16_t)((uint16_t)bytes[i] << 8U);
        for (bit = 0U; bit < 8U; ++bit)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)(((uint32_t)crc << 1U) ^ 0x1021U);
            }
            else
            {
                crc = (uint16_t)((uint32_t)crc << 1U);
            }
        }
    }
    return crc;
}

static void TSspi_Set_U16(uint16_t value, uint8_t * bytes)
{
    bytes[0] = (uint8_t)((uint32_t)value >> 8U);
    bytes[1] = (uint8_t)(value & 0xFFU);
}
static uint16_t TSspi_Get_U16(const uint8_t * bytes)
{
    return (uint16_t)(((uint32_t)bytes[0] << 8U) | (uint32_t)bytes[1]);
}
static void TSspi_Set_U32(uint32_t value, uint8_t * bytes)
{
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)((value >> 16U) & 0xFFU);
    bytes[2] = (uint8_t)((value >> 8U) & 0xFFU);
    bytes[3] = (uint8_t)(value & 0xFFU);
}
static uint32_t TSspi_Get_U32(const uint8_t * bytes)
{
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) | ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}
