/*
 * Copyright 2016 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DT_BINDINGS_CLK_SUN5I_H_
#define _DT_BINDINGS_CLK_SUN5I_H_

#define CLK_HOSC		1

#define CLK_CPU			17

#define CLK_AHB_OTG		23
#define CLK_AHB_EHCI		24
#define CLK_AHB_OHCI		25
#define CLK_AHB_SS		26
#define CLK_AHB_DMA		27
#define CLK_AHB_BIST		28
#define CLK_AHB_MMC0		29
#define CLK_AHB_MMC1		30
#define CLK_AHB_MMC2		31
#define CLK_AHB_NAND		32
#define CLK_AHB_SDRAM		33
#define CLK_AHB_EMAC		34
#define CLK_AHB_TS		35
#define CLK_AHB_SPI0		36
#define CLK_AHB_SPI1		37
#define CLK_AHB_SPI2		38
#define CLK_AHB_GPS		39
#define CLK_AHB_HSTIMER		40
#define CLK_AHB_VE		41
#define CLK_AHB_TVE		42
#define CLK_AHB_LCD		43
#define CLK_AHB_CSI		44
#define CLK_AHB_HDMI		45
#define CLK_AHB_DE_BE		46
#define CLK_AHB_DE_FE		47
#define CLK_AHB_IEP		48
#define CLK_AHB_GPU		49
#define CLK_APB0_CODEC		50
#define CLK_APB0_SPDIF		51
#define CLK_APB0_I2S		52
#define CLK_APB0_PIO		53
#define CLK_APB0_IR		54
#define CLK_APB0_KEYPAD		55
#define CLK_APB1_I2C0		56
#define CLK_APB1_I2C1		57
#define CLK_APB1_I2C2		58
#define CLK_APB1_UART0		59
#define CLK_APB1_UART1		60
#define CLK_APB1_UART2		61
#define CLK_APB1_UART3		62
#define CLK_NAND		63
#define CLK_MMC0		64
#define CLK_MMC1		65
#define CLK_MMC2		66
#define CLK_TS			67
#define CLK_SS			68
#define CLK_SPI0		69
#define CLK_SPI1		70
#define CLK_SPI2		71
#define CLK_IR			72
#define CLK_I2S			73
#define CLK_SPDIF		74
#define CLK_KEYPAD		75
#define CLK_USB_OHCI		76
#define CLK_USB_PHY0		77
#define CLK_USB_PHY1		78
#define CLK_GPS			79
#define CLK_DRAM_VE		80
#define CLK_DRAM_CSI		81
#define CLK_DRAM_TS		82
#define CLK_DRAM_TVE		83
#define CLK_DRAM_DE_FE		84
#define CLK_DRAM_DE_BE		85
#define CLK_DRAM_ACE		86
#define CLK_DRAM_IEP		87
#define CLK_DE_BE		88
#define CLK_DE_FE		89
#define CLK_TCON_CH0		90

#define CLK_TCON_CH1		92
#define CLK_CSI			93
#define CLK_VE			94
#define CLK_CODEC		95
#define CLK_AVS			96
#define CLK_HDMI		97
#define CLK_GPU			98

#define CLK_IEP			100

#endif /* _DT_BINDINGS_CLK_SUN5I_H_ */
