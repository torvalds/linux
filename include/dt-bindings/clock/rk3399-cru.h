/*
 * Copyright (c) 2015 Rockchip Electronics Co. Ltd.
 * Author: Xing Zheng <zhengxing@rock-chips.com>
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

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RK3399_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RK3399_H

/* core clocks */
#define PLL_APLLL		1
#define PLL_APLLB		2
#define PLL_DPLL		3
#define PLL_CPLL		4
#define PLL_GPLL		5
#define PLL_NPLL		6
#define PLL_VPLL		7
#define PLL_PPLL		8
#define ARMCLKL			9
#define ARMCLKB			10

/* sclk gates (special clocks) */
#define SCLK_GPU_CORE		64
#define SCLK_SPI0		65
#define SCLK_SPI1		66
#define SCLK_SPI2		68
#define SCLK_SPI3		69
#define SCLK_SPI4		70
#define SCLK_SDMMC		71
#define SCLK_SDIO0		72
#define SCLK_EMMC		73
#define SCLK_TSADC		74
#define SCLK_SARADC		75
#define SCLK_NANDC0		76
#define SCLK_UART0		77
#define SCLK_UART1		78
#define SCLK_UART2		79
#define SCLK_UART3		80
#define SCLK_UART4		81
#define SCLK_SPDIF_8CH		82
#define SCLK_I2S0_8CH		83
#define SCLK_I2S1_8CH		84
#define SCLK_I2S2_8CH		85
#define SCLK_TIMER00		86
#define SCLK_TIMER01		87
#define SCLK_TIMER02		88
#define SCLK_TIMER03		89
#define SCLK_TIMER04		90
#define SCLK_TIMER05		91
#define SCLK_TIMER06		92
#define SCLK_TIMER07		93
#define SCLK_TIMER08		94
#define SCLK_TIMER09		95
#define SCLK_TIMER10		96
#define SCLK_TIMER11		97
#define SCLK_HSICPHY480M	98
#define SCLK_HSICPHY12M		99
#define SCLK_MACREF		100
#define SCLK_VOPB_PWM		101
#define SCLK_VOPL_PWM		102
#define SCLK_EDP_24M		104
#define SCLK_EDP		105
#define SCLK_RGA		106
#define SCLK_ISP		107
#define SCLK_HDCP		108
#define SCLK_HDMI_HDCP		109
#define SCLK_HDMI_CEC		110
#define SCLK_HEVC_CABAC		111
#define SCLK_HEVC_CORE		112
#define SCLK_I2S_8CH_OUT	113
#define SCLK_SDMMC_DRV		114
#define SCLK_SDIO0_DRV		115
#define SCLK_SDIO1_DRV		116
#define SCLK_EMMC_DRV		117
#define SCLK_SDMMC_SAMPLE	118
#define SCLK_SDIO0_SAMPLE	119
#define SCLK_SDIO1_SAMPLE	120
#define SCLK_EMMC_SAMPLE	121
#define SCLK_USBPHY480M		122
#define SCLK_PVTM_CORE_L	123
#define SCLK_PVTM_CORE_B	124
#define SCLK_PVTM_GPU		125
#define SCLK_PVTM_PMU		126
#define SCLK_SFC		127
#define SCLK_MAC_RX		128
#define SCLK_MAC_TX		129
#define SCLK_MAC		130
#define SCLK_MACREF_OUT		131
#define SCLK_USB2_PHY0_REF	132
#define SCLK_USB2_PHY1_REF	133
#define SCLK_USB3_OTG0_REF	134
#define SCLK_USB3_OTG1_REF	135
#define SCLK_USB3_OTG0_SUSPEND	136
#define SCLK_USB3_OTG1_SUSPEND	137
#define SCLK_CRYPTO		138
#define SCLK_CRYPTO1		139

#define DCLK_VOPB		170
#define DCLK_VOPL		171
#define MCLK_CRYPTO		172
#define MCLK_CRYPTO1		173

/* aclk gates */
#define ACLK_GPU_MEM		192
#define ACLK_GPU_CFG		193
#define ACLK_DMAC_BUS		194
#define ACLK_DMAC_PERI		195
#define ACLK_PERI_MMU		196
#define ACLK_GMAC		197
#define ACLK_VOPB		198
#define ACLK_VOPL		199
#define ACLK_RGA		200
#define ACLK_HDCP		201
#define ACLK_IEP		202
#define ACLK_VIO0_NOC		203
#define ACLK_VIP		204
#define ACLK_ISP		205
#define ACLK_VIO1_NOC		206
#define ACLK_VIDEO		208
#define ACLK_BUS		209
#define ACLK_PERI		210
#define ACLK_EMMC_GPLL		211
#define ACLK_EMMC_CPLL		212
#define ACLK_EMMC_CORE		213
#define ACLK_EMMC_NIU 		214
#define ACLK_EMMC_GRF 		215
#define ACLK_USB3_OTG0		216
#define ACLK_USB3_OTG1		217
#define ACLK_USB3_GRF 		218

/* pclk gates */
#define PCLK_GPIO0		320
#define PCLK_GPIO1		321
#define PCLK_GPIO2		322
#define PCLK_GPIO3		323
#define PCLK_GPIO4		324
#define PCLK_PMUGRF		325
#define PCLK_MAILBOX		326
#define PCLK_GRF		329
#define PCLK_SGRF		330
#define PCLK_PMU		331
#define PCLK_I2C0		332
#define PCLK_I2C1		333
#define PCLK_I2C2		334
#define PCLK_I2C3		335
#define PCLK_I2C4		336
#define PCLK_I2C5		337
#define PCLK_SPI0		338
#define PCLK_SPI1		339
#define PCLK_SPI2		340
#define PCLK_UART0		341
#define PCLK_UART1		342
#define PCLK_UART2		343
#define PCLK_UART3		344
#define PCLK_UART4		345
#define PCLK_TSADC		346
#define PCLK_SARADC		347
#define PCLK_SIM		348
#define PCLK_GMAC		349
#define PCLK_PWM0		350
#define PCLK_PWM1		351
#define PCLK_TIMER0		353
#define PCLK_TIMER1		354
#define PCLK_EDP_CTRL		355
#define PCLK_MIPI_DSI0		356
#define PCLK_MIPI_CSI		358
#define PCLK_HDCP		359
#define PCLK_HDMI_CTRL		360
#define PCLK_VIO_H2P		361
#define PCLK_BUS		362
#define PCLK_PERI		363
#define PCLK_DDRUPCTL		364
#define PCLK_DDRPHY		365
#define PCLK_ISP		366
#define PCLK_VIP		367
#define PCLK_WDT		368

/* hclk gates */
#define HCLK_SFC		448
#define HCLK_HOST0		450
#define HCLK_HOST1		451
#define HCLK_HSIC		452
#define HCLK_NANDC0		453
#define HCLK_TSP		455
#define HCLK_SDMMC		456
#define HCLK_SDIO0		457
#define HCLK_EMMC		459
#define HCLK_HSADC		460
#define HCLK_CRYPTO		461
#define HCLK_I2S0_8CH		462
#define HCLK_I2S1_8CH		463
#define HCLK_I2S2_8CH		464
#define HCLK_SPDIF		465
#define HCLK_VOPB		466
#define HCLK_VOPL		467
#define HCLK_ROM		468
#define HCLK_IEP		469
#define HCLK_ISP		470
#define HCLK_RGA		471
#define HCLK_VIO_AHB_ARBI	472
#define HCLK_VIO_NOC		473
#define HCLK_VIP		474
#define HCLK_VIO_H2P		475
#define HCLK_VIO_HDCPMMU	476
#define HCLK_VIDEO		477
#define HCLK_BUS		478
#define HCLK_PERI		479

/* pmu-clocks indices */
#define SCLK_CM0S		500
#define SCLK_SPI5		501
#define SCLK_TIMER12		502
#define SCLK_TIMER13		503
#define SCLK_UART		504

#define CLK_NR_CLKS		(SCLK_UART + 1)

/* soft-reset indices */

/* cru_softrst_con0 */
#define SRST_CORE_L0			0
#define SRST_CORE_B0			1
#define SRST_CORE_PO_L0			2
#define SRST_CORE_PO_B0			3
#define SRST_L2_L			4
#define SRST_L2_B			5
#define SRST_ADB_L			6
#define SRST_ADB_B			7
#define SRST_A_CCI			8
#define SRST_A_CCIM0_NIU		9
#define SRST_A_CCIM1_NIU		10
#define SRST_DBG_NIU			11

/* cru_softrst_con1 */
#define SRST_CORE_L0_T			16
#define SRST_CORE_L1			17
#define SRST_CORE_L2			18
#define SRST_CORE_L3			19
#define SRST_CORE_PO_L0_T		20
#define SRST_CORE_PO_L1			21
#define SRST_CORE_PO_L2			22
#define SRST_CORE_PO_L3			23
#define SRST_A_ADB400_GIC2_CORE_L	24
#define SRST_A_ADB400_CORE_L_GIC2	25
#define SRST_P_DBG_L			26
#define SRST_SOC_DBG_L			27
#define SRST_L2_L_T			28
#define SRST_ADB_L_T			29
#define SRST_A_RKREF_L			30
#define SRST_PVTM_CORE_L		31

/* cru_softrst_con2 */
#define SRST_CORE_B0_T			32
#define SRST_CORE_B1			33
#define SRST_CORE_PO_B0_T		36
#define SRST_CORE_PO_B1			37
#define SRST_A_ADB400_GIC2_CORE_B	40
#define SRST_A_ADB400_CORE_B_GIC2	41
#define SRST_P_DBG_B			42
#define SRST_L2_B_T			43
#define SRST_SOC_DBG_B			44
#define SRST_ADB_B_T			45
#define SRST_A_RKREF_B			46
#define SRST_PVTM_CORE_B		47

/* cru_softrst_con3 */
#define SRST_A_CCI_T			50
#define SRST_A_CCIM0_NIU_T		51
#define SRST_A_CCIM1_NIU_T		52
#define SRST_A_ADB400M_PD_CORE_B_T	53
#define SRST_A_ADB400M_PD_CORE_L_T	54
#define SRST_DBG_NIU_T			55
#define SRST_DBG_CXCS			56
#define SRST_CCI_TRACE			57
#define SRST_P_CCI_GRF			58

/* cru_softrst_con4 */
#define SRST_A_CENTER_MAIN_NIU		64
#define SRST_A_CENTER_PERI_NIU		65
#define SRST_P_CENTER_MAIN		66
#define SRST_P_DDRMAON			67
#define SRST_P_CIC			68
#define SRST_P_CENTER_SGRF		69
#define SRST_DDR0_MSCH			70
#define SRST_DDRCFG0_MSCH		71
#define SRST_DDR0			72
#define SRST_DDRPHY0			73
#define SRST_DDR1_MSCH			74
#define SRST_DDRCFG1_MSCH		75
#define SRST_DDR1			76
#define SRST_DDRPHY1			77
#define SRST_DDR_CIC			78
#define SRST_PVTM_DDR			79

/* cru_softrst_con5 */
#define SRST_A_VCODEC_NIU		80
#define SRST_A_VCODEC			81
#define SRST_H_VCODEC_NIU		82
#define SRST_H_VCODEC			83
#define SRST_A_VDU_NIU			88
#define SRST_A_VDU 			89
#define SRST_H_VDU_NIU			90
#define SRST_H_VDU			91
#define SRST_VDU_CORE			92
#define SRST_VDU_CA			93

/* cru_softrst_con6 */
#define SRST_A_IEP_NIU			96
#define SRST_A_VOP_IEP			97
#define SRST_A_IEP			98
#define SRST_H_IEP_NIU			99
#define SRST_H_IEP			100
#define SRST_A_RGA_NIU			102
#define SRST_A_RGA			103
#define SRST_H_RGA_NIU			104
#define SRST_H_RGA			105
#define SRST_RGA_CORE			106
#define SRST_EMMC_NIU			108
#define SRST_EMMC			109
#define SRST_EMMC_GRF			110
#define SRST_EMMCPHY_SYSRX		111

/* cru_softrst_con7 */
#define SRST_A_PERIHP_NIU		112
#define SRST_A_PERIHP_GRF		113
#define SRST_H_PERIHP_NIU		114
#define SRST_USBHOST0 			115
#define SRST_HOSTC0_AUX			116
#define SRST_HOSTC0_ARB			117
#define SRST_USBHOST1			118
#define SRST_HOSTC1_AUX			119
#define SRST_HOSTC1_ARB			120
#define SRST_SDIO0			121
#define SRST_SDMMC			122
#define SRST_HSIC			123
#define SRST_HSIC_AUX			124
#define SRST_AHB1TOM			125
#define SRST_P_PERIHP_NIU		126
#define SRST_HSICPHY			127

/* cru_softrst_con8 */
#define SRST_A_PCIE			128
#define SRST_P_PCIE			129
#define SRST_PCIE_CORE			130
#define SRST_PCIE_MGMT			131
#define SRST_PCIE_MGMT_STICKY		132
#define SRST_PCIE_PIPE			133
#define SRST_PCIE_PM			134
#define SRST_PCIEPHY			135
#define SRST_A_GMAC_NIU			136
#define SRST_A_GMAC 			137
#define SRST_P_GMAC_NIU			138
#define SRST_P_GMAC			139
#define SRST_P_GMAC_GRF			140
#define SRST_HSICPHY_POR		142
#define SRST_HSICPHY_UTMI		143

/* cru_softrst_con9 */
#define SRST_USB2PHY0_POR		144
#define SRST_USB2PHY0_UTMI_PORT0	145
#define SRST_USB2PHY0_UTMI_PORT1	146
#define SRST_USB2PHY0_EHCIPHY		147
#define SRST_UPHY0_PIPE_L00		148
#define SRST_UPHY0			149
#define SRST_UPHY0_TCPDPWRUP		150
#define SRST_USB2PHY1_POR		152
#define SRST_USB2PHY1_UTMI_PORT0	153
#define SRST_USB2PHY1_UTMI_PORT1	154
#define SRST_USB2PHY1_EHCIPHY		155
#define SRST_UPHY1_PIPE_L00		156
#define SRST_UPHY1			157
#define SRST_UPHY1_TCPDPWRUP		158

/* cru_softrst_con10 */
#define SRST_A_PERILP0_NIU		160
#define SRST_A_DCF			161
#define SRST_GIC500			162
#define SRST_DMAC0_PERILP0		163
#define SRST_DMAC1_PERILP0		164
#define SRST_TZMA			165
#define SRST_INTMEM			166
#define SRST_ADB400_MST0		167
#define SRST_ADB400_MST1		168
#define SRST_ADB400_SLV0		169
#define SRST_ADB400_SLV1		170
#define SRST_H_PERILP0			171
#define SRST_H_PERILP0_NIU		172
#define SRST_ROM			173
#define SRST_CRYPTO_S			174
#define SRST_CRYPTO_M			175

/* cru_softrst_con11 */
#define SRST_P_DCF			176
#define SRST_CM0S_NIU			177
#define SRST_CM0S			178
#define SRST_CM0S_DBG			179
#define SRST_CM0S_PO			180
#define SRST_CRYPTO			181
#define SRST_P_PERILP1_SGRF		182
#define SRST_P_PERILP1_GRF		183
#define SRST_CRYPTO1_S			184
#define SRST_CRYPTO1_M			185
#define SRST_CRYPTO1			186
#define SRST_GIC_NIU			188
#define SRST_SD_NIU			189
#define SRST_SDIOAUDIO_BRG		190

/* cru_softrst_con12 */
#define SRST_H_PERILP1			192
#define SRST_H_PERILP1_NIU		193
#define SRST_H_I2S0_8CH			194
#define SRST_H_I2S1_8CH			195
#define SRST_H_I2S2_8CH			196
#define SRST_H_SPDIF_8CH		197
#define SRST_P_PERILP1_NIU		198
#define SRST_P_EFUSE_1024		199
#define SRST_P_EFUSE_1024S		200
#define SRST_P_I2C0			201
#define SRST_P_I2C1			202
#define SRST_P_I2C2			203
#define SRST_P_I2C3			204
#define SRST_P_I2C4			205
#define SRST_P_I2C5			206
#define SRST_P_MAILBOX0			207

/* cru_softrst_con13 */
#define SRST_P_UART0			208
#define SRST_P_UART1			209
#define SRST_P_UART2			210
#define SRST_P_UART3			211
#define SRST_P_SARADC			212
#define SRST_P_TSADC			213
#define SRST_P_SPI0			214
#define SRST_P_SPI1			215
#define SRST_P_SPI2			216
#define SRST_P_SPI3			217
#define SRST_P_SPI4			218
#define SRST_SPI0			219
#define SRST_SPI1			220
#define SRST_SPI2			221
#define SRST_SPI3			222
#define SRST_SPI4			223

/* cru_softrst_con14 */
#define SRST_I2S0_8CH			224
#define SRST_I2S1_8CH			225
#define SRST_I2S2_8CH			226
#define SRST_SPDIF_8CH			227
#define SRST_UART0			228
#define SRST_UART1			229
#define SRST_UART2			230
#define SRST_UART3			231
#define SRST_TSADC			232
#define SRST_I2C0			233
#define SRST_I2C1			234
#define SRST_I2C2			235
#define SRST_I2C3			236
#define SRST_I2C4			237
#define SRST_I2C5			238
#define SRST_SDIOAUDIO_NIU		239

/* cru_softrst_con15 */
#define SRST_A_VIO_NIU			240
#define SRST_A_HDCP_NIU			241
#define SRST_A_HDCP			242
#define SRST_H_HDCP_NIU			243
#define SRST_H_HDCP			244
#define SRST_P_HDCP_NIU			245
#define SRST_P_HDCP			246
#define SRST_P_HDMI_CTRL		247
#define SRST_P_DP_CTRL			248
#define SRST_S_DP_CTRL			249
#define SRST_C_DP_CTRL			250
#define SRST_P_MIPI_DSI0		251
#define SRST_P_MIPI_DSI1		252
#define SRST_DP_CORE			253
#define SRST_DP_I2S			254
#define SRST_DP_VIF			255

/* cru_softrst_con16 */
#define SRST_GASKET			256
#define SRST_VIO_SGRF			257
#define SRST_VIO_GRF			258
#define SRST_DPTX_SPDIF_REC		259
#define SRST_HDMI_CTRL			260
#define SRST_HDCP_CTRL			261
#define SRST_A_ISP0_NIU			262
#define SRST_A_ISP1_NIU			263
#define SRST_A_ISP0			264
#define SRST_A_ISP1			265
#define SRST_H_ISP0_NIU			266
#define SRST_H_ISP1_NIU			267
#define SRST_H_ISP0			268
#define SRST_H_ISP1			269
#define SRST_ISP0			270
#define SRST_ISP1			271

/* cru_softrst_con17 */
#define SRST_A_VOP0_NIU			272
#define SRST_A_VOP1_NIU			273
#define SRST_A_VOP0			274
#define SRST_A_VOP1			275
#define SRST_H_VOP0_NIU			276
#define SRST_H_VOP1_NIU			277
#define SRST_H_VOP0			278
#define SRST_H_VOP1			279
#define SRST_D_VOP0			280
#define SRST_D_VOP1			281
#define SRST_VOP0_PWM			282
#define SRST_VOP1_PWM			283
#define SRST_P_EDP_NIU			284
#define SRST_P_EDP_CTRL			285

/* cru_softrst_con18 */
#define SRST_A_GPU			288
#define SRST_A_GPU_NIU			289
#define SRST_A_GPU_GRF			290
#define SRST_PVTM_GPU			291
#define SRST_A_USB3_NIU			292
#define SRST_A_USB3_OTG0		293
#define SRST_A_USB3_OTG1		294
#define SRST_A_USB3_GRF			295
#define SRST_PMU			296
#define SRST_PVTM_PMU			297

/* cru_softrst_con19 */
#define SRST_P_TIMER0_5			304
#define SRST_TIMER0			305
#define SRST_TIMER1			306
#define SRST_TIMER2			307
#define SRST_TIMER3			308
#define SRST_TIMER4			309
#define SRST_TIME5			310
#define SRST_P_TIMER6_11		311
#define SRST_TIMER6			312
#define SRST_TIMER7			313
#define SRST_TIMER8			314
#define SRST_TIMER9			315
#define SRST_TIMER10			316
#define SRST_TIMER11			317
#define SRST_P_INTR_ARB_PMU		318
#define SRST_P_ALIVE_SGRF		319

/* cru_softrst_con20 */
#define SRST_P_GPIO2			320
#define SRST_P_GPIO3			321
#define SRST_P_GPIO4			322
#define SRST_P_GRF			323
#define SRST_P_ALIVE_NIU		324
#define SRST_P_WDT0			325
#define SRST_P_WDT1			326
#define SRST_P_INTR_ARB 		327
#define SRST_P_UPHY0_DPTX		328
#define SRST_P_UPHY1_DPTX		329
#define SRST_P_UPHY0_APB		330
#define SRST_P_UPHY1_APB		331
#define SRST_P_UPHY0_TCPHY		332
#define SRST_P_UPHY1_TCPHY		333
#define SRST_P_UPHY0_TCPDCTRL		334
#define SRST_P_UPHY1_TCPDCTRL		335

/* pmu soft-reset indices */

/* pmu_cru_softrst_con0 */
#define SRST_P_NIU			0
#define SRST_P_INTMEN			1
#define SRST_H_CM0S			2
#define SRST_H_CM0S_NIU			3
#define SRST_DBG_CM0S			4
#define SRST_PO_CM0S			5
#define SRST_P_SPI6			6
#define SRST_SPI6			7
#define SRST_P_TIMER_0_1		8
#define SRST_P_TIMER_0			9
#define SRST_P_TIMER_1			10
#define SRST_P_UART4			11
#define SRST_UART4			12
#define SRST_P_WDT			13

/* pmu_cru_softrst_con1 */
#define SRST_P_I2C6			16
#define SRST_P_I2C7			17
#define SRST_P_I2C8			18
#define SRST_P_MAILBOX			19
#define SRST_P_RKPWM			20
#define SRST_P_PMUGRF			21
#define SRST_P_SGRF			22
#define SRST_P_GPIO0			23
#define SRST_P_GPIO1			24
#define SRST_P_CRU			25
#define SRST_P_INTR			26
#define SRST_PVTM 			27
#define SRST_I2C6			28
#define SRST_I2C7			29
#define SRST_I2C8			30

#endif
