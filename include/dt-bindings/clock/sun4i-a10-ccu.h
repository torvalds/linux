/*
 * Copyright (C) 2017 Priit Laes <plaes@plaes.org>
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
 *     FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *     OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DT_BINDINGS_CLK_SUN4I_A10_H_
#define _DT_BINDINGS_CLK_SUN4I_A10_H_

#define CLK_HOSC		1
#define CLK_CPU			20

/* AHB Gates */
#define CLK_AHB_OTG		26
#define CLK_AHB_EHCI0		27
#define CLK_AHB_OHCI0		28
#define CLK_AHB_EHCI1		29
#define CLK_AHB_OHCI1		30
#define CLK_AHB_SS		31
#define CLK_AHB_DMA		32
#define CLK_AHB_BIST		33
#define CLK_AHB_MMC0		34
#define CLK_AHB_MMC1		35
#define CLK_AHB_MMC2		36
#define CLK_AHB_MMC3		37
#define CLK_AHB_MS		38
#define CLK_AHB_NAND		39
#define CLK_AHB_SDRAM		40
#define CLK_AHB_ACE		41
#define CLK_AHB_EMAC		42
#define CLK_AHB_TS		43
#define CLK_AHB_SPI0		44
#define CLK_AHB_SPI1		45
#define CLK_AHB_SPI2		46
#define CLK_AHB_SPI3		47
#define CLK_AHB_PATA		48
#define CLK_AHB_SATA		49
#define CLK_AHB_GPS		50
#define CLK_AHB_HSTIMER		51
#define CLK_AHB_VE		52
#define CLK_AHB_TVD		53
#define CLK_AHB_TVE0		54
#define CLK_AHB_TVE1		55
#define CLK_AHB_LCD0		56
#define CLK_AHB_LCD1		57
#define CLK_AHB_CSI0		58
#define CLK_AHB_CSI1		59
#define CLK_AHB_HDMI0		60
#define CLK_AHB_HDMI1		61
#define CLK_AHB_DE_BE0		62
#define CLK_AHB_DE_BE1		63
#define CLK_AHB_DE_FE0		64
#define CLK_AHB_DE_FE1		65
#define CLK_AHB_GMAC		66
#define CLK_AHB_MP		67
#define CLK_AHB_GPU		68

/* APB0 Gates */
#define CLK_APB0_CODEC		69
#define CLK_APB0_SPDIF		70
#define CLK_APB0_I2S0		71
#define CLK_APB0_AC97		72
#define CLK_APB0_I2S1		73
#define CLK_APB0_PIO		74
#define CLK_APB0_IR0		75
#define CLK_APB0_IR1		76
#define CLK_APB0_I2S2		77
#define CLK_APB0_KEYPAD		78

/* APB1 Gates */
#define CLK_APB1_I2C0		79
#define CLK_APB1_I2C1		80
#define CLK_APB1_I2C2		81
#define CLK_APB1_I2C3		82
#define CLK_APB1_CAN		83
#define CLK_APB1_SCR		84
#define CLK_APB1_PS20		85
#define CLK_APB1_PS21		86
#define CLK_APB1_I2C4		87
#define CLK_APB1_UART0		88
#define CLK_APB1_UART1		89
#define CLK_APB1_UART2		90
#define CLK_APB1_UART3		91
#define CLK_APB1_UART4		92
#define CLK_APB1_UART5		93
#define CLK_APB1_UART6		94
#define CLK_APB1_UART7		95

/* IP clocks */
#define CLK_NAND		96
#define CLK_MS			97
#define CLK_MMC0		98
#define CLK_MMC0_OUTPUT		99
#define CLK_MMC0_SAMPLE		100
#define CLK_MMC1		101
#define CLK_MMC1_OUTPUT		102
#define CLK_MMC1_SAMPLE		103
#define CLK_MMC2		104
#define CLK_MMC2_OUTPUT		105
#define CLK_MMC2_SAMPLE		106
#define CLK_MMC3		107
#define CLK_MMC3_OUTPUT		108
#define CLK_MMC3_SAMPLE		109
#define CLK_TS			110
#define CLK_SS			111
#define CLK_SPI0		112
#define CLK_SPI1		113
#define CLK_SPI2		114
#define CLK_PATA		115
#define CLK_IR0			116
#define CLK_IR1			117
#define CLK_I2S0		118
#define CLK_AC97		119
#define CLK_SPDIF		120
#define CLK_KEYPAD		121
#define CLK_SATA		122
#define CLK_USB_OHCI0		123
#define CLK_USB_OHCI1		124
#define CLK_USB_PHY		125
#define CLK_GPS			126
#define CLK_SPI3		127
#define CLK_I2S1		128
#define CLK_I2S2		129

/* DRAM Gates */
#define CLK_DRAM_VE		130
#define CLK_DRAM_CSI0		131
#define CLK_DRAM_CSI1		132
#define CLK_DRAM_TS		133
#define CLK_DRAM_TVD		134
#define CLK_DRAM_TVE0		135
#define CLK_DRAM_TVE1		136
#define CLK_DRAM_OUT		137
#define CLK_DRAM_DE_FE1		138
#define CLK_DRAM_DE_FE0		139
#define CLK_DRAM_DE_BE0		140
#define CLK_DRAM_DE_BE1		141
#define CLK_DRAM_MP		142
#define CLK_DRAM_ACE		143

/* Display Engine Clocks */
#define CLK_DE_BE0		144
#define CLK_DE_BE1		145
#define CLK_DE_FE0		146
#define CLK_DE_FE1		147
#define CLK_DE_MP		148
#define CLK_TCON0_CH0		149
#define CLK_TCON1_CH0		150
#define CLK_CSI_SCLK		151
#define CLK_TVD_SCLK2		152
#define CLK_TVD			153
#define CLK_TCON0_CH1_SCLK2	154
#define CLK_TCON0_CH1		155
#define CLK_TCON1_CH1_SCLK2	156
#define CLK_TCON1_CH1		157
#define CLK_CSI0		158
#define CLK_CSI1		159
#define CLK_CODEC		160
#define CLK_VE			161
#define CLK_AVS			162
#define CLK_ACE			163
#define CLK_HDMI		164
#define CLK_GPU			165

#endif /* _DT_BINDINGS_CLK_SUN4I_A10_H_ */
