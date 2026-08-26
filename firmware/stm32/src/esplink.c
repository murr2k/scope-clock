/* esplink.c -- see esplink.h.  USART2 on PA2 (TX) / PA3 (RX). */
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/cm3/nvic.h>
#include <string.h>
#include <stdlib.h>
#include "board.h"
#include "esplink.h"

#define LINE_MAX 96

static char linebuf[LINE_MAX];
static uint8_t linelen;

static void parse_line(char *s)
{
    if (!strncmp(s, "T=", 2)) {
        app_set_epoch((uint32_t)strtoul(s + 2, NULL, 10));
    } else if (!strncmp(s, "TZ=", 3)) {
        app_set_tz((int32_t)strtol(s + 3, NULL, 10));
    } else if (!strncmp(s, "M=", 2)) {
        app_set_message(s + 2);
    } else if (!strncmp(s, "MODE=", 5)) {
        app_set_mode_name(s + 5);
    }
}

void esplink_init(void)
{
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_USART2);

    gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO2 | GPIO3);
    gpio_set_af(GPIOA, GPIO_AF7, GPIO2 | GPIO3);   /* USART2 = AF7 */

    usart_set_baudrate(ESP_USART, ESP_BAUD);
    usart_set_databits(ESP_USART, 8);
    usart_set_stopbits(ESP_USART, USART_STOPBITS_1);
    usart_set_parity(ESP_USART, USART_PARITY_NONE);
    usart_set_flow_control(ESP_USART, USART_FLOWCONTROL_NONE);
    usart_set_mode(ESP_USART, USART_MODE_TX_RX);

    usart_enable_rx_interrupt(ESP_USART);
    nvic_enable_irq(NVIC_USART2_IRQ);
    usart_enable(ESP_USART);
}

void usart2_isr(void)
{
    if (!usart_get_flag(ESP_USART, USART_FLAG_RXNE))
        return;
    char c = (char)usart_recv(ESP_USART);
    if (c == '\r')
        return;
    if (c == '\n') {
        linebuf[linelen] = '\0';
        if (linelen)
            parse_line(linebuf);
        linelen = 0;
    } else if (linelen < LINE_MAX - 1) {
        linebuf[linelen++] = c;
    } else {
        linelen = 0;               /* overflow: drop the runaway line */
    }
}
