#include "control.h"
#include <stddef.h>
static uint32_t comm_last_rx;
static bool comm_link_seen, comm_pending, comm_have_seq, comm_transport_error;
static uint16_t comm_last_seq, comm_ack_seq;
static uint8_t comm_last_cmd, comm_last_arg, comm_remote, comm_result, comm_last_result;
static uint32_t comm_last_target;
static control_command_t comm_command;
static uint16_t comm_Get_U16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8U) | p[1]);
}
static void comm_Set_U16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8U);
    p[1] = (uint8_t)value;
}
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
void comm_Set_Receipt(control_error_t error)
{
    comm_result = (uint8_t)error;
    comm_last_result = comm_result;
}
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
    if (!spislave_Get_Frame(frame)) { return; }
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
    /* A structurally valid STOP always stops, even with old/zero sequence. */
    if (frame[3] != CONTROL_CMD_STOP)
    {
        if (seq == 0U) { comm_result = CONTROL_ERR_SEQUENCE; return; }
        if (comm_have_seq && (delta == 0U))
        {
            comm_result = ((frame[3] == comm_last_cmd) && (frame[6] == comm_last_arg) &&
                           (target == comm_last_target)) ? comm_last_result : CONTROL_ERR_SEQUENCE;
            return;
        }
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
bool comm_Get_Command(control_command_t *command)
{
    bool ready = comm_pending && (command != NULL);
    if (ready) { *command = comm_command; comm_pending = false; }
    return ready;
}
bool comm_Get_Link(void)
{
    return comm_link_seen && ((uint32_t)(timer_Get_Tick_Ms() - comm_last_rx) < CONFIG_COMM_TIMEOUT_MS);
}
uint8_t comm_Get_Remote(void) { return comm_Get_Link() ? comm_remote : 0U; }
bool comm_Get_Transport_Error(void)
{
    bool error = comm_transport_error;
    comm_transport_error = false;
    return error;
}
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
