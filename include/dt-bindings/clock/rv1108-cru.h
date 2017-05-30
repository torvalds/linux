/*
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
 * Author: Shawn Lin <shawn.lin@rock-chips.com>
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

/* aclk gates */
#define ACLK_DMAC			192
#define ACLK_PRE			193
#define ACLK_CORE			194
#define ACLK_ENMCORE			195

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

/* hclk gates */
#define HCLK_I2S0_8CH			320
#define HCLK_I2S1_8CH			321
#define HCLK_I2S2_2CH			322
#define HCLK_NANDC			323
#define HCLK_SDMMC			324
#define HCLK_SDIO			325
#define HCLK_EMMC			326
#define HCLK_PERI			327
#define HCLK_SFC			328

#define CLK_NR_CLKS			(HCLK_SFC + 1)

/* reset id */
#define SRST_CORE_PO_AD		0
#define SRST_CORE_AD			1
#define SRST_L2_AD			2
#define SRST_CPU_NIU_AD		3
#define SRST_CORE_PO			4
#define SRST_CORE			5
#define SRST_L2			6
#define SRST_CORE_DBG			8
#define PRST_DBG			9
#define RST_DAP			10
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

#define ARST_PERIPH_NIU		80
#define HRST_PERIPH_NIU		81
#define PRST_PERIPH_NIU		82
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
#define SRST_HOST0_UTMI		99
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
#define ARST_RKVDEC_NIU		144
#define HRST_RKVDEC_NIU		145
#define ARST_RKVDEC			146
#define HRST_RKVDEC			147
#define SRST_RKVDEC_CABAC		148
#define SRST_RKVDEC_CORE		149
#define ARST_RKVENC_NIU		150
#define HRST_RKVENC_NIU		151
#define ARST_RKVENC			152
#define HRST_RKVENC			153
#define SRST_RKVENC_CORE		154

#define SRST_DSP_CORE			156
#define SRST_DSP_SYS			157
#define SRST_DSP_GLOBAL		158
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
#define PRST_PMU_INTMEM		176
#define PRST_PMU_PWM0			177
#define SRST_PMU_PWM0			178
#define PRST_PMU_GRF			179
#define SRST_PMU_NIU			180
#define SRST_PMU_PVTM			181
#define ARST_DSP_EDP_PERF		184
#define ARST_DSP_EPP_PERF		185

#endif /* _DT_BINDINGS_CLK_ROCKCHIP_RV1108_H */
