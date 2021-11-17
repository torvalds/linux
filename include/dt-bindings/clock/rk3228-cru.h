/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2015 Rockchip Electronics Co. Ltd.
 * Author: Jeffy Chen <jeffy.chen@rock-chips.com>
 */

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RK3228_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RK3228_H

/* core clocks */
#define PLL_APLL		1
#define PLL_DPLL		2
#define PLL_CPLL		3
#define PLL_GPLL		4
#define ARMCLK			5

/* sclk gates (special clocks) */
#define SCLK_SPI0		65
#define SCLK_NANDC		67
#define SCLK_SDMMC		68
#define SCLK_SDIO		69
#define SCLK_EMMC		71
#define SCLK_TSADC		72
#define SCLK_UART0		77
#define SCLK_UART1		78
#define SCLK_UART2		79
#define SCLK_I2S0		80
#define SCLK_I2S1		81
#define SCLK_I2S2		82
#define SCLK_SPDIF		83
#define SCLK_TIMER0		85
#define SCLK_TIMER1		86
#define SCLK_TIMER2		87
#define SCLK_TIMER3		88
#define SCLK_TIMER4		89
#define SCLK_TIMER5		90
#define SCLK_I2S_OUT		113
#define SCLK_SDMMC_DRV		114
#define SCLK_SDIO_DRV		115
#define SCLK_EMMC_DRV		117
#define SCLK_SDMMC_SAMPLE	118
#define SCLK_SDIO_SAMPLE	119
#define SCLK_SDIO_SRC		120
#define SCLK_EMMC_SAMPLE	121
#define SCLK_VOP		122
#define SCLK_HDMI_HDCP		123
#define SCLK_MAC_SRC		124
#define SCLK_MAC_EXTCLK		125
#define SCLK_MAC		126
#define SCLK_MAC_REFOUT		127
#define SCLK_MAC_REF		128
#define SCLK_MAC_RX		129
#define SCLK_MAC_TX		130
#define SCLK_MAC_PHY		131
#define SCLK_MAC_OUT		132
#define SCLK_VDEC_CABAC		133
#define SCLK_VDEC_CORE		134
#define SCLK_RGA		135
#define SCLK_HDCP		136
#define SCLK_HDMI_CEC		137
#define SCLK_CRYPTO		138
#define SCLK_TSP		139
#define SCLK_HSADC		140
#define SCLK_WIFI		141
#define SCLK_OTGPHY0		142
#define SCLK_OTGPHY1		143
#define SCLK_HDMI_PHY		144

/* dclk gates */
#define DCLK_VOP		190
#define DCLK_HDMI_PHY		191

/* aclk gates */
#define ACLK_DMAC		194
#define ACLK_CPU		195
#define ACLK_VPU_PRE		196
#define ACLK_RKVDEC_PRE		197
#define ACLK_RGA_PRE		198
#define ACLK_IEP_PRE		199
#define ACLK_HDCP_PRE		200
#define ACLK_VOP_PRE		201
#define ACLK_VPU		202
#define ACLK_RKVDEC		203
#define ACLK_IEP		204
#define ACLK_RGA		205
#define ACLK_HDCP		206
#define ACLK_PERI		210
#define ACLK_VOP		211
#define ACLK_GMAC		212
#define ACLK_GPU		213

/* pclk gates */
#define PCLK_GPIO0		320
#define PCLK_GPIO1		321
#define PCLK_GPIO2		322
#define PCLK_GPIO3		323
#define PCLK_VIO_H2P		324
#define PCLK_HDCP		325
#define PCLK_EFUSE_1024		326
#define PCLK_EFUSE_256		327
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
#define PCLK_HDMI_CTRL		364
#define PCLK_HDMI_PHY		365
#define PCLK_GMAC		367

/* hclk gates */
#define HCLK_I2S0_8CH		442
#define HCLK_I2S1_8CH		443
#define HCLK_I2S2_2CH		444
#define HCLK_SPDIF_8CH		445
#define HCLK_VOP		452
#define HCLK_NANDC		453
#define HCLK_SDMMC		456
#define HCLK_SDIO		457
#define HCLK_EMMC		459
#define HCLK_CPU		460
#define HCLK_VPU_PRE		461
#define HCLK_RKVDEC_PRE		462
#define HCLK_VIO_PRE		463
#define HCLK_VPU		464
#define HCLK_RKVDEC		465
#define HCLK_VIO		466
#define HCLK_RGA		467
#define HCLK_IEP		468
#define HCLK_VIO_H2P		469
#define HCLK_HDCP_MMU		470
#define HCLK_HOST0		471
#define HCLK_HOST1		472
#define HCLK_HOST2		473
#define HCLK_OTG		474
#define HCLK_TSP		475
#define HCLK_M_CRYPTO		476
#define HCLK_S_CRYPTO		477
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
#define SRST_NOC		14
#define SRST_L2C		15

#define SRST_CPUSYS_H		18
#define SRST_BUSSYS_H		19
#define SRST_SPDIF		20
#define SRST_INTMEM		21
#define SRST_ROM		22
#define SRST_OTG_ADP		23
#define SRST_I2S0		24
#define SRST_I2S1		25
#define SRST_I2S2		26
#define SRST_ACODEC_P		27
#define SRST_DFIMON		28
#define SRST_MSCH		29
#define SRST_EFUSE1024		30
#define SRST_EFUSE256		31

#define SRST_GPIO0		32
#define SRST_GPIO1		33
#define SRST_GPIO2		34
#define SRST_GPIO3		35
#define SRST_PERIPH_NOC_A	36
#define SRST_PERIPH_NOC_BUS_H	37
#define SRST_PERIPH_NOC_P	38
#define SRST_UART0		39
#define SRST_UART1		40
#define SRST_UART2		41
#define SRST_PHYNOC		42
#define SRST_I2C0		43
#define SRST_I2C1		44
#define SRST_I2C2		45
#define SRST_I2C3		46

#define SRST_PWM		48
#define SRST_A53_GIC		49
#define SRST_DAP		51
#define SRST_DAP_NOC		52
#define SRST_CRYPTO		53
#define SRST_SGRF		54
#define SRST_GRF		55
#define SRST_GMAC		56
#define SRST_PERIPH_NOC_H	58
#define SRST_MACPHY		63

#define SRST_DMA		64
#define SRST_NANDC		68
#define SRST_USBOTG		69
#define SRST_OTGC		70
#define SRST_USBHOST0		71
#define SRST_HOST_CTRL0		72
#define SRST_USBHOST1		73
#define SRST_HOST_CTRL1		74
#define SRST_USBHOST2		75
#define SRST_HOST_CTRL2		76
#define SRST_USBPOR0		77
#define SRST_USBPOR1		78
#define SRST_DDRMSCH		79

#define SRST_SMART_CARD		80
#define SRST_SDMMC		81
#define SRST_SDIO		82
#define SRST_EMMC		83
#define SRST_SPI		84
#define SRST_TSP_H		85
#define SRST_TSP		86
#define SRST_TSADC		87
#define SRST_DDRPHY		88
#define SRST_DDRPHY_P		89
#define SRST_DDRCTRL		90
#define SRST_DDRCTRL_P		91
#define SRST_HOST0_ECHI		92
#define SRST_HOST1_ECHI		93
#define SRST_HOST2_ECHI		94
#define SRST_VOP_NOC_A		95

#define SRST_HDMI_P		96
#define SRST_VIO_ARBI_H		97
#define SRST_IEP_NOC_A		98
#define SRST_VIO_NOC_H		99
#define SRST_VOP_A		100
#define SRST_VOP_H		101
#define SRST_VOP_D		102
#define SRST_UTMI0		103
#define SRST_UTMI1		104
#define SRST_UTMI2		105
#define SRST_UTMI3		106
#define SRST_RGA		107
#define SRST_RGA_NOC_A		108
#define SRST_RGA_A		109
#define SRST_RGA_H		110
#define SRST_HDCP_A		111

#define SRST_VPU_A		112
#define SRST_VPU_H		113
#define SRST_VPU_NOC_A		116
#define SRST_VPU_NOC_H		117
#define SRST_RKVDEC_A		118
#define SRST_RKVDEC_NOC_A	119
#define SRST_RKVDEC_H		120
#define SRST_RKVDEC_NOC_H	121
#define SRST_RKVDEC_CORE	122
#define SRST_RKVDEC_CABAC	123
#define SRST_IEP_A		124
#define SRST_IEP_H		125
#define SRST_GPU_A		126
#define SRST_GPU_NOC_A		127

#define SRST_CORE_DBG		128
#define SRST_DBG_P		129
#define SRST_TIMER0		130
#define SRST_TIMER1		131
#define SRST_TIMER2		132
#define SRST_TIMER3		133
#define SRST_TIMER4		134
#define SRST_TIMER5		135
#define SRST_VIO_H2P		136
#define SRST_HDMIPHY		139
#define SRST_VDAC		140
#define SRST_TIMER_6CH_P	141

#endif
