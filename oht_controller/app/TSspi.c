#include "TSspi.h"

#include <stddef.h>

void * TSspi_Init_Task(void * argument)
{
    /* 설정값 0은 미설정 상태이다. 통신 처리는 아직 구현하지 않는다. */
    const TSspi_config_t config =
    {
        .heartbeat_period_ms = 0U,
        .heartbeat_timeout_ms = 0U,
        .response_timeout_ms = 0U
    };

    (void)argument;
    TSspi_Init_Communication(&config);

    /* TODO: SPI 태스크의 반복 처리와 종료 처리를 구현한다. */
    return NULL;
}

/* SPI 장치 경로, 속도 및 프레임 규격은 하드웨어 사양 확정 후 정의한다. */
void TSspi_Init_Communication(const TSspi_config_t * config)
{
    (void)config;
    
}

void TSspi_Set_Axis_Command(const main_axis_command_t * command)
{
    (void)command;
}

void TSspi_Get_Target_Axis(main_axis_t * axis)
{
    if (axis != NULL)
    {
        *axis = main_AXIS_NONE;
    }
}

void TSspi_Set_Device_Status(const main_axis_status_t * status)
{
    (void)status;
}

void TSspi_Get_Heartbeat_Status(main_axis_t axis,
                                main_heartbeat_status_t * status)
{
    (void)axis;
    if (status != NULL)
    {
        *status = main_HEARTBEAT_UNKNOWN;
    }
}
