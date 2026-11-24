/* ============================================================================
 * control.h — 전체 공통 헤더
 *
 * 모든 모듈의 공개 함수 선언과 상태·고장·명령 코드를 한곳에 모았다.
 * 함수 이름 규칙: 모듈_Get_무엇() = 읽기, 모듈_Set_무엇() = 동작/쓰기, 모듈_Init_무엇() = 초기화.
 * 계층: L4 app → L3 service → L2 driver → L1 bsp (호출은 위에서 아래로).
 * 예외: comm.c(L3)가 접수 판정을 위해 controller_Get_Receipt()(L4)를 부른다.
 * ============================================================================ */
#ifndef CONTROL_H
#define CONTROL_H
#include <stdbool.h>
#include <stdint.h>
#include "config.h"
/* 동작 상태. 값이 그대로 응답 state 바이트가 된다 (괄호 = 프로토콜 이름). */
typedef enum
{
    CONTROL_STOPPED = 0, /* 0 UNKNOWN : 정지 / ARM 전 */
    CONTROL_IDLE,        /* 1 IDLE    : ARM 완료, 대기 */
    CONTROL_BUSY,        /* 2 MOVING  : 동작 중 */
    CONTROL_DONE,        /* 3 COMPLETE: 완료 (다음 명령까지 유지) */
    CONTROL_ERROR        /* 4 FAULT   : 고장 */
} control_state_t;
/* 고장·접수 결과 코드. 응답 fault / receipt 바이트 값. 7~9는 receipt에만 나온다. */
typedef enum
{
    CONTROL_OK = 0,        /* 0  정상 */
    CONTROL_ERR_STOP,      /* 1  STOP 명령으로 정지 / ARM 안 됨 */
    CONTROL_ERR_LINK,      /* 2  통신 끊김 (500 ms) */
    CONTROL_ERR_SENSOR,    /* 3  센서 모순·출발 센서 안 풀림·센서 준비 안 됨 */
    CONTROL_ERR_TIMEOUT,   /* 4  이동 시간 초과 */
    CONTROL_ERR_INTERLOCK, /* 5  상대 축 조건 불만족 */
    CONTROL_ERR_GRIP,      /* 6  집게: 접촉 없음·파지 상실·열림 확인 실패 */
    CONTROL_ERR_BUSY,      /* 7  [접수 거부] 동작 중 */
    CONTROL_ERR_COMMAND,   /* 8  [접수 거부] 잘못된 cmd/arg/target */
    CONTROL_ERR_SEQUENCE,  /* 9  [접수 거부] seq 오류 */
    CONTROL_ERR_TRANSPORT  /* 10 SPI 전송 오류 */
} control_error_t;
/* remote / relay 비트: 상대 보드 상태. Jetson이 한 보드의 relay를 다른 보드의 remote로 넘긴다. */
enum
{
    CONTROL_REMOTE_Z_UP = 1,      /* bit0: Z가 상단에 있고 정지 (X 이동 조건) */
    CONTROL_REMOTE_X_STOPPED = 2, /* bit1: X 정지 (Z 상승 조건) */
    CONTROL_REMOTE_X_STATION = 4  /* bit2: X가 끝 센서(정거장) 위 (Z 하강 조건) */
};
/* 명령 코드 (요청 cmd 바이트) */
enum
{
    CONTROL_CMD_POLL = 0,     /* NOP: 상태 조회만 */
    CONTROL_CMD_ARM = 1,      /* 동작 허용, 고장 해제 */
    CONTROL_CMD_STOP = 2,     /* 즉시 정지, ARM 해제 */
    CONTROL_CMD_RECOVER = 3,  /* Z: 상단으로 / X: A로 */
    CONTROL_CMD_X_MOVE = 16,  /* X: arg 0 = A, 1 = B */
    CONTROL_CMD_Z_UP = 32,    /* 이하 32~35는 내부 전용 (SPI로는 거부) */
    CONTROL_CMD_Z_DOWN = 33,
    CONTROL_CMD_OPEN = 34,
    CONTROL_CMD_CLOSE = 35,
    CONTROL_CMD_PICK = 36,    /* Z: 하강 → 열기 → 닫기 → 상승 */
    CONTROL_CMD_PLACE = 37    /* Z: 하강 → 열기 → 상승 */
};
/* 접수된 명령 (comm.c → controller.c) */
typedef struct
{
    uint16_t seq;
    uint8_t cmd;
    uint8_t arg;
    uint8_t remote;
} control_command_t;

/* ---- L1 bsp.c / timer.c — 하드웨어 직접 제어, ms 틱 ---- */
void bsp_Init_Board(void);
uint32_t timer_Get_Tick_Ms(void);
uint8_t bsp_Get_Sensors(void);
bool bsp_Get_Button(void);
void bsp_Set_X_Direction(bool forward);
void bsp_Set_X_Enable(bool enabled);
void bsp_Set_Z_Output(bool down, bool run);
void bsp_Set_Grip_Phase(uint8_t coils);
void bsp_Set_Step_Timer(uint16_t hz);
void bsp_Set_Step_Timer_Stop(void);
void bsp_Set_Led(bool on);
void bsp_Set_Watchdog(void);

/* ---- L2 sensor.c — 센서 디바운스, 가상 센서, B1 버튼 ---- */
void sensor_Init_State(void);
void sensor_Set_Update(void);
bool sensor_Get_State(uint8_t index);
bool sensor_Get_Ready(void);
uint8_t sensor_Get_Bits(void);
void sensor_Set_Virtual(uint8_t index, bool active);
bool sensor_Get_Button_Event(void);

/* ---- L2 stepper.c — [보드2] X TB6600 STEP/DIR ---- */
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

/* ---- L2 linact.c — [보드1] Z L298N 온/오프 ---- */
void linact_Set_Run(bool down);
void linact_Set_Stop(void);

/* ---- L2 gripstep.c — [보드1] 집게 하프스텝 ---- */
void gripstep_Init_Driver(void);
void gripstep_Set_Step(bool close);
void gripstep_Set_Release(void);
int32_t gripstep_Get_Position(void);

/* ---- L3 xmotion.c — [보드2] X 이동·가감속·원점 ---- */
void xmotion_Init_State(void);
bool xmotion_Get_Referenced(void);
void xmotion_Set_Target(uint8_t target);
void xmotion_Set_Update(void);
void xmotion_Set_Stop(void);
control_state_t xmotion_Get_State(void);
control_error_t xmotion_Get_Error(void);

/* ---- L3 zlift.c — [보드1] Z 상승/하강 ---- */
void zlift_Set_Target(uint8_t target);
void zlift_Set_Update(void);
void zlift_Set_Stop(void);
control_state_t zlift_Get_State(void);
control_error_t zlift_Get_Error(void);
void zlift_Set_Confirm(void);

/* ---- L3 gripper.c — [보드1] 집게 열기/닫기 ---- */
void gripper_Set_Target(bool close);
void gripper_Set_Update(void);
void gripper_Set_Stop(void);
control_state_t gripper_Get_State(void);
control_error_t gripper_Get_Error(void);
bool gripper_Get_Holding(void);
void gripper_Set_Confirm(void);

/* ---- L3 interlock.c — [보드1] Z 동작 허용 조건 ---- */
bool interlock_Get_Permit(uint8_t command, uint8_t remote);

/* ---- L4 pick.c — [보드1] PICK/PLACE 시퀀스 ---- */
void pick_Set_Command(uint8_t command, uint8_t remote);
void pick_Set_Update(uint8_t remote);
void pick_Set_Stop(void);
control_state_t pick_Get_State(void);
control_error_t pick_Get_Error(void);

/* ---- L4 axis.c — [보드2] X 명령 + Z 상단 인터록 ---- */
void axis_Set_Command(uint8_t target, uint8_t remote);
void axis_Set_Update(uint8_t remote);
void axis_Set_Stop(void);
control_state_t axis_Get_State(void);
control_error_t axis_Get_Error(void);

/* ---- L3 status.c — state/fault/stage/op_seq/heartbeat ---- */
void status_Set_State(control_state_t state, control_error_t error);
control_state_t status_Get_State(void);
control_error_t status_Get_Error(void);
void status_Set_Update(void);
void status_Set_Sequence(uint16_t seq);
uint16_t status_Get_Sequence(void);

/* ---- L3 comm.c — SPI 프로토콜 v2 (24바이트 프레임) ---- */
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

/* ---- L2 spislave.c — SPI2 슬레이브 + DMA ---- */
void spislave_Init_Driver(void);
bool spislave_Get_Frame(uint8_t *frame);
void spislave_Set_Response(const uint8_t *frame);
bool spislave_Get_Error(void);

/* ---- 명령 접수 판정(controller.c) / receipt 기록(comm.c) ---- */
control_error_t controller_Get_Receipt(uint8_t cmd, uint8_t arg);
void comm_Set_Receipt(control_error_t error);

/* ---- L3 status.c (계속) ---- */
void status_Init_State(void);
void status_Set_Stage(uint8_t stage);
uint8_t status_Get_Stage(void);
uint16_t status_Get_Heartbeat(void);

/* ---- L3 position.c — 진단용 위치 표시 ---- */
void position_Init_State(void);
void position_Set_Motion(uint8_t target);
void position_Set_Stop(void);
void position_Set_Update(void);
int32_t position_Get_Value(void);
uint8_t position_Get_Source(void);

/* ---- L4 controller.c — 메인 루프 ---- */
void controller_Init_State(void);
void controller_Set_Update(void);
#endif
