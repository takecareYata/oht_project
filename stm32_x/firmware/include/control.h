#ifndef CONTROL_H
#define CONTROL_H
#include <stdbool.h>
#include <stdint.h>
#include "config.h"
typedef enum
{
    CONTROL_STOPPED = 0,
    CONTROL_IDLE,
    CONTROL_BUSY,
    CONTROL_DONE,
    CONTROL_ERROR
} control_state_t;
typedef enum
{
    CONTROL_OK = 0,
    CONTROL_ERR_STOP,
    CONTROL_ERR_LINK,
    CONTROL_ERR_SENSOR,
    CONTROL_ERR_TIMEOUT,
    CONTROL_ERR_INTERLOCK,
    CONTROL_ERR_GRIP,
    CONTROL_ERR_BUSY,
    CONTROL_ERR_COMMAND,
    CONTROL_ERR_SEQUENCE,
    CONTROL_ERR_TRANSPORT
} control_error_t;
enum
{
    CONTROL_REMOTE_Z_UP = 1,
    CONTROL_REMOTE_X_STOPPED = 2,
    CONTROL_REMOTE_X_STATION = 4
};
enum
{
    CONTROL_CMD_POLL = 0,
    CONTROL_CMD_ARM = 1,
    CONTROL_CMD_STOP = 2,
    CONTROL_CMD_RECOVER = 3,
    CONTROL_CMD_X_MOVE = 16,
    CONTROL_CMD_Z_UP = 32,
    CONTROL_CMD_Z_DOWN = 33,
    CONTROL_CMD_OPEN = 34,
    CONTROL_CMD_CLOSE = 35,
    CONTROL_CMD_PICK = 36,
    CONTROL_CMD_PLACE = 37
};
typedef struct
{
    uint16_t seq;
    uint8_t cmd;
    uint8_t arg;
    uint8_t remote;
} control_command_t;
void bsp_Init_Board(void);
uint32_t timer_Get_Tick_Ms(void);
uint8_t bsp_Get_Sensors(void);
void bsp_Set_X_Direction(bool forward);
void bsp_Set_X_Enable(bool enabled);
void bsp_Set_Z_Output(bool down, uint16_t duty);
void bsp_Set_Servo_Pulse(uint16_t pulse);
void bsp_Set_Step_Timer(uint16_t hz);
void bsp_Set_Step_Timer_Stop(void);
void bsp_Set_Led(bool on);
void bsp_Set_Watchdog(void);
void sensor_Init_State(void);
void sensor_Set_Update(void);
bool sensor_Get_State(uint8_t index);
bool sensor_Get_Ready(void);
uint8_t sensor_Get_Bits(void);
void stepper_Init_Driver(void);
void stepper_Set_Run(bool forward);
void stepper_Set_Goal(int32_t goal);
bool stepper_Get_Running(void);
int32_t stepper_Get_Position(void);
void stepper_Set_Position(int32_t position);
uint32_t stepper_Get_Remaining(void);
void stepper_Set_Stop(void);
void stepper_Set_Disable(void);
void stepper_Set_Rate(uint16_t hz);
void stepper_Set_Tick(void);
void linact_Set_Run(bool down);
void linact_Set_Stop(void);
void servo_Set_Pulse(uint16_t pulse);
void xmotion_Init_State(void);
bool xmotion_Get_Referenced(void);
void xmotion_Set_Target(uint8_t target);
void xmotion_Set_Update(void);
void xmotion_Set_Stop(void);
control_state_t xmotion_Get_State(void);
control_error_t xmotion_Get_Error(void);
void zlift_Set_Target(uint8_t target);
void zlift_Set_Update(void);
void zlift_Set_Stop(void);
control_state_t zlift_Get_State(void);
control_error_t zlift_Get_Error(void);
void gripper_Set_Target(bool close);
void gripper_Set_Update(void);
void gripper_Set_Stop(void);
control_state_t gripper_Get_State(void);
control_error_t gripper_Get_Error(void);
bool gripper_Get_Holding(void);
bool interlock_Get_Permit(uint8_t command, uint8_t remote);
void pick_Set_Command(uint8_t command, uint8_t remote);
void pick_Set_Update(uint8_t remote);
void pick_Set_Stop(void);
control_state_t pick_Get_State(void);
control_error_t pick_Get_Error(void);
void axis_Set_Command(uint8_t target, uint8_t remote);
void axis_Set_Update(uint8_t remote);
void axis_Set_Stop(void);
control_state_t axis_Get_State(void);
control_error_t axis_Get_Error(void);
void status_Set_State(control_state_t state, control_error_t error);
control_state_t status_Get_State(void);
control_error_t status_Get_Error(void);
void status_Set_Update(void);
void status_Set_Sequence(uint16_t seq);
uint16_t status_Get_Sequence(void);
#define COMM_FRAME_SIZE 24U
#define COMM_VERSION 2U
uint16_t comm_Get_Crc(const uint8_t *data, uint32_t size);
void comm_Init_Link(void);
void comm_Set_Update(void);
bool comm_Get_Command(control_command_t *command);
bool comm_Get_Link(void);
uint8_t comm_Get_Remote(void);
bool comm_Get_Transport_Error(void);
void comm_Set_Response(void);
void spislave_Init_Driver(void);
bool spislave_Get_Frame(uint8_t *frame);
void spislave_Set_Response(const uint8_t *frame);
bool spislave_Get_Error(void);
control_error_t controller_Get_Receipt(uint8_t cmd, uint8_t arg);
void comm_Set_Receipt(control_error_t error);
void status_Init_State(void);
void status_Set_Stage(uint8_t stage);
uint8_t status_Get_Stage(void);
uint16_t status_Get_Heartbeat(void);
void position_Init_State(void);
void position_Set_Motion(uint8_t target);
void position_Set_Stop(void);
void position_Set_Update(void);
int32_t position_Get_Value(void);
uint8_t position_Get_Source(void);
void controller_Init_State(void);
void controller_Set_Update(void);
#endif
