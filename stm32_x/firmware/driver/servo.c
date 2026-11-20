#include "control.h"
void servo_Set_Pulse(uint16_t pulse)
{
    if ((pulse >= 500U) && (pulse <= 2500U))
    {
        bsp_Set_Servo_Pulse(pulse);
    }
}
