/* ============================================================================
 * controller.c — 메인 루프와 명령 분배 (L4)
 *
 * 매 루프 입력 → 안전 검사 → 명령 처리 → 진행 → 출력 순으로 실행한다.
 *
 * 정지 조건(우선순위 높음): SPI 전송 오류, 통신 끊김(ARM 중), 양 끝 센서 동시 감지, STOP.
 * ARM 성공만 고장을 해제하며, 정지 후 이전 명령을 자동으로 재개하지 않는다.
 * Z 보드는 pick.c, X 보드는 axis.c로 명령을 넘긴다.
 * ============================================================================ */
#include "control.h"
static bool controller_armed; /* ARM 상태: true일 때만 동작 명령 실행 */
/* 모든 동작 정지 + ARM 해제. 이미 FAULT면 최초 고장 코드를 유지한다.
 * error == STOP이면 UNKNOWN(정상 정지), 그 외는 FAULT. */
static void controller_Set_Stop(control_error_t error)
{
#if BOARD_ID == 1
    pick_Set_Stop();
#else
    axis_Set_Stop();
#endif
    controller_armed = false;
    if (status_Get_State() == CONTROL_ERROR) { return; }
    status_Set_State((error == CONTROL_ERR_STOP) ? CONTROL_STOPPED : CONTROL_ERROR, error);
}
/* 부팅 시 1회: 하위 모듈 상태 초기화. ARM은 해제 상태로 시작. */
void controller_Init_State(void)
{
    controller_armed = false;
#if BOARD_ID == 2
    xmotion_Init_State();
#endif
    sensor_Init_State();
    comm_Init_Link();
    status_Init_State();
    position_Init_State();
}
/* 메인 루프 1회분. 순서: ① 입력 → ② 안전 검사(정지 우선) → ③ 명령 처리
 * → ④ 동작 진행 → ⑤ 위치·상태·응답 갱신 → 워치독. */
void controller_Set_Update(void)
{
    control_command_t command;
    bool have;
    bool fault = false;
    control_state_t state;
    control_error_t error;
    /* ① 입력: 센서 디바운스, SPI 프레임 검사·접수 */
    sensor_Set_Update();
    comm_Set_Update();
    have = comm_Get_Command(&command);
    /* ② 안전 검사: 하나라도 걸리면 즉시 정지 + ARM 해제 */
    if (comm_Get_Transport_Error())
    {
        controller_Set_Stop(CONTROL_ERR_TRANSPORT);
        fault = true;
    }
    if (controller_armed && !comm_Get_Link())
    {
        controller_Set_Stop(CONTROL_ERR_LINK);
        fault = true;
    }
    if (sensor_Get_Ready() && sensor_Get_State(0U) && sensor_Get_State(1U))
    {
        controller_Set_Stop(CONTROL_ERR_SENSOR);
        fault = true;
    }
    if (have && (command.cmd == CONTROL_CMD_STOP))
    {
        controller_Set_Stop(CONTROL_ERR_STOP);
        fault = true;
    }
    /* 정지와 같은 루프에 들어온 일반 명령은 실행하지 않고 고장 코드로 응답 */
    if (have && fault && (command.cmd != CONTROL_CMD_STOP))
    {
        comm_Set_Receipt(status_Get_Error());
    }
    /* ③ 명령 처리 */
    if (have && !fault)
    {
        if (command.cmd == CONTROL_CMD_ARM)
        {
            /* ARM: 센서 준비 + 통신 정상일 때만. 이전 동작은 정지하고 고장 해제 */
            if (sensor_Get_Ready() && comm_Get_Link())
            {
#if BOARD_ID == 1
                pick_Set_Stop();
#else
                axis_Set_Stop();
#endif
                controller_armed = true;
                status_Set_State(CONTROL_IDLE, CONTROL_OK);
                status_Set_Stage(0U);
            }
            else
            {
                controller_Set_Stop(CONTROL_ERR_SENSOR);
            }
        }
        else if (!controller_armed) /* ARM 전 동작 명령은 실행하지 않음 */
        {
            status_Set_State(CONTROL_STOPPED, CONTROL_ERR_STOP);
        }
        else
        {
            /* 동작 명령: op_seq 기록 후 보드별 하위 계층으로 전달 */
            status_Set_Sequence(command.seq);
            status_Set_State(CONTROL_BUSY, CONTROL_OK);
#if BOARD_ID == 1
            /* Z: RECOVER는 내부 명령 Z_UP(상승)으로 바꿔 전달 */
            if ((command.cmd == CONTROL_CMD_RECOVER) || (command.cmd == CONTROL_CMD_PICK) ||
                (command.cmd == CONTROL_CMD_PLACE))
            {
                pick_Set_Command((command.cmd == CONTROL_CMD_RECOVER) ? CONTROL_CMD_Z_UP : command.cmd,
                                 comm_Get_Remote());
            }
            else
            {
                controller_Set_Stop(CONTROL_ERR_COMMAND);
            }
#else
            /* X: RECOVER는 A(0)로 이동. stage 1 = A로, 2 = B로 */
            if ((command.cmd == CONTROL_CMD_X_MOVE) || (command.cmd == CONTROL_CMD_RECOVER))
            {
                uint8_t target = (command.cmd == CONTROL_CMD_RECOVER) ? 0U : command.arg;
                status_Set_Stage((target == 0U) ? 1U : 2U);
                axis_Set_Command(target, comm_Get_Remote());
            }
            else
            {
                controller_Set_Stop(CONTROL_ERR_COMMAND);
            }
#endif
        }
    }
    /* ④ 동작 진행: 하위 상태를 전체 상태로 반영, 고장이면 정지 */
    if (controller_armed)
    {
#if BOARD_ID == 1
        pick_Set_Update(comm_Get_Remote());
        state = pick_Get_State();
        error = pick_Get_Error();
#else
        axis_Set_Update(comm_Get_Remote());
        state = axis_Get_State();
        error = axis_Get_Error();
#endif
        if (state == CONTROL_ERROR)
        {
            controller_Set_Stop(error);
        }
        else if (state != CONTROL_STOPPED)
        {
            status_Set_State(state, CONTROL_OK);
        }
    }
    /* ⑤ 출력: 진단 위치, heartbeat/LED, 다음 응답 프레임, 워치독 갱신 */
    position_Set_Update();
    status_Set_Update();
    comm_Set_Response();
    bsp_Set_Watchdog();
}

/* 새 명령의 접수 판정(comm.c가 호출). 결과는 응답 receipt 바이트에 기록된다.
 * OK가 아니면 그 명령은 실행되지 않는다. 판정 순서가 곧 우선순위. */
control_error_t controller_Get_Receipt(uint8_t cmd, uint8_t arg)
{
    if (status_Get_State() == CONTROL_BUSY) { return CONTROL_ERR_BUSY; }
    if (cmd == CONTROL_CMD_ARM)
    {
        if (arg != 0U) { return CONTROL_ERR_COMMAND; }
        if (!sensor_Get_Ready() || (sensor_Get_State(0U) && sensor_Get_State(1U)))
        { return CONTROL_ERR_SENSOR; }
        return comm_Get_Link() ? CONTROL_OK : CONTROL_ERR_LINK;
    }
#if BOARD_ID == 1
    if (((cmd != CONTROL_CMD_RECOVER) && (cmd != CONTROL_CMD_PICK) &&
         (cmd != CONTROL_CMD_PLACE)) || (arg != 0U)) { return CONTROL_ERR_COMMAND; }
#else
    if (((cmd != CONTROL_CMD_RECOVER) && (cmd != CONTROL_CMD_X_MOVE)) ||
        ((cmd == CONTROL_CMD_RECOVER) && (arg != 0U)) || (arg > 1U))
    { return CONTROL_ERR_COMMAND; }
#endif
    if (!controller_armed)
    { return (status_Get_State() == CONTROL_ERROR) ? status_Get_Error() : CONTROL_ERR_STOP; }
#if BOARD_ID == 1
    if (!interlock_Get_Permit((cmd == CONTROL_CMD_RECOVER) ? CONTROL_CMD_Z_UP : cmd,
                              comm_Get_Remote())) { return CONTROL_ERR_INTERLOCK; }
    if ((cmd == CONTROL_CMD_PLACE) && !gripper_Get_Holding()) { return CONTROL_ERR_GRIP; }
#else
    if ((comm_Get_Remote() & CONTROL_REMOTE_Z_UP) == 0U) { return CONTROL_ERR_INTERLOCK; }
#endif
    return CONTROL_OK;
}
