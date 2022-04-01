/*
 * Copyright (C) 2016 Maxime Ripard <maxime.ripard@free-electrons.com>
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

#ifndef _DT_BINDINGS_CLK_SUN50I_A64_H_
#define _DT_BINDINGS_CLK_SUN50I_A64_H_

#define CLK_PLL_VIDEO0		7
#define CLK_PLL_PERIPH0		11

#define CLK_CPUX		21
#define CLK_BUS_MIPI_DSI	28
#define CLK_BUS_CE		29
#define CLK_BUS_DMA		30
#define CLK_BUS_MMC0		31
#define CLK_BUS_MMC1		32
#define CLK_BUS_MMC2		33
#define CLK_BUS_NAND		34
#define CLK_BUS_DRAM		35
#define CLK_BUS_EMAC		36
#define CLK_BUS_TS		37
#define CLK_BUS_HSTIMER		38
#define CLK_BUS_SPI0		39
#define CLK_BUS_SPI1		40
#define CLK_BUS_OTG		41
#define CLK_BUS_EHCI0		42
#define CLK_BUS_EHCI1		43
#define CLK_BUS_OHCI0		44
#define CLK_BUS_OHCI1		45
#define CLK_BUS_VE		46
#define CLK_BUS_TCON0		47
#define CLK_BUS_TCON1		48
#define CLK_BUS_DEINTERLACE	49
#define CLK_BUS_CSI		50
#define CLK_BUS_HDMI		51
#define CLK_BUS_DE		52
#define CLK_BUS_GPU		53
#define CLK_BUS_MSGBOX		54
#define CLK_BUS_SPINLOCK	55
#define CLK_BUS_CODEC		56
#define CLK_BUS_SPDIF		57
#define CLK_BUS_PIO		58
#define CLK_BUS_THS		59
#define CLK_BUS_I2S0		60
#define CLK_BUS_I2S1		61
#define CLK_BUS_I2S2		62
#define CLK_BUS_I2C0		63
#define CLK_BUS_I2C1		64
#define CLK_BUS_I2C2		65
#define CLK_BUS_SCR		66
#define CLK_BUS_UART0		67
#define CLK_BUS_UART1		68
#define CLK_BUS_UART2		69
#define CLK_BUS_UART3		70
#define CLK_BUS_UART4		71
#define CLK_BUS_DBG		72
#define CLK_THS			73
#define CLK_NAND		74
#define CLK_MMC0		75
#define CLK_MMC1		76
#define CLK_MMC2		77
#define CLK_TS			78
#define CLK_CE			79
#define CLK_SPI0		80
#define CLK_SPI1		81
#define CLK_I2S0		82
#define CLK_I2S1		83
#define CLK_I2S2		84
#define CLK_SPDIF		85
#define CLK_USB_PHY0		86
#define CLK_USB_PHY1		87
#define CLK_USB_HSIC		88
#define CLK_USB_HSIC_12M	89

#define CLK_USB_OHCI0		91

#define CLK_USB_OHCI1		93
#define CLK_DRAM		94
#define CLK_DRAM_VE		95
#define CLK_DRAM_CSI		96
#define CLK_DRAM_DEINTERLACE	97
#define CLK_DRAM_TS		98
#define CLK_DE			99
#define CLK_TCON0		100
#define CLK_TCON1		101
#define CLK_DEINTERLACE		102
#define CLK_CSI_MISC		103
#define CLK_CSI_SCLK		104
#define CLK_CSI_MCLK		105
#define CLK_VE			106
#define CLK_AC_DIG		107
#define CLK_AC_DIG_4X		108
#define CLK_AVS			109
#define CLK_HDMI		110
#define CLK_HDMI_DDC		111
#define CLK_MBUS		112
#define CLK_DSI_DPHY		113
#define CLK_GPU			114

#endif /* _DT_BINDINGS_CLK_SUN50I_H_ */
