/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PLAT_RK_FIQ_DEBUGGER_H
#define __PLAT_RK_FIQ_DEBUGGER_H

#ifdef CONFIG_FIQ_DEBUGGER_TRUST_ZONE
void fiq_debugger_fiq(void *regs, u32 cpu);

#ifdef CONFIG_ARM_SDE_INTERFACE
int sdei_fiq_debugger_is_enabled(void);
int fiq_sdei_event_enable(u32 event_num);
int fiq_sdei_event_routing_set(u32 event_num, unsigned long flags,
			       unsigned long affinity);
int fiq_sdei_event_disable(u32 event_num);
#else
static inline int sdei_fiq_debugger_is_enabled(void)
{
	return 0;
}
#endif
#endif

#endif
