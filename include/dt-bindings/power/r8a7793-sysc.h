/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Glider bvba
 */
#ifndef __DT_BINDINGS_POWER_R8A7793_SYSC_H__
#define __DT_BINDINGS_POWER_R8A7793_SYSC_H__

/*
 * These power domain indices match the numbers of the interrupt bits
 * representing the power areas in the various Interrupt Registers
 * (e.g. SYSCISR, Interrupt Status Register)
 *
 * Note that R-Car M2-N is identical to R-Car M2-W w.r.t. power domains.
 */

#define R8A7793_PD_CA15_CPU0		 0
#define R8A7793_PD_CA15_CPU1		 1
#define R8A7793_PD_CA15_SCU		12
#define R8A7793_PD_SH_4A		16
#define R8A7793_PD_SGX			20

/* Always-on power area */
#define R8A7793_PD_ALWAYS_ON		32

#endif /* __DT_BINDINGS_POWER_R8A7793_SYSC_H__ */
