#include "control.h"
static control_state_t pick_state;
static control_error_t pick_error;
static uint8_t pick_command;
static uint8_t pick_stage;
static bool pick_sequence;
static void pick_Set_Fault(control_error_t error)
{
    zlift_Set_Stop();
    gripper_Set_Stop();
    pick_state = CONTROL_ERROR;
    pick_error = error;
}
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
void pick_Set_Update(uint8_t remote)
{
    control_state_t state = CONTROL_IDLE;
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
                        /* PICK opens at the support before closing; PLACE opens only. */
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
            else
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
    else if (gripper_Get_State() == CONTROL_ERROR)
    {
        pick_Set_Fault(gripper_Get_Error());
    }
}
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
