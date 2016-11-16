/*
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
 * Author: Xiao Feng <xf@rock-chips.com>
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

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RK3366_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RK3366_H

/* core clocks */
#define PLL_APLL		1
#define PLL_DPLL		2
#define PLL_CPLL		3
#define PLL_GPLL		4
#define PLL_NPLL		5
#define PLL_MPLL		6
#define PLL_WPLL		7
#define PLL_BPLL		8
#define ARMCLK			9

/* sclk gates (special clocks) */
#define SCLK_CRYPTO		64
#define SCLK_I2S_8CH_OUT	65
#define SCLK_I2S_8CH		66
#define SCLK_I2S_2CH		67
#define SCLK_SPDIF_8CH		68
#define SCLK_RGA		69
#define SCLK_VOP_FULL_PWM	70
#define SCLK_ISP		71
#define SCLK_HDMI_HDCP		72
#define SCLK_HDMI_CEC		73
#define SCLK_HDCP		75
#define SCLK_PVTM_CORE		76
#define SCLK_PVTM_GPU		77
#define SCLK_SPI0		78
#define SCLK_SPI1		79
#define SCLK_SPI2		80
#define SCLK_SDMMC		81
#define SCLK_SDIO0		82
#define SCLK_SDIO1		83
#define SCLK_EMMC		84
#define SCLK_SDMMC_DRV		85
#define SCLK_SDMMC_SAMPLE	86
#define SCLK_SDIO0_DRV		87
#define SCLK_SDIO0_SAMPLE	88
#define SCLK_SDIO1_DRV		89
#define SCLK_SDIO1_SAMPLE	90
#define SCLK_EMMC_DRV		91
#define SCLK_EMMC_SAMPLE	92
#define SCLK_OTG_PHY0		93
#define SCLK_OTG_PHY1		94
#define SCLK_OTG_ADP		95
#define SCLK_USB3_REF		96
#define SCLK_USB3_SUSPEND	97
#define SCLK_TSADC		98
#define SCLK_SARADC		99
#define SCLK_NANDC0		100
#define SCLK_SFC		101
#define SCLK_UART0		102
#define SCLK_UART1		103
#define SCLK_UART2		104
#define SCLK_UART3		105
#define SCLK_UART4		106
#define SCLK_MAC		107
#define SCLK_MACREF_OUT		108
#define SCLK_MACREF		109
#define SCLK_MAC_RX		110
#define SCLK_MAC_TX		111
#define SCLK_BT_52		112
#define SCLK_BT_M0		113
#define SCLK_WIFIDSP		114
#define SCLK_TIMER0		115
#define SCLK_TIMER1		116
#define SCLK_TIMER2		117
#define SCLK_TIMER3		118
#define SCLK_TIMER4		119
#define SCLK_TIMER5		120
#define SCLK_USBPHY480M		121
#define SCLK_WIFI_WPLL		122
#define SCLK_WIFI_USBPHY480M	123
#define SCLK_MIPIDSI_24M	124
#define SCLK_HEVC_CABAC		125
#define SCLK_HEVC_CORE		126
#define SCLK_VIP_SRC		127
#define SCLK_VIP_OUT		128
#define SCLK_PVTM_PMU		129
#define SCLK_MPLL_SRC		130
#define SCLK_32K_INTR		131
#define SCLK_32K		132
#define SCLK_I2S_8CH_SRC	133
#define SCLK_I2S_2CH_SRC	134
#define SCLK_SPDIF_8CH_SRC	135

#define DCLK_VOP_FULL		170
#define DCLK_VOP_LITE		171
#define DCLK_HDMIPHY		172
#define MCLK_CRYPTO		173

/* aclk gates */
#define ACLK_DMAC_BUS		194
#define ACLK_DFC		195
#define ACLK_GPU		196
#define ACLK_GPU_NOC		197
#define ACLK_USB3		198
#define ACLK_GMAC		199
#define ACLK_DMAC_PERI		200
#define ACLK_VIDEO		201
#define ACLK_RKVDEC		202
#define ACLK_RGA		203
#define ACLK_IEP		204
#define ACLK_VOP_LITE		205
#define ACLK_VOP_FULL		206
#define ACLK_VOP_IEP		207
#define ACLK_ISP		208
#define ACLK_HDCP		209
#define ACLK_BUS		210
#define ACLK_PERI0		211
#define ACLK_PERI1		212

/* pclk gates */
#define PCLK_PMU		322
#define PCLK_SGRF		323
#define PCLK_PMUGRF		324
#define PCLK_GPIO0		325
#define PCLK_GPIO1		326
#define PCLK_GPIO2		327
#define PCLK_GPIO3		328
#define PCLK_GPIO4		329
#define PCLK_GPIO5		330
#define PCLK_GRF		331
#define PCLK_DPHYRX		332
#define PCLK_DPHYTX		333
#define PCLK_TIMER0		334
#define PCLK_DMFIMON		335
#define PCLK_MAILBOX		336
#define PCLK_DFC		337
#define PCLK_DDRUPCTL		338
#define PCLK_DDRPHY		339
#define PCLK_RKPWM		340
#define PCLK_GMAC		341
#define PCLK_SPI0		342
#define PCLK_SPI1		343
#define PCLK_I2C0		344
#define PCLK_I2C1		345
#define PCLK_I2C2		346
#define PCLK_I2C3		347
#define PCLK_I2C4		348
#define PCLK_I2C5		349
#define PCLK_UART0		350
#define PCLK_UART2		351
#define PCLK_UART3		352
#define PCLK_SARADC		353
#define PCLK_TSADC		354
#define PCLK_SIM		355
#define PCLK_HDCP		356
#define PCLK_HDMI_CTRL		357
#define PCLK_VIO_H2P		358
#define PCLK_WDT		359
#define PCLK_BUS		361
#define PCLK_PERI0		362
#define PCLK_PERI1		363
#define PCLK_MIPI_DSI0		364
#define PCLK_ISP		365
#define PCLK_EFUSE_1024		366
#define PCLK_EFUSE_256		367

/* hclk gates */
#define HCLK_I2S_8CH		448
#define HCLK_I2S_2CH		449
#define HCLK_SPDIF		450
#define HCLK_ROM		451
#define HCLK_CRYPTO		452
#define HCLK_OTG		453
#define HCLK_HOST		454
#define HCLK_SDMMC		455
#define HCLK_SDIO		456
#define HCLK_EMMC		457
#define HCLK_NANDC0		458
#define HCLK_SFC		459
#define HCLK_VIDEO		460
#define HCLK_RKVDEC		461
#define HCLK_ISP		462
#define HCLK_RGA		463
#define HCLK_IEP		464
#define HCLK_VOP_FULL		465
#define HCLK_VOP_LITE		466
#define HCLK_VIO_AHB_ARBITER	467
#define HCLK_VIO_NOC		468
#define HCLK_VIO_H2P		469
#define HCLK_VIO_HDCPMMU	470
#define HCLK_BUS		471
#define HCLK_PERI0		472
#define HCLK_PERI1		473

#define CLK_NR_CLKS		(HCLK_PERI1 + 1)

/* soft-reset indices */

/* cru_softrst0_con */
#define SRST_CORE0		0
#define SRST_CORE1		1
#define SRST_CORE2		2
#define SRST_CORE3		3
#define SRST_CORE0_PO		4
#define SRST_CORE1_PO		5
#define SRST_CORE2_PO		6
#define SRST_CORE3_PO		7
#define SRST_SOCDBG		14
#define SRST_CORE_DBG		15

/* cru_softrst1_con */
#define SRST_DCF_AXI		16
#define SRST_DCF_APB		17
#define SRST_DMAC1		18
#define SRST_INTMEM		19
#define SRST_ROM		20
#define SRST_SPDIF8CH		21
#define SRST_I2S8CH		23
#define SRST_MAILBOX		24
#define SRST_I2S2CH		25
#define SRST_EFUSE_256		26
#define SRST_MCU_SYS		28
#define SRST_MCU_PO		29
#define SRST_MCU_NOC		30
#define SRST_EFUSE		31

/* cru_softrst2_con */
#define SRST_GPIO0		32
#define SRST_GPIO1		33
#define SRST_GPIO2		34
#define SRST_GPIO3		35
#define SRST_GPIO4		36
#define SRST_GPIO5		37
#define SRST_PMUGRF		41
#define SRST_I2C0		42
#define SRST_I2C1		43
#define SRST_I2C2		44
#define SRST_I2C3		45
#define SRST_I2C4		46
#define SRST_I2C5		47

/* cru_softrst3_con */
#define SRST_DWPWM		48
#define SRST_PERIPH1_AXI	50
#define SRST_PERIPH1_AHB	51
#define SRST_PERIPH1_APB	52
#define SRST_PERIPH1_NIU	53
#define SRST_PERI_AHB_ARBI1	54
#define SRST_GRF		55
#define SRST_PMU		56
#define SRST_PERIPH0_AXI	57
#define SRST_PERIPH0_AHB	58
#define SRST_PERIPH0_APB	59
#define SRST_PERIPH0_NIU	60
#define SRST_PDPERI_AHB_ARBI0	61
#define SRST_USBHOST0_ARBI	62

/* cru_softrst4_con */
#define SRST_DMAC2		64
#define SRST_MAC		66
#define SRST_USB3		67
#define SRST_USB3PHY		68
#define SRST_RKPWM		69
#define SRST_USBHOST0		72
#define SRST_HSADC		76
#define SRST_NANDC0		77
#define SRST_SFC		79

/* cru_softrst5_con */
#define SRST_TZPC		80
#define SRST_SPI0		83
#define SRST_SPI1		84
#define SRST_SARADC		87
#define SRST_PDALIVE_NIU	88
#define SRST_PDPMU_INTMEM	89
#define SRST_PDPMU_NIU		90
#define SRST_SGRF		91
#define SRST_VOP1_AXI		93
#define SRST_VOP1_AHB		94
#define SRST_VOP1_DCLK		95

/* cru_softrst6_con */
#define SRST_VIO_ARBI		96
#define SRST_RGA_NIU		97
#define SRST_VIO0_NIU_AXI	98
#define SRST_VIO_NIU_AHB	99
#define SRST_VOP0_AXI		100
#define SRST_VOP0_AHB		101
#define SRST_VOP0_DCLK		102
#define SRST_HDCP_NIU		103
#define SRST_VIP		104
#define SRST_RGA_CORE		105
#define SRST_IEP_AXI		106
#define SRST_IEP_AHB		107
#define SRST_RGA_AXI		108
#define SRST_RGA_AHB		109
#define SRST_ISP		110

/* cru_softrst7_con */
#define SRST_VIDEO_AXI		112
#define SRST_VIDEO_AHB		113
#define SRST_MIPIDPHYTX		114
#define SRST_MIPIDSI0		115
#define SRST_MIPIDPHYRX		116
#define SRST_MIPICSI		117
#define SRST_LVDS_CON		119
#define SRST_GPU		120
#define SRST_HDMI		121
#define SRST_RGA_H2P		122
#define SRST_PMU_PVTM		123
#define SRST_CORE_PVTM		124
#define SRST_GPU_PVTM		125
#define SRST_GPU_NOC		126

/* cru_softrst8_con */
#define SRST_MMC0		128
#define SRST_SDIO0		129
#define SRST_EMMC		131
#define SRST_USBOTG_AHB		132
#define SRST_USBOTG_PHY		133
#define SRST_USBOTG_CON		134
#define SRST_USBHOST0_AHB	135
#define SRST_USBHPHY1		136
#define SRST_USBHOST0_CON	137
#define SRST_USBOTG_UTMI	138
#define SRST_USBHOST0_UTMI	139
#define SRST_USB_ADP		141
#define SRST_TSADC		142

/* cru_softrst9_con */
#define SRST_CORESIGHT		144
#define SRST_PD_CORE_AHB_NOC	145
#define SRST_PD_CORE_APB_NOC	146
#define SRST_RKVDEC_NIU_AHB	147
#define SRST_GIC		148
#define SRST_LCDC_PWM0		149
#define SRST_RKVDEC		150
#define SRST_RKVDEC_NIU		151
#define SRST_RKVDEC_AHB		152
#define SRST_RKVDEC_CABAC	154
#define SRST_RKVDEC_CORE	155
#define SRST_GPU_CFG_NIU	157
#define SRST_DFIMON		158
#define SRST_TSADC_APB		159

/* cru_softrst10_con */
#define SRST_DDRPHY0		160
#define SRST_DDRPHY0_APB	161
#define SRST_DDRCTRL0		162
#define SRST_DDRCTRL0_APB	163
#define SRST_DDRPHY0_CTL	164
#define SRST_VIDEO_NIU		165
#define SRST_VIDEO_NIU_AHB	167
#define SRST_DDRMSCH0		170
#define SRST_PDBUS_AHB		173
#define SRST_CRYPTO		174
#define SRST_DDR_NOC		175

/* cru_softrst11_con */
#define SRST_PSPVTM_TOP		176
#define SRST_PSPVTM_CORE	177
#define SRST_PSPVTM_GPU		178
#define SRST_UART0		179
#define SRST_UART1		180
#define SRST_UART2		181
#define SRST_UART3		182
#define SRST_UART4		183
#define SRST_PSPVTM_VIDEO	184
#define SRST_PSPVTM_VIO		185
#define SRST_SIMC		186
#define SRST_PSPVTM_PERI	187

/* cru_softrst12_con */
#define SRST_WIFI_MAC_CORE	192
#define SRST_WIFI_MAC_WT	193
#define SRST_WIFI_MPIF		194
#define SRST_WIFI_EXT		195
#define SRST_WIFI_AHB		196
#define SRST_WIFI_DSP		197
#define SRST_BT_FAST_AHB	198
#define SRST_BT_SLOW_AHB	199
#define SRST_BT_SLOW_APB	200
#define SRST_BT_MODEM		201
#define SRST_BT_MCU		202
#define SRST_BT_DM		203
#define SRST_WIFI_LP		204
#define SRST_BT_LP		205
#define SRST_BT_MCU_SYS		206
#define SRST_WIFI_DSP_ORSTN	207

/* cru_softrst13_con */
#define SRST_CORE0_WFI		208
#define SRST_CORE0_PO_WFI	209
#define SRST_CORE_L2		210
#define SRST_PD_CORE_NIU	212
#define SRST_PDBUS_STRSYS	213
#define SRST_TRACE		222

/* cru_softrst14_con */
#define SRST_TIMER00		224
#define SRST_TIMER01		225
#define SRST_TIMER02		226
#define SRST_TIMER03		227
#define SRST_TIMER04		228
#define SRST_TIMER05		229
#define SRST_TIMER10		230
#define SRST_TIMER11		231
#define SRST_TIMER12		232
#define SRST_TIMER13		233
#define SRST_TIMER14		234
#define SRST_TIMER15		235
#define SRST_TIMER0_APB		236
#define SRST_TIMER1_APB		237

#endif
