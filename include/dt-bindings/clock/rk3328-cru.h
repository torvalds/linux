/*
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
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

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RK3328_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RK3328_H

/* core clocks */
#define PLL_APLL		1
#define PLL_DPLL		2
#define PLL_CPLL		3
#define PLL_GPLL		4
#define PLL_NPLL		5
#define ARMCLK			6

/* sclk gates (special clocks) */
#define SCLK_RTC32K		30
#define SCLK_SDMMC_EXT		31
#define SCLK_SPI		32
#define SCLK_SDMMC		33
#define SCLK_SDIO		34
#define SCLK_EMMC		35
#define SCLK_TSADC		36
#define SCLK_SARADC		37
#define SCLK_UART0		38
#define SCLK_UART1		39
#define SCLK_UART2		40
#define SCLK_I2S0		41
#define SCLK_I2S1		42
#define SCLK_I2S2		43
#define SCLK_I2S1_OUT		44
#define SCLK_I2S2_OUT		45
#define SCLK_SPDIF		46
#define SCLK_TIMER0		47
#define SCLK_TIMER1		48
#define SCLK_TIMER2		49
#define SCLK_TIMER3		50
#define SCLK_TIMER4		51
#define SCLK_TIMER5		52
#define SCLK_WIFI		53
#define SCLK_CIF_OUT		54
#define SCLK_I2C0		55
#define SCLK_I2C1		56
#define SCLK_I2C2		57
#define SCLK_I2C3		58
#define SCLK_CRYPTO		59
#define SCLK_PWM		60
#define SCLK_PDM		61
#define SCLK_EFUSE		62
#define SCLK_OTP		63
#define SCLK_DDRCLK		64
#define SCLK_VDEC_CABAC		65
#define SCLK_VDEC_CORE		66
#define SCLK_VENC_DSP		67
#define SCLK_VENC_CORE		68
#define SCLK_RGA		69
#define SCLK_HDMI_SFC		70
#define SCLK_HDMI_CEC		71
#define SCLK_USB3_REF		72
#define SCLK_USB3_SUSPEND	73
#define SCLK_SDMMC_DRV		74
#define SCLK_SDIO_DRV		75
#define SCLK_EMMC_DRV		76
#define SCLK_SDMMC_EXT_DRV	77
#define SCLK_SDMMC_SAMPLE	78
#define SCLK_SDIO_SAMPLE	79
#define SCLK_EMMC_SAMPLE	80
#define SCLK_SDMMC_EXT_SAMPLE	81
#define SCLK_VOP		82
#define SCLK_MAC2PHY_RXTX	83
#define SCLK_MAC2PHY_SRC	84
#define SCLK_MAC2PHY_REF	85
#define SCLK_MAC2PHY_OUT	86
#define SCLK_MAC2IO_RX		87
#define SCLK_MAC2IO_TX		88
#define SCLK_MAC2IO_REFOUT	89
#define SCLK_MAC2IO_REF		90
#define SCLK_MAC2IO_OUT		91
#define SCLK_TSP		92
#define SCLK_HSADC_TSP		93
#define SCLK_USB3PHY_REF	94
#define SCLK_REF_USB3OTG	95
#define SCLK_USB3OTG_REF	96
#define SCLK_USB3OTG_SUSPEND	97
#define SCLK_REF_USB3OTG_SRC	98
#define SCLK_MAC2IO_SRC		99

/* dclk gates */
#define DCLK_LCDC		180
#define DCLK_HDMIPHY		181
#define HDMIPHY			182
#define USB480M			183
#define DCLK_LCDC_SRC		184

/* aclk gates */
#define ACLK_AXISRAM		190
#define ACLK_VOP_PRE		191
#define ACLK_USB3OTG		192
#define ACLK_RGA_PRE		193
#define ACLK_DMAC		194
#define ACLK_GPU		195
#define ACLK_BUS_PRE		196
#define ACLK_PERI_PRE		197
#define ACLK_RKVDEC_PRE		198
#define ACLK_RKVDEC		199
#define ACLK_RKVENC		200
#define ACLK_VPU_PRE		201
#define ACLK_VIO_PRE		202
#define ACLK_VPU		203
#define ACLK_VIO		204
#define ACLK_VOP		205
#define ACLK_GMAC		206
#define ACLK_H265		207
#define ACLK_H264		208
#define ACLK_MAC2PHY		209
#define ACLK_MAC2IO		210
#define ACLK_DCF		211
#define ACLK_TSP		212
#define ACLK_PERI		213
#define ACLK_RGA		214
#define ACLK_IEP		215
#define ACLK_CIF		216
#define ACLK_HDCP		217

/* pclk gates */
#define PCLK_GPIO0		300
#define PCLK_GPIO1		301
#define PCLK_GPIO2		302
#define PCLK_GPIO3		303
#define PCLK_GRF		304
#define PCLK_I2C0		305
#define PCLK_I2C1		306
#define PCLK_I2C2		307
#define PCLK_I2C3		308
#define PCLK_SPI		309
#define PCLK_UART0		310
#define PCLK_UART1		311
#define PCLK_UART2		312
#define PCLK_TSADC		313
#define PCLK_PWM		314
#define PCLK_TIMER		315
#define PCLK_BUS_PRE		316
#define PCLK_PERI_PRE		317
#define PCLK_HDMI_CTRL		318
#define PCLK_HDMI_PHY		319
#define PCLK_GMAC		320
#define PCLK_H265		321
#define PCLK_MAC2PHY		322
#define PCLK_MAC2IO		323
#define PCLK_USB3PHY_OTG	324
#define PCLK_USB3PHY_PIPE	325
#define PCLK_USB3_GRF		326
#define PCLK_USB2_GRF		327
#define PCLK_HDMIPHY		328
#define PCLK_DDR		329
#define PCLK_PERI		330
#define PCLK_HDMI		331
#define PCLK_HDCP		332
#define PCLK_DCF		333
#define PCLK_SARADC		334

/* hclk gates */
#define HCLK_PERI		408
#define HCLK_TSP		409
#define HCLK_GMAC		410
#define HCLK_I2S0_8CH		411
#define HCLK_I2S1_8CH		413
#define HCLK_I2S2_2CH		413
#define HCLK_SPDIF_8CH		414
#define HCLK_VOP		415
#define HCLK_NANDC		416
#define HCLK_SDMMC		417
#define HCLK_SDIO		418
#define HCLK_EMMC		419
#define HCLK_SDMMC_EXT		420
#define HCLK_RKVDEC_PRE		421
#define HCLK_RKVDEC		422
#define HCLK_RKVENC		423
#define HCLK_VPU_PRE		424
#define HCLK_VIO_PRE		425
#define HCLK_VPU		426
#define HCLK_VIO		427
#define HCLK_BUS_PRE		428
#define HCLK_PERI_PRE		429
#define HCLK_H264		430
#define HCLK_CIF		431
#define HCLK_OTG_PMU		432
#define HCLK_OTG		433
#define HCLK_HOST0		434
#define HCLK_HOST0_ARB		435
#define HCLK_CRYPTO_MST		436
#define HCLK_CRYPTO_SLV		437
#define HCLK_PDM		438
#define HCLK_IEP		439
#define HCLK_RGA		440
#define HCLK_HDCP		441

#define CLK_NR_CLKS		(HCLK_HDCP + 1)

#define SCLK_MAC2IO		0
#define SCLK_MAC2PHY		1

#define CLKGRF_NR_CLKS		(SCLK_MAC2PHY + 1)

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
#define SRST_CORE_NIU		13
#define SRST_STRC_A		14
#define SRST_L2C		15

#define SRST_A53_GIC		18
#define SRST_DAP		19
#define SRST_PMU_P		21
#define SRST_EFUSE		22
#define SRST_BUSSYS_H		23
#define SRST_BUSSYS_P		24
#define SRST_SPDIF		25
#define SRST_INTMEM		26
#define SRST_ROM		27
#define SRST_GPIO0		28
#define SRST_GPIO1		29
#define SRST_GPIO2		30
#define SRST_GPIO3		31

#define SRST_I2S0		32
#define SRST_I2S1		33
#define SRST_I2S2		34
#define SRST_I2S0_H		35
#define SRST_I2S1_H		36
#define SRST_I2S2_H		37
#define SRST_UART0		38
#define SRST_UART1		39
#define SRST_UART2		40
#define SRST_UART0_P		41
#define SRST_UART1_P		42
#define SRST_UART2_P		43
#define SRST_I2C0		44
#define SRST_I2C1		45
#define SRST_I2C2		46
#define SRST_I2C3		47

#define SRST_I2C0_P		48
#define SRST_I2C1_P		49
#define SRST_I2C2_P		50
#define SRST_I2C3_P		51
#define SRST_EFUSE_SE_P		52
#define SRST_EFUSE_NS_P		53
#define SRST_PWM0		54
#define SRST_PWM0_P		55
#define SRST_DMA		56
#define SRST_TSP_A		57
#define SRST_TSP_H		58
#define SRST_TSP		59
#define SRST_TSP_HSADC		60
#define SRST_DCF_A		61
#define SRST_DCF_P		62

#define SRST_SCR		64
#define SRST_SPI		65
#define SRST_TSADC		66
#define SRST_TSADC_P		67
#define SRST_CRYPTO		68
#define SRST_SGRF		69
#define SRST_GRF		70
#define SRST_USB_GRF		71
#define SRST_TIMER_6CH_P	72
#define SRST_TIMER0		73
#define SRST_TIMER1		74
#define SRST_TIMER2		75
#define SRST_TIMER3		76
#define SRST_TIMER4		77
#define SRST_TIMER5		78
#define SRST_USB3GRF		79

#define SRST_PHYNIU		80
#define SRST_HDMIPHY		81
#define SRST_VDAC		82
#define SRST_ACODEC_p		83
#define SRST_SARADC		85
#define SRST_SARADC_P		86
#define SRST_GRF_DDR		87
#define SRST_DFIMON		88
#define SRST_MSCH		89
#define SRST_DDRMSCH		91
#define SRST_DDRCTRL		92
#define SRST_DDRCTRL_P		93
#define SRST_DDRPHY		94
#define SRST_DDRPHY_P		95

#define SRST_GMAC_NIU_A		96
#define SRST_GMAC_NIU_P		97
#define SRST_GMAC2PHY_A		98
#define SRST_GMAC2IO_A		99
#define SRST_MACPHY		100
#define SRST_OTP_PHY		101
#define SRST_GPU_A		102
#define SRST_GPU_NIU_A		103
#define SRST_SDMMCEXT		104
#define SRST_PERIPH_NIU_A	105
#define SRST_PERIHP_NIU_H	106
#define SRST_PERIHP_P		107
#define SRST_PERIPHSYS_H	108
#define SRST_MMC0		109
#define SRST_SDIO		110
#define SRST_EMMC		111

#define SRST_USB2OTG_H		112
#define SRST_USB2OTG		113
#define SRST_USB2OTG_ADP	114
#define SRST_USB2HOST_H		115
#define SRST_USB2HOST_ARB	116
#define SRST_USB2HOST_AUX	117
#define SRST_USB2HOST_EHCIPHY	118
#define SRST_USB2HOST_UTMI	119
#define SRST_USB3OTG		120
#define SRST_USBPOR		121
#define SRST_USB2OTG_UTMI	122
#define SRST_USB2HOST_PHY_UTMI	123
#define SRST_USB3OTG_UTMI	124
#define SRST_USB3PHY_U2		125
#define SRST_USB3PHY_U3		126
#define SRST_USB3PHY_PIPE	127

#define SRST_VIO_A		128
#define SRST_VIO_BUS_H		129
#define SRST_VIO_H2P_H		130
#define SRST_VIO_ARBI_H		131
#define SRST_VOP_NIU_A		132
#define SRST_VOP_A		133
#define SRST_VOP_H		134
#define SRST_VOP_D		135
#define SRST_RGA		136
#define SRST_RGA_NIU_A		137
#define SRST_RGA_A		138
#define SRST_RGA_H		139
#define SRST_IEP_A		140
#define SRST_IEP_H		141
#define SRST_HDMI		142
#define SRST_HDMI_P		143

#define SRST_HDCP_A		144
#define SRST_HDCP		145
#define SRST_HDCP_H		146
#define SRST_CIF_A		147
#define SRST_CIF_H		148
#define SRST_CIF_P		149
#define SRST_OTP_P		150
#define SRST_OTP_SBPI		151
#define SRST_OTP_USER		152
#define SRST_DDRCTRL_A		153
#define SRST_DDRSTDY_P		154
#define SRST_DDRSTDY		155
#define SRST_PDM_H		156
#define SRST_PDM		157
#define SRST_USB3PHY_OTG_P	158
#define SRST_USB3PHY_PIPE_P	159

#define SRST_VCODEC_A		160
#define SRST_VCODEC_NIU_A	161
#define SRST_VCODEC_H		162
#define SRST_VCODEC_NIU_H	163
#define SRST_VDEC_A		164
#define SRST_VDEC_NIU_A		165
#define SRST_VDEC_H		166
#define SRST_VDEC_NIU_H		167
#define SRST_VDEC_CORE		168
#define SRST_VDEC_CABAC		169
#define SRST_DDRPHYDIV		175

#define SRST_RKVENC_NIU_A	176
#define SRST_RKVENC_NIU_H	177
#define SRST_RKVENC_H265_A	178
#define SRST_RKVENC_H265_P	179
#define SRST_RKVENC_H265_CORE	180
#define SRST_RKVENC_H265_DSP	181
#define SRST_RKVENC_H264_A	182
#define SRST_RKVENC_H264_H	183
#define SRST_RKVENC_INTMEM	184

#endif
