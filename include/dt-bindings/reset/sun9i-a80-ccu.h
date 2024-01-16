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

#ifndef _DT_BINDINGS_RESET_SUN9I_A80_CCU_H_
#define _DT_BINDINGS_RESET_SUN9I_A80_CCU_H_

#define RST_BUS_FD		0
#define RST_BUS_VE		1
#define RST_BUS_GPU_CTRL	2
#define RST_BUS_SS		3
#define RST_BUS_MMC		4
#define RST_BUS_NAND0		5
#define RST_BUS_NAND1		6
#define RST_BUS_SDRAM		7
#define RST_BUS_SATA		8
#define RST_BUS_TS		9
#define RST_BUS_SPI0		10
#define RST_BUS_SPI1		11
#define RST_BUS_SPI2		12
#define RST_BUS_SPI3		13

#define RST_BUS_OTG		14
#define RST_BUS_OTG_PHY		15
#define RST_BUS_MIPI_HSI	16
#define RST_BUS_GMAC		17
#define RST_BUS_MSGBOX		18
#define RST_BUS_SPINLOCK	19
#define RST_BUS_HSTIMER		20
#define RST_BUS_DMA		21

#define RST_BUS_LCD0		22
#define RST_BUS_LCD1		23
#define RST_BUS_EDP		24
#define RST_BUS_LVDS		25
#define RST_BUS_CSI		26
#define RST_BUS_HDMI0		27
#define RST_BUS_HDMI1		28
#define RST_BUS_DE		29
#define RST_BUS_MP		30
#define RST_BUS_GPU		31
#define RST_BUS_MIPI_DSI	32

#define RST_BUS_SPDIF		33
#define RST_BUS_AC97		34
#define RST_BUS_I2S0		35
#define RST_BUS_I2S1		36
#define RST_BUS_LRADC		37
#define RST_BUS_GPADC		38
#define RST_BUS_CIR_TX		39

#define RST_BUS_I2C0		40
#define RST_BUS_I2C1		41
#define RST_BUS_I2C2		42
#define RST_BUS_I2C3		43
#define RST_BUS_I2C4		44
#define RST_BUS_UART0		45
#define RST_BUS_UART1		46
#define RST_BUS_UART2		47
#define RST_BUS_UART3		48
#define RST_BUS_UART4		49
#define RST_BUS_UART5		50

#endif /* _DT_BINDINGS_RESET_SUN9I_A80_CCU_H_ */
