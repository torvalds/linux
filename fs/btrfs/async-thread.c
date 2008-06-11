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

/*
 * container for the kthread task pointer and the list of pending work
 * One of these is allocated per thread.
 */
struct btrfs_worker_thread {
	/* list of struct btrfs_work that are waiting for service */
	struct list_head pending;

	/* list of worker threads from struct btrfs_workers */
	struct list_head worker_list;

	/* kthread */
	struct task_struct *task;

	/* number of things on the pending list */
	atomic_t num_pending;

	/* protects the pending list. */
	spinlock_t lock;

	/* set to non-zero when this thread is already awake and kicking */
	int working;
};

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
		while(!list_empty(&worker->pending)) {
			cur = worker->pending.next;
			work = list_entry(cur, struct btrfs_work, list);
			list_del(&work->list);
			clear_bit(0, &work->flags);

			work->worker = worker;
			spin_unlock_irq(&worker->lock);

			work->func(work);

			atomic_dec(&worker->num_pending);
			spin_lock_irq(&worker->lock);
		}
		worker->working = 0;
		if (freezing(current)) {
			refrigerator();
		} else {
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irq(&worker->lock);
			schedule();
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

	while(!list_empty(&workers->worker_list)) {
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
void btrfs_init_workers(struct btrfs_workers *workers, int max)
{
	workers->num_workers = 0;
	INIT_LIST_HEAD(&workers->worker_list);
	workers->last = NULL;
	spin_lock_init(&workers->lock);
	workers->max_workers = max;
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
		INIT_LIST_HEAD(&worker->worker_list);
		spin_lock_init(&worker->lock);
		atomic_set(&worker->num_pending, 0);
		worker->task = kthread_run(worker_loop, worker, "btrfs");
		if (IS_ERR(worker->task)) {
			ret = PTR_ERR(worker->task);
			goto fail;
		}

		spin_lock_irq(&workers->lock);
		list_add_tail(&worker->worker_list, &workers->worker_list);
		workers->last = worker;
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
	struct list_head *start;
	int enforce_min = workers->num_workers < workers->max_workers;

	/* start with the last thread if it isn't busy */
	worker = workers->last;
	if (atomic_read(&worker->num_pending) < 64)
		goto done;

	next = worker->worker_list.next;
	start = &worker->worker_list;

	/*
	 * check all the workers for someone that is bored.  FIXME, do
	 * something smart here
	 */
	while(next != start) {
		if (next == &workers->worker_list) {
			next = workers->worker_list.next;
			continue;
		}
		worker = list_entry(next, struct btrfs_worker_thread,
				    worker_list);
		if (atomic_read(&worker->num_pending) < 64 || !enforce_min)
			goto done;
		next = next->next;
	}
	/*
	 * nobody was bored, if we're already at the max thread count,
	 * use the last thread
	 */
	if (!enforce_min || atomic_read(&workers->last->num_pending) < 64) {
		return workers->last;
	}
	return NULL;
done:
	workers->last = worker;
	return worker;
}

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
			/*
			 * we have failed to find any workers, just
			 * return the force one
			 */
			worker = list_entry(workers->worker_list.next,
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

	if (test_and_set_bit(0, &work->flags))
		goto out;

	spin_lock_irqsave(&worker->lock, flags);
	atomic_inc(&worker->num_pending);
	list_add_tail(&work->list, &worker->pending);
	spin_unlock_irqrestore(&worker->lock, flags);
out:
	return 0;
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
	if (test_and_set_bit(0, &work->flags))
		goto out;

	worker = find_worker(workers);

	spin_lock_irqsave(&worker->lock, flags);
	atomic_inc(&worker->num_pending);
	list_add_tail(&work->list, &worker->pending);

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
