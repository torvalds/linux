/*
 * Copyright (C) 2016 Chen-Yu Tsai <wens@csie.org>
 *
 * This file is dual-licensed: you can use it either under the terms
 * of the GPL or the X11 license, at your option. Note that this dual
 * licensing only applies to this file, and not this project as a
 * whole.
 *
 *  a) This file is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of the
 *     License, or (at your option) any later version.
 *
 *     This file is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 * Or, alternatively,
 *
 *  b) Permission is hereby granted, free of charge, to any person
 *     obtaining a copy of this software and associated documentation
 *     files (the "Software"), to deal in the Software without
 *     restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or
 *     sell copies of the Software, and to permit persons to whom the
 *     Software is furnished to do so, subject to the following
 *     conditions:
 *
 *     The above copyright notice and this permission notice shall be
 *     included in all copies or substantial portions of the Software.
 *
 *     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *     EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *     OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *     NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *     HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *     WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *     FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *     OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DT_BINDINGS_CLOCK_SUN9I_A80_CCU_H_
#define _DT_BINDINGS_CLOCK_SUN9I_A80_CCU_H_

#define CLK_PLL_AUDIO		2
#define CLK_PLL_PERIPH0		3

#define CLK_C0CPUX		12
#define CLK_C1CPUX		13

#define CLK_OUT_A		27
#define CLK_OUT_B		28

#define CLK_NAND0_0		29
#define CLK_NAND0_1		30
#define CLK_NAND1_0		31
#define CLK_NAND1_1		32
#define CLK_MMC0		33
#define CLK_MMC0_SAMPLE		34
#define CLK_MMC0_OUTPUT		35
#define CLK_MMC1		36
#define CLK_MMC1_SAMPLE		37
#define CLK_MMC1_OUTPUT		38
#define CLK_MMC2		39
#define CLK_MMC2_SAMPLE		40
#define CLK_MMC2_OUTPUT		41
#define CLK_MMC3		42
#define CLK_MMC3_SAMPLE		43
#define CLK_MMC3_OUTPUT		44
#define CLK_TS			45
#define CLK_SS			46
#define CLK_SPI0		47
#define CLK_SPI1		48
#define CLK_SPI2		49
#define CLK_SPI3		50
#define CLK_I2S0		51
#define CLK_I2S1		52
#define CLK_SPDIF		53
#define CLK_SDRAM		54
#define CLK_DE			55
#define CLK_EDP			56
#define CLK_MP			57
#define CLK_LCD0		58
#define CLK_LCD1		59
#define CLK_MIPI_DSI0		60
#define CLK_MIPI_DSI1		61
#define CLK_HDMI		62
#define CLK_HDMI_SLOW		63
#define CLK_MIPI_CSI		64
#define CLK_CSI_ISP		65
#define CLK_CSI_MISC		66
#define CLK_CSI0_MCLK		67
#define CLK_CSI1_MCLK		68
#define CLK_FD			69
#define CLK_VE			70
#define CLK_AVS			71
#define CLK_GPU_CORE		72
#define CLK_GPU_MEMORY		73
#define CLK_GPU_AXI		74
#define CLK_SATA		75
#define CLK_AC97		76
#define CLK_MIPI_HSI		77
#define CLK_GPADC		78
#define CLK_CIR_TX		79

#define CLK_BUS_FD		80
#define CLK_BUS_VE		81
#define CLK_BUS_GPU_CTRL	82
#define CLK_BUS_SS		83
#define CLK_BUS_MMC		84
#define CLK_BUS_NAND0		85
#define CLK_BUS_NAND1		86
#define CLK_BUS_SDRAM		87
#define CLK_BUS_MIPI_HSI	88
#define CLK_BUS_SATA		89
#define CLK_BUS_TS		90
#define CLK_BUS_SPI0		91
#define CLK_BUS_SPI1		92
#define CLK_BUS_SPI2		93
#define CLK_BUS_SPI3		94

#define CLK_BUS_OTG		95
#define CLK_BUS_USB		96
#define CLK_BUS_GMAC		97
#define CLK_BUS_MSGBOX		98
#define CLK_BUS_SPINLOCK	99
#define CLK_BUS_HSTIMER		100
#define CLK_BUS_DMA		101

#define CLK_BUS_LCD0		102
#define CLK_BUS_LCD1		103
#define CLK_BUS_EDP		104
#define CLK_BUS_CSI		105
#define CLK_BUS_HDMI		106
#define CLK_BUS_DE		107
#define CLK_BUS_MP		108
#define CLK_BUS_MIPI_DSI	109

#define CLK_BUS_SPDIF		110
#define CLK_BUS_PIO		111
#define CLK_BUS_AC97		112
#define CLK_BUS_I2S0		113
#define CLK_BUS_I2S1		114
#define CLK_BUS_LRADC		115
#define CLK_BUS_GPADC		116
#define CLK_BUS_TWD		117
#define CLK_BUS_CIR_TX		118

#define CLK_BUS_I2C0		119
#define CLK_BUS_I2C1		120
#define CLK_BUS_I2C2		121
#define CLK_BUS_I2C3		122
#define CLK_BUS_I2C4		123
#define CLK_BUS_UART0		124
#define CLK_BUS_UART1		125
#define CLK_BUS_UART2		126
#define CLK_BUS_UART3		127
#define CLK_BUS_UART4		128
#define CLK_BUS_UART5		129

#endif /* _DT_BINDINGS_CLOCK_SUN9I_A80_CCU_H_ */
