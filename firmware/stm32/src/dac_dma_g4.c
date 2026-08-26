/* dac_dma_g4.c -- G431 backend for display.h.
 *
 * Same architecture as the F407 backend, but the G4 has DMA *channels* behind a
 * DMAMUX instead of the F4's fixed request-to-stream mapping, so any peripheral
 * request can be routed to any channel.  That removes the F4's
 * "which stream is TIM6_UP on?" puzzle entirely.
 *
 * DMAMUX request IDs are from libopencm3's own stm32/g4/dmamux.h:
 *     DMAMUX_CxCR_DMAREQ_ID_DAC1_CH1 = 6
 *     DMAMUX_CxCR_DMAREQ_ID_DAC1_CH2 = 7
 *     DMAMUX_CxCR_DMAREQ_ID_TIM6_UP  = 8
 * DMAMUX1 channel N drives DMA1 channel N+1.
 *
 * ---------------------------------------------------------------------------
 * BRING-UP CHECKLIST:
 *  1. Scope in X-Y, MODE_TEST -> centred box + circle + crosshair.  If the
 *     axes swap or mirror, fix pack_xy() or the probe order, not the geometry.
 *  2. TIM6 update must be SAMPLE_RATE_HZ.  ARR assumes the 170 MHz PLL came up
 *     (boost mode: the G4 needs the R1MODE/voltage-scaling sequence at 170 MHz;
 *     libopencm3's rcc_clock_setup_pll handles it, but verify with the LED or
 *     a toggled pin before blaming the DMA).
 *  3. Z polarity: MODE_TEST retrace must be dark; flip ZBLANK_ACTIVE_HIGH.
 *  4. If nothing moves at all, check DAC1's output buffers are ENABLED (they
 *     are by default -- HFSEL/MODE bits) and that PA4/PA5 are in analog mode.
 *  5. libopencm3's DAC API changed across versions (DAC vs DAC1 instance
 *     macros).  This file pokes the DAC registers directly to avoid that.
 * ---------------------------------------------------------------------------
 */
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/dmamux.h>
#include <libopencm3/stm32/dac.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/nvic.h>
#include "board.h"
#include "display.h"

/* Register access goes through libopencm3's own macros rather than hand-rolled
 * base+offset arithmetic.  That is not just style: DAC_MCR sits at offset 0x3C,
 * not the 0x38 an earlier version of this file assumed (0x38 is DAC_CCR), and a
 * wrong offset there fails silently. */

/* CR built from libopencm3's macros, NOT copied from the F407 backend.
 *
 * The trigger encodings are INVERTED between the two families, and getting it
 * wrong is silent -- the DAC simply never requests DMA:
 *     F4 (dac_common_v1): TSEL1 = 0 -> TIM6_TRGO,        7 -> software
 *     G4 (dac_common_v2): TSEL1 = 0 -> internal clock,   7 -> TIM6  (T6)
 * The G4 also moves TEN1 to bit 1 (bit 2 on the F4) and widens TSEL1 to bits
 * 5:2, so the F407's hand-rolled `1<<2` lands *inside* TSEL1 and leaves the
 * trigger disabled altogether.  Seen on hardware as DMA1_CNDTR frozen at the
 * frame length with DHR12RD never written and the beam parked at 0,0.
 *
 * Only channel 1 requests DMA: in dual mode one DHR12RD write feeds both. */
#define DAC_CR_VALUE (DAC_CR_EN1 | DAC_CR_TEN1 | DAC_CR_TSEL1_T6 | \
                      DAC_CR_DMAEN1 | \
                      DAC_CR_EN2 | DAC_CR_TEN2 | DAC_CR_TSEL2_T6)
/* MCR MODE1/MODE2 = 000 -> external pin WITH the output buffer enabled, which
 * is what lets the DAC drive the scope with no op-amp. */
#define DAC_MCR_VALUE (DAC_MCR_MODE1_E_BUFF | DAC_MCR_MODE2_E_BUFF)

#define XY_DMA_CH        DMA_CHANNEL1     /* DAC1_CH1 request -> DHR12RD */
#define Z_DMA_CH         DMA_CHANNEL2     /* TIM6_UP  request -> GPIO BSRR */

#define TIM6_ARR_VALUE   ((TIMER_CLK_HZ / SAMPLE_RATE_HZ) - 1u)
#define ZBSRR_ADDR       ((uint32_t)&GPIO_BSRR(ZBLANK_PORT))

static volatile frame_t *g_active;
static volatile frame_t *g_pending;

static void chan_cfg(uint8_t chan, uint8_t request,
                     uint32_t periph, uint32_t mem, uint32_t ndt,
                     int want_tc_irq)
{
    dma_channel_reset(DMA1, chan);
    dmamux_set_dma_channel_request(DMAMUX1, chan, request);
    dma_set_peripheral_address(DMA1, chan, periph);
    dma_set_memory_address(DMA1, chan, mem);
    dma_set_number_of_data(DMA1, chan, ndt);
    dma_set_read_from_memory(DMA1, chan);
    dma_set_peripheral_size(DMA1, chan, DMA_CCR_PSIZE_32BIT);
    dma_set_memory_size(DMA1, chan, DMA_CCR_MSIZE_32BIT);
    dma_enable_memory_increment_mode(DMA1, chan);
    dma_enable_circular_mode(DMA1, chan);
    if (want_tc_irq)
        dma_enable_transfer_complete_interrupt(DMA1, chan);
    dma_enable_channel(DMA1, chan);
}

void z_blank_now(void)
{
    GPIO_BSRR(ZBLANK_PORT) = ZBSRR_OFF;
}

void display_init(frame_t *initial)
{
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_DAC1);
    rcc_periph_clock_enable(RCC_TIM6);
    rcc_periph_clock_enable(RCC_DMA1);
    rcc_periph_clock_enable(RCC_DMAMUX1);

    /* PA4/PA5 analog: DAC1_OUT1 / DAC1_OUT2 (buffered, drive the scope direct) */
    gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO4 | GPIO5);
    /* PB6 push-pull Z-blank, start blanked */
    gpio_mode_setup(ZBLANK_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, ZBLANK_PIN);
    gpio_set_output_options(ZBLANK_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ,
                            ZBLANK_PIN);
    z_blank_now();

    g_active = initial;
    g_pending = 0;

    chan_cfg(XY_DMA_CH, DMAMUX_CxCR_DMAREQ_ID_DAC1_CH1,
             (uint32_t)&DAC_DHR12RD(DAC1),
             (uint32_t)initial->stream_a, initial->n, 1);
    chan_cfg(Z_DMA_CH, DMAMUX_CxCR_DMAREQ_ID_TIM6_UP, ZBSRR_ADDR,
             (uint32_t)initial->stream_b, initial->n, 0);

    /* MCR must be written while the channels are still disabled. */
    DAC_MCR(DAC1) = DAC_MCR_VALUE;      /* buffered output to the pins */
    DAC_CR(DAC1)  = DAC_CR_VALUE;       /* both channels, TIM6 trig, ch1 DMA */

    nvic_enable_irq(NVIC_DMA1_CHANNEL1_IRQ);

    /* TIM6: sample clock; TRGO on update drives the DAC, UDE the Z channel. */
    timer_set_prescaler(TIM6, 0);
    timer_set_period(TIM6, TIM6_ARR_VALUE);
    timer_set_master_mode(TIM6, TIM_CR2_MMS_UPDATE);
    timer_enable_irq(TIM6, TIM_DIER_UDE);
    timer_enable_counter(TIM6);
}

void display_present(frame_t *f)
{
    g_pending = f;
}

int display_swap_pending(void)
{
    return g_pending != 0;
}

/* Transfer-complete on the X/Y channel = frame boundary == safe swap point. */
void dma1_channel1_isr(void)
{
    if (!dma_get_interrupt_flag(DMA1, XY_DMA_CH, DMA_TCIF))
        return;
    dma_clear_interrupt_flags(DMA1, XY_DMA_CH, DMA_TCIF);

    if (g_pending) {
        frame_t *nf = (frame_t *)g_pending;

        timer_disable_counter(TIM6);    /* keep the two channels in step */

        dma_disable_channel(DMA1, XY_DMA_CH);
        dma_disable_channel(DMA1, Z_DMA_CH);

        dma_set_memory_address(DMA1, XY_DMA_CH, (uint32_t)nf->stream_a);
        dma_set_number_of_data(DMA1, XY_DMA_CH, nf->n);
        dma_set_memory_address(DMA1, Z_DMA_CH, (uint32_t)nf->stream_b);
        dma_set_number_of_data(DMA1, Z_DMA_CH, nf->n);

        dma_enable_channel(DMA1, XY_DMA_CH);
        dma_enable_channel(DMA1, Z_DMA_CH);

        timer_enable_counter(TIM6);

        g_active  = nf;
        g_pending = 0;
    }
}
