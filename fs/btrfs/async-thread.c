/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/freezer.h>
#include "async-thread.h"

#define WORK_QUEUED_BIT 0
#define WORK_DONE_BIT 1
#define WORK_ORDER_DONE_BIT 2
#define WORK_HIGH_PRIO_BIT 3

/*
 * container for the kthread task pointer and the list of pending work
 * One of these is allocated per thread.
 */
struct btrfs_worker_thread {
	/* pool we belong to */
	struct btrfs_workers *workers;

	/* list of struct btrfs_work that are waiting for service */
	struct list_head pending;
	struct list_head prio_pending;

	/* list of worker threads from struct btrfs_workers */
	struct list_head worker_list;

	/* kthread */
	struct task_struct *task;

	/* number of things on the pending list */
	atomic_t num_pending;

	unsigned long sequence;

	/* protects the pending list. */
	spinlock_t lock;

	/* set to non-zero when this thread is already awake and kicking */
	int working;

	/* are we currently idle */
	int idle;
};

/*
 * helper function to move a thread onto the idle list after it
 * has finished some requests.
 */
static void check_idle_worker(struct btrfs_worker_thread *worker)
{
	if (!worker->idle && atomic_read(&worker->num_pending) <
	    worker->workers->idle_thresh / 2) {
		unsigned long flags;
		spin_lock_irqsave(&worker->workers->lock, flags);
		worker->idle = 1;
		list_move(&worker->worker_list, &worker->workers->idle_list);
		spin_unlock_irqrestore(&worker->workers->lock, flags);
	}
}

/*
 * helper function to move a thread off the idle list after new
 * pending work is added.
 */
static void check_busy_worker(struct btrfs_worker_thread *worker)
{
	if (worker->idle && atomic_read(&worker->num_pending) >=
	    worker->workers->idle_thresh) {
		unsigned long flags;
		spin_lock_irqsave(&worker->workers->lock, flags);
		worker->idle = 0;
		list_move_tail(&worker->worker_list,
			       &worker->workers->worker_list);
		spin_unlock_irqrestore(&worker->workers->lock, flags);
	}
}

static noinline int run_ordered_completions(struct btrfs_workers *workers,
					    struct btrfs_work *work)
{
	unsigned long flags;

	if (!workers->ordered)
		return 0;

	set_bit(WORK_DONE_BIT, &work->flags);

	spin_lock_irqsave(&workers->lock, flags);

	while (1) {
		if (!list_empty(&workers->prio_order_list)) {
			work = list_entry(workers->prio_order_list.next,
					  struct btrfs_work, order_list);
		} else if (!list_empty(&workers->order_list)) {
			work = list_entry(workers->order_list.next,
					  struct btrfs_work, order_list);
		} else {
			break;
		}
		if (!test_bit(WORK_DONE_BIT, &work->flags))
			break;

		/* we are going to call the ordered done function, but
		 * we leave the work item on the list as a barrier so
		 * that later work items that are done don't have their
		 * functions called before this one returns
		 */
		if (test_and_set_bit(WORK_ORDER_DONE_BIT, &work->flags))
			break;

		spin_unlock_irqrestore(&workers->lock, flags);

		work->ordered_func(work);

		/* now take the lock again and call the freeing code */
		spin_lock_irqsave(&workers->lock, flags);
		list_del(&work->order_list);
		work->ordered_free(work);
	}

	spin_unlock_irqrestore(&workers->lock, flags);
	return 0;
}

/*
 * main loop for servicing work items
 */
static int worker_loop(void *arg)
{
	struct btrfs_worker_thread *worker = arg;
	struct list_head *cur;
	struct btrfs_work *work;
	do {
		spin_lock_irq(&worker->lock);
again_locked:
		while (1) {
			if (!list_empty(&worker->prio_pending))
				cur = worker->prio_pending.next;
			else if (!list_empty(&worker->pending))
				cur = worker->pending.next;
			else
				break;

			work = list_entry(cur, struct btrfs_work, list);
			list_del(&work->list);
			clear_bit(WORK_QUEUED_BIT, &work->flags);

			work->worker = worker;
			spin_unlock_irq(&worker->lock);

			work->func(work);

			atomic_dec(&worker->num_pending);
			/*
			 * unless this is an ordered work queue,
			 * 'work' was probably freed by func above.
			 */
			run_ordered_completions(worker->workers, work);

			spin_lock_irq(&worker->lock);
			check_idle_worker(worker);
		}
		if (freezing(current)) {
			worker->working = 0;
			spin_unlock_irq(&worker->lock);
			refrigerator();
		} else {
			spin_unlock_irq(&worker->lock);
			if (!kthread_should_stop()) {
				cpu_relax();
				/*
				 * we've dropped the lock, did someone else
				 * jump_in?
				 */
				smp_mb();
				if (!list_empty(&worker->pending) ||
				    !list_empty(&worker->prio_pending))
					continue;

				/*
				 * this short schedule allows more work to
				 * come in without the queue functions
				 * needing to go through wake_up_process()
				 *
				 * worker->working is still 1, so nobody
				 * is going to try and wake us up
				 */
				schedule_timeout(1);
				smp_mb();
				if (!list_empty(&worker->pending) ||
				    !list_empty(&worker->prio_pending))
					continue;

				if (kthread_should_stop())
					break;

				/* still no more work?, sleep for real */
				spin_lock_irq(&worker->lock);
				set_current_state(TASK_INTERRUPTIBLE);
				if (!list_empty(&worker->pending) ||
				    !list_empty(&worker->prio_pending))
					goto again_locked;

				/*
				 * this makes sure we get a wakeup when someone
				 * adds something new to the queue
				 */
				worker->working = 0;
				spin_unlock_irq(&worker->lock);

				if (!kthread_should_stop())
					schedule();
			}
			__set_current_state(TASK_RUNNING);
		}
	} while (!kthread_should_stop());
	return 0;
}

/*
 * this will wait for all the worker threads to shutdown
 */
int btrfs_stop_workers(struct btrfs_workers *workers)
{
	struct list_head *cur;
	struct btrfs_worker_thread *worker;

	list_splice_init(&workers->idle_list, &workers->worker_list);
	while (!list_empty(&workers->worker_list)) {
		cur = workers->worker_list.next;
		worker = list_entry(cur, struct btrfs_worker_thread,
				    worker_list);
		kthread_stop(worker->task);
		list_del(&worker->worker_list);
		kfree(worker);
	}
	return 0;
}

/*
 * simple init on struct btrfs_workers
 */
void btrfs_init_workers(struct btrfs_workers *workers, char *name, int max)
{
	workers->num_workers = 0;
	INIT_LIST_HEAD(&workers->worker_list);
	INIT_LIST_HEAD(&workers->idle_list);
	INIT_LIST_HEAD(&workers->order_list);
	INIT_LIST_HEAD(&workers->prio_order_list);
	spin_lock_init(&workers->lock);
	workers->max_workers = max;
	workers->idle_thresh = 32;
	workers->name = name;
	workers->ordered = 0;
}

/*
 * starts new worker threads.  This does not enforce the max worker
 * count in case you need to temporarily go past it.
 */
int btrfs_start_workers(struct btrfs_workers *workers, int num_workers)
{
	struct btrfs_worker_thread *worker;
	int ret = 0;
	int i;

	for (i = 0; i < num_workers; i++) {
		worker = kzalloc(sizeof(*worker), GFP_NOFS);
		if (!worker) {
			ret = -ENOMEM;
			goto fail;
		}

		INIT_LIST_HEAD(&worker->pending);
		INIT_LIST_HEAD(&worker->prio_pending);
		INIT_LIST_HEAD(&worker->worker_list);
		spin_lock_init(&worker->lock);
		atomic_set(&worker->num_pending, 0);
		worker->workers = workers;
		worker->task = kthread_run(worker_loop, worker,
					   "btrfs-%s-%d", workers->name,
					   workers->num_workers + i);
		if (IS_ERR(worker->task)) {
			ret = PTR_ERR(worker->task);
			kfree(worker);
			goto fail;
		}

		spin_lock_irq(&workers->lock);
		list_add_tail(&worker->worker_list, &workers->idle_list);
		worker->idle = 1;
		workers->num_workers++;
		spin_unlock_irq(&workers->lock);
	}
	return 0;
fail:
	btrfs_stop_workers(workers);
	return ret;
}

/*
 * run through the list and find a worker thread that doesn't have a lot
 * to do right now.  This can return null if we aren't yet at the thread
 * count limit and all of the threads are busy.
 */
static struct btrfs_worker_thread *next_worker(struct btrfs_workers *workers)
{
	struct btrfs_worker_thread *worker;
	struct list_head *next;
	int enforce_min = workers->num_workers < workers->max_workers;

	/*
	 * if we find an idle thread, don't move it to the end of the
	 * idle list.  This improves the chance that the next submission
	 * will reuse the same thread, and maybe catch it while it is still
	 * working
	 */
	if (!list_empty(&workers->idle_list)) {
		next = workers->idle_list.next;
		worker = list_entry(next, struct btrfs_worker_thread,
				    worker_list);
		return worker;
	}
	if (enforce_min || list_empty(&workers->worker_list))
		return NULL;

	/*
	 * if we pick a busy task, move the task to the end of the list.
	 * hopefully this will keep things somewhat evenly balanced.
	 * Do the move in batches based on the sequence number.  This groups
	 * requests submitted at roughly the same time onto the same worker.
	 */
	next = workers->worker_list.next;
	worker = list_entry(next, struct btrfs_worker_thread, worker_list);
	atomic_inc(&worker->num_pending);
	worker->sequence++;

	if (worker->sequence % workers->idle_thresh == 0)
		list_move_tail(next, &workers->worker_list);
	return worker;
}

/*
 * selects a worker thread to take the next job.  This will either find
 * an idle worker, start a new worker up to the max count, or just return
 * one of the existing busy workers.
 */
static struct btrfs_worker_thread *find_worker(struct btrfs_workers *workers)
{
	struct btrfs_worker_thread *worker;
	unsigned long flags;

again:
	spin_lock_irqsave(&workers->lock, flags);
	worker = next_worker(workers);
	spin_unlock_irqrestore(&workers->lock, flags);

	if (!worker) {
		spin_lock_irqsave(&workers->lock, flags);
		if (workers->num_workers >= workers->max_workers) {
			struct list_head *fallback = NULL;
			/*
			 * we have failed to find any workers, just
			 * return the force one
			 */
			if (!list_empty(&workers->worker_list))
				fallback = workers->worker_list.next;
			if (!list_empty(&workers->idle_list))
				fallback = workers->idle_list.next;
			BUG_ON(!fallback);
			worker = list_entry(fallback,
				  struct btrfs_worker_thread, worker_list);
			spin_unlock_irqrestore(&workers->lock, flags);
		} else {
			spin_unlock_irqrestore(&workers->lock, flags);
			/* we're below the limit, start another worker */
			btrfs_start_workers(workers, 1);
			goto again;
		}
	}
	return worker;
}

/*
 * btrfs_requeue_work just puts the work item back on the tail of the list
 * it was taken from.  It is intended for use with long running work functions
 * that make some progress and want to give the cpu up for others.
 */
int btrfs_requeue_work(struct btrfs_work *work)
{
	struct btrfs_worker_thread *worker = work->worker;
	unsigned long flags;
	int wake = 0;

	if (test_and_set_bit(WORK_QUEUED_BIT, &work->flags))
		goto out;

	spin_lock_irqsave(&worker->lock, flags);
	if (test_bit(WORK_HIGH_PRIO_BIT, &work->flags))
		list_add_tail(&work->list, &worker->prio_pending);
	else
		list_add_tail(&work->list, &worker->pending);
	atomic_inc(&worker->num_pending);

	/* by definition we're busy, take ourselves off the idle
	 * list
	 */
	if (worker->idle) {
		spin_lock(&worker->workers->lock);
		worker->idle = 0;
		list_move_tail(&worker->worker_list,
			       &worker->workers->worker_list);
		spin_unlock(&worker->workers->lock);
	}
	if (!worker->working) {
		wake = 1;
		worker->working = 1;
	}

	spin_unlock_irqrestore(&worker->lock, flags);
	if (wake)
		wake_up_process(worker->task);
out:

	return 0;
}

void btrfs_set_work_high_prio(struct btrfs_work *work)
{
	set_bit(WORK_HIGH_PRIO_BIT, &work->flags);
}

/*
 * places a struct btrfs_work into the pending queue of one of the kthreads
 */
int btrfs_queue_worker(struct btrfs_workers *workers, struct btrfs_work *work)
{
	struct btrfs_worker_thread *worker;
	unsigned long flags;
	int wake = 0;

	/* don't requeue something already on a list */
	if (test_and_set_bit(WORK_QUEUED_BIT, &work->flags))
		goto out;

	worker = find_worker(workers);
	if (workers->ordered) {
		spin_lock_irqsave(&workers->lock, flags);
		if (test_bit(WORK_HIGH_PRIO_BIT, &work->flags)) {
			list_add_tail(&work->order_list,
				      &workers->prio_order_list);
		} else {
			list_add_tail(&work->order_list, &workers->order_list);
		}
		spin_unlock_irqrestore(&workers->lock, flags);
	} else {
		INIT_LIST_HEAD(&work->order_list);
	}

	spin_lock_irqsave(&worker->lock, flags);

	if (test_bit(WORK_HIGH_PRIO_BIT, &work->flags))
		list_add_tail(&work->list, &worker->prio_pending);
	else
		list_add_tail(&work->list, &worker->pending);
	atomic_inc(&worker->num_pending);
	check_busy_worker(worker);

	/*
	 * avoid calling into wake_up_process if this thread has already
	 * been kicked
	 */
	if (!worker->working)
		wake = 1;
	worker->working = 1;

	spin_unlock_irqrestore(&worker->lock, flags);

	if (wake)
		wake_up_process(worker->task);
out:
	return 0;
}
