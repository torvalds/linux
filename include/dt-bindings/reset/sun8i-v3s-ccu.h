/*
 * Copyright (C) 2016 Icenowy Zheng <icenowy@aosc.xyz>
 *
 * Based on sun8i-v3s-ccu.h, which is
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

#ifndef _DT_BINDINGS_RST_SUN8I_V3S_H_
#define _DT_BINDINGS_RST_SUN8I_V3S_H_

#define RST_USB_PHY0		0

#define RST_MBUS		1

#define RST_BUS_CE		5
#define RST_BUS_DMA		6
#define RST_BUS_MMC0		7
#define RST_BUS_MMC1		8
#define RST_BUS_MMC2		9
#define RST_BUS_DRAM		11
#define RST_BUS_EMAC		12
#define RST_BUS_HSTIMER		14
#define RST_BUS_SPI0		15
#define RST_BUS_OTG		17
#define RST_BUS_EHCI0		18
#define RST_BUS_OHCI0		22
#define RST_BUS_VE		26
#define RST_BUS_TCON0		27
#define RST_BUS_CSI		30
#define RST_BUS_DE		34
#define RST_BUS_DBG		38
#define RST_BUS_EPHY		39
#define RST_BUS_CODEC		40
#define RST_BUS_I2C0		46
#define RST_BUS_I2C1		47
#define RST_BUS_UART0		49
#define RST_BUS_UART1		50
#define RST_BUS_UART2		51

#endif /* _DT_BINDINGS_RST_SUN8I_H3_H_ */
