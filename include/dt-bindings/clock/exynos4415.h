/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Device Tree binding constants for Samsung Exynos4415 clock controllers.
 */

#ifndef _DT_BINDINGS_CLOCK_SAMSUNG_EXYNOS4415_CLOCK_H
#define _DT_BINDINGS_CLOCK_SAMSUNG_EXYNOS4415_CLOCK_H

/*
 * Let each exported clock get a unique index, which is used on DT-enabled
 * platforms to lookup the clock from a clock specifier. These indices are
 * therefore considered an ABI and so must not be changed. This implies
 * that new clocks should be added either in free spaces between clock groups
 * or at the end.
 */

/*
 * Main CMU
 */

#define CLK_OSCSEL			1
#define CLK_FIN_PLL			2
#define CLK_FOUT_APLL			3
#define CLK_FOUT_MPLL			4
#define CLK_FOUT_EPLL			5
#define CLK_FOUT_G3D_PLL		6
#define CLK_FOUT_ISP_PLL		7
#define CLK_FOUT_DISP_PLL		8

/* Muxes */
#define CLK_MOUT_MPLL_USER_L		16
#define CLK_MOUT_GDL			17
#define CLK_MOUT_MPLL_USER_R		18
#define CLK_MOUT_GDR			19
#define CLK_MOUT_EBI			20
#define CLK_MOUT_ACLK_200		21
#define CLK_MOUT_ACLK_160		22
#define CLK_MOUT_ACLK_100		23
#define CLK_MOUT_ACLK_266		24
#define CLK_MOUT_G3D_PLL		25
#define CLK_MOUT_EPLL			26
#define CLK_MOUT_EBI_1			27
#define CLK_MOUT_ISP_PLL		28
#define CLK_MOUT_DISP_PLL		29
#define CLK_MOUT_MPLL_USER_T		30
#define CLK_MOUT_ACLK_400_MCUISP	31
#define CLK_MOUT_G3D_PLLSRC		32
#define CLK_MOUT_CSIS1			33
#define CLK_MOUT_CSIS0			34
#define CLK_MOUT_CAM1			35
#define CLK_MOUT_FIMC3_LCLK		36
#define CLK_MOUT_FIMC2_LCLK		37
#define CLK_MOUT_FIMC1_LCLK		38
#define CLK_MOUT_FIMC0_LCLK		39
#define CLK_MOUT_MFC			40
#define CLK_MOUT_MFC_1			41
#define CLK_MOUT_MFC_0			42
#define CLK_MOUT_G3D			43
#define CLK_MOUT_G3D_1			44
#define CLK_MOUT_G3D_0			45
#define CLK_MOUT_MIPI0			46
#define CLK_MOUT_FIMD0			47
#define CLK_MOUT_TSADC_ISP		48
#define CLK_MOUT_UART_ISP		49
#define CLK_MOUT_SPI1_ISP		50
#define CLK_MOUT_SPI0_ISP		51
#define CLK_MOUT_PWM_ISP		52
#define CLK_MOUT_AUDIO0			53
#define CLK_MOUT_TSADC			54
#define CLK_MOUT_MMC2			55
#define CLK_MOUT_MMC1			56
#define CLK_MOUT_MMC0			57
#define CLK_MOUT_UART3			58
#define CLK_MOUT_UART2			59
#define CLK_MOUT_UART1			60
#define CLK_MOUT_UART0			61
#define CLK_MOUT_SPI2			62
#define CLK_MOUT_SPI1			63
#define CLK_MOUT_SPI0			64
#define CLK_MOUT_SPDIF			65
#define CLK_MOUT_AUDIO2			66
#define CLK_MOUT_AUDIO1			67
#define CLK_MOUT_MPLL_USER_C		68
#define CLK_MOUT_HPM			69
#define CLK_MOUT_CORE			70
#define CLK_MOUT_APLL			71
#define CLK_MOUT_PXLASYNC_CSIS1_FIMC	72
#define CLK_MOUT_PXLASYNC_CSIS0_FIMC	73
#define CLK_MOUT_JPEG			74
#define CLK_MOUT_JPEG1			75
#define CLK_MOUT_JPEG0			76
#define CLK_MOUT_ACLK_ISP0_300		77
#define CLK_MOUT_ACLK_ISP0_400		78
#define CLK_MOUT_ACLK_ISP0_300_USER	79
#define CLK_MOUT_ACLK_ISP1_300		80
#define CLK_MOUT_ACLK_ISP1_300_USER	81
#define CLK_MOUT_HDMI			82

/* Dividers */
#define CLK_DIV_GPL			90
#define CLK_DIV_GDL			91
#define CLK_DIV_GPR			92
#define CLK_DIV_GDR			93
#define CLK_DIV_ACLK_400_MCUISP		94
#define CLK_DIV_EBI			95
#define CLK_DIV_ACLK_200		96
#define CLK_DIV_ACLK_160		97
#define CLK_DIV_ACLK_100		98
#define CLK_DIV_ACLK_266		99
#define CLK_DIV_CSIS1			100
#define CLK_DIV_CSIS0			101
#define CLK_DIV_CAM1			102
#define CLK_DIV_FIMC3_LCLK		103
#define CLK_DIV_FIMC2_LCLK		104
#define CLK_DIV_FIMC1_LCLK		105
#define CLK_DIV_FIMC0_LCLK		106
#define CLK_DIV_TV_BLK			107
#define CLK_DIV_MFC			108
#define CLK_DIV_G3D			109
#define CLK_DIV_MIPI0_PRE		110
#define CLK_DIV_MIPI0			111
#define CLK_DIV_FIMD0			112
#define CLK_DIV_UART_ISP		113
#define CLK_DIV_SPI1_ISP_PRE		114
#define CLK_DIV_SPI1_ISP		115
#define CLK_DIV_SPI0_ISP_PRE		116
#define CLK_DIV_SPI0_ISP		117
#define CLK_DIV_PWM_ISP			118
#define CLK_DIV_PCM0			119
#define CLK_DIV_AUDIO0			120
#define CLK_DIV_TSADC_PRE		121
#define CLK_DIV_TSADC			122
#define CLK_DIV_MMC1_PRE		123
#define CLK_DIV_MMC1			124
#define CLK_DIV_MMC0_PRE		125
#define CLK_DIV_MMC0			126
#define CLK_DIV_MMC2_PRE		127
#define CLK_DIV_MMC2			128
#define CLK_DIV_UART3			129
#define CLK_DIV_UART2			130
#define CLK_DIV_UART1			131
#define CLK_DIV_UART0			132
#define CLK_DIV_SPI1_PRE		133
#define CLK_DIV_SPI1			134
#define CLK_DIV_SPI0_PRE		135
#define CLK_DIV_SPI0			136
#define CLK_DIV_SPI2_PRE		137
#define CLK_DIV_SPI2			138
#define CLK_DIV_PCM2			139
#define CLK_DIV_AUDIO2			140
#define CLK_DIV_PCM1			141
#define CLK_DIV_AUDIO1			142
#define CLK_DIV_I2S1			143
#define CLK_DIV_PXLASYNC_CSIS1_FIMC	144
#define CLK_DIV_PXLASYNC_CSIS0_FIMC	145
#define CLK_DIV_JPEG			146
#define CLK_DIV_CORE2			147
#define CLK_DIV_APLL			148
#define CLK_DIV_PCLK_DBG		149
#define CLK_DIV_ATB			150
#define CLK_DIV_PERIPH			151
#define CLK_DIV_COREM1			152
#define CLK_DIV_COREM0			153
#define CLK_DIV_CORE			154
#define CLK_DIV_HPM			155
#define CLK_DIV_COPY			156

/* Gates */
#define CLK_ASYNC_G3D			180
#define CLK_ASYNC_MFCL			181
#define CLK_ASYNC_TVX			182
#define CLK_PPMULEFT			183
#define CLK_GPIO_LEFT			184
#define CLK_PPMUIMAGE			185
#define CLK_QEMDMA2			186
#define CLK_QEROTATOR			187
#define CLK_SMMUMDMA2			188
#define CLK_SMMUROTATOR			189
#define CLK_MDMA2			190
#define CLK_ROTATOR			191
#define CLK_ASYNC_ISPMX			192
#define CLK_ASYNC_MAUDIOX		193
#define CLK_ASYNC_MFCR			194
#define CLK_ASYNC_FSYSD			195
#define CLK_ASYNC_LCD0X			196
#define CLK_ASYNC_CAMX			197
#define CLK_PPMURIGHT			198
#define CLK_GPIO_RIGHT			199
#define CLK_ANTIRBK_APBIF		200
#define CLK_EFUSE_WRITER_APBIF		201
#define CLK_MONOCNT			202
#define CLK_TZPC6			203
#define CLK_PROVISIONKEY1		204
#define CLK_PROVISIONKEY0		205
#define CLK_CMU_ISPPART			206
#define CLK_TMU_APBIF			207
#define CLK_KEYIF			208
#define CLK_RTC				209
#define CLK_WDT				210
#define CLK_MCT				211
#define CLK_SECKEY			212
#define CLK_HDMI_CEC			213
#define CLK_TZPC5			214
#define CLK_TZPC4			215
#define CLK_TZPC3			216
#define CLK_TZPC2			217
#define CLK_TZPC1			218
#define CLK_TZPC0			219
#define CLK_CMU_COREPART		220
#define CLK_CMU_TOPPART			221
#define CLK_PMU_APBIF			222
#define CLK_SYSREG			223
#define CLK_CHIP_ID			224
#define CLK_SMMUFIMC_LITE2		225
#define CLK_FIMC_LITE2			226
#define CLK_PIXELASYNCM1		227
#define CLK_PIXELASYNCM0		228
#define CLK_PPMUCAMIF			229
#define CLK_SMMUJPEG			230
#define CLK_SMMUFIMC3			231
#define CLK_SMMUFIMC2			232
#define CLK_SMMUFIMC1			233
#define CLK_SMMUFIMC0			234
#define CLK_JPEG			235
#define CLK_CSIS1			236
#define CLK_CSIS0			237
#define CLK_FIMC3			238
#define CLK_FIMC2			239
#define CLK_FIMC1			240
#define CLK_FIMC0			241
#define CLK_PPMUTV			242
#define CLK_SMMUTV			243
#define CLK_HDMI			244
#define CLK_MIXER			245
#define CLK_VP				246
#define CLK_PPMUMFC_R			247
#define CLK_PPMUMFC_L			248
#define CLK_SMMUMFC_R			249
#define CLK_SMMUMFC_L			250
#define CLK_MFC				251
#define CLK_PPMUG3D			252
#define CLK_G3D				253
#define CLK_PPMULCD0			254
#define CLK_SMMUFIMD0			255
#define CLK_DSIM0			256
#define CLK_SMIES			257
#define CLK_MIE0			258
#define CLK_FIMD0			259
#define CLK_TSADC			260
#define CLK_PPMUFILE			261
#define CLK_NFCON			262
#define CLK_USBDEVICE			263
#define CLK_USBHOST			264
#define CLK_SROMC			265
#define CLK_SDMMC2			266
#define CLK_SDMMC1			267
#define CLK_SDMMC0			268
#define CLK_PDMA1			269
#define CLK_PDMA0			270
#define CLK_SPDIF			271
#define CLK_PWM				272
#define CLK_PCM2			273
#define CLK_PCM1			274
#define CLK_I2S1			275
#define CLK_SPI2			276
#define CLK_SPI1			277
#define CLK_SPI0			278
#define CLK_I2CHDMI			279
#define CLK_I2C7			280
#define CLK_I2C6			281
#define CLK_I2C5			282
#define CLK_I2C4			283
#define CLK_I2C3			284
#define CLK_I2C2			285
#define CLK_I2C1			286
#define CLK_I2C0			287
#define CLK_UART3			288
#define CLK_UART2			289
#define CLK_UART1			290
#define CLK_UART0			291

/* Special clocks */
#define CLK_SCLK_PXLAYSNC_CSIS1_FIMC	330
#define CLK_SCLK_PXLAYSNC_CSIS0_FIMC	331
#define CLK_SCLK_JPEG			332
#define CLK_SCLK_CSIS1			333
#define CLK_SCLK_CSIS0			334
#define CLK_SCLK_CAM1			335
#define CLK_SCLK_FIMC3_LCLK		336
#define CLK_SCLK_FIMC2_LCLK		337
#define CLK_SCLK_FIMC1_LCLK		338
#define CLK_SCLK_FIMC0_LCLK		339
#define CLK_SCLK_PIXEL			340
#define CLK_SCLK_HDMI			341
#define CLK_SCLK_MIXER			342
#define CLK_SCLK_MFC			343
#define CLK_SCLK_G3D			344
#define CLK_SCLK_MIPIDPHY4L		345
#define CLK_SCLK_MIPI0			346
#define CLK_SCLK_MDNIE0			347
#define CLK_SCLK_FIMD0			348
#define CLK_SCLK_PCM0			349
#define CLK_SCLK_AUDIO0			350
#define CLK_SCLK_TSADC			351
#define CLK_SCLK_EBI			352
#define CLK_SCLK_MMC2			353
#define CLK_SCLK_MMC1			354
#define CLK_SCLK_MMC0			355
#define CLK_SCLK_I2S			356
#define CLK_SCLK_PCM2			357
#define CLK_SCLK_PCM1			358
#define CLK_SCLK_AUDIO2			359
#define CLK_SCLK_AUDIO1			360
#define CLK_SCLK_SPDIF			361
#define CLK_SCLK_SPI2			362
#define CLK_SCLK_SPI1			363
#define CLK_SCLK_SPI0			364
#define CLK_SCLK_UART3			365
#define CLK_SCLK_UART2			366
#define CLK_SCLK_UART1			367
#define CLK_SCLK_UART0			368
#define CLK_SCLK_HDMIPHY		369

/*
 * Total number of clocks of main CMU.
 * NOTE: Must be equal to last clock ID increased by one.
 */
#define CLK_NR_CLKS			370

/*
 * CMU DMC
 */
#define CLK_DMC_FOUT_MPLL		1
#define CLK_DMC_FOUT_BPLL		2

#define CLK_DMC_MOUT_MPLL		3
#define CLK_DMC_MOUT_BPLL		4
#define CLK_DMC_MOUT_DPHY		5
#define CLK_DMC_MOUT_DMC_BUS		6

#define CLK_DMC_DIV_DMC			7
#define CLK_DMC_DIV_DPHY		8
#define CLK_DMC_DIV_DMC_PRE		9
#define CLK_DMC_DIV_DMCP		10
#define CLK_DMC_DIV_DMCD		11
#define CLK_DMC_DIV_MPLL_PRE		12

/*
 * Total number of clocks of CMU_DMC.
 * NOTE: Must be equal to highest clock ID increased by one.
 */
#define NR_CLKS_DMC			13

#endif /* _DT_BINDINGS_CLOCK_SAMSUNG_EXYNOS4415_CLOCK_H */
