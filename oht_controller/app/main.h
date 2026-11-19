#ifndef main_H
#define main_H

#include <stdbool.h>
#include <stdint.h>

/* CSU 입력 포인터는 호출 중 복사하며 보관하지 않는다. SPI v2는 작업 명령 기반이다. */

/* CSU 사이에서 공유하는 내부 자료형이다. 열거값은 통신 프레임 값이 아니다. */
typedef enum
{
    main_AXIS_NONE = 0,
    main_AXIS_X,
    main_AXIS_Z
} main_axis_t;

typedef enum
{
    main_COMMAND_NONE = 0,
    main_COMMAND_START,
    main_COMMAND_STOP,
    main_COMMAND_RECOVER
} main_command_t;

typedef enum
{
    main_COMMAND_RESULT_NONE = 0,
    main_COMMAND_RESULT_ACCEPTED,
    main_COMMAND_RESULT_REJECTED
} main_command_result_t;

typedef enum
{
    main_STATE_INIT = 0,
    main_STATE_IDLE,
    main_STATE_WAFER_PICKUP,
    main_STATE_X_TRANSFER,
    main_STATE_E84_PREPARE,
    main_STATE_WAFER_HANDOVER,
    main_STATE_E84_COMPLETE,
    main_STATE_Z_RETURN,
    main_STATE_COMPLETE,
    main_STATE_FAULT,
    main_STATE_STOPPING,
    main_STATE_ARMING_Z,
    main_STATE_ARMING_X,
    main_STATE_RECOVER_Z,
    main_STATE_RECOVER_X
} main_state_t;

typedef enum
{
    main_HEARTBEAT_UNKNOWN = 0,
    main_HEARTBEAT_NORMAL,
    main_HEARTBEAT_LOST
} main_heartbeat_status_t;

typedef enum
{
    main_FAULT_NONE = 0,
    main_FAULT_INTERLOCK,
    main_FAULT_SPI_COMMUNICATION,
    main_FAULT_E84_HEARTBEAT,
    main_FAULT_E84_TIMEOUT,
    main_FAULT_STOP
} main_fault_t;

typedef enum
{
    main_AXIS_COMMAND_NONE = 0,
    main_AXIS_COMMAND_ARM,
    main_AXIS_COMMAND_STOP,
    main_AXIS_COMMAND_RECOVER,
    main_AXIS_COMMAND_X_MOVE,
    main_AXIS_COMMAND_PICK,
    main_AXIS_COMMAND_PLACE,
    main_AXIS_COMMAND_GET_STATUS
} main_axis_command_code_t;

typedef enum
{
    main_AXIS_STATE_UNKNOWN = 0,
    main_AXIS_STATE_IDLE,
    main_AXIS_STATE_MOVING,
    main_AXIS_STATE_COMPLETE,
    main_AXIS_STATE_FAULT
} main_axis_state_t;

typedef enum
{
    main_AXIS_EVENT_NONE = 0,
    main_AXIS_EVENT_ACK,
    main_AXIS_EVENT_STATUS,
    main_AXIS_EVENT_COMPLETE,
    main_AXIS_EVENT_ARMED,
    main_AXIS_EVENT_STOPPED
} main_axis_event_t;

typedef enum
{
    main_E84_REQUEST_NONE = 0,
    main_E84_REQUEST_PREPARE,
    main_E84_REQUEST_BUSY,
    main_E84_REQUEST_COMPLETE
} main_e84_request_t;

typedef enum
{
    main_E84_EVENT_NONE = 0,
    main_E84_EVENT_READY,
    main_E84_EVENT_LOAD_REQUEST_OFF,
    main_E84_EVENT_COMPLETE
} main_e84_event_t;

typedef struct
{
    main_axis_t axis;
    main_axis_command_code_t command;
    int32_t target_position; /* v2 현재 명령은 0. 향후 위치 명령용. */
    uint8_t arg; /* X_MOVE: 정거장 번호 */
    uint16_t option; /* 현재 0 */
} main_axis_command_t;

typedef struct
{
    main_axis_t axis;
    main_axis_state_t state;
    int32_t current_position;
    bool acknowledged;
    main_heartbeat_status_t heartbeat_status;
    uint32_t heartbeat_counter;
    main_fault_t fault;
    main_axis_command_code_t command; /* ACK/완료가 해당하는 호스트 명령 */
    uint16_t ack_seq;
    uint16_t op_seq;
    uint8_t receipt;
    uint8_t device_fault;
    uint8_t sensors;
    uint8_t relay;
    uint8_t stage;
    uint8_t position_source;
    bool link_valid;
    bool armed;
} main_axis_status_t;

typedef struct
{
    main_state_t state;
    main_axis_state_t x_axis_state;
    main_axis_state_t z_axis_state;
    main_e84_event_t e84_event;
    main_fault_t fault;
} main_status_t;

#define main_TASK_COUNT (4U)

typedef enum
{
    main_TASK_RESULT_SUCCESS = 0,
    main_TASK_RESULT_FAILURE
} main_task_result_t;

/* POSIX 콜백 형식은 ../docs/MISRA.md의 DEV-VOID-01 및 DEV-PTHREAD-01을 따른다. */
typedef void * (*main_task_entry_t)(void * argument);

#endif /* main_H */
