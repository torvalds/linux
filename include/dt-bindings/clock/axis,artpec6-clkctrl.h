/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ARTPEC-6 clock controller indexes
 *
 * Copyright 2016 Axis Communications AB.
 */

#ifndef DT_BINDINGS_CLK_ARTPEC6_CLKCTRL_H
#define DT_BINDINGS_CLK_ARTPEC6_CLKCTRL_H

#define ARTPEC6_CLK_CPU			0
#define ARTPEC6_CLK_CPU_PERIPH		1
#define ARTPEC6_CLK_NAND_CLKA		2
#define ARTPEC6_CLK_NAND_CLKB		3
#define ARTPEC6_CLK_ETH_ACLK		4
#define ARTPEC6_CLK_DMA_ACLK		5
#define ARTPEC6_CLK_PTP_REF		6
#define ARTPEC6_CLK_SD_PCLK		7
#define ARTPEC6_CLK_SD_IMCLK		8
#define ARTPEC6_CLK_I2S_HST		9
#define ARTPEC6_CLK_I2S0_CLK		10
#define ARTPEC6_CLK_I2S1_CLK		11
#define ARTPEC6_CLK_UART_PCLK		12
#define ARTPEC6_CLK_UART_REFCLK		13
#define ARTPEC6_CLK_I2C			14
#define ARTPEC6_CLK_SPI_PCLK		15
#define ARTPEC6_CLK_SPI_SSPCLK		16
#define ARTPEC6_CLK_SYS_TIMER		17
#define ARTPEC6_CLK_FRACDIV_IN		18
#define ARTPEC6_CLK_DBG_PCLK		19

/* This must be the highest clock index plus one. */
#define ARTPEC6_CLK_NUMCLOCKS		20

#endif
