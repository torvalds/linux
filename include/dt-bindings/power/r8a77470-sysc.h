/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2018 Renesas Electronics Corp.
 */
#ifndef __DT_BINDINGS_POWER_R8A77470_SYSC_H__
#define __DT_BINDINGS_POWER_R8A77470_SYSC_H__

/*
 * These power domain indices match the numbers of the interrupt bits
 * representing the power areas in the various Interrupt Registers
 * (e.g. SYSCISR, Interrupt Status Register)
 */

#define R8A77470_PD_CA7_CPU0		 5
#define R8A77470_PD_CA7_CPU1		 6
#define R8A77470_PD_SGX			20
#define R8A77470_PD_CA7_SCU		21

/* Always-on power area */
#define R8A77470_PD_ALWAYS_ON		32

#endif /* __DT_BINDINGS_POWER_R8A77470_SYSC_H__ */
