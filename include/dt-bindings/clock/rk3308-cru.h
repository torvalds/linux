/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
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

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RK3308_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RK3308_H

/* core clocks */
#define PLL_APLL		1
#define PLL_DPLL		2
#define PLL_VPLL0		3
#define PLL_VPLL1		4
#define ARMCLK			5

/* sclk (special clocks) */
#define USB480M			14
#define SCLK_RTC32K		15
#define SCLK_PVTM_CORE		16
#define SCLK_UART0		17
#define SCLK_UART1		18
#define SCLK_UART2		19
#define SCLK_UART3		20
#define SCLK_UART4		21
#define SCLK_I2C0		22
#define SCLK_I2C1		23
#define SCLK_I2C2		24
#define SCLK_I2C3		25
#define SCLK_PWM		26
#define SCLK_SPI0		27
#define SCLK_SPI1		28
#define SCLK_SPI2		29
#define SCLK_TIMER0		30
#define SCLK_TIMER1		31
#define SCLK_TIMER2		32
#define SCLK_TIMER3		33
#define SCLK_TIMER4		34
#define SCLK_TIMER5		35
#define SCLK_TSADC		36
#define SCLK_SARADC		37
#define SCLK_OTP		38
#define SCLK_OTP_USR		39
#define SCLK_CPU_BOOST		40
#define SCLK_CRYPTO		41
#define SCLK_CRYPTO_APK		42
#define SCLK_NANDC_DIV		43
#define SCLK_NANDC_DIV50	44
#define SCLK_NANDC		45
#define SCLK_SDMMC_DIV		46
#define SCLK_SDMMC_DIV50	47
#define SCLK_SDMMC		48
#define SCLK_SDMMC_DRV		49
#define SCLK_SDMMC_SAMPLE	50
#define SCLK_SDIO_DIV		51
#define SCLK_SDIO_DIV50		52
#define SCLK_SDIO		53
#define SCLK_SDIO_DRV		54
#define SCLK_SDIO_SAMPLE	55
#define SCLK_EMMC_DIV		56
#define SCLK_EMMC_DIV50		57
#define SCLK_EMMC		58
#define SCLK_EMMC_DRV		59
#define SCLK_EMMC_SAMPLE	60
#define SCLK_SFC		61
#define SCLK_OTG_ADP		62
#define SCLK_MAC_SRC		63
#define SCLK_MAC		64
#define SCLK_MAC_REF		65
#define SCLK_MAC_RX_TX		66
#define SCLK_MAC_RMII		67
#define SCLK_DDR_MON_TIMER	68
#define SCLK_DDR_MON		69
#define SCLK_DDRCLK		70
#define SCLK_PMU		71
#define SCLK_USBPHY_REF		72
#define SCLK_WIFI		73
#define SCLK_PVTM_PMU		74
#define SCLK_PDM		75
#define SCLK_I2S0_8CH_TX	76
#define SCLK_I2S0_8CH_TX_OUT	77
#define SCLK_I2S0_8CH_RX	78
#define SCLK_I2S0_8CH_RX_OUT	79
#define SCLK_I2S1_8CH_TX	80
#define SCLK_I2S1_8CH_TX_OUT	81
#define SCLK_I2S1_8CH_RX	82
#define SCLK_I2S1_8CH_RX_OUT	83
#define SCLK_I2S2_8CH_TX	84
#define SCLK_I2S2_8CH_TX_OUT	85
#define SCLK_I2S2_8CH_RX	86
#define SCLK_I2S2_8CH_RX_OUT	87
#define SCLK_I2S3_8CH_TX	88
#define SCLK_I2S3_8CH_TX_OUT	89
#define SCLK_I2S3_8CH_RX	90
#define SCLK_I2S3_8CH_RX_OUT	91
#define SCLK_I2S0_2CH		92
#define SCLK_I2S0_2CH_OUT	93
#define SCLK_I2S1_2CH		94
#define SCLK_I2S1_2CH_OUT	95
#define SCLK_SPDIF_TX_DIV	96
#define SCLK_SPDIF_TX_DIV50	97
#define SCLK_SPDIF_TX		98
#define SCLK_SPDIF_RX_DIV	99
#define SCLK_SPDIF_RX_DIV50	100
#define SCLK_SPDIF_RX		101
#define SCLK_I2S0_8CH_TX_MUX	102
#define SCLK_I2S0_8CH_RX_MUX	103
#define SCLK_I2S1_8CH_TX_MUX	104
#define SCLK_I2S1_8CH_RX_MUX	105
#define SCLK_I2S2_8CH_TX_MUX	106
#define SCLK_I2S2_8CH_RX_MUX	107
#define SCLK_I2S3_8CH_TX_MUX	108
#define SCLK_I2S3_8CH_RX_MUX	109
#define SCLK_I2S0_8CH_TX_SRC	110
#define SCLK_I2S0_8CH_RX_SRC	111
#define SCLK_I2S1_8CH_TX_SRC	112
#define SCLK_I2S1_8CH_RX_SRC	113
#define SCLK_I2S2_8CH_TX_SRC	114
#define SCLK_I2S2_8CH_RX_SRC	115
#define SCLK_I2S3_8CH_TX_SRC	116
#define SCLK_I2S3_8CH_RX_SRC	117
#define SCLK_I2S0_2CH_SRC	118
#define SCLK_I2S1_2CH_SRC	119

/* dclk */
#define DCLK_VOP		120

/* aclk */
#define ACLK_BUS_SRC		130
#define ACLK_BUS		131
#define ACLK_PERI_SRC		132
#define ACLK_PERI		133
#define ACLK_MAC		134
#define ACLK_CRYPTO		135
#define ACLK_VOP		136
#define ACLK_GIC		137
#define ACLK_DMAC0		138
#define ACLK_DMAC1		139

/* hclk */
#define HCLK_BUS		150
#define HCLK_PERI		151
#define HCLK_AUDIO		152
#define HCLK_NANDC		153
#define HCLK_SDMMC		154
#define HCLK_SDIO		155
#define HCLK_EMMC		156
#define HCLK_SFC		157
#define HCLK_OTG		158
#define HCLK_HOST		159
#define HCLK_HOST_ARB		160
#define HCLK_PDM		161
#define HCLK_SPDIFTX		162
#define HCLK_SPDIFRX		163
#define HCLK_I2S0_8CH		164
#define HCLK_I2S1_8CH		165
#define HCLK_I2S2_8CH		166
#define HCLK_I2S3_8CH		167
#define HCLK_I2S0_2CH		168
#define HCLK_I2S1_2CH		169
#define HCLK_VAD		170
#define HCLK_CRYPTO		171
#define HCLK_VOP		172

/* pclk */
#define PCLK_BUS		190
#define PCLK_DDR		191
#define PCLK_PERI		192
#define PCLK_PMU		193
#define PCLK_AUDIO		194
#define PCLK_MAC		195
#define PCLK_ACODEC		196
#define PCLK_UART0		197
#define PCLK_UART1		198
#define PCLK_UART2		199
#define PCLK_UART3		200
#define PCLK_UART4		201
#define PCLK_I2C0		202
#define PCLK_I2C1		203
#define PCLK_I2C2		204
#define PCLK_I2C3		205
#define PCLK_PWM		206
#define PCLK_SPI0		207
#define PCLK_SPI1		208
#define PCLK_SPI2		209
#define PCLK_SARADC		210
#define PCLK_TSADC		211
#define PCLK_TIMER		212
#define PCLK_OTP_NS		213
#define PCLK_WDT		214
#define PCLK_GPIO0		215
#define PCLK_GPIO1		216
#define PCLK_GPIO2		217
#define PCLK_GPIO3		218
#define PCLK_GPIO4		219
#define PCLK_SGRF		220
#define PCLK_GRF		221
#define PCLK_USBSD_DET		222
#define PCLK_DDR_UPCTL		223
#define PCLK_DDR_MON		224
#define PCLK_DDRPHY		225
#define PCLK_DDR_STDBY		226
#define PCLK_USB_GRF		227
#define PCLK_CRU		228
#define PCLK_OTP_PHY		229
#define PCLK_CPU_BOOST		230

#define CLK_NR_CLKS		(PCLK_CPU_BOOST + 1)

/* soft-reset indices */

/* cru_softrst_con0 */
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

/* cru_softrst_con1 */
#define SRST_DAP		16
#define SRST_CORE_PVTM		17
#define SRST_CORE_PRF		18
#define SRST_CORE_GRF		19
#define SRST_DDRUPCTL		20
#define SRST_DDRUPCTL_P		22
#define SRST_MSCH		23
#define SRST_DDRMON_P		25
#define SRST_DDRSTDBY_P		26
#define SRST_DDRSTDBY		27
#define SRST_DDRPHY		28
#define SRST_DDRPHY_DIV		29
#define SRST_DDRPHY_P		30

/* cru_softrst_con2 */
#define SRST_BUS_NIU_H		32
#define SRST_USB_NIU_P		33
#define SRST_CRYPTO_A		34
#define SRST_CRYPTO_H		35
#define SRST_CRYPTO		36
#define SRST_CRYPTO_APK		37
#define SRST_VOP_A		38
#define SRST_VOP_H		39
#define SRST_VOP_D		40
#define SRST_INTMEM_A		41
#define SRST_ROM_H		42
#define SRST_GIC_A		43
#define SRST_UART0_P		44
#define SRST_UART0		45
#define SRST_UART1_P		46
#define SRST_UART1		47

/* cru_softrst_con3 */
#define SRST_UART2_P		48
#define SRST_UART2		49
#define SRST_UART3_P		50
#define SRST_UART3		51
#define SRST_UART4_P		52
#define SRST_UART4		53
#define SRST_I2C0_P		54
#define SRST_I2C0		55
#define SRST_I2C1_P		56
#define SRST_I2C1		57
#define SRST_I2C2_P		58
#define SRST_I2C2		59
#define SRST_I2C3_P		60
#define SRST_I2C3		61
#define SRST_PWM_P		62
#define SRST_PWM		63

/* cru_softrst_con4 */
#define SRST_SPI0_P		64
#define SRST_SPI0		65
#define SRST_SPI1_P		66
#define SRST_SPI1		67
#define SRST_SPI2_P		68
#define SRST_SPI2		69
#define SRST_SARADC_P		70
#define SRST_TSADC_P		71
#define SRST_TSADC		72
#define SRST_TIMER0_P		73
#define SRST_TIMER0		74
#define SRST_TIMER1		75
#define SRST_TIMER2		76
#define SRST_TIMER3		77
#define SRST_TIMER4		78
#define SRST_TIMER5		79

/* cru_softrst_con5 */
#define SRST_OTP_NS_P		80
#define SRST_OTP_NS_SBPI	81
#define SRST_OTP_NS_USR		82
#define SRST_OTP_PHY_P		83
#define SRST_OTP_PHY		84
#define SRST_GPIO0_P		86
#define SRST_GPIO1_P		87
#define SRST_GPIO2_P		88
#define SRST_GPIO3_P		89
#define SRST_GPIO4_P		90
#define SRST_GRF_P		91
#define SRST_USBSD_DET_P	92
#define SRST_PMU		93
#define SRST_PMU_PVTM		94
#define SRST_USB_GRF_P		95

/* cru_softrst_con6 */
#define SRST_CPU_BOOST		96
#define SRST_CPU_BOOST_P	97
#define SRST_PERI_NIU_A		104
#define SRST_PERI_NIU_H		105
#define SRST_PERI_NIU_p		106
#define SRST_USB2OTG_H		107
#define SRST_USB2OTG		108
#define SRST_USB2OTG_ADP	109
#define SRST_USB2HOST_H		110
#define SRST_USB2HOST_ARB_H	111

/* cru_softrst_con7 */
#define SRST_USB2HOST_AUX_H	112
#define SRST_USB2HOST_EHCI	113
#define SRST_USB2HOST		114
#define SRST_USBPHYPOR		115
#define SRST_UTMI0		116
#define SRST_UTMI1		117
#define SRST_SDIO_H		118
#define SRST_EMMC_H		119
#define SRST_SFC_H		120
#define SRST_SFC		121
#define SRST_SD_H		122
#define SRST_NANDC_H		123
#define SRST_NANDC_N		124
#define SRST_MAC_A		125

/* cru_softrst_con8 */
#define SRST_AUDIO_NIU_H	128
#define SRST_AUDIO_NIU_P	129
#define SRST_PDM_H		130
#define SRST_PDM_M		131
#define SRST_SPDIFTX_H		132
#define SRST_SPDIFTX_M		133
#define SRST_SPDIFRX_H		134
#define SRST_SPDIFRX_M		135
#define SRST_I2S0_8CH_H		136
#define SRST_I2S0_8CH_TX_M	137
#define SRST_I2S0_8CH_RX_M	138
#define SRST_I2S1_8CH_H		139
#define SRST_I2S1_8CH_TX_M	140
#define SRST_I2S1_8CH_RX_M	141
#define SRST_I2S2_8CH_H		142
#define SRST_I2S2_8CH_TX_M	143

/* cru_softrst_con9 */
#define SRST_I2S2_8CH_RX_M	144
#define SRST_I2S3_8CH_H		145
#define SRST_I2S3_8CH_TX_M	146
#define SRST_I2S3_8CH_RX_M	147
#define SRST_I2S0_2CH_H		148
#define SRST_I2S0_2CH_M		149
#define SRST_I2S1_2CH_H		150
#define SRST_I2S1_2CH_M		151
#define SRST_VAD_H		152
#define SRST_ACODEC_P		153

#endif
