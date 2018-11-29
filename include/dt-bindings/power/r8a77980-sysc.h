/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2018 Renesas Electronics Corp.
 * Copyright (C) 2018 Cogent Embedded, Inc.
 */
#ifndef __DT_BINDINGS_POWER_R8A77980_SYSC_H__
#define __DT_BINDINGS_POWER_R8A77980_SYSC_H__

/*
 * These power domain indices match the numbers of the interrupt bits
 * representing the power areas in the various Interrupt Registers
 * (e.g. SYSCISR, Interrupt Status Register)
 */

#define R8A77980_PD_A2SC2		0
#define R8A77980_PD_A2SC3		1
#define R8A77980_PD_A2SC4		2
#define R8A77980_PD_A2DP0		3
#define R8A77980_PD_A2DP1		4
#define R8A77980_PD_CA53_CPU0		5
#define R8A77980_PD_CA53_CPU1		6
#define R8A77980_PD_CA53_CPU2		7
#define R8A77980_PD_CA53_CPU3		8
#define R8A77980_PD_A2CN		10
#define R8A77980_PD_A3VIP		11
#define R8A77980_PD_A2IR5		12
#define R8A77980_PD_CR7			13
#define R8A77980_PD_A2IR4		15
#define R8A77980_PD_CA53_SCU		21
#define R8A77980_PD_A2IR0		23
#define R8A77980_PD_A3IR		24
#define R8A77980_PD_A3VIP1		25
#define R8A77980_PD_A3VIP2		26
#define R8A77980_PD_A2IR1		27
#define R8A77980_PD_A2IR2		28
#define R8A77980_PD_A2IR3		29
#define R8A77980_PD_A2SC0		30
#define R8A77980_PD_A2SC1		31

/* Always-on power area */
#define R8A77980_PD_ALWAYS_ON		32

#endif /* __DT_BINDINGS_POWER_R8A77980_SYSC_H__ */
