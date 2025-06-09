/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2024-2025 Rockchip Electronics Co. Ltd.
 *
 * Author: Elaine Zhang <zhangqing@rock-chips.com>
 */

#ifndef _DT_BINDINGS_RESET_ROCKCHIP_RK3562_H
#define _DT_BINDINGS_RESET_ROCKCHIP_RK3562_H

/********Name=SOFTRST_CON01,Offset=0x404********/
#define SRST_A_TOP_BIU			0
#define SRST_A_TOP_VIO_BIU		1
#define SRST_REF_PVTPLL_LOGIC		2
/********Name=SOFTRST_CON03,Offset=0x40C********/
#define SRST_NCOREPORESET0		3
#define SRST_NCOREPORESET1		4
#define SRST_NCOREPORESET2		5
#define SRST_NCOREPORESET3		6
#define SRST_NCORESET0			7
#define SRST_NCORESET1			8
#define SRST_NCORESET2			9
#define SRST_NCORESET3			10
#define SRST_NL2RESET			11
/********Name=SOFTRST_CON04,Offset=0x410********/
#define SRST_DAP			12
#define SRST_P_DBG_DAPLITE		13
#define SRST_REF_PVTPLL_CORE		14
/********Name=SOFTRST_CON05,Offset=0x414********/
#define SRST_A_CORE_BIU			15
#define SRST_P_CORE_BIU			16
#define SRST_H_CORE_BIU			17
/********Name=SOFTRST_CON06,Offset=0x418********/
#define SRST_A_NPU_BIU			18
#define SRST_H_NPU_BIU			19
#define SRST_A_RKNN			20
#define SRST_H_RKNN			21
#define SRST_REF_PVTPLL_NPU		22
/********Name=SOFTRST_CON08,Offset=0x420********/
#define SRST_A_GPU_BIU			23
#define SRST_GPU			24
#define SRST_REF_PVTPLL_GPU		25
#define SRST_GPU_BRG_BIU		26
/********Name=SOFTRST_CON09,Offset=0x424********/
#define SRST_RKVENC_CORE		27
#define SRST_A_VEPU_BIU			28
#define SRST_H_VEPU_BIU			29
#define SRST_A_RKVENC			30
#define SRST_H_RKVENC			31
/********Name=SOFTRST_CON10,Offset=0x428********/
#define SRST_RKVDEC_HEVC_CA		32
#define SRST_A_VDPU_BIU			33
#define SRST_H_VDPU_BIU			34
#define SRST_A_RKVDEC			35
#define SRST_H_RKVDEC			36
/********Name=SOFTRST_CON11,Offset=0x42C********/
#define SRST_A_VI_BIU			37
#define SRST_H_VI_BIU			38
#define SRST_P_VI_BIU			39
#define SRST_ISP			40
#define SRST_A_VICAP			41
#define SRST_H_VICAP			42
#define SRST_D_VICAP			43
#define SRST_I0_VICAP			44
#define SRST_I1_VICAP			45
#define SRST_I2_VICAP			46
#define SRST_I3_VICAP			47
/********Name=SOFTRST_CON12,Offset=0x430********/
#define SRST_P_CSIHOST0			48
#define SRST_P_CSIHOST1			49
#define SRST_P_CSIHOST2			50
#define SRST_P_CSIHOST3			51
#define SRST_P_CSIPHY0			52
#define SRST_P_CSIPHY1			53
/********Name=SOFTRST_CON13,Offset=0x434********/
#define SRST_A_VO_BIU			54
#define SRST_H_VO_BIU			55
#define SRST_A_VOP			56
#define SRST_H_VOP			57
#define SRST_D_VOP			58
#define SRST_D_VOP1			59
/********Name=SOFTRST_CON14,Offset=0x438********/
#define SRST_A_RGA_BIU			60
#define SRST_H_RGA_BIU			61
#define SRST_A_RGA			62
#define SRST_H_RGA			63
#define SRST_RGA_CORE			64
#define SRST_A_JDEC			65
#define SRST_H_JDEC			66
/********Name=SOFTRST_CON15,Offset=0x43C********/
#define SRST_B_EBK_BIU			67
#define SRST_P_EBK_BIU			68
#define SRST_AHB2AXI_EBC		69
#define SRST_H_EBC			70
#define SRST_D_EBC			71
#define SRST_H_EINK			72
#define SRST_P_EINK			73
/********Name=SOFTRST_CON16,Offset=0x440********/
#define SRST_P_PHP_BIU			74
#define SRST_A_PHP_BIU			75
#define SRST_P_PCIE20			76
#define SRST_PCIE20_POWERUP		77
#define SRST_USB3OTG			78
/********Name=SOFTRST_CON17,Offset=0x444********/
#define SRST_PIPEPHY			79
/********Name=SOFTRST_CON18,Offset=0x448********/
#define SRST_A_BUS_BIU			80
#define SRST_H_BUS_BIU			81
#define SRST_P_BUS_BIU			82
/********Name=SOFTRST_CON19,Offset=0x44C********/
#define SRST_P_I2C1			83
#define SRST_P_I2C2			84
#define SRST_P_I2C3			85
#define SRST_P_I2C4			86
#define SRST_P_I2C5			87
#define SRST_I2C1			88
#define SRST_I2C2			89
#define SRST_I2C3			90
#define SRST_I2C4			91
#define SRST_I2C5			92
/********Name=SOFTRST_CON20,Offset=0x450********/
#define SRST_BUS_GPIO3			93
#define SRST_BUS_GPIO4			94
/********Name=SOFTRST_CON21,Offset=0x454********/
#define SRST_P_TIMER			95
#define SRST_TIMER0			96
#define SRST_TIMER1			97
#define SRST_TIMER2			98
#define SRST_TIMER3			99
#define SRST_TIMER4			100
#define SRST_TIMER5			101
#define SRST_P_STIMER			102
#define SRST_STIMER0			103
#define SRST_STIMER1			104
/********Name=SOFTRST_CON22,Offset=0x458********/
#define SRST_P_WDTNS			105
#define SRST_WDTNS			106
#define SRST_P_GRF			107
#define SRST_P_SGRF			108
#define SRST_P_MAILBOX			109
#define SRST_P_INTC			110
#define SRST_A_BUS_GIC400		111
#define SRST_A_BUS_GIC400_DEBUG		112
/********Name=SOFTRST_CON23,Offset=0x45C********/
#define SRST_A_BUS_SPINLOCK		113
#define SRST_A_DCF			114
#define SRST_P_DCF			115
#define SRST_F_BUS_CM0_CORE		116
#define SRST_T_BUS_CM0_JTAG		117
#define SRST_H_ICACHE			118
#define SRST_H_DCACHE			119
/********Name=SOFTRST_CON24,Offset=0x460********/
#define SRST_P_TSADC			120
#define SRST_TSADC			121
#define SRST_TSADCPHY			122
#define SRST_P_DFT2APB			123
/********Name=SOFTRST_CON25,Offset=0x464********/
#define SRST_A_GMAC			124
#define SRST_P_APB2ASB_VCCIO156		125
#define SRST_P_DSIPHY			126
#define SRST_P_DSITX			127
#define SRST_P_CPU_EMA_DET		128
#define SRST_P_HASH			129
#define SRST_P_TOPCRU			130
/********Name=SOFTRST_CON26,Offset=0x468********/
#define SRST_P_ASB2APB_VCCIO156		131
#define SRST_P_IOC_VCCIO156		132
#define SRST_P_GPIO3_VCCIO156		133
#define SRST_P_GPIO4_VCCIO156		134
#define SRST_P_SARADC_VCCIO156		135
#define SRST_SARADC_VCCIO156		136
#define SRST_SARADC_VCCIO156_PHY	137
/********Name=SOFTRST_CON27,Offset=0x46c********/
#define SRST_A_MAC100			138

/********Name=PMU0SOFTRST_CON00,Offset=0x10200********/
#define SRST_P_PMU0_CRU			139
#define SRST_P_PMU0_PMU			140
#define SRST_PMU0_PMU			141
#define SRST_P_PMU0_HP_TIMER		142
#define SRST_PMU0_HP_TIMER		143
#define SRST_PMU0_32K_HP_TIMER		144
#define SRST_P_PMU0_PVTM		145
#define SRST_PMU0_PVTM			146
#define SRST_P_IOC_PMUIO		147
#define SRST_P_PMU0_GPIO0		148
#define SRST_PMU0_GPIO0			149
#define SRST_P_PMU0_GRF			150
#define SRST_P_PMU0_SGRF		151
/********Name=PMU0SOFTRST_CON01,Offset=0x10204********/
#define SRST_DDR_FAIL_SAFE		152
#define SRST_P_PMU0_SCRKEYGEN		153
/********Name=PMU0SOFTRST_CON02,Offset=0x10208********/
#define SRST_P_PMU0_I2C0		154
#define SRST_PMU0_I2C0			155

/********Name=PMU1SOFTRST_CON00,Offset=0x18200********/
#define SRST_P_PMU1_CRU			156
#define SRST_H_PMU1_MEM			157
#define SRST_H_PMU1_BIU			158
#define SRST_P_PMU1_BIU			159
#define SRST_P_PMU1_UART0		160
#define SRST_S_PMU1_UART0		161
/********Name=PMU1SOFTRST_CON01,Offset=0x18204********/
#define SRST_P_PMU1_SPI0		162
#define SRST_PMU1_SPI0			163
#define SRST_P_PMU1_PWM0		164
#define SRST_PMU1_PWM0			165
/********Name=PMU1SOFTRST_CON02,Offset=0x18208********/
#define SRST_F_PMU1_CM0_CORE		166
#define SRST_T_PMU1_CM0_JTAG		167
#define SRST_P_PMU1_WDTNS		168
#define SRST_PMU1_WDTNS			169
#define SRST_PMU1_MAILBOX		170

/********Name=DDRSOFTRST_CON00,Offset=0x20200********/
#define SRST_MSCH_BRG_BIU		171
#define SRST_P_MSCH_BIU			172
#define SRST_P_DDR_HWLP			173
#define SRST_P_DDR_PHY			290
#define SRST_P_DDR_DFICTL		174
#define SRST_P_DDR_DMA2DDR		175
/********Name=DDRSOFTRST_CON01,Offset=0x20204********/
#define SRST_P_DDR_MON			176
#define SRST_TM_DDR_MON			177
#define SRST_P_DDR_GRF			178
#define SRST_P_DDR_CRU			179
#define SRST_P_SUBDDR_CRU		180

/********Name=SUBDDRSOFTRST_CON00,Offset=0x28200********/
#define SRST_MSCH_BIU			181
#define SRST_DDR_PHY			182
#define SRST_DDR_DFICTL			183
#define SRST_DDR_SCRAMBLE		184
#define SRST_DDR_MON			185
#define SRST_A_DDR_SPLIT		186
#define SRST_DDR_DMA2DDR		187

/********Name=PERISOFTRST_CON01,Offset=0x30404********/
#define SRST_A_PERI_BIU			188
#define SRST_H_PERI_BIU			189
#define SRST_P_PERI_BIU			190
#define SRST_P_PERICRU			191
/********Name=PERISOFTRST_CON02,Offset=0x30408********/
#define SRST_H_SAI0_8CH			192
#define SRST_M_SAI0_8CH			193
#define SRST_H_SAI1_8CH			194
#define SRST_M_SAI1_8CH			195
#define SRST_H_SAI2_2CH			196
#define SRST_M_SAI2_2CH			197
/********Name=PERISOFTRST_CON03,Offset=0x3040C********/
#define SRST_H_DSM			198
#define SRST_DSM			199
#define SRST_H_PDM			200
#define SRST_M_PDM			201
#define SRST_H_SPDIF			202
#define SRST_M_SPDIF			203
/********Name=PERISOFTRST_CON04,Offset=0x30410********/
#define SRST_H_SDMMC0			204
#define SRST_H_SDMMC1			205
#define SRST_H_EMMC			206
#define SRST_A_EMMC			207
#define SRST_C_EMMC			208
#define SRST_B_EMMC			209
#define SRST_T_EMMC			210
#define SRST_S_SFC			211
#define SRST_H_SFC			212
/********Name=PERISOFTRST_CON05,Offset=0x30414********/
#define SRST_H_USB2HOST			213
#define SRST_H_USB2HOST_ARB		214
#define SRST_USB2HOST_UTMI		215
/********Name=PERISOFTRST_CON06,Offset=0x30418********/
#define SRST_P_SPI1			216
#define SRST_SPI1			217
#define SRST_P_SPI2			218
#define SRST_SPI2			219
/********Name=PERISOFTRST_CON07,Offset=0x3041C********/
#define SRST_P_UART1			220
#define SRST_P_UART2			221
#define SRST_P_UART3			222
#define SRST_P_UART4			223
#define SRST_P_UART5			224
#define SRST_P_UART6			225
#define SRST_P_UART7			226
#define SRST_P_UART8			227
#define SRST_P_UART9			228
#define SRST_S_UART1			229
#define SRST_S_UART2			230
/********Name=PERISOFTRST_CON08,Offset=0x30420********/
#define SRST_S_UART3			231
#define SRST_S_UART4			232
#define SRST_S_UART5			233
#define SRST_S_UART6			234
#define SRST_S_UART7			235
/********Name=PERISOFTRST_CON09,Offset=0x30424********/
#define SRST_S_UART8			236
#define SRST_S_UART9			237
/********Name=PERISOFTRST_CON10,Offset=0x30428********/
#define SRST_P_PWM1_PERI		238
#define SRST_PWM1_PERI			239
#define SRST_P_PWM2_PERI		240
#define SRST_PWM2_PERI			241
#define SRST_P_PWM3_PERI		242
#define SRST_PWM3_PERI			243
/********Name=PERISOFTRST_CON11,Offset=0x3042C********/
#define SRST_P_CAN0			244
#define SRST_CAN0			245
#define SRST_P_CAN1			246
#define SRST_CAN1			247
/********Name=PERISOFTRST_CON12,Offset=0x30430********/
#define SRST_A_CRYPTO			248
#define SRST_H_CRYPTO			249
#define SRST_P_CRYPTO			250
#define SRST_CORE_CRYPTO		251
#define SRST_PKA_CRYPTO			252
#define SRST_H_KLAD			253
#define SRST_P_KEY_READER		254
#define SRST_H_RK_RNG_NS		255
#define SRST_H_RK_RNG_S			256
#define SRST_H_TRNG_NS			257
#define SRST_H_TRNG_S			258
#define SRST_H_CRYPTO_S			259
/********Name=PERISOFTRST_CON13,Offset=0x30434********/
#define SRST_P_PERI_WDT			260
#define SRST_T_PERI_WDT			261
#define SRST_A_SYSMEM			262
#define SRST_H_BOOTROM			263
#define SRST_P_PERI_GRF			264
#define SRST_A_DMAC			265
#define SRST_A_RKDMAC			267
/********Name=PERISOFTRST_CON14,Offset=0x30438********/
#define SRST_P_OTPC_NS			268
#define SRST_SBPI_OTPC_NS		269
#define SRST_USER_OTPC_NS		270
#define SRST_P_OTPC_S			271
#define SRST_SBPI_OTPC_S		272
#define SRST_USER_OTPC_S		273
#define SRST_OTPC_ARB			274
#define SRST_P_OTPPHY			275
#define SRST_OTP_NPOR			276
/********Name=PERISOFTRST_CON15,Offset=0x3043C********/
#define SRST_P_USB2PHY			277
#define SRST_USB2PHY_POR		278
#define SRST_USB2PHY_OTG		279
#define SRST_USB2PHY_HOST		280
#define SRST_P_PIPEPHY			281
/********Name=PERISOFTRST_CON16,Offset=0x30440********/
#define SRST_P_SARADC			282
#define SRST_SARADC			283
#define SRST_SARADC_PHY			284
#define SRST_P_IOC_VCCIO234		285
/********Name=PERISOFTRST_CON17,Offset=0x30444********/
#define SRST_P_PERI_GPIO1		286
#define SRST_P_PERI_GPIO2		287
#define SRST_PERI_GPIO1			288
#define SRST_PERI_GPIO2			289

#endif
