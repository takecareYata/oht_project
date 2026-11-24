/* ============================================================================
 * pick.c — [Z 보드] PICK/PLACE 시퀀스 (L4)
 *
 * PICK : 하강 → 열기(스위치 해제 확인) → 닫기(접촉 확인) → 상승
 * PLACE: [파지 확인] → 하강 → 열기 → 상승
 *
 * 매 루프 인터록(X 정지 + 정거장 도착)을 다시 확인하고, 사라지면 INTERLOCK 정지.
 * RECOVER는 controller.c에서 내부 명령 Z_UP으로 바뀌어 들어온다.
 * ============================================================================ */
#include "control.h"
static control_state_t pick_state;
static control_error_t pick_error;
static uint8_t pick_command; /* 실행 중 명령 (Z_UP/Z_DOWN/OPEN/CLOSE/PICK/PLACE) */
static uint8_t pick_stage;   /* PICK/PLACE 내부 단계 0~3 */
static bool pick_sequence;   /* true = PICK/PLACE 연속 동작, false = 단일 동작 */
/* 고장: Z·집게 모두 정지하고 고장 코드 기록. */
static void pick_Set_Fault(control_error_t error)
{
    zlift_Set_Stop();
    gripper_Set_Stop();
    pick_state = CONTROL_ERROR;
    pick_error = error;
}
/* 명령 시작. 인터록과 PLACE 파지 조건을 확인한 뒤 첫 동작을 건다.
 * 응답 stage: Z_UP 3, PICK 4(하강부터), PLACE 8(하강부터). */
void pick_Set_Command(uint8_t command, uint8_t remote)
{
    pick_command = command;
    pick_error = CONTROL_OK;
    pick_stage = 0U;
    status_Set_Stage((command == CONTROL_CMD_PICK) ? 4U :
                     ((command == CONTROL_CMD_PLACE) ? 8U : 3U));
    pick_sequence = (command == CONTROL_CMD_PICK) || (command == CONTROL_CMD_PLACE);
    if (!interlock_Get_Permit(command, remote))
    {
        pick_Set_Fault(CONTROL_ERR_INTERLOCK);
    }
    else if ((command == CONTROL_CMD_PLACE) && !gripper_Get_Holding())
    {
        pick_Set_Fault(CONTROL_ERR_GRIP);
    }
    else
    {
        pick_state = CONTROL_BUSY;
        if ((command == CONTROL_CMD_Z_UP) || (command == CONTROL_CMD_Z_DOWN))
        {
            zlift_Set_Target((command == CONTROL_CMD_Z_DOWN) ? 1U : 0U);
        }
        else if ((command == CONTROL_CMD_OPEN) || (command == CONTROL_CMD_CLOSE))
        {
            gripper_Set_Target(command == CONTROL_CMD_CLOSE);
        }
        else if (pick_sequence)
        {
            zlift_Set_Target(1U);
        }
        else
        {
            pick_Set_Fault(CONTROL_ERR_COMMAND);
        }
    }
}
/* 매 루프: 하위 모듈 진행, 고장 감시, PICK/PLACE 단계 전환.
 * 내부 단계(pick_stage) → 응답 stage
 *   0 하강 대기      → PICK 4 / PLACE 8
 *   1 열기 대기      → PICK 5 / PLACE 9
 *   2 닫기·접촉 대기 → PICK 6 (PICK만)
 *   3 상승 대기      → PICK 7 / PLACE 10 */
void pick_Set_Update(uint8_t remote)
{
    control_state_t state = CONTROL_IDLE;
#if CONFIG_Z_TIMED != 0U
    /* Z 백도어: B1 버튼 = 지금 진행 중인 동작(Z 이동 또는 집게 닫기) 완료 */
    if (sensor_Get_Button_Event())
    {
        zlift_Set_Confirm();
        gripper_Set_Confirm();
    }
#endif
    zlift_Set_Update();
    gripper_Set_Update();
    if (pick_state == CONTROL_BUSY)
    {
        if (!interlock_Get_Permit(pick_command, remote))
        {
            pick_Set_Fault(CONTROL_ERR_INTERLOCK);
        }
        else if (gripper_Get_Error() == CONTROL_ERR_GRIP && gripper_Get_State() == CONTROL_ERROR)
        {
            pick_Set_Fault(CONTROL_ERR_GRIP);
        }
        else if (zlift_Get_State() == CONTROL_ERROR)
        {
            pick_Set_Fault(zlift_Get_Error());
        }
        else
        {
            if (pick_sequence)
            {
                if (pick_stage == 0U)
                {
                    if (zlift_Get_State() == CONTROL_DONE)
                    {
                        /* 하강 완료 → 열기. PICK은 열고 나서 닫고, PLACE는 열기만 */
                        gripper_Set_Target(false);
                        pick_stage = 1U;
                        status_Set_Stage((pick_command == CONTROL_CMD_PICK) ? 5U : 9U);
                    }
                }
                else if (pick_stage == 1U)
                {
                    if (gripper_Get_State() == CONTROL_DONE)
                    {
                        if (pick_command == CONTROL_CMD_PICK)
                        {
                            gripper_Set_Target(true);
                            pick_stage = 2U;
                            status_Set_Stage(6U);
                        }
                        else
                        {
                            zlift_Set_Target(0U);
                            pick_stage = 3U;
                            status_Set_Stage(10U);
                        }
                    }
                }
                else if (pick_stage == 2U)
                {
                    if (gripper_Get_State() == CONTROL_DONE)
                    {
                        zlift_Set_Target(0U);
                        pick_stage = 3U;
                        status_Set_Stage(7U);
                    }
                }
                else if (zlift_Get_State() == CONTROL_DONE)
                {
                    pick_state = CONTROL_DONE;
                }
                else
                {
                }
            }
            else /* 단일 동작: 해당 모듈이 DONE이면 완료 */
            {
                state = ((pick_command == CONTROL_CMD_Z_UP) || (pick_command == CONTROL_CMD_Z_DOWN))
                            ? zlift_Get_State()
                            : gripper_Get_State();
                if (state == CONTROL_DONE)
                {
                    pick_state = CONTROL_DONE;
                }
            }
        }
    }
    /* 대기·완료 중에도 파지 상실(GRIP)은 고장으로 올린다 */
    else if (gripper_Get_State() == CONTROL_ERROR)
    {
        pick_Set_Fault(gripper_Get_Error());
    }
}
/* 정지: Z·집게 출력 off. 집게 스텝 위치와 파지 여부는 유지된다. */
void pick_Set_Stop(void)
{
    zlift_Set_Stop();
    gripper_Set_Stop();
    pick_state = CONTROL_STOPPED;
    pick_error = CONTROL_OK;
}
control_state_t pick_Get_State(void)
{
    return pick_state;
}
control_error_t pick_Get_Error(void)
{
    return pick_error;
}
