#include "control.h"
static bool controller_armed;
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
void controller_Set_Update(void)
{
    control_command_t command;
    bool have;
    bool fault = false;
    control_state_t state;
    control_error_t error;
    sensor_Set_Update();
    comm_Set_Update();
    have = comm_Get_Command(&command);
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
    if (have && fault && (command.cmd != CONTROL_CMD_STOP))
    {
        comm_Set_Receipt(status_Get_Error());
    }
    if (have && !fault)
    {
        if (command.cmd == CONTROL_CMD_ARM)
        {
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
        else if (!controller_armed)
        {
            status_Set_State(CONTROL_STOPPED, CONTROL_ERR_STOP);
        }
        else
        {
            status_Set_Sequence(command.seq);
            status_Set_State(CONTROL_BUSY, CONTROL_OK);
#if BOARD_ID == 1
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
    position_Set_Update();
    status_Set_Update();
    comm_Set_Response();
    bsp_Set_Watchdog();
}

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
