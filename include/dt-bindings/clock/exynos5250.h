/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Author: Andrzej Hajda <a.hajda@samsung.com>
 *
 * Device Tree binding constants for Exynos5250 clock controller.
 */

#ifndef _DT_BINDINGS_CLOCK_EXYNOS_5250_H
#define _DT_BINDINGS_CLOCK_EXYNOS_5250_H

/* core clocks */
#define CLK_FIN_PLL		1
#define CLK_FOUT_APLL		2
#define CLK_FOUT_MPLL		3
#define CLK_FOUT_BPLL		4
#define CLK_FOUT_GPLL		5
#define CLK_FOUT_CPLL		6
#define CLK_FOUT_EPLL		7
#define CLK_FOUT_VPLL		8
#define CLK_ARM_CLK		9
#define CLK_DIV_ARM2		10

/* gate for special clocks (sclk) */
#define CLK_SCLK_CAM_BAYER	128
#define CLK_SCLK_CAM0		129
#define CLK_SCLK_CAM1		130
#define CLK_SCLK_GSCL_WA	131
#define CLK_SCLK_GSCL_WB	132
#define CLK_SCLK_FIMD1		133
#define CLK_SCLK_MIPI1		134
#define CLK_SCLK_DP		135
#define CLK_SCLK_HDMI		136
#define CLK_SCLK_PIXEL		137
#define CLK_SCLK_AUDIO0		138
#define CLK_SCLK_MMC0		139
#define CLK_SCLK_MMC1		140
#define CLK_SCLK_MMC2		141
#define CLK_SCLK_MMC3		142
#define CLK_SCLK_SATA		143
#define CLK_SCLK_USB3		144
#define CLK_SCLK_JPEG		145
#define CLK_SCLK_UART0		146
#define CLK_SCLK_UART1		147
#define CLK_SCLK_UART2		148
#define CLK_SCLK_UART3		149
#define CLK_SCLK_PWM		150
#define CLK_SCLK_AUDIO1		151
#define CLK_SCLK_AUDIO2		152
#define CLK_SCLK_SPDIF		153
#define CLK_SCLK_SPI0		154
#define CLK_SCLK_SPI1		155
#define CLK_SCLK_SPI2		156
#define CLK_DIV_I2S1		157
#define CLK_DIV_I2S2		158
#define CLK_SCLK_HDMIPHY	159
#define CLK_DIV_PCM0		160

/* gate clocks */
#define CLK_GSCL0		256
#define CLK_GSCL1		257
#define CLK_GSCL2		258
#define CLK_GSCL3		259
#define CLK_GSCL_WA		260
#define CLK_GSCL_WB		261
#define CLK_SMMU_GSCL0		262
#define CLK_SMMU_GSCL1		263
#define CLK_SMMU_GSCL2		264
#define CLK_SMMU_GSCL3		265
#define CLK_MFC			266
#define CLK_SMMU_MFCL		267
#define CLK_SMMU_MFCR		268
#define CLK_ROTATOR		269
#define CLK_JPEG		270
#define CLK_MDMA1		271
#define CLK_SMMU_ROTATOR	272
#define CLK_SMMU_JPEG		273
#define CLK_SMMU_MDMA1		274
#define CLK_PDMA0		275
#define CLK_PDMA1		276
#define CLK_SATA		277
#define CLK_USBOTG		278
#define CLK_MIPI_HSI		279
#define CLK_SDMMC0		280
#define CLK_SDMMC1		281
#define CLK_SDMMC2		282
#define CLK_SDMMC3		283
#define CLK_SROMC		284
#define CLK_USB2		285
#define CLK_USB3		286
#define CLK_SATA_PHYCTRL	287
#define CLK_SATA_PHYI2C		288
#define CLK_UART0		289
#define CLK_UART1		290
#define CLK_UART2		291
#define CLK_UART3		292
#define CLK_UART4		293
#define CLK_I2C0		294
#define CLK_I2C1		295
#define CLK_I2C2		296
#define CLK_I2C3		297
#define CLK_I2C4		298
#define CLK_I2C5		299
#define CLK_I2C6		300
#define CLK_I2C7		301
#define CLK_I2C_HDMI		302
#define CLK_ADC			303
#define CLK_SPI0		304
#define CLK_SPI1		305
#define CLK_SPI2		306
#define CLK_I2S1		307
#define CLK_I2S2		308
#define CLK_PCM1		309
#define CLK_PCM2		310
#define CLK_PWM			311
#define CLK_SPDIF		312
#define CLK_AC97		313
#define CLK_HSI2C0		314
#define CLK_HSI2C1		315
#define CLK_HSI2C2		316
#define CLK_HSI2C3		317
#define CLK_CHIPID		318
#define CLK_SYSREG		319
#define CLK_PMU			320
#define CLK_CMU_TOP		321
#define CLK_CMU_CORE		322
#define CLK_CMU_MEM		323
#define CLK_TZPC0		324
#define CLK_TZPC1		325
#define CLK_TZPC2		326
#define CLK_TZPC3		327
#define CLK_TZPC4		328
#define CLK_TZPC5		329
#define CLK_TZPC6		330
#define CLK_TZPC7		331
#define CLK_TZPC8		332
#define CLK_TZPC9		333
#define CLK_HDMI_CEC		334
#define CLK_MCT			335
#define CLK_WDT			336
#define CLK_RTC			337
#define CLK_TMU			338
#define CLK_FIMD1		339
#define CLK_MIE1		340
#define CLK_DSIM0		341
#define CLK_DP			342
#define CLK_MIXER		343
#define CLK_HDMI		344
#define CLK_G2D			345
#define CLK_MDMA0		346
#define CLK_SMMU_MDMA0		347
#define CLK_SSS			348
#define CLK_G3D			349
#define CLK_SMMU_TV		350
#define CLK_SMMU_FIMD1		351
#define CLK_SMMU_2D		352
#define CLK_SMMU_FIMC_ISP	353
#define CLK_SMMU_FIMC_DRC	354
#define CLK_SMMU_FIMC_SCC	355
#define CLK_SMMU_FIMC_SCP	356
#define CLK_SMMU_FIMC_FD	357
#define CLK_SMMU_FIMC_MCU	358
#define CLK_SMMU_FIMC_ODC	359
#define CLK_SMMU_FIMC_DIS0	360
#define CLK_SMMU_FIMC_DIS1	361
#define CLK_SMMU_FIMC_3DNR	362
#define CLK_SMMU_FIMC_LITE0	363
#define CLK_SMMU_FIMC_LITE1	364
#define CLK_CAMIF_TOP		365

/* mux clocks */
#define CLK_MOUT_HDMI		1024
#define CLK_MOUT_GPLL		1025
#define CLK_MOUT_ACLK200_DISP1_SUB	1026
#define CLK_MOUT_ACLK300_DISP1_SUB	1027
#define CLK_MOUT_APLL		1028
#define CLK_MOUT_MPLL		1029
#define CLK_MOUT_VPLLSRC	1030

#endif /* _DT_BINDINGS_CLOCK_EXYNOS_5250_H */
