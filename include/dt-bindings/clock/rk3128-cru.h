/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 * Author: Elaine <zhangqing@rock-chips.com>
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

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RK3128_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RK3128_H

/* core clocks */
#define PLL_APLL		1
#define PLL_DPLL		2
#define PLL_CPLL		3
#define PLL_GPLL		4
#define ARMCLK			5
#define PLL_GPLL_DIV2		6
#define PLL_GPLL_DIV3		7

/* sclk gates (special clocks) */
#define SCLK_SPI0		65
#define SCLK_NANDC		67
#define SCLK_SDMMC		68
#define SCLK_SDIO		69
#define SCLK_EMMC		71
#define SCLK_UART0		77
#define SCLK_UART1		78
#define SCLK_UART2		79
#define SCLK_I2S0		80
#define SCLK_I2S1		81
#define SCLK_SPDIF		83
#define SCLK_TIMER0		85
#define SCLK_TIMER1		86
#define SCLK_TIMER2		87
#define SCLK_TIMER3		88
#define SCLK_TIMER4		89
#define SCLK_TIMER5		90
#define SCLK_SARADC		91
#define SCLK_I2S_OUT		113
#define SCLK_SDMMC_DRV		114
#define SCLK_SDIO_DRV		115
#define SCLK_EMMC_DRV		117
#define SCLK_SDMMC_SAMPLE	118
#define SCLK_SDIO_SAMPLE	119
#define SCLK_EMMC_SAMPLE	121
#define SCLK_VOP		122
#define SCLK_MAC_SRC		124
#define SCLK_MAC		126
#define SCLK_MAC_REFOUT		127
#define SCLK_MAC_REF		128
#define SCLK_MAC_RX		129
#define SCLK_MAC_TX		130
#define SCLK_HEVC_CORE		134
#define SCLK_RGA		135
#define SCLK_CRYPTO		138
#define SCLK_TSP		139
#define SCLK_OTGPHY0		142
#define SCLK_OTGPHY1		143
#define SCLK_DDRC		144
#define SCLK_PVTM_FUNC		145
#define SCLK_PVTM_CORE		146
#define SCLK_PVTM_GPU		147
#define SCLK_MIPI_24M		148
#define SCLK_PVTM		149
#define SCLK_CIF_SRC		150
#define SCLK_CIF_OUT_SRC	151
#define SCLK_CIF_OUT		152
#define SCLK_SFC		153
#define SCLK_USB480M		154

/* dclk gates */
#define DCLK_VOP		190
#define DCLK_EBC		191

/* aclk gates */
#define ACLK_VIO0		192
#define ACLK_VIO1		193
#define ACLK_DMAC		194
#define ACLK_CPU		195
#define ACLK_VEPU		196
#define ACLK_VDPU		197
#define ACLK_CIF		198
#define ACLK_IEP		199
#define ACLK_LCDC0		204
#define ACLK_RGA		205
#define ACLK_PERI		210
#define ACLK_VOP		211
#define ACLK_GMAC		212
#define ACLK_GPU		213

/* pclk gates */
#define PCLK_SARADC		318
#define PCLK_WDT		319
#define PCLK_GPIO0		320
#define PCLK_GPIO1		321
#define PCLK_GPIO2		322
#define PCLK_GPIO3		323
#define PCLK_VIO_H2P		324
#define PCLK_MIPI		325
#define PCLK_EFUSE		326
#define PCLK_HDMI		327
#define PCLK_ACODEC		328
#define PCLK_GRF		329
#define PCLK_I2C0		332
#define PCLK_I2C1		333
#define PCLK_I2C2		334
#define PCLK_I2C3		335
#define PCLK_SPI0		338
#define PCLK_UART0		341
#define PCLK_UART1		342
#define PCLK_UART2		343
#define PCLK_TSADC		344
#define PCLK_PWM		350
#define PCLK_TIMER		353
#define PCLK_CPU		354
#define PCLK_PERI		363
#define PCLK_GMAC		367
#define PCLK_PMU_PRE		368
#define PCLK_SIM_CARD		369

/* hclk gates */
#define HCLK_SPDIF		440
#define HCLK_GPS		441
#define HCLK_USBHOST		442
#define HCLK_I2S_8CH		443
#define HCLK_I2S_2CH		444
#define HCLK_VOP		452
#define HCLK_NANDC		453
#define HCLK_SDMMC		456
#define HCLK_SDIO		457
#define HCLK_EMMC		459
#define HCLK_CPU		460
#define HCLK_VEPU		461
#define HCLK_VDPU		462
#define HCLK_LCDC0		463
#define HCLK_EBC		465
#define HCLK_VIO		466
#define HCLK_RGA		467
#define HCLK_IEP		468
#define HCLK_VIO_H2P		469
#define HCLK_CIF		470
#define HCLK_HOST2		473
#define HCLK_OTG		474
#define HCLK_TSP		475
#define HCLK_CRYPTO		476
#define HCLK_PERI		478

#define CLK_NR_CLKS		(HCLK_PERI + 1)

/* soft-reset indices */
#define SRST_CORE0_PO		0
#define SRST_CORE1_PO		1
#define SRST_CORE2_PO		2
#define SRST_CORE3_PO		3
#define SRST_CORE0		4
#define SRST_CORE1		5
#define SRST_CORE2		6
#define SRST_CORE3		7
#define SRST_CORE0_DBG		8
#define SRST_CORE1_DBG		9
#define SRST_CORE2_DBG		10
#define SRST_CORE3_DBG		11
#define SRST_TOPDBG		12
#define SRST_ACLK_CORE		13
#define SRST_STRC_SYS_A		14
#define SRST_L2C		15

#define SRST_CPUSYS_H		18
#define SRST_AHB2APBSYS_H	19
#define SRST_SPDIF		20
#define SRST_INTMEM		21
#define SRST_ROM		22
#define SRST_PERI_NIU		23
#define SRST_I2S_2CH		24
#define SRST_I2S_8CH		25
#define SRST_GPU_PVTM		26
#define SRST_FUNC_PVTM		27
#define SRST_CORE_PVTM		29
#define SRST_EFUSE_P		30
#define SRST_ACODEC_P		31

#define SRST_GPIO0		32
#define SRST_GPIO1		33
#define SRST_GPIO2		34
#define SRST_GPIO3		35
#define SRST_MIPIPHY_P		36
#define SRST_UART0		39
#define SRST_UART1		40
#define SRST_UART2		41
#define SRST_I2C0		43
#define SRST_I2C1		44
#define SRST_I2C2		45
#define SRST_I2C3		46
#define SRST_SFC		47

#define SRST_PWM		48
#define SRST_DAP_PO		50
#define SRST_DAP		51
#define SRST_DAP_SYS		52
#define SRST_CRYPTO		53
#define SRST_GRF		55
#define SRST_GMAC		56
#define SRST_PERIPH_SYS_A	57
#define SRST_PERIPH_SYS_H	58
#define SRST_PERIPH_SYS_P       59
#define SRST_SMART_CARD		60
#define SRST_CPU_PERI		61
#define SRST_EMEM_PERI		62
#define SRST_USB_PERI		63

#define SRST_DMA		64
#define SRST_GPS		67
#define SRST_NANDC		68
#define SRST_USBOTG0		69
#define SRST_OTGC0		71
#define SRST_USBOTG1		72
#define SRST_OTGC1		74
#define SRST_DDRMSCH		79

#define SRST_SDMMC		81
#define SRST_SDIO		82
#define SRST_EMMC		83
#define SRST_SPI		84
#define SRST_WDT		86
#define SRST_SARADC		87
#define SRST_DDRPHY		88
#define SRST_DDRPHY_P		89
#define SRST_DDRCTRL		90
#define SRST_DDRCTRL_P		91
#define SRST_TSP		92
#define SRST_TSP_CLKIN		93
#define SRST_HOST0_ECHI		94

#define SRST_HDMI_P		96
#define SRST_VIO_ARBI_H		97
#define SRST_VIO0_A		98
#define SRST_VIO_BUS_H		99
#define SRST_VOP_A		100
#define SRST_VOP_H		101
#define SRST_VOP_D		102
#define SRST_UTMI0		103
#define SRST_UTMI1		104
#define SRST_USBPOR		105
#define SRST_IEP_A		106
#define SRST_IEP_H		107
#define SRST_RGA_A		108
#define SRST_RGA_H		109
#define SRST_CIF0		110
#define SRST_PMU		111

#define SRST_VCODEC_A		112
#define SRST_VCODEC_H		113
#define SRST_VIO1_A		114
#define SRST_HEVC_CORE		115
#define SRST_VCODEC_NIU_A	116
#define SRST_PMU_NIU_P		117
#define SRST_LCDC0_S		119
#define SRST_GPU		120
#define SRST_GPU_NIU_A		122
#define SRST_EBC_A		123
#define SRST_EBC_H		124

#define SRST_CORE_DBG		128
#define SRST_DBG_P		129
#define SRST_TIMER0		130
#define SRST_TIMER1		131
#define SRST_TIMER2		132
#define SRST_TIMER3		133
#define SRST_TIMER4		134
#define SRST_TIMER5		135
#define SRST_VIO_H2P		136
#define SRST_VIO_MIPI_DSI	137

#endif
