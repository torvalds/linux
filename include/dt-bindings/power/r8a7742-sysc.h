/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 */
#ifndef __DT_BINDINGS_POWER_R8A7742_SYSC_H__
#define __DT_BINDINGS_POWER_R8A7742_SYSC_H__

/*
 * These power domain indices match the numbers of the interrupt bits
 * representing the power areas in the various Interrupt Registers
 * (e.g. SYSCISR, Interrupt Status Register)
 */

#define R8A7742_PD_CA15_CPU0		 0
#define R8A7742_PD_CA15_CPU1		 1
#define R8A7742_PD_CA15_CPU2		 2
#define R8A7742_PD_CA15_CPU3		 3
#define R8A7742_PD_CA7_CPU0		 5
#define R8A7742_PD_CA7_CPU1		 6
#define R8A7742_PD_CA7_CPU2		 7
#define R8A7742_PD_CA7_CPU3		 8
#define R8A7742_PD_CA15_SCU		12
#define R8A7742_PD_RGX			20
#define R8A7742_PD_CA7_SCU		21

/* Always-on power area */
#define R8A7742_PD_ALWAYS_ON		32

#endif /* __DT_BINDINGS_POWER_R8A7742_SYSC_H__ */
