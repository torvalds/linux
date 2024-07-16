/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Glider bvba
 */
#ifndef __DT_BINDINGS_POWER_R8A7779_SYSC_H__
#define __DT_BINDINGS_POWER_R8A7779_SYSC_H__

/*
 * These power domain indices match the numbers of the interrupt bits
 * representing the power areas in the various Interrupt Registers
 * (e.g. SYSCISR, Interrupt Status Register)
 */

#define R8A7779_PD_ARM1			 1
#define R8A7779_PD_ARM2			 2
#define R8A7779_PD_ARM3			 3
#define R8A7779_PD_SGX			20
#define R8A7779_PD_VDP			21
#define R8A7779_PD_IMP			24

/* Always-on power area */
#define R8A7779_PD_ALWAYS_ON		32

#endif /* __DT_BINDINGS_POWER_R8A7779_SYSC_H__ */
