/**
 * @file    pio.h
 * @brief   [L2 Driver] E84 PIO GPIO 입출력 드라이버
 * @details CSC : Load Port / CSU : E84 Passive
 *          입력 PC0~PC4 (Jetson Active -> Load Port), 출력 PC5~PC9 (Load Port -> Jetson)
 *          신호 극성(Active-High)은 이 파일에서만 처리한다.
 */
#ifndef PIO_H
#define PIO_H

#include <stdint.h>
#include <stdbool.h>

/* 입력 비트 (= PC0~PC4 핀 번호 순서) */
#define PIO_IN_VALID        (0x01U)     /* PC0 */
#define PIO_IN_CS_0         (0x02U)     /* PC1 */
#define PIO_IN_TR_REQ       (0x04U)     /* PC2 */
#define PIO_IN_BUSY         (0x08U)     /* PC3 */
#define PIO_IN_COMPT        (0x10U)     /* PC4 */

/* 출력 비트 (= PC5~PC9 핀 번호 순서) */
#define PIO_OUT_L_REQ       (0x01U)     /* PC5 */
#define PIO_OUT_U_REQ       (0x02U)     /* PC6 */
#define PIO_OUT_READY       (0x04U)     /* PC7 */
#define PIO_OUT_HO_AVBL     (0x08U)     /* PC8 */
#define PIO_OUT_ES          (0x10U)     /* PC9 */
#define PIO_OUT_ALL         (0x1FU)

void    Pio_Init_Gpio(void);
uint8_t Pio_Get_Input(void);
void    Pio_Set_Output(uint8_t outMask, bool on);
uint8_t Pio_Get_Output(void);

#endif /* PIO_H */
