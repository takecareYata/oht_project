/* 실제 TCP 파서와 STOP 처리기를 실행한다. 네트워크/실물 SPI는 사용하지 않는다. */
#include "../app/TStcp.h"
#include "../app/TScontrol.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static unsigned int TStcp_test_command_count;
static main_command_t TStcp_test_command = main_COMMAND_NONE;

void TScontrol_Set_Command(main_command_t command)
{
    TStcp_test_command = command;
    ++TStcp_test_command_count;
}

void TScontrol_Process_Notifications(void)
{
    /* 이 시험에서는 TCP 수신 루프를 실행하지 않는다. */
}

int main(void)
{
    TStcp_frame_t frame = { TStcp_MSGID_GET_STOP, TStcp_PAYLOAD_SIZE, 0U, 0U, 0U };
    TStcp_result_t result;
    const uint32_t payloads[] = { 0U, (uint32_t)main_COMMAND_START, (uint32_t)main_COMMAND_RECOVER, UINT32_MAX };
    size_t index;

    for (index = 0U; index < (sizeof(payloads) / sizeof(payloads[0])); ++index)
    {
        frame.payload = payloads[index];
        frame.crc = TStcp_Get_Crc(&frame);
        TStcp_Parse(&frame);
        TStcp_Get_Result(&result);
        assert(result == TStcp_RESULT_OK);
        assert(TStcp_test_command == main_COMMAND_STOP);
        assert(TStcp_test_command_count == (index + 1U));
    }
    TStcp_Get_Stop(NULL);
    TStcp_Get_Result(&result);
    assert(result == TStcp_RESULT_INVALID_ARGUMENT);
    frame.len = 0U;
    TStcp_Parse(&frame);
    TStcp_Get_Result(&result);
    assert(result == TStcp_RESULT_INVALID_ARGUMENT);
    frame.len = TStcp_PAYLOAD_SIZE;
    frame.msgid = TStcp_MSGID_GET_COMMAND;
    TStcp_Get_Stop(&frame);
    TStcp_Get_Result(&result);
    assert(result == TStcp_RESULT_INVALID_ARGUMENT);
    assert(TStcp_test_command_count == 4U);
    frame.msgid = TStcp_MSGID_GET_STOP;
    TStcp_Parse(&frame);
    TStcp_Get_Result(&result);
    assert(result == TStcp_RESULT_OK && TStcp_test_command_count == 5U);
    puts("TCP STOP: dispatch, forced STOP for any payload, NULL/length/msgid checks PASS");
    return 0;
}
