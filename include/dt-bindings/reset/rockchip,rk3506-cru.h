/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2023-2025 Rockchip Electronics Co., Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 */

#ifndef _DT_BINDINGS_REST_ROCKCHIP_RK3506_H
#define _DT_BINDINGS_REST_ROCKCHIP_RK3506_H

/* CRU-->SOFTRST_CON00 */
#define SRST_NCOREPORESET0_AC	0
#define SRST_NCOREPORESET1_AC	1
#define SRST_NCOREPORESET2_AC	2
#define SRST_NCORESET0_AC	3
#define SRST_NCORESET1_AC	4
#define SRST_NCORESET2_AC	5
#define SRST_NL2RESET_AC	6
#define SRST_A_CORE_BIU_AC	7
#define SRST_H_M0_AC		8

/* CRU-->SOFTRST_CON02 */
#define SRST_NDBGRESET		9
#define SRST_P_CORE_BIU		10
#define SRST_PMU		11

/* CRU-->SOFTRST_CON03 */
#define SRST_P_DBG		12
#define SRST_POT_DBG		13
#define SRST_P_CORE_GRF		14
#define SRST_CORE_EMA_DETECT	15
#define SRST_REF_PVTPLL_CORE	16
#define SRST_P_GPIO1		17
#define SRST_DB_GPIO1		18

/* CRU-->SOFTRST_CON04 */
#define SRST_A_CORE_PERI_BIU	19
#define SRST_A_DSMC		20
#define SRST_P_DSMC		21
#define SRST_FLEXBUS		22
#define SRST_A_FLEXBUS		23
#define SRST_H_FLEXBUS		24
#define SRST_A_DSMC_SLV		25
#define SRST_H_DSMC_SLV		26
#define SRST_DSMC_SLV		27

/* CRU-->SOFTRST_CON05 */
#define SRST_A_BUS_BIU		28
#define SRST_H_BUS_BIU		29
#define SRST_P_BUS_BIU		30
#define SRST_A_SYSRAM		31
#define SRST_H_SYSRAM		32
#define SRST_A_DMAC0		33
#define SRST_A_DMAC1		34
#define SRST_H_M0		35
#define SRST_M0_JTAG		36
#define SRST_H_CRYPTO		37

/* CRU-->SOFTRST_CON06 */
#define SRST_H_RNG		38
#define SRST_P_BUS_GRF		39
#define SRST_P_TIMER0		40
#define SRST_TIMER0_CH0		41
#define SRST_TIMER0_CH1		42
#define SRST_TIMER0_CH2		43
#define SRST_TIMER0_CH3		44
#define SRST_TIMER0_CH4		45
#define SRST_TIMER0_CH5		46
#define SRST_P_WDT0		47
#define SRST_T_WDT0		48
#define SRST_P_WDT1		49
#define SRST_T_WDT1		50
#define SRST_P_MAILBOX		51
#define SRST_P_INTMUX		52
#define SRST_P_SPINLOCK		53

/* CRU-->SOFTRST_CON07 */
#define SRST_P_DDRC		54
#define SRST_H_DDRPHY		55
#define SRST_P_DDRMON		56
#define SRST_DDRMON_OSC		57
#define SRST_P_DDR_LPC		58
#define SRST_H_USBOTG0		59
#define SRST_USBOTG0_ADP	60
#define SRST_H_USBOTG1		61
#define SRST_USBOTG1_ADP	62
#define SRST_P_USBPHY		63
#define SRST_USBPHY_POR		64
#define SRST_USBPHY_OTG0	65
#define SRST_USBPHY_OTG1	66

/* CRU-->SOFTRST_CON08 */
#define SRST_A_DMA2DDR		67
#define SRST_P_DMA2DDR		68

/* CRU-->SOFTRST_CON09 */
#define SRST_USBOTG0_UTMI	69
#define SRST_USBOTG1_UTMI	70

/* CRU-->SOFTRST_CON10 */
#define SRST_A_DDRC_0		71
#define SRST_A_DDRC_1		72
#define SRST_A_DDR_BIU		73
#define SRST_DDRC		74
#define SRST_DDRMON		75

/* CRU-->SOFTRST_CON11 */
#define SRST_H_LSPERI_BIU	76
#define SRST_P_UART0		77
#define SRST_P_UART1		78
#define SRST_P_UART2		79
#define SRST_P_UART3		80
#define SRST_P_UART4		81
#define SRST_UART0		82
#define SRST_UART1		83
#define SRST_UART2		84
#define SRST_UART3		85
#define SRST_UART4		86
#define SRST_P_I2C0		87
#define SRST_I2C0		88

/* CRU-->SOFTRST_CON12 */
#define SRST_P_I2C1		89
#define SRST_I2C1		90
#define SRST_P_I2C2		91
#define SRST_I2C2		92
#define SRST_P_PWM1		93
#define SRST_PWM1		94
#define SRST_P_SPI0		95
#define SRST_SPI0		96
#define SRST_P_SPI1		97
#define SRST_SPI1		98
#define SRST_P_GPIO2		99
#define SRST_DB_GPIO2		100

/* CRU-->SOFTRST_CON13 */
#define SRST_P_GPIO3		101
#define SRST_DB_GPIO3		102
#define SRST_P_GPIO4		103
#define SRST_DB_GPIO4		104
#define SRST_H_CAN0		105
#define SRST_CAN0		106
#define SRST_H_CAN1		107
#define SRST_CAN1		108
#define SRST_H_PDM		109
#define SRST_M_PDM		110
#define SRST_PDM		111
#define SRST_SPDIFTX		112
#define SRST_H_SPDIFTX		113
#define SRST_H_SPDIFRX		114
#define SRST_SPDIFRX		115
#define SRST_M_SAI0		116

/* CRU-->SOFTRST_CON14 */
#define SRST_H_SAI0		117
#define SRST_M_SAI1		118
#define SRST_H_SAI1		119
#define SRST_H_ASRC0		120
#define SRST_ASRC0		121
#define SRST_H_ASRC1		122
#define SRST_ASRC1		123

/* CRU-->SOFTRST_CON17 */
#define SRST_H_HSPERI_BIU	124
#define SRST_H_SDMMC		125
#define SRST_H_FSPI		126
#define SRST_S_FSPI		127
#define SRST_P_SPI2		128
#define SRST_A_MAC0		129
#define SRST_A_MAC1		130

/* CRU-->SOFTRST_CON18 */
#define SRST_M_SAI2		131
#define SRST_H_SAI2		132
#define SRST_H_SAI3		133
#define SRST_M_SAI3		134
#define SRST_H_SAI4		135
#define SRST_M_SAI4		136
#define SRST_H_DSM		137
#define SRST_M_DSM		138
#define SRST_P_AUDIO_ADC	139
#define SRST_M_AUDIO_ADC	140

/* CRU-->SOFTRST_CON19 */
#define SRST_P_SARADC		141
#define SRST_SARADC		142
#define SRST_SARADC_PHY		143
#define SRST_P_OTPC_NS		144
#define SRST_SBPI_OTPC_NS	145
#define SRST_USER_OTPC_NS	146
#define SRST_P_UART5		147
#define SRST_UART5		148
#define SRST_P_GPIO234_IOC	149

/* CRU-->SOFTRST_CON21 */
#define SRST_A_VIO_BIU		150
#define SRST_H_VIO_BIU		151
#define SRST_H_RGA		152
#define SRST_A_RGA		153
#define SRST_CORE_RGA		154
#define SRST_A_VOP		155
#define SRST_H_VOP		156
#define SRST_VOP		157
#define SRST_P_DPHY		158
#define SRST_P_DSI_HOST		159
#define SRST_P_TSADC		160
#define SRST_TSADC		161

/* CRU-->SOFTRST_CON22 */
#define SRST_P_GPIO1_IOC	162

#endif
