/* SPDX-License-Identifier: GPL-2.0 */
#ifndef WORKQUEUES_H
#define WORKQUEUES_H

#include <stdbool.h>

#include "barriers.h"
#include "bug_on.h"
#include "int_typedefs.h"

#include <linux/types.h>

/* Stub workqueue implementation. */

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);
void delayed_work_timer_fn(unsigned long __data);

struct work_struct {
/*	atomic_long_t data; */
	unsigned long data;

	struct list_head entry;
	work_func_t func;
#ifdef CONFIG_LOCKDEP
	struct lockdep_map lockdep_map;
#endif
};

struct timer_list {
	struct hlist_node	entry;
	unsigned long		expires;
	void			(*function)(unsigned long);
	unsigned long		data;
	u32			flags;
	int			slack;
};

struct delayed_work {
	struct work_struct work;
	struct timer_list timer;

	/* target workqueue and CPU ->timer uses to queue ->work */
	struct workqueue_struct *wq;
	int cpu;
};


static inline bool schedule_work(struct work_struct *work)
{
	BUG();
	return true;
}

static inline bool schedule_work_on(int cpu, struct work_struct *work)
{
	BUG();
	return true;
}

static inline bool queue_work(struct workqueue_struct *wq,
			      struct work_struct *work)
{
	BUG();
	return true;
}

static inline bool queue_delayed_work(struct workqueue_struct *wq,
				      struct delayed_work *dwork,
				      unsigned long delay)
{
	BUG();
	return true;
}

#define INIT_WORK(w, f) \
	do { \
		(w)->data = 0; \
		(w)->func = (f); \
	} while (0)

#define INIT_DELAYED_WORK(w, f) INIT_WORK(&(w)->work, (f))

#define __WORK_INITIALIZER(n, f) { \
		.data = 0, \
		.entry = { &(n).entry, &(n).entry }, \
		.func = f \
	}

/* Don't bother initializing timer. */
#define __DELAYED_WORK_INITIALIZER(n, f, tflags) { \
	.work = __WORK_INITIALIZER((n).work, (f)), \
	}

#define DECLARE_WORK(n, f) \
	struct workqueue_struct n = __WORK_INITIALIZER

#define DECLARE_DELAYED_WORK(n, f) \
	struct delayed_work n = __DELAYED_WORK_INITIALIZER(n, f, 0)

#define system_power_efficient_wq ((struct workqueue_struct *) NULL)

#endif
