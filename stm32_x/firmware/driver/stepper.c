/* ============================================================================
 * stepper.c — [보드2] X축 TB6600 STEP/DIR 드라이버 (L2)
 *
 * TIM1 CH1(PA8)이 하드웨어로 PUL 펄스를 내고, PA9 = DIR, PC7 = ENA.
 * TIM1 업데이트 인터럽트 1회 = 펄스 1개 완료.
 * ============================================================================ */
#include "control.h"
/* 위치는 부호 있는 펄스 수: + = B 방향, - = A 방향.
 * (CONFIG_X_REVERSE는 DIR 핀만 뒤집고 카운트 방향은 그대로)
 * 목표가 있으면 TIM1 인터럽트가 목표 펄스에서 정확히 타이머를 멈춘다. */
/* 인터럽트와 공유하므로 volatile */
static volatile bool stepper_running;    /* 펄스 출력 중 */
static volatile bool stepper_forward;    /* true = B 방향 */
static volatile bool stepper_has_goal;   /* 목표 위치가 있는 이동인가 */
static volatile int32_t stepper_position; /* 현재 펄스 위치 */
static volatile int32_t stepper_goal;     /* 목표 펄스 위치 */
/* 펄스 출력 시작 (공통 처리). */
static void stepper_Set_Start(bool forward)
{
    /* 펄스가 나가는 중에는 DIR을 바꾸지 않는다: 먼저 멈추고 DIR 설정.
     * 첫 PUL 상승 엣지는 타이머 시작 후 반 주기 뒤라 TB6600의 DIR/ENA 준비 시간(≥ 5 µs)을 만족. */
    stepper_running = false;
    bsp_Set_Step_Timer_Stop();
    stepper_forward = forward;
    bsp_Set_X_Direction(forward != (CONFIG_X_REVERSE != 0U));
    bsp_Set_X_Enable(true);
    stepper_running = true;
    bsp_Set_Step_Timer(CONFIG_X_START_HZ);
}
/* 부팅 시: 펄스 정지, 드라이버 해제, 위치 0. */
void stepper_Init_Driver(void)
{
    stepper_running = false;
    stepper_has_goal = false;
    stepper_position = 0;
    bsp_Set_Step_Timer_Stop();
    bsp_Set_X_Enable(false);
}
/* 목표 없이 계속 회전 (센서 탐색용). */
void stepper_Set_Run(bool forward)
{
    stepper_has_goal = false;
    stepper_Set_Start(forward);
}
/* 절대 펄스 위치까지 이동하고 그 자리에서 정지. */
void stepper_Set_Goal(int32_t goal)
{
    if (goal == stepper_position)
    {
        stepper_Set_Stop();
        return;
    }
    stepper_goal = goal;
    stepper_has_goal = true;
    stepper_Set_Start(goal > stepper_position);
}
/* 정지. CONFIG_X_HOLD_ON_STOP = 0이면 드라이버도 해제(코일 전류 off). 위치는 유지. */
void stepper_Set_Stop(void)
{
    stepper_running = false;
    stepper_has_goal = false;
    bsp_Set_Step_Timer_Stop();
    if (CONFIG_X_HOLD_ON_STOP == 0U)
    {
        bsp_Set_X_Enable(false);
    }
}
/* 정지 + 드라이버 무조건 해제. */
void stepper_Set_Disable(void)
{
    stepper_Set_Stop();
    bsp_Set_X_Enable(false);
}
/* 이동 중 펄스 주파수 변경 (가감속용). */
void stepper_Set_Rate(uint16_t hz)
{
    if (stepper_running)
    {
        bsp_Set_Step_Timer(hz);
    }
}
/* TIM1 인터럽트에서 호출: 펄스 1개 완료 → 위치 ±1, 목표 도달 시 즉시 정지. */
void stepper_Set_Tick(void)
{
    if (stepper_running)
    {
        stepper_position += stepper_forward ? 1 : -1;
        if (stepper_has_goal && (stepper_position == stepper_goal))
        {
            stepper_Set_Stop();
        }
    }
}
/* 펄스 출력 중인가. */
bool stepper_Get_Running(void) { return stepper_running; }
/* 현재 펄스 위치. */
int32_t stepper_Get_Position(void) { return stepper_position; }
/* 센서로 위치 보정. 정렬된 32비트 쓰기 1회라 인터럽트와 경쟁해도 안전(Cortex-M4). */
void stepper_Set_Position(int32_t position) { stepper_position = position; }
/* 목표까지 남은 펄스 수. 목표 없는 이동이면 UINT32_MAX. */
uint32_t stepper_Get_Remaining(void)
{
    int32_t diff;
    if (!stepper_has_goal) { return UINT32_MAX; }
    diff = stepper_goal - stepper_position;
    return (uint32_t)((diff < 0) ? -diff : diff);
}
