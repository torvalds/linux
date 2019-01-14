/*
 * Copyright (C) 2017 Icenowy Zheng <icenowy@aosc.io>
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

#ifndef _DT_BINDINGS_CLK_SUN8I_R40_H_
#define _DT_BINDINGS_CLK_SUN8I_R40_H_

#define CLK_PLL_VIDEO0		7

#define CLK_PLL_VIDEO1		16

#define CLK_CPU			24

#define CLK_BUS_MIPI_DSI	29
#define CLK_BUS_CE		30
#define CLK_BUS_DMA		31
#define CLK_BUS_MMC0		32
#define CLK_BUS_MMC1		33
#define CLK_BUS_MMC2		34
#define CLK_BUS_MMC3		35
#define CLK_BUS_NAND		36
#define CLK_BUS_DRAM		37
#define CLK_BUS_EMAC		38
#define CLK_BUS_TS		39
#define CLK_BUS_HSTIMER		40
#define CLK_BUS_SPI0		41
#define CLK_BUS_SPI1		42
#define CLK_BUS_SPI2		43
#define CLK_BUS_SPI3		44
#define CLK_BUS_SATA		45
#define CLK_BUS_OTG		46
#define CLK_BUS_EHCI0		47
#define CLK_BUS_EHCI1		48
#define CLK_BUS_EHCI2		49
#define CLK_BUS_OHCI0		50
#define CLK_BUS_OHCI1		51
#define CLK_BUS_OHCI2		52
#define CLK_BUS_VE		53
#define CLK_BUS_MP		54
#define CLK_BUS_DEINTERLACE	55
#define CLK_BUS_CSI0		56
#define CLK_BUS_CSI1		57
#define CLK_BUS_HDMI1		58
#define CLK_BUS_HDMI0		59
#define CLK_BUS_DE		60
#define CLK_BUS_TVE0		61
#define CLK_BUS_TVE1		62
#define CLK_BUS_TVE_TOP		63
#define CLK_BUS_GMAC		64
#define CLK_BUS_GPU		65
#define CLK_BUS_TVD0		66
#define CLK_BUS_TVD1		67
#define CLK_BUS_TVD2		68
#define CLK_BUS_TVD3		69
#define CLK_BUS_TVD_TOP		70
#define CLK_BUS_TCON_LCD0	71
#define CLK_BUS_TCON_LCD1	72
#define CLK_BUS_TCON_TV0	73
#define CLK_BUS_TCON_TV1	74
#define CLK_BUS_TCON_TOP	75
#define CLK_BUS_CODEC		76
#define CLK_BUS_SPDIF		77
#define CLK_BUS_AC97		78
#define CLK_BUS_PIO		79
#define CLK_BUS_IR0		80
#define CLK_BUS_IR1		81
#define CLK_BUS_THS		82
#define CLK_BUS_KEYPAD		83
#define CLK_BUS_I2S0		84
#define CLK_BUS_I2S1		85
#define CLK_BUS_I2S2		86
#define CLK_BUS_I2C0		87
#define CLK_BUS_I2C1		88
#define CLK_BUS_I2C2		89
#define CLK_BUS_I2C3		90
#define CLK_BUS_CAN		91
#define CLK_BUS_SCR		92
#define CLK_BUS_PS20		93
#define CLK_BUS_PS21		94
#define CLK_BUS_I2C4		95
#define CLK_BUS_UART0		96
#define CLK_BUS_UART1		97
#define CLK_BUS_UART2		98
#define CLK_BUS_UART3		99
#define CLK_BUS_UART4		100
#define CLK_BUS_UART5		101
#define CLK_BUS_UART6		102
#define CLK_BUS_UART7		103
#define CLK_BUS_DBG		104

#define CLK_THS			105
#define CLK_NAND		106
#define CLK_MMC0		107
#define CLK_MMC1		108
#define CLK_MMC2		109
#define CLK_MMC3		110
#define CLK_TS			111
#define CLK_CE			112
#define CLK_SPI0		113
#define CLK_SPI1		114
#define CLK_SPI2		115
#define CLK_SPI3		116
#define CLK_I2S0		117
#define CLK_I2S1		118
#define CLK_I2S2		119
#define CLK_AC97		120
#define CLK_SPDIF		121
#define CLK_KEYPAD		122
#define CLK_SATA		123
#define CLK_USB_PHY0		124
#define CLK_USB_PHY1		125
#define CLK_USB_PHY2		126
#define CLK_USB_OHCI0		127
#define CLK_USB_OHCI1		128
#define CLK_USB_OHCI2		129
#define CLK_IR0			130
#define CLK_IR1			131

#define CLK_DRAM_VE		133
#define CLK_DRAM_CSI0		134
#define CLK_DRAM_CSI1		135
#define CLK_DRAM_TS		136
#define CLK_DRAM_TVD		137
#define CLK_DRAM_MP		138
#define CLK_DRAM_DEINTERLACE	139
#define CLK_DE			140
#define CLK_MP			141
#define CLK_TCON_LCD0		142
#define CLK_TCON_LCD1		143
#define CLK_TCON_TV0		144
#define CLK_TCON_TV1		145
#define CLK_DEINTERLACE		146
#define CLK_CSI1_MCLK		147
#define CLK_CSI_SCLK		148
#define CLK_CSI0_MCLK		149
#define CLK_VE			150
#define CLK_CODEC		151
#define CLK_AVS			152
#define CLK_HDMI		153
#define CLK_HDMI_SLOW		154

#define CLK_DSI_DPHY		156
#define CLK_TVE0		157
#define CLK_TVE1		158
#define CLK_TVD0		159
#define CLK_TVD1		160
#define CLK_TVD2		161
#define CLK_TVD3		162
#define CLK_GPU			163
#define CLK_OUTA		164
#define CLK_OUTB		165

#endif /* _DT_BINDINGS_CLK_SUN8I_R40_H_ */
