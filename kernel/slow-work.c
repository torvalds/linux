/* Worker thread pool for slow items, such as filesystem lookups or mkdirs
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 *
 * See Documentation/slow-work.txt
 */

#include <linux/module.h>
#include <linux/slow-work.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/wait.h>

#define SLOW_WORK_CULL_TIMEOUT (5 * HZ)	/* cull threads 5s after running out of
					 * things to do */
#define SLOW_WORK_OOM_TIMEOUT (5 * HZ)	/* can't start new threads for 5s after
					 * OOM */

static void slow_work_cull_timeout(unsigned long);
static void slow_work_oom_timeout(unsigned long);

#ifdef CONFIG_SYSCTL
static int slow_work_min_threads_sysctl(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);

static int slow_work_max_threads_sysctl(struct ctl_table *, int ,
					void __user *, size_t *, loff_t *);
#endif

/*
 * The pool of threads has at least min threads in it as long as someone is
 * using the facility, and may have as many as max.
 *
 * A portion of the pool may be processing very slow operations.
 */
static unsigned slow_work_min_threads = 2;
static unsigned slow_work_max_threads = 4;
static unsigned vslow_work_proportion = 50; /* % of threads that may process
					     * very slow work */

#ifdef CONFIG_SYSCTL
static const int slow_work_min_min_threads = 2;
static int slow_work_max_max_threads = 255;
static const int slow_work_min_vslow = 1;
static const int slow_work_max_vslow = 99;

ctl_table slow_work_sysctls[] = {
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "min-threads",
		.data		= &slow_work_min_threads,
		.maxlen		= sizeof(unsigned),
		.mode		= 0644,
		.proc_handler	= slow_work_min_threads_sysctl,
		.extra1		= (void *) &slow_work_min_min_threads,
		.extra2		= &slow_work_max_threads,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "max-threads",
		.data		= &slow_work_max_threads,
		.maxlen		= sizeof(unsigned),
		.mode		= 0644,
		.proc_handler	= slow_work_max_threads_sysctl,
		.extra1		= &slow_work_min_threads,
		.extra2		= (void *) &slow_work_max_max_threads,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "vslow-percentage",
		.data		= &vslow_work_proportion,
		.maxlen		= sizeof(unsigned),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.extra1		= (void *) &slow_work_min_vslow,
		.extra2		= (void *) &slow_work_max_vslow,
	},
	{ .ctl_name = 0 }
};
#endif

/*
 * The active state of the thread pool
 */
static atomic_t slow_work_thread_count;
static atomic_t vslow_work_executing_count;

static bool slow_work_may_not_start_new_thread;
static bool slow_work_cull; /* cull a thread due to lack of activity */
static DEFINE_TIMER(slow_work_cull_timer, slow_work_cull_timeout, 0, 0);
static DEFINE_TIMER(slow_work_oom_timer, slow_work_oom_timeout, 0, 0);
static struct slow_work slow_work_new_thread; /* new thread starter */

/*
 * The queues of work items and the lock governing access to them.  These are
 * shared between all the CPUs.  It doesn't make sense to have per-CPU queues
 * as the number of threads bears no relation to the number of CPUs.
 *
 * There are two queues of work items: one for slow work items, and one for
 * very slow work items.
 */
static LIST_HEAD(slow_work_queue);
static LIST_HEAD(vslow_work_queue);
static DEFINE_SPINLOCK(slow_work_queue_lock);

/*
 * The thread controls.  A variable used to signal to the threads that they
 * should exit when the queue is empty, a waitqueue used by the threads to wait
 * for signals, and a completion set by the last thread to exit.
 */
static bool slow_work_threads_should_exit;
static DECLARE_WAIT_QUEUE_HEAD(slow_work_thread_wq);
static DECLARE_COMPLETION(slow_work_last_thread_exited);

/*
 * The number of users of the thread pool and its lock.  Whilst this is zero we
 * have no threads hanging around, and when this reaches zero, we wait for all
 * active or queued work items to complete and kill all the threads we do have.
 */
static int slow_work_user_count;
static DEFINE_MUTEX(slow_work_user_lock);

/*
 * Calculate the maximum number of active threads in the pool that are
 * permitted to process very slow work items.
 *
 * The answer is rounded up to at least 1, but may not equal or exceed the
 * maximum number of the threads in the pool.  This means we always have at
 * least one thread that can process slow work items, and we always have at
 * least one thread that won't get tied up doing so.
 */
static unsigned slow_work_calc_vsmax(void)
{
	unsigned vsmax;

	vsmax = atomic_read(&slow_work_thread_count) * vslow_work_proportion;
	vsmax /= 100;
	vsmax = max(vsmax, 1U);
	return min(vsmax, slow_work_max_threads - 1);
}

/*
 * Attempt to execute stuff queued on a slow thread.  Return true if we managed
 * it, false if there was nothing to do.
 */
static bool slow_work_execute(void)
{
	struct slow_work *work = NULL;
	unsigned vsmax;
	bool very_slow;

	vsmax = slow_work_calc_vsmax();

	/* see if we can schedule a new thread to be started if we're not
	 * keeping up with the work */
	if (!waitqueue_active(&slow_work_thread_wq) &&
	    (!list_empty(&slow_work_queue) || !list_empty(&vslow_work_queue)) &&
	    atomic_read(&slow_work_thread_count) < slow_work_max_threads &&
	    !slow_work_may_not_start_new_thread)
		slow_work_enqueue(&slow_work_new_thread);

	/* find something to execute */
	spin_lock_irq(&slow_work_queue_lock);
	if (!list_empty(&vslow_work_queue) &&
	    atomic_read(&vslow_work_executing_count) < vsmax) {
		work = list_entry(vslow_work_queue.next,
				  struct slow_work, link);
		if (test_and_set_bit_lock(SLOW_WORK_EXECUTING, &work->flags))
			BUG();
		list_del_init(&work->link);
		atomic_inc(&vslow_work_executing_count);
		very_slow = true;
	} else if (!list_empty(&slow_work_queue)) {
		work = list_entry(slow_work_queue.next,
				  struct slow_work, link);
		if (test_and_set_bit_lock(SLOW_WORK_EXECUTING, &work->flags))
			BUG();
		list_del_init(&work->link);
		very_slow = false;
	} else {
		very_slow = false; /* avoid the compiler warning */
	}
	spin_unlock_irq(&slow_work_queue_lock);

	if (!work)
		return false;

	if (!test_and_clear_bit(SLOW_WORK_PENDING, &work->flags))
		BUG();

	work->ops->execute(work);

	if (very_slow)
		atomic_dec(&vslow_work_executing_count);
	clear_bit_unlock(SLOW_WORK_EXECUTING, &work->flags);

	/* if someone tried to enqueue the item whilst we were executing it,
	 * then it'll be left unenqueued to avoid multiple threads trying to
	 * execute it simultaneously
	 *
	 * there is, however, a race between us testing the pending flag and
	 * getting the spinlock, and between the enqueuer setting the pending
	 * flag and getting the spinlock, so we use a deferral bit to tell us
	 * if the enqueuer got there first
	 */
	if (test_bit(SLOW_WORK_PENDING, &work->flags)) {
		spin_lock_irq(&slow_work_queue_lock);

		if (!test_bit(SLOW_WORK_EXECUTING, &work->flags) &&
		    test_and_clear_bit(SLOW_WORK_ENQ_DEFERRED, &work->flags))
			goto auto_requeue;

		spin_unlock_irq(&slow_work_queue_lock);
	}

	work->ops->put_ref(work);
	return true;

auto_requeue:
	/* we must complete the enqueue operation
	 * - we transfer our ref on the item back to the appropriate queue
	 * - don't wake another thread up as we're awake already
	 */
	if (test_bit(SLOW_WORK_VERY_SLOW, &work->flags))
		list_add_tail(&work->link, &vslow_work_queue);
	else
		list_add_tail(&work->link, &slow_work_queue);
	spin_unlock_irq(&slow_work_queue_lock);
	return true;
}

/**
 * slow_work_enqueue - Schedule a slow work item for processing
 * @work: The work item to queue
 *
 * Schedule a slow work item for processing.  If the item is already undergoing
 * execution, this guarantees not to re-enter the execution routine until the
 * first execution finishes.
 *
 * The item is pinned by this function as it retains a reference to it, managed
 * through the item operations.  The item is unpinned once it has been
 * executed.
 *
 * An item may hog the thread that is running it for a relatively large amount
 * of time, sufficient, for example, to perform several lookup, mkdir, create
 * and setxattr operations.  It may sleep on I/O and may sleep to obtain locks.
 *
 * Conversely, if a number of items are awaiting processing, it may take some
 * time before any given item is given attention.  The number of threads in the
 * pool may be increased to deal with demand, but only up to a limit.
 *
 * If SLOW_WORK_VERY_SLOW is set on the work item, then it will be placed in
 * the very slow queue, from which only a portion of the threads will be
 * allowed to pick items to execute.  This ensures that very slow items won't
 * overly block ones that are just ordinarily slow.
 *
 * Returns 0 if successful, -EAGAIN if not.
 */
int slow_work_enqueue(struct slow_work *work)
{
	unsigned long flags;

	BUG_ON(slow_work_user_count <= 0);
	BUG_ON(!work);
	BUG_ON(!work->ops);
	BUG_ON(!work->ops->get_ref);

	/* when honouring an enqueue request, we only promise that we will run
	 * the work function in the future; we do not promise to run it once
	 * per enqueue request
	 *
	 * we use the PENDING bit to merge together repeat requests without
	 * having to disable IRQs and take the spinlock, whilst still
	 * maintaining our promise
	 */
	if (!test_and_set_bit_lock(SLOW_WORK_PENDING, &work->flags)) {
		spin_lock_irqsave(&slow_work_queue_lock, flags);

		/* we promise that we will not attempt to execute the work
		 * function in more than one thread simultaneously
		 *
		 * this, however, leaves us with a problem if we're asked to
		 * enqueue the work whilst someone is executing the work
		 * function as simply queueing the work immediately means that
		 * another thread may try executing it whilst it is already
		 * under execution
		 *
		 * to deal with this, we set the ENQ_DEFERRED bit instead of
		 * enqueueing, and the thread currently executing the work
		 * function will enqueue the work item when the work function
		 * returns and it has cleared the EXECUTING bit
		 */
		if (test_bit(SLOW_WORK_EXECUTING, &work->flags)) {
			set_bit(SLOW_WORK_ENQ_DEFERRED, &work->flags);
		} else {
			if (work->ops->get_ref(work) < 0)
				goto cant_get_ref;
			if (test_bit(SLOW_WORK_VERY_SLOW, &work->flags))
				list_add_tail(&work->link, &vslow_work_queue);
			else
				list_add_tail(&work->link, &slow_work_queue);
			wake_up(&slow_work_thread_wq);
		}

		spin_unlock_irqrestore(&slow_work_queue_lock, flags);
	}
	return 0;

cant_get_ref:
	spin_unlock_irqrestore(&slow_work_queue_lock, flags);
	return -EAGAIN;
}
EXPORT_SYMBOL(slow_work_enqueue);

/*
 * Schedule a cull of the thread pool at some time in the near future
 */
static void slow_work_schedule_cull(void)
{
	mod_timer(&slow_work_cull_timer,
		  round_jiffies(jiffies + SLOW_WORK_CULL_TIMEOUT));
}

/*
 * Worker thread culling algorithm
 */
static bool slow_work_cull_thread(void)
{
	unsigned long flags;
	bool do_cull = false;

	spin_lock_irqsave(&slow_work_queue_lock, flags);

	if (slow_work_cull) {
		slow_work_cull = false;

		if (list_empty(&slow_work_queue) &&
		    list_empty(&vslow_work_queue) &&
		    atomic_read(&slow_work_thread_count) >
		    slow_work_min_threads) {
			slow_work_schedule_cull();
			do_cull = true;
		}
	}

	spin_unlock_irqrestore(&slow_work_queue_lock, flags);
	return do_cull;
}

/*
 * Determine if there is slow work available for dispatch
 */
static inline bool slow_work_available(int vsmax)
{
	return !list_empty(&slow_work_queue) ||
		(!list_empty(&vslow_work_queue) &&
		 atomic_read(&vslow_work_executing_count) < vsmax);
}

/*
 * Worker thread dispatcher
 */
static int slow_work_thread(void *_data)
{
	int vsmax;

	DEFINE_WAIT(wait);

	set_freezable();
	set_user_nice(current, -5);

	for (;;) {
		vsmax = vslow_work_proportion;
		vsmax *= atomic_read(&slow_work_thread_count);
		vsmax /= 100;

		prepare_to_wait_exclusive(&slow_work_thread_wq, &wait,
					  TASK_INTERRUPTIBLE);
		if (!freezing(current) &&
		    !slow_work_threads_should_exit &&
		    !slow_work_available(vsmax) &&
		    !slow_work_cull)
			schedule();
		finish_wait(&slow_work_thread_wq, &wait);

		try_to_freeze();

		vsmax = vslow_work_proportion;
		vsmax *= atomic_read(&slow_work_thread_count);
		vsmax /= 100;

		if (slow_work_available(vsmax) && slow_work_execute()) {
			cond_resched();
			if (list_empty(&slow_work_queue) &&
			    list_empty(&vslow_work_queue) &&
			    atomic_read(&slow_work_thread_count) >
			    slow_work_min_threads)
				slow_work_schedule_cull();
			continue;
		}

		if (slow_work_threads_should_exit)
			break;

		if (slow_work_cull && slow_work_cull_thread())
			break;
	}

	if (atomic_dec_and_test(&slow_work_thread_count))
		complete_and_exit(&slow_work_last_thread_exited, 0);
	return 0;
}

/*
 * Handle thread cull timer expiration
 */
static void slow_work_cull_timeout(unsigned long data)
{
	slow_work_cull = true;
	wake_up(&slow_work_thread_wq);
}

/*
 * Get a reference on slow work thread starter
 */
static int slow_work_new_thread_get_ref(struct slow_work *work)
{
	return 0;
}

/*
 * Drop a reference on slow work thread starter
 */
static void slow_work_new_thread_put_ref(struct slow_work *work)
{
}

/*
 * Start a new slow work thread
 */
static void slow_work_new_thread_execute(struct slow_work *work)
{
	struct task_struct *p;

	if (slow_work_threads_should_exit)
		return;

	if (atomic_read(&slow_work_thread_count) >= slow_work_max_threads)
		return;

	if (!mutex_trylock(&slow_work_user_lock))
		return;

	slow_work_may_not_start_new_thread = true;
	atomic_inc(&slow_work_thread_count);
	p = kthread_run(slow_work_thread, NULL, "kslowd");
	if (IS_ERR(p)) {
		printk(KERN_DEBUG "Slow work thread pool: OOM\n");
		if (atomic_dec_and_test(&slow_work_thread_count))
			BUG(); /* we're running on a slow work thread... */
		mod_timer(&slow_work_oom_timer,
			  round_jiffies(jiffies + SLOW_WORK_OOM_TIMEOUT));
	} else {
		/* ratelimit the starting of new threads */
		mod_timer(&slow_work_oom_timer, jiffies + 1);
	}

	mutex_unlock(&slow_work_user_lock);
}

static const struct slow_work_ops slow_work_new_thread_ops = {
	.get_ref	= slow_work_new_thread_get_ref,
	.put_ref	= slow_work_new_thread_put_ref,
	.execute	= slow_work_new_thread_execute,
};

/*
 * post-OOM new thread start suppression expiration
 */
static void slow_work_oom_timeout(unsigned long data)
{
	slow_work_may_not_start_new_thread = false;
}

#ifdef CONFIG_SYSCTL
/*
 * Handle adjustment of the minimum number of threads
 */
static int slow_work_min_threads_sysctl(struct ctl_table *table, int write,
					void __user *buffer,
					size_t *lenp, loff_t *ppos)
{
	int ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	int n;

	if (ret == 0) {
		mutex_lock(&slow_work_user_lock);
		if (slow_work_user_count > 0) {
			/* see if we need to start or stop threads */
			n = atomic_read(&slow_work_thread_count) -
				slow_work_min_threads;

			if (n < 0 && !slow_work_may_not_start_new_thread)
				slow_work_enqueue(&slow_work_new_thread);
			else if (n > 0)
				slow_work_schedule_cull();
		}
		mutex_unlock(&slow_work_user_lock);
	}

	return ret;
}

/*
 * Handle adjustment of the maximum number of threads
 */
static int slow_work_max_threads_sysctl(struct ctl_table *table, int write,
					void __user *buffer,
					size_t *lenp, loff_t *ppos)
{
	int ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	int n;

	if (ret == 0) {
		mutex_lock(&slow_work_user_lock);
		if (slow_work_user_count > 0) {
			/* see if we need to stop threads */
			n = slow_work_max_threads -
				atomic_read(&slow_work_thread_count);

			if (n < 0)
				slow_work_schedule_cull();
		}
		mutex_unlock(&slow_work_user_lock);
	}

	return ret;
}
#endif /* CONFIG_SYSCTL */

/**
 * slow_work_register_user - Register a user of the facility
 *
 * Register a user of the facility, starting up the initial threads if there
 * aren't any other users at this point.  This will return 0 if successful, or
 * an error if not.
 */
int slow_work_register_user(void)
{
	struct task_struct *p;
	int loop;

	mutex_lock(&slow_work_user_lock);

	if (slow_work_user_count == 0) {
		printk(KERN_NOTICE "Slow work thread pool: Starting up\n");
		init_completion(&slow_work_last_thread_exited);

		slow_work_threads_should_exit = false;
		slow_work_init(&slow_work_new_thread,
			       &slow_work_new_thread_ops);
		slow_work_may_not_start_new_thread = false;
		slow_work_cull = false;

		/* start the minimum number of threads */
		for (loop = 0; loop < slow_work_min_threads; loop++) {
			atomic_inc(&slow_work_thread_count);
			p = kthread_run(slow_work_thread, NULL, "kslowd");
			if (IS_ERR(p))
				goto error;
		}
		printk(KERN_NOTICE "Slow work thread pool: Ready\n");
	}

	slow_work_user_count++;
	mutex_unlock(&slow_work_user_lock);
	return 0;

error:
	if (atomic_dec_and_test(&slow_work_thread_count))
		complete(&slow_work_last_thread_exited);
	if (loop > 0) {
		printk(KERN_ERR "Slow work thread pool:"
		       " Aborting startup on ENOMEM\n");
		slow_work_threads_should_exit = true;
		wake_up_all(&slow_work_thread_wq);
		wait_for_completion(&slow_work_last_thread_exited);
		printk(KERN_ERR "Slow work thread pool: Aborted\n");
	}
	mutex_unlock(&slow_work_user_lock);
	return PTR_ERR(p);
}
EXPORT_SYMBOL(slow_work_register_user);

/**
 * slow_work_unregister_user - Unregister a user of the facility
 *
 * Unregister a user of the facility, killing all the threads if this was the
 * last one.
 */
void slow_work_unregister_user(void)
{
	mutex_lock(&slow_work_user_lock);

	BUG_ON(slow_work_user_count <= 0);

	slow_work_user_count--;
	if (slow_work_user_count == 0) {
		printk(KERN_NOTICE "Slow work thread pool: Shutting down\n");
		slow_work_threads_should_exit = true;
		del_timer_sync(&slow_work_cull_timer);
		del_timer_sync(&slow_work_oom_timer);
		wake_up_all(&slow_work_thread_wq);
		wait_for_completion(&slow_work_last_thread_exited);
		printk(KERN_NOTICE "Slow work thread pool:"
		       " Shut down complete\n");
	}

	mutex_unlock(&slow_work_user_lock);
}
EXPORT_SYMBOL(slow_work_unregister_user);

/*
 * Initialise the slow work facility
 */
static int __init init_slow_work(void)
{
	unsigned nr_cpus = num_possible_cpus();

	if (slow_work_max_threads < nr_cpus)
		slow_work_max_threads = nr_cpus;
#ifdef CONFIG_SYSCTL
	if (slow_work_max_max_threads < nr_cpus * 2)
		slow_work_max_max_threads = nr_cpus * 2;
#endif
	return 0;
}

subsys_initcall(init_slow_work);
