#ifndef TSspi_H
#define TSspi_H

#include "main.h"

/* 배열 인덱스는 main_axis_t의 열거값과 별개이다. */
#define TSspi_AXIS_COUNT (2U)
#define TSspi_X_AXIS_INDEX (0U)
#define TSspi_Z_AXIS_INDEX (1U)
#define TSspi_INVALID_DEVICE (-1)

typedef struct
{
    /* 주기 및 제한시간은 설정값이다. 그림에 수치가 없어 초기값은 미정이다. */
    uint32_t heartbeat_period_ms;
    uint32_t heartbeat_timeout_ms;
    uint32_t response_timeout_ms;
} TSspi_config_t;

/* DEV-PTHREAD-01/DEV-VOID-01: pthread_create에 등록하는 태스크 진입 함수이다. */
void * TSspi_Init_Task(void * argument);

void TSspi_Init_Communication(const TSspi_config_t * config);

/* 중앙 FSM에서 X축 또는 Z축의 MOVE/GET_STATUS 명령을 전달받는다. */
void TSspi_Set_Axis_Command(const main_axis_command_t * command);

/* 미구현 상태에서는 main_AXIS_NONE을 반환한다. */
void TSspi_Get_Target_Axis(main_axis_t * axis);

/* STM32에서 수신한 축별 ACK, 위치, 상태 및 Heartbeat를 반영한다. */
void TSspi_Set_Device_Status(const main_axis_status_t * status);

/* 미구현 상태에서는 main_HEARTBEAT_UNKNOWN을 반환한다. */
void TSspi_Get_Heartbeat_Status(main_axis_t axis,
                                main_heartbeat_status_t * status);

#endif /* TSspi_H */
