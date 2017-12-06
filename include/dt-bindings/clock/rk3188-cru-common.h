/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
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

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RK3188_COMMON_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RK3188_COMMON_H

/* core clocks from */
#define PLL_APLL		1
#define PLL_DPLL		2
#define PLL_CPLL		3
#define PLL_GPLL		4
#define CORE_PERI		5
#define CORE_L2C		6
#define ARMCLK			7

/* sclk gates (special clocks) */
#define SCLK_UART0		64
#define SCLK_UART1		65
#define SCLK_UART2		66
#define SCLK_UART3		67
#define SCLK_MAC		68
#define SCLK_SPI0		69
#define SCLK_SPI1		70
#define SCLK_SARADC		71
#define SCLK_SDMMC		72
#define SCLK_SDIO		73
#define SCLK_EMMC		74
#define SCLK_I2S0		75
#define SCLK_I2S1		76
#define SCLK_I2S2		77
#define SCLK_SPDIF		78
#define SCLK_CIF0		79
#define SCLK_CIF1		80
#define SCLK_OTGPHY0		81
#define SCLK_OTGPHY1		82
#define SCLK_HSADC		83
#define SCLK_TIMER0		84
#define SCLK_TIMER1		85
#define SCLK_TIMER2		86
#define SCLK_TIMER3		87
#define SCLK_TIMER4		88
#define SCLK_TIMER5		89
#define SCLK_TIMER6		90
#define SCLK_JTAG		91
#define SCLK_SMC		92

#define DCLK_LCDC0		190
#define DCLK_LCDC1		191

/* aclk gates */
#define ACLK_DMA1		192
#define ACLK_DMA2		193
#define ACLK_GPS		194
#define ACLK_LCDC0		195
#define ACLK_LCDC1		196
#define ACLK_GPU		197
#define ACLK_SMC		198
#define ACLK_CIF		199
#define ACLK_IPP		200
#define ACLK_RGA		201
#define ACLK_CIF0		202
#define ACLK_VEPU		203
#define ACLK_VDPU		204
#define ACLK_CPU		205
#define ACLK_PERI		206
#define ACLK_CIF1		207

/* pclk gates */
#define PCLK_GRF		320
#define PCLK_PMU		321
#define PCLK_TIMER0		322
#define PCLK_TIMER1		323
#define PCLK_TIMER2		324
#define PCLK_TIMER3		325
#define PCLK_PWM01		326
#define PCLK_PWM23		327
#define PCLK_SPI0		328
#define PCLK_SPI1		329
#define PCLK_SARADC		330
#define PCLK_WDT		331
#define PCLK_UART0		332
#define PCLK_UART1		333
#define PCLK_UART2		334
#define PCLK_UART3		335
#define PCLK_I2C0		336
#define PCLK_I2C1		337
#define PCLK_I2C2		338
#define PCLK_I2C3		339
#define PCLK_I2C4		340
#define PCLK_GPIO0		341
#define PCLK_GPIO1		342
#define PCLK_GPIO2		343
#define PCLK_GPIO3		344
#define PCLK_GPIO4		345
#define PCLK_GPIO6		346
#define PCLK_EFUSE		347
#define PCLK_TZPC		348
#define PCLK_TSADC		349
#define PCLK_CPU		350
#define PCLK_PERI		351
#define PCLK_CIF0		352
#define PCLK_CIF1		353

/* hclk gates */
#define HCLK_SDMMC		448
#define HCLK_SDIO		449
#define HCLK_EMMC		450
#define HCLK_OTG0		451
#define HCLK_EMAC		452
#define HCLK_SPDIF		453
#define HCLK_I2S0_2CH		454
#define HCLK_I2S1_2CH		455
#define HCLK_I2S_8CH		456
#define HCLK_OTG1		457
#define HCLK_HSIC		458
#define HCLK_HSADC		459
#define HCLK_PIDF		460
#define HCLK_LCDC0		461
#define HCLK_LCDC1		462
#define HCLK_ROM		463
#define HCLK_CIF0		464
#define HCLK_IPP		465
#define HCLK_RGA		466
#define HCLK_NANDC0		467
#define HCLK_VEPU		468
#define HCLK_VDPU		469
#define HCLK_CPU		470
#define HCLK_PERI		471
#define HCLK_CIF1		472
#define HCLK_HDMI		473

#define CLK_NR_CLKS		(HCLK_HDMI + 1)

/* soft-reset indices */
#define SRST_MCORE		2
#define SRST_CORE0		3
#define SRST_CORE1		4
#define SRST_MCORE_DBG		7
#define SRST_CORE0_DBG		8
#define SRST_CORE1_DBG		9
#define SRST_CORE0_WDT		12
#define SRST_CORE1_WDT		13
#define SRST_STRC_SYS		14
#define SRST_L2C		15

#define SRST_CPU_AHB		17
#define SRST_AHB2APB		19
#define SRST_DMA1		20
#define SRST_INTMEM		21
#define SRST_ROM		22
#define SRST_SPDIF		26
#define SRST_TIMER0		27
#define SRST_TIMER1		28
#define SRST_EFUSE		30

#define SRST_GPIO0		32
#define SRST_GPIO1		33
#define SRST_GPIO2		34
#define SRST_GPIO3		35

#define SRST_UART0		39
#define SRST_UART1		40
#define SRST_UART2		41
#define SRST_UART3		42
#define SRST_I2C0		43
#define SRST_I2C1		44
#define SRST_I2C2		45
#define SRST_I2C3		46
#define SRST_I2C4		47

#define SRST_PWM0		48
#define SRST_PWM1		49
#define SRST_DAP_PO		50
#define SRST_DAP		51
#define SRST_DAP_SYS		52
#define SRST_TPIU_ATB		53
#define SRST_PMU_APB		54
#define SRST_GRF		55
#define SRST_PMU		56
#define SRST_PERI_AXI		57
#define SRST_PERI_AHB		58
#define SRST_PERI_APB		59
#define SRST_PERI_NIU		60
#define SRST_CPU_PERI		61
#define SRST_EMEM_PERI		62
#define SRST_USB_PERI		63

#define SRST_DMA2		64
#define SRST_SMC		65
#define SRST_MAC		66
#define SRST_NANC0		68
#define SRST_USBOTG0		69
#define SRST_USBPHY0		70
#define SRST_OTGC0		71
#define SRST_USBOTG1		72
#define SRST_USBPHY1		73
#define SRST_OTGC1		74
#define SRST_HSADC		76
#define SRST_PIDFILTER		77
#define SRST_DDR_MSCH		79

#define SRST_TZPC		80
#define SRST_SDMMC		81
#define SRST_SDIO		82
#define SRST_EMMC		83
#define SRST_SPI0		84
#define SRST_SPI1		85
#define SRST_WDT		86
#define SRST_SARADC		87
#define SRST_DDRPHY		88
#define SRST_DDRPHY_APB		89
#define SRST_DDRCTL		90
#define SRST_DDRCTL_APB		91
#define SRST_DDRPUB		93

#define SRST_VIO0_AXI		98
#define SRST_VIO0_AHB		99
#define SRST_LCDC0_AXI		100
#define SRST_LCDC0_AHB		101
#define SRST_LCDC0_DCLK		102
#define SRST_LCDC1_AXI		103
#define SRST_LCDC1_AHB		104
#define SRST_LCDC1_DCLK		105
#define SRST_IPP_AXI		106
#define SRST_IPP_AHB		107
#define SRST_RGA_AXI		108
#define SRST_RGA_AHB		109
#define SRST_CIF0		110

#define SRST_VCODEC_AXI		112
#define SRST_VCODEC_AHB		113
#define SRST_VIO1_AXI		114
#define SRST_VCODEC_CPU		115
#define SRST_VCODEC_NIU		116
#define SRST_GPU		120
#define SRST_GPU_NIU		122
#define SRST_TFUN_ATB		125
#define SRST_TFUN_APB		126
#define SRST_CTI4_APB		127

#define SRST_TPIU_APB		128
#define SRST_TRACE		129
#define SRST_CORE_DBG		130
#define SRST_DBG_APB		131
#define SRST_CTI0		132
#define SRST_CTI0_APB		133
#define SRST_CTI1		134
#define SRST_CTI1_APB		135
#define SRST_PTM_CORE0		136
#define SRST_PTM_CORE1		137
#define SRST_PTM0		138
#define SRST_PTM0_ATB		139
#define SRST_PTM1		140
#define SRST_PTM1_ATB		141
#define SRST_CTM		142
#define SRST_TS			143

#endif
