/* Linux 호출만 교체하고 실제 TSspi/TScontrol을 실행한다. 실물 장치를 열지 않는다. */
#define _POSIX_C_SOURCE 200809L
#include "../app/TScontrol.c"
#include "../app/TSspi.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint64_t TSspi_test_now;
static uint64_t TSspi_test_last_transfer_ms[2];
static TSspi_response_t TSspi_test_reply[2];
static uint16_t TSspi_test_previous_ack[2];
static bool TSspi_test_hold_ack[2];
static bool TSspi_test_freeze;
static bool TSspi_test_bad_crc;
static bool TSspi_test_bad_axis;
static bool TSspi_test_io_error;
static bool TSspi_test_short;
static bool TSspi_test_open_error;
static bool TSspi_test_reject;
static unsigned int TSspi_test_commands[2][38];
static unsigned int TSspi_test_order[256];
static size_t TSspi_test_order_count;
static unsigned int TSspi_test_completions;
static main_e84_request_t TSspi_test_request;

int __wrap_open(const char * path, int flags, ...);
int __wrap_close(int device);
int __wrap_ioctl(int device, unsigned long request, ...);
int __wrap_clock_gettime(clockid_t clock_id, struct timespec * stamp);
int __wrap_nanosleep(const struct timespec * delay, struct timespec * remaining);

/* 별도 CRC 구현과 명시적 바이트 조립으로 송신 코드를 교차 확인한다. */
static uint16_t TSspi_test_crc(const uint8_t * data, size_t size)
{
    uint32_t value = 65535U;
    size_t i;
    unsigned int b;
    for (i = 0U; i < size; ++i)
    {
        value ^= (uint32_t)data[i] * 256U;
        for (b = 0U; b < 8U; ++b)
        {
            value = (value & 32768U) ? (value * 2U) ^ 4129U : value * 2U;
            value &= 65535U;
        }
    }
    return (uint16_t)value;
}
static void TSspi_test_u16(uint8_t * bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value / 256U);
    bytes[1] = (uint8_t)(value % 256U);
}
static void TSspi_test_encode(TSspi_response_t reply, uint8_t * bytes)
{
    uint32_t position = (uint32_t)reply.position;
    memset(bytes, 0, 24U);
    bytes[0] = reply.magic; bytes[1] = reply.version; bytes[2] = reply.axis; bytes[3] = reply.state;
    TSspi_test_u16(&bytes[4], reply.ack_seq);
    bytes[6] = reply.receipt; bytes[7] = reply.fault; bytes[8] = reply.sensors; bytes[9] = reply.relay;
    TSspi_test_u16(&bytes[10], reply.op_seq);
    bytes[12] = (uint8_t)(position >> 24U); bytes[13] = (uint8_t)(position >> 16U);
    bytes[14] = (uint8_t)(position >> 8U); bytes[15] = (uint8_t)position;
    bytes[16] = reply.pos_src; bytes[17] = reply.reserved0;
    TSspi_test_u16(&bytes[18], reply.heartbeat);
    TSspi_test_u16(&bytes[20], reply.reserved1);
    TSspi_test_u16(&bytes[22], TSspi_test_crc(bytes, 22U));
}
int __wrap_open(const char * path, int flags, ...)
{
    (void)flags;
    if (TSspi_test_open_error) { errno = ENOENT; return -1; }
    assert(strcmp(path, TSspi_X_DEVICE) == 0 || strcmp(path, TSspi_Z_DEVICE) == 0);
    return strcmp(path, TSspi_X_DEVICE) == 0 ? 100 : 101;
}
int __wrap_close(int device) { assert(device == 100 || device == 101); return 0; }
int __wrap_clock_gettime(clockid_t clock_id, struct timespec * stamp)
{
    assert(clock_id == CLOCK_MONOTONIC);
    stamp->tv_sec = (time_t)(TSspi_test_now / 1000U);
    stamp->tv_nsec = (long)((TSspi_test_now % 1000U) * 1000000U);
    return 0;
}
int __wrap_nanosleep(const struct timespec * delay, struct timespec * remaining)
{
    (void)remaining;
    TSspi_test_now += (uint64_t)delay->tv_sec * 1000U + (uint64_t)delay->tv_nsec / 1000000U;
    return 0;
}
int __wrap_ioctl(int device, unsigned long request, ...)
{
    va_list arguments;
    size_t index = device == 100 ? 0U : 1U;
    va_start(arguments, request);
    if (request == SPI_IOC_MESSAGE(1))
    {
        const struct spi_ioc_transfer * transfer = va_arg(arguments, const struct spi_ioc_transfer *);
        const uint8_t * tx = (const uint8_t *)(uintptr_t)transfer->tx_buf;
        uint8_t * rx = (uint8_t *)(uintptr_t)transfer->rx_buf;
        uint16_t seq = (uint16_t)((uint16_t)tx[4] * 256U + tx[5]);
        uint8_t cmd = tx[3];
        TSspi_response_t reply;
        size_t i;
        if (TSspi_test_last_transfer_ms[index] != 0U)
        {
            assert((TSspi_test_now - TSspi_test_last_transfer_ms[index]) >= 1U);
        }
        TSspi_test_last_transfer_ms[index] = TSspi_test_now;
        assert(transfer->len == 24U && transfer->bits_per_word == 8U);
        assert(tx[0] == 0xA5U && tx[1] == 2U && tx[2] == index + 1U);
        assert(TSspi_test_crc(tx, 22U) == (uint16_t)((uint16_t)tx[22] * 256U + tx[23]));
        for (i = 8U; i < 22U; ++i) { assert(tx[i] == 0U); }
        if (TSspi_test_io_error) { va_end(arguments); errno = EIO; return -1; }
        if (cmd == 0U) { assert(seq == 0U && tx[6] == 0U); }
        else
        {
            assert(cmd < 38U && seq != 0U);
            ++TSspi_test_commands[index][cmd];
            assert(TSspi_test_order_count < 256U);
            TSspi_test_order[TSspi_test_order_count++] = (unsigned int)(index + 1U) * 256U + cmd;
            if (TSspi_test_reply[index].ack_seq != seq) { TSspi_test_previous_ack[index] = TSspi_test_reply[index].ack_seq; }
            TSspi_test_reply[index].ack_seq = seq;
            TSspi_test_reply[index].receipt = TSspi_test_reject ? 7U : 0U;
            if (!TSspi_test_reject)
            {
                if (cmd == 2U)
                {
                    TSspi_test_reply[index].state = 0U;
                    TSspi_test_reply[index].fault = 1U;
                }
                else if (cmd == 1U)
                {
                    TSspi_test_reply[index].state = 1U;
                    TSspi_test_reply[index].fault = 0U;
                    TSspi_test_reply[index].relay &= 0x87U;
                }
                else
                {
                    TSspi_test_reply[index].state = cmd == 3U ? 3U : 2U;
                    TSspi_test_reply[index].op_seq = seq;
                }
            }
        }
        reply = TSspi_test_reply[index];
        if (!TSspi_test_freeze) { reply.heartbeat = (uint16_t)(TSspi_test_now / 100U); }
        if (TSspi_test_hold_ack[index]) { reply.ack_seq = TSspi_test_previous_ack[index]; }
        if (TSspi_test_bad_axis) { reply.axis = (uint8_t)(3U - reply.axis); }
        TSspi_test_encode(reply, rx);
        if (TSspi_test_bad_crc) { rx[23] ^= 1U; }
        va_end(arguments);
        return TSspi_test_short ? 23 : 24;
    }
    if (request == SPI_IOC_WR_MAX_SPEED_HZ) { assert(*va_arg(arguments, const uint32_t *) == 1000000U); }
    else if (request == SPI_IOC_WR_MODE) { assert(*va_arg(arguments, const uint8_t *) == 0U); }
    else if (request == SPI_IOC_WR_LSB_FIRST) { assert(*va_arg(arguments, const uint8_t *) == 0U); }
    else if (request == SPI_IOC_WR_BITS_PER_WORD) { assert(*va_arg(arguments, const uint8_t *) == 8U); }
    else { assert(false); }
    va_end(arguments);
    return 0;
}
void TSe84_Set_Request(main_e84_request_t request) { TSspi_test_request = request; }
void TStcp_Set_Status(const main_status_t * status) { (void)status; }
void TStcp_Set_Command_Result(main_command_result_t result) { (void)result; }
void TStcp_Set_Complete(main_command_t command) { assert(command != main_COMMAND_NONE); ++TSspi_test_completions; }
void TStcp_Set_Alarm(main_fault_t fault) { assert(fault != main_FAULT_NONE); }
void TStcp_Get_Result(TStcp_result_t * result) { *result = TStcp_RESULT_OK; }

static void TSspi_test_drain(void)
{
    while (TScontrol_event_count > 0U || TScontrol_event_overflow)
    {
        TScontrol_event_t event;
        assert(TScontrol_Get_Event(&event));
        TScontrol_Parse(&event);
    }
    TScontrol_Process_Notifications();
}
static void TSspi_test_tick(void)
{
    main_axis_command_t command;
    TSspi_test_now += 100U;
    TSspi_Set_Stop_Requests();
    TSspi_Set_Poll(&TSspi_axes[0]);
    TSspi_Set_Poll(&TSspi_axes[1]);
    if (TSspi_Get_Command(&command)) { TSspi_Parse_Command(&command); }
    TSspi_test_drain();
}
static void TSspi_test_reset(void)
{
    const TSspi_config_t config = { 100U, 1000U, 500U, 30000U, 45000U };
    size_t index;
#if TSspi_DEBUG_ENABLED
    /* 실제 출력 스레드 없이 기록 경로만 함께 실행한다. */
    TSspi_debug_ready = true;
    TSspi_debug_head = 0U;
    TSspi_debug_count = 0U;
    TSspi_debug_dropped = 0U;
#endif
    memset(TSspi_axes, 0, sizeof(TSspi_axes));
    memset(TSspi_snapshots, 0, sizeof(TSspi_snapshots));
    memset(TSspi_test_reply, 0, sizeof(TSspi_test_reply));
    memset(TSspi_test_commands, 0, sizeof(TSspi_test_commands));
    memset(TSspi_test_hold_ack, 0, sizeof(TSspi_test_hold_ack));
    memset(TSspi_test_last_transfer_ms, 0, sizeof(TSspi_test_last_transfer_ms));
    memset(TScontrol_stop_confirmed, 0, sizeof(TScontrol_stop_confirmed));
    memset(TScontrol_axis_armed, 0, sizeof(TScontrol_axis_armed));
    TSspi_stop_mask = 0U; TSspi_recovery_requested = false; TSspi_blocked = true;
    TSspi_command_head = 0U; TSspi_command_count = 0U;
    TScontrol_event_head = 0U; TScontrol_event_count = 0U; TScontrol_event_overflow = false;
    TScontrol_notice_head = 0U; TScontrol_notice_count = 0U; TScontrol_notice_overflow = false;
    TScontrol_status = (main_status_t){ main_STATE_INIT, main_AXIS_STATE_UNKNOWN, main_AXIS_STATE_UNKNOWN, main_E84_EVENT_NONE, main_FAULT_NONE };
    TScontrol_command = main_COMMAND_NONE;
    TScontrol_move.command = main_AXIS_COMMAND_NONE;
    TSspi_test_freeze = false; TSspi_test_bad_crc = false; TSspi_test_bad_axis = false;
    TSspi_test_io_error = false; TSspi_test_short = false; TSspi_test_open_error = false; TSspi_test_reject = false;
    TSspi_test_now = 1000U; TSspi_test_completions = 0U; TSspi_test_order_count = 0U;
    TSspi_test_request = main_E84_REQUEST_NONE;
    for (index = 0U; index < 2U; ++index)
    {
        TSspi_axes[index].device = -1;
        TSspi_axes[index].axis = index == 0U ? main_AXIS_X : main_AXIS_Z;
        TSspi_test_reply[index] = (TSspi_response_t){ .magic = 0x5A, .version = 2U, .axis = (uint8_t)(index + 1U), .ack_seq = 65535U, .state = 4U, .fault = 1U, .relay = (uint8_t)(index == 0U ? 0xFEU : 0xF9U), .position = -123, .pos_src = 0U };
    }
    TSspi_Init_Communication(&config);
    TScontrol_Init_Fsm();
    TSspi_test_drain();
}
static void TSspi_test_ready(void)
{
    unsigned int i;
    TSspi_test_reset();
    for (i = 0U; i < 12U; ++i) { TSspi_test_tick(); }
    assert(TScontrol_status.state == main_STATE_IDLE);
    assert(TScontrol_status.fault == main_FAULT_NONE && !TSspi_blocked);
    assert(TSspi_test_order_count == 6U);
    assert(TSspi_test_order[0] == 0x102U && TSspi_test_order[1] == 0x202U);
    assert(TSspi_test_order[2] == 0x201U && TSspi_test_order[3] == 0x101U);
    assert(TSspi_test_order[4] == 0x203U && TSspi_test_order[5] == 0x103U);
    assert(TSspi_axes[0].seq == 3U && TSspi_axes[1].seq == 3U); /* 65535 -> 1 */
    assert(TScontrol_Is_Ready());
}
static void TSspi_test_finish(size_t index)
{
    TSspi_test_reply[index].state = 3U;
    TSspi_test_reply[index].op_seq = TSspi_axes[index].request.seq;
    TSspi_test_tick();
}
int main(void)
{
    unsigned int i;
    uint8_t bytes[24];
    TSspi_response_t decoded;
    const main_axis_command_t move = { .axis = main_AXIS_X, .command = main_AXIS_COMMAND_X_MOVE, .arg = 1U };
    const main_axis_command_t stop = { .axis = main_AXIS_X, .command = main_AXIS_COMMAND_STOP };
    const uint8_t text[] = "123456789";
    assert(TSspi_Get_Crc(text, 9U) == 0x29B1U);
    assert(TSspi_Get_Crc(text, 9U) == TSspi_test_crc(text, 9U));

    /* 한 축의 NOP 교환 직후 다음 요청을 보내도 CS HIGH 1ms가 필요하다. */
    TSspi_test_ready();
    const TSspi_request_t nop = { .cmd = TSspi_CMD_NOP };
    assert(TSspi_Set_Exchange(&TSspi_axes[0], &nop));
    assert(TSspi_Set_Exchange(&TSspi_axes[0], &nop));
    TSspi_test_drain();

    TSspi_test_ready();
    TSspi_test_encode(TSspi_test_reply[0], bytes);
    assert(TSspi_Get_Response(bytes, &decoded) && decoded.position == -123);
    bytes[17] = 1U; TSspi_test_u16(&bytes[22], TSspi_test_crc(bytes, 22U));
    assert(!TSspi_Get_Response(bytes, &decoded));
    TSspi_test_encode(TSspi_test_reply[0], bytes); bytes[1] = 1U;
    assert(!TSspi_Get_Response(bytes, &decoded));
    assert(TSspi_Get_Remote(main_AXIS_X) == 1U && TSspi_Get_Remote(main_AXIS_Z) == 6U);
    TSspi_axes[1].status.link_valid = false;
    assert(TSspi_Get_Remote(main_AXIS_X) == 0U);
    TSspi_axes[1].status.link_valid = true;
    TSspi_test_now += 301U;
    assert(TSspi_Get_Remote(main_AXIS_X) == 0U);

    /* 지연 접수, op_seq 불일치, 실제 완료, E84 순서 및 중복 완료. */
    TSspi_test_ready();
    TSspi_test_hold_ack[1] = true;
    TScontrol_Set_Command(main_COMMAND_START); TSspi_test_drain(); TSspi_test_tick();
    assert(TScontrol_status.state == main_STATE_WAFER_PICKUP);
    assert(TSspi_axes[1].pending == TSspi_PENDING_RECEIPT);
    TSspi_test_tick();
    assert(TSspi_test_commands[1][36] == 1U);
    TSspi_test_hold_ack[1] = false; TSspi_test_tick();
    assert(TSspi_axes[1].pending == TSspi_PENDING_COMPLETE);
    TSspi_test_reply[1].state = 3U; --TSspi_test_reply[1].op_seq; TSspi_test_tick();
    assert(TScontrol_status.state == main_STATE_WAFER_PICKUP);
    TSspi_test_finish(1U);
    assert(TScontrol_status.state == main_STATE_X_TRANSFER);
    TSspi_test_tick(); TSspi_test_finish(0U);
    assert(TScontrol_status.state == main_STATE_E84_PREPARE);
    assert(TSspi_test_request == main_E84_REQUEST_PREPARE);
    TScontrol_Set_E84_Event(main_E84_EVENT_READY); TSspi_test_drain(); TSspi_test_tick();
    TScontrol_Set_E84_Event(main_E84_EVENT_LOAD_REQUEST_OFF); TSspi_test_drain();
    assert(TScontrol_status.state == main_STATE_WAFER_HANDOVER);
    TSspi_test_finish(1U);
    assert(TScontrol_status.state == main_STATE_E84_COMPLETE);
    TScontrol_Set_E84_Event(main_E84_EVENT_COMPLETE); TSspi_test_drain();
    assert(TScontrol_status.state == main_STATE_COMPLETE && TSspi_test_completions == 1U);
    TSspi_test_tick(); assert(TSspi_test_completions == 1U);
    assert(TSspi_test_commands[1][36] == 1U && TSspi_test_commands[1][37] == 1U);

    /* 응답 없는 접수, 명시적 거부, 잘못된 CRC/축/전송 길이, I/O 오류. */
    for (i = 0U; i < 6U; ++i)
    {
        TSspi_test_ready();
        if (i == 0U) { TSspi_test_hold_ack[0] = true; }
        if (i == 1U) { TSspi_test_reject = true; }
        if (i == 2U) { TSspi_test_bad_crc = true; }
        if (i == 3U) { TSspi_test_bad_axis = true; }
        if (i == 4U) { TSspi_test_short = true; }
        if (i == 5U) { TSspi_test_io_error = true; }
        TSspi_Set_Axis_Command(&move);
        TSspi_test_tick();
        for (unsigned int j = 0U; j < 6U; ++j) { TSspi_test_tick(); }
        assert(TScontrol_status.state == main_STATE_FAULT && TSspi_blocked);
        assert(TSspi_test_commands[0][16] <= 1U);
    }
    TSspi_test_ready();
    TSspi_test_freeze = true;
    for (i = 0U; i < 13U; ++i) { TSspi_test_tick(); }
    assert(TScontrol_status.state == main_STATE_FAULT);

    /* 일반 큐가 가득 차도 STOP은 별도 비트로 보관하고 큐를 지운다. */
    TSspi_test_ready();
    for (i = 0U; i < TSspi_QUEUE_SIZE; ++i) { TSspi_Set_Axis_Command(&move); }
    TSspi_Set_Axis_Command(&stop);
    assert(TSspi_command_count == 0U && TSspi_stop_mask != 0U);
    TSspi_test_tick();
    assert(TSspi_test_commands[0][16] == 0U && TSspi_axes[0].stopped);

    TSspi_test_ready();
    TSspi_test_reply[0].relay = 0xFEU; /* ARM 후 BOOT: 자동 작업 재개 금지 */
    TSspi_test_tick();
    assert(TScontrol_status.state == main_STATE_FAULT && TSspi_blocked);
    TSspi_test_reply[0].relay = 0x86U;
    for (i = 0U; i < 15U; ++i) { TSspi_test_tick(); }
    assert(TScontrol_status.state == main_STATE_FAULT);
    TScontrol_Set_Command(main_COMMAND_RECOVER); TSspi_test_drain();
    for (i = 0U; i < 12U; ++i) { TSspi_test_tick(); }
    assert(TScontrol_status.state == main_STATE_IDLE && TScontrol_status.fault == main_FAULT_NONE);

    TSspi_test_reset(); TSspi_test_hold_ack[1] = true;
    for (i = 0U; i < 4U; ++i) { TSspi_test_tick(); }
    assert(TSspi_test_commands[0][1] == 0U && TSspi_test_commands[1][1] == 0U);
    TSspi_test_hold_ack[1] = false;
    for (i = 0U; i < 8U; ++i) { TSspi_test_tick(); }
    assert(TScontrol_status.state == main_STATE_IDLE);
    /* STOP 접수 미확인 시 ARM 금지. 통신이 돌아와도 명시적 복구 전에는 고장 유지. */
    TSspi_test_reset(); TSspi_test_hold_ack[1] = true;
    for (i = 0U; i < 8U; ++i) { TSspi_test_tick(); }
    assert(TScontrol_status.state == main_STATE_FAULT);
    assert(TSspi_test_commands[0][1] == 0U && TSspi_test_commands[1][1] == 0U);

    /* 완료 제한시간은 접수 시각부터 새로 세지 않고 최초 요청 시각부터 센다. */
    for (i = 0U; i < 2U; ++i)
    {
        main_axis_command_t work = { .axis = main_AXIS_Z, .command = main_AXIS_COMMAND_PICK };
        uint64_t limit = 45000U;
        TSspi_test_ready();
        if (i == 0U) { work = move; limit = 30000U; }
        TSspi_Set_Axis_Command(&work); TSspi_test_tick();
        size_t index = work.axis == main_AXIS_X ? 0U : 1U;
        TSspi_axes[index].command_ms = TSspi_test_now - limit + 2U;
        assert(TSspi_Check_Timeout(&TSspi_axes[index], TSspi_test_now));
        TSspi_test_now += 2U;
        assert(!TSspi_Check_Timeout(&TSspi_axes[index], TSspi_test_now));
        TSspi_test_drain();
        assert(TScontrol_status.state == main_STATE_FAULT);
    }
    TSspi_test_ready();
    TSspi_test_now = 6553500U;
    for (i = 0U; i < 2U; ++i)
    {
        TSspi_axes[i].last_response_ms = TSspi_test_now;
        TSspi_axes[i].heartbeat_changed_ms = TSspi_test_now;
        TSspi_axes[i].status.heartbeat_counter = 65535U;
    }
    TSspi_test_tick();
    assert(TSspi_axes[0].status.heartbeat_counter == 0U);
    assert(TSspi_axes[0].status.heartbeat_status == main_HEARTBEAT_NORMAL);
    assert(TScontrol_status.state == main_STATE_IDLE);

    TSspi_test_ready();
    for (i = 0U; i <= TSspi_QUEUE_SIZE; ++i) { TSspi_Set_Axis_Command(&move); }
    TSspi_test_drain();
    assert(TSspi_command_count == 0U && TSspi_stop_mask == 3U);
    assert(TScontrol_status.state == main_STATE_FAULT);
    TSspi_test_tick();
    assert(TSspi_test_commands[0][16] == 0U);

    TSspi_test_ready();
    TScontrol_Set_Command(main_COMMAND_STOP); TSspi_test_drain(); TSspi_test_tick();
    assert(TScontrol_status.fault == main_FAULT_STOP);
    assert(TSspi_axes[0].stopped && TSspi_axes[1].stopped);
    TScontrol_Set_Command(main_COMMAND_START); TSspi_test_drain();
    assert(TScontrol_status.state == main_STATE_FAULT);

    TSspi_test_reset(); TSspi_test_bad_crc = true;
    TSspi_test_tick();
    assert(TSspi_test_commands[0][2] != 0U && TSspi_test_commands[1][2] != 0U);
    assert(!TSspi_axes[0].stopped && !TSspi_axes[1].stopped);
    assert(TScontrol_status.state == main_STATE_FAULT);

    TSspi_test_reset();
    TSspi_test_reply[0].ack_seq = 100U; TSspi_test_reply[1].ack_seq = 500U;
    for (i = 0U; i < 12U; ++i) { TSspi_test_tick(); }
    assert(TSspi_axes[0].seq == 103U && TSspi_axes[1].seq == 503U);
    assert(TScontrol_status.state == main_STATE_IDLE);

    puts("SPI v2/FSM: CRC, 24-byte wire, startup/recovery, delayed ACK, op_seq, PICK/PLACE, STOP, faults PASS");
    return 0;
}
