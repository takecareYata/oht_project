#ifndef TStcp_H
#define TStcp_H

#include "main.h"

#define TStcp_INVALID_SOCKET (-1)
#define TStcp_RECONNECT_DELAY_SECONDS (1U)

/* 1: 디버그 출력 켜기, 0: 출력 코드/호출/인수 평가를 전처리 단계에서 제외.
 * 컴파일 옵션 -DTStcp_DEBUG_ENABLED=0 또는 1로도 지정할 수 있다.
 */
#ifndef TStcp_DEBUG_ENABLED
#define TStcp_DEBUG_ENABLED (1)
#endif

#if (TStcp_DEBUG_ENABLED != 0) && (TStcp_DEBUG_ENABLED != 1)
#error "TStcp_DEBUG_ENABLED must be 0 or 1"
#endif

/* 임시 서버 설정이다. 컴파일 시 -D로 변경할 수 있다. */
#ifndef TStcp_SERVER_ADDRESS
#define TStcp_SERVER_ADDRESS "10.10.15.32"//"127.0.0.1"
#endif

#ifndef TStcp_SERVER_PORT
#define TStcp_SERVER_PORT "5000"
#endif

#define TStcp_PAYLOAD_SIZE (4U)
#define TStcp_MSGID_SIZE (2U)
#define TStcp_LEN_SIZE (4U)
#define TStcp_RESERVED_SIZE (2U)
#define TStcp_CRC_SIZE (2U)
#define TStcp_FRAME_SIZE (TStcp_MSGID_SIZE + TStcp_LEN_SIZE + TStcp_PAYLOAD_SIZE + \
                          TStcp_RESERVED_SIZE + TStcp_CRC_SIZE)

/* 상태 payload: state, x_axis_state, z_axis_state, e84_event, fault 각 4바이트. */
#define TStcp_STATUS_PAYLOAD_SIZE (5U * TStcp_PAYLOAD_SIZE)
#define TStcp_STATUS_FRAME_SIZE (TStcp_MSGID_SIZE + TStcp_LEN_SIZE + TStcp_STATUS_PAYLOAD_SIZE + TStcp_RESERVED_SIZE + TStcp_CRC_SIZE)

#define TStcp_MSGID_LINK_TEST (0x0000U)

#define TStcp_MSGID_GET_COMMAND (0x0010U)
#define TStcp_MSGID_SET_COMMAND (0x0011U)
#define TStcp_MSGID_GET_STATUS (0x0012U)
#define TStcp_MSGID_SET_STATUS (0x0013U)
#define TStcp_MSGID_GET_COMPLETE (0x0014U)
#define TStcp_MSGID_SET_COMPLETE (0x0015U)

#define TStcp_MSGID_GET_ALARM   (0xFFFEU)
#define TStcp_MSGID_SET_ALARM   (0xFFFFU)

/*
 * 고정 14바이트 프레임: msgid(2) + len(4) + payload(4) + reserved(2) + crc(2).
 * len의 수신 값은 그대로 보관한다. LinkTest 응답의 len은 payload 크기 4이다.
 * 다중 바이트 정수의 전송 순서는 빅 엔디언이다.
 * 이 구조체는 해석된 값의 저장용이다. sizeof(struct)를 전송 길이로 쓰지 않는다.
 * CRC는 crc 앞 12바이트의 바이트 합계에서 하위 16비트를 취한다.
 */
typedef struct
{
    uint16_t msgid;
    uint32_t len;
    uint32_t payload;
    uint16_t reserved;
    uint16_t crc;
} TStcp_frame_t;

typedef struct
{
    uint16_t msgid;
    uint32_t len;
    main_status_t status;
    uint16_t reserved;
    uint16_t crc;
} TStcp_Set_Status_frame_tframe_t;

typedef enum
{
    TStcp_RESULT_OK = 0,
    TStcp_RESULT_INVALID_ARGUMENT,
    TStcp_RESULT_UNSUPPORTED_MSGID,
    TStcp_RESULT_RECEIVE_ERROR,
    TStcp_RESULT_INCOMPLETE_FRAME,
    TStcp_RESULT_CLOSE_ERROR,
    TStcp_RESULT_SEND_ERROR,
    TStcp_RESULT_CONNECT_ERROR
} TStcp_result_t;

/* 패킷 종류는 내부 구분용이다. 실제 프로토콜의 숫자값은 별도로 확정한다. */
typedef enum
{
    TStcp_PACKET_NONE = 0,
    TStcp_PACKET_ACK,
    TStcp_PACKET_STATUS,
    TStcp_PACKET_COMPLETE,
    TStcp_PACKET_ERROR
} TStcp_packet_t;

/* DEV-PTHREAD-01/DEV-VOID-01: pthread_create에 등록하는 태스크 진입 함수이다. */
void * TStcp_Init_Task(void * argument);

/* 소켓은 모듈 내부에서 소유한다. 아래 통신/처리 API는 TCP 태스크에서만 호출한다.
 * 다른 태스크에서 호출하려면 큐 등으로 TCP 태스크에 요청을 전달해야 한다.
 */
/* TCP 서버에 연결하고 소켓을 보관한다. 반환값은 연결 소켓 또는 -1이다. */
int TStcp_Init_Communication(void);

/* 내부 소켓으로 EOF/오류까지 수신하고 소켓을 닫은 뒤 내부 값을 무효화한다. */
TStcp_result_t TStcp_Init_Client(void);

/* 사용자 지정 함수명. 해석된 프레임의 msgid에 해당하는 함수를 호출한다. */
void TStcp_Parse(const TStcp_frame_t * frame);

/* 내부 소켓으로 14바이트를 모두 송신한다. CRC는 지정한 값을 그대로 보낸다.
 * 오류는 TStcp_Get_Result()로 확인한다. 수신 루프는 송신 오류 시 연결을 정리한다.
 */
void TStcp_Send(const TStcp_frame_t * frame);

/* 구조체 패딩과 crc 필드를 제외한 전송 바이트의 합계 & 0xFFFF.
 * 입력 프레임을 수정하지 않으며 NULL이면 0을 반환한다.
 */
uint16_t TStcp_Get_Crc(const TStcp_frame_t * frame);

/* 직전 연결/수신/파싱/송신 결과를 출력 인수로 얻는다. */
void TStcp_Get_Result(TStcp_result_t * result);

/* LinkTest 응답: 같은 msgid/reserved, len=4, payload=0x1234, 계산된 crc. */
void TStcp_LinkTest(const TStcp_frame_t * frame);
void TStcp_Get_LinkTest(TStcp_frame_t * frame);
void TStcp_Set_LinkTest(const TStcp_frame_t * frame);

/* GET_COMMAND 메시지 처리 골격. 중앙 FSM 전달은 추후 구현한다. */
void TStcp_Get_Command(const TStcp_frame_t * frame);

/* 명령 수락/거부 결과와 ACK 응답을 준비한다. */
void TStcp_Set_Command_Result(main_command_result_t result);

/* STATUS 응답: len=20, 전체 30바이트. enum 값은 각각 uint32 빅 엔디언으로 송신한다. */
void TStcp_Set_Status(const main_status_t * status);

/* 작업 완료에 따른 COMPLETE 응답을 준비한다. */
void TStcp_Set_Complete(main_command_t command);

/* 명령 거부 또는 오류에 따른 ERROR 응답을 준비한다. */
void TStcp_Set_Alarm(main_fault_t fault);

#endif /* TStcp_H */
