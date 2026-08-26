/* dac_dma.c -- F407 backend for display.h.
 *
 * ---------------------------------------------------------------------------
 * BRING-UP CHECKLIST (this file is the primary on-hardware validation target):
 *  1. Scope in X-Y; run MODE_TEST -> expect a centred box + circle + crosshair.
 *     If axes swap/mirror, fix pack_xy()/op-amp polarity, not the geometry.
 *  2. Confirm sample rate: TIM6 update = SAMPLE_RATE_HZ (toggle a pin in the
 *     TC ISR and check on a logic analyser).  ARR assumes APB1 timer = 84 MHz.
 *  3. DMA request mapping is per RM0090 Table 42/43:
 *       DAC1 (ch1) request  -> DMA1 Stream5, Channel 7
 *       TIM6_UP    request  -> DMA1 Stream1, Channel 7
 *     Verify before trusting the Z stream.
 *  4. Z polarity: MODE_TEST retrace (box corner -> circle) must be dark.
 *     Flip ZBLANK_ACTIVE_HIGH in board.h if the retrace is bright instead.
 * ---------------------------------------------------------------------------
 */
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/dac.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/nvic.h>
#include "board.h"
#include "display.h"

/* Registers come from libopencm3's macros, not hand-rolled base+offset
 * arithmetic: redefining DAC1_BASE/TIM6_BASE shadowed the library's own
 * definitions, which is exactly how a wrong offset slips through unnoticed. */

/* CR bits: EN1=0 TEN1=2 DMAEN1=12 EN2=16 TEN2=18; TSEL=000 selects TIM6 TRGO */
#define DAC_CR_VALUE ((1u<<0) | (1u<<2) | (1u<<12) | (1u<<16) | (1u<<18))

#define TIM6_ARR_VALUE ((TIMER_CLK_HZ / SAMPLE_RATE_HZ) - 1u)

#define ZBSRR_ADDR ((uint32_t)&GPIO_BSRR(ZBLANK_PORT))

static volatile frame_t *g_active;
static volatile frame_t *g_pending;

static void dac_stream_cfg(uint32_t mem, uint32_t ndt)
{
    dma_stream_reset(DMA1, DMA_STREAM5);
    dma_channel_select(DMA1, DMA_STREAM5, DMA_SxCR_CHSEL_7);
    dma_set_peripheral_address(DMA1, DMA_STREAM5, (uint32_t)&DAC_DHR12RD(DAC1));
    dma_set_memory_address(DMA1, DMA_STREAM5, mem);
    dma_set_number_of_data(DMA1, DMA_STREAM5, ndt);
    dma_set_transfer_mode(DMA1, DMA_STREAM5, DMA_SxCR_DIR_MEM_TO_PERIPHERAL);
    dma_set_peripheral_size(DMA1, DMA_STREAM5, DMA_SxCR_PSIZE_32BIT);
    dma_set_memory_size(DMA1, DMA_STREAM5, DMA_SxCR_MSIZE_32BIT);
    dma_enable_memory_increment_mode(DMA1, DMA_STREAM5);
    dma_enable_circular_mode(DMA1, DMA_STREAM5);
    dma_enable_transfer_complete_interrupt(DMA1, DMA_STREAM5);
    dma_enable_stream(DMA1, DMA_STREAM5);
}

static void z_stream_cfg(uint32_t mem, uint32_t ndt)
{
    dma_stream_reset(DMA1, DMA_STREAM1);
    dma_channel_select(DMA1, DMA_STREAM1, DMA_SxCR_CHSEL_7);
    dma_set_peripheral_address(DMA1, DMA_STREAM1, ZBSRR_ADDR);
    dma_set_memory_address(DMA1, DMA_STREAM1, mem);
    dma_set_number_of_data(DMA1, DMA_STREAM1, ndt);
    dma_set_transfer_mode(DMA1, DMA_STREAM1, DMA_SxCR_DIR_MEM_TO_PERIPHERAL);
    dma_set_peripheral_size(DMA1, DMA_STREAM1, DMA_SxCR_PSIZE_32BIT);
    dma_set_memory_size(DMA1, DMA_STREAM1, DMA_SxCR_MSIZE_32BIT);
    dma_enable_memory_increment_mode(DMA1, DMA_STREAM1);
    dma_enable_circular_mode(DMA1, DMA_STREAM1);
    dma_enable_stream(DMA1, DMA_STREAM1);
}

void z_blank_now(void)
{
    GPIO_BSRR(ZBLANK_PORT) = ZBSRR_OFF;
}

void display_init(frame_t *initial)
{
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOE);
    rcc_periph_clock_enable(RCC_DAC);
    rcc_periph_clock_enable(RCC_TIM6);
    rcc_periph_clock_enable(RCC_DMA1);

    /* PA4/PA5 analog for DAC1_OUT1/OUT2 */
    gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO4 | GPIO5);
    /* PE2 push-pull Z-blank, start blanked */
    gpio_mode_setup(ZBLANK_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, ZBLANK_PIN);
    gpio_set_output_options(ZBLANK_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ,
                            ZBLANK_PIN);
    z_blank_now();

    g_active = initial;
    g_pending = 0;

    /* DMA streams first, then DAC, then start the timer */
    dac_stream_cfg((uint32_t)initial->stream_a, initial->n);
    z_stream_cfg((uint32_t)initial->stream_b, initial->n);

    DAC_CR(DAC1) = DAC_CR_VALUE;          /* both channels: TIM6 trig + ch1 DMA */

    nvic_enable_irq(NVIC_DMA1_STREAM5_IRQ);

    TIM6_CR1  = 0;
    TIM6_CR2  = (2u << 4);              /* MMS = 010: TRGO on update -> DAC    */
    TIM6_DIER = (1u << 8);              /* UDE: update DMA request -> Z stream */
    TIM6_PSC  = 0;
    TIM6_ARR  = TIM6_ARR_VALUE;
    TIM6_EGR  = 1u;                     /* UG: load PSC/ARR                    */
    TIM6_CR1  = 1u;                     /* CEN: run                            */
}

void display_present(frame_t *f)
{
    g_pending = f;                      /* consumed by the TC ISR at boundary */
}

int display_swap_pending(void)
{
    return g_pending != 0;
}

/* Transfer-complete on the DAC stream = end of a frame == safe swap point. */
void dma1_stream5_isr(void)
{
    if (!dma_get_interrupt_flag(DMA1, DMA_STREAM5, DMA_TCIF))
        return;
    dma_clear_interrupt_flags(DMA1, DMA_STREAM5, DMA_TCIF);

    if (g_pending) {
        frame_t *nf = (frame_t *)g_pending;

        dma_disable_stream(DMA1, DMA_STREAM5);
        dma_disable_stream(DMA1, DMA_STREAM1);
        while (DMA_SCR(DMA1, DMA_STREAM5) & DMA_SxCR_EN) { }
        while (DMA_SCR(DMA1, DMA_STREAM1) & DMA_SxCR_EN) { }

        dma_set_memory_address(DMA1, DMA_STREAM5, (uint32_t)nf->stream_a);
        dma_set_number_of_data(DMA1, DMA_STREAM5, nf->n);
        dma_set_memory_address(DMA1, DMA_STREAM1, (uint32_t)nf->stream_b);
        dma_set_number_of_data(DMA1, DMA_STREAM1, nf->n);

        dma_enable_stream(DMA1, DMA_STREAM5);
        dma_enable_stream(DMA1, DMA_STREAM1);

        g_active  = nf;
        g_pending = 0;
    }
}
