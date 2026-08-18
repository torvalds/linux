// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit helpers for backtrace suppression
 *
 * Copyright (C) 2025 Alessandro Carminati <acarmina@redhat.com>
 * Copyright (C) 2024 Guenter Roeck <linux@roeck-us.net>
 */

#include <kunit/resource.h>
#include <linux/export.h>
#include <linux/rculist.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/spinlock.h>

#include "hooks-impl.h"

struct kunit_suppressed_warning {
	struct list_head node;
	struct task_struct *task;
	struct kunit *test;
	atomic_t counter;
};

static LIST_HEAD(suppressed_warnings);
static DEFINE_SPINLOCK(suppressed_warnings_lock);

static void kunit_suppress_warning_remove(struct kunit_suppressed_warning *w)
{
	unsigned long flags;

	spin_lock_irqsave(&suppressed_warnings_lock, flags);
	list_del_rcu(&w->node);
	spin_unlock_irqrestore(&suppressed_warnings_lock, flags);
	put_task_struct(w->task);
}

KUNIT_DEFINE_ACTION_WRAPPER(kunit_suppress_warning_cleanup,
			    kunit_suppress_warning_remove,
			    struct kunit_suppressed_warning *);

bool kunit_has_active_suppress_warning(void)
{
	return __kunit_is_suppressed_warning_impl(false);
}
EXPORT_SYMBOL_GPL(kunit_has_active_suppress_warning);

struct kunit_suppressed_warning *
kunit_start_suppress_warning(struct kunit *test)
{
	struct kunit_suppressed_warning *w;
	unsigned long flags;
	int ret;

	if (kunit_has_active_suppress_warning()) {
		KUNIT_FAIL(test, "Another suppression block is already active");
		return NULL;
	}

	w = kunit_kzalloc(test, sizeof(*w), GFP_KERNEL);
	if (!w) {
		KUNIT_FAIL(test, "Failed to allocate suppression handle.");
		return NULL;
	}

	w->task = get_task_struct(current);
	w->test = test;

	spin_lock_irqsave(&suppressed_warnings_lock, flags);
	list_add_rcu(&w->node, &suppressed_warnings);
	spin_unlock_irqrestore(&suppressed_warnings_lock, flags);

	ret = kunit_add_action_or_reset(test,
					kunit_suppress_warning_cleanup, w);
	if (ret) {
		KUNIT_FAIL(test, "Failed to add suppression cleanup action.");
		return NULL;
	}

	return w;
}
EXPORT_SYMBOL_GPL(kunit_start_suppress_warning);

void kunit_end_suppress_warning(struct kunit *test,
				struct kunit_suppressed_warning *w)
{
	if (!w)
		return;
	kunit_release_action(test, kunit_suppress_warning_cleanup, w);
}
EXPORT_SYMBOL_GPL(kunit_end_suppress_warning);

void __kunit_suppress_auto_cleanup(struct kunit_suppressed_warning **wp)
{
	if (*wp)
		kunit_end_suppress_warning((*wp)->test, *wp);
}
EXPORT_SYMBOL_GPL(__kunit_suppress_auto_cleanup);

int kunit_suppressed_warning_count(struct kunit_suppressed_warning *w)
{
	return w ? atomic_read(&w->counter) : 0;
}
EXPORT_SYMBOL_GPL(kunit_suppressed_warning_count);

bool __kunit_is_suppressed_warning_impl(bool count)
{
	struct kunit_suppressed_warning *w;

	guard(rcu)();
	list_for_each_entry_rcu(w, &suppressed_warnings, node) {
		if (w->task == current) {
			if (count)
				atomic_inc(&w->counter);
			return true;
		}
	}

	return false;
}
