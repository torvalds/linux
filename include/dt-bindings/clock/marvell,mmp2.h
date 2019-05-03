/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DTS_MARVELL_MMP2_CLOCK_H
#define __DTS_MARVELL_MMP2_CLOCK_H

/* fixed clocks and plls */
#define MMP2_CLK_CLK32			1
#define MMP2_CLK_VCTCXO			2
#define MMP2_CLK_PLL1			3
#define MMP2_CLK_PLL1_2			8
#define MMP2_CLK_PLL1_4			9
#define MMP2_CLK_PLL1_8			10
#define MMP2_CLK_PLL1_16		11
#define MMP2_CLK_PLL1_3			12
#define MMP2_CLK_PLL1_6			13
#define MMP2_CLK_PLL1_12		14
#define MMP2_CLK_PLL1_20		15
#define MMP2_CLK_PLL2			16
#define MMP2_CLK_PLL2_2			17
#define MMP2_CLK_PLL2_4			18
#define MMP2_CLK_PLL2_8			19
#define MMP2_CLK_PLL2_16		20
#define MMP2_CLK_PLL2_3			21
#define MMP2_CLK_PLL2_6			22
#define MMP2_CLK_PLL2_12		23
#define MMP2_CLK_VCTCXO_2		24
#define MMP2_CLK_VCTCXO_4		25
#define MMP2_CLK_UART_PLL		26
#define MMP2_CLK_USB_PLL		27

/* apb periphrals */
#define MMP2_CLK_TWSI0			60
#define MMP2_CLK_TWSI1			61
#define MMP2_CLK_TWSI2			62
#define MMP2_CLK_TWSI3			63
#define MMP2_CLK_TWSI4			64
#define MMP2_CLK_TWSI5			65
#define MMP2_CLK_GPIO			66
#define MMP2_CLK_KPC			67
#define MMP2_CLK_RTC			68
#define MMP2_CLK_PWM0			69
#define MMP2_CLK_PWM1			70
#define MMP2_CLK_PWM2			71
#define MMP2_CLK_PWM3			72
#define MMP2_CLK_UART0			73
#define MMP2_CLK_UART1			74
#define MMP2_CLK_UART2			75
#define MMP2_CLK_UART3			76
#define MMP2_CLK_SSP0			77
#define MMP2_CLK_SSP1			78
#define MMP2_CLK_SSP2			79
#define MMP2_CLK_SSP3			80
#define MMP2_CLK_TIMER			81

/* axi periphrals */
#define MMP2_CLK_SDH0			101
#define MMP2_CLK_SDH1			102
#define MMP2_CLK_SDH2			103
#define MMP2_CLK_SDH3			104
#define MMP2_CLK_USB			105
#define MMP2_CLK_DISP0			106
#define MMP2_CLK_DISP0_MUX		107
#define MMP2_CLK_DISP0_SPHY		108
#define MMP2_CLK_DISP1			109
#define MMP2_CLK_DISP1_MUX		110
#define MMP2_CLK_CCIC_ARBITER		111
#define MMP2_CLK_CCIC0			112
#define MMP2_CLK_CCIC0_MIX		113
#define MMP2_CLK_CCIC0_PHY		114
#define MMP2_CLK_CCIC0_SPHY		115
#define MMP2_CLK_CCIC1			116
#define MMP2_CLK_CCIC1_MIX		117
#define MMP2_CLK_CCIC1_PHY		118
#define MMP2_CLK_CCIC1_SPHY		119
#define MMP2_CLK_DISP0_LCDC		120

#define MMP2_NR_CLKS			200
#endif
