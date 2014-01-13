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
 *  Copyright (C) 2004 Nadia Yvette Chambers
 */

#include <linux/stop_machine.h>
#include <linux/clocksource.h>
#include <linux/kallsyms.h>
#include <linux/seq_file.h>
#include <linux/suspend.h>
#include <linux/debugfs.h>
#include <linux/hardirq.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include <linux/bsearch.h>
#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/sysctl.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/sort.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/rcupdate.h>

#include <trace/events/sched.h>

#include <asm/setup.h>

#include "trace_output.h"
#include "trace_stat.h"

#define FTRACE_WARN_ON(cond)			\
	({					\
		int ___r = cond;		\
		if (WARN_ON(___r))		\
			ftrace_kill();		\
		___r;				\
	})

#define FTRACE_WARN_ON_ONCE(cond)		\
	({					\
		int ___r = cond;		\
		if (WARN_ON_ONCE(___r))		\
			ftrace_kill();		\
		___r;				\
	})

/* hash bits for specific function selection */
#define FTRACE_HASH_BITS 7
#define FTRACE_FUNC_HASHSIZE (1 << FTRACE_HASH_BITS)
#define FTRACE_HASH_DEFAULT_BITS 10
#define FTRACE_HASH_MAX_BITS 12

#define FL_GLOBAL_CONTROL_MASK (FTRACE_OPS_FL_GLOBAL | FTRACE_OPS_FL_CONTROL)

#ifdef CONFIG_DYNAMIC_FTRACE
#define INIT_REGEX_LOCK(opsname)	\
	.regex_lock	= __MUTEX_INITIALIZER(opsname.regex_lock),
#else
#define INIT_REGEX_LOCK(opsname)
#endif

static struct ftrace_ops ftrace_list_end __read_mostly = {
	.func		= ftrace_stub,
	.flags		= FTRACE_OPS_FL_RECURSION_SAFE | FTRACE_OPS_FL_STUB,
};

/* ftrace_enabled is a method to turn ftrace on or off */
int ftrace_enabled __read_mostly;
static int last_ftrace_enabled;

/* Quick disabling of function tracer. */
int function_trace_stop __read_mostly;

/* Current function tracing op */
struct ftrace_ops *function_trace_op __read_mostly = &ftrace_list_end;
/* What to set function_trace_op to */
static struct ftrace_ops *set_function_trace_op;

/* List for set_ftrace_pid's pids. */
LIST_HEAD(ftrace_pids);
struct ftrace_pid {
	struct list_head list;
	struct pid *pid;
};

/*
 * ftrace_disabled is set when an anomaly is discovered.
 * ftrace_disabled is much stronger than ftrace_enabled.
 */
static int ftrace_disabled __read_mostly;

static DEFINE_MUTEX(ftrace_lock);

static struct ftrace_ops *ftrace_global_list __read_mostly = &ftrace_list_end;
static struct ftrace_ops *ftrace_control_list __read_mostly = &ftrace_list_end;
static struct ftrace_ops *ftrace_ops_list __read_mostly = &ftrace_list_end;
ftrace_func_t ftrace_trace_function __read_mostly = ftrace_stub;
ftrace_func_t ftrace_pid_function __read_mostly = ftrace_stub;
static struct ftrace_ops global_ops;
static struct ftrace_ops control_ops;

#if ARCH_SUPPORTS_FTRACE_OPS
static void ftrace_ops_list_func(unsigned long ip, unsigned long parent_ip,
				 struct ftrace_ops *op, struct pt_regs *regs);
#else
/* See comment below, where ftrace_ops_list_func is defined */
static void ftrace_ops_no_ops(unsigned long ip, unsigned long parent_ip);
#define ftrace_ops_list_func ((ftrace_func_t)ftrace_ops_no_ops)
#endif

/*
 * Traverse the ftrace_global_list, invoking all entries.  The reason that we
 * can use rcu_dereference_raw_notrace() is that elements removed from this list
 * are simply leaked, so there is no need to interact with a grace-period
 * mechanism.  The rcu_dereference_raw_notrace() calls are needed to handle
 * concurrent insertions into the ftrace_global_list.
 *
 * Silly Alpha and silly pointer-speculation compiler optimizations!
 */
#define do_for_each_ftrace_op(op, list)			\
	op = rcu_dereference_raw_notrace(list);			\
	do

/*
 * Optimized for just a single item in the list (as that is the normal case).
 */
#define while_for_each_ftrace_op(op)				\
	while (likely(op = rcu_dereference_raw_notrace((op)->next)) &&	\
	       unlikely((op) != &ftrace_list_end))

static inline void ftrace_ops_init(struct ftrace_ops *ops)
{
#ifdef CONFIG_DYNAMIC_FTRACE
	if (!(ops->flags & FTRACE_OPS_FL_INITIALIZED)) {
		mutex_init(&ops->regex_lock);
		ops->flags |= FTRACE_OPS_FL_INITIALIZED;
	}
#endif
}

/**
 * ftrace_nr_registered_ops - return number of ops registered
 *
 * Returns the number of ftrace_ops registered and tracing functions
 */
int ftrace_nr_registered_ops(void)
{
	struct ftrace_ops *ops;
	int cnt = 0;

	mutex_lock(&ftrace_lock);

	for (ops = ftrace_ops_list;
	     ops != &ftrace_list_end; ops = ops->next)
		cnt++;

	mutex_unlock(&ftrace_lock);

	return cnt;
}

static void
ftrace_global_list_func(unsigned long ip, unsigned long parent_ip,
			struct ftrace_ops *op, struct pt_regs *regs)
{
	int bit;

	bit = trace_test_and_set_recursion(TRACE_GLOBAL_START, TRACE_GLOBAL_MAX);
	if (bit < 0)
		return;

	do_for_each_ftrace_op(op, ftrace_global_list) {
		op->func(ip, parent_ip, op, regs);
	} while_for_each_ftrace_op(op);

	trace_clear_recursion(bit);
}

static void ftrace_pid_func(unsigned long ip, unsigned long parent_ip,
			    struct ftrace_ops *op, struct pt_regs *regs)
{
	if (!test_tsk_trace_trace(current))
		return;

	ftrace_pid_function(ip, parent_ip, op, regs);
}

static void set_ftrace_pid_function(ftrace_func_t func)
{
	/* do not set ftrace_pid_function to itself! */
	if (func != ftrace_pid_func)
		ftrace_pid_function = func;
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
	ftrace_pid_function = ftrace_stub;
}

static void control_ops_disable_all(struct ftrace_ops *ops)
{
	int cpu;

	for_each_possible_cpu(cpu)
		*per_cpu_ptr(ops->disabled, cpu) = 1;
}

static int control_ops_alloc(struct ftrace_ops *ops)
{
	int __percpu *disabled;

	disabled = alloc_percpu(int);
	if (!disabled)
		return -ENOMEM;

	ops->disabled = disabled;
	control_ops_disable_all(ops);
	return 0;
}

static void control_ops_free(struct ftrace_ops *ops)
{
	free_percpu(ops->disabled);
}

static void update_global_ops(void)
{
	ftrace_func_t func;

	/*
	 * If there's only one function registered, then call that
	 * function directly. Otherwise, we need to iterate over the
	 * registered callers.
	 */
	if (ftrace_global_list == &ftrace_list_end ||
	    ftrace_global_list->next == &ftrace_list_end) {
		func = ftrace_global_list->func;
		/*
		 * As we are calling the function directly.
		 * If it does not have recursion protection,
		 * the function_trace_op needs to be updated
		 * accordingly.
		 */
		if (ftrace_global_list->flags & FTRACE_OPS_FL_RECURSION_SAFE)
			global_ops.flags |= FTRACE_OPS_FL_RECURSION_SAFE;
		else
			global_ops.flags &= ~FTRACE_OPS_FL_RECURSION_SAFE;
	} else {
		func = ftrace_global_list_func;
		/* The list has its own recursion protection. */
		global_ops.flags |= FTRACE_OPS_FL_RECURSION_SAFE;
	}


	/* If we filter on pids, update to use the pid function */
	if (!list_empty(&ftrace_pids)) {
		set_ftrace_pid_function(func);
		func = ftrace_pid_func;
	}

	global_ops.func = func;
}

static void ftrace_sync(struct work_struct *work)
{
	/*
	 * This function is just a stub to implement a hard force
	 * of synchronize_sched(). This requires synchronizing
	 * tasks even in userspace and idle.
	 *
	 * Yes, function tracing is rude.
	 */
}

static void ftrace_sync_ipi(void *data)
{
	/* Probably not needed, but do it anyway */
	smp_rmb();
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static void update_function_graph_func(void);
#else
static inline void update_function_graph_func(void) { }
#endif

static void update_ftrace_function(void)
{
	ftrace_func_t func;

	update_global_ops();

	/*
	 * If we are at the end of the list and this ops is
	 * recursion safe and not dynamic and the arch supports passing ops,
	 * then have the mcount trampoline call the function directly.
	 */
	if (ftrace_ops_list == &ftrace_list_end ||
	    (ftrace_ops_list->next == &ftrace_list_end &&
	     !(ftrace_ops_list->flags & FTRACE_OPS_FL_DYNAMIC) &&
	     (ftrace_ops_list->flags & FTRACE_OPS_FL_RECURSION_SAFE) &&
	     !FTRACE_FORCE_LIST_FUNC)) {
		/* Set the ftrace_ops that the arch callback uses */
		if (ftrace_ops_list == &global_ops)
			set_function_trace_op = ftrace_global_list;
		else
			set_function_trace_op = ftrace_ops_list;
		func = ftrace_ops_list->func;
	} else {
		/* Just use the default ftrace_ops */
		set_function_trace_op = &ftrace_list_end;
		func = ftrace_ops_list_func;
	}

	/* If there's no change, then do nothing more here */
	if (ftrace_trace_function == func)
		return;

	update_function_graph_func();

	/*
	 * If we are using the list function, it doesn't care
	 * about the function_trace_ops.
	 */
	if (func == ftrace_ops_list_func) {
		ftrace_trace_function = func;
		/*
		 * Don't even bother setting function_trace_ops,
		 * it would be racy to do so anyway.
		 */
		return;
	}

#ifndef CONFIG_DYNAMIC_FTRACE
	/*
	 * For static tracing, we need to be a bit more careful.
	 * The function change takes affect immediately. Thus,
	 * we need to coorditate the setting of the function_trace_ops
	 * with the setting of the ftrace_trace_function.
	 *
	 * Set the function to the list ops, which will call the
	 * function we want, albeit indirectly, but it handles the
	 * ftrace_ops and doesn't depend on function_trace_op.
	 */
	ftrace_trace_function = ftrace_ops_list_func;
	/*
	 * Make sure all CPUs see this. Yes this is slow, but static
	 * tracing is slow and nasty to have enabled.
	 */
	schedule_on_each_cpu(ftrace_sync);
	/* Now all cpus are using the list ops. */
	function_trace_op = set_function_trace_op;
	/* Make sure the function_trace_op is visible on all CPUs */
	smp_wmb();
	/* Nasty way to force a rmb on all cpus */
	smp_call_function(ftrace_sync_ipi, NULL, 1);
	/* OK, we are all set to update the ftrace_trace_function now! */
#endif /* !CONFIG_DYNAMIC_FTRACE */

	ftrace_trace_function = func;
}

static void add_ftrace_ops(struct ftrace_ops **list, struct ftrace_ops *ops)
{
	ops->next = *list;
	/*
	 * We are entering ops into the list but another
	 * CPU might be walking that list. We need to make sure
	 * the ops->next pointer is valid before another CPU sees
	 * the ops pointer included into the list.
	 */
	rcu_assign_pointer(*list, ops);
}

static int remove_ftrace_ops(struct ftrace_ops **list, struct ftrace_ops *ops)
{
	struct ftrace_ops **p;

	/*
	 * If we are removing the last function, then simply point
	 * to the ftrace_stub.
	 */
	if (*list == ops && ops->next == &ftrace_list_end) {
		*list = &ftrace_list_end;
		return 0;
	}

	for (p = list; *p != &ftrace_list_end; p = &(*p)->next)
		if (*p == ops)
			break;

	if (*p != ops)
		return -1;

	*p = (*p)->next;
	return 0;
}

static void add_ftrace_list_ops(struct ftrace_ops **list,
				struct ftrace_ops *main_ops,
				struct ftrace_ops *ops)
{
	int first = *list == &ftrace_list_end;
	add_ftrace_ops(list, ops);
	if (first)
		add_ftrace_ops(&ftrace_ops_list, main_ops);
}

static int remove_ftrace_list_ops(struct ftrace_ops **list,
				  struct ftrace_ops *main_ops,
				  struct ftrace_ops *ops)
{
	int ret = remove_ftrace_ops(list, ops);
	if (!ret && *list == &ftrace_list_end)
		ret = remove_ftrace_ops(&ftrace_ops_list, main_ops);
	return ret;
}

static int __register_ftrace_function(struct ftrace_ops *ops)
{
	if (FTRACE_WARN_ON(ops == &global_ops))
		return -EINVAL;

	if (WARN_ON(ops->flags & FTRACE_OPS_FL_ENABLED))
		return -EBUSY;

	/* We don't support both control and global flags set. */
	if ((ops->flags & FL_GLOBAL_CONTROL_MASK) == FL_GLOBAL_CONTROL_MASK)
		return -EINVAL;

#ifndef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	/*
	 * If the ftrace_ops specifies SAVE_REGS, then it only can be used
	 * if the arch supports it, or SAVE_REGS_IF_SUPPORTED is also set.
	 * Setting SAVE_REGS_IF_SUPPORTED makes SAVE_REGS irrelevant.
	 */
	if (ops->flags & FTRACE_OPS_FL_SAVE_REGS &&
	    !(ops->flags & FTRACE_OPS_FL_SAVE_REGS_IF_SUPPORTED))
		return -EINVAL;

	if (ops->flags & FTRACE_OPS_FL_SAVE_REGS_IF_SUPPORTED)
		ops->flags |= FTRACE_OPS_FL_SAVE_REGS;
#endif

	if (!core_kernel_data((unsigned long)ops))
		ops->flags |= FTRACE_OPS_FL_DYNAMIC;

	if (ops->flags & FTRACE_OPS_FL_GLOBAL) {
		add_ftrace_list_ops(&ftrace_global_list, &global_ops, ops);
		ops->flags |= FTRACE_OPS_FL_ENABLED;
	} else if (ops->flags & FTRACE_OPS_FL_CONTROL) {
		if (control_ops_alloc(ops))
			return -ENOMEM;
		add_ftrace_list_ops(&ftrace_control_list, &control_ops, ops);
	} else
		add_ftrace_ops(&ftrace_ops_list, ops);

	if (ftrace_enabled)
		update_ftrace_function();

	return 0;
}

static int __unregister_ftrace_function(struct ftrace_ops *ops)
{
	int ret;

	if (WARN_ON(!(ops->flags & FTRACE_OPS_FL_ENABLED)))
		return -EBUSY;

	if (FTRACE_WARN_ON(ops == &global_ops))
		return -EINVAL;

	if (ops->flags & FTRACE_OPS_FL_GLOBAL) {
		ret = remove_ftrace_list_ops(&ftrace_global_list,
					     &global_ops, ops);
		if (!ret)
			ops->flags &= ~FTRACE_OPS_FL_ENABLED;
	} else if (ops->flags & FTRACE_OPS_FL_CONTROL) {
		ret = remove_ftrace_list_ops(&ftrace_control_list,
					     &control_ops, ops);
	} else
		ret = remove_ftrace_ops(&ftrace_ops_list, ops);

	if (ret < 0)
		return ret;

	if (ftrace_enabled)
		update_ftrace_function();

	return 0;
}

static void ftrace_update_pid_func(void)
{
	/* Only do something if we are tracing something */
	if (ftrace_trace_function == ftrace_stub)
		return;

	update_ftrace_function();
}

#ifdef CONFIG_FUNCTION_PROFILER
struct ftrace_profile {
	struct hlist_node		node;
	unsigned long			ip;
	unsigned long			counter;
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	unsigned long long		time;
	unsigned long long		time_squared;
#endif
};

struct ftrace_profile_page {
	struct ftrace_profile_page	*next;
	unsigned long			index;
	struct ftrace_profile		records[];
};

struct ftrace_profile_stat {
	atomic_t			disabled;
	struct hlist_head		*hash;
	struct ftrace_profile_page	*pages;
	struct ftrace_profile_page	*start;
	struct tracer_stat		stat;
};

#define PROFILE_RECORDS_SIZE						\
	(PAGE_SIZE - offsetof(struct ftrace_profile_page, records))

#define PROFILES_PER_PAGE					\
	(PROFILE_RECORDS_SIZE / sizeof(struct ftrace_profile))

static int ftrace_profile_enabled __read_mostly;

/* ftrace_profile_lock - synchronize the enable and disable of the profiler */
static DEFINE_MUTEX(ftrace_profile_lock);

static DEFINE_PER_CPU(struct ftrace_profile_stat, ftrace_profile_stats);

#define FTRACE_PROFILE_HASH_BITS 10
#define FTRACE_PROFILE_HASH_SIZE (1 << FTRACE_PROFILE_HASH_BITS)

static void *
function_stat_next(void *v, int idx)
{
	struct ftrace_profile *rec = v;
	struct ftrace_profile_page *pg;

	pg = (struct ftrace_profile_page *)((unsigned long)rec & PAGE_MASK);

 again:
	if (idx != 0)
		rec++;

	if ((void *)rec >= (void *)&pg->records[pg->index]) {
		pg = pg->next;
		if (!pg)
			return NULL;
		rec = &pg->records[0];
		if (!rec->counter)
			goto again;
	}

	return rec;
}

static void *function_stat_start(struct tracer_stat *trace)
{
	struct ftrace_profile_stat *stat =
		container_of(trace, struct ftrace_profile_stat, stat);

	if (!stat || !stat->start)
		return NULL;

	return function_stat_next(&stat->start->records[0], 0);
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/* function graph compares on total time */
static int function_stat_cmp(void *p1, void *p2)
{
	struct ftrace_profile *a = p1;
	struct ftrace_profile *b = p2;

	if (a->time < b->time)
		return -1;
	if (a->time > b->time)
		return 1;
	else
		return 0;
}
#else
/* not function graph compares against hits */
static int function_stat_cmp(void *p1, void *p2)
{
	struct ftrace_profile *a = p1;
	struct ftrace_profile *b = p2;

	if (a->counter < b->counter)
		return -1;
	if (a->counter > b->counter)
		return 1;
	else
		return 0;
}
#endif

static int function_stat_headers(struct seq_file *m)
{
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	seq_printf(m, "  Function                               "
		   "Hit    Time            Avg             s^2\n"
		      "  --------                               "
		   "---    ----            ---             ---\n");
#else
	seq_printf(m, "  Function                               Hit\n"
		      "  --------                               ---\n");
#endif
	return 0;
}

static int function_stat_show(struct seq_file *m, void *v)
{
	struct ftrace_profile *rec = v;
	char str[KSYM_SYMBOL_LEN];
	int ret = 0;
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	static struct trace_seq s;
	unsigned long long avg;
	unsigned long long stddev;
#endif
	mutex_lock(&ftrace_profile_lock);

	/* we raced with function_profile_reset() */
	if (unlikely(rec->counter == 0)) {
		ret = -EBUSY;
		goto out;
	}

	kallsyms_lookup(rec->ip, NULL, NULL, NULL, str);
	seq_printf(m, "  %-30.30s  %10lu", str, rec->counter);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	seq_printf(m, "    ");
	avg = rec->time;
	do_div(avg, rec->counter);

	/* Sample standard deviation (s^2) */
	if (rec->counter <= 1)
		stddev = 0;
	else {
		/*
		 * Apply Welford's method:
		 * s^2 = 1 / (n * (n-1)) * (n * \Sum (x_i)^2 - (\Sum x_i)^2)
		 */
		stddev = rec->counter * rec->time_squared -
			 rec->time * rec->time;

		/*
		 * Divide only 1000 for ns^2 -> us^2 conversion.
		 * trace_print_graph_duration will divide 1000 again.
		 */
		do_div(stddev, rec->counter * (rec->counter - 1) * 1000);
	}

	trace_seq_init(&s);
	trace_print_graph_duration(rec->time, &s);
	trace_seq_puts(&s, "    ");
	trace_print_graph_duration(avg, &s);
	trace_seq_puts(&s, "    ");
	trace_print_graph_duration(stddev, &s);
	trace_print_seq(m, &s);
#endif
	seq_putc(m, '\n');
out:
	mutex_unlock(&ftrace_profile_lock);

	return ret;
}

static void ftrace_profile_reset(struct ftrace_profile_stat *stat)
{
	struct ftrace_profile_page *pg;

	pg = stat->pages = stat->start;

	while (pg) {
		memset(pg->records, 0, PROFILE_RECORDS_SIZE);
		pg->index = 0;
		pg = pg->next;
	}

	memset(stat->hash, 0,
	       FTRACE_PROFILE_HASH_SIZE * sizeof(struct hlist_head));
}

int ftrace_profile_pages_init(struct ftrace_profile_stat *stat)
{
	struct ftrace_profile_page *pg;
	int functions;
	int pages;
	int i;

	/* If we already allocated, do nothing */
	if (stat->pages)
		return 0;

	stat->pages = (void *)get_zeroed_page(GFP_KERNEL);
	if (!stat->pages)
		return -ENOMEM;

#ifdef CONFIG_DYNAMIC_FTRACE
	functions = ftrace_update_tot_cnt;
#else
	/*
	 * We do not know the number of functions that exist because
	 * dynamic tracing is what counts them. With past experience
	 * we have around 20K functions. That should be more than enough.
	 * It is highly unlikely we will execute every function in
	 * the kernel.
	 */
	functions = 20000;
#endif

	pg = stat->start = stat->pages;

	pages = DIV_ROUND_UP(functions, PROFILES_PER_PAGE);

	for (i = 1; i < pages; i++) {
		pg->next = (void *)get_zeroed_page(GFP_KERNEL);
		if (!pg->next)
			goto out_free;
		pg = pg->next;
	}

	return 0;

 out_free:
	pg = stat->start;
	while (pg) {
		unsigned long tmp = (unsigned long)pg;

		pg = pg->next;
		free_page(tmp);
	}

	stat->pages = NULL;
	stat->start = NULL;

	return -ENOMEM;
}

static int ftrace_profile_init_cpu(int cpu)
{
	struct ftrace_profile_stat *stat;
	int size;

	stat = &per_cpu(ftrace_profile_stats, cpu);

	if (stat->hash) {
		/* If the profile is already created, simply reset it */
		ftrace_profile_reset(stat);
		return 0;
	}

	/*
	 * We are profiling all functions, but usually only a few thousand
	 * functions are hit. We'll make a hash of 1024 items.
	 */
	size = FTRACE_PROFILE_HASH_SIZE;

	stat->hash = kzalloc(sizeof(struct hlist_head) * size, GFP_KERNEL);

	if (!stat->hash)
		return -ENOMEM;

	/* Preallocate the function profiling pages */
	if (ftrace_profile_pages_init(stat) < 0) {
		kfree(stat->hash);
		stat->hash = NULL;
		return -ENOMEM;
	}

	return 0;
}

static int ftrace_profile_init(void)
{
	int cpu;
	int ret = 0;

	for_each_possible_cpu(cpu) {
		ret = ftrace_profile_init_cpu(cpu);
		if (ret)
			break;
	}

	return ret;
}

/* interrupts must be disabled */
static struct ftrace_profile *
ftrace_find_profiled_func(struct ftrace_profile_stat *stat, unsigned long ip)
{
	struct ftrace_profile *rec;
	struct hlist_head *hhd;
	unsigned long key;

	key = hash_long(ip, FTRACE_PROFILE_HASH_BITS);
	hhd = &stat->hash[key];

	if (hlist_empty(hhd))
		return NULL;

	hlist_for_each_entry_rcu_notrace(rec, hhd, node) {
		if (rec->ip == ip)
			return rec;
	}

	return NULL;
}

static void ftrace_add_profile(struct ftrace_profile_stat *stat,
			       struct ftrace_profile *rec)
{
	unsigned long key;

	key = hash_long(rec->ip, FTRACE_PROFILE_HASH_BITS);
	hlist_add_head_rcu(&rec->node, &stat->hash[key]);
}

/*
 * The memory is already allocated, this simply finds a new record to use.
 */
static struct ftrace_profile *
ftrace_profile_alloc(struct ftrace_profile_stat *stat, unsigned long ip)
{
	struct ftrace_profile *rec = NULL;

	/* prevent recursion (from NMIs) */
	if (atomic_inc_return(&stat->disabled) != 1)
		goto out;

	/*
	 * Try to find the function again since an NMI
	 * could have added it
	 */
	rec = ftrace_find_profiled_func(stat, ip);
	if (rec)
		goto out;

	if (stat->pages->index == PROFILES_PER_PAGE) {
		if (!stat->pages->next)
			goto out;
		stat->pages = stat->pages->next;
	}

	rec = &stat->pages->records[stat->pages->index++];
	rec->ip = ip;
	ftrace_add_profile(stat, rec);

 out:
	atomic_dec(&stat->disabled);

	return rec;
}

static void
function_profile_call(unsigned long ip, unsigned long parent_ip,
		      struct ftrace_ops *ops, struct pt_regs *regs)
{
	struct ftrace_profile_stat *stat;
	struct ftrace_profile *rec;
	unsigned long flags;

	if (!ftrace_profile_enabled)
		return;

	local_irq_save(flags);

	stat = &__get_cpu_var(ftrace_profile_stats);
	if (!stat->hash || !ftrace_profile_enabled)
		goto out;

	rec = ftrace_find_profiled_func(stat, ip);
	if (!rec) {
		rec = ftrace_profile_alloc(stat, ip);
		if (!rec)
			goto out;
	}

	rec->counter++;
 out:
	local_irq_restore(flags);
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static int profile_graph_entry(struct ftrace_graph_ent *trace)
{
	function_profile_call(trace->func, 0, NULL, NULL);
	return 1;
}

static void profile_graph_return(struct ftrace_graph_ret *trace)
{
	struct ftrace_profile_stat *stat;
	unsigned long long calltime;
	struct ftrace_profile *rec;
	unsigned long flags;

	local_irq_save(flags);
	stat = &__get_cpu_var(ftrace_profile_stats);
	if (!stat->hash || !ftrace_profile_enabled)
		goto out;

	/* If the calltime was zero'd ignore it */
	if (!trace->calltime)
		goto out;

	calltime = trace->rettime - trace->calltime;

	if (!(trace_flags & TRACE_ITER_GRAPH_TIME)) {
		int index;

		index = trace->depth;

		/* Append this call time to the parent time to subtract */
		if (index)
			current->ret_stack[index - 1].subtime += calltime;

		if (current->ret_stack[index].subtime < calltime)
			calltime -= current->ret_stack[index].subtime;
		else
			calltime = 0;
	}

	rec = ftrace_find_profiled_func(stat, trace->func);
	if (rec) {
		rec->time += calltime;
		rec->time_squared += calltime * calltime;
	}

 out:
	local_irq_restore(flags);
}

static int register_ftrace_profiler(void)
{
	return register_ftrace_graph(&profile_graph_return,
				     &profile_graph_entry);
}

static void unregister_ftrace_profiler(void)
{
	unregister_ftrace_graph();
}
#else
static struct ftrace_ops ftrace_profile_ops __read_mostly = {
	.func		= function_profile_call,
	.flags		= FTRACE_OPS_FL_RECURSION_SAFE | FTRACE_OPS_FL_INITIALIZED,
	INIT_REGEX_LOCK(ftrace_profile_ops)
};

static int register_ftrace_profiler(void)
{
	return register_ftrace_function(&ftrace_profile_ops);
}

static void unregister_ftrace_profiler(void)
{
	unregister_ftrace_function(&ftrace_profile_ops);
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

static ssize_t
ftrace_profile_write(struct file *filp, const char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	val = !!val;

	mutex_lock(&ftrace_profile_lock);
	if (ftrace_profile_enabled ^ val) {
		if (val) {
			ret = ftrace_profile_init();
			if (ret < 0) {
				cnt = ret;
				goto out;
			}

			ret = register_ftrace_profiler();
			if (ret < 0) {
				cnt = ret;
				goto out;
			}
			ftrace_profile_enabled = 1;
		} else {
			ftrace_profile_enabled = 0;
			/*
			 * unregister_ftrace_profiler calls stop_machine
			 * so this acts like an synchronize_sched.
			 */
			unregister_ftrace_profiler();
		}
	}
 out:
	mutex_unlock(&ftrace_profile_lock);

	*ppos += cnt;

	return cnt;
}

static ssize_t
ftrace_profile_read(struct file *filp, char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	char buf[64];		/* big enough to hold a number */
	int r;

	r = sprintf(buf, "%u\n", ftrace_profile_enabled);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static const struct file_operations ftrace_profile_fops = {
	.open		= tracing_open_generic,
	.read		= ftrace_profile_read,
	.write		= ftrace_profile_write,
	.llseek		= default_llseek,
};

/* used to initialize the real stat files */
static struct tracer_stat function_stats __initdata = {
	.name		= "functions",
	.stat_start	= function_stat_start,
	.stat_next	= function_stat_next,
	.stat_cmp	= function_stat_cmp,
	.stat_headers	= function_stat_headers,
	.stat_show	= function_stat_show
};

static __init void ftrace_profile_debugfs(struct dentry *d_tracer)
{
	struct ftrace_profile_stat *stat;
	struct dentry *entry;
	char *name;
	int ret;
	int cpu;

	for_each_possible_cpu(cpu) {
		stat = &per_cpu(ftrace_profile_stats, cpu);

		/* allocate enough for function name + cpu number */
		name = kmalloc(32, GFP_KERNEL);
		if (!name) {
			/*
			 * The files created are permanent, if something happens
			 * we still do not free memory.
			 */
			WARN(1,
			     "Could not allocate stat file for cpu %d\n",
			     cpu);
			return;
		}
		stat->stat = function_stats;
		snprintf(name, 32, "function%d", cpu);
		stat->stat.name = name;
		ret = register_stat_tracer(&stat->stat);
		if (ret) {
			WARN(1,
			     "Could not register function stat for cpu %d\n",
			     cpu);
			kfree(name);
			return;
		}
	}

	entry = debugfs_create_file("function_profile_enabled", 0644,
				    d_tracer, NULL, &ftrace_profile_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'function_profile_enabled' entry\n");
}

#else /* CONFIG_FUNCTION_PROFILER */
static __init void ftrace_profile_debugfs(struct dentry *d_tracer)
{
}
#endif /* CONFIG_FUNCTION_PROFILER */

static struct pid * const ftrace_swapper_pid = &init_struct_pid;

loff_t
ftrace_filter_lseek(struct file *file, loff_t offset, int whence)
{
	loff_t ret;

	if (file->f_mode & FMODE_READ)
		ret = seq_lseek(file, offset, whence);
	else
		file->f_pos = ret = 1;

	return ret;
}

#ifdef CONFIG_DYNAMIC_FTRACE

#ifndef CONFIG_FTRACE_MCOUNT_RECORD
# error Dynamic ftrace depends on MCOUNT_RECORD
#endif

static struct hlist_head ftrace_func_hash[FTRACE_FUNC_HASHSIZE] __read_mostly;

struct ftrace_func_probe {
	struct hlist_node	node;
	struct ftrace_probe_ops	*ops;
	unsigned long		flags;
	unsigned long		ip;
	void			*data;
	struct list_head	free_list;
};

struct ftrace_func_entry {
	struct hlist_node hlist;
	unsigned long ip;
};

struct ftrace_hash {
	unsigned long		size_bits;
	struct hlist_head	*buckets;
	unsigned long		count;
	struct rcu_head		rcu;
};

/*
 * We make these constant because no one should touch them,
 * but they are used as the default "empty hash", to avoid allocating
 * it all the time. These are in a read only section such that if
 * anyone does try to modify it, it will cause an exception.
 */
static const struct hlist_head empty_buckets[1];
static const struct ftrace_hash empty_hash = {
	.buckets = (struct hlist_head *)empty_buckets,
};
#define EMPTY_HASH	((struct ftrace_hash *)&empty_hash)

static struct ftrace_ops global_ops = {
	.func			= ftrace_stub,
	.notrace_hash		= EMPTY_HASH,
	.filter_hash		= EMPTY_HASH,
	.flags			= FTRACE_OPS_FL_RECURSION_SAFE | FTRACE_OPS_FL_INITIALIZED,
	INIT_REGEX_LOCK(global_ops)
};

struct ftrace_page {
	struct ftrace_page	*next;
	struct dyn_ftrace	*records;
	int			index;
	int			size;
};

static struct ftrace_page *ftrace_new_pgs;

#define ENTRY_SIZE sizeof(struct dyn_ftrace)
#define ENTRIES_PER_PAGE (PAGE_SIZE / ENTRY_SIZE)

/* estimate from running different kernels */
#define NR_TO_INIT		10000

static struct ftrace_page	*ftrace_pages_start;
static struct ftrace_page	*ftrace_pages;

static bool ftrace_hash_empty(struct ftrace_hash *hash)
{
	return !hash || !hash->count;
}

static struct ftrace_func_entry *
ftrace_lookup_ip(struct ftrace_hash *hash, unsigned long ip)
{
	unsigned long key;
	struct ftrace_func_entry *entry;
	struct hlist_head *hhd;

	if (ftrace_hash_empty(hash))
		return NULL;

	if (hash->size_bits > 0)
		key = hash_long(ip, hash->size_bits);
	else
		key = 0;

	hhd = &hash->buckets[key];

	hlist_for_each_entry_rcu_notrace(entry, hhd, hlist) {
		if (entry->ip == ip)
			return entry;
	}
	return NULL;
}

static void __add_hash_entry(struct ftrace_hash *hash,
			     struct ftrace_func_entry *entry)
{
	struct hlist_head *hhd;
	unsigned long key;

	if (hash->size_bits)
		key = hash_long(entry->ip, hash->size_bits);
	else
		key = 0;

	hhd = &hash->buckets[key];
	hlist_add_head(&entry->hlist, hhd);
	hash->count++;
}

static int add_hash_entry(struct ftrace_hash *hash, unsigned long ip)
{
	struct ftrace_func_entry *entry;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->ip = ip;
	__add_hash_entry(hash, entry);

	return 0;
}

static void
free_hash_entry(struct ftrace_hash *hash,
		  struct ftrace_func_entry *entry)
{
	hlist_del(&entry->hlist);
	kfree(entry);
	hash->count--;
}

static void
remove_hash_entry(struct ftrace_hash *hash,
		  struct ftrace_func_entry *entry)
{
	hlist_del(&entry->hlist);
	hash->count--;
}

static void ftrace_hash_clear(struct ftrace_hash *hash)
{
	struct hlist_head *hhd;
	struct hlist_node *tn;
	struct ftrace_func_entry *entry;
	int size = 1 << hash->size_bits;
	int i;

	if (!hash->count)
		return;

	for (i = 0; i < size; i++) {
		hhd = &hash->buckets[i];
		hlist_for_each_entry_safe(entry, tn, hhd, hlist)
			free_hash_entry(hash, entry);
	}
	FTRACE_WARN_ON(hash->count);
}

static void free_ftrace_hash(struct ftrace_hash *hash)
{
	if (!hash || hash == EMPTY_HASH)
		return;
	ftrace_hash_clear(hash);
	kfree(hash->buckets);
	kfree(hash);
}

static void __free_ftrace_hash_rcu(struct rcu_head *rcu)
{
	struct ftrace_hash *hash;

	hash = container_of(rcu, struct ftrace_hash, rcu);
	free_ftrace_hash(hash);
}

static void free_ftrace_hash_rcu(struct ftrace_hash *hash)
{
	if (!hash || hash == EMPTY_HASH)
		return;
	call_rcu_sched(&hash->rcu, __free_ftrace_hash_rcu);
}

void ftrace_free_filter(struct ftrace_ops *ops)
{
	ftrace_ops_init(ops);
	free_ftrace_hash(ops->filter_hash);
	free_ftrace_hash(ops->notrace_hash);
}

static struct ftrace_hash *alloc_ftrace_hash(int size_bits)
{
	struct ftrace_hash *hash;
	int size;

	hash = kzalloc(sizeof(*hash), GFP_KERNEL);
	if (!hash)
		return NULL;

	size = 1 << size_bits;
	hash->buckets = kcalloc(size, sizeof(*hash->buckets), GFP_KERNEL);

	if (!hash->buckets) {
		kfree(hash);
		return NULL;
	}

	hash->size_bits = size_bits;

	return hash;
}

static struct ftrace_hash *
alloc_and_copy_ftrace_hash(int size_bits, struct ftrace_hash *hash)
{
	struct ftrace_func_entry *entry;
	struct ftrace_hash *new_hash;
	int size;
	int ret;
	int i;

	new_hash = alloc_ftrace_hash(size_bits);
	if (!new_hash)
		return NULL;

	/* Empty hash? */
	if (ftrace_hash_empty(hash))
		return new_hash;

	size = 1 << hash->size_bits;
	for (i = 0; i < size; i++) {
		hlist_for_each_entry(entry, &hash->buckets[i], hlist) {
			ret = add_hash_entry(new_hash, entry->ip);
			if (ret < 0)
				goto free_hash;
		}
	}

	FTRACE_WARN_ON(new_hash->count != hash->count);

	return new_hash;

 free_hash:
	free_ftrace_hash(new_hash);
	return NULL;
}

static void
ftrace_hash_rec_disable(struct ftrace_ops *ops, int filter_hash);
static void
ftrace_hash_rec_enable(struct ftrace_ops *ops, int filter_hash);

static int
ftrace_hash_move(struct ftrace_ops *ops, int enable,
		 struct ftrace_hash **dst, struct ftrace_hash *src)
{
	struct ftrace_func_entry *entry;
	struct hlist_node *tn;
	struct hlist_head *hhd;
	struct ftrace_hash *old_hash;
	struct ftrace_hash *new_hash;
	int size = src->count;
	int bits = 0;
	int ret;
	int i;

	/*
	 * Remove the current set, update the hash and add
	 * them back.
	 */
	ftrace_hash_rec_disable(ops, enable);

	/*
	 * If the new source is empty, just free dst and assign it
	 * the empty_hash.
	 */
	if (!src->count) {
		free_ftrace_hash_rcu(*dst);
		rcu_assign_pointer(*dst, EMPTY_HASH);
		/* still need to update the function records */
		ret = 0;
		goto out;
	}

	/*
	 * Make the hash size about 1/2 the # found
	 */
	for (size /= 2; size; size >>= 1)
		bits++;

	/* Don't allocate too much */
	if (bits > FTRACE_HASH_MAX_BITS)
		bits = FTRACE_HASH_MAX_BITS;

	ret = -ENOMEM;
	new_hash = alloc_ftrace_hash(bits);
	if (!new_hash)
		goto out;

	size = 1 << src->size_bits;
	for (i = 0; i < size; i++) {
		hhd = &src->buckets[i];
		hlist_for_each_entry_safe(entry, tn, hhd, hlist) {
			remove_hash_entry(src, entry);
			__add_hash_entry(new_hash, entry);
		}
	}

	old_hash = *dst;
	rcu_assign_pointer(*dst, new_hash);
	free_ftrace_hash_rcu(old_hash);

	ret = 0;
 out:
	/*
	 * Enable regardless of ret:
	 *  On success, we enable the new hash.
	 *  On failure, we re-enable the original hash.
	 */
	ftrace_hash_rec_enable(ops, enable);

	return ret;
}

/*
 * Test the hashes for this ops to see if we want to call
 * the ops->func or not.
 *
 * It's a match if the ip is in the ops->filter_hash or
 * the filter_hash does not exist or is empty,
 *  AND
 * the ip is not in the ops->notrace_hash.
 *
 * This needs to be called with preemption disabled as
 * the hashes are freed with call_rcu_sched().
 */
static int
ftrace_ops_test(struct ftrace_ops *ops, unsigned long ip, void *regs)
{
	struct ftrace_hash *filter_hash;
	struct ftrace_hash *notrace_hash;
	int ret;

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	/*
	 * There's a small race when adding ops that the ftrace handler
	 * that wants regs, may be called without them. We can not
	 * allow that handler to be called if regs is NULL.
	 */
	if (regs == NULL && (ops->flags & FTRACE_OPS_FL_SAVE_REGS))
		return 0;
#endif

	filter_hash = rcu_dereference_raw_notrace(ops->filter_hash);
	notrace_hash = rcu_dereference_raw_notrace(ops->notrace_hash);

	if ((ftrace_hash_empty(filter_hash) ||
	     ftrace_lookup_ip(filter_hash, ip)) &&
	    (ftrace_hash_empty(notrace_hash) ||
	     !ftrace_lookup_ip(notrace_hash, ip)))
		ret = 1;
	else
		ret = 0;

	return ret;
}

/*
 * This is a double for. Do not use 'break' to break out of the loop,
 * you must use a goto.
 */
#define do_for_each_ftrace_rec(pg, rec)					\
	for (pg = ftrace_pages_start; pg; pg = pg->next) {		\
		int _____i;						\
		for (_____i = 0; _____i < pg->index; _____i++) {	\
			rec = &pg->records[_____i];

#define while_for_each_ftrace_rec()		\
		}				\
	}


static int ftrace_cmp_recs(const void *a, const void *b)
{
	const struct dyn_ftrace *key = a;
	const struct dyn_ftrace *rec = b;

	if (key->flags < rec->ip)
		return -1;
	if (key->ip >= rec->ip + MCOUNT_INSN_SIZE)
		return 1;
	return 0;
}

static unsigned long ftrace_location_range(unsigned long start, unsigned long end)
{
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	struct dyn_ftrace key;

	key.ip = start;
	key.flags = end;	/* overload flags, as it is unsigned long */

	for (pg = ftrace_pages_start; pg; pg = pg->next) {
		if (end < pg->records[0].ip ||
		    start >= (pg->records[pg->index - 1].ip + MCOUNT_INSN_SIZE))
			continue;
		rec = bsearch(&key, pg->records, pg->index,
			      sizeof(struct dyn_ftrace),
			      ftrace_cmp_recs);
		if (rec)
			return rec->ip;
	}

	return 0;
}

/**
 * ftrace_location - return true if the ip giving is a traced location
 * @ip: the instruction pointer to check
 *
 * Returns rec->ip if @ip given is a pointer to a ftrace location.
 * That is, the instruction that is either a NOP or call to
 * the function tracer. It checks the ftrace internal tables to
 * determine if the address belongs or not.
 */
unsigned long ftrace_location(unsigned long ip)
{
	return ftrace_location_range(ip, ip);
}

/**
 * ftrace_text_reserved - return true if range contains an ftrace location
 * @start: start of range to search
 * @end: end of range to search (inclusive). @end points to the last byte to check.
 *
 * Returns 1 if @start and @end contains a ftrace location.
 * That is, the instruction that is either a NOP or call to
 * the function tracer. It checks the ftrace internal tables to
 * determine if the address belongs or not.
 */
int ftrace_text_reserved(void *start, void *end)
{
	unsigned long ret;

	ret = ftrace_location_range((unsigned long)start,
				    (unsigned long)end);

	return (int)!!ret;
}

static void __ftrace_hash_rec_update(struct ftrace_ops *ops,
				     int filter_hash,
				     bool inc)
{
	struct ftrace_hash *hash;
	struct ftrace_hash *other_hash;
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	int count = 0;
	int all = 0;

	/* Only update if the ops has been registered */
	if (!(ops->flags & FTRACE_OPS_FL_ENABLED))
		return;

	/*
	 * In the filter_hash case:
	 *   If the count is zero, we update all records.
	 *   Otherwise we just update the items in the hash.
	 *
	 * In the notrace_hash case:
	 *   We enable the update in the hash.
	 *   As disabling notrace means enabling the tracing,
	 *   and enabling notrace means disabling, the inc variable
	 *   gets inversed.
	 */
	if (filter_hash) {
		hash = ops->filter_hash;
		other_hash = ops->notrace_hash;
		if (ftrace_hash_empty(hash))
			all = 1;
	} else {
		inc = !inc;
		hash = ops->notrace_hash;
		other_hash = ops->filter_hash;
		/*
		 * If the notrace hash has no items,
		 * then there's nothing to do.
		 */
		if (ftrace_hash_empty(hash))
			return;
	}

	do_for_each_ftrace_rec(pg, rec) {
		int in_other_hash = 0;
		int in_hash = 0;
		int match = 0;

		if (all) {
			/*
			 * Only the filter_hash affects all records.
			 * Update if the record is not in the notrace hash.
			 */
			if (!other_hash || !ftrace_lookup_ip(other_hash, rec->ip))
				match = 1;
		} else {
			in_hash = !!ftrace_lookup_ip(hash, rec->ip);
			in_other_hash = !!ftrace_lookup_ip(other_hash, rec->ip);

			/*
			 *
			 */
			if (filter_hash && in_hash && !in_other_hash)
				match = 1;
			else if (!filter_hash && in_hash &&
				 (in_other_hash || ftrace_hash_empty(other_hash)))
				match = 1;
		}
		if (!match)
			continue;

		if (inc) {
			rec->flags++;
			if (FTRACE_WARN_ON((rec->flags & ~FTRACE_FL_MASK) == FTRACE_REF_MAX))
				return;
			/*
			 * If any ops wants regs saved for this function
			 * then all ops will get saved regs.
			 */
			if (ops->flags & FTRACE_OPS_FL_SAVE_REGS)
				rec->flags |= FTRACE_FL_REGS;
		} else {
			if (FTRACE_WARN_ON((rec->flags & ~FTRACE_FL_MASK) == 0))
				return;
			rec->flags--;
		}
		count++;
		/* Shortcut, if we handled all records, we are done. */
		if (!all && count == hash->count)
			return;
	} while_for_each_ftrace_rec();
}

static void ftrace_hash_rec_disable(struct ftrace_ops *ops,
				    int filter_hash)
{
	__ftrace_hash_rec_update(ops, filter_hash, 0);
}

static void ftrace_hash_rec_enable(struct ftrace_ops *ops,
				   int filter_hash)
{
	__ftrace_hash_rec_update(ops, filter_hash, 1);
}

static void print_ip_ins(const char *fmt, unsigned char *p)
{
	int i;

	printk(KERN_CONT "%s", fmt);

	for (i = 0; i < MCOUNT_INSN_SIZE; i++)
		printk(KERN_CONT "%s%02x", i ? ":" : "", p[i]);
}

/**
 * ftrace_bug - report and shutdown function tracer
 * @failed: The failed type (EFAULT, EINVAL, EPERM)
 * @ip: The address that failed
 *
 * The arch code that enables or disables the function tracing
 * can call ftrace_bug() when it has detected a problem in
 * modifying the code. @failed should be one of either:
 * EFAULT - if the problem happens on reading the @ip address
 * EINVAL - if what is read at @ip is not what was expected
 * EPERM - if the problem happens on writting to the @ip address
 */
void ftrace_bug(int failed, unsigned long ip)
{
	switch (failed) {
	case -EFAULT:
		FTRACE_WARN_ON_ONCE(1);
		pr_info("ftrace faulted on modifying ");
		print_ip_sym(ip);
		break;
	case -EINVAL:
		FTRACE_WARN_ON_ONCE(1);
		pr_info("ftrace failed to modify ");
		print_ip_sym(ip);
		print_ip_ins(" actual: ", (unsigned char *)ip);
		printk(KERN_CONT "\n");
		break;
	case -EPERM:
		FTRACE_WARN_ON_ONCE(1);
		pr_info("ftrace faulted on writing ");
		print_ip_sym(ip);
		break;
	default:
		FTRACE_WARN_ON_ONCE(1);
		pr_info("ftrace faulted on unknown error ");
		print_ip_sym(ip);
	}
}

static int ftrace_check_record(struct dyn_ftrace *rec, int enable, int update)
{
	unsigned long flag = 0UL;

	/*
	 * If we are updating calls:
	 *
	 *   If the record has a ref count, then we need to enable it
	 *   because someone is using it.
	 *
	 *   Otherwise we make sure its disabled.
	 *
	 * If we are disabling calls, then disable all records that
	 * are enabled.
	 */
	if (enable && (rec->flags & ~FTRACE_FL_MASK))
		flag = FTRACE_FL_ENABLED;

	/*
	 * If enabling and the REGS flag does not match the REGS_EN, then
	 * do not ignore this record. Set flags to fail the compare against
	 * ENABLED.
	 */
	if (flag &&
	    (!(rec->flags & FTRACE_FL_REGS) != !(rec->flags & FTRACE_FL_REGS_EN)))
		flag |= FTRACE_FL_REGS;

	/* If the state of this record hasn't changed, then do nothing */
	if ((rec->flags & FTRACE_FL_ENABLED) == flag)
		return FTRACE_UPDATE_IGNORE;

	if (flag) {
		/* Save off if rec is being enabled (for return value) */
		flag ^= rec->flags & FTRACE_FL_ENABLED;

		if (update) {
			rec->flags |= FTRACE_FL_ENABLED;
			if (flag & FTRACE_FL_REGS) {
				if (rec->flags & FTRACE_FL_REGS)
					rec->flags |= FTRACE_FL_REGS_EN;
				else
					rec->flags &= ~FTRACE_FL_REGS_EN;
			}
		}

		/*
		 * If this record is being updated from a nop, then
		 *   return UPDATE_MAKE_CALL.
		 * Otherwise, if the EN flag is set, then return
		 *   UPDATE_MODIFY_CALL_REGS to tell the caller to convert
		 *   from the non-save regs, to a save regs function.
		 * Otherwise,
		 *   return UPDATE_MODIFY_CALL to tell the caller to convert
		 *   from the save regs, to a non-save regs function.
		 */
		if (flag & FTRACE_FL_ENABLED)
			return FTRACE_UPDATE_MAKE_CALL;
		else if (rec->flags & FTRACE_FL_REGS_EN)
			return FTRACE_UPDATE_MODIFY_CALL_REGS;
		else
			return FTRACE_UPDATE_MODIFY_CALL;
	}

	if (update) {
		/* If there's no more users, clear all flags */
		if (!(rec->flags & ~FTRACE_FL_MASK))
			rec->flags = 0;
		else
			/* Just disable the record (keep REGS state) */
			rec->flags &= ~FTRACE_FL_ENABLED;
	}

	return FTRACE_UPDATE_MAKE_NOP;
}

/**
 * ftrace_update_record, set a record that now is tracing or not
 * @rec: the record to update
 * @enable: set to 1 if the record is tracing, zero to force disable
 *
 * The records that represent all functions that can be traced need
 * to be updated when tracing has been enabled.
 */
int ftrace_update_record(struct dyn_ftrace *rec, int enable)
{
	return ftrace_check_record(rec, enable, 1);
}

/**
 * ftrace_test_record, check if the record has been enabled or not
 * @rec: the record to test
 * @enable: set to 1 to check if enabled, 0 if it is disabled
 *
 * The arch code may need to test if a record is already set to
 * tracing to determine how to modify the function code that it
 * represents.
 */
int ftrace_test_record(struct dyn_ftrace *rec, int enable)
{
	return ftrace_check_record(rec, enable, 0);
}

static int
__ftrace_replace_code(struct dyn_ftrace *rec, int enable)
{
	unsigned long ftrace_old_addr;
	unsigned long ftrace_addr;
	int ret;

	ret = ftrace_update_record(rec, enable);

	if (rec->flags & FTRACE_FL_REGS)
		ftrace_addr = (unsigned long)FTRACE_REGS_ADDR;
	else
		ftrace_addr = (unsigned long)FTRACE_ADDR;

	switch (ret) {
	case FTRACE_UPDATE_IGNORE:
		return 0;

	case FTRACE_UPDATE_MAKE_CALL:
		return ftrace_make_call(rec, ftrace_addr);

	case FTRACE_UPDATE_MAKE_NOP:
		return ftrace_make_nop(NULL, rec, ftrace_addr);

	case FTRACE_UPDATE_MODIFY_CALL_REGS:
	case FTRACE_UPDATE_MODIFY_CALL:
		if (rec->flags & FTRACE_FL_REGS)
			ftrace_old_addr = (unsigned long)FTRACE_ADDR;
		else
			ftrace_old_addr = (unsigned long)FTRACE_REGS_ADDR;

		return ftrace_modify_call(rec, ftrace_old_addr, ftrace_addr);
	}

	return -1; /* unknow ftrace bug */
}

void __weak ftrace_replace_code(int enable)
{
	struct dyn_ftrace *rec;
	struct ftrace_page *pg;
	int failed;

	if (unlikely(ftrace_disabled))
		return;

	do_for_each_ftrace_rec(pg, rec) {
		failed = __ftrace_replace_code(rec, enable);
		if (failed) {
			ftrace_bug(failed, rec->ip);
			/* Stop processing */
			return;
		}
	} while_for_each_ftrace_rec();
}

struct ftrace_rec_iter {
	struct ftrace_page	*pg;
	int			index;
};

/**
 * ftrace_rec_iter_start, start up iterating over traced functions
 *
 * Returns an iterator handle that is used to iterate over all
 * the records that represent address locations where functions
 * are traced.
 *
 * May return NULL if no records are available.
 */
struct ftrace_rec_iter *ftrace_rec_iter_start(void)
{
	/*
	 * We only use a single iterator.
	 * Protected by the ftrace_lock mutex.
	 */
	static struct ftrace_rec_iter ftrace_rec_iter;
	struct ftrace_rec_iter *iter = &ftrace_rec_iter;

	iter->pg = ftrace_pages_start;
	iter->index = 0;

	/* Could have empty pages */
	while (iter->pg && !iter->pg->index)
		iter->pg = iter->pg->next;

	if (!iter->pg)
		return NULL;

	return iter;
}

/**
 * ftrace_rec_iter_next, get the next record to process.
 * @iter: The handle to the iterator.
 *
 * Returns the next iterator after the given iterator @iter.
 */
struct ftrace_rec_iter *ftrace_rec_iter_next(struct ftrace_rec_iter *iter)
{
	iter->index++;

	if (iter->index >= iter->pg->index) {
		iter->pg = iter->pg->next;
		iter->index = 0;

		/* Could have empty pages */
		while (iter->pg && !iter->pg->index)
			iter->pg = iter->pg->next;
	}

	if (!iter->pg)
		return NULL;

	return iter;
}

/**
 * ftrace_rec_iter_record, get the record at the iterator location
 * @iter: The current iterator location
 *
 * Returns the record that the current @iter is at.
 */
struct dyn_ftrace *ftrace_rec_iter_record(struct ftrace_rec_iter *iter)
{
	return &iter->pg->records[iter->index];
}

static int
ftrace_code_disable(struct module *mod, struct dyn_ftrace *rec)
{
	unsigned long ip;
	int ret;

	ip = rec->ip;

	if (unlikely(ftrace_disabled))
		return 0;

	ret = ftrace_make_nop(mod, rec, MCOUNT_ADDR);
	if (ret) {
		ftrace_bug(ret, ip);
		return 0;
	}
	return 1;
}

/*
 * archs can override this function if they must do something
 * before the modifying code is performed.
 */
int __weak ftrace_arch_code_modify_prepare(void)
{
	return 0;
}

/*
 * archs can override this function if they must do something
 * after the modifying code is performed.
 */
int __weak ftrace_arch_code_modify_post_process(void)
{
	return 0;
}

void ftrace_modify_all_code(int command)
{
	int update = command & FTRACE_UPDATE_TRACE_FUNC;

	/*
	 * If the ftrace_caller calls a ftrace_ops func directly,
	 * we need to make sure that it only traces functions it
	 * expects to trace. When doing the switch of functions,
	 * we need to update to the ftrace_ops_list_func first
	 * before the transition between old and new calls are set,
	 * as the ftrace_ops_list_func will check the ops hashes
	 * to make sure the ops are having the right functions
	 * traced.
	 */
	if (update)
		ftrace_update_ftrace_func(ftrace_ops_list_func);

	if (command & FTRACE_UPDATE_CALLS)
		ftrace_replace_code(1);
	else if (command & FTRACE_DISABLE_CALLS)
		ftrace_replace_code(0);

	if (update && ftrace_trace_function != ftrace_ops_list_func) {
		function_trace_op = set_function_trace_op;
		smp_wmb();
		/* If irqs are disabled, we are in stop machine */
		if (!irqs_disabled())
			smp_call_function(ftrace_sync_ipi, NULL, 1);
		ftrace_update_ftrace_func(ftrace_trace_function);
	}

	if (command & FTRACE_START_FUNC_RET)
		ftrace_enable_ftrace_graph_caller();
	else if (command & FTRACE_STOP_FUNC_RET)
		ftrace_disable_ftrace_graph_caller();
}

static int __ftrace_modify_code(void *data)
{
	int *command = data;

	ftrace_modify_all_code(*command);

	return 0;
}

/**
 * ftrace_run_stop_machine, go back to the stop machine method
 * @command: The command to tell ftrace what to do
 *
 * If an arch needs to fall back to the stop machine method, the
 * it can call this function.
 */
void ftrace_run_stop_machine(int command)
{
	stop_machine(__ftrace_modify_code, &command, NULL);
}

/**
 * arch_ftrace_update_code, modify the code to trace or not trace
 * @command: The command that needs to be done
 *
 * Archs can override this function if it does not need to
 * run stop_machine() to modify code.
 */
void __weak arch_ftrace_update_code(int command)
{
	ftrace_run_stop_machine(command);
}

static void ftrace_run_update_code(int command)
{
	int ret;

	ret = ftrace_arch_code_modify_prepare();
	FTRACE_WARN_ON(ret);
	if (ret)
		return;
	/*
	 * Do not call function tracer while we update the code.
	 * We are in stop machine.
	 */
	function_trace_stop++;

	/*
	 * By default we use stop_machine() to modify the code.
	 * But archs can do what ever they want as long as it
	 * is safe. The stop_machine() is the safest, but also
	 * produces the most overhead.
	 */
	arch_ftrace_update_code(command);

	function_trace_stop--;

	ret = ftrace_arch_code_modify_post_process();
	FTRACE_WARN_ON(ret);
}

static ftrace_func_t saved_ftrace_func;
static int ftrace_start_up;
static int global_start_up;

static void ftrace_startup_enable(int command)
{
	if (saved_ftrace_func != ftrace_trace_function) {
		saved_ftrace_func = ftrace_trace_function;
		command |= FTRACE_UPDATE_TRACE_FUNC;
	}

	if (!command || !ftrace_enabled)
		return;

	ftrace_run_update_code(command);
}

static int ftrace_startup(struct ftrace_ops *ops, int command)
{
	bool hash_enable = true;
	int ret;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	ret = __register_ftrace_function(ops);
	if (ret)
		return ret;

	ftrace_start_up++;
	command |= FTRACE_UPDATE_CALLS;

	/* ops marked global share the filter hashes */
	if (ops->flags & FTRACE_OPS_FL_GLOBAL) {
		ops = &global_ops;
		/* Don't update hash if global is already set */
		if (global_start_up)
			hash_enable = false;
		global_start_up++;
	}

	ops->flags |= FTRACE_OPS_FL_ENABLED;
	if (hash_enable)
		ftrace_hash_rec_enable(ops, 1);

	ftrace_startup_enable(command);

	return 0;
}

static int ftrace_shutdown(struct ftrace_ops *ops, int command)
{
	bool hash_disable = true;
	int ret;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	ret = __unregister_ftrace_function(ops);
	if (ret)
		return ret;

	ftrace_start_up--;
	/*
	 * Just warn in case of unbalance, no need to kill ftrace, it's not
	 * critical but the ftrace_call callers may be never nopped again after
	 * further ftrace uses.
	 */
	WARN_ON_ONCE(ftrace_start_up < 0);

	if (ops->flags & FTRACE_OPS_FL_GLOBAL) {
		ops = &global_ops;
		global_start_up--;
		WARN_ON_ONCE(global_start_up < 0);
		/* Don't update hash if global still has users */
		if (global_start_up) {
			WARN_ON_ONCE(!ftrace_start_up);
			hash_disable = false;
		}
	}

	if (hash_disable)
		ftrace_hash_rec_disable(ops, 1);

	if (ops != &global_ops || !global_start_up)
		ops->flags &= ~FTRACE_OPS_FL_ENABLED;

	command |= FTRACE_UPDATE_CALLS;

	if (saved_ftrace_func != ftrace_trace_function) {
		saved_ftrace_func = ftrace_trace_function;
		command |= FTRACE_UPDATE_TRACE_FUNC;
	}

	if (!command || !ftrace_enabled) {
		/*
		 * If these are control ops, they still need their
		 * per_cpu field freed. Since, function tracing is
		 * not currently active, we can just free them
		 * without synchronizing all CPUs.
		 */
		if (ops->flags & FTRACE_OPS_FL_CONTROL)
			control_ops_free(ops);
		return 0;
	}

	ftrace_run_update_code(command);

	/*
	 * Dynamic ops may be freed, we must make sure that all
	 * callers are done before leaving this function.
	 * The same goes for freeing the per_cpu data of the control
	 * ops.
	 *
	 * Again, normal synchronize_sched() is not good enough.
	 * We need to do a hard force of sched synchronization.
	 * This is because we use preempt_disable() to do RCU, but
	 * the function tracers can be called where RCU is not watching
	 * (like before user_exit()). We can not rely on the RCU
	 * infrastructure to do the synchronization, thus we must do it
	 * ourselves.
	 */
	if (ops->flags & (FTRACE_OPS_FL_DYNAMIC | FTRACE_OPS_FL_CONTROL)) {
		schedule_on_each_cpu(ftrace_sync);

		if (ops->flags & FTRACE_OPS_FL_CONTROL)
			control_ops_free(ops);
	}

	return 0;
}

static void ftrace_startup_sysctl(void)
{
	if (unlikely(ftrace_disabled))
		return;

	/* Force update next time */
	saved_ftrace_func = NULL;
	/* ftrace_start_up is true if we want ftrace running */
	if (ftrace_start_up)
		ftrace_run_update_code(FTRACE_UPDATE_CALLS);
}

static void ftrace_shutdown_sysctl(void)
{
	if (unlikely(ftrace_disabled))
		return;

	/* ftrace_start_up is true if ftrace is running */
	if (ftrace_start_up)
		ftrace_run_update_code(FTRACE_DISABLE_CALLS);
}

static cycle_t		ftrace_update_time;
static unsigned long	ftrace_update_cnt;
unsigned long		ftrace_update_tot_cnt;

static inline int ops_traces_mod(struct ftrace_ops *ops)
{
	/*
	 * Filter_hash being empty will default to trace module.
	 * But notrace hash requires a test of individual module functions.
	 */
	return ftrace_hash_empty(ops->filter_hash) &&
		ftrace_hash_empty(ops->notrace_hash);
}

/*
 * Check if the current ops references the record.
 *
 * If the ops traces all functions, then it was already accounted for.
 * If the ops does not trace the current record function, skip it.
 * If the ops ignores the function via notrace filter, skip it.
 */
static inline bool
ops_references_rec(struct ftrace_ops *ops, struct dyn_ftrace *rec)
{
	/* If ops isn't enabled, ignore it */
	if (!(ops->flags & FTRACE_OPS_FL_ENABLED))
		return 0;

	/* If ops traces all mods, we already accounted for it */
	if (ops_traces_mod(ops))
		return 0;

	/* The function must be in the filter */
	if (!ftrace_hash_empty(ops->filter_hash) &&
	    !ftrace_lookup_ip(ops->filter_hash, rec->ip))
		return 0;

	/* If in notrace hash, we ignore it too */
	if (ftrace_lookup_ip(ops->notrace_hash, rec->ip))
		return 0;

	return 1;
}

static int referenced_filters(struct dyn_ftrace *rec)
{
	struct ftrace_ops *ops;
	int cnt = 0;

	for (ops = ftrace_ops_list; ops != &ftrace_list_end; ops = ops->next) {
		if (ops_references_rec(ops, rec))
		    cnt++;
	}

	return cnt;
}

static int ftrace_update_code(struct module *mod)
{
	struct ftrace_page *pg;
	struct dyn_ftrace *p;
	cycle_t start, stop;
	unsigned long ref = 0;
	bool test = false;
	int i;

	/*
	 * When adding a module, we need to check if tracers are
	 * currently enabled and if they are set to trace all functions.
	 * If they are, we need to enable the module functions as well
	 * as update the reference counts for those function records.
	 */
	if (mod) {
		struct ftrace_ops *ops;

		for (ops = ftrace_ops_list;
		     ops != &ftrace_list_end; ops = ops->next) {
			if (ops->flags & FTRACE_OPS_FL_ENABLED) {
				if (ops_traces_mod(ops))
					ref++;
				else
					test = true;
			}
		}
	}

	start = ftrace_now(raw_smp_processor_id());
	ftrace_update_cnt = 0;

	for (pg = ftrace_new_pgs; pg; pg = pg->next) {

		for (i = 0; i < pg->index; i++) {
			int cnt = ref;

			/* If something went wrong, bail without enabling anything */
			if (unlikely(ftrace_disabled))
				return -1;

			p = &pg->records[i];
			if (test)
				cnt += referenced_filters(p);
			p->flags = cnt;

			/*
			 * Do the initial record conversion from mcount jump
			 * to the NOP instructions.
			 */
			if (!ftrace_code_disable(mod, p))
				break;

			ftrace_update_cnt++;

			/*
			 * If the tracing is enabled, go ahead and enable the record.
			 *
			 * The reason not to enable the record immediatelly is the
			 * inherent check of ftrace_make_nop/ftrace_make_call for
			 * correct previous instructions.  Making first the NOP
			 * conversion puts the module to the correct state, thus
			 * passing the ftrace_make_call check.
			 */
			if (ftrace_start_up && cnt) {
				int failed = __ftrace_replace_code(p, 1);
				if (failed)
					ftrace_bug(failed, p->ip);
			}
		}
	}

	ftrace_new_pgs = NULL;

	stop = ftrace_now(raw_smp_processor_id());
	ftrace_update_time = stop - start;
	ftrace_update_tot_cnt += ftrace_update_cnt;

	return 0;
}

static int ftrace_allocate_records(struct ftrace_page *pg, int count)
{
	int order;
	int cnt;

	if (WARN_ON(!count))
		return -EINVAL;

	order = get_count_order(DIV_ROUND_UP(count, ENTRIES_PER_PAGE));

	/*
	 * We want to fill as much as possible. No more than a page
	 * may be empty.
	 */
	while ((PAGE_SIZE << order) / ENTRY_SIZE >= count + ENTRIES_PER_PAGE)
		order--;

 again:
	pg->records = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, order);

	if (!pg->records) {
		/* if we can't allocate this size, try something smaller */
		if (!order)
			return -ENOMEM;
		order >>= 1;
		goto again;
	}

	cnt = (PAGE_SIZE << order) / ENTRY_SIZE;
	pg->size = cnt;

	if (cnt > count)
		cnt = count;

	return cnt;
}

static struct ftrace_page *
ftrace_allocate_pages(unsigned long num_to_init)
{
	struct ftrace_page *start_pg;
	struct ftrace_page *pg;
	int order;
	int cnt;

	if (!num_to_init)
		return 0;

	start_pg = pg = kzalloc(sizeof(*pg), GFP_KERNEL);
	if (!pg)
		return NULL;

	/*
	 * Try to allocate as much as possible in one continues
	 * location that fills in all of the space. We want to
	 * waste as little space as possible.
	 */
	for (;;) {
		cnt = ftrace_allocate_records(pg, num_to_init);
		if (cnt < 0)
			goto free_pages;

		num_to_init -= cnt;
		if (!num_to_init)
			break;

		pg->next = kzalloc(sizeof(*pg), GFP_KERNEL);
		if (!pg->next)
			goto free_pages;

		pg = pg->next;
	}

	return start_pg;

 free_pages:
	while (start_pg) {
		order = get_count_order(pg->size / ENTRIES_PER_PAGE);
		free_pages((unsigned long)pg->records, order);
		start_pg = pg->next;
		kfree(pg);
		pg = start_pg;
	}
	pr_info("ftrace: FAILED to allocate memory for functions\n");
	return NULL;
}

static int __init ftrace_dyn_table_alloc(unsigned long num_to_init)
{
	int cnt;

	if (!num_to_init) {
		pr_info("ftrace: No functions to be traced?\n");
		return -1;
	}

	cnt = num_to_init / ENTRIES_PER_PAGE;
	pr_info("ftrace: allocating %ld entries in %d pages\n",
		num_to_init, cnt + 1);

	return 0;
}

#define FTRACE_BUFF_MAX (KSYM_SYMBOL_LEN+4) /* room for wildcards */

struct ftrace_iterator {
	loff_t				pos;
	loff_t				func_pos;
	struct ftrace_page		*pg;
	struct dyn_ftrace		*func;
	struct ftrace_func_probe	*probe;
	struct trace_parser		parser;
	struct ftrace_hash		*hash;
	struct ftrace_ops		*ops;
	int				hidx;
	int				idx;
	unsigned			flags;
};

static void *
t_hash_next(struct seq_file *m, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	struct hlist_node *hnd = NULL;
	struct hlist_head *hhd;

	(*pos)++;
	iter->pos = *pos;

	if (iter->probe)
		hnd = &iter->probe->node;
 retry:
	if (iter->hidx >= FTRACE_FUNC_HASHSIZE)
		return NULL;

	hhd = &ftrace_func_hash[iter->hidx];

	if (hlist_empty(hhd)) {
		iter->hidx++;
		hnd = NULL;
		goto retry;
	}

	if (!hnd)
		hnd = hhd->first;
	else {
		hnd = hnd->next;
		if (!hnd) {
			iter->hidx++;
			goto retry;
		}
	}

	if (WARN_ON_ONCE(!hnd))
		return NULL;

	iter->probe = hlist_entry(hnd, struct ftrace_func_probe, node);

	return iter;
}

static void *t_hash_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	void *p = NULL;
	loff_t l;

	if (!(iter->flags & FTRACE_ITER_DO_HASH))
		return NULL;

	if (iter->func_pos > *pos)
		return NULL;

	iter->hidx = 0;
	for (l = 0; l <= (*pos - iter->func_pos); ) {
		p = t_hash_next(m, &l);
		if (!p)
			break;
	}
	if (!p)
		return NULL;

	/* Only set this if we have an item */
	iter->flags |= FTRACE_ITER_HASH;

	return iter;
}

static int
t_hash_show(struct seq_file *m, struct ftrace_iterator *iter)
{
	struct ftrace_func_probe *rec;

	rec = iter->probe;
	if (WARN_ON_ONCE(!rec))
		return -EIO;

	if (rec->ops->print)
		return rec->ops->print(m, rec->ip, rec->ops, rec->data);

	seq_printf(m, "%ps:%ps", (void *)rec->ip, (void *)rec->ops->func);

	if (rec->data)
		seq_printf(m, ":%p", rec->data);
	seq_putc(m, '\n');

	return 0;
}

static void *
t_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	struct ftrace_ops *ops = iter->ops;
	struct dyn_ftrace *rec = NULL;

	if (unlikely(ftrace_disabled))
		return NULL;

	if (iter->flags & FTRACE_ITER_HASH)
		return t_hash_next(m, pos);

	(*pos)++;
	iter->pos = iter->func_pos = *pos;

	if (iter->flags & FTRACE_ITER_PRINTALL)
		return t_hash_start(m, pos);

 retry:
	if (iter->idx >= iter->pg->index) {
		if (iter->pg->next) {
			iter->pg = iter->pg->next;
			iter->idx = 0;
			goto retry;
		}
	} else {
		rec = &iter->pg->records[iter->idx++];
		if (((iter->flags & FTRACE_ITER_FILTER) &&
		     !(ftrace_lookup_ip(ops->filter_hash, rec->ip))) ||

		    ((iter->flags & FTRACE_ITER_NOTRACE) &&
		     !ftrace_lookup_ip(ops->notrace_hash, rec->ip)) ||

		    ((iter->flags & FTRACE_ITER_ENABLED) &&
		     !(rec->flags & FTRACE_FL_ENABLED))) {

			rec = NULL;
			goto retry;
		}
	}

	if (!rec)
		return t_hash_start(m, pos);

	iter->func = rec;

	return iter;
}

static void reset_iter_read(struct ftrace_iterator *iter)
{
	iter->pos = 0;
	iter->func_pos = 0;
	iter->flags &= ~(FTRACE_ITER_PRINTALL | FTRACE_ITER_HASH);
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	struct ftrace_ops *ops = iter->ops;
	void *p = NULL;
	loff_t l;

	mutex_lock(&ftrace_lock);

	if (unlikely(ftrace_disabled))
		return NULL;

	/*
	 * If an lseek was done, then reset and start from beginning.
	 */
	if (*pos < iter->pos)
		reset_iter_read(iter);

	/*
	 * For set_ftrace_filter reading, if we have the filter
	 * off, we can short cut and just print out that all
	 * functions are enabled.
	 */
	if (iter->flags & FTRACE_ITER_FILTER &&
	    ftrace_hash_empty(ops->filter_hash)) {
		if (*pos > 0)
			return t_hash_start(m, pos);
		iter->flags |= FTRACE_ITER_PRINTALL;
		/* reset in case of seek/pread */
		iter->flags &= ~FTRACE_ITER_HASH;
		return iter;
	}

	if (iter->flags & FTRACE_ITER_HASH)
		return t_hash_start(m, pos);

	/*
	 * Unfortunately, we need to restart at ftrace_pages_start
	 * every time we let go of the ftrace_mutex. This is because
	 * those pointers can change without the lock.
	 */
	iter->pg = ftrace_pages_start;
	iter->idx = 0;
	for (l = 0; l <= *pos; ) {
		p = t_next(m, p, &l);
		if (!p)
			break;
	}

	if (!p)
		return t_hash_start(m, pos);

	return iter;
}

static void t_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&ftrace_lock);
}

static int t_show(struct seq_file *m, void *v)
{
	struct ftrace_iterator *iter = m->private;
	struct dyn_ftrace *rec;

	if (iter->flags & FTRACE_ITER_HASH)
		return t_hash_show(m, iter);

	if (iter->flags & FTRACE_ITER_PRINTALL) {
		seq_printf(m, "#### all functions enabled ####\n");
		return 0;
	}

	rec = iter->func;

	if (!rec)
		return 0;

	seq_printf(m, "%ps", (void *)rec->ip);
	if (iter->flags & FTRACE_ITER_ENABLED)
		seq_printf(m, " (%ld)%s",
			   rec->flags & ~FTRACE_FL_MASK,
			   rec->flags & FTRACE_FL_REGS ? " R" : "");
	seq_printf(m, "\n");

	return 0;
}

static const struct seq_operations show_ftrace_seq_ops = {
	.start = t_start,
	.next = t_next,
	.stop = t_stop,
	.show = t_show,
};

static int
ftrace_avail_open(struct inode *inode, struct file *file)
{
	struct ftrace_iterator *iter;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	iter = __seq_open_private(file, &show_ftrace_seq_ops, sizeof(*iter));
	if (iter) {
		iter->pg = ftrace_pages_start;
		iter->ops = &global_ops;
	}

	return iter ? 0 : -ENOMEM;
}

static int
ftrace_enabled_open(struct inode *inode, struct file *file)
{
	struct ftrace_iterator *iter;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	iter = __seq_open_private(file, &show_ftrace_seq_ops, sizeof(*iter));
	if (iter) {
		iter->pg = ftrace_pages_start;
		iter->flags = FTRACE_ITER_ENABLED;
		iter->ops = &global_ops;
	}

	return iter ? 0 : -ENOMEM;
}

static void ftrace_filter_reset(struct ftrace_hash *hash)
{
	mutex_lock(&ftrace_lock);
	ftrace_hash_clear(hash);
	mutex_unlock(&ftrace_lock);
}

/**
 * ftrace_regex_open - initialize function tracer filter files
 * @ops: The ftrace_ops that hold the hash filters
 * @flag: The type of filter to process
 * @inode: The inode, usually passed in to your open routine
 * @file: The file, usually passed in to your open routine
 *
 * ftrace_regex_open() initializes the filter files for the
 * @ops. Depending on @flag it may process the filter hash or
 * the notrace hash of @ops. With this called from the open
 * routine, you can use ftrace_filter_write() for the write
 * routine if @flag has FTRACE_ITER_FILTER set, or
 * ftrace_notrace_write() if @flag has FTRACE_ITER_NOTRACE set.
 * ftrace_filter_lseek() should be used as the lseek routine, and
 * release must call ftrace_regex_release().
 */
int
ftrace_regex_open(struct ftrace_ops *ops, int flag,
		  struct inode *inode, struct file *file)
{
	struct ftrace_iterator *iter;
	struct ftrace_hash *hash;
	int ret = 0;

	ftrace_ops_init(ops);

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	if (trace_parser_get_init(&iter->parser, FTRACE_BUFF_MAX)) {
		kfree(iter);
		return -ENOMEM;
	}

	iter->ops = ops;
	iter->flags = flag;

	mutex_lock(&ops->regex_lock);

	if (flag & FTRACE_ITER_NOTRACE)
		hash = ops->notrace_hash;
	else
		hash = ops->filter_hash;

	if (file->f_mode & FMODE_WRITE) {
		iter->hash = alloc_and_copy_ftrace_hash(FTRACE_HASH_DEFAULT_BITS, hash);
		if (!iter->hash) {
			trace_parser_put(&iter->parser);
			kfree(iter);
			ret = -ENOMEM;
			goto out_unlock;
		}
	}

	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC))
		ftrace_filter_reset(iter->hash);

	if (file->f_mode & FMODE_READ) {
		iter->pg = ftrace_pages_start;

		ret = seq_open(file, &show_ftrace_seq_ops);
		if (!ret) {
			struct seq_file *m = file->private_data;
			m->private = iter;
		} else {
			/* Failed */
			free_ftrace_hash(iter->hash);
			trace_parser_put(&iter->parser);
			kfree(iter);
		}
	} else
		file->private_data = iter;

 out_unlock:
	mutex_unlock(&ops->regex_lock);

	return ret;
}

static int
ftrace_filter_open(struct inode *inode, struct file *file)
{
	return ftrace_regex_open(&global_ops,
			FTRACE_ITER_FILTER | FTRACE_ITER_DO_HASH,
			inode, file);
}

static int
ftrace_notrace_open(struct inode *inode, struct file *file)
{
	return ftrace_regex_open(&global_ops, FTRACE_ITER_NOTRACE,
				 inode, file);
}

static int ftrace_match(char *str, char *regex, int len, int type)
{
	int matched = 0;
	int slen;

	switch (type) {
	case MATCH_FULL:
		if (strcmp(str, regex) == 0)
			matched = 1;
		break;
	case MATCH_FRONT_ONLY:
		if (strncmp(str, regex, len) == 0)
			matched = 1;
		break;
	case MATCH_MIDDLE_ONLY:
		if (strstr(str, regex))
			matched = 1;
		break;
	case MATCH_END_ONLY:
		slen = strlen(str);
		if (slen >= len && memcmp(str + slen - len, regex, len) == 0)
			matched = 1;
		break;
	}

	return matched;
}

static int
enter_record(struct ftrace_hash *hash, struct dyn_ftrace *rec, int not)
{
	struct ftrace_func_entry *entry;
	int ret = 0;

	entry = ftrace_lookup_ip(hash, rec->ip);
	if (not) {
		/* Do nothing if it doesn't exist */
		if (!entry)
			return 0;

		free_hash_entry(hash, entry);
	} else {
		/* Do nothing if it exists */
		if (entry)
			return 0;

		ret = add_hash_entry(hash, rec->ip);
	}
	return ret;
}

static int
ftrace_match_record(struct dyn_ftrace *rec, char *mod,
		    char *regex, int len, int type)
{
	char str[KSYM_SYMBOL_LEN];
	char *modname;

	kallsyms_lookup(rec->ip, NULL, NULL, &modname, str);

	if (mod) {
		/* module lookup requires matching the module */
		if (!modname || strcmp(modname, mod))
			return 0;

		/* blank search means to match all funcs in the mod */
		if (!len)
			return 1;
	}

	return ftrace_match(str, regex, len, type);
}

static int
match_records(struct ftrace_hash *hash, char *buff,
	      int len, char *mod, int not)
{
	unsigned search_len = 0;
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	int type = MATCH_FULL;
	char *search = buff;
	int found = 0;
	int ret;

	if (len) {
		type = filter_parse_regex(buff, len, &search, &not);
		search_len = strlen(search);
	}

	mutex_lock(&ftrace_lock);

	if (unlikely(ftrace_disabled))
		goto out_unlock;

	do_for_each_ftrace_rec(pg, rec) {
		if (ftrace_match_record(rec, mod, search, search_len, type)) {
			ret = enter_record(hash, rec, not);
			if (ret < 0) {
				found = ret;
				goto out_unlock;
			}
			found = 1;
		}
	} while_for_each_ftrace_rec();
 out_unlock:
	mutex_unlock(&ftrace_lock);

	return found;
}

static int
ftrace_match_records(struct ftrace_hash *hash, char *buff, int len)
{
	return match_records(hash, buff, len, NULL, 0);
}

static int
ftrace_match_module_records(struct ftrace_hash *hash, char *buff, char *mod)
{
	int not = 0;

	/* blank or '*' mean the same */
	if (strcmp(buff, "*") == 0)
		buff[0] = 0;

	/* handle the case of 'dont filter this module' */
	if (strcmp(buff, "!") == 0 || strcmp(buff, "!*") == 0) {
		buff[0] = 0;
		not = 1;
	}

	return match_records(hash, buff, strlen(buff), mod, not);
}

/*
 * We register the module command as a template to show others how
 * to register the a command as well.
 */

static int
ftrace_mod_callback(struct ftrace_hash *hash,
		    char *func, char *cmd, char *param, int enable)
{
	char *mod;
	int ret = -EINVAL;

	/*
	 * cmd == 'mod' because we only registered this func
	 * for the 'mod' ftrace_func_command.
	 * But if you register one func with multiple commands,
	 * you can tell which command was used by the cmd
	 * parameter.
	 */

	/* we must have a module name */
	if (!param)
		return ret;

	mod = strsep(&param, ":");
	if (!strlen(mod))
		return ret;

	ret = ftrace_match_module_records(hash, func, mod);
	if (!ret)
		ret = -EINVAL;
	if (ret < 0)
		return ret;

	return 0;
}

static struct ftrace_func_command ftrace_mod_cmd = {
	.name			= "mod",
	.func			= ftrace_mod_callback,
};

static int __init ftrace_mod_cmd_init(void)
{
	return register_ftrace_command(&ftrace_mod_cmd);
}
core_initcall(ftrace_mod_cmd_init);

static void function_trace_probe_call(unsigned long ip, unsigned long parent_ip,
				      struct ftrace_ops *op, struct pt_regs *pt_regs)
{
	struct ftrace_func_probe *entry;
	struct hlist_head *hhd;
	unsigned long key;

	key = hash_long(ip, FTRACE_HASH_BITS);

	hhd = &ftrace_func_hash[key];

	if (hlist_empty(hhd))
		return;

	/*
	 * Disable preemption for these calls to prevent a RCU grace
	 * period. This syncs the hash iteration and freeing of items
	 * on the hash. rcu_read_lock is too dangerous here.
	 */
	preempt_disable_notrace();
	hlist_for_each_entry_rcu_notrace(entry, hhd, node) {
		if (entry->ip == ip)
			entry->ops->func(ip, parent_ip, &entry->data);
	}
	preempt_enable_notrace();
}

static struct ftrace_ops trace_probe_ops __read_mostly =
{
	.func		= function_trace_probe_call,
	.flags		= FTRACE_OPS_FL_INITIALIZED,
	INIT_REGEX_LOCK(trace_probe_ops)
};

static int ftrace_probe_registered;

static void __enable_ftrace_function_probe(void)
{
	int ret;
	int i;

	if (ftrace_probe_registered) {
		/* still need to update the function call sites */
		if (ftrace_enabled)
			ftrace_run_update_code(FTRACE_UPDATE_CALLS);
		return;
	}

	for (i = 0; i < FTRACE_FUNC_HASHSIZE; i++) {
		struct hlist_head *hhd = &ftrace_func_hash[i];
		if (hhd->first)
			break;
	}
	/* Nothing registered? */
	if (i == FTRACE_FUNC_HASHSIZE)
		return;

	ret = ftrace_startup(&trace_probe_ops, 0);

	ftrace_probe_registered = 1;
}

static void __disable_ftrace_function_probe(void)
{
	int i;

	if (!ftrace_probe_registered)
		return;

	for (i = 0; i < FTRACE_FUNC_HASHSIZE; i++) {
		struct hlist_head *hhd = &ftrace_func_hash[i];
		if (hhd->first)
			return;
	}

	/* no more funcs left */
	ftrace_shutdown(&trace_probe_ops, 0);

	ftrace_probe_registered = 0;
}


static void ftrace_free_entry(struct ftrace_func_probe *entry)
{
	if (entry->ops->free)
		entry->ops->free(entry->ops, entry->ip, &entry->data);
	kfree(entry);
}

int
register_ftrace_function_probe(char *glob, struct ftrace_probe_ops *ops,
			      void *data)
{
	struct ftrace_func_probe *entry;
	struct ftrace_hash **orig_hash = &trace_probe_ops.filter_hash;
	struct ftrace_hash *hash;
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	int type, len, not;
	unsigned long key;
	int count = 0;
	char *search;
	int ret;

	type = filter_parse_regex(glob, strlen(glob), &search, &not);
	len = strlen(search);

	/* we do not support '!' for function probes */
	if (WARN_ON(not))
		return -EINVAL;

	mutex_lock(&trace_probe_ops.regex_lock);

	hash = alloc_and_copy_ftrace_hash(FTRACE_HASH_DEFAULT_BITS, *orig_hash);
	if (!hash) {
		count = -ENOMEM;
		goto out;
	}

	if (unlikely(ftrace_disabled)) {
		count = -ENODEV;
		goto out;
	}

	mutex_lock(&ftrace_lock);

	do_for_each_ftrace_rec(pg, rec) {

		if (!ftrace_match_record(rec, NULL, search, len, type))
			continue;

		entry = kmalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry) {
			/* If we did not process any, then return error */
			if (!count)
				count = -ENOMEM;
			goto out_unlock;
		}

		count++;

		entry->data = data;

		/*
		 * The caller might want to do something special
		 * for each function we find. We call the callback
		 * to give the caller an opportunity to do so.
		 */
		if (ops->init) {
			if (ops->init(ops, rec->ip, &entry->data) < 0) {
				/* caller does not like this func */
				kfree(entry);
				continue;
			}
		}

		ret = enter_record(hash, rec, 0);
		if (ret < 0) {
			kfree(entry);
			count = ret;
			goto out_unlock;
		}

		entry->ops = ops;
		entry->ip = rec->ip;

		key = hash_long(entry->ip, FTRACE_HASH_BITS);
		hlist_add_head_rcu(&entry->node, &ftrace_func_hash[key]);

	} while_for_each_ftrace_rec();

	ret = ftrace_hash_move(&trace_probe_ops, 1, orig_hash, hash);
	if (ret < 0)
		count = ret;

	__enable_ftrace_function_probe();

 out_unlock:
	mutex_unlock(&ftrace_lock);
 out:
	mutex_unlock(&trace_probe_ops.regex_lock);
	free_ftrace_hash(hash);

	return count;
}

enum {
	PROBE_TEST_FUNC		= 1,
	PROBE_TEST_DATA		= 2
};

static void
__unregister_ftrace_function_probe(char *glob, struct ftrace_probe_ops *ops,
				  void *data, int flags)
{
	struct ftrace_func_entry *rec_entry;
	struct ftrace_func_probe *entry;
	struct ftrace_func_probe *p;
	struct ftrace_hash **orig_hash = &trace_probe_ops.filter_hash;
	struct list_head free_list;
	struct ftrace_hash *hash;
	struct hlist_node *tmp;
	char str[KSYM_SYMBOL_LEN];
	int type = MATCH_FULL;
	int i, len = 0;
	char *search;

	if (glob && (strcmp(glob, "*") == 0 || !strlen(glob)))
		glob = NULL;
	else if (glob) {
		int not;

		type = filter_parse_regex(glob, strlen(glob), &search, &not);
		len = strlen(search);

		/* we do not support '!' for function probes */
		if (WARN_ON(not))
			return;
	}

	mutex_lock(&trace_probe_ops.regex_lock);

	hash = alloc_and_copy_ftrace_hash(FTRACE_HASH_DEFAULT_BITS, *orig_hash);
	if (!hash)
		/* Hmm, should report this somehow */
		goto out_unlock;

	INIT_LIST_HEAD(&free_list);

	for (i = 0; i < FTRACE_FUNC_HASHSIZE; i++) {
		struct hlist_head *hhd = &ftrace_func_hash[i];

		hlist_for_each_entry_safe(entry, tmp, hhd, node) {

			/* break up if statements for readability */
			if ((flags & PROBE_TEST_FUNC) && entry->ops != ops)
				continue;

			if ((flags & PROBE_TEST_DATA) && entry->data != data)
				continue;

			/* do this last, since it is the most expensive */
			if (glob) {
				kallsyms_lookup(entry->ip, NULL, NULL,
						NULL, str);
				if (!ftrace_match(str, glob, len, type))
					continue;
			}

			rec_entry = ftrace_lookup_ip(hash, entry->ip);
			/* It is possible more than one entry had this ip */
			if (rec_entry)
				free_hash_entry(hash, rec_entry);

			hlist_del_rcu(&entry->node);
			list_add(&entry->free_list, &free_list);
		}
	}
	mutex_lock(&ftrace_lock);
	__disable_ftrace_function_probe();
	/*
	 * Remove after the disable is called. Otherwise, if the last
	 * probe is removed, a null hash means *all enabled*.
	 */
	ftrace_hash_move(&trace_probe_ops, 1, orig_hash, hash);
	synchronize_sched();
	list_for_each_entry_safe(entry, p, &free_list, free_list) {
		list_del(&entry->free_list);
		ftrace_free_entry(entry);
	}
	mutex_unlock(&ftrace_lock);
		
 out_unlock:
	mutex_unlock(&trace_probe_ops.regex_lock);
	free_ftrace_hash(hash);
}

void
unregister_ftrace_function_probe(char *glob, struct ftrace_probe_ops *ops,
				void *data)
{
	__unregister_ftrace_function_probe(glob, ops, data,
					  PROBE_TEST_FUNC | PROBE_TEST_DATA);
}

void
unregister_ftrace_function_probe_func(char *glob, struct ftrace_probe_ops *ops)
{
	__unregister_ftrace_function_probe(glob, ops, NULL, PROBE_TEST_FUNC);
}

void unregister_ftrace_function_probe_all(char *glob)
{
	__unregister_ftrace_function_probe(glob, NULL, NULL, 0);
}

static LIST_HEAD(ftrace_commands);
static DEFINE_MUTEX(ftrace_cmd_mutex);

/*
 * Currently we only register ftrace commands from __init, so mark this
 * __init too.
 */
__init int register_ftrace_command(struct ftrace_func_command *cmd)
{
	struct ftrace_func_command *p;
	int ret = 0;

	mutex_lock(&ftrace_cmd_mutex);
	list_for_each_entry(p, &ftrace_commands, list) {
		if (strcmp(cmd->name, p->name) == 0) {
			ret = -EBUSY;
			goto out_unlock;
		}
	}
	list_add(&cmd->list, &ftrace_commands);
 out_unlock:
	mutex_unlock(&ftrace_cmd_mutex);

	return ret;
}

/*
 * Currently we only unregister ftrace commands from __init, so mark
 * this __init too.
 */
__init int unregister_ftrace_command(struct ftrace_func_command *cmd)
{
	struct ftrace_func_command *p, *n;
	int ret = -ENODEV;

	mutex_lock(&ftrace_cmd_mutex);
	list_for_each_entry_safe(p, n, &ftrace_commands, list) {
		if (strcmp(cmd->name, p->name) == 0) {
			ret = 0;
			list_del_init(&p->list);
			goto out_unlock;
		}
	}
 out_unlock:
	mutex_unlock(&ftrace_cmd_mutex);

	return ret;
}

static int ftrace_process_regex(struct ftrace_hash *hash,
				char *buff, int len, int enable)
{
	char *func, *command, *next = buff;
	struct ftrace_func_command *p;
	int ret = -EINVAL;

	func = strsep(&next, ":");

	if (!next) {
		ret = ftrace_match_records(hash, func, len);
		if (!ret)
			ret = -EINVAL;
		if (ret < 0)
			return ret;
		return 0;
	}

	/* command found */

	command = strsep(&next, ":");

	mutex_lock(&ftrace_cmd_mutex);
	list_for_each_entry(p, &ftrace_commands, list) {
		if (strcmp(p->name, command) == 0) {
			ret = p->func(hash, func, command, next, enable);
			goto out_unlock;
		}
	}
 out_unlock:
	mutex_unlock(&ftrace_cmd_mutex);

	return ret;
}

static ssize_t
ftrace_regex_write(struct file *file, const char __user *ubuf,
		   size_t cnt, loff_t *ppos, int enable)
{
	struct ftrace_iterator *iter;
	struct trace_parser *parser;
	ssize_t ret, read;

	if (!cnt)
		return 0;

	if (file->f_mode & FMODE_READ) {
		struct seq_file *m = file->private_data;
		iter = m->private;
	} else
		iter = file->private_data;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	/* iter->hash is a local copy, so we don't need regex_lock */

	parser = &iter->parser;
	read = trace_get_user(parser, ubuf, cnt, ppos);

	if (read >= 0 && trace_parser_loaded(parser) &&
	    !trace_parser_cont(parser)) {
		ret = ftrace_process_regex(iter->hash, parser->buffer,
					   parser->idx, enable);
		trace_parser_clear(parser);
		if (ret < 0)
			goto out;
	}

	ret = read;
 out:
	return ret;
}

ssize_t
ftrace_filter_write(struct file *file, const char __user *ubuf,
		    size_t cnt, loff_t *ppos)
{
	return ftrace_regex_write(file, ubuf, cnt, ppos, 1);
}

ssize_t
ftrace_notrace_write(struct file *file, const char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	return ftrace_regex_write(file, ubuf, cnt, ppos, 0);
}

static int
ftrace_match_addr(struct ftrace_hash *hash, unsigned long ip, int remove)
{
	struct ftrace_func_entry *entry;

	if (!ftrace_location(ip))
		return -EINVAL;

	if (remove) {
		entry = ftrace_lookup_ip(hash, ip);
		if (!entry)
			return -ENOENT;
		free_hash_entry(hash, entry);
		return 0;
	}

	return add_hash_entry(hash, ip);
}

static void ftrace_ops_update_code(struct ftrace_ops *ops)
{
	if (ops->flags & FTRACE_OPS_FL_ENABLED && ftrace_enabled)
		ftrace_run_update_code(FTRACE_UPDATE_CALLS);
}

static int
ftrace_set_hash(struct ftrace_ops *ops, unsigned char *buf, int len,
		unsigned long ip, int remove, int reset, int enable)
{
	struct ftrace_hash **orig_hash;
	struct ftrace_hash *hash;
	int ret;

	/* All global ops uses the global ops filters */
	if (ops->flags & FTRACE_OPS_FL_GLOBAL)
		ops = &global_ops;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	mutex_lock(&ops->regex_lock);

	if (enable)
		orig_hash = &ops->filter_hash;
	else
		orig_hash = &ops->notrace_hash;

	hash = alloc_and_copy_ftrace_hash(FTRACE_HASH_DEFAULT_BITS, *orig_hash);
	if (!hash) {
		ret = -ENOMEM;
		goto out_regex_unlock;
	}

	if (reset)
		ftrace_filter_reset(hash);
	if (buf && !ftrace_match_records(hash, buf, len)) {
		ret = -EINVAL;
		goto out_regex_unlock;
	}
	if (ip) {
		ret = ftrace_match_addr(hash, ip, remove);
		if (ret < 0)
			goto out_regex_unlock;
	}

	mutex_lock(&ftrace_lock);
	ret = ftrace_hash_move(ops, enable, orig_hash, hash);
	if (!ret)
		ftrace_ops_update_code(ops);

	mutex_unlock(&ftrace_lock);

 out_regex_unlock:
	mutex_unlock(&ops->regex_lock);

	free_ftrace_hash(hash);
	return ret;
}

static int
ftrace_set_addr(struct ftrace_ops *ops, unsigned long ip, int remove,
		int reset, int enable)
{
	return ftrace_set_hash(ops, 0, 0, ip, remove, reset, enable);
}

/**
 * ftrace_set_filter_ip - set a function to filter on in ftrace by address
 * @ops - the ops to set the filter with
 * @ip - the address to add to or remove from the filter.
 * @remove - non zero to remove the ip from the filter
 * @reset - non zero to reset all filters before applying this filter.
 *
 * Filters denote which functions should be enabled when tracing is enabled
 * If @ip is NULL, it failes to update filter.
 */
int ftrace_set_filter_ip(struct ftrace_ops *ops, unsigned long ip,
			 int remove, int reset)
{
	ftrace_ops_init(ops);
	return ftrace_set_addr(ops, ip, remove, reset, 1);
}
EXPORT_SYMBOL_GPL(ftrace_set_filter_ip);

static int
ftrace_set_regex(struct ftrace_ops *ops, unsigned char *buf, int len,
		 int reset, int enable)
{
	return ftrace_set_hash(ops, buf, len, 0, 0, reset, enable);
}

/**
 * ftrace_set_filter - set a function to filter on in ftrace
 * @ops - the ops to set the filter with
 * @buf - the string that holds the function filter text.
 * @len - the length of the string.
 * @reset - non zero to reset all filters before applying this filter.
 *
 * Filters denote which functions should be enabled when tracing is enabled.
 * If @buf is NULL and reset is set, all functions will be enabled for tracing.
 */
int ftrace_set_filter(struct ftrace_ops *ops, unsigned char *buf,
		       int len, int reset)
{
	ftrace_ops_init(ops);
	return ftrace_set_regex(ops, buf, len, reset, 1);
}
EXPORT_SYMBOL_GPL(ftrace_set_filter);

/**
 * ftrace_set_notrace - set a function to not trace in ftrace
 * @ops - the ops to set the notrace filter with
 * @buf - the string that holds the function notrace text.
 * @len - the length of the string.
 * @reset - non zero to reset all filters before applying this filter.
 *
 * Notrace Filters denote which functions should not be enabled when tracing
 * is enabled. If @buf is NULL and reset is set, all functions will be enabled
 * for tracing.
 */
int ftrace_set_notrace(struct ftrace_ops *ops, unsigned char *buf,
			int len, int reset)
{
	ftrace_ops_init(ops);
	return ftrace_set_regex(ops, buf, len, reset, 0);
}
EXPORT_SYMBOL_GPL(ftrace_set_notrace);
/**
 * ftrace_set_filter - set a function to filter on in ftrace
 * @ops - the ops to set the filter with
 * @buf - the string that holds the function filter text.
 * @len - the length of the string.
 * @reset - non zero to reset all filters before applying this filter.
 *
 * Filters denote which functions should be enabled when tracing is enabled.
 * If @buf is NULL and reset is set, all functions will be enabled for tracing.
 */
void ftrace_set_global_filter(unsigned char *buf, int len, int reset)
{
	ftrace_set_regex(&global_ops, buf, len, reset, 1);
}
EXPORT_SYMBOL_GPL(ftrace_set_global_filter);

/**
 * ftrace_set_notrace - set a function to not trace in ftrace
 * @ops - the ops to set the notrace filter with
 * @buf - the string that holds the function notrace text.
 * @len - the length of the string.
 * @reset - non zero to reset all filters before applying this filter.
 *
 * Notrace Filters denote which functions should not be enabled when tracing
 * is enabled. If @buf is NULL and reset is set, all functions will be enabled
 * for tracing.
 */
void ftrace_set_global_notrace(unsigned char *buf, int len, int reset)
{
	ftrace_set_regex(&global_ops, buf, len, reset, 0);
}
EXPORT_SYMBOL_GPL(ftrace_set_global_notrace);

/*
 * command line interface to allow users to set filters on boot up.
 */
#define FTRACE_FILTER_SIZE		COMMAND_LINE_SIZE
static char ftrace_notrace_buf[FTRACE_FILTER_SIZE] __initdata;
static char ftrace_filter_buf[FTRACE_FILTER_SIZE] __initdata;

/* Used by function selftest to not test if filter is set */
bool ftrace_filter_param __initdata;

static int __init set_ftrace_notrace(char *str)
{
	ftrace_filter_param = true;
	strlcpy(ftrace_notrace_buf, str, FTRACE_FILTER_SIZE);
	return 1;
}
__setup("ftrace_notrace=", set_ftrace_notrace);

static int __init set_ftrace_filter(char *str)
{
	ftrace_filter_param = true;
	strlcpy(ftrace_filter_buf, str, FTRACE_FILTER_SIZE);
	return 1;
}
__setup("ftrace_filter=", set_ftrace_filter);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static char ftrace_graph_buf[FTRACE_FILTER_SIZE] __initdata;
static int ftrace_set_func(unsigned long *array, int *idx, int size, char *buffer);

static int __init set_graph_function(char *str)
{
	strlcpy(ftrace_graph_buf, str, FTRACE_FILTER_SIZE);
	return 1;
}
__setup("ftrace_graph_filter=", set_graph_function);

static void __init set_ftrace_early_graph(char *buf)
{
	int ret;
	char *func;

	while (buf) {
		func = strsep(&buf, ",");
		/* we allow only one expression at a time */
		ret = ftrace_set_func(ftrace_graph_funcs, &ftrace_graph_count,
				      FTRACE_GRAPH_MAX_FUNCS, func);
		if (ret)
			printk(KERN_DEBUG "ftrace: function %s not "
					  "traceable\n", func);
	}
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

void __init
ftrace_set_early_filter(struct ftrace_ops *ops, char *buf, int enable)
{
	char *func;

	ftrace_ops_init(ops);

	while (buf) {
		func = strsep(&buf, ",");
		ftrace_set_regex(ops, func, strlen(func), 0, enable);
	}
}

static void __init set_ftrace_early_filters(void)
{
	if (ftrace_filter_buf[0])
		ftrace_set_early_filter(&global_ops, ftrace_filter_buf, 1);
	if (ftrace_notrace_buf[0])
		ftrace_set_early_filter(&global_ops, ftrace_notrace_buf, 0);
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (ftrace_graph_buf[0])
		set_ftrace_early_graph(ftrace_graph_buf);
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
}

int ftrace_regex_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct ftrace_iterator *iter;
	struct ftrace_hash **orig_hash;
	struct trace_parser *parser;
	int filter_hash;
	int ret;

	if (file->f_mode & FMODE_READ) {
		iter = m->private;
		seq_release(inode, file);
	} else
		iter = file->private_data;

	parser = &iter->parser;
	if (trace_parser_loaded(parser)) {
		parser->buffer[parser->idx] = 0;
		ftrace_match_records(iter->hash, parser->buffer, parser->idx);
	}

	trace_parser_put(parser);

	mutex_lock(&iter->ops->regex_lock);

	if (file->f_mode & FMODE_WRITE) {
		filter_hash = !!(iter->flags & FTRACE_ITER_FILTER);

		if (filter_hash)
			orig_hash = &iter->ops->filter_hash;
		else
			orig_hash = &iter->ops->notrace_hash;

		mutex_lock(&ftrace_lock);
		ret = ftrace_hash_move(iter->ops, filter_hash,
				       orig_hash, iter->hash);
		if (!ret)
			ftrace_ops_update_code(iter->ops);

		mutex_unlock(&ftrace_lock);
	}

	mutex_unlock(&iter->ops->regex_lock);
	free_ftrace_hash(iter->hash);
	kfree(iter);

	return 0;
}

static const struct file_operations ftrace_avail_fops = {
	.open = ftrace_avail_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

static const struct file_operations ftrace_enabled_fops = {
	.open = ftrace_enabled_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

static const struct file_operations ftrace_filter_fops = {
	.open = ftrace_filter_open,
	.read = seq_read,
	.write = ftrace_filter_write,
	.llseek = ftrace_filter_lseek,
	.release = ftrace_regex_release,
};

static const struct file_operations ftrace_notrace_fops = {
	.open = ftrace_notrace_open,
	.read = seq_read,
	.write = ftrace_notrace_write,
	.llseek = ftrace_filter_lseek,
	.release = ftrace_regex_release,
};

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

static DEFINE_MUTEX(graph_lock);

int ftrace_graph_count;
int ftrace_graph_notrace_count;
unsigned long ftrace_graph_funcs[FTRACE_GRAPH_MAX_FUNCS] __read_mostly;
unsigned long ftrace_graph_notrace_funcs[FTRACE_GRAPH_MAX_FUNCS] __read_mostly;

struct ftrace_graph_data {
	unsigned long *table;
	size_t size;
	int *count;
	const struct seq_operations *seq_ops;
};

static void *
__g_next(struct seq_file *m, loff_t *pos)
{
	struct ftrace_graph_data *fgd = m->private;

	if (*pos >= *fgd->count)
		return NULL;
	return &fgd->table[*pos];
}

static void *
g_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return __g_next(m, pos);
}

static void *g_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_graph_data *fgd = m->private;

	mutex_lock(&graph_lock);

	/* Nothing, tell g_show to print all functions are enabled */
	if (!*fgd->count && !*pos)
		return (void *)1;

	return __g_next(m, pos);
}

static void g_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&graph_lock);
}

static int g_show(struct seq_file *m, void *v)
{
	unsigned long *ptr = v;

	if (!ptr)
		return 0;

	if (ptr == (unsigned long *)1) {
		seq_printf(m, "#### all functions enabled ####\n");
		return 0;
	}

	seq_printf(m, "%ps\n", (void *)*ptr);

	return 0;
}

static const struct seq_operations ftrace_graph_seq_ops = {
	.start = g_start,
	.next = g_next,
	.stop = g_stop,
	.show = g_show,
};

static int
__ftrace_graph_open(struct inode *inode, struct file *file,
		    struct ftrace_graph_data *fgd)
{
	int ret = 0;

	mutex_lock(&graph_lock);
	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC)) {
		*fgd->count = 0;
		memset(fgd->table, 0, fgd->size * sizeof(*fgd->table));
	}
	mutex_unlock(&graph_lock);

	if (file->f_mode & FMODE_READ) {
		ret = seq_open(file, fgd->seq_ops);
		if (!ret) {
			struct seq_file *m = file->private_data;
			m->private = fgd;
		}
	} else
		file->private_data = fgd;

	return ret;
}

static int
ftrace_graph_open(struct inode *inode, struct file *file)
{
	struct ftrace_graph_data *fgd;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	fgd = kmalloc(sizeof(*fgd), GFP_KERNEL);
	if (fgd == NULL)
		return -ENOMEM;

	fgd->table = ftrace_graph_funcs;
	fgd->size = FTRACE_GRAPH_MAX_FUNCS;
	fgd->count = &ftrace_graph_count;
	fgd->seq_ops = &ftrace_graph_seq_ops;

	return __ftrace_graph_open(inode, file, fgd);
}

static int
ftrace_graph_notrace_open(struct inode *inode, struct file *file)
{
	struct ftrace_graph_data *fgd;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	fgd = kmalloc(sizeof(*fgd), GFP_KERNEL);
	if (fgd == NULL)
		return -ENOMEM;

	fgd->table = ftrace_graph_notrace_funcs;
	fgd->size = FTRACE_GRAPH_MAX_FUNCS;
	fgd->count = &ftrace_graph_notrace_count;
	fgd->seq_ops = &ftrace_graph_seq_ops;

	return __ftrace_graph_open(inode, file, fgd);
}

static int
ftrace_graph_release(struct inode *inode, struct file *file)
{
	if (file->f_mode & FMODE_READ) {
		struct seq_file *m = file->private_data;

		kfree(m->private);
		seq_release(inode, file);
	} else {
		kfree(file->private_data);
	}

	return 0;
}

static int
ftrace_set_func(unsigned long *array, int *idx, int size, char *buffer)
{
	struct dyn_ftrace *rec;
	struct ftrace_page *pg;
	int search_len;
	int fail = 1;
	int type, not;
	char *search;
	bool exists;
	int i;

	/* decode regex */
	type = filter_parse_regex(buffer, strlen(buffer), &search, &not);
	if (!not && *idx >= size)
		return -EBUSY;

	search_len = strlen(search);

	mutex_lock(&ftrace_lock);

	if (unlikely(ftrace_disabled)) {
		mutex_unlock(&ftrace_lock);
		return -ENODEV;
	}

	do_for_each_ftrace_rec(pg, rec) {

		if (ftrace_match_record(rec, NULL, search, search_len, type)) {
			/* if it is in the array */
			exists = false;
			for (i = 0; i < *idx; i++) {
				if (array[i] == rec->ip) {
					exists = true;
					break;
				}
			}

			if (!not) {
				fail = 0;
				if (!exists) {
					array[(*idx)++] = rec->ip;
					if (*idx >= size)
						goto out;
				}
			} else {
				if (exists) {
					array[i] = array[--(*idx)];
					array[*idx] = 0;
					fail = 0;
				}
			}
		}
	} while_for_each_ftrace_rec();
out:
	mutex_unlock(&ftrace_lock);

	if (fail)
		return -EINVAL;

	return 0;
}

static ssize_t
ftrace_graph_write(struct file *file, const char __user *ubuf,
		   size_t cnt, loff_t *ppos)
{
	struct trace_parser parser;
	ssize_t read, ret = 0;
	struct ftrace_graph_data *fgd = file->private_data;

	if (!cnt)
		return 0;

	if (trace_parser_get_init(&parser, FTRACE_BUFF_MAX))
		return -ENOMEM;

	read = trace_get_user(&parser, ubuf, cnt, ppos);

	if (read >= 0 && trace_parser_loaded((&parser))) {
		parser.buffer[parser.idx] = 0;

		mutex_lock(&graph_lock);

		/* we allow only one expression at a time */
		ret = ftrace_set_func(fgd->table, fgd->count, fgd->size,
				      parser.buffer);

		mutex_unlock(&graph_lock);
	}

	if (!ret)
		ret = read;

	trace_parser_put(&parser);

	return ret;
}

static const struct file_operations ftrace_graph_fops = {
	.open		= ftrace_graph_open,
	.read		= seq_read,
	.write		= ftrace_graph_write,
	.llseek		= ftrace_filter_lseek,
	.release	= ftrace_graph_release,
};

static const struct file_operations ftrace_graph_notrace_fops = {
	.open		= ftrace_graph_notrace_open,
	.read		= seq_read,
	.write		= ftrace_graph_write,
	.llseek		= ftrace_filter_lseek,
	.release	= ftrace_graph_release,
};
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

static __init int ftrace_init_dyn_debugfs(struct dentry *d_tracer)
{

	trace_create_file("available_filter_functions", 0444,
			d_tracer, NULL, &ftrace_avail_fops);

	trace_create_file("enabled_functions", 0444,
			d_tracer, NULL, &ftrace_enabled_fops);

	trace_create_file("set_ftrace_filter", 0644, d_tracer,
			NULL, &ftrace_filter_fops);

	trace_create_file("set_ftrace_notrace", 0644, d_tracer,
				    NULL, &ftrace_notrace_fops);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	trace_create_file("set_graph_function", 0444, d_tracer,
				    NULL,
				    &ftrace_graph_fops);
	trace_create_file("set_graph_notrace", 0444, d_tracer,
				    NULL,
				    &ftrace_graph_notrace_fops);
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

	return 0;
}

static int ftrace_cmp_ips(const void *a, const void *b)
{
	const unsigned long *ipa = a;
	const unsigned long *ipb = b;

	if (*ipa > *ipb)
		return 1;
	if (*ipa < *ipb)
		return -1;
	return 0;
}

static void ftrace_swap_ips(void *a, void *b, int size)
{
	unsigned long *ipa = a;
	unsigned long *ipb = b;
	unsigned long t;

	t = *ipa;
	*ipa = *ipb;
	*ipb = t;
}

static int ftrace_process_locs(struct module *mod,
			       unsigned long *start,
			       unsigned long *end)
{
	struct ftrace_page *start_pg;
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	unsigned long count;
	unsigned long *p;
	unsigned long addr;
	unsigned long flags = 0; /* Shut up gcc */
	int ret = -ENOMEM;

	count = end - start;

	if (!count)
		return 0;

	sort(start, count, sizeof(*start),
	     ftrace_cmp_ips, ftrace_swap_ips);

	start_pg = ftrace_allocate_pages(count);
	if (!start_pg)
		return -ENOMEM;

	mutex_lock(&ftrace_lock);

	/*
	 * Core and each module needs their own pages, as
	 * modules will free them when they are removed.
	 * Force a new page to be allocated for modules.
	 */
	if (!mod) {
		WARN_ON(ftrace_pages || ftrace_pages_start);
		/* First initialization */
		ftrace_pages = ftrace_pages_start = start_pg;
	} else {
		if (!ftrace_pages)
			goto out;

		if (WARN_ON(ftrace_pages->next)) {
			/* Hmm, we have free pages? */
			while (ftrace_pages->next)
				ftrace_pages = ftrace_pages->next;
		}

		ftrace_pages->next = start_pg;
	}

	p = start;
	pg = start_pg;
	while (p < end) {
		addr = ftrace_call_adjust(*p++);
		/*
		 * Some architecture linkers will pad between
		 * the different mcount_loc sections of different
		 * object files to satisfy alignments.
		 * Skip any NULL pointers.
		 */
		if (!addr)
			continue;

		if (pg->index == pg->size) {
			/* We should have allocated enough */
			if (WARN_ON(!pg->next))
				break;
			pg = pg->next;
		}

		rec = &pg->records[pg->index++];
		rec->ip = addr;
	}

	/* We should have used all pages */
	WARN_ON(pg->next);

	/* Assign the last page to ftrace_pages */
	ftrace_pages = pg;

	/* These new locations need to be initialized */
	ftrace_new_pgs = start_pg;

	/*
	 * We only need to disable interrupts on start up
	 * because we are modifying code that an interrupt
	 * may execute, and the modification is not atomic.
	 * But for modules, nothing runs the code we modify
	 * until we are finished with it, and there's no
	 * reason to cause large interrupt latencies while we do it.
	 */
	if (!mod)
		local_irq_save(flags);
	ftrace_update_code(mod);
	if (!mod)
		local_irq_restore(flags);
	ret = 0;
 out:
	mutex_unlock(&ftrace_lock);

	return ret;
}

#ifdef CONFIG_MODULES

#define next_to_ftrace_page(p) container_of(p, struct ftrace_page, next)

void ftrace_release_mod(struct module *mod)
{
	struct dyn_ftrace *rec;
	struct ftrace_page **last_pg;
	struct ftrace_page *pg;
	int order;

	mutex_lock(&ftrace_lock);

	if (ftrace_disabled)
		goto out_unlock;

	/*
	 * Each module has its own ftrace_pages, remove
	 * them from the list.
	 */
	last_pg = &ftrace_pages_start;
	for (pg = ftrace_pages_start; pg; pg = *last_pg) {
		rec = &pg->records[0];
		if (within_module_core(rec->ip, mod)) {
			/*
			 * As core pages are first, the first
			 * page should never be a module page.
			 */
			if (WARN_ON(pg == ftrace_pages_start))
				goto out_unlock;

			/* Check if we are deleting the last page */
			if (pg == ftrace_pages)
				ftrace_pages = next_to_ftrace_page(last_pg);

			*last_pg = pg->next;
			order = get_count_order(pg->size / ENTRIES_PER_PAGE);
			free_pages((unsigned long)pg->records, order);
			kfree(pg);
		} else
			last_pg = &pg->next;
	}
 out_unlock:
	mutex_unlock(&ftrace_lock);
}

static void ftrace_init_module(struct module *mod,
			       unsigned long *start, unsigned long *end)
{
	if (ftrace_disabled || start == end)
		return;
	ftrace_process_locs(mod, start, end);
}

static int ftrace_module_notify_enter(struct notifier_block *self,
				      unsigned long val, void *data)
{
	struct module *mod = data;

	if (val == MODULE_STATE_COMING)
		ftrace_init_module(mod, mod->ftrace_callsites,
				   mod->ftrace_callsites +
				   mod->num_ftrace_callsites);
	return 0;
}

static int ftrace_module_notify_exit(struct notifier_block *self,
				     unsigned long val, void *data)
{
	struct module *mod = data;

	if (val == MODULE_STATE_GOING)
		ftrace_release_mod(mod);

	return 0;
}
#else
static int ftrace_module_notify_enter(struct notifier_block *self,
				      unsigned long val, void *data)
{
	return 0;
}
static int ftrace_module_notify_exit(struct notifier_block *self,
				     unsigned long val, void *data)
{
	return 0;
}
#endif /* CONFIG_MODULES */

struct notifier_block ftrace_module_enter_nb = {
	.notifier_call = ftrace_module_notify_enter,
	.priority = INT_MAX,	/* Run before anything that can use kprobes */
};

struct notifier_block ftrace_module_exit_nb = {
	.notifier_call = ftrace_module_notify_exit,
	.priority = INT_MIN,	/* Run after anything that can remove kprobes */
};

extern unsigned long __start_mcount_loc[];
extern unsigned long __stop_mcount_loc[];

void __init ftrace_init(void)
{
	unsigned long count, addr, flags;
	int ret;

	/* Keep the ftrace pointer to the stub */
	addr = (unsigned long)ftrace_stub;

	local_irq_save(flags);
	ftrace_dyn_arch_init(&addr);
	local_irq_restore(flags);

	/* ftrace_dyn_arch_init places the return code in addr */
	if (addr)
		goto failed;

	count = __stop_mcount_loc - __start_mcount_loc;

	ret = ftrace_dyn_table_alloc(count);
	if (ret)
		goto failed;

	last_ftrace_enabled = ftrace_enabled = 1;

	ret = ftrace_process_locs(NULL,
				  __start_mcount_loc,
				  __stop_mcount_loc);

	ret = register_module_notifier(&ftrace_module_enter_nb);
	if (ret)
		pr_warning("Failed to register trace ftrace module enter notifier\n");

	ret = register_module_notifier(&ftrace_module_exit_nb);
	if (ret)
		pr_warning("Failed to register trace ftrace module exit notifier\n");

	set_ftrace_early_filters();

	return;
 failed:
	ftrace_disabled = 1;
}

#else

static struct ftrace_ops global_ops = {
	.func			= ftrace_stub,
	.flags			= FTRACE_OPS_FL_RECURSION_SAFE | FTRACE_OPS_FL_INITIALIZED,
	INIT_REGEX_LOCK(global_ops)
};

static int __init ftrace_nodyn_init(void)
{
	ftrace_enabled = 1;
	return 0;
}
core_initcall(ftrace_nodyn_init);

static inline int ftrace_init_dyn_debugfs(struct dentry *d_tracer) { return 0; }
static inline void ftrace_startup_enable(int command) { }
/* Keep as macros so we do not need to define the commands */
# define ftrace_startup(ops, command)					\
	({								\
		int ___ret = __register_ftrace_function(ops);		\
		if (!___ret)						\
			(ops)->flags |= FTRACE_OPS_FL_ENABLED;		\
		___ret;							\
	})
# define ftrace_shutdown(ops, command) __unregister_ftrace_function(ops)

# define ftrace_startup_sysctl()	do { } while (0)
# define ftrace_shutdown_sysctl()	do { } while (0)

static inline int
ftrace_ops_test(struct ftrace_ops *ops, unsigned long ip, void *regs)
{
	return 1;
}

#endif /* CONFIG_DYNAMIC_FTRACE */

static void
ftrace_ops_control_func(unsigned long ip, unsigned long parent_ip,
			struct ftrace_ops *op, struct pt_regs *regs)
{
	if (unlikely(trace_recursion_test(TRACE_CONTROL_BIT)))
		return;

	/*
	 * Some of the ops may be dynamically allocated,
	 * they must be freed after a synchronize_sched().
	 */
	preempt_disable_notrace();
	trace_recursion_set(TRACE_CONTROL_BIT);

	/*
	 * Control funcs (perf) uses RCU. Only trace if
	 * RCU is currently active.
	 */
	if (!rcu_is_watching())
		goto out;

	do_for_each_ftrace_op(op, ftrace_control_list) {
		if (!(op->flags & FTRACE_OPS_FL_STUB) &&
		    !ftrace_function_local_disabled(op) &&
		    ftrace_ops_test(op, ip, regs))
			op->func(ip, parent_ip, op, regs);
	} while_for_each_ftrace_op(op);
 out:
	trace_recursion_clear(TRACE_CONTROL_BIT);
	preempt_enable_notrace();
}

static struct ftrace_ops control_ops = {
	.func	= ftrace_ops_control_func,
	.flags	= FTRACE_OPS_FL_RECURSION_SAFE | FTRACE_OPS_FL_INITIALIZED,
	INIT_REGEX_LOCK(control_ops)
};

static inline void
__ftrace_ops_list_func(unsigned long ip, unsigned long parent_ip,
		       struct ftrace_ops *ignored, struct pt_regs *regs)
{
	struct ftrace_ops *op;
	int bit;

	if (function_trace_stop)
		return;

	bit = trace_test_and_set_recursion(TRACE_LIST_START, TRACE_LIST_MAX);
	if (bit < 0)
		return;

	/*
	 * Some of the ops may be dynamically allocated,
	 * they must be freed after a synchronize_sched().
	 */
	preempt_disable_notrace();
	do_for_each_ftrace_op(op, ftrace_ops_list) {
		if (ftrace_ops_test(op, ip, regs))
			op->func(ip, parent_ip, op, regs);
	} while_for_each_ftrace_op(op);
	preempt_enable_notrace();
	trace_clear_recursion(bit);
}

/*
 * Some archs only support passing ip and parent_ip. Even though
 * the list function ignores the op parameter, we do not want any
 * C side effects, where a function is called without the caller
 * sending a third parameter.
 * Archs are to support both the regs and ftrace_ops at the same time.
 * If they support ftrace_ops, it is assumed they support regs.
 * If call backs want to use regs, they must either check for regs
 * being NULL, or CONFIG_DYNAMIC_FTRACE_WITH_REGS.
 * Note, CONFIG_DYNAMIC_FTRACE_WITH_REGS expects a full regs to be saved.
 * An architecture can pass partial regs with ftrace_ops and still
 * set the ARCH_SUPPORT_FTARCE_OPS.
 */
#if ARCH_SUPPORTS_FTRACE_OPS
static void ftrace_ops_list_func(unsigned long ip, unsigned long parent_ip,
				 struct ftrace_ops *op, struct pt_regs *regs)
{
	__ftrace_ops_list_func(ip, parent_ip, NULL, regs);
}
#else
static void ftrace_ops_no_ops(unsigned long ip, unsigned long parent_ip)
{
	__ftrace_ops_list_func(ip, parent_ip, NULL, NULL);
}
#endif

static void clear_ftrace_swapper(void)
{
	struct task_struct *p;
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu) {
		p = idle_task(cpu);
		clear_tsk_trace_trace(p);
	}
	put_online_cpus();
}

static void set_ftrace_swapper(void)
{
	struct task_struct *p;
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu) {
		p = idle_task(cpu);
		set_tsk_trace_trace(p);
	}
	put_online_cpus();
}

static void clear_ftrace_pid(struct pid *pid)
{
	struct task_struct *p;

	rcu_read_lock();
	do_each_pid_task(pid, PIDTYPE_PID, p) {
		clear_tsk_trace_trace(p);
	} while_each_pid_task(pid, PIDTYPE_PID, p);
	rcu_read_unlock();

	put_pid(pid);
}

static void set_ftrace_pid(struct pid *pid)
{
	struct task_struct *p;

	rcu_read_lock();
	do_each_pid_task(pid, PIDTYPE_PID, p) {
		set_tsk_trace_trace(p);
	} while_each_pid_task(pid, PIDTYPE_PID, p);
	rcu_read_unlock();
}

static void clear_ftrace_pid_task(struct pid *pid)
{
	if (pid == ftrace_swapper_pid)
		clear_ftrace_swapper();
	else
		clear_ftrace_pid(pid);
}

static void set_ftrace_pid_task(struct pid *pid)
{
	if (pid == ftrace_swapper_pid)
		set_ftrace_swapper();
	else
		set_ftrace_pid(pid);
}

static int ftrace_pid_add(int p)
{
	struct pid *pid;
	struct ftrace_pid *fpid;
	int ret = -EINVAL;

	mutex_lock(&ftrace_lock);

	if (!p)
		pid = ftrace_swapper_pid;
	else
		pid = find_get_pid(p);

	if (!pid)
		goto out;

	ret = 0;

	list_for_each_entry(fpid, &ftrace_pids, list)
		if (fpid->pid == pid)
			goto out_put;

	ret = -ENOMEM;

	fpid = kmalloc(sizeof(*fpid), GFP_KERNEL);
	if (!fpid)
		goto out_put;

	list_add(&fpid->list, &ftrace_pids);
	fpid->pid = pid;

	set_ftrace_pid_task(pid);

	ftrace_update_pid_func();
	ftrace_startup_enable(0);

	mutex_unlock(&ftrace_lock);
	return 0;

out_put:
	if (pid != ftrace_swapper_pid)
		put_pid(pid);

out:
	mutex_unlock(&ftrace_lock);
	return ret;
}

static void ftrace_pid_reset(void)
{
	struct ftrace_pid *fpid, *safe;

	mutex_lock(&ftrace_lock);
	list_for_each_entry_safe(fpid, safe, &ftrace_pids, list) {
		struct pid *pid = fpid->pid;

		clear_ftrace_pid_task(pid);

		list_del(&fpid->list);
		kfree(fpid);
	}

	ftrace_update_pid_func();
	ftrace_startup_enable(0);

	mutex_unlock(&ftrace_lock);
}

static void *fpid_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&ftrace_lock);

	if (list_empty(&ftrace_pids) && (!*pos))
		return (void *) 1;

	return seq_list_start(&ftrace_pids, *pos);
}

static void *fpid_next(struct seq_file *m, void *v, loff_t *pos)
{
	if (v == (void *)1)
		return NULL;

	return seq_list_next(v, &ftrace_pids, pos);
}

static void fpid_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&ftrace_lock);
}

static int fpid_show(struct seq_file *m, void *v)
{
	const struct ftrace_pid *fpid = list_entry(v, struct ftrace_pid, list);

	if (v == (void *)1) {
		seq_printf(m, "no pid\n");
		return 0;
	}

	if (fpid->pid == ftrace_swapper_pid)
		seq_printf(m, "swapper tasks\n");
	else
		seq_printf(m, "%u\n", pid_vnr(fpid->pid));

	return 0;
}

static const struct seq_operations ftrace_pid_sops = {
	.start = fpid_start,
	.next = fpid_next,
	.stop = fpid_stop,
	.show = fpid_show,
};

static int
ftrace_pid_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC))
		ftrace_pid_reset();

	if (file->f_mode & FMODE_READ)
		ret = seq_open(file, &ftrace_pid_sops);

	return ret;
}

static ssize_t
ftrace_pid_write(struct file *filp, const char __user *ubuf,
		   size_t cnt, loff_t *ppos)
{
	char buf[64], *tmp;
	long val;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	/*
	 * Allow "echo > set_ftrace_pid" or "echo -n '' > set_ftrace_pid"
	 * to clean the filter quietly.
	 */
	tmp = strstrip(buf);
	if (strlen(tmp) == 0)
		return 1;

	ret = kstrtol(tmp, 10, &val);
	if (ret < 0)
		return ret;

	ret = ftrace_pid_add(val);

	return ret ? ret : cnt;
}

static int
ftrace_pid_release(struct inode *inode, struct file *file)
{
	if (file->f_mode & FMODE_READ)
		seq_release(inode, file);

	return 0;
}

static const struct file_operations ftrace_pid_fops = {
	.open		= ftrace_pid_open,
	.write		= ftrace_pid_write,
	.read		= seq_read,
	.llseek		= ftrace_filter_lseek,
	.release	= ftrace_pid_release,
};

static __init int ftrace_init_debugfs(void)
{
	struct dentry *d_tracer;

	d_tracer = tracing_init_dentry();
	if (!d_tracer)
		return 0;

	ftrace_init_dyn_debugfs(d_tracer);

	trace_create_file("set_ftrace_pid", 0644, d_tracer,
			    NULL, &ftrace_pid_fops);

	ftrace_profile_debugfs(d_tracer);

	return 0;
}
fs_initcall(ftrace_init_debugfs);

/**
 * ftrace_kill - kill ftrace
 *
 * This function should be used by panic code. It stops ftrace
 * but in a not so nice way. If you need to simply kill ftrace
 * from a non-atomic section, use ftrace_kill.
 */
void ftrace_kill(void)
{
	ftrace_disabled = 1;
	ftrace_enabled = 0;
	clear_ftrace_function();
}

/**
 * Test if ftrace is dead or not.
 */
int ftrace_is_dead(void)
{
	return ftrace_disabled;
}

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
	int ret = -1;

	ftrace_ops_init(ops);

	mutex_lock(&ftrace_lock);

	ret = ftrace_startup(ops, 0);

	mutex_unlock(&ftrace_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(register_ftrace_function);

/**
 * unregister_ftrace_function - unregister a function for profiling.
 * @ops - ops structure that holds the function to unregister
 *
 * Unregister a function that was added to be called by ftrace profiling.
 */
int unregister_ftrace_function(struct ftrace_ops *ops)
{
	int ret;

	mutex_lock(&ftrace_lock);
	ret = ftrace_shutdown(ops, 0);
	mutex_unlock(&ftrace_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(unregister_ftrace_function);

int
ftrace_enable_sysctl(struct ctl_table *table, int write,
		     void __user *buffer, size_t *lenp,
		     loff_t *ppos)
{
	int ret = -ENODEV;

	mutex_lock(&ftrace_lock);

	if (unlikely(ftrace_disabled))
		goto out;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	if (ret || !write || (last_ftrace_enabled == !!ftrace_enabled))
		goto out;

	last_ftrace_enabled = !!ftrace_enabled;

	if (ftrace_enabled) {

		ftrace_startup_sysctl();

		/* we are starting ftrace again */
		if (ftrace_ops_list != &ftrace_list_end)
			update_ftrace_function();

	} else {
		/* stopping ftrace calls (just send to ftrace_stub) */
		ftrace_trace_function = ftrace_stub;

		ftrace_shutdown_sysctl();
	}

 out:
	mutex_unlock(&ftrace_lock);
	return ret;
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

static int ftrace_graph_active;
static struct notifier_block ftrace_suspend_notifier;

int ftrace_graph_entry_stub(struct ftrace_graph_ent *trace)
{
	return 0;
}

/* The callbacks that hook a function */
trace_func_graph_ret_t ftrace_graph_return =
			(trace_func_graph_ret_t)ftrace_stub;
trace_func_graph_ent_t ftrace_graph_entry = ftrace_graph_entry_stub;
static trace_func_graph_ent_t __ftrace_graph_entry = ftrace_graph_entry_stub;

/* Try to assign a return stack array on FTRACE_RETSTACK_ALLOC_SIZE tasks. */
static int alloc_retstack_tasklist(struct ftrace_ret_stack **ret_stack_list)
{
	int i;
	int ret = 0;
	unsigned long flags;
	int start = 0, end = FTRACE_RETSTACK_ALLOC_SIZE;
	struct task_struct *g, *t;

	for (i = 0; i < FTRACE_RETSTACK_ALLOC_SIZE; i++) {
		ret_stack_list[i] = kmalloc(FTRACE_RETFUNC_DEPTH
					* sizeof(struct ftrace_ret_stack),
					GFP_KERNEL);
		if (!ret_stack_list[i]) {
			start = 0;
			end = i;
			ret = -ENOMEM;
			goto free;
		}
	}

	read_lock_irqsave(&tasklist_lock, flags);
	do_each_thread(g, t) {
		if (start == end) {
			ret = -EAGAIN;
			goto unlock;
		}

		if (t->ret_stack == NULL) {
			atomic_set(&t->tracing_graph_pause, 0);
			atomic_set(&t->trace_overrun, 0);
			t->curr_ret_stack = -1;
			/* Make sure the tasks see the -1 first: */
			smp_wmb();
			t->ret_stack = ret_stack_list[start++];
		}
	} while_each_thread(g, t);

unlock:
	read_unlock_irqrestore(&tasklist_lock, flags);
free:
	for (i = start; i < end; i++)
		kfree(ret_stack_list[i]);
	return ret;
}

static void
ftrace_graph_probe_sched_switch(void *ignore,
			struct task_struct *prev, struct task_struct *next)
{
	unsigned long long timestamp;
	int index;

	/*
	 * Does the user want to count the time a function was asleep.
	 * If so, do not update the time stamps.
	 */
	if (trace_flags & TRACE_ITER_SLEEP_TIME)
		return;

	timestamp = trace_clock_local();

	prev->ftrace_timestamp = timestamp;

	/* only process tasks that we timestamped */
	if (!next->ftrace_timestamp)
		return;

	/*
	 * Update all the counters in next to make up for the
	 * time next was sleeping.
	 */
	timestamp -= next->ftrace_timestamp;

	for (index = next->curr_ret_stack; index >= 0; index--)
		next->ret_stack[index].calltime += timestamp;
}

/* Allocate a return stack for each task */
static int start_graph_tracing(void)
{
	struct ftrace_ret_stack **ret_stack_list;
	int ret, cpu;

	ret_stack_list = kmalloc(FTRACE_RETSTACK_ALLOC_SIZE *
				sizeof(struct ftrace_ret_stack *),
				GFP_KERNEL);

	if (!ret_stack_list)
		return -ENOMEM;

	/* The cpu_boot init_task->ret_stack will never be freed */
	for_each_online_cpu(cpu) {
		if (!idle_task(cpu)->ret_stack)
			ftrace_graph_init_idle_task(idle_task(cpu), cpu);
	}

	do {
		ret = alloc_retstack_tasklist(ret_stack_list);
	} while (ret == -EAGAIN);

	if (!ret) {
		ret = register_trace_sched_switch(ftrace_graph_probe_sched_switch, NULL);
		if (ret)
			pr_info("ftrace_graph: Couldn't activate tracepoint"
				" probe to kernel_sched_switch\n");
	}

	kfree(ret_stack_list);
	return ret;
}

/*
 * Hibernation protection.
 * The state of the current task is too much unstable during
 * suspend/restore to disk. We want to protect against that.
 */
static int
ftrace_suspend_notifier_call(struct notifier_block *bl, unsigned long state,
							void *unused)
{
	switch (state) {
	case PM_HIBERNATION_PREPARE:
		pause_graph_tracing();
		break;

	case PM_POST_HIBERNATION:
		unpause_graph_tracing();
		break;
	}
	return NOTIFY_DONE;
}

/* Just a place holder for function graph */
static struct ftrace_ops fgraph_ops __read_mostly = {
	.func		= ftrace_stub,
	.flags		= FTRACE_OPS_FL_STUB | FTRACE_OPS_FL_GLOBAL |
				FTRACE_OPS_FL_RECURSION_SAFE,
};

static int ftrace_graph_entry_test(struct ftrace_graph_ent *trace)
{
	if (!ftrace_ops_test(&global_ops, trace->func, NULL))
		return 0;
	return __ftrace_graph_entry(trace);
}

/*
 * The function graph tracer should only trace the functions defined
 * by set_ftrace_filter and set_ftrace_notrace. If another function
 * tracer ops is registered, the graph tracer requires testing the
 * function against the global ops, and not just trace any function
 * that any ftrace_ops registered.
 */
static void update_function_graph_func(void)
{
	if (ftrace_ops_list == &ftrace_list_end ||
	    (ftrace_ops_list == &global_ops &&
	     global_ops.next == &ftrace_list_end))
		ftrace_graph_entry = __ftrace_graph_entry;
	else
		ftrace_graph_entry = ftrace_graph_entry_test;
}

int register_ftrace_graph(trace_func_graph_ret_t retfunc,
			trace_func_graph_ent_t entryfunc)
{
	int ret = 0;

	mutex_lock(&ftrace_lock);

	/* we currently allow only one tracer registered at a time */
	if (ftrace_graph_active) {
		ret = -EBUSY;
		goto out;
	}

	ftrace_suspend_notifier.notifier_call = ftrace_suspend_notifier_call;
	register_pm_notifier(&ftrace_suspend_notifier);

	ftrace_graph_active++;
	ret = start_graph_tracing();
	if (ret) {
		ftrace_graph_active--;
		goto out;
	}

	ftrace_graph_return = retfunc;

	/*
	 * Update the indirect function to the entryfunc, and the
	 * function that gets called to the entry_test first. Then
	 * call the update fgraph entry function to determine if
	 * the entryfunc should be called directly or not.
	 */
	__ftrace_graph_entry = entryfunc;
	ftrace_graph_entry = ftrace_graph_entry_test;
	update_function_graph_func();

	ret = ftrace_startup(&fgraph_ops, FTRACE_START_FUNC_RET);

out:
	mutex_unlock(&ftrace_lock);
	return ret;
}

void unregister_ftrace_graph(void)
{
	mutex_lock(&ftrace_lock);

	if (unlikely(!ftrace_graph_active))
		goto out;

	ftrace_graph_active--;
	ftrace_graph_return = (trace_func_graph_ret_t)ftrace_stub;
	ftrace_graph_entry = ftrace_graph_entry_stub;
	__ftrace_graph_entry = ftrace_graph_entry_stub;
	ftrace_shutdown(&fgraph_ops, FTRACE_STOP_FUNC_RET);
	unregister_pm_notifier(&ftrace_suspend_notifier);
	unregister_trace_sched_switch(ftrace_graph_probe_sched_switch, NULL);

 out:
	mutex_unlock(&ftrace_lock);
}

static DEFINE_PER_CPU(struct ftrace_ret_stack *, idle_ret_stack);

static void
graph_init_task(struct task_struct *t, struct ftrace_ret_stack *ret_stack)
{
	atomic_set(&t->tracing_graph_pause, 0);
	atomic_set(&t->trace_overrun, 0);
	t->ftrace_timestamp = 0;
	/* make curr_ret_stack visible before we add the ret_stack */
	smp_wmb();
	t->ret_stack = ret_stack;
}

/*
 * Allocate a return stack for the idle task. May be the first
 * time through, or it may be done by CPU hotplug online.
 */
void ftrace_graph_init_idle_task(struct task_struct *t, int cpu)
{
	t->curr_ret_stack = -1;
	/*
	 * The idle task has no parent, it either has its own
	 * stack or no stack at all.
	 */
	if (t->ret_stack)
		WARN_ON(t->ret_stack != per_cpu(idle_ret_stack, cpu));

	if (ftrace_graph_active) {
		struct ftrace_ret_stack *ret_stack;

		ret_stack = per_cpu(idle_ret_stack, cpu);
		if (!ret_stack) {
			ret_stack = kmalloc(FTRACE_RETFUNC_DEPTH
					    * sizeof(struct ftrace_ret_stack),
					    GFP_KERNEL);
			if (!ret_stack)
				return;
			per_cpu(idle_ret_stack, cpu) = ret_stack;
		}
		graph_init_task(t, ret_stack);
	}
}

/* Allocate a return stack for newly created task */
void ftrace_graph_init_task(struct task_struct *t)
{
	/* Make sure we do not use the parent ret_stack */
	t->ret_stack = NULL;
	t->curr_ret_stack = -1;

	if (ftrace_graph_active) {
		struct ftrace_ret_stack *ret_stack;

		ret_stack = kmalloc(FTRACE_RETFUNC_DEPTH
				* sizeof(struct ftrace_ret_stack),
				GFP_KERNEL);
		if (!ret_stack)
			return;
		graph_init_task(t, ret_stack);
	}
}

void ftrace_graph_exit_task(struct task_struct *t)
{
	struct ftrace_ret_stack	*ret_stack = t->ret_stack;

	t->ret_stack = NULL;
	/* NULL must become visible to IRQs before we free it: */
	barrier();

	kfree(ret_stack);
}

void ftrace_graph_stop(void)
{
	ftrace_stop();
}
#endif
