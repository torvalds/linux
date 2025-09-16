/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_LIVEPATCH_SCHED_H_
#define _LINUX_LIVEPATCH_SCHED_H_

#include <linux/jump_label.h>
#include <linux/sched.h>

#ifdef CONFIG_LIVEPATCH

void __klp_sched_try_switch(void);

DECLARE_STATIC_KEY_FALSE(klp_sched_try_switch_key);

static __always_inline void klp_sched_try_switch(struct task_struct *curr)
{
	if (static_branch_unlikely(&klp_sched_try_switch_key) &&
	    READ_ONCE(curr->__state) & TASK_FREEZABLE)
		__klp_sched_try_switch();
}

#else /* !CONFIG_LIVEPATCH */
static inline void klp_sched_try_switch(struct task_struct *curr) {}
#endif /* CONFIG_LIVEPATCH */

#endif /* _LINUX_LIVEPATCH_SCHED_H_ */
