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
	int				bit;
};

#ifdef CONFIG_UNWIND_USER

enum {
	UNWIND_PENDING_BIT = 0,
	UNWIND_USED_BIT,
};

enum {
	UNWIND_PENDING		= BIT(UNWIND_PENDING_BIT),

	/* Set if the unwinding was used (directly or deferred) */
	UNWIND_USED		= BIT(UNWIND_USED_BIT)
};

void unwind_task_init(struct task_struct *task);
void unwind_task_free(struct task_struct *task);

int unwind_user_faultable(struct unwind_stacktrace *trace);

int unwind_deferred_init(struct unwind_work *work, unwind_callback_t func);
int unwind_deferred_request(struct unwind_work *work, u64 *cookie);
void unwind_deferred_cancel(struct unwind_work *work);

void unwind_deferred_task_exit(struct task_struct *task);

static __always_inline void unwind_reset_info(void)
{
	struct unwind_task_info *info = &current->unwind_info;
	unsigned long bits;

	/* Was there any unwinding? */
	if (unlikely(info->unwind_mask)) {
		bits = info->unwind_mask;
		do {
			/* Is a task_work going to run again before going back */
			if (bits & UNWIND_PENDING)
				return;
		} while (!try_cmpxchg(&info->unwind_mask, &bits, 0UL));
		current->unwind_info.id.id = 0;

		if (unlikely(info->cache)) {
			info->cache->nr_entries = 0;
			info->cache->unwind_completed = 0;
		}
	}
}

#else /* !CONFIG_UNWIND_USER */

static inline void unwind_task_init(struct task_struct *task) {}
static inline void unwind_task_free(struct task_struct *task) {}

static inline int unwind_user_faultable(struct unwind_stacktrace *trace) { return -ENOSYS; }
static inline int unwind_deferred_init(struct unwind_work *work, unwind_callback_t func) { return -ENOSYS; }
static inline int unwind_deferred_request(struct unwind_work *work, u64 *timestamp) { return -ENOSYS; }
static inline void unwind_deferred_cancel(struct unwind_work *work) {}

static inline void unwind_deferred_task_exit(struct task_struct *task) {}
static inline void unwind_reset_info(void) {}

#endif /* !CONFIG_UNWIND_USER */

#endif /* _LINUX_UNWIND_USER_DEFERRED_H */
