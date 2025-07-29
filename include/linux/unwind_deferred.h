/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UNWIND_USER_DEFERRED_H
#define _LINUX_UNWIND_USER_DEFERRED_H

#include <linux/unwind_user.h>
#include <linux/unwind_deferred_types.h>

#ifdef CONFIG_UNWIND_USER

void unwind_task_init(struct task_struct *task);
void unwind_task_free(struct task_struct *task);

int unwind_user_faultable(struct unwind_stacktrace *trace);

static __always_inline void unwind_reset_info(void)
{
	if (unlikely(current->unwind_info.cache))
		current->unwind_info.cache->nr_entries = 0;
}

#else /* !CONFIG_UNWIND_USER */

static inline void unwind_task_init(struct task_struct *task) {}
static inline void unwind_task_free(struct task_struct *task) {}

static inline int unwind_user_faultable(struct unwind_stacktrace *trace) { return -ENOSYS; }

static inline void unwind_reset_info(void) {}

#endif /* !CONFIG_UNWIND_USER */

#endif /* _LINUX_UNWIND_USER_DEFERRED_H */
