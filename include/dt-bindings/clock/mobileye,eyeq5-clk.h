/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (C) 2024 Mobileye Vision Technologies Ltd.
 */

#ifndef _DT_BINDINGS_CLOCK_MOBILEYE_EYEQ5_CLK_H
#define _DT_BINDINGS_CLOCK_MOBILEYE_EYEQ5_CLK_H

#define EQ5C_PLL_CPU		0
#define EQ5C_PLL_VMP		1
#define EQ5C_PLL_PMA		2
#define EQ5C_PLL_VDI		3
#define EQ5C_PLL_DDR0		4
#define EQ5C_PLL_PCI		5
#define EQ5C_PLL_PER		6
#define EQ5C_PLL_PMAC		7
#define EQ5C_PLL_MPC		8
#define EQ5C_PLL_DDR1		9

#define EQ5C_DIV_OSPI		10

/* EQ5C_PLL_CPU children */
#define EQ5C_CPU_CORE0		11
#define EQ5C_CPU_CORE1		12
#define EQ5C_CPU_CORE2		13
#define EQ5C_CPU_CORE3		14

/* EQ5C_PLL_PER children */
#define EQ5C_PER_OCC		15
#define EQ5C_PER_UART		16
#define EQ5C_PER_SPI		17
#define EQ5C_PER_I2C		18
#define EQ5C_PER_GPIO		19
#define EQ5C_PER_EMMC		20
#define EQ5C_PER_OCC_PCI	21

#define EQ6LC_PLL_DDR		0
#define EQ6LC_PLL_CPU		1
#define EQ6LC_PLL_PER		2
#define EQ6LC_PLL_VDI		3

#define EQ6HC_CENTRAL_PLL_CPU	0
#define EQ6HC_CENTRAL_CPU_OCC	1

#define EQ6HC_WEST_PLL_PER	0
#define EQ6HC_WEST_PER_OCC	1
#define EQ6HC_WEST_PER_UART	2

#define EQ6HC_SOUTH_PLL_VDI		0
#define EQ6HC_SOUTH_PLL_PCIE		1
#define EQ6HC_SOUTH_PLL_PER		2
#define EQ6HC_SOUTH_PLL_ISP		3

#define EQ6HC_SOUTH_DIV_EMMC		4
#define EQ6HC_SOUTH_DIV_OSPI_REF	5
#define EQ6HC_SOUTH_DIV_OSPI_SYS	6
#define EQ6HC_SOUTH_DIV_TSU		7

#define EQ6HC_ACC_PLL_XNN		0
#define EQ6HC_ACC_PLL_VMP		1
#define EQ6HC_ACC_PLL_PMA		2
#define EQ6HC_ACC_PLL_MPC		3
#define EQ6HC_ACC_PLL_NOC		4

#endif
