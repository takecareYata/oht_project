/* ============================================================================
 * comm.c — SPI 프로토콜 v2 (24바이트) 처리 (L3)
 *
 * 요청 검사, seq 중복·역행 처리, 링크(통신) 감시, 응답 프레임 생성.
 * 모든 다중 바이트 값과 CRC는 big endian. 상세 규약: jetson_STM32_v5_24바이트_제안.md
 *
 * 요청: A5 ver axis cmd | seq(2) arg remote | target(4) | 0×10 | CRC(2)
 * 응답: 5A ver axis state | ack_seq(2) receipt fault | sensors relay | op_seq(2) |
 *       position(4) pos_src 0 | heartbeat(2) 0 0 | CRC(2)
 * ============================================================================ */
#include "control.h"
#include <stddef.h>
static uint32_t comm_last_rx;  /* 마지막 유효 요청 수신 시각 (링크 판정) */
/* link_seen: 유효 요청을 한 번이라도 받음, pending: controller가 가져갈 명령 있음,
 * have_seq: seq 이력 있음, transport_error: SPI 전송 오류 발생 */
static bool comm_link_seen, comm_pending, comm_have_seq, comm_transport_error;
static uint16_t comm_last_seq, comm_ack_seq; /* 마지막 처리 seq / 응답에 싣는 ack_seq */
/* 중복 요청 판정용 이력(cmd/arg/target), 최신 remote 비트, 접수 결과(receipt) */
static uint8_t comm_last_cmd, comm_last_arg, comm_remote, comm_result, comm_last_result;
static uint32_t comm_last_target;
static control_command_t comm_command; /* 접수된 명령 (controller로 전달) */
/* big endian 2바이트 → uint16 */
static uint16_t comm_Get_U16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8U) | p[1]);
}
/* uint16 → big endian 2바이트 */
static void comm_Set_U16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8U);
    p[1] = (uint8_t)value;
}
/* CRC-16/CCITT-FALSE (다항식 0x1021, 초기값 0xFFFF). "123456789" → 0x29B1. */
uint16_t comm_Get_Crc(const uint8_t *data, uint32_t size)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t j;
    for (i = 0U; i < size; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (j = 0U; j < 8U; j++)
        {
            crc =
                ((crc & 0x8000U) != 0U) ? (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}
/* 부팅 시: 링크·seq 이력 초기화 (통신 미연결 상태로 시작). */
void comm_Init_Link(void)
{
    comm_link_seen = false;
    comm_pending = false;
    comm_have_seq = false;
    comm_transport_error = false;
    comm_remote = 0U;
    comm_result = 0U;
    comm_ack_seq = 0U;
    comm_last_seq = 0U;
    comm_last_rx = 0U;
}
/* 접수 결과(receipt) 기록. 응답 byte6에 실린다. */
void comm_Set_Receipt(control_error_t error)
{
    comm_result = (uint8_t)error;
    comm_last_result = comm_result;
}
/* 매 루프: 새 요청 프레임 처리.
 * 1) 헤더(A5 02 axis)·remote 예약 비트·CRC·reserved(12~21)가 틀리면 조용히 버림
 * 2) NOP: 링크·remote만 갱신 (seq/arg/target은 0이어야 함)
 * 3) seq 검사: 0 / 같은 번호(동일 내용이면 이전 결과 반환) / 역행이면 SEQUENCE
 * 4) 접수 판정(controller_Get_Receipt) 후 OK 또는 STOP이면 실행 대기열에 넣음 */
void comm_Set_Update(void)
{
    uint8_t frame[COMM_FRAME_SIZE];
    uint16_t seq, delta;
    uint32_t target;
    uint8_t i;
    if (spislave_Get_Error())
    {
        comm_transport_error = true;
    }
    if (!spislave_Get_Frame(frame)) { return; } /* 새 프레임 없음 */
    if ((frame[0] != 0xA5U) || (frame[1] != COMM_VERSION) ||
        (frame[2] != CONFIG_AXIS_ID) || ((frame[7] & 0xF8U) != 0U) ||
        (comm_Get_Crc(frame, 22U) != comm_Get_U16(&frame[22]))) { return; }
    for (i = 12U; i < 22U; i++)
    {
        if (frame[i] != 0U) { return; }
    }
    seq = comm_Get_U16(&frame[4]);
    target = ((uint32_t)frame[8] << 24U) | ((uint32_t)frame[9] << 16U) |
             ((uint32_t)frame[10] << 8U) | frame[11];
    if (frame[3] == CONTROL_CMD_POLL)
    {
        if ((seq != 0U) || (frame[6] != 0U) || (target != 0U)) { return; }
        comm_last_rx = timer_Get_Tick_Ms();
        comm_link_seen = true;
        comm_remote = frame[7];
        return;
    }
    comm_last_rx = timer_Get_Tick_Ms();
    comm_link_seen = true;
    comm_remote = frame[7];
    comm_ack_seq = seq;
    delta = (uint16_t)(seq - comm_last_seq);
    /* 형식이 맞는 STOP은 seq가 오래됐거나 0이어도 항상 정지시킨다. */
    if (frame[3] != CONTROL_CMD_STOP)
    {
        if (seq == 0U) { comm_result = CONTROL_ERR_SEQUENCE; return; }
        if (comm_have_seq && (delta == 0U))
        {
            comm_result = ((frame[3] == comm_last_cmd) && (frame[6] == comm_last_arg) &&
                           (target == comm_last_target)) ? comm_last_result : CONTROL_ERR_SEQUENCE;
            return;
        }
        /* 16비트 차이 1~32767 = 새 요청, 그 이상 = 과거 요청 */
        if (comm_have_seq && (delta >= 0x8000U))
        {
            comm_result = CONTROL_ERR_SEQUENCE;
            return;
        }
    }
    comm_have_seq = true;
    comm_last_seq = seq;
    comm_last_cmd = frame[3];
    comm_last_arg = frame[6];
    comm_last_target = target;
    if (frame[3] == CONTROL_CMD_STOP)
    {
        comm_Set_Receipt(((target == 0U) && (frame[6] == 0U)) ? CONTROL_OK : CONTROL_ERR_COMMAND);
    }
    else if (target != 0U)
    {
        comm_Set_Receipt(CONTROL_ERR_COMMAND);
    }
    else
    {
        comm_Set_Receipt(controller_Get_Receipt(frame[3], frame[6]));
    }
    if ((comm_result == CONTROL_OK) || (frame[3] == CONTROL_CMD_STOP))
    {
        comm_command = (control_command_t){seq, frame[3], frame[6], frame[7]};
        comm_pending = true;
    }
}
/* controller: 접수된 명령이 있으면 꺼내 간다. */
bool comm_Get_Command(control_command_t *command)
{
    bool ready = comm_pending && (command != NULL);
    if (ready) { *command = comm_command; comm_pending = false; }
    return ready;
}
/* 통신 정상: 최근 CONFIG_COMM_TIMEOUT_MS(500 ms) 안에 유효 요청을 받음. */
bool comm_Get_Link(void)
{
    return comm_link_seen && ((uint32_t)(timer_Get_Tick_Ms() - comm_last_rx) < CONFIG_COMM_TIMEOUT_MS);
}
/* 최신 remote 비트. 통신이 끊기면 0(아무것도 허용 안 함). */
uint8_t comm_Get_Remote(void) { return comm_Get_Link() ? comm_remote : 0U; }
/* SPI 전송 오류가 있었는지 읽고 지운다. */
bool comm_Get_Transport_Error(void)
{
    bool error = comm_transport_error;
    comm_transport_error = false;
    return error;
}
/* 매 루프: 다음 응답 프레임(24바이트) 생성 후 spislave에 게시.
 * relay 비트 — Z 보드: bit0 = Z 상단 & 정지 / X 보드: bit1 = 정지, bit2 = 끝 센서 위
 * Jetson은 이 relay를 상대 보드 요청의 remote로 넘겨 준다. */
void comm_Set_Response(void)
{
    uint8_t frame[COMM_FRAME_SIZE] = {0U};
    uint8_t relay = 0U;
    uint32_t position = (uint32_t)position_Get_Value();
    bool valid = sensor_Get_Ready() && !(sensor_Get_State(0U) && sensor_Get_State(1U));
#if BOARD_ID == 1
    if (valid && sensor_Get_State(0U) && (status_Get_State() != CONTROL_BUSY))
    { relay = CONTROL_REMOTE_Z_UP; }
#else
    if (status_Get_State() != CONTROL_BUSY) { relay = CONTROL_REMOTE_X_STOPPED; }
    if (valid && (sensor_Get_State(0U) || sensor_Get_State(1U)))
    { relay |= CONTROL_REMOTE_X_STATION; }
#endif
    /* 0~3 헤더·state, 4~7 ack_seq·receipt·fault, 8~9 sensors·relay/stage/link,
     * 10~11 op_seq, 12~16 position·pos_src, 18~19 heartbeat, 22~23 CRC */
    frame[0] = 0x5AU;
    frame[1] = COMM_VERSION;
    frame[2] = CONFIG_AXIS_ID;
    frame[3] = (uint8_t)status_Get_State();
    comm_Set_U16(&frame[4], comm_ack_seq);
    frame[6] = comm_result;
    frame[7] = (uint8_t)status_Get_Error();
    frame[8] = sensor_Get_Bits();
    frame[9] = (uint8_t)(relay | (uint8_t)(status_Get_Stage() << 3U) |
                         (comm_Get_Link() ? 0x80U : 0U));
    comm_Set_U16(&frame[10], status_Get_Sequence());
    frame[12] = (uint8_t)(position >> 24U);
    frame[13] = (uint8_t)(position >> 16U);
    frame[14] = (uint8_t)(position >> 8U);
    frame[15] = (uint8_t)position;
    frame[16] = position_Get_Source();
    comm_Set_U16(&frame[18], status_Get_Heartbeat());
    comm_Set_U16(&frame[22], comm_Get_Crc(frame, 22U));
    spislave_Set_Response(frame);
}
