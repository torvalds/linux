/*
 * Copyright (c) 2015 Joachim Eastwood <manabian@gmail.com>
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 *
 */

/* LPC18xx/43xx base clock ids */
#define BASE_SAFE_CLK		0
#define BASE_USB0_CLK		1
#define BASE_PERIPH_CLK		2
#define BASE_USB1_CLK		3
#define BASE_CPU_CLK		4
#define BASE_SPIFI_CLK		5
#define BASE_SPI_CLK		6
#define BASE_PHY_RX_CLK		7
#define BASE_PHY_TX_CLK		8
#define BASE_APB1_CLK		9
#define BASE_APB3_CLK		10
#define BASE_LCD_CLK		11
#define BASE_ADCHS_CLK		12
#define BASE_SDIO_CLK		13
#define BASE_SSP0_CLK		14
#define BASE_SSP1_CLK		15
#define BASE_UART0_CLK		16
#define BASE_UART1_CLK		17
#define BASE_UART2_CLK		18
#define BASE_UART3_CLK		19
#define BASE_OUT_CLK		20
#define BASE_RES1_CLK		21
#define BASE_RES2_CLK		22
#define BASE_RES3_CLK		23
#define BASE_RES4_CLK		24
#define BASE_AUDIO_CLK		25
#define BASE_CGU_OUT0_CLK	26
#define BASE_CGU_OUT1_CLK	27
#define BASE_CLK_MAX		(BASE_CGU_OUT1_CLK + 1)
