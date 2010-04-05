/*
 * async.c: Asynchronous function calls for boot performance
 *
 * (C) Copyright 2009 Intel Corporation
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */


/*

Goals and Theory of Operation

The primary goal of this feature is to reduce the kernel boot time,
by doing various independent hardware delays and discovery operations
decoupled and not strictly serialized.

More specifically, the asynchronous function call concept allows
certain operations (primarily during system boot) to happen
asynchronously, out of order, while these operations still
have their externally visible parts happen sequentially and in-order.
(not unlike how out-of-order CPUs retire their instructions in order)

Key to the asynchronous function call implementation is the concept of
a "sequence cookie" (which, although it has an abstracted type, can be
thought of as a monotonically incrementing number).

The async core will assign each scheduled event such a sequence cookie and
pass this to the called functions.

The asynchronously called function should before doing a globally visible
operation, such as registering device numbers, call the
async_synchronize_cookie() function and pass in its own cookie. The
async_synchronize_cookie() function will make sure that all asynchronous
operations that were scheduled prior to the operation corresponding with the
cookie have completed.

Subsystem/driver initialization code that scheduled asynchronous probe
functions, but which shares global resources with other drivers/subsystems
that do not use the asynchronous call feature, need to do a full
synchronization with the async_synchronize_full() function, before returning
from their init function. This is to maintain strict ordering between the
asynchronous and synchronous parts of the kernel.

*/

#include <linux/async.h>
#include <linux/bug.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <asm/atomic.h>

static async_cookie_t next_cookie = 1;

#define MAX_THREADS	256
#define MAX_WORK	32768

static LIST_HEAD(async_pending);
static LIST_HEAD(async_running);
static DEFINE_SPINLOCK(async_lock);

static int async_enabled = 0;

struct async_entry {
	struct list_head list;
	async_cookie_t   cookie;
	async_func_ptr	 *func;
	void             *data;
	struct list_head *running;
};

static DECLARE_WAIT_QUEUE_HEAD(async_done);
static DECLARE_WAIT_QUEUE_HEAD(async_new);

static atomic_t entry_count;
static atomic_t thread_count;

extern int initcall_debug;


/*
 * MUST be called with the lock held!
 */
static async_cookie_t  __lowest_in_progress(struct list_head *running)
{
	struct async_entry *entry;

	if (!list_empty(running)) {
		entry = list_first_entry(running,
			struct async_entry, list);
		return entry->cookie;
	}

	list_for_each_entry(entry, &async_pending, list)
		if (entry->running == running)
			return entry->cookie;

	return next_cookie;	/* "infinity" value */
}

static async_cookie_t  lowest_in_progress(struct list_head *running)
{
	unsigned long flags;
	async_cookie_t ret;

	spin_lock_irqsave(&async_lock, flags);
	ret = __lowest_in_progress(running);
	spin_unlock_irqrestore(&async_lock, flags);
	return ret;
}
/*
 * pick the first pending entry and run it
 */
static void run_one_entry(void)
{
	unsigned long flags;
	struct async_entry *entry;
	ktime_t calltime, delta, rettime;

	/* 1) pick one task from the pending queue */

	spin_lock_irqsave(&async_lock, flags);
	if (list_empty(&async_pending))
		goto out;
	entry = list_first_entry(&async_pending, struct async_entry, list);

	/* 2) move it to the running queue */
	list_move_tail(&entry->list, entry->running);
	spin_unlock_irqrestore(&async_lock, flags);

	/* 3) run it (and print duration)*/
	if (initcall_debug && system_state == SYSTEM_BOOTING) {
		printk("calling  %lli_%pF @ %i\n", (long long)entry->cookie,
			entry->func, task_pid_nr(current));
		calltime = ktime_get();
	}
	entry->func(entry->data, entry->cookie);
	if (initcall_debug && system_state == SYSTEM_BOOTING) {
		rettime = ktime_get();
		delta = ktime_sub(rettime, calltime);
		printk("initcall %lli_%pF returned 0 after %lld usecs\n",
			(long long)entry->cookie,
			entry->func,
			(long long)ktime_to_ns(delta) >> 10);
	}

	/* 4) remove it from the running queue */
	spin_lock_irqsave(&async_lock, flags);
	list_del(&entry->list);

	/* 5) free the entry  */
	kfree(entry);
	atomic_dec(&entry_count);

	spin_unlock_irqrestore(&async_lock, flags);

	/* 6) wake up any waiters. */
	wake_up(&async_done);
	return;

out:
	spin_unlock_irqrestore(&async_lock, flags);
}


static async_cookie_t __async_schedule(async_func_ptr *ptr, void *data, struct list_head *running)
{
	struct async_entry *entry;
	unsigned long flags;
	async_cookie_t newcookie;
	

	/* allow irq-off callers */
	entry = kzalloc(sizeof(struct async_entry), GFP_ATOMIC);

	/*
	 * If we're out of memory or if there's too much work
	 * pending already, we execute synchronously.
	 */
	if (!async_enabled || !entry || atomic_read(&entry_count) > MAX_WORK) {
		kfree(entry);
		spin_lock_irqsave(&async_lock, flags);
		newcookie = next_cookie++;
		spin_unlock_irqrestore(&async_lock, flags);

		/* low on memory.. run synchronously */
		ptr(data, newcookie);
		return newcookie;
	}
	entry->func = ptr;
	entry->data = data;
	entry->running = running;

	spin_lock_irqsave(&async_lock, flags);
	newcookie = entry->cookie = next_cookie++;
	list_add_tail(&entry->list, &async_pending);
	atomic_inc(&entry_count);
	spin_unlock_irqrestore(&async_lock, flags);
	wake_up(&async_new);
	return newcookie;
}

/**
 * async_schedule - schedule a function for asynchronous execution
 * @ptr: function to execute asynchronously
 * @data: data pointer to pass to the function
 *
 * Returns an async_cookie_t that may be used for checkpointing later.
 * Note: This function may be called from atomic or non-atomic contexts.
 */
async_cookie_t async_schedule(async_func_ptr *ptr, void *data)
{
	return __async_schedule(ptr, data, &async_running);
}
EXPORT_SYMBOL_GPL(async_schedule);

/**
 * async_schedule_domain - schedule a function for asynchronous execution within a certain domain
 * @ptr: function to execute asynchronously
 * @data: data pointer to pass to the function
 * @running: running list for the domain
 *
 * Returns an async_cookie_t that may be used for checkpointing later.
 * @running may be used in the async_synchronize_*_domain() functions
 * to wait within a certain synchronization domain rather than globally.
 * A synchronization domain is specified via the running queue @running to use.
 * Note: This function may be called from atomic or non-atomic contexts.
 */
async_cookie_t async_schedule_domain(async_func_ptr *ptr, void *data,
				     struct list_head *running)
{
	return __async_schedule(ptr, data, running);
}
EXPORT_SYMBOL_GPL(async_schedule_domain);

/**
 * async_synchronize_full - synchronize all asynchronous function calls
 *
 * This function waits until all asynchronous function calls have been done.
 */
void async_synchronize_full(void)
{
	do {
		async_synchronize_cookie(next_cookie);
	} while (!list_empty(&async_running) || !list_empty(&async_pending));
}
EXPORT_SYMBOL_GPL(async_synchronize_full);

/**
 * async_synchronize_full_domain - synchronize all asynchronous function within a certain domain
 * @list: running list to synchronize on
 *
 * This function waits until all asynchronous function calls for the
 * synchronization domain specified by the running list @list have been done.
 */
void async_synchronize_full_domain(struct list_head *list)
{
	async_synchronize_cookie_domain(next_cookie, list);
}
EXPORT_SYMBOL_GPL(async_synchronize_full_domain);

/**
 * async_synchronize_cookie_domain - synchronize asynchronous function calls within a certain domain with cookie checkpointing
 * @cookie: async_cookie_t to use as checkpoint
 * @running: running list to synchronize on
 *
 * This function waits until all asynchronous function calls for the
 * synchronization domain specified by the running list @list submitted
 * prior to @cookie have been done.
 */
void async_synchronize_cookie_domain(async_cookie_t cookie,
				     struct list_head *running)
{
	ktime_t starttime, delta, endtime;

	if (initcall_debug && system_state == SYSTEM_BOOTING) {
		printk("async_waiting @ %i\n", task_pid_nr(current));
		starttime = ktime_get();
	}

	wait_event(async_done, lowest_in_progress(running) >= cookie);

	if (initcall_debug && system_state == SYSTEM_BOOTING) {
		endtime = ktime_get();
		delta = ktime_sub(endtime, starttime);

		printk("async_continuing @ %i after %lli usec\n",
			task_pid_nr(current),
			(long long)ktime_to_ns(delta) >> 10);
	}
}
EXPORT_SYMBOL_GPL(async_synchronize_cookie_domain);

/**
 * async_synchronize_cookie - synchronize asynchronous function calls with cookie checkpointing
 * @cookie: async_cookie_t to use as checkpoint
 *
 * This function waits until all asynchronous function calls prior to @cookie
 * have been done.
 */
void async_synchronize_cookie(async_cookie_t cookie)
{
	async_synchronize_cookie_domain(cookie, &async_running);
}
EXPORT_SYMBOL_GPL(async_synchronize_cookie);


static int async_thread(void *unused)
{
	DECLARE_WAITQUEUE(wq, current);
	add_wait_queue(&async_new, &wq);

	while (!kthread_should_stop()) {
		int ret = HZ;
		set_current_state(TASK_INTERRUPTIBLE);
		/*
		 * check the list head without lock.. false positives
		 * are dealt with inside run_one_entry() while holding
		 * the lock.
		 */
		rmb();
		if (!list_empty(&async_pending))
			run_one_entry();
		else
			ret = schedule_timeout(HZ);

		if (ret == 0) {
			/*
			 * we timed out, this means we as thread are redundant.
			 * we sign off and die, but we to avoid any races there
			 * is a last-straw check to see if work snuck in.
			 */
			atomic_dec(&thread_count);
			wmb(); /* manager must see our departure first */
			if (list_empty(&async_pending))
				break;
			/*
			 * woops work came in between us timing out and us
			 * signing off; we need to stay alive and keep working.
			 */
			atomic_inc(&thread_count);
		}
	}
	remove_wait_queue(&async_new, &wq);

	return 0;
}

static int async_manager_thread(void *unused)
{
	DECLARE_WAITQUEUE(wq, current);
	add_wait_queue(&async_new, &wq);

	while (!kthread_should_stop()) {
		int tc, ec;

		set_current_state(TASK_INTERRUPTIBLE);

		tc = atomic_read(&thread_count);
		rmb();
		ec = atomic_read(&entry_count);

		while (tc < ec && tc < MAX_THREADS) {
			if (IS_ERR(kthread_run(async_thread, NULL, "async/%i",
					       tc))) {
				msleep(100);
				continue;
			}
			atomic_inc(&thread_count);
			tc++;
		}

		schedule();
	}
	remove_wait_queue(&async_new, &wq);

	return 0;
}

static int __init async_init(void)
{
	async_enabled =
		!IS_ERR(kthread_run(async_manager_thread, NULL, "async/mgr"));

	WARN_ON(!async_enabled);
	return 0;
}

core_initcall(async_init);
