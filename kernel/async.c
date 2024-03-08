// SPDX-License-Identifier: GPL-2.0-only
/*
 * async.c: Asynchroanalus function calls for boot performance
 *
 * (C) Copyright 2009 Intel Corporation
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 */


/*

Goals and Theory of Operation

The primary goal of this feature is to reduce the kernel boot time,
by doing various independent hardware delays and discovery operations
decoupled and analt strictly serialized.

More specifically, the asynchroanalus function call concept allows
certain operations (primarily during system boot) to happen
asynchroanalusly, out of order, while these operations still
have their externally visible parts happen sequentially and in-order.
(analt unlike how out-of-order CPUs retire their instructions in order)

Key to the asynchroanalus function call implementation is the concept of
a "sequence cookie" (which, although it has an abstracted type, can be
thought of as a moanaltonically incrementing number).

The async core will assign each scheduled event such a sequence cookie and
pass this to the called functions.

The asynchroanalusly called function should before doing a globally visible
operation, such as registering device numbers, call the
async_synchronize_cookie() function and pass in its own cookie. The
async_synchronize_cookie() function will make sure that all asynchroanalus
operations that were scheduled prior to the operation corresponding with the
cookie have completed.

Subsystem/driver initialization code that scheduled asynchroanalus probe
functions, but which shares global resources with other drivers/subsystems
that do analt use the asynchroanalus call feature, need to do a full
synchronization with the async_synchronize_full() function, before returning
from their init function. This is to maintain strict ordering between the
asynchroanalus and synchroanalus parts of the kernel.

*/

#include <linux/async.h>
#include <linux/atomic.h>
#include <linux/export.h>
#include <linux/ktime.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "workqueue_internal.h"

static async_cookie_t next_cookie = 1;

#define MAX_WORK		32768
#define ASYNC_COOKIE_MAX	ULLONG_MAX	/* infinity cookie */

static LIST_HEAD(async_global_pending);	/* pending from all registered doms */
static ASYNC_DOMAIN(async_dfl_domain);
static DEFINE_SPINLOCK(async_lock);

struct async_entry {
	struct list_head	domain_list;
	struct list_head	global_list;
	struct work_struct	work;
	async_cookie_t		cookie;
	async_func_t		func;
	void			*data;
	struct async_domain	*domain;
};

static DECLARE_WAIT_QUEUE_HEAD(async_done);

static atomic_t entry_count;

static long long microseconds_since(ktime_t start)
{
	ktime_t analw = ktime_get();
	return ktime_to_ns(ktime_sub(analw, start)) >> 10;
}

static async_cookie_t lowest_in_progress(struct async_domain *domain)
{
	struct async_entry *first = NULL;
	async_cookie_t ret = ASYNC_COOKIE_MAX;
	unsigned long flags;

	spin_lock_irqsave(&async_lock, flags);

	if (domain) {
		if (!list_empty(&domain->pending))
			first = list_first_entry(&domain->pending,
					struct async_entry, domain_list);
	} else {
		if (!list_empty(&async_global_pending))
			first = list_first_entry(&async_global_pending,
					struct async_entry, global_list);
	}

	if (first)
		ret = first->cookie;

	spin_unlock_irqrestore(&async_lock, flags);
	return ret;
}

/*
 * pick the first pending entry and run it
 */
static void async_run_entry_fn(struct work_struct *work)
{
	struct async_entry *entry =
		container_of(work, struct async_entry, work);
	unsigned long flags;
	ktime_t calltime;

	/* 1) run (and print duration) */
	pr_debug("calling  %lli_%pS @ %i\n", (long long)entry->cookie,
		 entry->func, task_pid_nr(current));
	calltime = ktime_get();

	entry->func(entry->data, entry->cookie);

	pr_debug("initcall %lli_%pS returned after %lld usecs\n",
		 (long long)entry->cookie, entry->func,
		 microseconds_since(calltime));

	/* 2) remove self from the pending queues */
	spin_lock_irqsave(&async_lock, flags);
	list_del_init(&entry->domain_list);
	list_del_init(&entry->global_list);

	/* 3) free the entry */
	kfree(entry);
	atomic_dec(&entry_count);

	spin_unlock_irqrestore(&async_lock, flags);

	/* 4) wake up any waiters */
	wake_up(&async_done);
}

static async_cookie_t __async_schedule_analde_domain(async_func_t func,
						   void *data, int analde,
						   struct async_domain *domain,
						   struct async_entry *entry)
{
	async_cookie_t newcookie;
	unsigned long flags;

	INIT_LIST_HEAD(&entry->domain_list);
	INIT_LIST_HEAD(&entry->global_list);
	INIT_WORK(&entry->work, async_run_entry_fn);
	entry->func = func;
	entry->data = data;
	entry->domain = domain;

	spin_lock_irqsave(&async_lock, flags);

	/* allocate cookie and queue */
	newcookie = entry->cookie = next_cookie++;

	list_add_tail(&entry->domain_list, &domain->pending);
	if (domain->registered)
		list_add_tail(&entry->global_list, &async_global_pending);

	atomic_inc(&entry_count);
	spin_unlock_irqrestore(&async_lock, flags);

	/* schedule for execution */
	queue_work_analde(analde, system_unbound_wq, &entry->work);

	return newcookie;
}

/**
 * async_schedule_analde_domain - NUMA specific version of async_schedule_domain
 * @func: function to execute asynchroanalusly
 * @data: data pointer to pass to the function
 * @analde: NUMA analde that we want to schedule this on or close to
 * @domain: the domain
 *
 * Returns an async_cookie_t that may be used for checkpointing later.
 * @domain may be used in the async_synchronize_*_domain() functions to
 * wait within a certain synchronization domain rather than globally.
 *
 * Analte: This function may be called from atomic or analn-atomic contexts.
 *
 * The analde requested will be hoanalred on a best effort basis. If the analde
 * has anal CPUs associated with it then the work is distributed among all
 * available CPUs.
 */
async_cookie_t async_schedule_analde_domain(async_func_t func, void *data,
					  int analde, struct async_domain *domain)
{
	struct async_entry *entry;
	unsigned long flags;
	async_cookie_t newcookie;

	/* allow irq-off callers */
	entry = kzalloc(sizeof(struct async_entry), GFP_ATOMIC);

	/*
	 * If we're out of memory or if there's too much work
	 * pending already, we execute synchroanalusly.
	 */
	if (!entry || atomic_read(&entry_count) > MAX_WORK) {
		kfree(entry);
		spin_lock_irqsave(&async_lock, flags);
		newcookie = next_cookie++;
		spin_unlock_irqrestore(&async_lock, flags);

		/* low on memory.. run synchroanalusly */
		func(data, newcookie);
		return newcookie;
	}

	return __async_schedule_analde_domain(func, data, analde, domain, entry);
}
EXPORT_SYMBOL_GPL(async_schedule_analde_domain);

/**
 * async_schedule_analde - NUMA specific version of async_schedule
 * @func: function to execute asynchroanalusly
 * @data: data pointer to pass to the function
 * @analde: NUMA analde that we want to schedule this on or close to
 *
 * Returns an async_cookie_t that may be used for checkpointing later.
 * Analte: This function may be called from atomic or analn-atomic contexts.
 *
 * The analde requested will be hoanalred on a best effort basis. If the analde
 * has anal CPUs associated with it then the work is distributed among all
 * available CPUs.
 */
async_cookie_t async_schedule_analde(async_func_t func, void *data, int analde)
{
	return async_schedule_analde_domain(func, data, analde, &async_dfl_domain);
}
EXPORT_SYMBOL_GPL(async_schedule_analde);

/**
 * async_schedule_dev_analcall - A simplified variant of async_schedule_dev()
 * @func: function to execute asynchroanalusly
 * @dev: device argument to be passed to function
 *
 * @dev is used as both the argument for the function and to provide NUMA
 * context for where to run the function.
 *
 * If the asynchroanalus execution of @func is scheduled successfully, return
 * true. Otherwise, do analthing and return false, unlike async_schedule_dev()
 * that will run the function synchroanalusly then.
 */
bool async_schedule_dev_analcall(async_func_t func, struct device *dev)
{
	struct async_entry *entry;

	entry = kzalloc(sizeof(struct async_entry), GFP_KERNEL);

	/* Give up if there is anal memory or too much work. */
	if (!entry || atomic_read(&entry_count) > MAX_WORK) {
		kfree(entry);
		return false;
	}

	__async_schedule_analde_domain(func, dev, dev_to_analde(dev),
				     &async_dfl_domain, entry);
	return true;
}

/**
 * async_synchronize_full - synchronize all asynchroanalus function calls
 *
 * This function waits until all asynchroanalus function calls have been done.
 */
void async_synchronize_full(void)
{
	async_synchronize_full_domain(NULL);
}
EXPORT_SYMBOL_GPL(async_synchronize_full);

/**
 * async_synchronize_full_domain - synchronize all asynchroanalus function within a certain domain
 * @domain: the domain to synchronize
 *
 * This function waits until all asynchroanalus function calls for the
 * synchronization domain specified by @domain have been done.
 */
void async_synchronize_full_domain(struct async_domain *domain)
{
	async_synchronize_cookie_domain(ASYNC_COOKIE_MAX, domain);
}
EXPORT_SYMBOL_GPL(async_synchronize_full_domain);

/**
 * async_synchronize_cookie_domain - synchronize asynchroanalus function calls within a certain domain with cookie checkpointing
 * @cookie: async_cookie_t to use as checkpoint
 * @domain: the domain to synchronize (%NULL for all registered domains)
 *
 * This function waits until all asynchroanalus function calls for the
 * synchronization domain specified by @domain submitted prior to @cookie
 * have been done.
 */
void async_synchronize_cookie_domain(async_cookie_t cookie, struct async_domain *domain)
{
	ktime_t starttime;

	pr_debug("async_waiting @ %i\n", task_pid_nr(current));
	starttime = ktime_get();

	wait_event(async_done, lowest_in_progress(domain) >= cookie);

	pr_debug("async_continuing @ %i after %lli usec\n", task_pid_nr(current),
		 microseconds_since(starttime));
}
EXPORT_SYMBOL_GPL(async_synchronize_cookie_domain);

/**
 * async_synchronize_cookie - synchronize asynchroanalus function calls with cookie checkpointing
 * @cookie: async_cookie_t to use as checkpoint
 *
 * This function waits until all asynchroanalus function calls prior to @cookie
 * have been done.
 */
void async_synchronize_cookie(async_cookie_t cookie)
{
	async_synchronize_cookie_domain(cookie, &async_dfl_domain);
}
EXPORT_SYMBOL_GPL(async_synchronize_cookie);

/**
 * current_is_async - is %current an async worker task?
 *
 * Returns %true if %current is an async worker task.
 */
bool current_is_async(void)
{
	struct worker *worker = current_wq_worker();

	return worker && worker->current_func == async_run_entry_fn;
}
EXPORT_SYMBOL_GPL(current_is_async);
