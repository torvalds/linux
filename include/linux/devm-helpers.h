/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LINUX_DEVM_HELPERS_H
#define __LINUX_DEVM_HELPERS_H

/*
 * Functions which do automatically cancel operations or release resources upon
 * driver detach.
 *
 * These should be helpful to avoid mixing the manual and devm-based resource
 * management which can be source of annoying, rarely occurring,
 * hard-to-reproduce bugs.
 *
 * Please take into account that devm based cancellation may be performed some
 * time after the remove() is ran.
 *
 * Thus mixing devm and manual resource management can easily cause problems
 * when unwinding operations with dependencies. IRQ scheduling a work in a queue
 * is typical example where IRQs are often devm-managed and WQs are manually
 * cleaned at remove(). If IRQs are not manually freed at remove() (and this is
 * often the case when we use devm for IRQs) we have a period of time after
 * remove() - and before devm managed IRQs are freed - where new IRQ may fire
 * and schedule a work item which won't be cancelled because remove() was
 * already ran.
 */

#include <linux/device.h>
#include <linux/workqueue.h>

static inline void devm_delayed_work_drop(void *res)
{
	cancel_delayed_work_sync(res);
}

/**
 * devm_delayed_work_autocancel - Resource-managed delayed work allocation
 * @dev:	Device which lifetime work is bound to
 * @w:		Work item to be queued
 * @worker:	Worker function
 *
 * Initialize delayed work which is automatically cancelled when driver is
 * detached. A few drivers need delayed work which must be cancelled before
 * driver is detached to avoid accessing removed resources.
 * devm_delayed_work_autocancel() can be used to omit the explicit
 * cancellation when driver is detached.
 */
static inline int devm_delayed_work_autocancel(struct device *dev,
					       struct delayed_work *w,
					       work_func_t worker)
{
	INIT_DELAYED_WORK(w, worker);
	return devm_add_action(dev, devm_delayed_work_drop, w);
}

static inline void devm_work_drop(void *res)
{
	cancel_work_sync(res);
}

/**
 * devm_work_autocancel - Resource-managed work allocation
 * @dev:	Device which lifetime work is bound to
 * @w:		Work to be added (and automatically cancelled)
 * @worker:	Worker function
 *
 * Initialize work which is automatically cancelled when driver is detached.
 * A few drivers need to queue work which must be cancelled before driver
 * is detached to avoid accessing removed resources.
 * devm_work_autocancel() can be used to omit the explicit
 * cancellation when driver is detached.
 */
static inline int devm_work_autocancel(struct device *dev,
				       struct work_struct *w,
				       work_func_t worker)
{
	INIT_WORK(w, worker);
	return devm_add_action(dev, devm_work_drop, w);
}

#endif
