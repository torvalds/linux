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

#ifndef _DT_BINDINGS_CLK_SUN8I_H3_H_
#define _DT_BINDINGS_CLK_SUN8I_H3_H_

#define CLK_PLL_VIDEO		6

#define CLK_PLL_PERIPH0		9

#define CLK_CPUX		14

#define CLK_BUS_CE		20
#define CLK_BUS_DMA		21
#define CLK_BUS_MMC0		22
#define CLK_BUS_MMC1		23
#define CLK_BUS_MMC2		24
#define CLK_BUS_NAND		25
#define CLK_BUS_DRAM		26
#define CLK_BUS_EMAC		27
#define CLK_BUS_TS		28
#define CLK_BUS_HSTIMER		29
#define CLK_BUS_SPI0		30
#define CLK_BUS_SPI1		31
#define CLK_BUS_OTG		32
#define CLK_BUS_EHCI0		33
#define CLK_BUS_EHCI1		34
#define CLK_BUS_EHCI2		35
#define CLK_BUS_EHCI3		36
#define CLK_BUS_OHCI0		37
#define CLK_BUS_OHCI1		38
#define CLK_BUS_OHCI2		39
#define CLK_BUS_OHCI3		40
#define CLK_BUS_VE		41
#define CLK_BUS_TCON0		42
#define CLK_BUS_TCON1		43
#define CLK_BUS_DEINTERLACE	44
#define CLK_BUS_CSI		45
#define CLK_BUS_TVE		46
#define CLK_BUS_HDMI		47
#define CLK_BUS_DE		48
#define CLK_BUS_GPU		49
#define CLK_BUS_MSGBOX		50
#define CLK_BUS_SPINLOCK	51
#define CLK_BUS_CODEC		52
#define CLK_BUS_SPDIF		53
#define CLK_BUS_PIO		54
#define CLK_BUS_THS		55
#define CLK_BUS_I2S0		56
#define CLK_BUS_I2S1		57
#define CLK_BUS_I2S2		58
#define CLK_BUS_I2C0		59
#define CLK_BUS_I2C1		60
#define CLK_BUS_I2C2		61
#define CLK_BUS_UART0		62
#define CLK_BUS_UART1		63
#define CLK_BUS_UART2		64
#define CLK_BUS_UART3		65
#define CLK_BUS_SCR0		66
#define CLK_BUS_EPHY		67
#define CLK_BUS_DBG		68

#define CLK_THS			69
#define CLK_NAND		70
#define CLK_MMC0		71
#define CLK_MMC0_SAMPLE		72
#define CLK_MMC0_OUTPUT		73
#define CLK_MMC1		74
#define CLK_MMC1_SAMPLE		75
#define CLK_MMC1_OUTPUT		76
#define CLK_MMC2		77
#define CLK_MMC2_SAMPLE		78
#define CLK_MMC2_OUTPUT		79
#define CLK_TS			80
#define CLK_CE			81
#define CLK_SPI0		82
#define CLK_SPI1		83
#define CLK_I2S0		84
#define CLK_I2S1		85
#define CLK_I2S2		86
#define CLK_SPDIF		87
#define CLK_USB_PHY0		88
#define CLK_USB_PHY1		89
#define CLK_USB_PHY2		90
#define CLK_USB_PHY3		91
#define CLK_USB_OHCI0		92
#define CLK_USB_OHCI1		93
#define CLK_USB_OHCI2		94
#define CLK_USB_OHCI3		95

#define CLK_DRAM_VE		97
#define CLK_DRAM_CSI		98
#define CLK_DRAM_DEINTERLACE	99
#define CLK_DRAM_TS		100
#define CLK_DE			101
#define CLK_TCON0		102
#define CLK_TVE			103
#define CLK_DEINTERLACE		104
#define CLK_CSI_MISC		105
#define CLK_CSI_SCLK		106
#define CLK_CSI_MCLK		107
#define CLK_VE			108
#define CLK_AC_DIG		109
#define CLK_AVS			110
#define CLK_HDMI		111
#define CLK_HDMI_DDC		112
#define CLK_MBUS		113
#define CLK_GPU			114

/* New clocks imported in H5 */
#define CLK_BUS_SCR1		115

#endif /* _DT_BINDINGS_CLK_SUN8I_H3_H_ */
