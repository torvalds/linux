/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides clock numbers for the ingenic,jz4780-cgu DT binding.
 *
 * They are roughly ordered as:
 *   - external clocks
 *   - PLLs
 *   - muxes/dividers in the order they appear in the jz4780 programmers manual
 *   - gates in order of their bit in the CLKGR* registers
 */

#ifndef __DT_BINDINGS_CLOCK_JZ4780_CGU_H__
#define __DT_BINDINGS_CLOCK_JZ4780_CGU_H__

#define JZ4780_CLK_EXCLK	0
#define JZ4780_CLK_RTCLK	1
#define JZ4780_CLK_APLL		2
#define JZ4780_CLK_MPLL		3
#define JZ4780_CLK_EPLL		4
#define JZ4780_CLK_VPLL		5
#define JZ4780_CLK_OTGPHY	6
#define JZ4780_CLK_SCLKA	7
#define JZ4780_CLK_CPUMUX	8
#define JZ4780_CLK_CPU		9
#define JZ4780_CLK_L2CACHE	10
#define JZ4780_CLK_AHB0		11
#define JZ4780_CLK_AHB2PMUX	12
#define JZ4780_CLK_AHB2		13
#define JZ4780_CLK_PCLK		14
#define JZ4780_CLK_DDR		15
#define JZ4780_CLK_VPU		16
#define JZ4780_CLK_I2SPLL	17
#define JZ4780_CLK_I2S		18
#define JZ4780_CLK_LCD0PIXCLK	19
#define JZ4780_CLK_LCD1PIXCLK	20
#define JZ4780_CLK_MSCMUX	21
#define JZ4780_CLK_MSC0		22
#define JZ4780_CLK_MSC1		23
#define JZ4780_CLK_MSC2		24
#define JZ4780_CLK_UHC		25
#define JZ4780_CLK_SSIPLL	26
#define JZ4780_CLK_SSI		27
#define JZ4780_CLK_CIMMCLK	28
#define JZ4780_CLK_PCMPLL	29
#define JZ4780_CLK_PCM		30
#define JZ4780_CLK_GPU		31
#define JZ4780_CLK_HDMI		32
#define JZ4780_CLK_BCH		33
#define JZ4780_CLK_NEMC		34
#define JZ4780_CLK_OTG0		35
#define JZ4780_CLK_SSI0		36
#define JZ4780_CLK_SMB0		37
#define JZ4780_CLK_SMB1		38
#define JZ4780_CLK_SCC		39
#define JZ4780_CLK_AIC		40
#define JZ4780_CLK_TSSI0	41
#define JZ4780_CLK_OWI		42
#define JZ4780_CLK_KBC		43
#define JZ4780_CLK_SADC		44
#define JZ4780_CLK_UART0	45
#define JZ4780_CLK_UART1	46
#define JZ4780_CLK_UART2	47
#define JZ4780_CLK_UART3	48
#define JZ4780_CLK_SSI1		49
#define JZ4780_CLK_SSI2		50
#define JZ4780_CLK_PDMA		51
#define JZ4780_CLK_GPS		52
#define JZ4780_CLK_MAC		53
#define JZ4780_CLK_SMB2		54
#define JZ4780_CLK_CIM		55
#define JZ4780_CLK_LCD		56
#define JZ4780_CLK_TVE		57
#define JZ4780_CLK_IPU		58
#define JZ4780_CLK_DDR0		59
#define JZ4780_CLK_DDR1		60
#define JZ4780_CLK_SMB3		61
#define JZ4780_CLK_TSSI1	62
#define JZ4780_CLK_COMPRESS	63
#define JZ4780_CLK_AIC1		64
#define JZ4780_CLK_GPVLC	65
#define JZ4780_CLK_OTG1		66
#define JZ4780_CLK_UART4	67
#define JZ4780_CLK_AHBMON	68
#define JZ4780_CLK_SMB4		69
#define JZ4780_CLK_DES		70
#define JZ4780_CLK_X2D		71
#define JZ4780_CLK_CORE1	72

#endif /* __DT_BINDINGS_CLOCK_JZ4780_CGU_H__ */
