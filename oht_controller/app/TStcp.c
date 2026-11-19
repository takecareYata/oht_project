#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include "TStcp.h"
#include "TScontrol.h"
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stddef.h>
#if TStcp_DEBUG_ENABLED
#include <string.h>
#endif
#include <sys/socket.h>
#include <unistd.h>

/* ==================== 프레임 바이트 위치 ==================== */
#define TStcp_NOTIFICATION_POLL_MS (100)
#define TStcp_LEN_OFFSET (TStcp_MSGID_SIZE)
#define TStcp_PAYLOAD_OFFSET (TStcp_LEN_OFFSET + TStcp_LEN_SIZE)
#define TStcp_RESERVED_OFFSET (TStcp_PAYLOAD_OFFSET + TStcp_PAYLOAD_SIZE)
#define TStcp_CRC_OFFSET (TStcp_RESERVED_OFFSET + TStcp_RESERVED_SIZE)
#define TStcp_STATUS_RESERVED_OFFSET (TStcp_PAYLOAD_OFFSET + TStcp_STATUS_PAYLOAD_SIZE)
#define TStcp_STATUS_CRC_OFFSET (TStcp_STATUS_RESERVED_OFFSET + TStcp_RESERVED_SIZE)

/* ==================== TCP 태스크 전용 연결 및 처리 상태 ==================== */
typedef struct
{
    int client_socket;
    TStcp_result_t last_result;
} TStcp_context_t;

static TStcp_context_t TStcp_context =
{
    .client_socket = TStcp_INVALID_SOCKET,
    .last_result = TStcp_RESULT_OK
};

/* ==================== 내부 함수 선언 및 디버그 전처리 ==================== */
static uint16_t TStcp_Get_U16(const uint8_t * bytes);
static uint32_t TStcp_Get_U32(const uint8_t * bytes);
static void TStcp_Get_Frame(const uint8_t * bytes, TStcp_frame_t * frame);
static void TStcp_Set_U16(uint16_t value, uint8_t * bytes);
static void TStcp_Set_U32(uint32_t value, uint8_t * bytes);
static void TStcp_Set_Frame(const TStcp_frame_t * frame, uint8_t * bytes);
static void TStcp_Set_Reconnect_Delay(void);
static void TStcp_Set_Send_Bytes(const uint8_t * bytes, size_t length);
static uint16_t TStcp_Get_Crc_Bytes(const uint8_t * bytes, size_t length);
#if TStcp_DEBUG_ENABLED
static void TStcp_Debug_Write(const char * message, size_t length);
static void TStcp_Debug_Message(const char * message);
static void TStcp_Debug_Value(const char * label, uint32_t value);
#else
/* 끈 상태에서는 인수도 평가하지 않는다. 디버그 인수에 부수 효과를 넣지 않는다. */
#define TStcp_Debug_Message(message) ((void)0)
#define TStcp_Debug_Value(label, value) ((void)0)
#endif

/* ==================== 태스크 실행 및 TCP 연결·재접속 관리 ==================== */
void * TStcp_Init_Task(void * argument)
{
    int client_socket;

    (void)argument;
    /* 메인 스레드는 이 태스크를 join하며 프로세스 수명을 유지한다. */
    while (true)
    {
        TStcp_Debug_Message("TStcp connecting to " TStcp_SERVER_ADDRESS ":"
                        TStcp_SERVER_PORT "\n");
        client_socket = TStcp_Init_Communication();
        if (client_socket != TStcp_INVALID_SOCKET)
        {
            TStcp_Debug_Message("TStcp connected\n");
            /* EOF/수신/송신 오류 시 이 함수가 기존 소켓을 닫는다. */
            (void)TStcp_Init_Client();
        }

        TStcp_Debug_Message("TStcp reconnect wait\n");
        /* 연결 실패 또는 종료 후에도 재접속한다. CPU를 소모하는 빈 루프를 피한다. */
        TStcp_Set_Reconnect_Delay();
    }
}

static void TStcp_Set_Reconnect_Delay(void)
{
    uint32_t remaining_seconds = TStcp_RECONNECT_DELAY_SECONDS;

    /* 신호로 sleep이 중단되면 남은 대기시간을 채운다. */
    while (remaining_seconds > 0U)
    {
        remaining_seconds = sleep(remaining_seconds);
    }
}

int TStcp_Init_Communication(void)
{
    const struct addrinfo hints =
    {
        .ai_flags = 0,
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = 0,
        .ai_addrlen = 0U,
        .ai_addr = NULL,
        .ai_canonname = NULL,
        .ai_next = NULL
    };
    struct addrinfo * addresses = NULL;
    const struct addrinfo * address;
    int client_socket = TStcp_context.client_socket;
    int candidate_socket;
    int ret;
    bool close_failed = false;

    /* DEV-SOCKET-01: getaddrinfo가 제공하는 sockaddr를 형변환 없이 사용한다. */
    if (client_socket == TStcp_INVALID_SOCKET)
    {
        ret = getaddrinfo(TStcp_SERVER_ADDRESS, TStcp_SERVER_PORT, &hints, &addresses);

        if (ret == 0)
        {
            address = addresses;
            while ((address != NULL) && (client_socket == TStcp_INVALID_SOCKET) &&
                   (close_failed == false))
            {
                candidate_socket = socket(address->ai_family, address->ai_socktype,
                                          address->ai_protocol);
                if (candidate_socket != TStcp_INVALID_SOCKET)
                {
                    ret = connect(candidate_socket, address->ai_addr, address->ai_addrlen);
                    if (ret == 0)
                    {
                        client_socket = candidate_socket;
                    }
                    else
                    {
                        /* Linux에서는 close 오류에도 같은 FD를 다시 close하지 않는다. */
                        ret = close(candidate_socket);
                        if (ret != 0)
                        {
                            close_failed = true;
                        }
                    }
                }
                address = address->ai_next;
            }
            freeaddrinfo(addresses);
        }
    }

    TStcp_context.client_socket = client_socket;
    if (client_socket == TStcp_INVALID_SOCKET)
    {
        TStcp_context.last_result = TStcp_RESULT_CONNECT_ERROR;
    }
    else
    {
        TStcp_context.last_result = TStcp_RESULT_OK;
    }
    return client_socket;
}

/* ==================== 공통 TCP 수신·송신 처리 ==================== */
TStcp_result_t TStcp_Init_Client(void)
{
    const int client_socket = TStcp_context.client_socket;
    uint8_t bytes[TStcp_FRAME_SIZE] = { 0U };
    TStcp_frame_t frame = { 0U, 0U, 0U, 0U, 0U };
    size_t received_size = 0U;
    ssize_t received;
    TStcp_result_t result = TStcp_RESULT_INVALID_ARGUMENT;
    TStcp_result_t parse_result;
    bool receiving = true;
    struct pollfd descriptor = { client_socket, POLLIN, 0 };
    int ready;
    int ret;

    if (client_socket >= 0)
    {
        result = TStcp_RESULT_OK;
        while (receiving == true)
        {
            /* TCP 소켓/처리 결과는 이 태스크만 접근한다. FSM 생산자는 알림 큐만 쓴다. */
            TScontrol_Process_Notifications();
            parse_result = TStcp_context.last_result;
            if ((parse_result != TStcp_RESULT_OK) &&
                (parse_result != TStcp_RESULT_UNSUPPORTED_MSGID))
            {
                result = parse_result;
                receiving = false;
                continue;
            }
            /* 수신이 없어도 알림을 송신하고, 부분 프레임 대기 중에도 큐를 처리한다. */
            ready = poll(&descriptor, 1U, TStcp_NOTIFICATION_POLL_MS);
            if (ready < 0)
            {
                if (errno != EINTR)
                {
                    result = TStcp_RESULT_RECEIVE_ERROR;
                    receiving = false;
                }
                continue;
            }
            if (ready == 0)
            {
                continue;
            }
            if ((descriptor.revents & POLLNVAL) != 0)
            {
                result = TStcp_RESULT_RECEIVE_ERROR;
                receiving = false;
                continue;
            }
            /* TCP는 바이트 스트림이므로 완성되지 않은 프레임은 다음 recv와 합친다. */
            received = recv(client_socket, &bytes[received_size],TStcp_FRAME_SIZE - received_size, 0);
            if (received > 0)
            {
                TStcp_Debug_Value("TStcp recv bytes=", (uint32_t)received);
                received_size += (size_t)received;
                if (received_size == TStcp_FRAME_SIZE)
                {
                    TStcp_Get_Frame(bytes, &frame);
                    TStcp_Debug_Value("TStcp rx msgid=", (uint32_t)frame.msgid);
                    TStcp_Debug_Value("TStcp rx len=", frame.len);
                    TStcp_Debug_Value("TStcp rx payload=", frame.payload);
                    TStcp_Parse(&frame);
                    parse_result = TStcp_context.last_result;
                    if ((parse_result != TStcp_RESULT_OK) &&
                        (parse_result != TStcp_RESULT_UNSUPPORTED_MSGID))
                    {
                        result = parse_result;
                        receiving = false;
                    }
                    /* 미지원 ID는 이 프레임만 버리고 다음 프레임을 수신한다. */
                    received_size = 0U;
                }
            }
            else if (received == 0)
            {
                TStcp_Debug_Message("TStcp peer closed connection\n");
                if (received_size != 0U)
                {
                    result = TStcp_RESULT_INCOMPLETE_FRAME;
                }
                receiving = false;
            }
            else
            {
                const int receive_error = errno;

                if (receive_error != EINTR)
                {
                    TStcp_Debug_Value("TStcp recv errno=", (uint32_t)receive_error);
                    result = TStcp_RESULT_RECEIVE_ERROR;
                    receiving = false;
                }
            }
        }

        ret = close(client_socket);
        TStcp_context.client_socket = TStcp_INVALID_SOCKET;
        if ((ret != 0) && (result == TStcp_RESULT_OK))
        {
            result = TStcp_RESULT_CLOSE_ERROR;
        }
    }

    TStcp_context.last_result = result;
    return result;
}

void TStcp_Send(const TStcp_frame_t * frame)
{
    uint8_t bytes[TStcp_FRAME_SIZE] = { 0U };

    if (frame != NULL)
    {
        TStcp_Set_Frame(frame, bytes);
        TStcp_Set_Send_Bytes(bytes, sizeof(bytes));
    }
    else
    {
        TStcp_context.last_result = TStcp_RESULT_INVALID_ARGUMENT;
    }
}

static void TStcp_Set_Send_Bytes(const uint8_t * bytes, size_t length)
{
    const int client_socket = TStcp_context.client_socket;
    size_t sent_size = 0U;
    ssize_t sent;
    TStcp_result_t result = TStcp_RESULT_INVALID_ARGUMENT;

    if ((client_socket >= 0) && (bytes != NULL) && (length > 0U))
    {
        result = TStcp_RESULT_OK;
        while ((sent_size < length) && (result == TStcp_RESULT_OK))
        {
            /* MSG_NOSIGNAL로 상대 종료 시 SIGPIPE에 의한 프로세스 종료를 막는다. */
            sent = send(client_socket, &bytes[sent_size],
                        length - sent_size, MSG_NOSIGNAL);
            if (sent > 0)
            {
                sent_size += (size_t)sent;
            }
            else if (sent == 0)
            {
                result = TStcp_RESULT_SEND_ERROR;
            }
            else
            {
                const int send_error = errno;

                if (send_error != EINTR)
                {
                    TStcp_Debug_Value("TStcp send errno=", (uint32_t)send_error);
                    result = TStcp_RESULT_SEND_ERROR;
                }
            }
        }
        if (result == TStcp_RESULT_OK)
        {
            TStcp_Debug_Value("TStcp sent bytes=", (uint32_t)sent_size);
        }
    }

    TStcp_context.last_result = result;
}

void TStcp_Get_Result(TStcp_result_t * result)
{
    if (result != NULL)
    {
        *result = TStcp_context.last_result;
    }
}

/* ==================== 프레임 직렬화·역직렬화 및 바이트 순서 변환 ==================== */
static uint16_t TStcp_Get_U16(const uint8_t * bytes)
{
    return (uint16_t)(((uint32_t)bytes[0] << 8U) | (uint32_t)bytes[1]);
}

static void TStcp_Get_Frame(const uint8_t * bytes, TStcp_frame_t * frame)
{
    frame->msgid = TStcp_Get_U16(bytes);
    frame->len = TStcp_Get_U32(&bytes[TStcp_LEN_OFFSET]);
    frame->payload = TStcp_Get_U32(&bytes[TStcp_PAYLOAD_OFFSET]);
    frame->reserved = TStcp_Get_U16(&bytes[TStcp_RESERVED_OFFSET]);
    frame->crc = TStcp_Get_U16(&bytes[TStcp_CRC_OFFSET]);
}

static uint32_t TStcp_Get_U32(const uint8_t * bytes)
{
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static void TStcp_Set_U16(uint16_t value, uint8_t * bytes)
{
    bytes[0] = (uint8_t)((uint32_t)value >> 8U);
    bytes[1] = (uint8_t)(value & 0xFFU);
}

static void TStcp_Set_U32(uint32_t value, uint8_t * bytes)
{
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)((value >> 16U) & 0xFFU);
    bytes[2] = (uint8_t)((value >> 8U) & 0xFFU);
    bytes[3] = (uint8_t)(value & 0xFFU);
}

static void TStcp_Set_Frame(const TStcp_frame_t * frame, uint8_t * bytes)
{
    TStcp_Set_U16(frame->msgid, bytes);
    TStcp_Set_U32(frame->len, &bytes[TStcp_LEN_OFFSET]);
    TStcp_Set_U32(frame->payload, &bytes[TStcp_PAYLOAD_OFFSET]);
    TStcp_Set_U16(frame->reserved, &bytes[TStcp_RESERVED_OFFSET]);
    TStcp_Set_U16(frame->crc, &bytes[TStcp_CRC_OFFSET]);
}

/* ==================== CRC: 전송 바이트 합계의 하위 16비트 ==================== */
uint16_t TStcp_Get_Crc(const TStcp_frame_t * frame)
{
    uint8_t bytes[TStcp_CRC_OFFSET] = { 0U };
    uint16_t crc = 0U;

    if (frame != NULL)
    {
        /* crc 멤버는 읽지 않으므로 기존 crc 값의 초기화 여부와 무관하다. */
        TStcp_Set_U16(frame->msgid, bytes);
        TStcp_Set_U32(frame->len, &bytes[TStcp_LEN_OFFSET]);
        TStcp_Set_U32(frame->payload, &bytes[TStcp_PAYLOAD_OFFSET]);
        TStcp_Set_U16(frame->reserved, &bytes[TStcp_RESERVED_OFFSET]);

        crc = TStcp_Get_Crc_Bytes(bytes, sizeof(bytes));
    }

    return crc;
}

static uint16_t TStcp_Get_Crc_Bytes(const uint8_t * bytes, size_t length)
{
    uint32_t sum = 0U;
    size_t index;

    for (index = 0U; index < length; ++index)
    {
        sum = (sum + (uint32_t)bytes[index]) & 0xFFFFU;
    }
    return (uint16_t)sum;
}

/* ==================== 디버그 출력: TStcp_DEBUG_ENABLED로 제어 ==================== */
#if TStcp_DEBUG_ENABLED
static void TStcp_Debug_Write(const char * message, size_t length)
{
    size_t position = 0U;
    bool writing = true;
    ssize_t count;

    while ((position < length) && (writing == true))
    {
        count = write(STDERR_FILENO, &message[position], length - position);
        if (count > 0)
        {
            position += (size_t)count;
        }
        else
        {
            if ((count == 0) || (errno != EINTR))
            {
                /* 진단 출력 실패는 통신 결과를 바꾸지 않는다. */
                writing = false;
            }
        }
    }
}

static void TStcp_Debug_Message(const char * message)
{
    TStcp_Debug_Write(message, strlen(message));
}

static void TStcp_Debug_Value(const char * label, uint32_t value)
{
    const char digits[] = "0123456789ABCDEF";
    char text[11] = { '0', 'x', '0', '0', '0', '0', '0', '0', '0', '0', '\n' };
    uint32_t index;
    uint32_t shift;

    for (index = 0U; index < 8U; ++index)
    {
        shift = (7U - index) * 4U;
        text[2U + index] = digits[(value >> shift) & 0xFU];
    }
    TStcp_Debug_Message(label);
    TStcp_Debug_Write(text, sizeof(text));
}
#endif /* TStcp_DEBUG_ENABLED */
/* ==================== LinkTest 메시지 처리 및 응답 프레임 구성·송신 ==================== */
void TStcp_LinkTest(const TStcp_frame_t * frame)
{
    TStcp_Set_LinkTest(frame);
}

void TStcp_Get_LinkTest(TStcp_frame_t * frame)
{
    if (frame != NULL)
    {
        frame->len = TStcp_PAYLOAD_SIZE;
        frame->payload = 0x1234U;
        TStcp_context.last_result = TStcp_RESULT_OK;
    }
    else
    {
        TStcp_context.last_result = TStcp_RESULT_INVALID_ARGUMENT;
    }
}

void TStcp_Set_LinkTest(const TStcp_frame_t * frame)
{
    if (frame != NULL)
    {
        TStcp_frame_t response = *frame;

        TStcp_Get_LinkTest(&response);
        response.crc = TStcp_Get_Crc(&response);
        TStcp_Debug_Message("TStcp LinkTest handler: sending response\n");
        TStcp_Send(&response);
    }
    else
    {
        TStcp_context.last_result = TStcp_RESULT_INVALID_ARGUMENT;
    }
}
/* ==================== 중앙 제어 연동 인터페이스: 명령·상태·완료·알람 ==================== */
void TStcp_Get_Command(const TStcp_frame_t * frame)
{
    TScontrol_Set_Command(frame->payload);
}

void TStcp_Set_Command_Result(main_command_result_t result)
{
    TStcp_frame_t response = { 0U, 0U, 0U, 0U, 0U };

    response.msgid = TStcp_MSGID_SET_COMMAND;
    response.len = TStcp_PAYLOAD_SIZE;
    response.payload = (uint32_t)result;
    response.crc = TStcp_Get_Crc(&response);
    TStcp_Debug_Message("TStcp_Set_Command_Result\n");
    TStcp_Send(&response);

}

void TStcp_Set_Status(const main_status_t * status)
{
    uint8_t bytes[TStcp_STATUS_FRAME_SIZE] = { 0U };
    TStcp_Set_Status_frame_tframe_t response =
    {
        .msgid = TStcp_MSGID_SET_STATUS,
        .len = TStcp_STATUS_PAYLOAD_SIZE,
        .status = { main_STATE_INIT, main_AXIS_STATE_UNKNOWN, main_AXIS_STATE_UNKNOWN,
                    main_E84_EVENT_NONE, main_FAULT_NONE },
        .reserved = 0U,
        .crc = 0U
    };

    if (status != NULL)
    {
        /* 구조체 대입으로 값을 복사하고 패딩/enum 크기에 의존하지 않게 직렬화한다. */
        response.status = *status;
        TStcp_Set_U16(response.msgid, bytes);
        TStcp_Set_U32(response.len, &bytes[TStcp_LEN_OFFSET]);
        TStcp_Set_U32((uint32_t)response.status.state, &bytes[TStcp_PAYLOAD_OFFSET]);
        TStcp_Set_U32((uint32_t)response.status.x_axis_state,&bytes[TStcp_PAYLOAD_OFFSET + TStcp_PAYLOAD_SIZE]);
        TStcp_Set_U32((uint32_t)response.status.z_axis_state,&bytes[TStcp_PAYLOAD_OFFSET + (2U * TStcp_PAYLOAD_SIZE)]);
        TStcp_Set_U32((uint32_t)response.status.e84_event,&bytes[TStcp_PAYLOAD_OFFSET + (3U * TStcp_PAYLOAD_SIZE)]);
        TStcp_Set_U32((uint32_t)response.status.fault,&bytes[TStcp_PAYLOAD_OFFSET + (4U * TStcp_PAYLOAD_SIZE)]);
        TStcp_Set_U16(response.reserved, &bytes[TStcp_STATUS_RESERVED_OFFSET]);
        response.crc = TStcp_Get_Crc_Bytes(bytes, TStcp_STATUS_CRC_OFFSET);
        TStcp_Set_U16(response.crc, &bytes[TStcp_STATUS_CRC_OFFSET]);
        TStcp_Debug_Message("TStcp_Set_Status\n");
        TStcp_Set_Send_Bytes(bytes, sizeof(bytes));
    }
    else
    {
        TStcp_context.last_result = TStcp_RESULT_INVALID_ARGUMENT;
    }
}

void TStcp_Set_Complete(main_command_t command)
{
    TStcp_frame_t response = { 0U, 0U, 0U, 0U, 0U };

    response.msgid = TStcp_MSGID_SET_COMPLETE;
    response.len = TStcp_PAYLOAD_SIZE;
    response.payload = (uint32_t)command;
    response.crc = TStcp_Get_Crc(&response);
    TStcp_Debug_Message("TStcp_Set_Complete\n");
    TStcp_Send(&response);
}

void TStcp_Set_Alarm(main_fault_t fault)
{
    TStcp_frame_t response = { 0U, 0U, 0U, 0U, 0U };

    response.msgid = TStcp_MSGID_SET_ALARM;
    response.len = TStcp_PAYLOAD_SIZE;
    response.payload = (uint32_t)fault;
    response.crc = TStcp_Get_Crc(&response);
    TStcp_Debug_Message("TStcp_Set_Alarm\n");
    TStcp_Send(&response);
}
/* ==================== 메시지 파싱: msgid별 처리 함수 분기 ==================== */
void TStcp_Parse(const TStcp_frame_t * frame)
{
    TStcp_context.last_result = TStcp_RESULT_INVALID_ARGUMENT;

    if (frame != NULL)
    {
        TStcp_context.last_result = TStcp_RESULT_OK;
        /* 수신 CRC 일치 여부에 따른 거부 처리는 추후 이 위치에 추가한다. */
        switch (frame->msgid)
        {
            case TStcp_MSGID_LINK_TEST:
            {
                TStcp_LinkTest(frame);
                break;
            }
            case TStcp_MSGID_GET_COMMAND:
            {
                TStcp_Get_Command(frame);
                break;
            }

            default:
            {
                TStcp_Debug_Message("TStcp unsupported msgid; LinkTest expects 0x0000\n");
                TStcp_context.last_result = TStcp_RESULT_UNSUPPORTED_MSGID;
                break;
            }
        }
    }
}
