// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 * Copyright (C) 2014 Fujitsu.  All rights reserved.
 */

#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/freezer.h>
#include "async-thread.h"
#include "ctree.h"

enum {
	WORK_DONE_BIT,
	WORK_ORDER_DONE_BIT,
};

#define NO_THRESHOLD (-1)
#define DFT_THRESHOLD (32)

struct btrfs_workqueue {
	struct workqueue_struct *normal_wq;

	/* File system this workqueue services */
	struct btrfs_fs_info *fs_info;

	/* List head pointing to ordered work list */
	struct list_head ordered_list;

	/* Spinlock for ordered_list */
	spinlock_t list_lock;

	/* Thresholding related variants */
	atomic_t pending;

	/* Up limit of concurrency workers */
	int limit_active;

	/* Current number of concurrency workers */
	int current_active;

	/* Threshold to change current_active */
	int thresh;
	unsigned int count;
	spinlock_t thres_lock;
};

struct btrfs_fs_info * __pure btrfs_workqueue_owner(const struct btrfs_workqueue *wq)
{
	return wq->fs_info;
}

struct btrfs_fs_info * __pure btrfs_work_owner(const struct btrfs_work *work)
{
	return work->wq->fs_info;
}

bool btrfs_workqueue_normal_congested(const struct btrfs_workqueue *wq)
{
	/*
	 * We could compare wq->pending with num_online_cpus()
	 * to support "thresh == NO_THRESHOLD" case, but it requires
	 * moving up atomic_inc/dec in thresh_queue/exec_hook. Let's
	 * postpone it until someone needs the support of that case.
	 */
	if (wq->thresh == NO_THRESHOLD)
		return false;

	return atomic_read(&wq->pending) > wq->thresh * 2;
}

struct btrfs_workqueue *btrfs_alloc_workqueue(struct btrfs_fs_info *fs_info,
					      const char *name, unsigned int flags,
					      int limit_active, int thresh)
{
	struct btrfs_workqueue *ret = kzalloc(sizeof(*ret), GFP_KERNEL);

	if (!ret)
		return NULL;

	ret->fs_info = fs_info;
	ret->limit_active = limit_active;
	atomic_set(&ret->pending, 0);
	if (thresh == 0)
		thresh = DFT_THRESHOLD;
	/* For low threshold, disabling threshold is a better choice */
	if (thresh < DFT_THRESHOLD) {
		ret->current_active = limit_active;
		ret->thresh = NO_THRESHOLD;
	} else {
		/*
		 * For threshold-able wq, let its concurrency grow on demand.
		 * Use minimal max_active at alloc time to reduce resource
		 * usage.
		 */
		ret->current_active = 1;
		ret->thresh = thresh;
	}

	ret->normal_wq = alloc_workqueue("btrfs-%s", flags, ret->current_active,
					 name);
	if (!ret->normal_wq) {
		kfree(ret);
		return NULL;
	}

	INIT_LIST_HEAD(&ret->ordered_list);
	spin_lock_init(&ret->list_lock);
	spin_lock_init(&ret->thres_lock);
	trace_btrfs_workqueue_alloc(ret, name);
	return ret;
}

/*
 * Hook for threshold which will be called in btrfs_queue_work.
 * This hook WILL be called in IRQ handler context,
 * so workqueue_set_max_active MUST NOT be called in this hook
 */
static inline void thresh_queue_hook(struct btrfs_workqueue *wq)
{
	if (wq->thresh == NO_THRESHOLD)
		return;
	atomic_inc(&wq->pending);
}

/*
 * Hook for threshold which will be called before executing the work,
 * This hook is called in kthread content.
 * So workqueue_set_max_active is called here.
 */
static inline void thresh_exec_hook(struct btrfs_workqueue *wq)
{
	int new_current_active;
	long pending;
	int need_change = 0;

	if (wq->thresh == NO_THRESHOLD)
		return;

	atomic_dec(&wq->pending);
	spin_lock(&wq->thres_lock);
	/*
	 * Use wq->count to limit the calling frequency of
	 * workqueue_set_max_active.
	 */
	wq->count++;
	wq->count %= (wq->thresh / 4);
	if (!wq->count)
		goto  out;
	new_current_active = wq->current_active;

	/*
	 * pending may be changed later, but it's OK since we really
	 * don't need it so accurate to calculate new_max_active.
	 */
	pending = atomic_read(&wq->pending);
	if (pending > wq->thresh)
		new_current_active++;
	if (pending < wq->thresh / 2)
		new_current_active--;
	new_current_active = clamp_val(new_current_active, 1, wq->limit_active);
	if (new_current_active != wq->current_active)  {
		need_change = 1;
		wq->current_active = new_current_active;
	}
out:
	spin_unlock(&wq->thres_lock);

	if (need_change) {
		workqueue_set_max_active(wq->normal_wq, wq->current_active);
	}
}

static void run_ordered_work(struct btrfs_workqueue *wq,
			     struct btrfs_work *self)
{
	struct list_head *list = &wq->ordered_list;
	struct btrfs_work *work;
	spinlock_t *lock = &wq->list_lock;
	unsigned long flags;
	bool free_self = false;

	while (1) {
		spin_lock_irqsave(lock, flags);
		if (list_empty(list))
			break;
		work = list_entry(list->next, struct btrfs_work,
				  ordered_list);
		if (!test_bit(WORK_DONE_BIT, &work->flags))
			break;
		/*
		 * Orders all subsequent loads after reading WORK_DONE_BIT,
		 * paired with the smp_mb__before_atomic in btrfs_work_helper
		 * this guarantees that the ordered function will see all
		 * updates from ordinary work function.
		 */
		smp_rmb();

		/*
		 * we are going to call the ordered done function, but
		 * we leave the work item on the list as a barrier so
		 * that later work items that are done don't have their
		 * functions called before this one returns
		 */
		if (test_and_set_bit(WORK_ORDER_DONE_BIT, &work->flags))
			break;
		trace_btrfs_ordered_sched(work);
		spin_unlock_irqrestore(lock, flags);
		work->ordered_func(work);

		/* now take the lock again and drop our item from the list */
		spin_lock_irqsave(lock, flags);
		list_del(&work->ordered_list);
		spin_unlock_irqrestore(lock, flags);

		if (work == self) {
			/*
			 * This is the work item that the worker is currently
			 * executing.
			 *
			 * The kernel workqueue code guarantees non-reentrancy
			 * of work items. I.e., if a work item with the same
			 * address and work function is queued twice, the second
			 * execution is blocked until the first one finishes. A
			 * work item may be freed and recycled with the same
			 * work function; the workqueue code assumes that the
			 * original work item cannot depend on the recycled work
			 * item in that case (see find_worker_executing_work()).
			 *
			 * Note that different types of Btrfs work can depend on
			 * each other, and one type of work on one Btrfs
			 * filesystem may even depend on the same type of work
			 * on another Btrfs filesystem via, e.g., a loop device.
			 * Therefore, we must not allow the current work item to
			 * be recycled until we are really done, otherwise we
			 * break the above assumption and can deadlock.
			 */
			free_self = true;
		} else {
			/*
			 * We don't want to call the ordered free functions with
			 * the lock held.
			 */
			work->ordered_free(work);
			/* NB: work must not be dereferenced past this point. */
			trace_btrfs_all_work_done(wq->fs_info, work);
		}
	}
	spin_unlock_irqrestore(lock, flags);

	if (free_self) {
		self->ordered_free(self);
		/* NB: self must not be dereferenced past this point. */
		trace_btrfs_all_work_done(wq->fs_info, self);
	}
}

static void btrfs_work_helper(struct work_struct *normal_work)
{
	struct btrfs_work *work = container_of(normal_work, struct btrfs_work,
					       normal_work);
	struct btrfs_workqueue *wq = work->wq;
	int need_order = 0;

	/*
	 * We should not touch things inside work in the following cases:
	 * 1) after work->func() if it has no ordered_free
	 *    Since the struct is freed in work->func().
	 * 2) after setting WORK_DONE_BIT
	 *    The work may be freed in other threads almost instantly.
	 * So we save the needed things here.
	 */
	if (work->ordered_func)
		need_order = 1;

	trace_btrfs_work_sched(work);
	thresh_exec_hook(wq);
	work->func(work);
	if (need_order) {
		/*
		 * Ensures all memory accesses done in the work function are
		 * ordered before setting the WORK_DONE_BIT. Ensuring the thread
		 * which is going to executed the ordered work sees them.
		 * Pairs with the smp_rmb in run_ordered_work.
		 */
		smp_mb__before_atomic();
		set_bit(WORK_DONE_BIT, &work->flags);
		run_ordered_work(wq, work);
	} else {
		/* NB: work must not be dereferenced past this point. */
		trace_btrfs_all_work_done(wq->fs_info, work);
	}
}

void btrfs_init_work(struct btrfs_work *work, btrfs_func_t func,
		     btrfs_func_t ordered_func, btrfs_func_t ordered_free)
{
	work->func = func;
	work->ordered_func = ordered_func;
	work->ordered_free = ordered_free;
	INIT_WORK(&work->normal_work, btrfs_work_helper);
	INIT_LIST_HEAD(&work->ordered_list);
	work->flags = 0;
}

void btrfs_queue_work(struct btrfs_workqueue *wq, struct btrfs_work *work)
{
	unsigned long flags;

	work->wq = wq;
	thresh_queue_hook(wq);
	if (work->ordered_func) {
		spin_lock_irqsave(&wq->list_lock, flags);
		list_add_tail(&work->ordered_list, &wq->ordered_list);
		spin_unlock_irqrestore(&wq->list_lock, flags);
	}
	trace_btrfs_work_queued(work);
	queue_work(wq->normal_wq, &work->normal_work);
}

void btrfs_destroy_workqueue(struct btrfs_workqueue *wq)
{
	if (!wq)
		return;
	destroy_workqueue(wq->normal_wq);
	trace_btrfs_workqueue_destroy(wq);
	kfree(wq);
}

void btrfs_workqueue_set_max(struct btrfs_workqueue *wq, int limit_active)
{
	if (wq)
		wq->limit_active = limit_active;
}

void btrfs_flush_workqueue(struct btrfs_workqueue *wq)
{
	flush_workqueue(wq->normal_wq);
}
