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

int ftrace_enabled;
static int last_ftrace_enabled;

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

enum {
	FTRACE_ENABLE_CALLS		= (1 << 0),
	FTRACE_DISABLE_CALLS		= (1 << 1),
	FTRACE_UPDATE_TRACE_FUNC	= (1 << 2),
	FTRACE_ENABLE_MCOUNT		= (1 << 3),
	FTRACE_DISABLE_MCOUNT		= (1 << 4),
};

static struct hlist_head ftrace_hash[FTRACE_HASHSIZE];

static DEFINE_PER_CPU(int, ftrace_shutdown_disable_cpu);

static DEFINE_SPINLOCK(ftrace_shutdown_lock);
static DEFINE_MUTEX(ftraced_lock);

struct ftrace_page {
	struct ftrace_page	*next;
	int			index;
	struct dyn_ftrace	records[];
} __attribute__((packed));

#define ENTRIES_PER_PAGE \
  ((PAGE_SIZE - sizeof(struct ftrace_page)) / sizeof(struct dyn_ftrace))

/* estimate from running different kernels */
#define NR_TO_INIT		10000

static struct ftrace_page	*ftrace_pages_start;
static struct ftrace_page	*ftrace_pages;

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

static notrace struct dyn_ftrace *ftrace_alloc_dyn_node(unsigned long ip)
{
	if (ftrace_pages->index == ENTRIES_PER_PAGE) {
		if (!ftrace_pages->next)
			return NULL;
		ftrace_pages = ftrace_pages->next;
	}

	return &ftrace_pages->records[ftrace_pages->index++];
}

static void notrace
ftrace_record_ip(unsigned long ip)
{
	struct dyn_ftrace *node;
	unsigned long flags;
	unsigned long key;
	int resched;
	int atomic;

	if (!ftrace_enabled)
		return;

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
	 * hash and reset here. If it is already converted, skip it.
	 */
	if (ftrace_ip_converted(ip))
		goto out_unlock;

	node = ftrace_alloc_dyn_node(ip);
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

#define FTRACE_ADDR ((long)(&ftrace_caller))
#define MCOUNT_ADDR ((long)(&mcount))

static void notrace ftrace_replace_code(int saved)
{
	unsigned char *new = NULL, *old = NULL;
	struct dyn_ftrace *rec;
	struct ftrace_page *pg;
	unsigned long ip;
	int failed;
	int i;

	if (saved)
		old = ftrace_nop_replace();
	else
		new = ftrace_nop_replace();

	for (pg = ftrace_pages_start; pg; pg = pg->next) {
		for (i = 0; i < pg->index; i++) {
			rec = &pg->records[i];

			/* don't modify code that has already faulted */
			if (rec->flags & FTRACE_FL_FAILED)
				continue;

			ip = rec->ip;

			if (saved)
				new = ftrace_call_replace(ip, FTRACE_ADDR);
			else
				old = ftrace_call_replace(ip, FTRACE_ADDR);

			failed = ftrace_modify_code(ip, old, new);
			if (failed)
				rec->flags |= FTRACE_FL_FAILED;
		}
	}
}

static notrace void ftrace_shutdown_replenish(void)
{
	if (ftrace_pages->next)
		return;

	/* allocate another page */
	ftrace_pages->next = (void *)get_zeroed_page(GFP_KERNEL);
}

static notrace void
ftrace_code_disable(struct dyn_ftrace *rec)
{
	unsigned long ip;
	unsigned char *nop, *call;
	int failed;

	ip = rec->ip;

	nop = ftrace_nop_replace();
	call = ftrace_call_replace(ip, MCOUNT_ADDR);

	failed = ftrace_modify_code(ip, call, nop);
	if (failed)
		rec->flags |= FTRACE_FL_FAILED;
}

static int notrace __ftrace_modify_code(void *data)
{
	unsigned long addr;
	int *command = data;

	if (*command & FTRACE_ENABLE_CALLS)
		ftrace_replace_code(1);
	else if (*command & FTRACE_DISABLE_CALLS)
		ftrace_replace_code(0);

	if (*command & FTRACE_UPDATE_TRACE_FUNC)
		ftrace_update_ftrace_func(ftrace_trace_function);

	if (*command & FTRACE_ENABLE_MCOUNT) {
		addr = (unsigned long)ftrace_record_ip;
		ftrace_mcount_set(&addr);
	} else if (*command & FTRACE_DISABLE_MCOUNT) {
		addr = (unsigned long)ftrace_stub;
		ftrace_mcount_set(&addr);
	}

	return 0;
}

static void notrace ftrace_run_update_code(int command)
{
	stop_machine_run(__ftrace_modify_code, &command, NR_CPUS);
}

static ftrace_func_t saved_ftrace_func;

static void notrace ftrace_startup(void)
{
	int command = 0;

	mutex_lock(&ftraced_lock);
	ftraced_suspend++;
	if (ftraced_suspend == 1)
		command |= FTRACE_ENABLE_CALLS;

	if (saved_ftrace_func != ftrace_trace_function) {
		saved_ftrace_func = ftrace_trace_function;
		command |= FTRACE_UPDATE_TRACE_FUNC;
	}

	if (!command || !ftrace_enabled)
		goto out;

	ftrace_run_update_code(command);
 out:
	mutex_unlock(&ftraced_lock);
}

static void notrace ftrace_shutdown(void)
{
	int command = 0;

	mutex_lock(&ftraced_lock);
	ftraced_suspend--;
	if (!ftraced_suspend)
		command |= FTRACE_DISABLE_CALLS;

	if (saved_ftrace_func != ftrace_trace_function) {
		saved_ftrace_func = ftrace_trace_function;
		command |= FTRACE_UPDATE_TRACE_FUNC;
	}

	if (!command || !ftrace_enabled)
		goto out;

	ftrace_run_update_code(command);
 out:
	mutex_unlock(&ftraced_lock);
}

static void notrace ftrace_startup_sysctl(void)
{
	int command = FTRACE_ENABLE_MCOUNT;

	mutex_lock(&ftraced_lock);
	/* Force update next time */
	saved_ftrace_func = NULL;
	/* ftraced_suspend is true if we want ftrace running */
	if (ftraced_suspend)
		command |= FTRACE_ENABLE_CALLS;

	ftrace_run_update_code(command);
	mutex_unlock(&ftraced_lock);
}

static void notrace ftrace_shutdown_sysctl(void)
{
	int command = FTRACE_DISABLE_MCOUNT;

	mutex_lock(&ftraced_lock);
	/* ftraced_suspend is true if ftrace is running */
	if (ftraced_suspend)
		command |= FTRACE_DISABLE_CALLS;

	ftrace_run_update_code(command);
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
	int save_ftrace_enabled;
	cycle_t start, stop;
	int i;

	/* Don't be recording funcs now */
	save_ftrace_enabled = ftrace_enabled;
	ftrace_enabled = 0;

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

	ftrace_enabled = save_ftrace_enabled;

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

static int __init ftrace_dyn_table_alloc(void)
{
	struct ftrace_page *pg;
	int cnt;
	int i;

	/* allocate a few pages */
	ftrace_pages_start = (void *)get_zeroed_page(GFP_KERNEL);
	if (!ftrace_pages_start)
		return -1;

	/*
	 * Allocate a few more pages.
	 *
	 * TODO: have some parser search vmlinux before
	 *   final linking to find all calls to ftrace.
	 *   Then we can:
	 *    a) know how many pages to allocate.
	 *     and/or
	 *    b) set up the table then.
	 *
	 *  The dynamic code is still necessary for
	 *  modules.
	 */

	pg = ftrace_pages = ftrace_pages_start;

	cnt = NR_TO_INIT / ENTRIES_PER_PAGE;

	for (i = 0; i < cnt; i++) {
		pg->next = (void *)get_zeroed_page(GFP_KERNEL);

		/* If we fail, we'll try later anyway */
		if (!pg->next)
			break;

		pg = pg->next;
	}

	return 0;
}

static int __init notrace ftrace_dynamic_init(void)
{
	struct task_struct *p;
	unsigned long addr;
	int ret;

	addr = (unsigned long)ftrace_record_ip;
	stop_machine_run(ftrace_dyn_arch_init, &addr, NR_CPUS);

	/* ftrace_dyn_arch_init places the return code in addr */
	if (addr)
		return addr;

	ret = ftrace_dyn_table_alloc();
	if (ret)
		return ret;

	p = kthread_run(ftraced, NULL, "ftraced");
	if (IS_ERR(p))
		return -1;

	last_ftrace_enabled = ftrace_enabled = 1;

	return 0;
}

core_initcall(ftrace_dynamic_init);
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
	ret = __register_ftrace_function(ops);
	ftrace_startup();
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
