/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_LIVEPATCH_SCHED_H_
#define _LINUX_LIVEPATCH_SCHED_H_

#include <linux/jump_label.h>
#include <linux/static_call_types.h>

#ifdef CONFIG_LIVEPATCH

void __klp_sched_try_switch(void);

#if !defined(CONFIG_PREEMPT_DYNAMIC) || !defined(CONFIG_HAVE_PREEMPT_DYNAMIC_CALL)

DECLARE_STATIC_KEY_FALSE(klp_sched_try_switch_key);

static __always_inline void klp_sched_try_switch(void)
{
	if (static_branch_unlikely(&klp_sched_try_switch_key))
		__klp_sched_try_switch();
}

#endif /* !CONFIG_PREEMPT_DYNAMIC || !CONFIG_HAVE_PREEMPT_DYNAMIC_CALL */

#else /* !CONFIG_LIVEPATCH */
static inline void klp_sched_try_switch(void) {}
static inline void __klp_sched_try_switch(void) {}
#endif /* CONFIG_LIVEPATCH */

#endif /* _LINUX_LIVEPATCH_SCHED_H_ */
