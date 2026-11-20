#include "stm32f411xe.h"
#include "control.h"
/* SPI2 RX: DMA1 stream3 channel0. TX: stream4 channel0. */
static uint8_t spislave_rx[COMM_FRAME_SIZE];
static uint8_t spislave_tx[COMM_FRAME_SIZE];
static uint8_t spislave_published[COMM_FRAME_SIZE];
static uint8_t spislave_completed[COMM_FRAME_SIZE];
static volatile bool spislave_ready;
static volatile bool spislave_error;
/* Read through ST-Link; capture before rearming clears the hardware flags.
 * No wire-protocol fields are changed by these diagnostic counters. */
volatile struct
{
    uint32_t cs_count;
    uint32_t valid_count;
    uint32_t invalid_count;
    uint32_t rx_remaining;
    uint32_t tx_remaining;
    uint32_t spi_status;
    uint32_t dma_rx_status;
    uint32_t dma_tx_status;
    uint32_t glitch_count; /* NSS rising edges ignored because PB12 was already low again */
    uint8_t rx[COMM_FRAME_SIZE];
} spislave_diag;
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
    SPI2->CR1 = SPI_CR1_SPE;
}
void spislave_Init_Driver(void)
{
    uint32_t pin;
    /* NSS pulled high; external pull-up is also required for boot/reset. */
    for (pin = 12U; pin < 16U; pin++)
    {
        GPIOB->MODER = (GPIOB->MODER & ~(3UL << (pin * 2U))) | (2UL << (pin * 2U));
        GPIOB->AFR[1] = (GPIOB->AFR[1] & ~(15UL << ((pin - 8U) * 4U))) | (5UL << ((pin - 8U) * 4U));
        GPIOB->OSPEEDR &= ~(3UL << (pin * 2U));
    }
    /* PB14 (MISO) is the only output: medium speed is ample for <=10 MHz and
     * keeps edges slow enough to limit crosstalk onto NSS/SCK. */
    GPIOB->OSPEEDR |= 1UL << (14U * 2U);
    GPIOB->PUPDR = (GPIOB->PUPDR & ~(3UL << 24U)) | (1UL << 24U);
    SYSCFG->EXTICR[3] = (SYSCFG->EXTICR[3] & ~15UL) | 1UL;
    EXTI->RTSR |= 1UL << 12U;
    EXTI->FTSR &= ~(1UL << 12U);
    EXTI->PR = 1UL << 12U;
    spislave_Set_Arm();
    EXTI->IMR |= 1UL << 12U;
    NVIC_SetPriority(EXTI15_10_IRQn, 1U);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}
void EXTI15_10_IRQHandler(void)
{
    uint32_t i;
    if ((EXTI->PR & (1UL << 12U)) != 0U)
    {
        EXTI->PR = 1UL << 12U;
        /* A real end of frame leaves NSS high. If PB12 is already low again,
         * the edge was a glitch inside a transaction: do not reset the SPI and
         * DMA mid-frame. */
        if ((GPIOB->IDR & (1UL << 12U)) == 0U)
        {
            spislave_diag.glitch_count++;
            return;
        }
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
        /* FEIF (stream3 bit22, stream4 bit0) is ignored: in direct mode it is set
         * whenever a stream is enabled while the SPI request is already pending
         * (TXE=1 at arm time), and data still transfers. Real loss shows up as
         * NDTR != 0, SPI OVR, or DMA TE/DME, and the frame CRC is checked later. */
        if ((spislave_diag.rx_remaining == 0U) &&
            ((spislave_diag.spi_status & (SPI_SR_OVR | SPI_SR_RXNE)) == 0U) &&
            ((spislave_diag.dma_rx_status & ((1UL << 24U) | (1UL << 25U))) == 0U) &&
            ((spislave_diag.dma_tx_status & ((1UL << 2U) | (1UL << 3U))) == 0U))
        {
            spislave_diag.valid_count++;
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
