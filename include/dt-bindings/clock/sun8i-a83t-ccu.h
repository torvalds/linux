/*
 * Copyright (C) 2017 Chen-Yu Tsai <wens@csie.org>
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

#ifndef _DT_BINDINGS_CLOCK_SUN8I_A83T_CCU_H_
#define _DT_BINDINGS_CLOCK_SUN8I_A83T_CCU_H_

#define CLK_PLL_PERIPH		6

#define CLK_PLL_DE		9

#define CLK_C0CPUX		11
#define CLK_C1CPUX		12

#define CLK_BUS_MIPI_DSI	19
#define CLK_BUS_SS		20
#define CLK_BUS_DMA		21
#define CLK_BUS_MMC0		22
#define CLK_BUS_MMC1		23
#define CLK_BUS_MMC2		24
#define CLK_BUS_NAND		25
#define CLK_BUS_DRAM		26
#define CLK_BUS_EMAC		27
#define CLK_BUS_HSTIMER		28
#define CLK_BUS_SPI0		29
#define CLK_BUS_SPI1		30
#define CLK_BUS_OTG		31
#define CLK_BUS_EHCI0		32
#define CLK_BUS_EHCI1		33
#define CLK_BUS_OHCI0		34

#define CLK_BUS_VE		35
#define CLK_BUS_TCON0		36
#define CLK_BUS_TCON1		37
#define CLK_BUS_CSI		38
#define CLK_BUS_HDMI		39
#define CLK_BUS_DE		40
#define CLK_BUS_GPU		41
#define CLK_BUS_MSGBOX		42
#define CLK_BUS_SPINLOCK	43

#define CLK_BUS_SPDIF		44
#define CLK_BUS_PIO		45
#define CLK_BUS_I2S0		46
#define CLK_BUS_I2S1		47
#define CLK_BUS_I2S2		48
#define CLK_BUS_TDM		49

#define CLK_BUS_I2C0		50
#define CLK_BUS_I2C1		51
#define CLK_BUS_I2C2		52
#define CLK_BUS_UART0		53
#define CLK_BUS_UART1		54
#define CLK_BUS_UART2		55
#define CLK_BUS_UART3		56
#define CLK_BUS_UART4		57

#define CLK_NAND		59
#define CLK_MMC0		60
#define CLK_MMC0_SAMPLE		61
#define CLK_MMC0_OUTPUT		62
#define CLK_MMC1		63
#define CLK_MMC1_SAMPLE		64
#define CLK_MMC1_OUTPUT		65
#define CLK_MMC2		66
#define CLK_MMC2_SAMPLE		67
#define CLK_MMC2_OUTPUT		68
#define CLK_SS			69
#define CLK_SPI0		70
#define CLK_SPI1		71
#define CLK_I2S0		72
#define CLK_I2S1		73
#define CLK_I2S2		74
#define CLK_TDM			75
#define CLK_SPDIF		76
#define CLK_USB_PHY0		77
#define CLK_USB_PHY1		78
#define CLK_USB_HSIC		79
#define CLK_USB_HSIC_12M	80
#define CLK_USB_OHCI0		81

#define CLK_DRAM_VE		83
#define CLK_DRAM_CSI		84

#define CLK_TCON0		85
#define CLK_TCON1		86
#define CLK_CSI_MISC		87
#define CLK_MIPI_CSI		88
#define CLK_CSI_MCLK		89
#define CLK_CSI_SCLK		90
#define CLK_VE			91
#define CLK_AVS			92
#define CLK_HDMI		93
#define CLK_HDMI_SLOW		94

#define CLK_MIPI_DSI0		96
#define CLK_MIPI_DSI1		97
#define CLK_GPU_CORE		98
#define CLK_GPU_MEMORY		99
#define CLK_GPU_HYD		100

#endif /* _DT_BINDINGS_CLOCK_SUN8I_A83T_CCU_H_ */
