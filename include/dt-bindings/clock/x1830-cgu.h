/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides clock numbers for the ingenic,x1830-cgu DT binding.
 *
 * They are roughly ordered as:
 *   - external clocks
 *   - PLLs
 *   - muxes/dividers in the order they appear in the x1830 programmers manual
 *   - gates in order of their bit in the CLKGR* registers
 */

#ifndef __DT_BINDINGS_CLOCK_X1830_CGU_H__
#define __DT_BINDINGS_CLOCK_X1830_CGU_H__

#define X1830_CLK_EXCLK			0
#define X1830_CLK_RTCLK			1
#define X1830_CLK_APLL			2
#define X1830_CLK_MPLL			3
#define X1830_CLK_EPLL			4
#define X1830_CLK_VPLL			5
#define X1830_CLK_OTGPHY		6
#define X1830_CLK_SCLKA			7
#define X1830_CLK_CPUMUX		8
#define X1830_CLK_CPU			9
#define X1830_CLK_L2CACHE		10
#define X1830_CLK_AHB0			11
#define X1830_CLK_AHB2PMUX		12
#define X1830_CLK_AHB2			13
#define X1830_CLK_PCLK			14
#define X1830_CLK_DDR			15
#define X1830_CLK_MAC			16
#define X1830_CLK_LCD			17
#define X1830_CLK_MSCMUX		18
#define X1830_CLK_MSC0			19
#define X1830_CLK_MSC1			20
#define X1830_CLK_SSIPLL		21
#define X1830_CLK_SSIPLL_DIV2	22
#define X1830_CLK_SSIMUX		23
#define X1830_CLK_EMC			24
#define X1830_CLK_EFUSE			25
#define X1830_CLK_OTG			26
#define X1830_CLK_SSI0			27
#define X1830_CLK_SMB0			28
#define X1830_CLK_SMB1			29
#define X1830_CLK_SMB2			30
#define X1830_CLK_UART0			31
#define X1830_CLK_UART1			32
#define X1830_CLK_SSI1			33
#define X1830_CLK_SFC			34
#define X1830_CLK_PDMA			35
#define X1830_CLK_TCU			36
#define X1830_CLK_DTRNG			37
#define X1830_CLK_OST			38

#endif /* __DT_BINDINGS_CLOCK_X1830_CGU_H__ */
