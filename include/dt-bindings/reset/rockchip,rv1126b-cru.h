/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright (c) 2025 Rockchip Electronics Co., Ltd.
 * Author: Elaine Zhang <zhangqing@rock-chips.com>
 */

#ifndef _DT_BINDINGS_RESET_ROCKCHIP_RV1126B_H
#define _DT_BINDINGS_RESET_ROCKCHIP_RV1126B_H

/* ==========================list all of reset fields id=========================== */
/* TOPCRU-->SOFTRST_CON00 */

/* TOPCRU-->SOFTRST_CON15 */
#define SRST_P_CRU				0
#define SRST_P_CRU_BIU				1

/* BUSCRU-->SOFTRST_CON00 */
#define SRST_A_TOP_BIU				2
#define SRST_A_RKCE_BIU				3
#define SRST_A_BUS_BIU				4
#define SRST_H_BUS_BIU				5
#define SRST_P_BUS_BIU				6
#define SRST_P_CRU_BUS				7
#define SRST_P_SYS_GRF				8
#define SRST_H_BOOTROM				9
#define SRST_A_GIC400				10
#define SRST_A_SPINLOCK				11
#define SRST_P_WDT_NS				12
#define SRST_T_WDT_NS				13

/* BUSCRU-->SOFTRST_CON01 */
#define SRST_P_WDT_HPMCU			14
#define SRST_T_WDT_HPMCU			15
#define SRST_H_CACHE				16
#define SRST_P_HPMCU_MAILBOX			17
#define SRST_P_HPMCU_INTMUX			18
#define SRST_HPMCU_FULL_CLUSTER			19
#define SRST_HPMCU_PWUP				20
#define SRST_HPMCU_ONLY_CORE			21
#define SRST_T_HPMCU_JTAG			22
#define SRST_P_RKDMA				23
#define SRST_A_RKDMA				24

/* BUSCRU-->SOFTRST_CON02 */
#define SRST_P_DCF				25
#define SRST_A_DCF				26
#define SRST_H_RGA				27
#define SRST_A_RGA				28
#define SRST_CORE_RGA				29
#define SRST_P_TIMER				30
#define SRST_TIMER0				31
#define SRST_TIMER1				32
#define SRST_TIMER2				33
#define SRST_TIMER3				34
#define SRST_TIMER4				35
#define SRST_TIMER5				36
#define SRST_A_RKCE				37
#define SRST_PKA_RKCE				38
#define SRST_H_RKRNG_S				39
#define SRST_H_RKRNG_NS				40

/* BUSCRU-->SOFTRST_CON03 */
#define SRST_P_I2C0				41
#define SRST_I2C0				42
#define SRST_P_I2C1				43
#define SRST_I2C1				44
#define SRST_P_I2C3				45
#define SRST_I2C3				46
#define SRST_P_I2C4				47
#define SRST_I2C4				48
#define SRST_P_I2C5				49
#define SRST_I2C5				50
#define SRST_P_SPI0				51
#define SRST_SPI0				52
#define SRST_P_SPI1				53
#define SRST_SPI1				54

/* BUSCRU-->SOFTRST_CON04 */
#define SRST_P_PWM0				55
#define SRST_PWM0				56
#define SRST_P_PWM2				57
#define SRST_PWM2				58
#define SRST_P_PWM3				59
#define SRST_PWM3				60

/* BUSCRU-->SOFTRST_CON05 */
#define SRST_P_UART1				61
#define SRST_S_UART1				62
#define SRST_P_UART2				63
#define SRST_S_UART2				64
#define SRST_P_UART3				65
#define SRST_S_UART3				66
#define SRST_P_UART4				67
#define SRST_S_UART4				68
#define SRST_P_UART5				69
#define SRST_S_UART5				70
#define SRST_P_UART6				71
#define SRST_S_UART6				72
#define SRST_P_UART7				73
#define SRST_S_UART7				74

/* BUSCRU-->SOFTRST_CON06 */
#define SRST_P_TSADC				75
#define SRST_TSADC				76
#define SRST_H_SAI0				77
#define SRST_M_SAI0				78
#define SRST_H_SAI1				79
#define SRST_M_SAI1				80
#define SRST_H_SAI2				81
#define SRST_M_SAI2				82
#define SRST_H_RKDSM				83
#define SRST_M_RKDSM				84
#define SRST_H_PDM				85
#define SRST_M_PDM				86
#define SRST_PDM				87

/* BUSCRU-->SOFTRST_CON07 */
#define SRST_H_ASRC0				88
#define SRST_ASRC0				89
#define SRST_H_ASRC1				90
#define SRST_ASRC1				91
#define SRST_P_AUDIO_ADC_BUS			92
#define SRST_M_AUDIO_ADC_BUS			93
#define SRST_P_RKCE				94
#define SRST_H_NS_RKCE				95
#define SRST_P_OTPC_NS				96
#define SRST_SBPI_OTPC_NS			97
#define SRST_USER_OTPC_NS			98
#define SRST_OTPC_ARB				99
#define SRST_P_OTP_MASK				100

/* PERICRU-->SOFTRST_CON00 */
#define SRST_A_PERI_BIU				101
#define SRST_P_PERI_BIU				102
#define SRST_P_RTC_BIU				103
#define SRST_P_CRU_PERI				104
#define SRST_P_PERI_GRF				105
#define SRST_P_GPIO1				106
#define SRST_DB_GPIO1				107
#define SRST_P_IOC_VCCIO1			108
#define SRST_A_USB3OTG				109
#define SRST_H_USB2HOST				110
#define SRST_H_ARB_USB2HOST			111
#define SRST_P_RTC_TEST				112

/* PERICRU-->SOFTRST_CON01 */
#define SRST_H_EMMC				113
#define SRST_H_FSPI0				114
#define SRST_H_XIP_FSPI0			115
#define SRST_S_2X_FSPI0				116
#define SRST_UTMI_USB2HOST			117
#define SRST_REF_PIPEPHY			118
#define SRST_P_PIPEPHY				119
#define SRST_P_PIPEPHY_GRF			120
#define SRST_P_USB2PHY				121
#define SRST_POR_USB2PHY			122
#define SRST_OTG_USB2PHY			123
#define SRST_HOST_USB2PHY			124

/* CORECRU-->SOFTRST_CON00 */
#define SRST_REF_PVTPLL_CORE			125
#define SRST_NCOREPORESET0			126
#define SRST_NCORESET0				127
#define SRST_NCOREPORESET1			128
#define SRST_NCORESET1				129
#define SRST_NCOREPORESET2			130
#define SRST_NCORESET2				131
#define SRST_NCOREPORESET3			132
#define SRST_NCORESET3				133
#define SRST_NDBGRESET				134
#define SRST_NL2RESET				135

/* CORECRU-->SOFTRST_CON01 */
#define SRST_A_CORE_BIU				136
#define SRST_P_CORE_BIU				137
#define SRST_H_CORE_BIU				138
#define SRST_P_DBG				139
#define SRST_POT_DBG				140
#define SRST_NT_DBG				141
#define SRST_P_CORE_PVTPLL			142
#define SRST_P_CRU_CORE				143
#define SRST_P_CORE_GRF				144
#define SRST_P_DFT2APB				145

/* PMUCRU-->SOFTRST_CON00 */
#define SRST_H_PMU_BIU				146
#define SRST_P_PMU_GPIO0			147
#define SRST_DB_PMU_GPIO0			148
#define SRST_P_PMU_HP_TIMER			149
#define SRST_PMU_HP_TIMER			150
#define SRST_PMU_32K_HP_TIMER			151

/* PMUCRU-->SOFTRST_CON01 */
#define SRST_P_PWM1				152
#define SRST_PWM1				153
#define SRST_P_I2C2				154
#define SRST_I2C2				155
#define SRST_P_UART0				156
#define SRST_S_UART0				157

/* PMUCRU-->SOFTRST_CON02 */
#define SRST_P_RCOSC_CTRL			158
#define SRST_REF_RCOSC_CTRL			159
#define SRST_P_IOC_PMUIO0			160
#define SRST_P_CRU_PMU				161
#define SRST_P_PMU_GRF				162
#define SRST_PREROLL				163
#define SRST_PREROLL_32K			164
#define SRST_H_PMU_SRAM				165

/* PMUCRU-->SOFTRST_CON03 */
#define SRST_P_WDT_LPMCU			166
#define SRST_T_WDT_LPMCU			167
#define SRST_LPMCU_FULL_CLUSTER			168
#define SRST_LPMCU_PWUP				169
#define SRST_LPMCU_ONLY_CORE			170
#define SRST_T_LPMCU_JTAG			171
#define SRST_P_LPMCU_MAILBOX			172

/* PMU1CRU-->SOFTRST_CON00 */
#define SRST_P_SPI2AHB				173
#define SRST_H_SPI2AHB				174
#define SRST_H_FSPI1				175
#define SRST_H_XIP_FSPI1			176
#define SRST_S_1X_FSPI1				177
#define SRST_P_IOC_PMUIO1			178
#define SRST_P_CRU_PMU1				179
#define SRST_P_AUDIO_ADC_PMU			180
#define SRST_M_AUDIO_ADC_PMU			181
#define SRST_H_PMU1_BIU				182

/* PMU1CRU-->SOFTRST_CON01 */
#define SRST_P_LPDMA				183
#define SRST_A_LPDMA				184
#define SRST_H_LPSAI				185
#define SRST_M_LPSAI				186
#define SRST_P_AOA_TDD				187
#define SRST_P_AOA_FE				188
#define SRST_P_AOA_AAD				189
#define SRST_P_AOA_APB				190
#define SRST_P_AOA_SRAM				191

/* DDRCRU-->SOFTRST_CON00 */
#define SRST_P_DDR_BIU				192
#define SRST_P_DDRC				193
#define SRST_P_DDRMON				194
#define SRST_TIMER_DDRMON			195
#define SRST_P_DFICTRL				196
#define SRST_P_DDR_GRF				197
#define SRST_P_CRU_DDR				198
#define SRST_P_DDRPHY				199
#define SRST_P_DMA2DDR				200

/* SUBDDRCRU-->SOFTRST_CON00 */
#define SRST_A_SYSMEM_BIU			201
#define SRST_A_SYSMEM				202
#define SRST_A_DDR_BIU				203
#define SRST_A_DDRSCH0_CPU			204
#define SRST_A_DDRSCH1_NPU			205
#define SRST_A_DDRSCH2_POE			206
#define SRST_A_DDRSCH3_VI			207
#define SRST_CORE_DDRC				208
#define SRST_DDRMON				209
#define SRST_DFICTRL				210
#define SRST_RS					211
#define SRST_A_DMA2DDR				212
#define SRST_DDRPHY				213

/* VICRU-->SOFTRST_CON00 */
#define SRST_REF_PVTPLL_ISP			214
#define SRST_A_GMAC_BIU				215
#define SRST_A_VI_BIU				216
#define SRST_H_VI_BIU				217
#define SRST_P_VI_BIU				218
#define SRST_P_CRU_VI				219
#define SRST_P_VI_GRF				220
#define SRST_P_VI_PVTPLL			221
#define SRST_P_DSMC				222
#define SRST_A_DSMC				223
#define SRST_H_CAN0				224
#define SRST_CAN0				225
#define SRST_H_CAN1				226
#define SRST_CAN1				227

/* VICRU-->SOFTRST_CON01 */
#define SRST_P_GPIO2				228
#define SRST_DB_GPIO2				229
#define SRST_P_GPIO4				230
#define SRST_DB_GPIO4				231
#define SRST_P_GPIO5				232
#define SRST_DB_GPIO5				233
#define SRST_P_GPIO6				234
#define SRST_DB_GPIO6				235
#define SRST_P_GPIO7				236
#define SRST_DB_GPIO7				237
#define SRST_P_IOC_VCCIO2			238
#define SRST_P_IOC_VCCIO4			239
#define SRST_P_IOC_VCCIO5			240
#define SRST_P_IOC_VCCIO6			241
#define SRST_P_IOC_VCCIO7			242

/* VICRU-->SOFTRST_CON02 */
#define SRST_CORE_ISP				243
#define SRST_H_VICAP				244
#define SRST_A_VICAP				245
#define SRST_D_VICAP				246
#define SRST_ISP0_VICAP				247
#define SRST_CORE_VPSS				248
#define SRST_CORE_VPSL				249
#define SRST_P_CSI2HOST0			250
#define SRST_P_CSI2HOST1			251
#define SRST_P_CSI2HOST2			252
#define SRST_P_CSI2HOST3			253
#define SRST_H_SDMMC0				254
#define SRST_A_GMAC				255
#define SRST_P_CSIPHY0				256
#define SRST_P_CSIPHY1				257

/* VICRU-->SOFTRST_CON03 */
#define SRST_P_MACPHY				258
#define SRST_MACPHY				259
#define SRST_P_SARADC1				260
#define SRST_SARADC1				261
#define SRST_P_SARADC2				262
#define SRST_SARADC2				263

/* VEPUCRU-->SOFTRST_CON00 */
#define SRST_REF_PVTPLL_VEPU			264
#define SRST_A_VEPU_BIU				265
#define SRST_H_VEPU_BIU				266
#define SRST_P_VEPU_BIU				267
#define SRST_P_CRU_VEPU				268
#define SRST_P_VEPU_GRF				269
#define SRST_P_GPIO3				270
#define SRST_DB_GPIO3				271
#define SRST_P_IOC_VCCIO3			272
#define SRST_P_SARADC0				273
#define SRST_SARADC0				274
#define SRST_H_SDMMC1				275

/* VEPUCRU-->SOFTRST_CON01 */
#define SRST_P_VEPU_PVTPLL			276
#define SRST_H_VEPU				277
#define SRST_A_VEPU				278
#define SRST_CORE_VEPU				279

/* NPUCRU-->SOFTRST_CON00 */
#define SRST_REF_PVTPLL_NPU			280
#define SRST_A_NPU_BIU				281
#define SRST_H_NPU_BIU				282
#define SRST_P_NPU_BIU				283
#define SRST_P_CRU_NPU				284
#define SRST_P_NPU_GRF				285
#define SRST_P_NPU_PVTPLL			286
#define SRST_H_RKNN				287
#define SRST_A_RKNN				288

/* VDOCRU-->SOFTRST_CON00 */
#define SRST_A_RKVDEC_BIU			289
#define SRST_A_VDO_BIU				290
#define SRST_H_VDO_BIU				291
#define SRST_P_VDO_BIU				292
#define SRST_P_CRU_VDO				293
#define SRST_P_VDO_GRF				294
#define SRST_A_RKVDEC				295
#define SRST_H_RKVDEC				296
#define SRST_HEVC_CA_RKVDEC			297
#define SRST_A_VOP				298
#define SRST_H_VOP				299
#define SRST_D_VOP				300
#define SRST_A_OOC				301
#define SRST_H_OOC				302
#define SRST_D_OOC				303

/* VDOCRU-->SOFTRST_CON01 */
#define SRST_H_RKJPEG				304
#define SRST_A_RKJPEG				305
#define SRST_A_RKMMU_DECOM			306
#define SRST_H_RKMMU_DECOM			307
#define SRST_D_DECOM				308
#define SRST_A_DECOM				309
#define SRST_P_DECOM				310
#define SRST_P_MIPI_DSI				311
#define SRST_P_DSIPHY				312

/* VCPCRU-->SOFTRST_CON00 */
#define SRST_REF_PVTPLL_VCP			313
#define SRST_A_VCP_BIU				314
#define SRST_H_VCP_BIU				315
#define SRST_P_VCP_BIU				316
#define SRST_P_CRU_VCP				317
#define SRST_P_VCP_GRF				318
#define SRST_P_VCP_PVTPLL			319
#define SRST_A_AISP_BIU				320
#define SRST_H_AISP_BIU				321
#define SRST_CORE_AISP				322

/* VCPCRU-->SOFTRST_CON01 */
#define SRST_H_FEC				323
#define SRST_A_FEC				324
#define SRST_CORE_FEC				325
#define SRST_H_AVSP				326
#define SRST_A_AVSP				327

#endif
