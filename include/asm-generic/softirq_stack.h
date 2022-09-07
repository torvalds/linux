/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __ASM_GENERIC_SOFTIRQ_STACK_H
#define __ASM_GENERIC_SOFTIRQ_STACK_H

#if defined(CONFIG_HAVE_SOFTIRQ_ON_OWN_STACK) && !defined(CONFIG_PREEMPT_RT)
void do_softirq_own_stack(void);
#else
static inline void do_softirq_own_stack(void)
{
	__do_softirq();
}
#endif

#endif
