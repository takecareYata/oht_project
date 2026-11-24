#ifndef TSspi_H
#define TSspi_H
#include "main.h"
#include <stddef.h>

/* 0이면 진단 코드/호출/인수 평가와 출력 스레드를 전처리로 제외한다.
 * 켠 상태에서는 SPI 태스크가 trylock 큐에 기록하고 보조 스레드가 stderr로 출력한다.
 */
#ifndef TSspi_DEBUG_ENABLED
#define TSspi_DEBUG_ENABLED (1)
#endif
#ifndef TSspi_DEBUG_FRAMES_ENABLED
#define TSspi_DEBUG_FRAMES_ENABLED (0)
#endif
#if (TSspi_DEBUG_ENABLED != 0) && (TSspi_DEBUG_ENABLED != 1)
#error "TSspi_DEBUG_ENABLED must be 0 or 1"
#endif
#if (TSspi_DEBUG_FRAMES_ENABLED != 0) && (TSspi_DEBUG_FRAMES_ENABLED != 1)
#error "TSspi_DEBUG_FRAMES_ENABLED must be 0 or 1"
#endif

/* ==================== 핀맵과 주기 ==================== */
#define TSspi_AXIS_COUNT (2U)
#define TSspi_X_AXIS_INDEX (0U)
#define TSspi_Z_AXIS_INDEX (1U)
#define TSspi_INVALID_DEVICE (-1)
#define TSspi_QUEUE_SIZE (16U)
#ifndef TSspi_X_DEVICE
#define TSspi_X_DEVICE "/dev/spidev0.0"
#endif
#ifndef TSspi_Z_DEVICE
#define TSspi_Z_DEVICE "/dev/spidev0.1"
#endif
/* 물리 핀 MOSI=19, MISO=21, SCLK=23, X CS0=24, Z CS1=26. */
#ifndef TSspi_SPEED_HZ
#define TSspi_SPEED_HZ (100000U)
#endif
#ifndef TSspi_MODE
#define TSspi_MODE (0U)
#endif
#if TSspi_MODE > 3U
#error "TSspi_MODE must be 0..3"
#endif
#if TSspi_SPEED_HZ == 0U
#error "TSspi_SPEED_HZ must be positive"
#endif
#define TSspi_BITS_PER_WORD (8U)
#define TSspi_LOOP_MS (10U)
#define TSspi_POLL_MS (50U)
#define TSspi_RESPONSE_TIMEOUT_MS (500U)
#define TSspi_HEARTBEAT_TIMEOUT_MS (1000U)
#define TSspi_REMOTE_TIMEOUT_MS (300U)
#define TSspi_MOVE_TIMEOUT_MS (30000U)
#define TSspi_WORK_TIMEOUT_MS (45000U)
#define TSspi_REOPEN_MS (1000U)
/* STM32 SPI2 DMA 재준비: 모든 프레임 사이 CS HIGH 최소 시간. */
#define TSspi_RESPONSE_DELAY_US (1000U)
#define TSspi_FRAME_SIZE (24U)
#if TSspi_DEBUG_ENABLED
#define TSspi_DEBUG_QUEUE_SIZE (64U)
#define TSspi_DEBUG_POLL_US (100000U)
#define TSspi_DEBUG_BATCH_SIZE (16U)
#define TSspi_DEBUG_TEXT_SIZE (384U)
#endif
#define TSspi_CRC_OFFSET (22U)
/* 정상 헤더의 수신 CRC 비교를 제어한다. 헤더 복원 시에는 항상 검사한다. */
#ifndef TSspi_RX_CRC_CHECK_ENABLED
#define TSspi_RX_CRC_CHECK_ENABLED (0)
#endif
#if (TSspi_RX_CRC_CHECK_ENABLED != 0) && (TSspi_RX_CRC_CHECK_ENABLED != 1)
#error "TSspi_RX_CRC_CHECK_ENABLED must be 0 or 1"
#endif
#define TSspi_REQUEST_MAGIC (0xA5U)
#define TSspi_RESPONSE_MAGIC (0x5AU)
#define TSspi_VERSION (2U)
#define TSspi_STOP_ALL_MASK (3U)
#define TSspi_LINK_MASK (0x80U)
#define TSspi_BOOT_STAGE (15U)
#ifndef TSspi_STATION_COUNT
#define TSspi_STATION_COUNT (2U)
#endif

typedef enum
{
    TSspi_RESPONSE_VALID = 0,
    TSspi_RESPONSE_BAD_MAGIC,
    TSspi_RESPONSE_BAD_VERSION,
    TSspi_RESPONSE_BAD_AXIS,
    TSspi_RESPONSE_BAD_FIELDS,
    TSspi_RESPONSE_BAD_CRC
} TSspi_response_result_t;

/* ==================== wire 코드 ==================== */
typedef enum
{
    TSspi_CMD_NOP = 0,
    TSspi_CMD_ARM = 1,
    TSspi_CMD_STOP = 2,
    TSspi_CMD_RECOVER = 3,
    TSspi_CMD_X_MOVE = 16,
    TSspi_CMD_PICK = 36,
    TSspi_CMD_PLACE = 37
} TSspi_cmd_t;

typedef enum
{
    TSspi_STATE_UNKNOWN = 0,
    TSspi_STATE_IDLE,
    TSspi_STATE_MOVING,
    TSspi_STATE_COMPLETE,
    TSspi_STATE_FAULT
} TSspi_state_t;

typedef enum
{
    TSspi_ERROR_OK = 0,
    TSspi_ERROR_STOP,
    TSspi_ERROR_LINK,
    TSspi_ERROR_SENSOR,
    TSspi_ERROR_TIMEOUT,
    TSspi_ERROR_INTERLOCK,
    TSspi_ERROR_GRIP,
    TSspi_ERROR_BUSY,
    TSspi_ERROR_COMMAND,
    TSspi_ERROR_SEQUENCE,
    TSspi_ERROR_TRANSPORT
} TSspi_error_t;

typedef enum
{
    TSspi_PENDING_NONE = 0,
    TSspi_PENDING_RECEIPT,
    TSspi_PENDING_COMPLETE
} TSspi_pending_state_t;

typedef struct
{
    uint8_t magic;
    uint8_t version;
    uint8_t axis;
    uint8_t cmd;
    uint16_t seq;
    uint8_t arg;
    uint8_t remote;
    int32_t target;
    uint16_t option;
    uint8_t reserved[8];
    uint16_t crc;
} TSspi_request_t;

typedef struct
{
    uint8_t magic;
    uint8_t version;
    uint8_t axis;
    uint8_t state;
    uint16_t ack_seq;
    uint8_t receipt;
    uint8_t fault;
    uint8_t sensors;
    uint8_t relay;
    uint16_t op_seq;
    int32_t position;
    uint8_t pos_src;
    uint8_t reserved0;
    uint16_t heartbeat;
    uint16_t reserved1;
    uint16_t crc;
} TSspi_response_t;

typedef struct
{
    uint32_t heartbeat_period_ms;
    uint32_t heartbeat_timeout_ms;
    uint32_t response_timeout_ms;
    uint32_t move_timeout_ms;
    uint32_t work_timeout_ms;
} TSspi_config_t;

typedef struct
{
    int device;
    main_axis_t axis;
    main_axis_status_t status;
    bool synced;
    bool valid_response;
    bool heartbeat_seen;
    bool armed;
    bool stopped;
    bool stop_required;
    bool fault_reported;
    uint16_t seq;
    TSspi_request_t request;
    main_axis_command_code_t command;
    TSspi_pending_state_t pending;
    uint64_t last_response_ms;
    uint64_t heartbeat_changed_ms;
    uint64_t command_ms;
    uint64_t last_poll_ms;
    uint64_t reopen_ms;
} TSspi_axis_context_t;

typedef struct
{
    main_axis_status_t status;
    uint64_t heartbeat_changed_ms;
    uint32_t heartbeat_timeout_ms;
} TSspi_snapshot_t;

#if TSspi_DEBUG_ENABLED
typedef struct
{
    const char * message; /* 정적 수명 문자열만 저장한다. */
    main_axis_t axis;
    uint32_t detail;
    uint16_t seq;
    uint16_t ack_seq;
    uint16_t op_seq;
    uint32_t heartbeat;
    uint8_t cmd;
    uint8_t state;
    uint8_t receipt;
    uint8_t fault;
    bool has_bytes;
    uint8_t bytes[TSspi_FRAME_SIZE];
} TSspi_debug_entry_t;
#endif

/* Init/Parse/Device_Status는 SPI 태스크 전용. 나머지 큐·캐시 API는 다른 태스크에서도 호출 가능. */
void * TSspi_Init_Task(void * argument);
void TSspi_Init_Communication(const TSspi_config_t * config);
void TSspi_Set_Axis_Command(const main_axis_command_t * command);
/* 명시적 복구 시작: 일반 큐를 지우고 양축 STOP을 우선 등록한다. */
void TSspi_Set_Recovery(void);
void TSspi_Get_Target_Axis(main_axis_t * axis);
void TSspi_Set_Device_Status(const main_axis_status_t * status);
void TSspi_Get_Heartbeat_Status(main_axis_t axis, main_heartbeat_status_t * status);
void TSspi_Parse(const TSspi_response_t * response);
/* CRC-16/CCITT-FALSE. 123456789 => 0x29B1. 프레임 계산 시 length=22. */
uint16_t TSspi_Get_Crc(const uint8_t * bytes, size_t length);
#endif /* TSspi_H */
