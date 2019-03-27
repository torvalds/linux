/*
 * Copyright (c) 2016 Icenowy Zheng <icenowy@aosc.xyz>
 *
 * Based on sun8i-h3-ccu.h, which is:
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

#ifndef _DT_BINDINGS_CLK_SUN8I_V3S_H_
#define _DT_BINDINGS_CLK_SUN8I_V3S_H_

#define CLK_CPU			14

#define CLK_BUS_CE		20
#define CLK_BUS_DMA		21
#define CLK_BUS_MMC0		22
#define CLK_BUS_MMC1		23
#define CLK_BUS_MMC2		24
#define CLK_BUS_DRAM		25
#define CLK_BUS_EMAC		26
#define CLK_BUS_HSTIMER		27
#define CLK_BUS_SPI0		28
#define CLK_BUS_OTG		29
#define CLK_BUS_EHCI0		30
#define CLK_BUS_OHCI0		31
#define CLK_BUS_VE		32
#define CLK_BUS_TCON0		33
#define CLK_BUS_CSI		34
#define CLK_BUS_DE		35
#define CLK_BUS_CODEC		36
#define CLK_BUS_PIO		37
#define CLK_BUS_I2C0		38
#define CLK_BUS_I2C1		39
#define CLK_BUS_UART0		40
#define CLK_BUS_UART1		41
#define CLK_BUS_UART2		42
#define CLK_BUS_EPHY		43
#define CLK_BUS_DBG		44

#define CLK_MMC0		45
#define CLK_MMC0_SAMPLE		46
#define CLK_MMC0_OUTPUT		47
#define CLK_MMC1		48
#define CLK_MMC1_SAMPLE		49
#define CLK_MMC1_OUTPUT		50
#define CLK_MMC2		51
#define CLK_MMC2_SAMPLE		52
#define CLK_MMC2_OUTPUT		53
#define CLK_CE			54
#define CLK_SPI0		55
#define CLK_USB_PHY0		56
#define CLK_USB_OHCI0		57

#define CLK_DRAM_VE		59
#define CLK_DRAM_CSI		60
#define CLK_DRAM_EHCI		61
#define CLK_DRAM_OHCI		62
#define CLK_DE			63
#define CLK_TCON0		64
#define CLK_CSI_MISC		65
#define CLK_CSI0_MCLK		66
#define CLK_CSI1_SCLK		67
#define CLK_CSI1_MCLK		68
#define CLK_VE			69
#define CLK_AC_DIG		70
#define CLK_AVS			71

#define CLK_MIPI_CSI		73

#endif /* _DT_BINDINGS_CLK_SUN8I_V3S_H_ */
