/* SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause */
/*
 * Copyright (C) STMicroelectronics 2020 - All Rights Reserved
 * Author: Gabriel Fernandez <gabriel.fernandez@foss.st.com> for STMicroelectronics.
 */

#ifndef _DT_BINDINGS_STM32MP13_CLKS_H_
#define _DT_BINDINGS_STM32MP13_CLKS_H_

/* OSCILLATOR clocks */
#define CK_HSE		0
#define CK_CSI		1
#define CK_LSI		2
#define CK_LSE		3
#define CK_HSI		4
#define CK_HSE_DIV2	5

/* PLL */
#define PLL1		6
#define PLL2		7
#define PLL3		8
#define PLL4		9

/* ODF */
#define PLL1_P		10
#define PLL1_Q		11
#define PLL1_R		12
#define PLL2_P		13
#define PLL2_Q		14
#define PLL2_R		15
#define PLL3_P		16
#define PLL3_Q		17
#define PLL3_R		18
#define PLL4_P		19
#define PLL4_Q		20
#define PLL4_R		21

#define PCLK1		22
#define PCLK2		23
#define PCLK3		24
#define PCLK4		25
#define PCLK5		26
#define PCLK6		27

/* SYSTEM CLOCK */
#define CK_PER		28
#define CK_MPU		29
#define CK_AXI		30
#define CK_MLAHB	31

/* BASE TIMER */
#define CK_TIMG1	32
#define CK_TIMG2	33
#define CK_TIMG3	34

/* AUX */
#define RTC		35

/* TRACE & DEBUG clocks */
#define CK_DBG		36
#define CK_TRACE	37

/* MCO clocks */
#define CK_MCO1		38
#define CK_MCO2		39

/* IP clocks */
#define SYSCFG		40
#define VREF		41
#define DTS		42
#define PMBCTRL		43
#define HDP		44
#define IWDG2		45
#define STGENRO		46
#define USART1		47
#define RTCAPB		48
#define TZC		49
#define TZPC		50
#define IWDG1		51
#define BSEC		52
#define DMA1		53
#define DMA2		54
#define DMAMUX1		55
#define DMAMUX2		56
#define GPIOA		57
#define GPIOB		58
#define GPIOC		59
#define GPIOD		60
#define GPIOE		61
#define GPIOF		62
#define GPIOG		63
#define GPIOH		64
#define GPIOI		65
#define CRYP1		66
#define HASH1		67
#define BKPSRAM		68
#define MDMA		69
#define CRC1		70
#define USBH		71
#define DMA3		72
#define TSC		73
#define PKA		74
#define AXIMC		75
#define MCE		76
#define ETH1TX		77
#define ETH2TX		78
#define ETH1RX		79
#define ETH2RX		80
#define ETH1MAC		81
#define ETH2MAC		82
#define ETH1STP		83
#define ETH2STP		84

/* IP clocks with parents */
#define SDMMC1_K	85
#define SDMMC2_K	86
#define ADC1_K		87
#define ADC2_K		88
#define FMC_K		89
#define QSPI_K		90
#define RNG1_K		91
#define USBPHY_K	92
#define STGEN_K		93
#define SPDIF_K		94
#define SPI1_K		95
#define SPI2_K		96
#define SPI3_K		97
#define SPI4_K		98
#define SPI5_K		99
#define I2C1_K		100
#define I2C2_K		101
#define I2C3_K		102
#define I2C4_K		103
#define I2C5_K		104
#define TIM2_K		105
#define TIM3_K		106
#define TIM4_K		107
#define TIM5_K		108
#define TIM6_K		109
#define TIM7_K		110
#define TIM12_K		111
#define TIM13_K		112
#define TIM14_K		113
#define TIM1_K		114
#define TIM8_K		115
#define TIM15_K		116
#define TIM16_K		117
#define TIM17_K		118
#define LPTIM1_K	119
#define LPTIM2_K	120
#define LPTIM3_K	121
#define LPTIM4_K	122
#define LPTIM5_K	123
#define USART1_K	124
#define USART2_K	125
#define USART3_K	126
#define UART4_K		127
#define UART5_K		128
#define USART6_K	129
#define UART7_K		130
#define UART8_K		131
#define DFSDM_K		132
#define FDCAN_K		133
#define SAI1_K		134
#define SAI2_K		135
#define ADFSDM_K	136
#define USBO_K		137
#define LTDC_PX		138
#define ETH1CK_K	139
#define ETH1PTP_K	140
#define ETH2CK_K	141
#define ETH2PTP_K	142
#define DCMIPP_K	143
#define SAES_K		144
#define DTS_K		145

/* DDR */
#define DDRC1		146
#define DDRC1LP		147
#define DDRC2		148
#define DDRC2LP		149
#define DDRPHYC		150
#define DDRPHYCLP	151
#define DDRCAPB		152
#define DDRCAPBLP	153
#define AXIDCG		154
#define DDRPHYCAPB	155
#define DDRPHYCAPBLP	156
#define DDRPERFM	157

#define ADC1		158
#define ADC2		159
#define SAI1		160
#define SAI2		161

#define STM32MP1_LAST_CLK 162

/* SCMI clock identifiers */
#define CK_SCMI_HSE		0
#define CK_SCMI_HSI		1
#define CK_SCMI_CSI		2
#define CK_SCMI_LSE		3
#define CK_SCMI_LSI		4
#define CK_SCMI_HSE_DIV2	5
#define CK_SCMI_PLL2_Q		6
#define CK_SCMI_PLL2_R		7
#define CK_SCMI_PLL3_P		8
#define CK_SCMI_PLL3_Q		9
#define CK_SCMI_PLL3_R		10
#define CK_SCMI_PLL4_P		11
#define CK_SCMI_PLL4_Q		12
#define CK_SCMI_PLL4_R		13
#define CK_SCMI_MPU		14
#define CK_SCMI_AXI		15
#define CK_SCMI_MLAHB		16
#define CK_SCMI_CKPER		17
#define CK_SCMI_PCLK1		18
#define CK_SCMI_PCLK2		19
#define CK_SCMI_PCLK3		20
#define CK_SCMI_PCLK4		21
#define CK_SCMI_PCLK5		22
#define CK_SCMI_PCLK6		23
#define CK_SCMI_CKTIMG1		24
#define CK_SCMI_CKTIMG2		25
#define CK_SCMI_CKTIMG3		26
#define CK_SCMI_RTC		27
#define CK_SCMI_RTCAPB		28

#endif /* _DT_BINDINGS_STM32MP13_CLKS_H_ */
