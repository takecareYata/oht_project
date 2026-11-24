/* ============================================================================
 * gripstep.c — [보드1] 집게 스텝모터 드라이버 (L2)
 *
 * 28BYJ-48(유니폴라) + ULN2003. 약 4096 하프스텝 = 출력축 1회전.
 * 호출 1번 = 1스텝. 속도와 정지 판단은 gripper.c가 한다.
 * ============================================================================ */
#include "control.h"
/* 하프스텝 8패턴. bit0~bit3 = IN1~IN4 (1 = 코일 on).
 * 위치는 하프스텝 수: + = 닫는 방향, 0 = 열림(전원 인가 시 열림이라고 가정). */
static const uint8_t gripstep_patterns[8] = {0x01U, 0x03U, 0x02U, 0x06U,
                                              0x04U, 0x0CU, 0x08U, 0x09U};
static uint8_t gripstep_phase;     /* 현재 패턴 인덱스 0~7 */
static int32_t gripstep_position;  /* 하프스텝 위치 (0 = 열림) */
/* 부팅 시: 코일 off, 위치 0(열림)으로 초기화. */
void gripstep_Init_Driver(void)
{
    gripstep_phase = 0U;
    gripstep_position = 0;
    bsp_Set_Grip_Phase(0U);
}
/* 한 스텝 이동. close = true면 닫는 방향. CONFIG_GRIP_REVERSE로 회전 방향 반전. */
void gripstep_Set_Step(bool close)
{
    bool forward = (close != (CONFIG_GRIP_REVERSE != 0U));
    gripstep_phase = (uint8_t)((gripstep_phase + (forward ? 1U : 7U)) & 7U);
    bsp_Set_Grip_Phase(gripstep_patterns[gripstep_phase]);
    gripstep_position += close ? 1 : -1;
}
/* 코일 전부 off. 1:64 기어가 전류 없이도 집게 위치를 유지한다. */
void gripstep_Set_Release(void)
{
    bsp_Set_Grip_Phase(0U);
}
/* 현재 하프스텝 위치. */
int32_t gripstep_Get_Position(void) { return gripstep_position; }
