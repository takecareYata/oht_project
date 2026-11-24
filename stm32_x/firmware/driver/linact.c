/* ============================================================================
 * linact.c — [보드1] Z 리니어 액추에이터 드라이버 (L2)
 *
 * L298N 온/오프 제어 (속도 100%, PWM 없음). 실제 핀 조작은 bsp_Set_Z_Output().
 * ============================================================================ */
#include "control.h"
/* Z 구동 시작. down = true면 하강. CONFIG_Z_REVERSE로 방향 반전. */
void linact_Set_Run(bool down)
{
    bsp_Set_Z_Output(down != (CONFIG_Z_REVERSE != 0U), true);
}
/* Z 출력 off (ENA Low, IN1 = IN2 = Low). */
void linact_Set_Stop(void)
{
    bsp_Set_Z_Output(false, false);
}
