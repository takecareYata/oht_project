#ifndef main_H
#define main_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 현재 CSU 기능은 구현 전 골격이다. Set 함수는 입력을 보관하거나 장치를 제어하지
 * 않는다. Get 함수는 유효한 출력 포인터에 기본값을 기록하며 NULL은 허용한다.
 * NULL 이외의 포인터는 호출자가 올바른 형식과 수명의 객체로 전달해야 한다.
 * config/status 등의 입력 포인터는 호출이 끝난 후 보관하지 않는다.
 */

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
    main_COMMAND_START
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
    main_STATE_FAULT
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
    main_FAULT_E84_TIMEOUT
} main_fault_t;

typedef enum
{
    main_AXIS_COMMAND_NONE = 0,
    main_AXIS_COMMAND_MOVE,
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
    main_AXIS_EVENT_COMPLETE
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
    /* 위치 단위와 유효 범위는 STM32 인터페이스 확정 후 정의한다. */
    int32_t target_position;
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

/* POSIX 콜백 형식은 MISRA.md의 DEV-VOID-01 및 DEV-PTHREAD-01을 따른다. */
typedef void * (*main_task_entry_t)(void * argument);

#endif /* main_H */
