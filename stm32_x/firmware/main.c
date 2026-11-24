/* ============================================================================
 * main.c — 펌웨어 진입점
 *
 * 하드웨어를 초기화한 뒤 무한 루프에서 controller_Set_Update()만 반복한다.
 * BOARD_ID 1 = Z축·집게 보드(STM32①), 2 = X축 보드(STM32②).
 * 부팅 직후 모든 모터 출력은 꺼져 있고, Jetson의 ARM 전에는 움직이지 않는다.
 * ============================================================================ */
#include "control.h"
int main(void)
{
    bsp_Init_Board(); /* 클록·핀·SysTick·워치독 */
#if BOARD_ID == 2
    stepper_Init_Driver(); /* X: TB6600 출력 off */
#else
    linact_Set_Stop();     /* Z: L298N 출력 off */
    gripstep_Init_Driver(); /* 집게 코일 off, 스텝 0 = 열림 */
#endif
    controller_Init_State(); /* 모든 모듈 상태 초기화 */
    comm_Set_Response();     /* 첫 응답 프레임 준비 */
    spislave_Init_Driver();  /* SPI 수신 시작 (마지막에 켬) */
    /* 슈퍼루프: 모든 처리는 controller_Set_Update() 안에서 */
    for (;;)
    {
        controller_Set_Update();
    }
}
