/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides clock numbers for the ingenic,jz4740-cgu DT binding.
 *
 * They are roughly ordered as:
 *   - external clocks
 *   - PLLs
 *   - muxes/dividers in the order they appear in the jz4740 programmers manual
 *   - gates in order of their bit in the CLKGR* registers
 */

#ifndef __DT_BINDINGS_CLOCK_JZ4740_CGU_H__
#define __DT_BINDINGS_CLOCK_JZ4740_CGU_H__

#define JZ4740_CLK_EXT		0
#define JZ4740_CLK_RTC		1
#define JZ4740_CLK_PLL		2
#define JZ4740_CLK_PLL_HALF	3
#define JZ4740_CLK_CCLK		4
#define JZ4740_CLK_HCLK		5
#define JZ4740_CLK_PCLK		6
#define JZ4740_CLK_MCLK		7
#define JZ4740_CLK_LCD		8
#define JZ4740_CLK_LCD_PCLK	9
#define JZ4740_CLK_I2S		10
#define JZ4740_CLK_SPI		11
#define JZ4740_CLK_MMC		12
#define JZ4740_CLK_UHC		13
#define JZ4740_CLK_UDC		14
#define JZ4740_CLK_UART0	15
#define JZ4740_CLK_UART1	16
#define JZ4740_CLK_DMA		17
#define JZ4740_CLK_IPU		18
#define JZ4740_CLK_ADC		19
#define JZ4740_CLK_I2C		20
#define JZ4740_CLK_AIC		21
#define JZ4740_CLK_TCU		22

#endif /* __DT_BINDINGS_CLOCK_JZ4740_CGU_H__ */
