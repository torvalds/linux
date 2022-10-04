/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Daire McNamara,<daire.mcnamara@microchip.com>
 * Copyright (C) 2020-2022 Microchip Technology Inc.  All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_MICROCHIP_MPFS_H_
#define _DT_BINDINGS_CLK_MICROCHIP_MPFS_H_

#define CLK_CPU		0
#define CLK_AXI		1
#define CLK_AHB		2

#define CLK_ENVM	3
#define CLK_MAC0	4
#define CLK_MAC1	5
#define CLK_MMC		6
#define CLK_TIMER	7
#define CLK_MMUART0	8
#define CLK_MMUART1	9
#define CLK_MMUART2	10
#define CLK_MMUART3	11
#define CLK_MMUART4	12
#define CLK_SPI0	13
#define CLK_SPI1	14
#define CLK_I2C0	15
#define CLK_I2C1	16
#define CLK_CAN0	17
#define CLK_CAN1	18
#define CLK_USB		19
#define CLK_RESERVED	20
#define CLK_RTC		21
#define CLK_QSPI	22
#define CLK_GPIO0	23
#define CLK_GPIO1	24
#define CLK_GPIO2	25
#define CLK_DDRC	26
#define CLK_FIC0	27
#define CLK_FIC1	28
#define CLK_FIC2	29
#define CLK_FIC3	30
#define CLK_ATHENA	31
#define CLK_CFM		32

#define CLK_RTCREF	33
#define CLK_MSSPLL	34

/* Clock Conditioning Circuitry Clock IDs */

#define CLK_CCC_PLL0		0
#define CLK_CCC_PLL1		1
#define CLK_CCC_DLL0		2
#define CLK_CCC_DLL1		3

#define CLK_CCC_PLL0_OUT0	4
#define CLK_CCC_PLL0_OUT1	5
#define CLK_CCC_PLL0_OUT2	6
#define CLK_CCC_PLL0_OUT3	7

#define CLK_CCC_PLL1_OUT0	8
#define CLK_CCC_PLL1_OUT1	9
#define CLK_CCC_PLL1_OUT2	10
#define CLK_CCC_PLL1_OUT3	11

#define CLK_CCC_DLL0_OUT0	12
#define CLK_CCC_DLL0_OUT1	13

#define CLK_CCC_DLL1_OUT0	14
#define CLK_CCC_DLL1_OUT1	15

#endif	/* _DT_BINDINGS_CLK_MICROCHIP_MPFS_H_ */
