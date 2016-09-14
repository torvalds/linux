/*
 * Copyright (C) 2016 Glider bvba
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef __DT_BINDINGS_POWER_R8A7794_SYSC_H__
#define __DT_BINDINGS_POWER_R8A7794_SYSC_H__

/*
 * These power domain indices match the numbers of the interrupt bits
 * representing the power areas in the various Interrupt Registers
 * (e.g. SYSCISR, Interrupt Status Register)
 */

#define R8A7794_PD_CA7_CPU0		 5
#define R8A7794_PD_CA7_CPU1		 6
#define R8A7794_PD_SH_4A		16
#define R8A7794_PD_SGX			20
#define R8A7794_PD_CA7_SCU		21

/* Always-on power area */
#define R8A7794_PD_ALWAYS_ON		32

#endif /* __DT_BINDINGS_POWER_R8A7794_SYSC_H__ */
