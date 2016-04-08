/*
 * Copyright (c) 2015 Vladimir Zapolskiy <vz@mleia.com>
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 *
 */

#ifndef __DT_BINDINGS_LPC32XX_CLOCK_H
#define __DT_BINDINGS_LPC32XX_CLOCK_H

/* LPC32XX System Control Block clocks */
#define LPC32XX_CLK_RTC		1
#define LPC32XX_CLK_DMA		2
#define LPC32XX_CLK_MLC		3
#define LPC32XX_CLK_SLC		4
#define LPC32XX_CLK_LCD		5
#define LPC32XX_CLK_MAC		6
#define LPC32XX_CLK_SD		7
#define LPC32XX_CLK_DDRAM	8
#define LPC32XX_CLK_SSP0	9
#define LPC32XX_CLK_SSP1	10
#define LPC32XX_CLK_UART3	11
#define LPC32XX_CLK_UART4	12
#define LPC32XX_CLK_UART5	13
#define LPC32XX_CLK_UART6	14
#define LPC32XX_CLK_IRDA	15
#define LPC32XX_CLK_I2C1	16
#define LPC32XX_CLK_I2C2	17
#define LPC32XX_CLK_TIMER0	18
#define LPC32XX_CLK_TIMER1	19
#define LPC32XX_CLK_TIMER2	20
#define LPC32XX_CLK_TIMER3	21
#define LPC32XX_CLK_TIMER4	22
#define LPC32XX_CLK_TIMER5	23
#define LPC32XX_CLK_WDOG	24
#define LPC32XX_CLK_I2S0	25
#define LPC32XX_CLK_I2S1	26
#define LPC32XX_CLK_SPI1	27
#define LPC32XX_CLK_SPI2	28
#define LPC32XX_CLK_MCPWM	29
#define LPC32XX_CLK_HSTIMER	30
#define LPC32XX_CLK_KEY		31
#define LPC32XX_CLK_PWM1	32
#define LPC32XX_CLK_PWM2	33
#define LPC32XX_CLK_ADC		34
#define LPC32XX_CLK_HCLK_PLL	35

/* LPC32XX USB clocks */
#define LPC32XX_USB_CLK_I2C	1
#define LPC32XX_USB_CLK_DEVICE	2
#define LPC32XX_USB_CLK_HOST	3

#endif /* __DT_BINDINGS_LPC32XX_CLOCK_H */
