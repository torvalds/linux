/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PLAT_RK_FIQ_DEBUGGER_H
#define __PLAT_RK_FIQ_DEBUGGER_H

#ifdef CONFIG_FIQ_DEBUGGER_TRUST_ZONE
void fiq_debugger_fiq(void *regs, u32 cpu);
#endif

#endif
