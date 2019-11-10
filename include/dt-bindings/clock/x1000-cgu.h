/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides clock numbers for the ingenic,x1000-cgu DT binding.
 *
 * They are roughly ordered as:
 *   - external clocks
 *   - PLLs
 *   - muxes/dividers in the order they appear in the x1000 programmers manual
 *   - gates in order of their bit in the CLKGR* registers
 */

#ifndef __DT_BINDINGS_CLOCK_X1000_CGU_H__
#define __DT_BINDINGS_CLOCK_X1000_CGU_H__

#define X1000_CLK_EXCLK		0
#define X1000_CLK_RTCLK		1
#define X1000_CLK_APLL		2
#define X1000_CLK_MPLL		3
#define X1000_CLK_SCLKA		4
#define X1000_CLK_CPUMUX	5
#define X1000_CLK_CPU		6
#define X1000_CLK_L2CACHE	7
#define X1000_CLK_AHB0		8
#define X1000_CLK_AHB2PMUX	9
#define X1000_CLK_AHB2		10
#define X1000_CLK_PCLK		11
#define X1000_CLK_DDR		12
#define X1000_CLK_MAC		13
#define X1000_CLK_MSCMUX	14
#define X1000_CLK_MSC0		15
#define X1000_CLK_MSC1		16
#define X1000_CLK_SSIPLL	17
#define X1000_CLK_SSIMUX	18
#define X1000_CLK_SFC		19
#define X1000_CLK_I2C0		20
#define X1000_CLK_I2C1		21
#define X1000_CLK_I2C2		22
#define X1000_CLK_UART0		23
#define X1000_CLK_UART1		24
#define X1000_CLK_UART2		25
#define X1000_CLK_SSI		26
#define X1000_CLK_PDMA		27

#endif /* __DT_BINDINGS_CLOCK_X1000_CGU_H__ */
