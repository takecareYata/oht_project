#include "control.h"
int main(void)
{
    bsp_Init_Board();
#if BOARD_ID == 2
    stepper_Init_Driver();
#else
    linact_Set_Stop();
#endif
    controller_Init_State();
    comm_Set_Response();
    spislave_Init_Driver();
    for (;;)
    {
        controller_Set_Update();
    }
}
