/*
 * Copyright (c) 2015 Heiko Stuebner <heiko@sntech.de>
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

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RK3368_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RK3368_H

/* core clocks */
#define PLL_APLLB		1
#define PLL_APLLL		2
#define PLL_DPLL		3
#define PLL_CPLL		4
#define PLL_GPLL		5
#define PLL_NPLL		6
#define ARMCLKB			7
#define ARMCLKL			8

/* sclk gates (special clocks) */
#define SCLK_GPU_CORE		64
#define SCLK_SPI0		65
#define SCLK_SPI1		66
#define SCLK_SPI2		67
#define SCLK_SDMMC		68
#define SCLK_SDIO0		69
#define SCLK_EMMC		71
#define SCLK_TSADC		72
#define SCLK_SARADC		73
#define SCLK_NANDC0		75
#define SCLK_UART0		77
#define SCLK_UART1		78
#define SCLK_UART2		79
#define SCLK_UART3		80
#define SCLK_UART4		81
#define SCLK_I2S_8CH		82
#define SCLK_SPDIF_8CH		83
#define SCLK_I2S_2CH		84
#define SCLK_TIMER00		85
#define SCLK_TIMER01		86
#define SCLK_TIMER02		87
#define SCLK_TIMER03		88
#define SCLK_TIMER04		89
#define SCLK_TIMER05		90
#define SCLK_OTGPHY0		93
#define SCLK_OTG_ADP		96
#define SCLK_HSICPHY480M	97
#define SCLK_HSICPHY12M		98
#define SCLK_MACREF		99
#define SCLK_VOP0_PWM		100
#define SCLK_MAC_RX		102
#define SCLK_MAC_TX		103
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
#define SCLK_EMMC_DRV		117
#define SCLK_SDMMC_SAMPLE	118
#define SCLK_SDIO0_SAMPLE	119
#define SCLK_EMMC_SAMPLE	121
#define SCLK_USBPHY480M		122
#define SCLK_PVTM_CORE		123
#define SCLK_PVTM_GPU		124
#define SCLK_PVTM_PMU		125
#define SCLK_SFC		126
#define SCLK_MAC		127
#define SCLK_MACREF_OUT		128
#define SCLK_TIMER10		133
#define SCLK_TIMER11		134
#define SCLK_TIMER12		135
#define SCLK_TIMER13		136
#define SCLK_TIMER14		137
#define SCLK_TIMER15		138

#define DCLK_VOP		190
#define MCLK_CRYPTO		191

/* aclk gates */
#define ACLK_GPU_MEM		192
#define ACLK_GPU_CFG		193
#define ACLK_DMAC_BUS		194
#define ACLK_DMAC_PERI		195
#define ACLK_PERI_MMU		196
#define ACLK_GMAC		197
#define ACLK_VOP		198
#define ACLK_VOP_IEP		199
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

/* pclk gates */
#define PCLK_GPIO0		320
#define PCLK_GPIO1		321
#define PCLK_GPIO2		322
#define PCLK_GPIO3		323
#define PCLK_PMUGRF		324
#define PCLK_MAILBOX		325
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
#define PCLK_EFUSE256		369

/* hclk gates */
#define HCLK_SFC		448
#define HCLK_OTG0		449
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
#define HCLK_I2S_2CH		462
#define HCLK_I2S_8CH		463
#define HCLK_SPDIF		464
#define HCLK_VOP		465
#define HCLK_ROM		467
#define HCLK_IEP		468
#define HCLK_ISP		469
#define HCLK_RGA		470
#define HCLK_VIO_AHB_ARBI	471
#define HCLK_VIO_NOC		472
#define HCLK_VIP		473
#define HCLK_VIO_H2P		474
#define HCLK_VIO_HDCPMMU	475
#define HCLK_VIDEO		476
#define HCLK_BUS		477
#define HCLK_PERI		478

#define CLK_NR_CLKS		(HCLK_PERI + 1)

/* soft-reset indices */
#define SRST_CORE_B0		0
#define SRST_CORE_B1		1
#define SRST_CORE_B2		2
#define SRST_CORE_B3		3
#define SRST_CORE_B0_PO		4
#define SRST_CORE_B1_PO		5
#define SRST_CORE_B2_PO		6
#define SRST_CORE_B3_PO		7
#define SRST_L2_B		8
#define SRST_ADB_B		9
#define SRST_PD_CORE_B_NIU	10
#define SRST_PDBUS_STRSYS	11
#define SRST_SOCDBG_B		14
#define SRST_CORE_B_DBG		15

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

#define SRST_GPIO0		32
#define SRST_GPIO1		33
#define SRST_GPIO2		34
#define SRST_GPIO3		35
#define SRST_GPIO4		36
#define SRST_PMUGRF		41
#define SRST_I2C0		42
#define SRST_I2C1		43
#define SRST_I2C2		44
#define SRST_I2C3		45
#define SRST_I2C4		46
#define SRST_I2C5		47

#define SRST_DWPWM		48
#define SRST_MMC_PERI		49
#define SRST_PERIPH_MMU		50
#define SRST_GRF		55
#define SRST_PMU		56
#define SRST_PERIPH_AXI		57
#define SRST_PERIPH_AHB		58
#define SRST_PERIPH_APB		59
#define SRST_PERIPH_NIU		60
#define SRST_PDPERI_AHB_ARBI	61
#define SRST_EMEM		62
#define SRST_USB_PERI		63

#define SRST_DMAC2		64
#define SRST_MAC		66
#define SRST_GPS		67
#define SRST_RKPWM		69
#define SRST_USBHOST0		72
#define SRST_HSIC		73
#define SRST_HSIC_AUX		74
#define SRST_HSIC_PHY		75
#define SRST_HSADC		76
#define SRST_NANDC0		77
#define SRST_SFC		79

#define SRST_SPI0		83
#define SRST_SPI1		84
#define SRST_SPI2		85
#define SRST_SARADC		87
#define SRST_PDALIVE_NIU	88
#define SRST_PDPMU_INTMEM	89
#define SRST_PDPMU_NIU		90
#define SRST_SGRF		91

#define SRST_VIO_ARBI		96
#define SRST_RGA_NIU		97
#define SRST_VIO0_NIU_AXI	98
#define SRST_VIO_NIU_AHB	99
#define SRST_LCDC0_AXI		100
#define SRST_LCDC0_AHB		101
#define SRST_LCDC0_DCLK		102
#define SRST_VIP		104
#define SRST_RGA_CORE		105
#define SRST_IEP_AXI		106
#define SRST_IEP_AHB		107
#define SRST_RGA_AXI		108
#define SRST_RGA_AHB		109
#define SRST_ISP		110
#define SRST_EDP_24M		111

#define SRST_VIDEO_AXI		112
#define SRST_VIDEO_AHB		113
#define SRST_MIPIDPHYTX		114
#define SRST_MIPIDSI0		115
#define SRST_MIPIDPHYRX		116
#define SRST_MIPICSI		117
#define SRST_GPU		120
#define SRST_HDMI		121
#define SRST_EDP		122
#define SRST_PMU_PVTM		123
#define SRST_CORE_PVTM		124
#define SRST_GPU_PVTM		125
#define SRST_GPU_SYS		126
#define SRST_GPU_MEM_NIU	127

#define SRST_MMC0		128
#define SRST_SDIO0		129
#define SRST_EMMC		131
#define SRST_USBOTG_AHB		132
#define SRST_USBOTG_PHY		133
#define SRST_USBOTG_CON		134
#define SRST_USBHOST0_AHB	135
#define SRST_USBHOST0_PHY	136
#define SRST_USBHOST0_CON	137
#define SRST_USBOTG_UTMI	138
#define SRST_USBHOST1_UTMI	139
#define SRST_USB_ADP		141

#define SRST_CORESIGHT		144
#define SRST_PD_CORE_AHB_NOC	145
#define SRST_PD_CORE_APB_NOC	146
#define SRST_GIC		148
#define SRST_LCDC_PWM0		149
#define SRST_RGA_H2P_BRG	153
#define SRST_VIDEO		154
#define SRST_GPU_CFG_NIU	157
#define SRST_TSADC		159

#define SRST_DDRPHY0		160
#define SRST_DDRPHY0_APB	161
#define SRST_DDRCTRL0		162
#define SRST_DDRCTRL0_APB	163
#define SRST_VIDEO_NIU		165
#define SRST_VIDEO_NIU_AHB	167
#define SRST_DDRMSCH0		170
#define SRST_PDBUS_AHB		173
#define SRST_CRYPTO		174

#define SRST_UART0		179
#define SRST_UART1		180
#define SRST_UART2		181
#define SRST_UART3		182
#define SRST_UART4		183
#define SRST_SIMC		186
#define SRST_TSP		188
#define SRST_TSP_CLKIN0		189

#define SRST_CORE_L0		192
#define SRST_CORE_L1		193
#define SRST_CORE_L2		194
#define SRST_CORE_L3		195
#define SRST_CORE_L0_PO		195
#define SRST_CORE_L1_PO		197
#define SRST_CORE_L2_PO		198
#define SRST_CORE_L3_PO		199
#define SRST_L2_L		200
#define SRST_ADB_L		201
#define SRST_PD_CORE_L_NIU	202
#define SRST_CCI_SYS		203
#define SRST_CCI_DDR		204
#define SRST_CCI		205
#define SRST_SOCDBG_L		206
#define SRST_CORE_L_DBG		207

#define SRST_CORE_B0_NC		208
#define SRST_CORE_B0_PO_NC	209
#define SRST_L2_B_NC		210
#define SRST_ADB_B_NC		211
#define SRST_PD_CORE_B_NIU_NC	212
#define SRST_PDBUS_STRSYS_NC	213
#define SRST_CORE_L0_NC		214
#define SRST_CORE_L0_PO_NC	215
#define SRST_L2_L_NC		216
#define SRST_ADB_L_NC		217
#define SRST_PD_CORE_L_NIU_NC	218
#define SRST_CCI_SYS_NC		219
#define SRST_CCI_DDR_NC		220
#define SRST_CCI_NC		221
#define SRST_TRACE_NC		222

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
