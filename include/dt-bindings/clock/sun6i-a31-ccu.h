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

#ifndef _DT_BINDINGS_CLK_SUN6I_A31_H_
#define _DT_BINDINGS_CLK_SUN6I_A31_H_

#define CLK_PLL_PERIPH		10

#define CLK_CPU			18

#define CLK_AHB1_MIPIDSI	23
#define CLK_AHB1_SS		24
#define CLK_AHB1_DMA		25
#define CLK_AHB1_MMC0		26
#define CLK_AHB1_MMC1		27
#define CLK_AHB1_MMC2		28
#define CLK_AHB1_MMC3		29
#define CLK_AHB1_NAND1		30
#define CLK_AHB1_NAND0		31
#define CLK_AHB1_SDRAM		32
#define CLK_AHB1_EMAC		33
#define CLK_AHB1_TS		34
#define CLK_AHB1_HSTIMER	35
#define CLK_AHB1_SPI0		36
#define CLK_AHB1_SPI1		37
#define CLK_AHB1_SPI2		38
#define CLK_AHB1_SPI3		39
#define CLK_AHB1_OTG		40
#define CLK_AHB1_EHCI0		41
#define CLK_AHB1_EHCI1		42
#define CLK_AHB1_OHCI0		43
#define CLK_AHB1_OHCI1		44
#define CLK_AHB1_OHCI2		45
#define CLK_AHB1_VE		46
#define CLK_AHB1_LCD0		47
#define CLK_AHB1_LCD1		48
#define CLK_AHB1_CSI		49
#define CLK_AHB1_HDMI		50
#define CLK_AHB1_BE0		51
#define CLK_AHB1_BE1		52
#define CLK_AHB1_FE0		53
#define CLK_AHB1_FE1		54
#define CLK_AHB1_MP		55
#define CLK_AHB1_GPU		56
#define CLK_AHB1_DEU0		57
#define CLK_AHB1_DEU1		58
#define CLK_AHB1_DRC0		59
#define CLK_AHB1_DRC1		60

#define CLK_APB1_CODEC		61
#define CLK_APB1_SPDIF		62
#define CLK_APB1_DIGITAL_MIC	63
#define CLK_APB1_PIO		64
#define CLK_APB1_DAUDIO0	65
#define CLK_APB1_DAUDIO1	66

#define CLK_APB2_I2C0		67
#define CLK_APB2_I2C1		68
#define CLK_APB2_I2C2		69
#define CLK_APB2_I2C3		70
#define CLK_APB2_UART0		71
#define CLK_APB2_UART1		72
#define CLK_APB2_UART2		73
#define CLK_APB2_UART3		74
#define CLK_APB2_UART4		75
#define CLK_APB2_UART5		76

#define CLK_NAND0		77
#define CLK_NAND1		78
#define CLK_MMC0		79
#define CLK_MMC0_SAMPLE		80
#define CLK_MMC0_OUTPUT		81
#define CLK_MMC1		82
#define CLK_MMC1_SAMPLE		83
#define CLK_MMC1_OUTPUT		84
#define CLK_MMC2		85
#define CLK_MMC2_SAMPLE		86
#define CLK_MMC2_OUTPUT		87
#define CLK_MMC3		88
#define CLK_MMC3_SAMPLE		89
#define CLK_MMC3_OUTPUT		90
#define CLK_TS			91
#define CLK_SS			92
#define CLK_SPI0		93
#define CLK_SPI1		94
#define CLK_SPI2		95
#define CLK_SPI3		96
#define CLK_DAUDIO0		97
#define CLK_DAUDIO1		98
#define CLK_SPDIF		99
#define CLK_USB_PHY0		100
#define CLK_USB_PHY1		101
#define CLK_USB_PHY2		102
#define CLK_USB_OHCI0		103
#define CLK_USB_OHCI1		104
#define CLK_USB_OHCI2		105

#define CLK_DRAM_VE		110
#define CLK_DRAM_CSI_ISP	111
#define CLK_DRAM_TS		112
#define CLK_DRAM_DRC0		113
#define CLK_DRAM_DRC1		114
#define CLK_DRAM_DEU0		115
#define CLK_DRAM_DEU1		116
#define CLK_DRAM_FE0		117
#define CLK_DRAM_FE1		118
#define CLK_DRAM_BE0		119
#define CLK_DRAM_BE1		120
#define CLK_DRAM_MP		121

#define CLK_BE0			122
#define CLK_BE1			123
#define CLK_FE0			124
#define CLK_FE1			125
#define CLK_MP			126
#define CLK_LCD0_CH0		127
#define CLK_LCD1_CH0		128
#define CLK_LCD0_CH1		129
#define CLK_LCD1_CH1		130
#define CLK_CSI0_SCLK		131
#define CLK_CSI0_MCLK		132
#define CLK_CSI1_MCLK		133
#define CLK_VE			134
#define CLK_CODEC		135
#define CLK_AVS			136
#define CLK_DIGITAL_MIC		137
#define CLK_HDMI		138
#define CLK_HDMI_DDC		139
#define CLK_PS			140

#define CLK_MIPI_DSI		143
#define CLK_MIPI_DSI_DPHY	144
#define CLK_MIPI_CSI_DPHY	145
#define CLK_IEP_DRC0		146
#define CLK_IEP_DRC1		147
#define CLK_IEP_DEU0		148
#define CLK_IEP_DEU1		149
#define CLK_GPU_CORE		150
#define CLK_GPU_MEMORY		151
#define CLK_GPU_HYD		152
#define CLK_ATS			153
#define CLK_TRACE		154

#define CLK_OUT_A		155
#define CLK_OUT_B		156
#define CLK_OUT_C		157

#endif /* _DT_BINDINGS_CLK_SUN6I_A31_H_ */
