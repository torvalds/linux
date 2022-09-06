/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
 * Author: Shawn Lin <shawn.lin@rock-chips.com>
 */

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RV1108_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RV1108_H

/* pll id */
#define PLL_APLL			0
#define PLL_DPLL			1
#define PLL_GPLL			2
#define ARMCLK				3

/* sclk gates (special clocks) */
#define SCLK_SPI0			65
#define SCLK_NANDC			67
#define SCLK_SDMMC			68
#define SCLK_SDIO			69
#define SCLK_EMMC			71
#define SCLK_UART0			72
#define SCLK_UART1			73
#define SCLK_UART2			74
#define SCLK_I2S0			75
#define SCLK_I2S1			76
#define SCLK_I2S2			77
#define SCLK_TIMER0			78
#define SCLK_TIMER1			79
#define SCLK_SFC			80
#define SCLK_SDMMC_DRV			81
#define SCLK_SDIO_DRV			82
#define SCLK_EMMC_DRV			83
#define SCLK_SDMMC_SAMPLE		84
#define SCLK_SDIO_SAMPLE		85
#define SCLK_EMMC_SAMPLE		86
#define SCLK_VENC_CORE			87
#define SCLK_HEVC_CORE			88
#define SCLK_HEVC_CABAC			89
#define SCLK_PWM0_PMU			90
#define SCLK_I2C0_PMU			91
#define SCLK_WIFI			92
#define SCLK_CIFOUT			93
#define SCLK_MIPI_CSI_OUT		94
#define SCLK_CIF0			95
#define SCLK_CIF1			96
#define SCLK_CIF2			97
#define SCLK_CIF3			98
#define SCLK_DSP			99
#define SCLK_DSP_IOP			100
#define SCLK_DSP_EPP			101
#define SCLK_DSP_EDP			102
#define SCLK_DSP_EDAP			103
#define SCLK_CVBS_HOST			104
#define SCLK_HDMI_SFR			105
#define SCLK_HDMI_CEC			106
#define SCLK_CRYPTO			107
#define SCLK_SPI			108
#define SCLK_SARADC			109
#define SCLK_TSADC			110
#define SCLK_MAC_PRE			111
#define SCLK_MAC			112
#define SCLK_MAC_RX			113
#define SCLK_MAC_REF			114
#define SCLK_MAC_REFOUT			115
#define SCLK_DSP_PFM			116
#define SCLK_RGA			117
#define SCLK_I2C1			118
#define SCLK_I2C2			119
#define SCLK_I2C3			120
#define SCLK_PWM			121
#define SCLK_ISP			122
#define SCLK_USBPHY			123
#define SCLK_I2S0_SRC			124
#define SCLK_I2S1_SRC			125
#define SCLK_I2S2_SRC			126
#define SCLK_UART0_SRC			127
#define SCLK_UART1_SRC			128
#define SCLK_UART2_SRC			129

#define DCLK_VOP_SRC			185
#define DCLK_HDMIPHY			186
#define DCLK_VOP			187

/* aclk gates */
#define ACLK_DMAC			192
#define ACLK_PRE			193
#define ACLK_CORE			194
#define ACLK_ENMCORE			195
#define ACLK_RKVENC			196
#define ACLK_RKVDEC			197
#define ACLK_VPU			198
#define ACLK_CIF0			199
#define ACLK_VIO0			200
#define ACLK_VIO1			201
#define ACLK_VOP			202
#define ACLK_IEP			203
#define ACLK_RGA			204
#define ACLK_ISP			205
#define ACLK_CIF1			206
#define ACLK_CIF2			207
#define ACLK_CIF3			208
#define ACLK_PERI			209
#define ACLK_GMAC			210

/* pclk gates */
#define PCLK_GPIO1			256
#define PCLK_GPIO2			257
#define PCLK_GPIO3			258
#define PCLK_GRF			259
#define PCLK_I2C1			260
#define PCLK_I2C2			261
#define PCLK_I2C3			262
#define PCLK_SPI			263
#define PCLK_SFC			264
#define PCLK_UART0			265
#define PCLK_UART1			266
#define PCLK_UART2			267
#define PCLK_TSADC			268
#define PCLK_PWM			269
#define PCLK_TIMER			270
#define PCLK_PERI			271
#define PCLK_GPIO0_PMU			272
#define PCLK_I2C0_PMU			273
#define PCLK_PWM0_PMU			274
#define PCLK_ISP			275
#define PCLK_VIO			276
#define PCLK_MIPI_DSI			277
#define PCLK_HDMI_CTRL			278
#define PCLK_SARADC			279
#define PCLK_DSP_CFG			280
#define PCLK_BUS			281
#define PCLK_EFUSE0			282
#define PCLK_EFUSE1			283
#define PCLK_WDT			284
#define PCLK_GMAC			285

/* hclk gates */
#define HCLK_I2S0_8CH			320
#define HCLK_I2S1_2CH			321
#define HCLK_I2S2_2CH			322
#define HCLK_NANDC			323
#define HCLK_SDMMC			324
#define HCLK_SDIO			325
#define HCLK_EMMC			326
#define HCLK_PERI			327
#define HCLK_SFC			328
#define HCLK_RKVENC			329
#define HCLK_RKVDEC			330
#define HCLK_CIF0			331
#define HCLK_VIO			332
#define HCLK_VOP			333
#define HCLK_IEP			334
#define HCLK_RGA			335
#define HCLK_ISP			336
#define HCLK_CRYPTO_MST			337
#define HCLK_CRYPTO_SLV			338
#define HCLK_HOST0			339
#define HCLK_OTG			340
#define HCLK_CIF1			341
#define HCLK_CIF2			342
#define HCLK_CIF3			343
#define HCLK_BUS			344
#define HCLK_VPU			345

#define CLK_NR_CLKS			(HCLK_VPU + 1)

/* reset id */
#define SRST_CORE_PO_AD			0
#define SRST_CORE_AD			1
#define SRST_L2_AD			2
#define SRST_CPU_NIU_AD			3
#define SRST_CORE_PO			4
#define SRST_CORE			5
#define SRST_L2				6
#define SRST_CORE_DBG			8
#define PRST_DBG			9
#define RST_DAP				10
#define PRST_DBG_NIU			11
#define ARST_STRC_SYS_AD		15

#define SRST_DDRPHY_CLKDIV		16
#define SRST_DDRPHY			17
#define PRST_DDRPHY			18
#define PRST_HDMIPHY			19
#define PRST_VDACPHY			20
#define PRST_VADCPHY			21
#define PRST_MIPI_CSI_PHY		22
#define PRST_MIPI_DSI_PHY		23
#define PRST_ACODEC			24
#define ARST_BUS_NIU			25
#define PRST_TOP_NIU			26
#define ARST_INTMEM			27
#define HRST_ROM			28
#define ARST_DMAC			29
#define SRST_MSCH_NIU			30
#define PRST_MSCH_NIU			31

#define PRST_DDRUPCTL			32
#define NRST_DDRUPCTL			33
#define PRST_DDRMON			34
#define HRST_I2S0_8CH			35
#define MRST_I2S0_8CH			36
#define HRST_I2S1_2CH			37
#define MRST_IS21_2CH			38
#define HRST_I2S2_2CH			39
#define MRST_I2S2_2CH			40
#define HRST_CRYPTO			41
#define SRST_CRYPTO			42
#define PRST_SPI			43
#define SRST_SPI			44
#define PRST_UART0			45
#define PRST_UART1			46
#define PRST_UART2			47

#define SRST_UART0			48
#define SRST_UART1			49
#define SRST_UART2			50
#define PRST_I2C1			51
#define PRST_I2C2			52
#define PRST_I2C3			53
#define SRST_I2C1			54
#define SRST_I2C2			55
#define SRST_I2C3			56
#define PRST_PWM1			58
#define SRST_PWM1			60
#define PRST_WDT			61
#define PRST_GPIO1			62
#define PRST_GPIO2			63

#define PRST_GPIO3			64
#define PRST_GRF			65
#define PRST_EFUSE			66
#define PRST_EFUSE512			67
#define PRST_TIMER0			68
#define SRST_TIMER0			69
#define SRST_TIMER1			70
#define PRST_TSADC			71
#define SRST_TSADC			72
#define PRST_SARADC			73
#define SRST_SARADC			74
#define HRST_SYSBUS			75
#define PRST_USBGRF			76

#define ARST_PERIPH_NIU			80
#define HRST_PERIPH_NIU			81
#define PRST_PERIPH_NIU			82
#define HRST_PERIPH			83
#define HRST_SDMMC			84
#define HRST_SDIO			85
#define HRST_EMMC			86
#define HRST_NANDC			87
#define NRST_NANDC			88
#define HRST_SFC			89
#define SRST_SFC			90
#define ARST_GMAC			91
#define HRST_OTG			92
#define SRST_OTG			93
#define SRST_OTG_ADP			94
#define HRST_HOST0			95

#define HRST_HOST0_AUX			96
#define HRST_HOST0_ARB			97
#define SRST_HOST0_EHCIPHY		98
#define SRST_HOST0_UTMI			99
#define SRST_USBPOR			100
#define SRST_UTMI0			101
#define SRST_UTMI1			102

#define ARST_VIO0_NIU			102
#define ARST_VIO1_NIU			103
#define HRST_VIO_NIU			104
#define PRST_VIO_NIU			105
#define ARST_VOP			106
#define HRST_VOP			107
#define DRST_VOP			108
#define ARST_IEP			109
#define HRST_IEP			110
#define ARST_RGA			111
#define HRST_RGA			112
#define SRST_RGA			113
#define PRST_CVBS			114
#define PRST_HDMI			115
#define SRST_HDMI			116
#define PRST_MIPI_DSI			117

#define ARST_ISP_NIU			118
#define HRST_ISP_NIU			119
#define HRST_ISP			120
#define SRST_ISP			121
#define ARST_VIP0			122
#define HRST_VIP0			123
#define PRST_VIP0			124
#define ARST_VIP1			125
#define HRST_VIP1			126
#define PRST_VIP1			127
#define ARST_VIP2			128
#define HRST_VIP2			129
#define PRST_VIP2			120
#define ARST_VIP3			121
#define HRST_VIP3			122
#define PRST_VIP4			123

#define PRST_CIF1TO4			124
#define SRST_CVBS_CLK			125
#define HRST_CVBS			126

#define ARST_VPU_NIU			140
#define HRST_VPU_NIU			141
#define ARST_VPU			142
#define HRST_VPU			143
#define ARST_RKVDEC_NIU			144
#define HRST_RKVDEC_NIU			145
#define ARST_RKVDEC			146
#define HRST_RKVDEC			147
#define SRST_RKVDEC_CABAC		148
#define SRST_RKVDEC_CORE		149
#define ARST_RKVENC_NIU			150
#define HRST_RKVENC_NIU			151
#define ARST_RKVENC			152
#define HRST_RKVENC			153
#define SRST_RKVENC_CORE		154

#define SRST_DSP_CORE			156
#define SRST_DSP_SYS			157
#define SRST_DSP_GLOBAL			158
#define SRST_DSP_OECM			159
#define PRST_DSP_IOP_NIU		160
#define ARST_DSP_EPP_NIU		161
#define ARST_DSP_EDP_NIU		162
#define PRST_DSP_DBG_NIU		163
#define PRST_DSP_CFG_NIU		164
#define PRST_DSP_GRF			165
#define PRST_DSP_MAILBOX		166
#define PRST_DSP_INTC			167
#define PRST_DSP_PFM_MON		169
#define SRST_DSP_PFM_MON		170
#define ARST_DSP_EDAP_NIU		171

#define SRST_PMU			172
#define SRST_PMU_I2C0			173
#define PRST_PMU_I2C0			174
#define PRST_PMU_GPIO0			175
#define PRST_PMU_INTMEM			176
#define PRST_PMU_PWM0			177
#define SRST_PMU_PWM0			178
#define PRST_PMU_GRF			179
#define SRST_PMU_NIU			180
#define SRST_PMU_PVTM			181
#define ARST_DSP_EDP_PERF		184
#define ARST_DSP_EPP_PERF		185

#endif /* _DT_BINDINGS_CLK_ROCKCHIP_RV1108_H */
