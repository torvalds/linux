/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright (C) STMicroelectronics 2018 - All Rights Reserved
 * Author: Gabriel Fernandez <gabriel.fernandez@st.com> for STMicroelectronics.
 */

#ifndef _DT_BINDINGS_STM32MP1_CLKS_H_
#define _DT_BINDINGS_STM32MP1_CLKS_H_

/* OSCILLATOR clocks */
#define CK_HSE		0
#define CK_CSI		1
#define CK_LSI		2
#define CK_LSE		3
#define CK_HSI		4
#define CK_HSE_DIV2	5

/* Bus clocks */
#define TIM2		6
#define TIM3		7
#define TIM4		8
#define TIM5		9
#define TIM6		10
#define TIM7		11
#define TIM12		12
#define TIM13		13
#define TIM14		14
#define LPTIM1		15
#define SPI2		16
#define SPI3		17
#define USART2		18
#define USART3		19
#define UART4		20
#define UART5		21
#define UART7		22
#define UART8		23
#define I2C1		24
#define I2C2		25
#define I2C3		26
#define I2C5		27
#define SPDIF		28
#define CEC		29
#define DAC12		30
#define MDIO		31
#define TIM1		32
#define TIM8		33
#define TIM15		34
#define TIM16		35
#define TIM17		36
#define SPI1		37
#define SPI4		38
#define SPI5		39
#define USART6		40
#define SAI1		41
#define SAI2		42
#define SAI3		43
#define DFSDM		44
#define FDCAN		45
#define LPTIM2		46
#define LPTIM3		47
#define LPTIM4		48
#define LPTIM5		49
#define SAI4		50
#define SYSCFG		51
#define VREF		52
#define TMPSENS		53
#define PMBCTRL		54
#define HDP		55
#define LTDC		56
#define DSI		57
#define IWDG2		58
#define USBPHY		59
#define STGENRO		60
#define SPI6		61
#define I2C4		62
#define I2C6		63
#define USART1		64
#define RTCAPB		65
#define TZC		66
#define TZPC		67
#define IWDG1		68
#define BSEC		69
#define STGEN		70
#define DMA1		71
#define DMA2		72
#define DMAMUX		73
#define ADC12		74
#define USBO		75
#define SDMMC3		76
#define DCMI		77
#define CRYP2		78
#define HASH2		79
#define RNG2		80
#define CRC2		81
#define HSEM		82
#define IPCC		83
#define GPIOA		84
#define GPIOB		85
#define GPIOC		86
#define GPIOD		87
#define GPIOE		88
#define GPIOF		89
#define GPIOG		90
#define GPIOH		91
#define GPIOI		92
#define GPIOJ		93
#define GPIOK		94
#define GPIOZ		95
#define CRYP1		96
#define HASH1		97
#define RNG1		98
#define BKPSRAM		99
#define MDMA		100
#define GPU		101
#define ETHCK		102
#define ETHTX		103
#define ETHRX		104
#define ETHMAC		105
#define FMC		106
#define QSPI		107
#define SDMMC1		108
#define SDMMC2		109
#define CRC1		110
#define USBH		111
#define ETHSTP		112

/* Kernel clocks */
#define SDMMC1_K	118
#define SDMMC2_K	119
#define SDMMC3_K	120
#define FMC_K		121
#define QSPI_K		122
#define ETHCK_K		123
#define RNG1_K		124
#define RNG2_K		125
#define GPU_K		126
#define USBPHY_K	127
#define STGEN_K		128
#define SPDIF_K		129
#define SPI1_K		130
#define SPI2_K		131
#define SPI3_K		132
#define SPI4_K		133
#define SPI5_K		134
#define SPI6_K		135
#define CEC_K		136
#define I2C1_K		137
#define I2C2_K		138
#define I2C3_K		139
#define I2C4_K		140
#define I2C5_K		141
#define I2C6_K		142
#define LPTIM1_K	143
#define LPTIM2_K	144
#define LPTIM3_K	145
#define LPTIM4_K	146
#define LPTIM5_K	147
#define USART1_K	148
#define USART2_K	149
#define USART3_K	150
#define UART4_K		151
#define UART5_K		152
#define USART6_K	153
#define UART7_K		154
#define UART8_K		155
#define DFSDM_K		156
#define FDCAN_K		157
#define SAI1_K		158
#define SAI2_K		159
#define SAI3_K		160
#define SAI4_K		161
#define ADC12_K		162
#define DSI_K		163
#define DSI_PX		164
#define ADFSDM_K	165
#define USBO_K		166
#define LTDC_PX		167
#define DAC12_K		168
#define ETHPTP_K	169

/* PLL */
#define PLL1		176
#define PLL2		177
#define PLL3		178
#define PLL4		179

/* ODF */
#define PLL1_P		180
#define PLL1_Q		181
#define PLL1_R		182
#define PLL2_P		183
#define PLL2_Q		184
#define PLL2_R		185
#define PLL3_P		186
#define PLL3_Q		187
#define PLL3_R		188
#define PLL4_P		189
#define PLL4_Q		190
#define PLL4_R		191

/* AUX */
#define RTC		192

/* MCLK */
#define CK_PER		193
#define CK_MPU		194
#define CK_AXI		195
#define CK_MCU		196

/* Time base */
#define TIM2_K		197
#define TIM3_K		198
#define TIM4_K		199
#define TIM5_K		200
#define TIM6_K		201
#define TIM7_K		202
#define TIM12_K		203
#define TIM13_K		204
#define TIM14_K		205
#define TIM1_K		206
#define TIM8_K		207
#define TIM15_K		208
#define TIM16_K		209
#define TIM17_K		210

/* MCO clocks */
#define CK_MCO1		211
#define CK_MCO2		212

/* TRACE & DEBUG clocks */
#define DBG		213
#define CK_DBG		214
#define CK_TRACE	215

/* DDR */
#define DDRC1		220
#define DDRC1LP		221
#define DDRC2		222
#define DDRC2LP		223
#define DDRPHYC		224
#define DDRPHYCLP	225
#define DDRCAPB		226
#define DDRCAPBLP	227
#define AXIDCG		228
#define DDRPHYCAPB	229
#define DDRPHYCAPBLP	230
#define DDRPERFM	231

#define STM32MP1_LAST_CLK 232

#define LTDC_K		LTDC_PX
#define ETHMAC_K	ETHCK_K

#endif /* _DT_BINDINGS_STM32MP1_CLKS_H_ */
