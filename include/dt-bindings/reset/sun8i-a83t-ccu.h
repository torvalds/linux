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

#ifndef _DT_BINDINGS_RESET_SUN8I_A83T_CCU_H_
#define _DT_BINDINGS_RESET_SUN8I_A83T_CCU_H_

#define RST_USB_PHY0		0
#define RST_USB_PHY1		1
#define RST_USB_HSIC		2

#define RST_DRAM		3
#define RST_MBUS		4

#define RST_BUS_MIPI_DSI	5
#define RST_BUS_SS		6
#define RST_BUS_DMA		7
#define RST_BUS_MMC0		8
#define RST_BUS_MMC1		9
#define RST_BUS_MMC2		10
#define RST_BUS_NAND		11
#define RST_BUS_DRAM		12
#define RST_BUS_EMAC		13
#define RST_BUS_HSTIMER		14
#define RST_BUS_SPI0		15
#define RST_BUS_SPI1		16
#define RST_BUS_OTG		17
#define RST_BUS_EHCI0		18
#define RST_BUS_EHCI1		19
#define RST_BUS_OHCI0		20

#define RST_BUS_VE		21
#define RST_BUS_TCON0		22
#define RST_BUS_TCON1		23
#define RST_BUS_CSI		24
#define RST_BUS_HDMI0		25
#define RST_BUS_HDMI1		26
#define RST_BUS_DE		27
#define RST_BUS_GPU		28
#define RST_BUS_MSGBOX		29
#define RST_BUS_SPINLOCK	30

#define RST_BUS_LVDS		31

#define RST_BUS_SPDIF		32
#define RST_BUS_I2S0		33
#define RST_BUS_I2S1		34
#define RST_BUS_I2S2		35
#define RST_BUS_TDM		36

#define RST_BUS_I2C0		37
#define RST_BUS_I2C1		38
#define RST_BUS_I2C2		39
#define RST_BUS_UART0		40
#define RST_BUS_UART1		41
#define RST_BUS_UART2		42
#define RST_BUS_UART3		43
#define RST_BUS_UART4		44

#endif /* _DT_BINDINGS_RESET_SUN8I_A83T_CCU_H_ */
