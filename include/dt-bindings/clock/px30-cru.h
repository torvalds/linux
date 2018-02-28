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

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_PX30_H
#define _DT_BINDINGS_CLK_ROCKCHIP_PX30_H

/* core clocks */
#define PLL_APLL		1
#define PLL_DPLL		2
#define PLL_CPLL		3
#define PLL_NPLL		4
#define APLL_BOOST_H		5
#define APLL_BOOST_L		6
#define ARMCLK			7

/* sclk gates (special clocks) */
#define USB480M			14
#define SCLK_PDM		15
#define SCLK_I2S0_TX		16
#define SCLK_I2S0_TX_OUT	17
#define SCLK_I2S0_RX		18
#define SCLK_I2S0_RX_OUT	19
#define SCLK_I2S1		20
#define SCLK_I2S1_OUT		21
#define SCLK_I2S2		22
#define SCLK_I2S2_OUT		23
#define SCLK_UART1		24
#define SCLK_UART2		25
#define SCLK_UART3		26
#define SCLK_UART4		27
#define SCLK_UART5		28
#define SCLK_I2C0		29
#define SCLK_I2C1		30
#define SCLK_I2C2		31
#define SCLK_I2C3		32
#define SCLK_I2C4		33
#define SCLK_PWM0		34
#define SCLK_PWM1		35
#define SCLK_SPI0		36
#define SCLK_SPI1		37
#define SCLK_TIMER0		38
#define SCLK_TIMER1		39
#define SCLK_TIMER2		40
#define SCLK_TIMER3		41
#define SCLK_TIMER4		42
#define SCLK_TIMER5		43
#define SCLK_TSADC		44
#define SCLK_SARADC		45
#define SCLK_OTP		46
#define SCLK_OTP_USR		47
#define SCLK_CRYPTO		48
#define SCLK_CRYPTO_APK		49
#define SCLK_DDRC		50
#define SCLK_ISP		51
#define SCLK_CIF_OUT		52
#define SCLK_RGA_CORE		53
#define SCLK_VOPB_PWM		54
#define SCLK_NANDC		55
#define SCLK_SDIO		56
#define SCLK_EMMC		57
#define SCLK_SFC		58
#define SCLK_SDMMC		59
#define SCLK_OTG_ADP		60
#define SCLK_GMAC_SRC		61
#define SCLK_GMAC		62
#define SCLK_GMAC_RX_TX		63
#define SCLK_MAC_REF		64
#define SCLK_MAC_REFOUT		65
#define SCLK_MAC_OUT		66
#define SCLK_SDMMC_DRV		67
#define SCLK_SDMMC_SAMPLE	68
#define SCLK_SDIO_DRV		69
#define SCLK_SDIO_SAMPLE	70
#define SCLK_EMMC_DRV		71
#define SCLK_EMMC_SAMPLE	72
#define SCLK_GPU		73
#define SCLK_PVTM		74
#define SCLK_CORE_VPU		75
#define SCLK_GMAC_RMII		76
#define SCLK_UART2_SRC		77
#define SCLK_NANDC_DIV		78
#define SCLK_NANDC_DIV50	79
#define SCLK_SDIO_DIV		80
#define SCLK_SDIO_DIV50		81
#define SCLK_EMMC_DIV		82
#define SCLK_EMMC_DIV50		83
#define SCLK_DDRCLK		84
#define SCLK_UART1_SRC		85

/* dclk gates */
#define DCLK_VOPB		150
#define DCLK_VOPL		151

/* aclk gates */
#define ACLK_GPU		170
#define ACLK_BUS_PRE		171
#define ACLK_CRYPTO		172
#define ACLK_VI_PRE		173
#define ACLK_VO_PRE		174
#define ACLK_VPU		175
#define ACLK_PERI_PRE		176
#define ACLK_GMAC		178
#define ACLK_CIF		179
#define ACLK_ISP		180
#define ACLK_VOPB		181
#define ACLK_VOPL		182
#define ACLK_RGA		183
#define ACLK_GIC		184
#define ACLK_DCF		186
#define ACLK_DMAC		187
#define ACLK_BUS_SRC		188
#define ACLK_PERI_SRC		189

/* hclk gates */
#define HCLK_BUS_PRE		240
#define HCLK_CRYPTO		241
#define HCLK_VI_PRE		242
#define HCLK_VO_PRE		243
#define HCLK_VPU		244
#define HCLK_PERI_PRE		245
#define HCLK_MMC_NAND		246
#define HCLK_SDMMC		247
#define HCLK_USB		248
#define HCLK_CIF		249
#define HCLK_ISP		250
#define HCLK_VOPB		251
#define HCLK_VOPL		252
#define HCLK_RGA		253
#define HCLK_NANDC		254
#define HCLK_SDIO		255
#define HCLK_EMMC		256
#define HCLK_SFC		257
#define HCLK_OTG		258
#define HCLK_HOST		259
#define HCLK_HOST_ARB		260
#define HCLK_PDM		261
#define HCLK_I2S0		262
#define HCLK_I2S1		263
#define HCLK_I2S2		264

/* pclk gates */
#define PCLK_BUS_PRE		320
#define PCLK_DDR		321
#define PCLK_VO_PRE		322
#define PCLK_GMAC		323
#define PCLK_MIPI_DSI		324
#define PCLK_MIPIDSIPHY		325
#define PCLK_MIPICSIPHY		326
#define PCLK_USB_GRF		327
#define PCLK_DCF		328
#define PCLK_UART1		329
#define PCLK_UART2		330
#define PCLK_UART3		331
#define PCLK_UART4		332
#define PCLK_UART5		333
#define PCLK_I2C0		334
#define PCLK_I2C1		335
#define PCLK_I2C2		336
#define PCLK_I2C3		337
#define PCLK_I2C4		338
#define PCLK_PWM0		339
#define PCLK_PWM1		340
#define PCLK_SPI0		341
#define PCLK_SPI1		342
#define PCLK_SARADC		343
#define PCLK_TSADC		344
#define PCLK_TIMER		345
#define PCLK_OTP_NS		346
#define PCLK_WDT_NS		347
#define PCLK_GPIO1		348
#define PCLK_GPIO2		349
#define PCLK_GPIO3		350
#define PCLK_ISP		351
#define PCLK_CIF		352
#define PCLK_OTP_PHY		353

#define CLK_NR_CLKS		(PCLK_OTP_PHY + 1)

/* pmu-clocks indices */

#define PLL_GPLL		1

#define SCLK_RTC32K_PMU		4
#define SCLK_WIFI_PMU		5
#define SCLK_UART0_PMU		6
#define SCLK_PVTM_PMU		7
#define PCLK_PMU_PRE		8
#define SCLK_REF24M_PMU		9
#define SCLK_USBPHY_REF		10
#define SCLK_MIPIDSIPHY_REF	11

#define XIN24M_DIV		12

#define PCLK_GPIO0_PMU		20
#define PCLK_UART0_PMU		21

#define CLKPMU_NR_CLKS		(PCLK_UART0_PMU + 1)

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
#define SRST_CORE_NOC		13
#define SRST_STRC_A		14
#define SRST_L2C		15

#define SRST_DAP		16
#define SRST_CORE_PVTM		17
#define SRST_GPU		18
#define SRST_GPU_NIU		19
#define SRST_UPCTL2		20
#define SRST_UPCTL2_A		21
#define SRST_UPCTL2_P		22
#define SRST_MSCH		23
#define SRST_MSCH_P		24
#define SRST_DDRMON_P		25
#define SRST_DDRSTDBY_P		26
#define SRST_DDRSTDBY		27
#define SRST_DDRGRF_p		28
#define SRST_AXI_SPLIT_A	29
#define SRST_AXI_CMD_A		30
#define SRST_AXI_CMD_P		31

#define SRST_DDRPHY		32
#define SRST_DDRPHYDIV		33
#define SRST_DDRPHY_P		34
#define SRST_VPU_A		36
#define SRST_VPU_NIU_A		37
#define SRST_VPU_H		38
#define SRST_VPU_NIU_H		39
#define SRST_VI_NIU_A		40
#define SRST_VI_NIU_H		41
#define SRST_ISP_H		42
#define SRST_ISP		43
#define SRST_CIF_A		44
#define SRST_CIF_H		45
#define SRST_CIF_PCLKIN		46
#define SRST_MIPICSIPHY_P	47

#define SRST_VO_NIU_A		48
#define SRST_VO_NIU_H		49
#define SRST_VO_NIU_P		50
#define SRST_VOPB_A		51
#define SRST_VOPB_H		52
#define SRST_VOPB		53
#define SRST_PWM_VOPB		54
#define SRST_VOPL_A		55
#define SRST_VOPL_H		56
#define SRST_VOPL		57
#define SRST_RGA_A		58
#define SRST_RGA_H		59
#define SRST_RGA		60
#define SRST_MIPIDSI_HOST_P	61
#define SRST_MIPIDSIPHY_P	62
#define SRST_VPU_CORE		63

#define SRST_PERI_NIU_A		64
#define SRST_USB_NIU_H		65
#define SRST_USB2OTG_H		66
#define SRST_USB2OTG		67
#define SRST_USB2OTG_ADP	68
#define SRST_USB2HOST_H		69
#define SRST_USB2HOST_ARB_H	70
#define SRST_USB2HOST_AUX_H	71
#define SRST_USB2HOST_EHCI	72
#define SRST_USB2HOST		73
#define SRST_USBPHYPOR		74
#define SRST_USBPHY_OTG_PORT	75
#define SRST_USBPHY_HOST_PORT	76
#define SRST_USBPHY_GRF		77
#define SRST_CPU_BOOST_P	78
#define SRST_CPU_BOOST		79

#define SRST_MMC_NAND_NIU_H	80
#define SRST_SDIO_H		81
#define SRST_EMMC_H		82
#define SRST_SFC_H		83
#define SRST_SFC		84
#define SRST_SDCARD_NIU_H	85
#define SRST_SDMMC_H		86
#define SRST_NANDC_H		89
#define SRST_NANDC		90
#define SRST_GMAC_NIU_A		92
#define SRST_GMAC_NIU_P		93
#define SRST_GMAC_A		94

#define SRST_PMU_NIU_P		96
#define SRST_PMU_SGRF_P		97
#define SRST_PMU_GRF_P		98
#define SRST_PMU		99
#define SRST_PMU_MEM_P		100
#define SRST_PMU_GPIO0_P	101
#define SRST_PMU_UART0_P	102
#define SRST_PMU_CRU_P		103
#define SRST_PMU_PVTM		104
#define SRST_PMU_UART		105
#define SRST_PMU_NIU_H		106
#define SRST_PMU_DDR_FAIL_SAVE	107
#define SRST_PMU_CORE_PERF_A	108
#define SRST_PMU_CORE_GRF_P	109
#define SRST_PMU_GPU_PERF_A	110
#define SRST_PMU_GPU_GRF_P	111

#define SRST_CRYPTO_NIU_A	112
#define SRST_CRYPTO_NIU_H	113
#define SRST_CRYPTO_A		114
#define SRST_CRYPTO_H		115
#define SRST_CRYPTO		116
#define SRST_CRYPTO_APK		117
#define SRST_BUS_NIU_H		120
#define SRST_USB_NIU_P		121
#define SRST_BUS_TOP_NIU_P	122
#define SRST_INTMEM_A		123
#define SRST_GIC_A		124
#define SRST_ROM_H		126
#define SRST_DCF_A		127

#define SRST_DCF_P		128
#define SRST_PDM_H		129
#define SRST_PDM		130
#define SRST_I2S0_H		131
#define SRST_I2S0_TX		132
#define SRST_I2S1_H		133
#define SRST_I2S1		134
#define SRST_I2S2_H		135
#define SRST_I2S2		136
#define SRST_UART1_P		137
#define SRST_UART1		138
#define SRST_UART2_P		139
#define SRST_UART2		140
#define SRST_UART3_P		141
#define SRST_UART3		142
#define SRST_UART4_P		143

#define SRST_UART4		144
#define SRST_UART5_P		145
#define SRST_UART5		146
#define SRST_I2C0_P		147
#define SRST_I2C0		148
#define SRST_I2C1_P		149
#define SRST_I2C1		150
#define SRST_I2C2_P		151
#define SRST_I2C2		152
#define SRST_I2C3_P		153
#define SRST_I2C3		154
#define SRST_PWM0_P		157
#define SRST_PWM0		158
#define SRST_PWM1_P		159

#define SRST_PWM1		160
#define SRST_SPI0_P		161
#define SRST_SPI0		162
#define SRST_SPI1_P		163
#define SRST_SPI1		164
#define SRST_SARADC_P		165
#define SRST_SARADC		166
#define SRST_TSADC_P		167
#define SRST_TSADC		168
#define SRST_TIMER_P		169
#define SRST_TIMER0		170
#define SRST_TIMER1		171
#define SRST_TIMER2		172
#define SRST_TIMER3		173
#define SRST_TIMER4		174
#define SRST_TIMER5		175

#define SRST_OTP_NS_P		176
#define SRST_OTP_NS_SBPI	177
#define SRST_OTP_NS_USR		178
#define SRST_OTP_PHY_P		179
#define SRST_OTP_PHY		180
#define SRST_WDT_NS_P		181
#define SRST_GPIO1_P		182
#define SRST_GPIO2_P		183
#define SRST_GPIO3_P		184
#define SRST_SGRF_P		185
#define SRST_GRF_P		186
#define SRST_I2S0_RX		191

#endif
