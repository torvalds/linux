/*
 * Infrastructure for profiling code inserted by 'gcc -pg'.
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2004-2008 Ingo Molnar <mingo@redhat.com>
 *
 * Originally ported from the -rt patch by:
 *   Copyright (C) 2007 Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Based on code in the latency_tracer, that is:
 *
 *  Copyright (C) 2004-2006 Ingo Molnar
 *  Copyright (C) 2004 William Lee Irwin III
 */

#include <linux/stop_machine.h>
#include <linux/clocksource.h>
#include <linux/kallsyms.h>
#include <linux/kthread.h>
#include <linux/hardirq.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/hash.h>
#include <linux/list.h>

#include "trace.h"

#ifdef CONFIG_DYNAMIC_FTRACE
# define FTRACE_ENABLED_INIT 1
#else
# define FTRACE_ENABLED_INIT 0
#endif

int ftrace_enabled = FTRACE_ENABLED_INIT;
static int last_ftrace_enabled = FTRACE_ENABLED_INIT;

static DEFINE_SPINLOCK(ftrace_lock);
static DEFINE_MUTEX(ftrace_sysctl_lock);

static struct ftrace_ops ftrace_list_end __read_mostly =
{
	.func = ftrace_stub,
};

static struct ftrace_ops *ftrace_list __read_mostly = &ftrace_list_end;
ftrace_func_t ftrace_trace_function __read_mostly = ftrace_stub;

/* mcount is defined per arch in assembly */
EXPORT_SYMBOL(mcount);

notrace void ftrace_list_func(unsigned long ip, unsigned long parent_ip)
{
	struct ftrace_ops *op = ftrace_list;

	/* in case someone actually ports this to alpha! */
	read_barrier_depends();

	while (op != &ftrace_list_end) {
		/* silly alpha */
		read_barrier_depends();
		op->func(ip, parent_ip);
		op = op->next;
	};
}

/**
 * clear_ftrace_function - reset the ftrace function
 *
 * This NULLs the ftrace function and in essence stops
 * tracing.  There may be lag
 */
void clear_ftrace_function(void)
{
	ftrace_trace_function = ftrace_stub;
}

static int notrace __register_ftrace_function(struct ftrace_ops *ops)
{
	/* Should never be called by interrupts */
	spin_lock(&ftrace_lock);

	ops->next = ftrace_list;
	/*
	 * We are entering ops into the ftrace_list but another
	 * CPU might be walking that list. We need to make sure
	 * the ops->next pointer is valid before another CPU sees
	 * the ops pointer included into the ftrace_list.
	 */
	smp_wmb();
	ftrace_list = ops;

	if (ftrace_enabled) {
		/*
		 * For one func, simply call it directly.
		 * For more than one func, call the chain.
		 */
		if (ops->next == &ftrace_list_end)
			ftrace_trace_function = ops->func;
		else
			ftrace_trace_function = ftrace_list_func;
	}

	spin_unlock(&ftrace_lock);

	return 0;
}

static int notrace __unregister_ftrace_function(struct ftrace_ops *ops)
{
	struct ftrace_ops **p;
	int ret = 0;

	spin_lock(&ftrace_lock);

	/*
	 * If we are removing the last function, then simply point
	 * to the ftrace_stub.
	 */
	if (ftrace_list == ops && ops->next == &ftrace_list_end) {
		ftrace_trace_function = ftrace_stub;
		ftrace_list = &ftrace_list_end;
		goto out;
	}

	for (p = &ftrace_list; *p != &ftrace_list_end; p = &(*p)->next)
		if (*p == ops)
			break;

	if (*p != ops) {
		ret = -1;
		goto out;
	}

	*p = (*p)->next;

	if (ftrace_enabled) {
		/* If we only have one func left, then call that directly */
		if (ftrace_list == &ftrace_list_end ||
		    ftrace_list->next == &ftrace_list_end)
			ftrace_trace_function = ftrace_list->func;
	}

 out:
	spin_unlock(&ftrace_lock);

	return ret;
}

#ifdef CONFIG_DYNAMIC_FTRACE

static struct hlist_head ftrace_hash[FTRACE_HASHSIZE];

static DEFINE_PER_CPU(int, ftrace_shutdown_disable_cpu);

static DEFINE_SPINLOCK(ftrace_shutdown_lock);
static DEFINE_MUTEX(ftraced_lock);

static int ftraced_trigger;
static int ftraced_suspend;

static int ftrace_record_suspend;

static inline int
notrace ftrace_ip_in_hash(unsigned long ip, unsigned long key)
{
	struct dyn_ftrace *p;
	struct hlist_node *t;
	int found = 0;

	hlist_for_each_entry(p, t, &ftrace_hash[key], node) {
		if (p->ip == ip) {
			found = 1;
			break;
		}
	}

	return found;
}

static inline void notrace
ftrace_add_hash(struct dyn_ftrace *node, unsigned long key)
{
	hlist_add_head(&node->node, &ftrace_hash[key]);
}

static void notrace
ftrace_record_ip(unsigned long ip, unsigned long parent_ip)
{
	struct dyn_ftrace *node;
	unsigned long flags;
	unsigned long key;
	int resched;
	int atomic;

	resched = need_resched();
	preempt_disable_notrace();

	/* We simply need to protect against recursion */
	__get_cpu_var(ftrace_shutdown_disable_cpu)++;
	if (__get_cpu_var(ftrace_shutdown_disable_cpu) != 1)
		goto out;

	if (unlikely(ftrace_record_suspend))
		goto out;

	key = hash_long(ip, FTRACE_HASHBITS);

	WARN_ON_ONCE(key >= FTRACE_HASHSIZE);

	if (ftrace_ip_in_hash(ip, key))
		goto out;

	atomic = irqs_disabled();

	spin_lock_irqsave(&ftrace_shutdown_lock, flags);

	/* This ip may have hit the hash before the lock */
	if (ftrace_ip_in_hash(ip, key))
		goto out_unlock;

	/*
	 * There's a slight race that the ftraced will update the
	 * hash and reset here. The arch alloc is responsible
	 * for seeing if the IP has already changed, and if
	 * it has, the alloc will fail.
	 */
	node = ftrace_alloc_shutdown_node(ip);
	if (!node)
		goto out_unlock;

	node->ip = ip;

	ftrace_add_hash(node, key);

	ftraced_trigger = 1;

 out_unlock:
	spin_unlock_irqrestore(&ftrace_shutdown_lock, flags);
 out:
	__get_cpu_var(ftrace_shutdown_disable_cpu)--;

	/* prevent recursion with scheduler */
	if (resched)
		preempt_enable_no_resched_notrace();
	else
		preempt_enable_notrace();
}

static struct ftrace_ops ftrace_shutdown_ops __read_mostly =
{
	.func = ftrace_record_ip,
};


static int notrace __ftrace_modify_code(void *data)
{
	void (*func)(void) = data;

	func();
	return 0;
}

static void notrace ftrace_run_startup_code(void)
{
	stop_machine_run(__ftrace_modify_code, ftrace_startup_code, NR_CPUS);
}

static void notrace ftrace_run_shutdown_code(void)
{
	stop_machine_run(__ftrace_modify_code, ftrace_shutdown_code, NR_CPUS);
}

static void notrace ftrace_startup(void)
{
	mutex_lock(&ftraced_lock);
	ftraced_suspend++;
	if (ftraced_suspend != 1)
		goto out;
	__unregister_ftrace_function(&ftrace_shutdown_ops);

	if (ftrace_enabled)
		ftrace_run_startup_code();
 out:
	mutex_unlock(&ftraced_lock);
}

static void notrace ftrace_shutdown(void)
{
	mutex_lock(&ftraced_lock);
	ftraced_suspend--;
	if (ftraced_suspend)
		goto out;

	if (ftrace_enabled)
		ftrace_run_shutdown_code();

	__register_ftrace_function(&ftrace_shutdown_ops);
 out:
	mutex_unlock(&ftraced_lock);
}

static void notrace ftrace_startup_sysctl(void)
{
	mutex_lock(&ftraced_lock);
	/* ftraced_suspend is true if we want ftrace running */
	if (ftraced_suspend)
		ftrace_run_startup_code();
	mutex_unlock(&ftraced_lock);
}

static void notrace ftrace_shutdown_sysctl(void)
{
	mutex_lock(&ftraced_lock);
	/* ftraced_suspend is true if ftrace is running */
	if (ftraced_suspend)
		ftrace_run_shutdown_code();
	mutex_unlock(&ftraced_lock);
}

static cycle_t		ftrace_update_time;
static unsigned long	ftrace_update_cnt;
unsigned long		ftrace_update_tot_cnt;

static int notrace __ftrace_update_code(void *ignore)
{
	struct dyn_ftrace *p;
	struct hlist_head head;
	struct hlist_node *t;
	cycle_t start, stop;
	int i;

	/* Don't be calling ftrace ops now */
	__unregister_ftrace_function(&ftrace_shutdown_ops);

	start = now(raw_smp_processor_id());
	ftrace_update_cnt = 0;

	/* No locks needed, the machine is stopped! */
	for (i = 0; i < FTRACE_HASHSIZE; i++) {
		if (hlist_empty(&ftrace_hash[i]))
			continue;

		head = ftrace_hash[i];
		INIT_HLIST_HEAD(&ftrace_hash[i]);

		/* all CPUS are stopped, we are safe to modify code */
		hlist_for_each_entry(p, t, &head, node) {
			ftrace_code_disable(p);
			ftrace_update_cnt++;
		}

	}

	stop = now(raw_smp_processor_id());
	ftrace_update_time = stop - start;
	ftrace_update_tot_cnt += ftrace_update_cnt;

	__register_ftrace_function(&ftrace_shutdown_ops);

	return 0;
}

static void notrace ftrace_update_code(void)
{
	stop_machine_run(__ftrace_update_code, NULL, NR_CPUS);
}

static int notrace ftraced(void *ignore)
{
	unsigned long usecs;

	set_current_state(TASK_INTERRUPTIBLE);

	while (!kthread_should_stop()) {

		/* check once a second */
		schedule_timeout(HZ);

		mutex_lock(&ftrace_sysctl_lock);
		mutex_lock(&ftraced_lock);
		if (ftrace_enabled && ftraced_trigger && !ftraced_suspend) {
			ftrace_record_suspend++;
			ftrace_update_code();
			usecs = nsecs_to_usecs(ftrace_update_time);
			if (ftrace_update_tot_cnt > 100000) {
				ftrace_update_tot_cnt = 0;
				pr_info("hm, dftrace overflow: %lu change%s"
					 " (%lu total) in %lu usec%s\n",
					ftrace_update_cnt,
					ftrace_update_cnt != 1 ? "s" : "",
					ftrace_update_tot_cnt,
					usecs, usecs != 1 ? "s" : "");
				WARN_ON_ONCE(1);
			}
			ftraced_trigger = 0;
			ftrace_record_suspend--;
		}
		mutex_unlock(&ftraced_lock);
		mutex_unlock(&ftrace_sysctl_lock);

		ftrace_shutdown_replenish();

		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);
	return 0;
}

static int __init notrace ftrace_shutdown_init(void)
{
	struct task_struct *p;
	int ret;

	ret = ftrace_shutdown_arch_init();
	if (ret)
		return ret;

	p = kthread_run(ftraced, NULL, "ftraced");
	if (IS_ERR(p))
		return -1;

	__register_ftrace_function(&ftrace_shutdown_ops);

	return 0;
}

core_initcall(ftrace_shutdown_init);
#else
# define ftrace_startup()	  do { } while (0)
# define ftrace_shutdown()	  do { } while (0)
# define ftrace_startup_sysctl()  do { } while (0)
# define ftrace_shutdown_sysctl() do { } while (0)
#endif /* CONFIG_DYNAMIC_FTRACE */

/**
 * register_ftrace_function - register a function for profiling
 * @ops - ops structure that holds the function for profiling.
 *
 * Register a function to be called by all functions in the
 * kernel.
 *
 * Note: @ops->func and all the functions it calls must be labeled
 *       with "notrace", otherwise it will go into a
 *       recursive loop.
 */
int register_ftrace_function(struct ftrace_ops *ops)
{
	int ret;

	mutex_lock(&ftrace_sysctl_lock);
	ftrace_startup();

	ret = __register_ftrace_function(ops);
	mutex_unlock(&ftrace_sysctl_lock);

	return ret;
}

/**
 * unregister_ftrace_function - unresgister a function for profiling.
 * @ops - ops structure that holds the function to unregister
 *
 * Unregister a function that was added to be called by ftrace profiling.
 */
int unregister_ftrace_function(struct ftrace_ops *ops)
{
	int ret;

	mutex_lock(&ftrace_sysctl_lock);
	ret = __unregister_ftrace_function(ops);

	if (ftrace_list == &ftrace_list_end)
		ftrace_shutdown();

	mutex_unlock(&ftrace_sysctl_lock);

	return ret;
}

notrace int
ftrace_enable_sysctl(struct ctl_table *table, int write,
		     struct file *filp, void __user *buffer, size_t *lenp,
		     loff_t *ppos)
{
	int ret;

	mutex_lock(&ftrace_sysctl_lock);

	ret  = proc_dointvec(table, write, filp, buffer, lenp, ppos);

	if (ret || !write || (last_ftrace_enabled == ftrace_enabled))
		goto out;

	last_ftrace_enabled = ftrace_enabled;

	if (ftrace_enabled) {

		ftrace_startup_sysctl();

		/* we are starting ftrace again */
		if (ftrace_list != &ftrace_list_end) {
			if (ftrace_list->next == &ftrace_list_end)
				ftrace_trace_function = ftrace_list->func;
			else
				ftrace_trace_function = ftrace_list_func;
		}

	} else {
		/* stopping ftrace calls (just send to ftrace_stub) */
		ftrace_trace_function = ftrace_stub;

		ftrace_shutdown_sysctl();
	}

 out:
	mutex_unlock(&ftrace_sysctl_lock);
	return ret;
}
