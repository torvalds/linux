/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * 	Author: Tomasz Figa <t.figa@samsung.com>
 *
 * Device Tree binding constants for Samsung Exynos3250 clock controllers.
 */

#ifndef _DT_BINDINGS_CLOCK_SAMSUNG_EXYNOS3250_CLOCK_H
#define _DT_BINDINGS_CLOCK_SAMSUNG_EXYNOS3250_CLOCK_H

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
#define CLK_FOUT_VPLL			4
#define CLK_FOUT_UPLL			5
#define CLK_FOUT_MPLL			6
#define CLK_ARM_CLK			7

/* Muxes */
#define CLK_MOUT_MPLL_USER_L		16
#define CLK_MOUT_GDL			17
#define CLK_MOUT_MPLL_USER_R		18
#define CLK_MOUT_GDR			19
#define CLK_MOUT_EBI			20
#define CLK_MOUT_ACLK_200		21
#define CLK_MOUT_ACLK_160		22
#define CLK_MOUT_ACLK_100		23
#define CLK_MOUT_ACLK_266_1		24
#define CLK_MOUT_ACLK_266_0		25
#define CLK_MOUT_ACLK_266		26
#define CLK_MOUT_VPLL			27
#define CLK_MOUT_EPLL_USER		28
#define CLK_MOUT_EBI_1			29
#define CLK_MOUT_UPLL			30
#define CLK_MOUT_ACLK_400_MCUISP_SUB	31
#define CLK_MOUT_MPLL			32
#define CLK_MOUT_ACLK_400_MCUISP	33
#define CLK_MOUT_VPLLSRC		34
#define CLK_MOUT_CAM1			35
#define CLK_MOUT_CAM_BLK		36
#define CLK_MOUT_MFC			37
#define CLK_MOUT_MFC_1			38
#define CLK_MOUT_MFC_0			39
#define CLK_MOUT_G3D			40
#define CLK_MOUT_G3D_1			41
#define CLK_MOUT_G3D_0			42
#define CLK_MOUT_MIPI0			43
#define CLK_MOUT_FIMD0			44
#define CLK_MOUT_UART_ISP		45
#define CLK_MOUT_SPI1_ISP		46
#define CLK_MOUT_SPI0_ISP		47
#define CLK_MOUT_TSADC			48
#define CLK_MOUT_MMC1			49
#define CLK_MOUT_MMC0			50
#define CLK_MOUT_UART1			51
#define CLK_MOUT_UART0			52
#define CLK_MOUT_SPI1			53
#define CLK_MOUT_SPI0			54
#define CLK_MOUT_AUDIO			55
#define CLK_MOUT_MPLL_USER_C		56
#define CLK_MOUT_HPM			57
#define CLK_MOUT_CORE			58
#define CLK_MOUT_APLL			59
#define CLK_MOUT_ACLK_266_SUB		60
#define CLK_MOUT_UART2			61
#define CLK_MOUT_MMC2			62

/* Dividers */
#define CLK_DIV_GPL			64
#define CLK_DIV_GDL			65
#define CLK_DIV_GPR			66
#define CLK_DIV_GDR			67
#define CLK_DIV_MPLL_PRE		68
#define CLK_DIV_ACLK_400_MCUISP		69
#define CLK_DIV_EBI			70
#define CLK_DIV_ACLK_200		71
#define CLK_DIV_ACLK_160		72
#define CLK_DIV_ACLK_100		73
#define CLK_DIV_ACLK_266		74
#define CLK_DIV_CAM1			75
#define CLK_DIV_CAM_BLK			76
#define CLK_DIV_MFC			77
#define CLK_DIV_G3D			78
#define CLK_DIV_MIPI0_PRE		79
#define CLK_DIV_MIPI0			80
#define CLK_DIV_FIMD0			81
#define CLK_DIV_UART_ISP		82
#define CLK_DIV_SPI1_ISP_PRE		83
#define CLK_DIV_SPI1_ISP		84
#define CLK_DIV_SPI0_ISP_PRE		85
#define CLK_DIV_SPI0_ISP		86
#define CLK_DIV_TSADC_PRE		87
#define CLK_DIV_TSADC			88
#define CLK_DIV_MMC1_PRE		89
#define CLK_DIV_MMC1			90
#define CLK_DIV_MMC0_PRE		91
#define CLK_DIV_MMC0			92
#define CLK_DIV_UART1			93
#define CLK_DIV_UART0			94
#define CLK_DIV_SPI1_PRE		95
#define CLK_DIV_SPI1			96
#define CLK_DIV_SPI0_PRE		97
#define CLK_DIV_SPI0			98
#define CLK_DIV_PCM			99
#define CLK_DIV_AUDIO			100
#define CLK_DIV_I2S			101
#define CLK_DIV_CORE2			102
#define CLK_DIV_APLL			103
#define CLK_DIV_PCLK_DBG		104
#define CLK_DIV_ATB			105
#define CLK_DIV_COREM			106
#define CLK_DIV_CORE			107
#define CLK_DIV_HPM			108
#define CLK_DIV_COPY			109
#define CLK_DIV_UART2			110
#define CLK_DIV_MMC2_PRE		111
#define CLK_DIV_MMC2			112

/* Gates */
#define CLK_ASYNC_G3D			128
#define CLK_ASYNC_MFCL			129
#define CLK_PPMULEFT			130
#define CLK_GPIO_LEFT			131
#define CLK_ASYNC_ISPMX			132
#define CLK_ASYNC_FSYSD			133
#define CLK_ASYNC_LCD0X			134
#define CLK_ASYNC_CAMX			135
#define CLK_PPMURIGHT			136
#define CLK_GPIO_RIGHT			137
#define CLK_MONOCNT			138
#define CLK_TZPC6			139
#define CLK_PROVISIONKEY1		140
#define CLK_PROVISIONKEY0		141
#define CLK_CMU_ISPPART			142
#define CLK_TMU_APBIF			143
#define CLK_KEYIF			144
#define CLK_RTC				145
#define CLK_WDT				146
#define CLK_MCT				147
#define CLK_SECKEY			148
#define CLK_TZPC5			149
#define CLK_TZPC4			150
#define CLK_TZPC3			151
#define CLK_TZPC2			152
#define CLK_TZPC1			153
#define CLK_TZPC0			154
#define CLK_CMU_COREPART		155
#define CLK_CMU_TOPPART			156
#define CLK_PMU_APBIF			157
#define CLK_SYSREG			158
#define CLK_CHIP_ID			159
#define CLK_QEJPEG			160
#define CLK_PIXELASYNCM1		161
#define CLK_PIXELASYNCM0		162
#define CLK_PPMUCAMIF			163
#define CLK_QEM2MSCALER			164
#define CLK_QEGSCALER1			165
#define CLK_QEGSCALER0			166
#define CLK_SMMUJPEG			167
#define CLK_SMMUM2M2SCALER		168
#define CLK_SMMUGSCALER1		169
#define CLK_SMMUGSCALER0		170
#define CLK_JPEG			171
#define CLK_M2MSCALER			172
#define CLK_GSCALER1			173
#define CLK_GSCALER0			174
#define CLK_QEMFC			175
#define CLK_PPMUMFC_L			176
#define CLK_SMMUMFC_L			177
#define CLK_MFC				178
#define CLK_SMMUG3D			179
#define CLK_QEG3D			180
#define CLK_PPMUG3D			181
#define CLK_G3D				182
#define CLK_QE_CH1_LCD			183
#define CLK_QE_CH0_LCD			184
#define CLK_PPMULCD0			185
#define CLK_SMMUFIMD0			186
#define CLK_DSIM0			187
#define CLK_FIMD0			188
#define CLK_CAM1			189
#define CLK_UART_ISP_TOP		190
#define CLK_SPI1_ISP_TOP		191
#define CLK_SPI0_ISP_TOP		192
#define CLK_TSADC			193
#define CLK_PPMUFILE			194
#define CLK_USBOTG			195
#define CLK_USBHOST			196
#define CLK_SROMC			197
#define CLK_SDMMC1			198
#define CLK_SDMMC0			199
#define CLK_PDMA1			200
#define CLK_PDMA0			201
#define CLK_PWM				202
#define CLK_PCM				203
#define CLK_I2S				204
#define CLK_SPI1			205
#define CLK_SPI0			206
#define CLK_I2C7			207
#define CLK_I2C6			208
#define CLK_I2C5			209
#define CLK_I2C4			210
#define CLK_I2C3			211
#define CLK_I2C2			212
#define CLK_I2C1			213
#define CLK_I2C0			214
#define CLK_UART1			215
#define CLK_UART0			216
#define CLK_BLOCK_LCD			217
#define CLK_BLOCK_G3D			218
#define CLK_BLOCK_MFC			219
#define CLK_BLOCK_CAM			220
#define CLK_SMIES			221
#define CLK_UART2			222
#define CLK_SDMMC2			223

/* Special clocks */
#define CLK_SCLK_JPEG			224
#define CLK_SCLK_M2MSCALER		225
#define CLK_SCLK_GSCALER1		226
#define CLK_SCLK_GSCALER0		227
#define CLK_SCLK_MFC			228
#define CLK_SCLK_G3D			229
#define CLK_SCLK_MIPIDPHY2L		230
#define CLK_SCLK_MIPI0			231
#define CLK_SCLK_FIMD0			232
#define CLK_SCLK_CAM1			233
#define CLK_SCLK_UART_ISP		234
#define CLK_SCLK_SPI1_ISP		235
#define CLK_SCLK_SPI0_ISP		236
#define CLK_SCLK_UPLL			237
#define CLK_SCLK_TSADC			238
#define CLK_SCLK_EBI			239
#define CLK_SCLK_MMC1			240
#define CLK_SCLK_MMC0			241
#define CLK_SCLK_I2S			242
#define CLK_SCLK_PCM			243
#define CLK_SCLK_SPI1			244
#define CLK_SCLK_SPI0			245
#define CLK_SCLK_UART1			246
#define CLK_SCLK_UART0			247
#define CLK_SCLK_UART2			248
#define CLK_SCLK_MMC2			249

/*
 * CMU DMC
 */

#define CLK_FOUT_BPLL			1
#define CLK_FOUT_EPLL			2

/* Muxes */
#define CLK_MOUT_MPLL_MIF		8
#define CLK_MOUT_BPLL			9
#define CLK_MOUT_DPHY			10
#define CLK_MOUT_DMC_BUS		11
#define CLK_MOUT_EPLL			12

/* Dividers */
#define CLK_DIV_DMC			16
#define CLK_DIV_DPHY			17
#define CLK_DIV_DMC_PRE			18
#define CLK_DIV_DMCP			19
#define CLK_DIV_DMCD			20

/*
 * CMU ISP
 */

/* Dividers */

#define CLK_DIV_ISP1			1
#define CLK_DIV_ISP0			2
#define CLK_DIV_MCUISP1			3
#define CLK_DIV_MCUISP0			4
#define CLK_DIV_MPWM			5

/* Gates */

#define CLK_UART_ISP			8
#define CLK_WDT_ISP			9
#define CLK_PWM_ISP			10
#define CLK_I2C1_ISP			11
#define CLK_I2C0_ISP			12
#define CLK_MPWM_ISP			13
#define CLK_MCUCTL_ISP			14
#define CLK_PPMUISPX			15
#define CLK_PPMUISPMX			16
#define CLK_QE_LITE1			17
#define CLK_QE_LITE0			18
#define CLK_QE_FD			19
#define CLK_QE_DRC			20
#define CLK_QE_ISP			21
#define CLK_CSIS1			22
#define CLK_SMMU_LITE1			23
#define CLK_SMMU_LITE0			24
#define CLK_SMMU_FD			25
#define CLK_SMMU_DRC			26
#define CLK_SMMU_ISP			27
#define CLK_GICISP			28
#define CLK_CSIS0			29
#define CLK_MCUISP			30
#define CLK_LITE1			31
#define CLK_LITE0			32
#define CLK_FD				33
#define CLK_DRC				34
#define CLK_ISP				35
#define CLK_QE_ISPCX			36
#define CLK_QE_SCALERP			37
#define CLK_QE_SCALERC			38
#define CLK_SMMU_SCALERP		39
#define CLK_SMMU_SCALERC		40
#define CLK_SCALERP			41
#define CLK_SCALERC			42
#define CLK_SPI1_ISP			43
#define CLK_SPI0_ISP			44
#define CLK_SMMU_ISPCX			45
#define CLK_ASYNCAXIM			46
#define CLK_SCLK_MPWM_ISP		47

#endif /* _DT_BINDINGS_CLOCK_SAMSUNG_EXYNOS3250_CLOCK_H */
