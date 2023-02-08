/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 */

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RK3562_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RK3562_H

/* cru-clocks indices */

/* cru plls */
#define PLL_APLL			1
#define PLL_GPLL			2
#define PLL_VPLL			3
#define PLL_HPLL			4
#define PLL_CPLL			5
#define PLL_DPLL			6

/* cru clocks */
#define ARMCLK				8
#define CLK_GPU				9
#define ACLK_RKNN			10
#define CLK_DDR				11
#define CLK_MATRIX_50M_SRC		12
#define CLK_MATRIX_100M_SRC		13
#define CLK_MATRIX_125M_SRC		14
#define CLK_MATRIX_200M_SRC		15
#define CLK_MATRIX_300M_SRC		16
#define ACLK_TOP			17
#define ACLK_TOP_VIO			18
#define CLK_CAM0_OUT2IO			19
#define CLK_CAM1_OUT2IO			20
#define CLK_CAM2_OUT2IO			21
#define CLK_CAM3_OUT2IO			22
#define ACLK_BUS			23
#define HCLK_BUS			24
#define PCLK_BUS			25
#define PCLK_I2C1			26
#define PCLK_I2C2			27
#define PCLK_I2C3			28
#define PCLK_I2C4			29
#define PCLK_I2C5			30
#define CLK_I2C				31
#define CLK_I2C1			32
#define CLK_I2C2			33
#define CLK_I2C3			34
#define CLK_I2C4			35
#define CLK_I2C5			36
#define DCLK_BUS_GPIO			37
#define DCLK_BUS_GPIO3			38
#define DCLK_BUS_GPIO4			39
#define PCLK_TIMER			40
#define CLK_TIMER0			41
#define CLK_TIMER1			42
#define CLK_TIMER2			43
#define CLK_TIMER3			44
#define CLK_TIMER4			45
#define CLK_TIMER5			46
#define PCLK_STIMER			47
#define CLK_STIMER0			48
#define CLK_STIMER1			49
#define PCLK_WDTNS			50
#define CLK_WDTNS			51
#define PCLK_GRF			52
#define PCLK_SGRF			53
#define PCLK_MAILBOX			54
#define PCLK_INTC			55
#define ACLK_BUS_GIC400			56
#define ACLK_BUS_SPINLOCK		57
#define ACLK_DCF			58
#define PCLK_DCF			59
#define FCLK_BUS_CM0_CORE		60
#define CLK_BUS_CM0_RTC			61
#define HCLK_ICACHE			62
#define HCLK_DCACHE			63
#define PCLK_TSADC			64
#define CLK_TSADC			65
#define CLK_TSADC_TSEN			66
#define PCLK_DFT2APB			67
#define CLK_SARADC_VCCIO156		68
#define PCLK_GMAC			69
#define ACLK_GMAC			70
#define CLK_GMAC_125M_CRU_I		71
#define CLK_GMAC_50M_CRU_I		72
#define CLK_GMAC_50M_O			73
#define CLK_GMAC_ETH_OUT2IO		74
#define PCLK_APB2ASB_VCCIO156		75
#define PCLK_TO_VCCIO156		76
#define PCLK_DSIPHY			77
#define PCLK_DSITX			78
#define PCLK_CPU_EMA_DET		79
#define PCLK_HASH			80
#define PCLK_TOPCRU			81
#define PCLK_ASB2APB_VCCIO156		82
#define PCLK_IOC_VCCIO156		83
#define PCLK_GPIO3_VCCIO156		84
#define PCLK_GPIO4_VCCIO156		85
#define PCLK_SARADC_VCCIO156		86
#define PCLK_MAC100			87
#define ACLK_MAC100			89
#define CLK_MAC100_50M_MATRIX		90
#define HCLK_CORE			91
#define PCLK_DDR			92
#define CLK_MSCH_BRG_BIU		93
#define PCLK_DDR_HWLP			94
#define PCLK_DDR_UPCTL			95
#define PCLK_DDR_PHY			96
#define PCLK_DDR_DFICTL			97
#define PCLK_DDR_DMA2DDR		98
#define PCLK_DDR_MON			99
#define TMCLK_DDR_MON			100
#define PCLK_DDR_GRF			101
#define PCLK_DDR_CRU			102
#define PCLK_SUBDDR_CRU			103
#define CLK_GPU_PRE			104
#define ACLK_GPU_PRE			105
#define CLK_GPU_BRG			107
#define CLK_NPU_PRE			108
#define HCLK_NPU_PRE			109
#define HCLK_RKNN			111
#define ACLK_PERI			112
#define HCLK_PERI			113
#define PCLK_PERI			114
#define PCLK_PERICRU			115
#define HCLK_SAI0			116
#define CLK_SAI0_SRC			117
#define CLK_SAI0_FRAC			118
#define CLK_SAI0			119
#define MCLK_SAI0			120
#define MCLK_SAI0_OUT2IO		121
#define HCLK_SAI1			122
#define CLK_SAI1_SRC			123
#define CLK_SAI1_FRAC			124
#define CLK_SAI1			125
#define MCLK_SAI1			126
#define MCLK_SAI1_OUT2IO		127
#define HCLK_SAI2			128
#define CLK_SAI2_SRC			129
#define CLK_SAI2_FRAC			130
#define CLK_SAI2			131
#define MCLK_SAI2			132
#define MCLK_SAI2_OUT2IO		133
#define HCLK_DSM			134
#define CLK_DSM				135
#define HCLK_PDM			136
#define MCLK_PDM			137
#define HCLK_SPDIF			138
#define CLK_SPDIF_SRC			139
#define CLK_SPDIF_FRAC			140
#define CLK_SPDIF			141
#define MCLK_SPDIF			142
#define HCLK_SDMMC0			143
#define CCLK_SDMMC0			144
#define HCLK_SDMMC1			145
#define CCLK_SDMMC1			146
#define SCLK_SDMMC0_DRV			147
#define SCLK_SDMMC0_SAMPLE		148
#define SCLK_SDMMC1_DRV			149
#define SCLK_SDMMC1_SAMPLE		150
#define HCLK_EMMC			151
#define ACLK_EMMC			152
#define CCLK_EMMC			153
#define BCLK_EMMC			154
#define TMCLK_EMMC			155
#define SCLK_SFC			156
#define HCLK_SFC			157
#define HCLK_USB2HOST			158
#define HCLK_USB2HOST_ARB		159
#define PCLK_SPI1			160
#define CLK_SPI1			161
#define SCLK_IN_SPI1			162
#define PCLK_SPI2			163
#define CLK_SPI2			164
#define SCLK_IN_SPI2			165
#define PCLK_UART1			166
#define PCLK_UART2			167
#define PCLK_UART3			168
#define PCLK_UART4			169
#define PCLK_UART5			170
#define PCLK_UART6			171
#define PCLK_UART7			172
#define PCLK_UART8			173
#define PCLK_UART9			174
#define CLK_UART1_SRC			175
#define CLK_UART1_FRAC			176
#define CLK_UART1			177
#define SCLK_UART1			178
#define CLK_UART2_SRC			179
#define CLK_UART2_FRAC			180
#define CLK_UART2			181
#define SCLK_UART2			182
#define CLK_UART3_SRC			183
#define CLK_UART3_FRAC			184
#define CLK_UART3			185
#define SCLK_UART3			186
#define CLK_UART4_SRC			187
#define CLK_UART4_FRAC			188
#define CLK_UART4			189
#define SCLK_UART4			190
#define CLK_UART5_SRC			191
#define CLK_UART5_FRAC			192
#define CLK_UART5			193
#define SCLK_UART5			194
#define CLK_UART6_SRC			195
#define CLK_UART6_FRAC			196
#define CLK_UART6			197
#define SCLK_UART6			198
#define CLK_UART7_SRC			199
#define CLK_UART7_FRAC			200
#define CLK_UART7			201
#define SCLK_UART7			202
#define CLK_UART8_SRC			203
#define CLK_UART8_FRAC			204
#define CLK_UART8			205
#define SCLK_UART8			206
#define CLK_UART9_SRC			207
#define CLK_UART9_FRAC			208
#define CLK_UART9			209
#define SCLK_UART9			210
#define PCLK_PWM1_PERI			211
#define CLK_PWM1_PERI			212
#define CLK_CAPTURE_PWM1_PERI		213
#define PCLK_PWM2_PERI			214
#define CLK_PWM2_PERI			215
#define CLK_CAPTURE_PWM2_PERI		216
#define PCLK_PWM3_PERI			217
#define CLK_PWM3_PERI			218
#define CLK_CAPTURE_PWM3_PERI		219
#define PCLK_CAN0			220
#define CLK_CAN0			221
#define PCLK_CAN1			222
#define CLK_CAN1			223
#define ACLK_CRYPTO			224
#define HCLK_CRYPTO			225
#define PCLK_CRYPTO			226
#define CLK_CORE_CRYPTO			227
#define CLK_PKA_CRYPTO			228
#define HCLK_KLAD			229
#define PCLK_KEY_READER			230
#define HCLK_RK_RNG_NS			231
#define HCLK_RK_RNG_S			232
#define HCLK_TRNG_NS			233
#define HCLK_TRNG_S			234
#define HCLK_CRYPTO_S			235
#define PCLK_PERI_WDT			236
#define TCLK_PERI_WDT			237
#define ACLK_SYSMEM			238
#define HCLK_BOOTROM			239
#define PCLK_PERI_GRF			240
#define ACLK_DMAC			241
#define ACLK_RKDMAC			242
#define PCLK_OTPC_NS			243
#define CLK_SBPI_OTPC_NS		244
#define CLK_USER_OTPC_NS		245
#define PCLK_OTPC_S			246
#define CLK_SBPI_OTPC_S			247
#define CLK_USER_OTPC_S			248
#define CLK_OTPC_ARB			249
#define PCLK_OTPPHY			250
#define PCLK_USB2PHY			251
#define PCLK_PIPEPHY			252
#define PCLK_SARADC			253
#define CLK_SARADC			254
#define PCLK_IOC_VCCIO234		255
#define PCLK_PERI_GPIO1			256
#define PCLK_PERI_GPIO2			257
#define DCLK_PERI_GPIO			258
#define DCLK_PERI_GPIO1			259
#define DCLK_PERI_GPIO2			260
#define ACLK_PHP			261
#define PCLK_PHP			262
#define ACLK_PCIE20_MST			263
#define ACLK_PCIE20_SLV			264
#define ACLK_PCIE20_DBI			265
#define PCLK_PCIE20			266
#define CLK_PCIE20_AUX			267
#define ACLK_USB3OTG			268
#define CLK_USB3OTG_SUSPEND		269
#define CLK_USB3OTG_REF			270
#define CLK_PIPEPHY_REF_FUNC		271
#define CLK_200M_PMU			272
#define CLK_RTC_32K			273
#define CLK_RTC32K_FRAC			274
#define BUSCLK_PDPMU0			275
#define PCLK_PMU0_CRU			276
#define PCLK_PMU0_PMU			277
#define CLK_PMU0_PMU			278
#define PCLK_PMU0_HP_TIMER		279
#define CLK_PMU0_HP_TIMER		280
#define CLK_PMU0_32K_HP_TIMER		281
#define PCLK_PMU0_PVTM			282
#define CLK_PMU0_PVTM			283
#define PCLK_IOC_PMUIO			284
#define PCLK_PMU0_GPIO0			285
#define DBCLK_PMU0_GPIO0		286
#define PCLK_PMU0_GRF			287
#define PCLK_PMU0_SGRF			288
#define CLK_DDR_FAIL_SAFE		289
#define PCLK_PMU0_SCRKEYGEN		290
#define PCLK_PMU1_CRU			291
#define HCLK_PMU1_MEM			292
#define PCLK_PMU0_I2C0			293
#define CLK_PMU0_I2C0			294
#define PCLK_PMU1_UART0			295
#define CLK_PMU1_UART0_SRC		296
#define CLK_PMU1_UART0_FRAC		297
#define CLK_PMU1_UART0			298
#define SCLK_PMU1_UART0			299
#define PCLK_PMU1_SPI0			300
#define CLK_PMU1_SPI0			301
#define SCLK_IN_PMU1_SPI0		302
#define PCLK_PMU1_PWM0			303
#define CLK_PMU1_PWM0			304
#define CLK_CAPTURE_PMU1_PWM0		305
#define CLK_PMU1_WIFI			306
#define FCLK_PMU1_CM0_CORE		307
#define CLK_PMU1_CM0_RTC		308
#define PCLK_PMU1_WDTNS			309
#define CLK_PMU1_WDTNS			310
#define PCLK_PMU1_MAILBOX		311
#define CLK_PIPEPHY_DIV			312
#define CLK_PIPEPHY_XIN24M		313
#define CLK_PIPEPHY_REF			314
#define CLK_24M_SSCSRC			315
#define CLK_USB2PHY_XIN24M		316
#define CLK_USB2PHY_REF			317
#define CLK_MIPIDSIPHY_XIN24M		318
#define CLK_MIPIDSIPHY_REF		319
#define ACLK_RGA_PRE			320
#define HCLK_RGA_PRE			321
#define ACLK_RGA			322
#define HCLK_RGA			323
#define CLK_RGA_CORE			324
#define ACLK_JDEC			325
#define HCLK_JDEC			326
#define ACLK_VDPU_PRE			327
#define CLK_RKVDEC_HEVC_CA		328
#define HCLK_VDPU_PRE			329
#define ACLK_RKVDEC			330
#define HCLK_RKVDEC			331
#define CLK_RKVENC_CORE			332
#define ACLK_VEPU_PRE			333
#define HCLK_VEPU_PRE			334
#define ACLK_RKVENC			335
#define HCLK_RKVENC			336
#define ACLK_VI				337
#define HCLK_VI				338
#define PCLK_VI				339
#define ACLK_ISP			340
#define HCLK_ISP			341
#define CLK_ISP				342
#define ACLK_VICAP			343
#define HCLK_VICAP			344
#define DCLK_VICAP			345
#define CSIRX0_CLK_DATA			346
#define CSIRX1_CLK_DATA			347
#define CSIRX2_CLK_DATA			348
#define CSIRX3_CLK_DATA			349
#define PCLK_CSIHOST0			350
#define PCLK_CSIHOST1			351
#define PCLK_CSIHOST2			352
#define PCLK_CSIHOST3			353
#define PCLK_CSIPHY0			354
#define PCLK_CSIPHY1			355
#define ACLK_VO_PRE			356
#define HCLK_VO_PRE			357
#define ACLK_VOP			358
#define HCLK_VOP			359
#define DCLK_VOP			360
#define DCLK_VOP1			361
#define ACLK_CRYPTO_S			362
#define PCLK_CRYPTO_S			363
#define CLK_CORE_CRYPTO_S		364
#define CLK_PKA_CRYPTO_S		365

#define CLK_NR_CLKS			(CLK_PKA_CRYPTO_S + 1)

/* soft-reset indices */

/********Name=SOFTRST_CON01,Offset=0x404********/
#define SRST_A_TOP_BIU			16
#define SRST_A_TOP_VIO_BIU		17
#define SRST_REF_PVTPLL_LOGIC		18
/********Name=SOFTRST_CON03,Offset=0x40C********/
#define SRST_NCOREPORESET0		48
#define SRST_NCOREPORESET1		49
#define SRST_NCOREPORESET2		50
#define SRST_NCOREPORESET3		51
#define SRST_NCORESET0			52
#define SRST_NCORESET1			53
#define SRST_NCORESET2			54
#define SRST_NCORESET3			55
#define SRST_NL2RESET			56
/********Name=SOFTRST_CON04,Offset=0x410********/
#define SRST_DAP			73
#define SRST_P_DBG_DAPLITE		74
#define SRST_REF_PVTPLL_CORE		77
/********Name=SOFTRST_CON05,Offset=0x414********/
#define SRST_A_CORE_BIU			80
#define SRST_P_CORE_BIU			81
#define SRST_H_CORE_BIU			82
/********Name=SOFTRST_CON06,Offset=0x418********/
#define SRST_A_NPU_BIU			98
#define SRST_H_NPU_BIU			99
#define SRST_A_RKNN			100
#define SRST_H_RKNN			101
#define SRST_REF_PVTPLL_NPU		102
/********Name=SOFTRST_CON08,Offset=0x420********/
#define SRST_A_GPU_BIU			131
#define SRST_GPU			132
#define SRST_REF_PVTPLL_GPU		133
#define SRST_GPU_BRG_BIU		134
/********Name=SOFTRST_CON09,Offset=0x424********/
#define SRST_RKVENC_CORE		144
#define SRST_A_VEPU_BIU			147
#define SRST_H_VEPU_BIU			148
#define SRST_A_RKVENC			149
#define SRST_H_RKVENC			150
/********Name=SOFTRST_CON10,Offset=0x428********/
#define SRST_RKVDEC_HEVC_CA		162
#define SRST_A_VDPU_BIU			165
#define SRST_H_VDPU_BIU			166
#define SRST_A_RKVDEC			167
#define SRST_H_RKVDEC			168
/********Name=SOFTRST_CON11,Offset=0x42C********/
#define SRST_A_VI_BIU			179
#define SRST_H_VI_BIU			180
#define SRST_P_VI_BIU			181
#define SRST_ISP			184
#define SRST_A_VICAP			185
#define SRST_H_VICAP			186
#define SRST_D_VICAP			187
#define SRST_I0_VICAP			188
#define SRST_I1_VICAP			189
#define SRST_I2_VICAP			190
#define SRST_I3_VICAP			191
/********Name=SOFTRST_CON12,Offset=0x430********/
#define SRST_P_CSIHOST0			192
#define SRST_P_CSIHOST1			193
#define SRST_P_CSIHOST2			194
#define SRST_P_CSIHOST3			195
#define SRST_P_CSIPHY0			196
#define SRST_P_CSIPHY1			197
/********Name=SOFTRST_CON13,Offset=0x434********/
#define SRST_A_VO_BIU			211
#define SRST_H_VO_BIU			212
#define SRST_A_VOP			214
#define SRST_H_VOP			215
#define SRST_D_VOP			216
#define SRST_D_VOP1			217
/********Name=SOFTRST_CON14,Offset=0x438********/
#define SRST_A_RGA_BIU			227
#define SRST_H_RGA_BIU			228
#define SRST_A_RGA			230
#define SRST_H_RGA			231
#define SRST_RGA_CORE			232
#define SRST_A_JDEC			233
#define SRST_H_JDEC			234
/********Name=SOFTRST_CON15,Offset=0x43C********/
#define SRST_B_EBK_BIU			242
#define SRST_P_EBK_BIU			243
#define SRST_AHB2AXI_EBC		244
#define SRST_H_EBC			245
#define SRST_D_EBC			246
#define SRST_H_EINK			247
#define SRST_P_EINK			248
/********Name=SOFTRST_CON16,Offset=0x440********/
#define SRST_P_PHP_BIU			258
#define SRST_A_PHP_BIU			259
#define SRST_P_PCIE20			263
#define SRST_PCIE20_POWERUP		264
#define SRST_USB3OTG			266
/********Name=SOFTRST_CON17,Offset=0x444********/
#define SRST_PIPEPHY			275
/********Name=SOFTRST_CON18,Offset=0x448********/
#define SRST_A_BUS_BIU			291
#define SRST_H_BUS_BIU			292
#define SRST_P_BUS_BIU			293
/********Name=SOFTRST_CON19,Offset=0x44C********/
#define SRST_P_I2C1			304
#define SRST_P_I2C2			305
#define SRST_P_I2C3			306
#define SRST_P_I2C4			307
#define SRST_P_I2C5			308
#define SRST_I2C1			310
#define SRST_I2C2			311
#define SRST_I2C3			312
#define SRST_I2C4			313
#define SRST_I2C5			314
/********Name=SOFTRST_CON20,Offset=0x450********/
#define SRST_BUS_GPIO3			325
#define SRST_BUS_GPIO4			326
/********Name=SOFTRST_CON21,Offset=0x454********/
#define SRST_P_TIMER			336
#define SRST_TIMER0			337
#define SRST_TIMER1			338
#define SRST_TIMER2			339
#define SRST_TIMER3			340
#define SRST_TIMER4			341
#define SRST_TIMER5			342
#define SRST_P_STIMER			343
#define SRST_STIMER0			344
#define SRST_STIMER1			345
/********Name=SOFTRST_CON22,Offset=0x458********/
#define SRST_P_WDTNS			352
#define SRST_WDTNS			353
#define SRST_P_GRF			354
#define SRST_P_SGRF			355
#define SRST_P_MAILBOX			356
#define SRST_P_INTC			357
#define SRST_A_BUS_GIC400		358
#define SRST_A_BUS_GIC400_DEBUG		359
/********Name=SOFTRST_CON23,Offset=0x45C********/
#define SRST_A_BUS_SPINLOCK		368
#define SRST_A_DCF			369
#define SRST_P_DCF			370
#define SRST_F_BUS_CM0_CORE		371
#define SRST_T_BUS_CM0_JTAG		373
#define SRST_H_ICACHE			376
#define SRST_H_DCACHE			377
/********Name=SOFTRST_CON24,Offset=0x460********/
#define SRST_P_TSADC			384
#define SRST_TSADC			385
#define SRST_TSADCPHY			386
#define SRST_P_DFT2APB			388
/********Name=SOFTRST_CON25,Offset=0x464********/
#define SRST_A_GMAC			401
#define SRST_P_APB2ASB_VCCIO156		405
#define SRST_P_DSIPHY			408
#define SRST_P_DSITX			409
#define SRST_P_CPU_EMA_DET		410
#define SRST_P_HASH			411
#define SRST_P_TOPCRU			415
/********Name=SOFTRST_CON26,Offset=0x468********/
#define SRST_P_ASB2APB_VCCIO156		416
#define SRST_P_IOC_VCCIO156		417
#define SRST_P_GPIO3_VCCIO156		418
#define SRST_P_GPIO4_VCCIO156		419
#define SRST_P_SARADC_VCCIO156		420
#define SRST_SARADC_VCCIO156		421
#define SRST_SARADC_VCCIO156_PHY	422
/********Name=SOFTRST_CON27,Offset=0x46c********/
#define SRST_A_MAC100			433

/* (0x10200 - 0x400) / 4 * 16 = 260096 */
/********Name=PMU0SOFTRST_CON00,Offset=0x10200********/
#define SRST_P_PMU0_CRU			260096
#define SRST_P_PMU0_PMU			260097
#define SRST_PMU0_PMU			260098
#define SRST_P_PMU0_HP_TIMER		260099
#define SRST_PMU0_HP_TIMER		260100
#define SRST_PMU0_32K_HP_TIMER		260101
#define SRST_P_PMU0_PVTM		260102
#define SRST_PMU0_PVTM			260103
#define SRST_P_IOC_PMUIO		260104
#define SRST_P_PMU0_GPIO0		260105
#define SRST_PMU0_GPIO0			260106
#define SRST_P_PMU0_GRF			260107
#define SRST_P_PMU0_SGRF		260108
/********Name=PMU0SOFTRST_CON01,Offset=0x10204********/
#define SRST_DDR_FAIL_SAFE		260112
#define SRST_P_PMU0_SCRKEYGEN		260113
/********Name=PMU0SOFTRST_CON02,Offset=0x10208********/
#define SRST_P_PMU0_I2C0		260136
#define SRST_PMU0_I2C0			260137

/* (0x18200 - 0x400) / 4 * 16 = 391168 */
/********Name=PMU1SOFTRST_CON00,Offset=0x18200********/
#define SRST_P_PMU1_CRU			391168
#define SRST_H_PMU1_MEM			391170
#define SRST_H_PMU1_BIU			391171
#define SRST_P_PMU1_BIU			391172
#define SRST_P_PMU1_UART0		391175
#define SRST_S_PMU1_UART0		391178
/********Name=PMU1SOFTRST_CON01,Offset=0x18204********/
#define SRST_P_PMU1_SPI0		391184
#define SRST_PMU1_SPI0			391185
#define SRST_P_PMU1_PWM0		391187
#define SRST_PMU1_PWM0			391188
/********Name=PMU1SOFTRST_CON02,Offset=0x18208********/
#define SRST_F_PMU1_CM0_CORE		391200
#define SRST_T_PMU1_CM0_JTAG		391202
#define SRST_P_PMU1_WDTNS		391203
#define SRST_PMU1_WDTNS			391204
#define SRST_PMU1_MAILBOX		391208

/* (0x20200 - 0x400) / 4 * 16 = 522240 */
/********Name=DDRSOFTRST_CON00,Offset=0x20200********/
#define SRST_MSCH_BRG_BIU		522244
#define SRST_P_MSCH_BIU			522245
#define SRST_P_DDR_HWLP			522246
#define SRST_P_DDR_PHY			522248
#define SRST_P_DDR_DFICTL		522249
#define SRST_P_DDR_DMA2DDR		522250
/********Name=DDRSOFTRST_CON01,Offset=0x20204********/
#define SRST_P_DDR_MON			522256
#define SRST_TM_DDR_MON			522257
#define SRST_P_DDR_GRF			522258
#define SRST_P_DDR_CRU			522259
#define SRST_P_SUBDDR_CRU		522260

/* (0x28200 - 0x400) / 4 * 16 = 653312 */
/********Name=SUBDDRSOFTRST_CON00,Offset=0x28200********/
#define SRST_MSCH_BIU			653313
#define SRST_DDR_PHY			653316
#define SRST_DDR_DFICTL			653317
#define SRST_DDR_SCRAMBLE		653318
#define SRST_DDR_MON			653319
#define SRST_A_DDR_SPLIT		653320
#define SRST_DDR_DMA2DDR		653321

/* (0x30400 - 0x400) / 4 * 16 = 786432 */
/********Name=PERISOFTRST_CON01,Offset=0x30404********/
#define SRST_A_PERI_BIU			786451
#define SRST_H_PERI_BIU			786452
#define SRST_P_PERI_BIU			786453
#define SRST_P_PERICRU			786454
/********Name=PERISOFTRST_CON02,Offset=0x30408********/
#define SRST_H_SAI0_8CH			786464
#define SRST_M_SAI0_8CH			786467
#define SRST_H_SAI1_8CH			786469
#define SRST_M_SAI1_8CH			786472
#define SRST_H_SAI2_2CH			786474
#define SRST_M_SAI2_2CH			786477
/********Name=PERISOFTRST_CON03,Offset=0x3040C********/
#define SRST_H_DSM			786481
#define SRST_DSM			786482
#define SRST_H_PDM			786484
#define SRST_M_PDM			786485
#define SRST_H_SPDIF			786488
#define SRST_M_SPDIF			786491
/********Name=PERISOFTRST_CON04,Offset=0x30410********/
#define SRST_H_SDMMC0			786496
#define SRST_H_SDMMC1			786498
#define SRST_H_EMMC			786504
#define SRST_A_EMMC			786505
#define SRST_C_EMMC			786506
#define SRST_B_EMMC			786507
#define SRST_T_EMMC			786508
#define SRST_S_SFC			786509
#define SRST_H_SFC			786510
/********Name=PERISOFTRST_CON05,Offset=0x30414********/
#define SRST_H_USB2HOST			786512
#define SRST_H_USB2HOST_ARB		786513
#define SRST_USB2HOST_UTMI		786514
/********Name=PERISOFTRST_CON06,Offset=0x30418********/
#define SRST_P_SPI1			786528
#define SRST_SPI1			786529
#define SRST_P_SPI2			786531
#define SRST_SPI2			786532
/********Name=PERISOFTRST_CON07,Offset=0x3041C********/
#define SRST_P_UART1			786544
#define SRST_P_UART2			786545
#define SRST_P_UART3			786546
#define SRST_P_UART4			786547
#define SRST_P_UART5			786548
#define SRST_P_UART6			786549
#define SRST_P_UART7			786550
#define SRST_P_UART8			786551
#define SRST_P_UART9			786552
#define SRST_S_UART1			786555
#define SRST_S_UART2			786558
/********Name=PERISOFTRST_CON08,Offset=0x30420********/
#define SRST_S_UART3			786561
#define SRST_S_UART4			786564
#define SRST_S_UART5			786567
#define SRST_S_UART6			786570
#define SRST_S_UART7			786573
/********Name=PERISOFTRST_CON09,Offset=0x30424********/
#define SRST_S_UART8			786576
#define SRST_S_UART9			786579
/********Name=PERISOFTRST_CON10,Offset=0x30428********/
#define SRST_P_PWM1_PERI		786592
#define SRST_PWM1_PERI			786593
#define SRST_P_PWM2_PERI		786595
#define SRST_PWM2_PERI			786596
#define SRST_P_PWM3_PERI		786598
#define SRST_PWM3_PERI			786599
/********Name=PERISOFTRST_CON11,Offset=0x3042C********/
#define SRST_P_CAN0			786608
#define SRST_CAN0			786609
#define SRST_P_CAN1			786610
#define SRST_CAN1			786611
/********Name=PERISOFTRST_CON12,Offset=0x30430********/
#define SRST_A_CRYPTO			786624
#define SRST_H_CRYPTO			786625
#define SRST_P_CRYPTO			786626
#define SRST_CORE_CRYPTO		786627
#define SRST_PKA_CRYPTO			786628
#define SRST_H_KLAD			786629
#define SRST_P_KEY_READER		786630
#define SRST_H_RK_RNG_NS		786631
#define SRST_H_RK_RNG_S			786632
#define SRST_H_TRNG_NS			786633
#define SRST_H_TRNG_S			786634
#define SRST_H_CRYPTO_S			786635
/********Name=PERISOFTRST_CON13,Offset=0x30434********/
#define SRST_P_PERI_WDT			786640
#define SRST_T_PERI_WDT			786641
#define SRST_A_SYSMEM			786642
#define SRST_H_BOOTROM			786643
#define SRST_P_PERI_GRF			786644
#define SRST_A_DMAC			786645
#define SRST_A_RKDMAC			786646
/********Name=PERISOFTRST_CON14,Offset=0x30438********/
#define SRST_P_OTPC_NS			786656
#define SRST_SBPI_OTPC_NS		786657
#define SRST_USER_OTPC_NS		786658
#define SRST_P_OTPC_S			786659
#define SRST_SBPI_OTPC_S		786660
#define SRST_USER_OTPC_S		786661
#define SRST_OTPC_ARB			786662
#define SRST_P_OTPPHY			786663
#define SRST_OTP_NPOR			786664
/********Name=PERISOFTRST_CON15,Offset=0x3043C********/
#define SRST_P_USB2PHY			786672
#define SRST_USB2PHY_POR		786676
#define SRST_USB2PHY_OTG		786677
#define SRST_USB2PHY_HOST		786678
#define SRST_P_PIPEPHY			786679
/********Name=PERISOFTRST_CON16,Offset=0x30440********/
#define SRST_P_SARADC			786692
#define SRST_SARADC			786693
#define SRST_SARADC_PHY			786694
#define SRST_P_IOC_VCCIO234		786700
/********Name=PERISOFTRST_CON17,Offset=0x30444********/
#define SRST_P_PERI_GPIO1		786704
#define SRST_P_PERI_GPIO2		786705
#define SRST_PERI_GPIO1			786706
#define SRST_PERI_GPIO2			786707

#endif
