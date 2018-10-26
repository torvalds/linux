/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Author: Andrzej Hajda <a.hajda@samsung.com>
 *
 * Device Tree binding constants for Exynos4 clock controller.
 */

#ifndef _DT_BINDINGS_CLOCK_EXYNOS_4_H
#define _DT_BINDINGS_CLOCK_EXYNOS_4_H

/* core clocks */
#define CLK_XXTI		1
#define CLK_XUSBXTI		2
#define CLK_FIN_PLL		3
#define CLK_FOUT_APLL		4
#define CLK_FOUT_MPLL		5
#define CLK_FOUT_EPLL		6
#define CLK_FOUT_VPLL		7
#define CLK_SCLK_APLL		8
#define CLK_SCLK_MPLL		9
#define CLK_SCLK_EPLL		10
#define CLK_SCLK_VPLL		11
#define CLK_ARM_CLK		12
#define CLK_ACLK200		13
#define CLK_ACLK100		14
#define CLK_ACLK160		15
#define CLK_ACLK133		16
#define CLK_MOUT_MPLL_USER_T	17 /* Exynos4x12 only */
#define CLK_MOUT_MPLL_USER_C	18 /* Exynos4x12 only */
#define CLK_MOUT_CORE		19
#define CLK_MOUT_APLL		20
#define CLK_SCLK_HDMIPHY	22
#define CLK_OUT_DMC		23
#define CLK_OUT_TOP		24
#define CLK_OUT_LEFTBUS		25
#define CLK_OUT_RIGHTBUS	26
#define CLK_OUT_CPU		27

/* gate for special clocks (sclk) */
#define CLK_SCLK_FIMC0		128
#define CLK_SCLK_FIMC1		129
#define CLK_SCLK_FIMC2		130
#define CLK_SCLK_FIMC3		131
#define CLK_SCLK_CAM0		132
#define CLK_SCLK_CAM1		133
#define CLK_SCLK_CSIS0		134
#define CLK_SCLK_CSIS1		135
#define CLK_SCLK_HDMI		136
#define CLK_SCLK_MIXER		137
#define CLK_SCLK_DAC		138
#define CLK_SCLK_PIXEL		139
#define CLK_SCLK_FIMD0		140
#define CLK_SCLK_MDNIE0		141 /* Exynos4412 only */
#define CLK_SCLK_MDNIE_PWM0	142
#define CLK_SCLK_MIPI0		143
#define CLK_SCLK_AUDIO0		144
#define CLK_SCLK_MMC0		145
#define CLK_SCLK_MMC1		146
#define CLK_SCLK_MMC2		147
#define CLK_SCLK_MMC3		148
#define CLK_SCLK_MMC4		149
#define CLK_SCLK_SATA		150 /* Exynos4210 only */
#define CLK_SCLK_UART0		151
#define CLK_SCLK_UART1		152
#define CLK_SCLK_UART2		153
#define CLK_SCLK_UART3		154
#define CLK_SCLK_UART4		155
#define CLK_SCLK_AUDIO1		156
#define CLK_SCLK_AUDIO2		157
#define CLK_SCLK_SPDIF		158
#define CLK_SCLK_SPI0		159
#define CLK_SCLK_SPI1		160
#define CLK_SCLK_SPI2		161
#define CLK_SCLK_SLIMBUS	162
#define CLK_SCLK_FIMD1		163 /* Exynos4210 only */
#define CLK_SCLK_MIPI1		164 /* Exynos4210 only */
#define CLK_SCLK_PCM1		165
#define CLK_SCLK_PCM2		166
#define CLK_SCLK_I2S1		167
#define CLK_SCLK_I2S2		168
#define CLK_SCLK_MIPIHSI	169 /* Exynos4412 only */
#define CLK_SCLK_MFC		170
#define CLK_SCLK_PCM0		171
#define CLK_SCLK_G3D		172
#define CLK_SCLK_PWM_ISP	173 /* Exynos4x12 only */
#define CLK_SCLK_SPI0_ISP	174 /* Exynos4x12 only */
#define CLK_SCLK_SPI1_ISP	175 /* Exynos4x12 only */
#define CLK_SCLK_UART_ISP	176 /* Exynos4x12 only */
#define CLK_SCLK_FIMG2D		177

/* gate clocks */
#define CLK_SSS			255
#define CLK_FIMC0		256
#define CLK_FIMC1		257
#define CLK_FIMC2		258
#define CLK_FIMC3		259
#define CLK_CSIS0		260
#define CLK_CSIS1		261
#define CLK_JPEG		262
#define CLK_SMMU_FIMC0		263
#define CLK_SMMU_FIMC1		264
#define CLK_SMMU_FIMC2		265
#define CLK_SMMU_FIMC3		266
#define CLK_SMMU_JPEG		267
#define CLK_VP			268
#define CLK_MIXER		269
#define CLK_TVENC		270 /* Exynos4210 only */
#define CLK_HDMI		271
#define CLK_SMMU_TV		272
#define CLK_MFC			273
#define CLK_SMMU_MFCL		274
#define CLK_SMMU_MFCR		275
#define CLK_G3D			276
#define CLK_G2D			277
#define CLK_ROTATOR		278
#define CLK_MDMA		279
#define CLK_SMMU_G2D		280
#define CLK_SMMU_ROTATOR	281
#define CLK_SMMU_MDMA		282
#define CLK_FIMD0		283
#define CLK_MIE0		284
#define CLK_MDNIE0		285 /* Exynos4412 only */
#define CLK_DSIM0		286
#define CLK_SMMU_FIMD0		287
#define CLK_FIMD1		288 /* Exynos4210 only */
#define CLK_MIE1		289 /* Exynos4210 only */
#define CLK_DSIM1		290 /* Exynos4210 only */
#define CLK_SMMU_FIMD1		291 /* Exynos4210 only */
#define CLK_PDMA0		292
#define CLK_PDMA1		293
#define CLK_PCIE_PHY		294
#define CLK_SATA_PHY		295 /* Exynos4210 only */
#define CLK_TSI			296
#define CLK_SDMMC0		297
#define CLK_SDMMC1		298
#define CLK_SDMMC2		299
#define CLK_SDMMC3		300
#define CLK_SDMMC4		301
#define CLK_SATA		302 /* Exynos4210 only */
#define CLK_SROMC		303
#define CLK_USB_HOST		304
#define CLK_USB_DEVICE		305
#define CLK_PCIE		306
#define CLK_ONENAND		307
#define CLK_NFCON		308
#define CLK_SMMU_PCIE		309
#define CLK_GPS			310
#define CLK_SMMU_GPS		311
#define CLK_UART0		312
#define CLK_UART1		313
#define CLK_UART2		314
#define CLK_UART3		315
#define CLK_UART4		316
#define CLK_I2C0		317
#define CLK_I2C1		318
#define CLK_I2C2		319
#define CLK_I2C3		320
#define CLK_I2C4		321
#define CLK_I2C5		322
#define CLK_I2C6		323
#define CLK_I2C7		324
#define CLK_I2C_HDMI		325
#define CLK_TSADC		326
#define CLK_SPI0		327
#define CLK_SPI1		328
#define CLK_SPI2		329
#define CLK_I2S1		330
#define CLK_I2S2		331
#define CLK_PCM0		332
#define CLK_I2S0		333
#define CLK_PCM1		334
#define CLK_PCM2		335
#define CLK_PWM			336
#define CLK_SLIMBUS		337
#define CLK_SPDIF		338
#define CLK_AC97		339
#define CLK_MODEMIF		340
#define CLK_CHIPID		341
#define CLK_SYSREG		342
#define CLK_HDMI_CEC		343
#define CLK_MCT			344
#define CLK_WDT			345
#define CLK_RTC			346
#define CLK_KEYIF		347
#define CLK_AUDSS		348
#define CLK_MIPI_HSI		349 /* Exynos4210 only */
#define CLK_PIXELASYNCM0	351
#define CLK_PIXELASYNCM1	352
#define CLK_FIMC_LITE0		353 /* Exynos4x12 only */
#define CLK_FIMC_LITE1		354 /* Exynos4x12 only */
#define CLK_PPMUISPX		355 /* Exynos4x12 only */
#define CLK_PPMUISPMX		356 /* Exynos4x12 only */
#define CLK_FIMC_ISP		357 /* Exynos4x12 only */
#define CLK_FIMC_DRC		358 /* Exynos4x12 only */
#define CLK_FIMC_FD		359 /* Exynos4x12 only */
#define CLK_MCUISP		360 /* Exynos4x12 only */
#define CLK_GICISP		361 /* Exynos4x12 only */
#define CLK_SMMU_ISP		362 /* Exynos4x12 only */
#define CLK_SMMU_DRC		363 /* Exynos4x12 only */
#define CLK_SMMU_FD		364 /* Exynos4x12 only */
#define CLK_SMMU_LITE0		365 /* Exynos4x12 only */
#define CLK_SMMU_LITE1		366 /* Exynos4x12 only */
#define CLK_MCUCTL_ISP		367 /* Exynos4x12 only */
#define CLK_MPWM_ISP		368 /* Exynos4x12 only */
#define CLK_I2C0_ISP		369 /* Exynos4x12 only */
#define CLK_I2C1_ISP		370 /* Exynos4x12 only */
#define CLK_MTCADC_ISP		371 /* Exynos4x12 only */
#define CLK_PWM_ISP		372 /* Exynos4x12 only */
#define CLK_WDT_ISP		373 /* Exynos4x12 only */
#define CLK_UART_ISP		374 /* Exynos4x12 only */
#define CLK_ASYNCAXIM		375 /* Exynos4x12 only */
#define CLK_SMMU_ISPCX		376 /* Exynos4x12 only */
#define CLK_SPI0_ISP		377 /* Exynos4x12 only */
#define CLK_SPI1_ISP		378 /* Exynos4x12 only */
#define CLK_PWM_ISP_SCLK	379 /* Exynos4x12 only */
#define CLK_SPI0_ISP_SCLK	380 /* Exynos4x12 only */
#define CLK_SPI1_ISP_SCLK	381 /* Exynos4x12 only */
#define CLK_UART_ISP_SCLK	382 /* Exynos4x12 only */
#define CLK_TMU_APBIF		383

/* mux clocks */
#define CLK_MOUT_FIMC0		384
#define CLK_MOUT_FIMC1		385
#define CLK_MOUT_FIMC2		386
#define CLK_MOUT_FIMC3		387
#define CLK_MOUT_CAM0		388
#define CLK_MOUT_CAM1		389
#define CLK_MOUT_CSIS0		390
#define CLK_MOUT_CSIS1		391
#define CLK_MOUT_G3D0		392
#define CLK_MOUT_G3D1		393
#define CLK_MOUT_G3D		394
#define CLK_ACLK400_MCUISP	395 /* Exynos4x12 only */
#define CLK_MOUT_HDMI		396
#define CLK_MOUT_MIXER		397

/* gate clocks - ppmu */
#define CLK_PPMULEFT		400
#define CLK_PPMURIGHT		401
#define CLK_PPMUCAMIF		402
#define CLK_PPMUTV		403
#define CLK_PPMUMFC_L		404
#define CLK_PPMUMFC_R		405
#define CLK_PPMUG3D		406
#define CLK_PPMUIMAGE		407
#define CLK_PPMULCD0		408
#define CLK_PPMULCD1		409 /* Exynos4210 only */
#define CLK_PPMUFILE		410
#define CLK_PPMUGPS		411
#define CLK_PPMUDMC0		412
#define CLK_PPMUDMC1		413
#define CLK_PPMUCPU		414
#define CLK_PPMUACP		415

/* div clocks */
#define CLK_DIV_ISP0		450 /* Exynos4x12 only */
#define CLK_DIV_ISP1		451 /* Exynos4x12 only */
#define CLK_DIV_MCUISP0		452 /* Exynos4x12 only */
#define CLK_DIV_MCUISP1		453 /* Exynos4x12 only */
#define CLK_DIV_ACLK200		454 /* Exynos4x12 only */
#define CLK_DIV_ACLK400_MCUISP	455 /* Exynos4x12 only */
#define CLK_DIV_ACP		456
#define CLK_DIV_DMC		457
#define CLK_DIV_C2C		458 /* Exynos4x12 only */
#define CLK_DIV_GDL		459
#define CLK_DIV_GDR		460

/* must be greater than maximal clock id */
#define CLK_NR_CLKS		461

/* Exynos4x12 ISP clocks */
#define CLK_ISP_FIMC_ISP		 1
#define CLK_ISP_FIMC_DRC		 2
#define CLK_ISP_FIMC_FD			 3
#define CLK_ISP_FIMC_LITE0		 4
#define CLK_ISP_FIMC_LITE1		 5
#define CLK_ISP_MCUISP			 6
#define CLK_ISP_GICISP			 7
#define CLK_ISP_SMMU_ISP		 8
#define CLK_ISP_SMMU_DRC		 9
#define CLK_ISP_SMMU_FD			10
#define CLK_ISP_SMMU_LITE0		11
#define CLK_ISP_SMMU_LITE1		12
#define CLK_ISP_PPMUISPMX		13
#define CLK_ISP_PPMUISPX		14
#define CLK_ISP_MCUCTL_ISP		15
#define CLK_ISP_MPWM_ISP		16
#define CLK_ISP_I2C0_ISP		17
#define CLK_ISP_I2C1_ISP		18
#define CLK_ISP_MTCADC_ISP		19
#define CLK_ISP_PWM_ISP			20
#define CLK_ISP_WDT_ISP			21
#define CLK_ISP_UART_ISP		22
#define CLK_ISP_ASYNCAXIM		23
#define CLK_ISP_SMMU_ISPCX		24
#define CLK_ISP_SPI0_ISP		25
#define CLK_ISP_SPI1_ISP		26

#define CLK_ISP_DIV_ISP0		27
#define CLK_ISP_DIV_ISP1		28
#define CLK_ISP_DIV_MCUISP0		29
#define CLK_ISP_DIV_MCUISP1		30

#define CLK_NR_ISP_CLKS			31

#endif /* _DT_BINDINGS_CLOCK_EXYNOS_4_H */
