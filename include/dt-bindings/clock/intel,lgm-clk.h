/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (C) 2020 Intel Corporation.
 * Lei Chuanhua <Chuanhua.lei@intel.com>
 * Zhu Yixin <Yixin.zhu@intel.com>
 */
#ifndef __INTEL_LGM_CLK_H
#define __INTEL_LGM_CLK_H

/* PLL clocks */
#define LGM_CLK_OSC		1
#define LGM_CLK_PLLPP		2
#define LGM_CLK_PLL2		3
#define LGM_CLK_PLL0CZ		4
#define LGM_CLK_PLL0B		5
#define LGM_CLK_PLL1		6
#define LGM_CLK_LJPLL3		7
#define LGM_CLK_LJPLL4		8
#define LGM_CLK_PLL0CM0		9
#define LGM_CLK_PLL0CM1		10

/* clocks from PLLs */

/* ROPLL clocks */
#define LGM_CLK_PP_HW		15
#define LGM_CLK_PP_UC		16
#define LGM_CLK_PP_FXD		17
#define LGM_CLK_PP_TBM		18

/* PLL2 clocks */
#define LGM_CLK_DDR		20

/* PLL0CZ */
#define LGM_CLK_CM		25
#define LGM_CLK_IC		26
#define LGM_CLK_SDXC3		27

/* PLL0B */
#define LGM_CLK_NGI		30
#define LGM_CLK_NOC4		31
#define LGM_CLK_SW		32
#define LGM_CLK_QSPI		33
#define LGM_CLK_CQEM		LGM_CLK_SW
#define LGM_CLK_EMMC5		LGM_CLK_NOC4

/* PLL1 */
#define LGM_CLK_CT		35
#define LGM_CLK_DSP		36
#define LGM_CLK_VIF		37

/* LJPLL3 */
#define LGM_CLK_CML		40
#define LGM_CLK_SERDES		41
#define LGM_CLK_POOL		42
#define LGM_CLK_PTP		43

/* LJPLL4 */
#define LGM_CLK_PCIE		45
#define LGM_CLK_SATA		LGM_CLK_PCIE

/* PLL0CM0 */
#define LGM_CLK_CPU0		50

/* PLL0CM1 */
#define LGM_CLK_CPU1		55

/* Miscellaneous clocks */
#define LGM_CLK_EMMC4		60
#define LGM_CLK_SDXC2		61
#define LGM_CLK_EMMC		62
#define LGM_CLK_SDXC		63
#define LGM_CLK_SLIC		64
#define LGM_CLK_DCL		65
#define LGM_CLK_DOCSIS		66
#define LGM_CLK_PCM		67
#define LGM_CLK_DDR_PHY		68
#define LGM_CLK_PONDEF		69
#define LGM_CLK_PL25M		70
#define LGM_CLK_PL10M		71
#define LGM_CLK_PL1544K		72
#define LGM_CLK_PL2048K		73
#define LGM_CLK_PL8K		74
#define LGM_CLK_PON_NTR		75
#define LGM_CLK_SYNC0		76
#define LGM_CLK_SYNC1		77
#define LGM_CLK_PROGDIV		78
#define LGM_CLK_OD0		79
#define LGM_CLK_OD1		80
#define LGM_CLK_CBPHY0		81
#define LGM_CLK_CBPHY1		82
#define LGM_CLK_CBPHY2		83
#define LGM_CLK_CBPHY3		84

/* Gate clocks */
/* Gate CLK0 */
#define LGM_GCLK_C55		100
#define LGM_GCLK_QSPI		101
#define LGM_GCLK_EIP197		102
#define LGM_GCLK_VAULT		103
#define LGM_GCLK_TOE		104
#define LGM_GCLK_SDXC		105
#define LGM_GCLK_EMMC		106
#define LGM_GCLK_SPI_DBG	107
#define LGM_GCLK_DMA3		108

/* Gate CLK1 */
#define LGM_GCLK_DMA0		120
#define LGM_GCLK_LEDC0		121
#define LGM_GCLK_LEDC1		122
#define LGM_GCLK_I2S0		123
#define LGM_GCLK_I2S1		124
#define LGM_GCLK_EBU		125
#define LGM_GCLK_PWM		126
#define LGM_GCLK_I2C0		127
#define LGM_GCLK_I2C1		128
#define LGM_GCLK_I2C2		129
#define LGM_GCLK_I2C3		130
#define LGM_GCLK_SSC0		131
#define LGM_GCLK_SSC1		132
#define LGM_GCLK_SSC2		133
#define LGM_GCLK_SSC3		134
#define LGM_GCLK_GPTC0		135
#define LGM_GCLK_GPTC1		136
#define LGM_GCLK_GPTC2		137
#define LGM_GCLK_GPTC3		138
#define LGM_GCLK_ASC0		139
#define LGM_GCLK_ASC1		140
#define LGM_GCLK_ASC2		141
#define LGM_GCLK_ASC3		142
#define LGM_GCLK_PCM0		143
#define LGM_GCLK_PCM1		144
#define LGM_GCLK_PCM2		145

/* Gate CLK2 */
#define LGM_GCLK_PCIE10		150
#define LGM_GCLK_PCIE11		151
#define LGM_GCLK_PCIE30		152
#define LGM_GCLK_PCIE31		153
#define LGM_GCLK_PCIE20		154
#define LGM_GCLK_PCIE21		155
#define LGM_GCLK_PCIE40		156
#define LGM_GCLK_PCIE41		157
#define LGM_GCLK_XPCS0		158
#define LGM_GCLK_XPCS1		159
#define LGM_GCLK_XPCS2		160
#define LGM_GCLK_XPCS3		161
#define LGM_GCLK_SATA0		162
#define LGM_GCLK_SATA1		163
#define LGM_GCLK_SATA2		164
#define LGM_GCLK_SATA3		165

/* Gate CLK3 */
#define LGM_GCLK_ARCEM4		170
#define LGM_GCLK_IDMAR1		171
#define LGM_GCLK_IDMAT0		172
#define LGM_GCLK_IDMAT1		173
#define LGM_GCLK_IDMAT2		174
#define LGM_GCLK_PPV4		175
#define LGM_GCLK_GSWIPO		176
#define LGM_GCLK_CQEM		177
#define LGM_GCLK_XPCS5		178
#define LGM_GCLK_USB1		179
#define LGM_GCLK_USB2		180

#endif /* __INTEL_LGM_CLK_H */
