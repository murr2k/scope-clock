/* pwm_dma.c -- F401 backend for display.h: PWM pair as a poor-man's dual DAC.
 *
 * The STM32F401 has no DAC, so X and Y are PWM duty cycles on TIM3_CH1 (PA6)
 * and TIM3_CH2 (PA7), each smoothed to an analog voltage by an external 2-pole
 * RC low-pass (see hardware/README.md).  Both channels are on ONE timer so they
 * share a carrier and latch new duty values on the same update event.
 *
 * TIM2 is the sample clock.  Its update event drives two DMA streams that write
 * TIM3_CCR1 and TIM3_CCR2 -- the PWM equivalent of the F407's single DHR12RD
 * write, and the reason X and Y never skew by a sample.
 *
 * DMA request mapping verified against RM0368 Table 28 (DMA1 request mapping,
 * STM32F401xB/C and xD/E):
 *     TIM2_UP -> DMA1 Stream 1, Channel 3
 *     TIM2_UP -> DMA1 Stream 7, Channel 3
 * Two independent streams on the same request is exactly what this needs.
 *
 * ---------------------------------------------------------------------------
 * BRING-UP CHECKLIST:
 *  1. Before wiring the filters, scope PA6 in normal timebase mode: expect a
 *     ~328 kHz square wave whose duty sweeps as the frame plays.
 *  2. Add the RC filters, scope in X-Y, run MODE_TEST -> centred box + circle
 *     + crosshair.  Square not square => the two RC networks do not match;
 *     axes swapped => swap the probes, do not rotate the geometry.
 *  3. Trace looks fuzzy/thick  => filter corner too high (more ripple).
 *     Corners rounded / shape shrunken => corner too low (too much lag).
 *     Use tools/preview.py --target f401 --pwm to predict before re-soldering.
 *  4. Beam parks in a corner instead of drawing => DMA not running; check the
 *     TIM2 UDE bit and that both streams were enabled.
 * ---------------------------------------------------------------------------
 */
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/cm3/nvic.h>
#include "board.h"
#include "display.h"

#define X_STREAM  DMA_STREAM1        /* TIM2_UP ch3 -> TIM3_CCR1 */
#define Y_STREAM  DMA_STREAM7        /* TIM2_UP ch3 -> TIM3_CCR2 */

static volatile frame_t *g_active;
static volatile frame_t *g_pending;

static void axis_stream_cfg(uint8_t stream, uint32_t periph, uint32_t mem,
                            uint32_t ndt, int want_tc_irq)
{
    dma_stream_reset(DMA1, stream);
    dma_channel_select(DMA1, stream, DMA_SxCR_CHSEL_3);
    dma_set_peripheral_address(DMA1, stream, periph);
    dma_set_memory_address(DMA1, stream, mem);
    dma_set_number_of_data(DMA1, stream, ndt);
    dma_set_transfer_mode(DMA1, stream, DMA_SxCR_DIR_MEM_TO_PERIPHERAL);
    /* CCR is a 32-bit register; the frame stores 16-bit words. */
    dma_set_peripheral_size(DMA1, stream, DMA_SxCR_PSIZE_32BIT);
    dma_set_memory_size(DMA1, stream, DMA_SxCR_MSIZE_16BIT);
    dma_enable_memory_increment_mode(DMA1, stream);
    dma_enable_circular_mode(DMA1, stream);
    if (want_tc_irq)
        dma_enable_transfer_complete_interrupt(DMA1, stream);
    dma_enable_stream(DMA1, stream);
}

static void pwm_carrier_init(void)
{
    rcc_periph_clock_enable(RCC_TIM3);

    timer_set_mode(TIM3, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM3, 0);
    timer_set_period(TIM3, PWM_ARR);
    timer_enable_preload(TIM3);                      /* ARPE */

    timer_set_oc_mode(TIM3, TIM_OC1, TIM_OCM_PWM1);
    timer_set_oc_mode(TIM3, TIM_OC2, TIM_OCM_PWM1);
    /* CCR preload: a DMA write lands at the next update, never mid-pulse. */
    timer_enable_oc_preload(TIM3, TIM_OC1);
    timer_enable_oc_preload(TIM3, TIM_OC2);
    timer_set_oc_value(TIM3, TIM_OC1, PWM_TOP / 2);  /* park at screen centre */
    timer_set_oc_value(TIM3, TIM_OC2, PWM_TOP / 2);
    timer_enable_oc_output(TIM3, TIM_OC1);
    timer_enable_oc_output(TIM3, TIM_OC2);

    timer_enable_counter(TIM3);
}

static void sample_clock_init(void)
{
    rcc_periph_clock_enable(RCC_TIM2);
    timer_set_mode(TIM2, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM2, 0);
    timer_set_period(TIM2, SAMPLE_TIM_ARR);
    timer_enable_irq(TIM2, TIM_DIER_UDE);   /* update event -> DMA request */
    timer_enable_counter(TIM2);
}

void display_init(frame_t *initial)
{
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_DMA1);

    /* PA6 = TIM3_CH1 (X), PA7 = TIM3_CH2 (Y); TIM3 is AF2 on the F4. */
    gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO6 | GPIO7);
    gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                            GPIO6 | GPIO7);
    gpio_set_af(GPIOA, GPIO_AF2, GPIO6 | GPIO7);

    g_active = initial;
    g_pending = 0;

    pwm_carrier_init();

    axis_stream_cfg(X_STREAM, (uint32_t)&TIM_CCR1(TIM3),
                    (uint32_t)initial->stream_a, initial->n, 1);
    axis_stream_cfg(Y_STREAM, (uint32_t)&TIM_CCR2(TIM3),
                    (uint32_t)initial->stream_b, initial->n, 0);

    nvic_enable_irq(NVIC_DMA1_STREAM1_IRQ);

    sample_clock_init();
}

void display_present(frame_t *f)
{
    g_pending = f;                      /* consumed by the TC ISR at boundary */
}

int display_swap_pending(void)
{
    return g_pending != 0;
}

/* Transfer-complete on the X stream = end of a frame == safe swap point.
 * TIM2 is halted across the reconfigure so the two streams cannot end up one
 * sample apart; the beam just dwells an extra ~12 us at its current point. */
void dma1_stream1_isr(void)
{
    if (!dma_get_interrupt_flag(DMA1, X_STREAM, DMA_TCIF))
        return;
    dma_clear_interrupt_flags(DMA1, X_STREAM, DMA_TCIF);

    if (g_pending) {
        frame_t *nf = (frame_t *)g_pending;

        timer_disable_counter(TIM2);

        dma_disable_stream(DMA1, X_STREAM);
        dma_disable_stream(DMA1, Y_STREAM);
        while (DMA_SCR(DMA1, X_STREAM) & DMA_SxCR_EN) { }
        while (DMA_SCR(DMA1, Y_STREAM) & DMA_SxCR_EN) { }

        dma_set_memory_address(DMA1, X_STREAM, (uint32_t)nf->stream_a);
        dma_set_number_of_data(DMA1, X_STREAM, nf->n);
        dma_set_memory_address(DMA1, Y_STREAM, (uint32_t)nf->stream_b);
        dma_set_number_of_data(DMA1, Y_STREAM, nf->n);

        dma_enable_stream(DMA1, X_STREAM);
        dma_enable_stream(DMA1, Y_STREAM);

        timer_enable_counter(TIM2);

        g_active  = nf;
        g_pending = 0;
    }
}
