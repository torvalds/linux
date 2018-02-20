/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Jacopo Mondi <jacopo+renesas@jmondi.org>
 * Copyright (C) 2016 Glider bvba
 */

#ifndef __DT_BINDINGS_POWER_R8A77965_SYSC_H__
#define __DT_BINDINGS_POWER_R8A77965_SYSC_H__

/*
 * These power domain indices match the numbers of the interrupt bits
 * representing the power areas in the various Interrupt Registers
 * (e.g. SYSCISR, Interrupt Status Register)
 */

#define R8A77965_PD_CA57_CPU0		 0
#define R8A77965_PD_CA57_CPU1		 1
#define R8A77965_PD_A3VP		 9
#define R8A77965_PD_CA57_SCU		12
#define R8A77965_PD_CR7			13
#define R8A77965_PD_A3VC		14
#define R8A77965_PD_3DG_A		17
#define R8A77965_PD_3DG_B		18
#define R8A77965_PD_A3IR		24
#define R8A77965_PD_A2VC1		26

/* Always-on power area */
#define R8A77965_PD_ALWAYS_ON		32

#endif /* __DT_BINDINGS_POWER_R8A77965_SYSC_H__ */
