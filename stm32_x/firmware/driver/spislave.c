/* ============================================================================
 * spislave.c — SPI2 슬레이브 + DMA (L2)
 *
 * Jetson과 24바이트 프레임을 주고받는다. RX = DMA1 Stream3/Ch0, TX = DMA1 Stream4/Ch0.
 *
 * 흐름: NSS 하강(인터럽트) → MISO 켬 → DMA가 24바이트 송수신
 *       → NSS 상승(인터럽트) → 길이·오류 검사 → completed에 보관 → 다음 프레임 준비
 *       → 메인 루프가 spislave_Get_Frame()으로 수거, comm.c가 CRC·내용 검사
 *
 * 응답은 "다음" 프레임에 실린다: 요청과 동시에 나가는 응답은 직전에 준비된 것이다.
 * ============================================================================ */
#include "stm32f411xe.h"
#include "control.h"
/* 버퍼를 셋으로 나눈다: DMA가 쓰는 버퍼 / 게시(다음 응답) / 완료(받은 프레임). */
static uint8_t spislave_rx[COMM_FRAME_SIZE];        /* RX DMA 대상 */
static uint8_t spislave_tx[COMM_FRAME_SIZE];        /* TX DMA 원본 */
static uint8_t spislave_published[COMM_FRAME_SIZE]; /* 메인 루프가 준비한 다음 응답 */
static uint8_t spislave_completed[COMM_FRAME_SIZE]; /* 다 받은 요청 프레임 (메인 루프가 수거) */
static volatile bool spislave_ready;  /* completed에 새 프레임 있음 */
static volatile bool spislave_error;  /* 전송 오류 (메인 루프가 TRANSPORT 고장 처리) */
/* MISO 선은 X·Z 두 보드가 같이 쓰는데, STM32F4 SPI 슬레이브는 NSS가 High여도 MISO를
 * 계속 구동한다. 그래서 PB14를 자기 NSS가 Low일 때만 AF(출력), 아니면 입력(High-Z)으로 둔다.
 * NSS 하강 인터럽트에서 켜므로 늦어질 수 있고, 그때 첫 바이트들(고정 헤더 5A 02 axis)이
 * 깨질 수 있다. Jetson이 CRC 검사 전에 헤더를 복원한다. */
static volatile bool spislave_selected; /* NSS Low 구간 안인가 */
/* MISO(PB14) 출력 켜기(AF) / 끄기(입력, High-Z). */
static void spislave_Set_Miso(bool drive)
{
    GPIOB->MODER = (GPIOB->MODER & ~(3UL << (14U * 2U))) | ((drive ? 2UL : 0UL) << (14U * 2U));
}
/* 진단용 카운터: ST-Link 디버거로 읽는다. 다시 준비하기 전에 하드웨어 플래그를 캡처해 둔다.
 * 통신 프로토콜 필드에는 영향 없음. */
volatile struct
{
    uint32_t cs_count;      /* 끝난 프레임 수 (NSS 상승) */
    uint32_t valid_count;   /* 길이·플래그가 정상인 프레임 수 (CRC는 미검사) */
    uint32_t invalid_count; /* 길이 부족·오버런·DMA 오류 프레임 수 */
    uint32_t rx_remaining;  /* 마지막 프레임 끝에서 RX DMA 남은 바이트 */
    uint32_t tx_remaining;  /* 마지막 프레임 끝에서 TX DMA 남은 바이트 */
    uint32_t spi_status;    /* SPI2->SR */
    uint32_t dma_rx_status; /* DMA1->LISR */
    uint32_t dma_tx_status; /* DMA1->HISR */
    uint32_t glitch_count; /* 선택 중에 NSS 하강이 또 옴 (상승 엣지 글리치) */
    uint8_t rx[COMM_FRAME_SIZE]; /* 마지막으로 받은 원시 바이트 */
} spislave_diag;
/* 다음 프레임 준비: SPI 리셋, DMA 재설정, 게시된 응답을 TX 버퍼로 복사.
 * 프레임이 끝날 때마다(NSS 상승) 인터럽트 안에서 호출된다. */
static void spislave_Set_Arm(void)
{
    uint32_t i;
    volatile uint32_t discard;
    SPI2->CR1 = 0U;
    DMA1_Stream3->CR &= ~DMA_SxCR_EN;
    DMA1_Stream4->CR &= ~DMA_SxCR_EN;
    while (((DMA1_Stream3->CR | DMA1_Stream4->CR) & DMA_SxCR_EN) != 0U)
    {
    }
    discard = SPI2->DR;
    discard = SPI2->SR;
    (void)discard;
    RCC->APB1RSTR |= RCC_APB1RSTR_SPI2RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_SPI2RST;
    DMA1->LIFCR = 0x0F400000U;
    DMA1->HIFCR = 0x3DU;
    for (i = 0U; i < COMM_FRAME_SIZE; i++)
    {
        spislave_tx[i] = spislave_published[i];
    }
    DMA1_Stream3->PAR = (uint32_t)&SPI2->DR;
    DMA1_Stream3->M0AR = (uint32_t)spislave_rx;
    DMA1_Stream3->NDTR = COMM_FRAME_SIZE;
    DMA1_Stream3->CR = DMA_SxCR_MINC | DMA_SxCR_PL_1;
    DMA1_Stream4->PAR = (uint32_t)&SPI2->DR;
    DMA1_Stream4->M0AR = (uint32_t)spislave_tx;
    DMA1_Stream4->NDTR = COMM_FRAME_SIZE;
    DMA1_Stream4->CR = DMA_SxCR_MINC | DMA_SxCR_DIR_0 | DMA_SxCR_PL_1;
    SPI2->CR2 = SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN;
    DMA1_Stream3->CR |= DMA_SxCR_EN;
    DMA1_Stream4->CR |= DMA_SxCR_EN;
    /* SPI 모드 0: CPOL = 0(SCK 평소 Low), CPHA = 0. Jetson도 SPI_MODE_0이어야 한다. */
    SPI2->CR1 = SPI_CR1_SPE;
}
/* 부팅 시: SPI2 핀(PB12~15), NSS 인터럽트, 첫 DMA 준비. */
void spislave_Init_Driver(void)
{
    uint32_t pin;
    /* PB12~15를 AF5(SPI2)로. 부팅·리셋 중을 위해 NSS 외부 풀업도 필요. */
    for (pin = 12U; pin < 16U; pin++)
    {
        GPIOB->MODER = (GPIOB->MODER & ~(3UL << (pin * 2U))) | (2UL << (pin * 2U));
        GPIOB->AFR[1] = (GPIOB->AFR[1] & ~(15UL << ((pin - 8U) * 4U))) | (5UL << ((pin - 8U) * 4U));
        GPIOB->OSPEEDR &= ~(3UL << (pin * 2U));
    }
    /* PB14(MISO)만 출력: 10 MHz 이하라 중간 속도면 충분하고,
     * 엣지를 완만하게 해 NSS/SCK로의 크로스토크를 줄인다. */
    GPIOB->OSPEEDR |= 1UL << (14U * 2U);
    /* PB12(NSS) 풀업: 평소 High. PB13(SCK) 풀다운: 모드 0이라 평소 Low (Jetson과 동일). */
    GPIOB->PUPDR = (GPIOB->PUPDR & ~((3UL << 24U) | (3UL << 26U))) | (1UL << 24U) | (2UL << 26U);
    spislave_selected = false;
    spislave_Set_Miso(false);
    SYSCFG->EXTICR[3] = (SYSCFG->EXTICR[3] & ~15UL) | 1UL;
    /* NSS 양쪽 엣지 인터럽트: 하강 = 선택(MISO 켬), 상승 = 프레임 끝. */
    EXTI->RTSR |= 1UL << 12U;
    EXTI->FTSR |= 1UL << 12U;
    EXTI->PR = 1UL << 12U;
    spislave_Set_Arm();
    EXTI->IMR |= 1UL << 12U;
    NVIC_SetPriority(EXTI15_10_IRQn, 0U);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}
/* NSS(PB12) 엣지 인터럽트 (우선순위 최고).
 * 하강: MISO 출력 켬. 상승: MISO 끄고, 프레임 검사 후 completed로 넘기고 다음 프레임 준비. */
void EXTI15_10_IRQHandler(void)
{
    uint32_t i;
    if ((EXTI->PR & (1UL << 12U)) != 0U)
    {
        EXTI->PR = 1UL << 12U;
        if ((GPIOB->IDR & (1UL << 12U)) == 0U)
        {
            /* NSS Low: 프레임 시작. 이미 선택 중이면 직전 상승이 글리치였던 것
             * → SPI/DMA는 계속 진행. */
            if (spislave_selected) { spislave_diag.glitch_count++; }
            spislave_selected = true;
            spislave_Set_Miso(true);
            return;
        }
        /* NSS High: MISO부터 놓고 프레임을 마무리한다. */
        spislave_Set_Miso(false);
        if (!spislave_selected) { return; } /* 시작 없이 온 상승 엣지 (부팅 직후 등) */
        spislave_selected = false;
        spislave_diag.cs_count++;
        spislave_diag.rx_remaining = DMA1_Stream3->NDTR;
        spislave_diag.tx_remaining = DMA1_Stream4->NDTR;
        spislave_diag.spi_status = SPI2->SR;
        spislave_diag.dma_rx_status = DMA1->LISR;
        spislave_diag.dma_tx_status = DMA1->HISR;
        for (i = 0U; i < COMM_FRAME_SIZE; i++)
        {
            spislave_diag.rx[i] = spislave_rx[i];
        }
        /* FEIF(FIFO 오류 플래그)는 무시: direct 모드에서는 SPI 요청이 이미 걸린 상태로
         * 스트림을 켜면(준비 시점에 TXE = 1) 항상 켜지지만 데이터는 정상 전송된다. 실제 손실은
         * NDTR ≠ 0, SPI OVR, DMA TE/DME로 드러나고, 내용은 나중에 CRC로 검사한다. */
        if ((spislave_diag.rx_remaining == 0U) &&
            ((spislave_diag.spi_status & (SPI_SR_OVR | SPI_SR_RXNE)) == 0U) &&
            ((spislave_diag.dma_rx_status & ((1UL << 24U) | (1UL << 25U))) == 0U) &&
            ((spislave_diag.dma_tx_status & ((1UL << 2U) | (1UL << 3U))) == 0U))
        {
            spislave_diag.valid_count++;
            /* 메인 루프가 이전 프레임을 아직 안 가져갔으면 덮어쓰지 않고 오류 */
            if (!spislave_ready)
            {
                for (i = 0U; i < COMM_FRAME_SIZE; i++)
                {
                    spislave_completed[i] = spislave_rx[i];
                }
                __DMB();
                spislave_ready = true;
            }
            else
            {
                spislave_error = true;
            }
        }
        else
        {
            spislave_diag.invalid_count++;
            spislave_error = true;
        }
        spislave_Set_Arm();
    }
}
/* 메인 루프: 새로 받은 프레임이 있으면 복사해 가져간다 (인터럽트 잠깐 차단). */
bool spislave_Get_Frame(uint8_t *frame)
{
    uint32_t mask = __get_PRIMASK();
    uint32_t i;
    bool ready;
    __disable_irq();
    ready = spislave_ready;
    if (ready)
    {
        for (i = 0U; i < COMM_FRAME_SIZE; i++)
        {
            frame[i] = spislave_completed[i];
        }
        spislave_ready = false;
    }
    __set_PRIMASK(mask);
    return ready;
}
/* 메인 루프: 다음 응답 프레임 게시. 다음 프레임 준비 때 TX로 복사된다. */
void spislave_Set_Response(const uint8_t *frame)
{
    uint32_t mask = __get_PRIMASK();
    uint32_t i;
    __disable_irq();
    for (i = 0U; i < COMM_FRAME_SIZE; i++)
    {
        spislave_published[i] = frame[i];
    }
    __set_PRIMASK(mask);
}
/* 전송 오류가 있었는지 읽고 지운다. */
bool spislave_Get_Error(void)
{
    uint32_t mask = __get_PRIMASK();
    bool error;
    __disable_irq();
    error = spislave_error;
    spislave_error = false;
    __set_PRIMASK(mask);
    return error;
}
