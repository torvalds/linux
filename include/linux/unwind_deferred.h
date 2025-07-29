/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UNWIND_USER_DEFERRED_H
#define _LINUX_UNWIND_USER_DEFERRED_H

#include <linux/task_work.h>
#include <linux/unwind_user.h>
#include <linux/unwind_deferred_types.h>

struct unwind_work;

typedef void (*unwind_callback_t)(struct unwind_work *work, struct unwind_stacktrace *trace, u64 cookie);

struct unwind_work {
	struct list_head		list;
	unwind_callback_t		func;
};

#ifdef CONFIG_UNWIND_USER

void unwind_task_init(struct task_struct *task);
void unwind_task_free(struct task_struct *task);

int unwind_user_faultable(struct unwind_stacktrace *trace);

int unwind_deferred_init(struct unwind_work *work, unwind_callback_t func);
int unwind_deferred_request(struct unwind_work *work, u64 *cookie);
void unwind_deferred_cancel(struct unwind_work *work);

static __always_inline void unwind_reset_info(void)
{
	if (unlikely(current->unwind_info.id.id))
		current->unwind_info.id.id = 0;
	/*
	 * As unwind_user_faultable() can be called directly and
	 * depends on nr_entries being cleared on exit to user,
	 * this needs to be a separate conditional.
	 */
	if (unlikely(current->unwind_info.cache))
		current->unwind_info.cache->nr_entries = 0;
}

#else /* !CONFIG_UNWIND_USER */

static inline void unwind_task_init(struct task_struct *task) {}
static inline void unwind_task_free(struct task_struct *task) {}

static inline int unwind_user_faultable(struct unwind_stacktrace *trace) { return -ENOSYS; }
static inline int unwind_deferred_init(struct unwind_work *work, unwind_callback_t func) { return -ENOSYS; }
static inline int unwind_deferred_request(struct unwind_work *work, u64 *timestamp) { return -ENOSYS; }
static inline void unwind_deferred_cancel(struct unwind_work *work) {}

static inline void unwind_reset_info(void) {}

#endif /* !CONFIG_UNWIND_USER */

#endif /* _LINUX_UNWIND_USER_DEFERRED_H */
