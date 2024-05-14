/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2018 Renesas Electronics Corp.
 */
#ifndef __DT_BINDINGS_POWER_R8A774C0_SYSC_H__
#define __DT_BINDINGS_POWER_R8A774C0_SYSC_H__

/*
 * These power domain indices match the numbers of the interrupt bits
 * representing the power areas in the various Interrupt Registers
 * (e.g. SYSCISR, Interrupt Status Register)
 */

#define R8A774C0_PD_CA53_CPU0		5
#define R8A774C0_PD_CA53_CPU1		6
#define R8A774C0_PD_A3VC		14
#define R8A774C0_PD_3DG_A		17
#define R8A774C0_PD_3DG_B		18
#define R8A774C0_PD_CA53_SCU		21
#define R8A774C0_PD_A2VC1		26

/* Always-on power area */
#define R8A774C0_PD_ALWAYS_ON		32

#endif /* __DT_BINDINGS_POWER_R8A774C0_SYSC_H__ */
