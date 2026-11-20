#include "control.h"
void linact_Set_Run(bool down)
{
    bsp_Set_Z_Output(down != (CONFIG_Z_REVERSE != 0U), CONFIG_Z_DUTY_PERMILLE);
}
void linact_Set_Stop(void)
{
    bsp_Set_Z_Output(false, 0U);
}
