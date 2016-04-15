/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Author: Andrzej Hajda <a.hajda@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Device Tree binding constants for Exynos5420 clock controller.
*/

#ifndef _DT_BINDINGS_CLOCK_EXYNOS_5420_H
#define _DT_BINDINGS_CLOCK_EXYNOS_5420_H

/* core clocks */
#define CLK_FIN_PLL		1
#define CLK_FOUT_APLL		2
#define CLK_FOUT_CPLL		3
#define CLK_FOUT_DPLL		4
#define CLK_FOUT_EPLL		5
#define CLK_FOUT_RPLL		6
#define CLK_FOUT_IPLL		7
#define CLK_FOUT_SPLL		8
#define CLK_FOUT_VPLL		9
#define CLK_FOUT_MPLL		10
#define CLK_FOUT_BPLL		11
#define CLK_FOUT_KPLL		12
#define CLK_ARM_CLK		13
#define CLK_KFC_CLK		14

/* gate for special clocks (sclk) */
#define CLK_SCLK_UART0		128
#define CLK_SCLK_UART1		129
#define CLK_SCLK_UART2		130
#define CLK_SCLK_UART3		131
#define CLK_SCLK_MMC0		132
#define CLK_SCLK_MMC1		133
#define CLK_SCLK_MMC2		134
#define CLK_SCLK_SPI0		135
#define CLK_SCLK_SPI1		136
#define CLK_SCLK_SPI2		137
#define CLK_SCLK_I2S1		138
#define CLK_SCLK_I2S2		139
#define CLK_SCLK_PCM1		140
#define CLK_SCLK_PCM2		141
#define CLK_SCLK_SPDIF		142
#define CLK_SCLK_HDMI		143
#define CLK_SCLK_PIXEL		144
#define CLK_SCLK_DP1		145
#define CLK_SCLK_MIPI1		146
#define CLK_SCLK_FIMD1		147
#define CLK_SCLK_MAUDIO0	148
#define CLK_SCLK_MAUPCM0	149
#define CLK_SCLK_USBD300	150
#define CLK_SCLK_USBD301	151
#define CLK_SCLK_USBPHY300	152
#define CLK_SCLK_USBPHY301	153
#define CLK_SCLK_UNIPRO		154
#define CLK_SCLK_PWM		155
#define CLK_SCLK_GSCL_WA	156
#define CLK_SCLK_GSCL_WB	157
#define CLK_SCLK_HDMIPHY	158
#define CLK_MAU_EPLL		159
#define CLK_SCLK_HSIC_12M	160
#define CLK_SCLK_MPHY_IXTAL24	161

/* gate clocks */
#define CLK_UART0		257
#define CLK_UART1		258
#define CLK_UART2		259
#define CLK_UART3		260
#define CLK_I2C0		261
#define CLK_I2C1		262
#define CLK_I2C2		263
#define CLK_I2C3		264
#define CLK_USI0		265
#define CLK_USI1		266
#define CLK_USI2		267
#define CLK_USI3		268
#define CLK_I2C_HDMI		269
#define CLK_TSADC		270
#define CLK_SPI0		271
#define CLK_SPI1		272
#define CLK_SPI2		273
#define CLK_KEYIF		274
#define CLK_I2S1		275
#define CLK_I2S2		276
#define CLK_PCM1		277
#define CLK_PCM2		278
#define CLK_PWM			279
#define CLK_SPDIF		280
#define CLK_USI4		281
#define CLK_USI5		282
#define CLK_USI6		283
#define CLK_ACLK66_PSGEN	300
#define CLK_CHIPID		301
#define CLK_SYSREG		302
#define CLK_TZPC0		303
#define CLK_TZPC1		304
#define CLK_TZPC2		305
#define CLK_TZPC3		306
#define CLK_TZPC4		307
#define CLK_TZPC5		308
#define CLK_TZPC6		309
#define CLK_TZPC7		310
#define CLK_TZPC8		311
#define CLK_TZPC9		312
#define CLK_HDMI_CEC		313
#define CLK_SECKEY		314
#define CLK_MCT			315
#define CLK_WDT			316
#define CLK_RTC			317
#define CLK_TMU			318
#define CLK_TMU_GPU		319
#define CLK_PCLK66_GPIO		330
#define CLK_ACLK200_FSYS2	350
#define CLK_MMC0		351
#define CLK_MMC1		352
#define CLK_MMC2		353
#define CLK_SROMC		354
#define CLK_UFS			355
#define CLK_ACLK200_FSYS	360
#define CLK_TSI			361
#define CLK_PDMA0		362
#define CLK_PDMA1		363
#define CLK_RTIC		364
#define CLK_USBH20		365
#define CLK_USBD300		366
#define CLK_USBD301		367
#define CLK_ACLK400_MSCL	380
#define CLK_MSCL0		381
#define CLK_MSCL1		382
#define CLK_MSCL2		383
#define CLK_SMMU_MSCL0		384
#define CLK_SMMU_MSCL1		385
#define CLK_SMMU_MSCL2		386
#define CLK_ACLK333		400
#define CLK_MFC			401
#define CLK_SMMU_MFCL		402
#define CLK_SMMU_MFCR		403
#define CLK_ACLK200_DISP1	410
#define CLK_DSIM1		411
#define CLK_DP1			412
#define CLK_HDMI		413
#define CLK_ACLK300_DISP1	420
#define CLK_FIMD1		421
#define CLK_SMMU_FIMD1M0	422
#define CLK_SMMU_FIMD1M1	423
#define CLK_ACLK166		430
#define CLK_MIXER		431
#define CLK_ACLK266		440
#define CLK_ROTATOR		441
#define CLK_MDMA1		442
#define CLK_SMMU_ROTATOR	443
#define CLK_SMMU_MDMA1		444
#define CLK_ACLK300_JPEG	450
#define CLK_JPEG		451
#define CLK_JPEG2		452
#define CLK_SMMU_JPEG		453
#define CLK_SMMU_JPEG2		454
#define CLK_ACLK300_GSCL	460
#define CLK_SMMU_GSCL0		461
#define CLK_SMMU_GSCL1		462
#define CLK_GSCL_WA		463
#define CLK_GSCL_WB		464
#define CLK_GSCL0		465
#define CLK_GSCL1		466
#define CLK_FIMC_3AA		467
#define CLK_ACLK266_G2D		470
#define CLK_SSS			471
#define CLK_SLIM_SSS		472
#define CLK_MDMA0		473
#define CLK_ACLK333_G2D		480
#define CLK_G2D			481
#define CLK_ACLK333_432_GSCL	490
#define CLK_SMMU_3AA		491
#define CLK_SMMU_FIMCL0		492
#define CLK_SMMU_FIMCL1		493
#define CLK_SMMU_FIMCL3		494
#define CLK_FIMC_LITE3		495
#define CLK_FIMC_LITE0		496
#define CLK_FIMC_LITE1		497
#define CLK_ACLK_G3D		500
#define CLK_G3D			501
#define CLK_SMMU_MIXER		502
#define CLK_SMMU_G2D		503
#define CLK_SMMU_MDMA0		504
#define CLK_MC			505
#define CLK_TOP_RTC		506
#define CLK_SCLK_UART_ISP	510
#define CLK_SCLK_SPI0_ISP	511
#define CLK_SCLK_SPI1_ISP	512
#define CLK_SCLK_PWM_ISP	513
#define CLK_SCLK_ISP_SENSOR0	514
#define CLK_SCLK_ISP_SENSOR1	515
#define CLK_SCLK_ISP_SENSOR2	516
#define CLK_ACLK432_SCALER	517
#define CLK_ACLK432_CAM		518
#define CLK_ACLK_FL1550_CAM	519
#define CLK_ACLK550_CAM		520

/* mux clocks */
#define CLK_MOUT_HDMI		640
#define CLK_MOUT_G3D		641
#define CLK_MOUT_VPLL		642
#define CLK_MOUT_MAUDIO0	643
#define CLK_MOUT_USER_ACLK333	644
#define CLK_MOUT_SW_ACLK333	645
#define CLK_MOUT_USER_ACLK200_DISP1	646
#define CLK_MOUT_SW_ACLK200	647
#define CLK_MOUT_USER_ACLK300_DISP1     648
#define CLK_MOUT_SW_ACLK300     649
#define CLK_MOUT_USER_ACLK400_DISP1     650
#define CLK_MOUT_SW_ACLK400     651
#define CLK_MOUT_USER_ACLK300_GSCL	652
#define CLK_MOUT_SW_ACLK300_GSCL	653

/* divider clocks */
#define CLK_DOUT_PIXEL		768
#define CLK_DOUT_ACLK400_WCORE	769
#define CLK_DOUT_ACLK400_ISP	770
#define CLK_DOUT_ACLK400_MSCL	771
#define CLK_DOUT_ACLK200	772
#define CLK_DOUT_ACLK200_FSYS2	773
#define CLK_DOUT_ACLK100_NOC	774
#define CLK_DOUT_PCLK200_FSYS	775
#define CLK_DOUT_ACLK200_FSYS	776
#define CLK_DOUT_ACLK333_432_GSCL	777
#define CLK_DOUT_ACLK333_432_ISP	778
#define CLK_DOUT_ACLK66		779
#define CLK_DOUT_ACLK333_432_ISP0	780
#define CLK_DOUT_ACLK266	781
#define CLK_DOUT_ACLK166	782
#define CLK_DOUT_ACLK333	783
#define CLK_DOUT_ACLK333_G2D	784
#define CLK_DOUT_ACLK266_G2D	785
#define CLK_DOUT_ACLK_G3D	786
#define CLK_DOUT_ACLK300_JPEG	787
#define CLK_DOUT_ACLK300_DISP1	788
#define CLK_DOUT_ACLK300_GSCL	789
#define CLK_DOUT_ACLK400_DISP1	790

/* must be greater than maximal clock id */
#define CLK_NR_CLKS		791

#endif /* _DT_BINDINGS_CLOCK_EXYNOS_5420_H */
