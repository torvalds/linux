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
#include <linux/seq_file.h>
#include <linux/suspend.h>
#include <linux/debugfs.h>
#include <linux/hardirq.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/sysctl.h>
#include <linux/ctype.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/rcupdate.h>

#include <trace/events/sched.h>

#include <asm/ftrace.h>
#include <asm/setup.h>

#include "trace_output.h"
#include "trace_stat.h"

#define FTRACE_WARN_ON(cond)			\
	do {					\
		if (WARN_ON(cond))		\
			ftrace_kill();		\
	} while (0)

#define FTRACE_WARN_ON_ONCE(cond)		\
	do {					\
		if (WARN_ON_ONCE(cond))		\
			ftrace_kill();		\
	} while (0)

/* hash bits for specific function selection */
#define FTRACE_HASH_BITS 7
#define FTRACE_FUNC_HASHSIZE (1 << FTRACE_HASH_BITS)

/* ftrace_enabled is a method to turn ftrace on or off */
int ftrace_enabled __read_mostly;
static int last_ftrace_enabled;

/* Quick disabling of function tracer. */
int function_trace_stop;

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

static struct ftrace_ops ftrace_list_end __read_mostly =
{
	.func		= ftrace_stub,
};

static struct ftrace_ops *ftrace_list __read_mostly = &ftrace_list_end;
ftrace_func_t ftrace_trace_function __read_mostly = ftrace_stub;
ftrace_func_t __ftrace_trace_function __read_mostly = ftrace_stub;
ftrace_func_t ftrace_pid_function __read_mostly = ftrace_stub;

/*
 * Traverse the ftrace_list, invoking all entries.  The reason that we
 * can use rcu_dereference_raw() is that elements removed from this list
 * are simply leaked, so there is no need to interact with a grace-period
 * mechanism.  The rcu_dereference_raw() calls are needed to handle
 * concurrent insertions into the ftrace_list.
 *
 * Silly Alpha and silly pointer-speculation compiler optimizations!
 */
static void ftrace_list_func(unsigned long ip, unsigned long parent_ip)
{
	struct ftrace_ops *op = rcu_dereference_raw(ftrace_list); /*see above*/

	while (op != &ftrace_list_end) {
		op->func(ip, parent_ip);
		op = rcu_dereference_raw(op->next); /*see above*/
	};
}

static void ftrace_pid_func(unsigned long ip, unsigned long parent_ip)
{
	if (!test_tsk_trace_trace(current))
		return;

	ftrace_pid_function(ip, parent_ip);
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
	__ftrace_trace_function = ftrace_stub;
	ftrace_pid_function = ftrace_stub;
}

#ifndef CONFIG_HAVE_FUNCTION_TRACE_MCOUNT_TEST
/*
 * For those archs that do not test ftrace_trace_stop in their
 * mcount call site, we need to do it from C.
 */
static void ftrace_test_stop_func(unsigned long ip, unsigned long parent_ip)
{
	if (function_trace_stop)
		return;

	__ftrace_trace_function(ip, parent_ip);
}
#endif

static int __register_ftrace_function(struct ftrace_ops *ops)
{
	ops->next = ftrace_list;
	/*
	 * We are entering ops into the ftrace_list but another
	 * CPU might be walking that list. We need to make sure
	 * the ops->next pointer is valid before another CPU sees
	 * the ops pointer included into the ftrace_list.
	 */
	rcu_assign_pointer(ftrace_list, ops);

	if (ftrace_enabled) {
		ftrace_func_t func;

		if (ops->next == &ftrace_list_end)
			func = ops->func;
		else
			func = ftrace_list_func;

		if (!list_empty(&ftrace_pids)) {
			set_ftrace_pid_function(func);
			func = ftrace_pid_func;
		}

		/*
		 * For one func, simply call it directly.
		 * For more than one func, call the chain.
		 */
#ifdef CONFIG_HAVE_FUNCTION_TRACE_MCOUNT_TEST
		ftrace_trace_function = func;
#else
		__ftrace_trace_function = func;
		ftrace_trace_function = ftrace_test_stop_func;
#endif
	}

	return 0;
}

static int __unregister_ftrace_function(struct ftrace_ops *ops)
{
	struct ftrace_ops **p;

	/*
	 * If we are removing the last function, then simply point
	 * to the ftrace_stub.
	 */
	if (ftrace_list == ops && ops->next == &ftrace_list_end) {
		ftrace_trace_function = ftrace_stub;
		ftrace_list = &ftrace_list_end;
		return 0;
	}

	for (p = &ftrace_list; *p != &ftrace_list_end; p = &(*p)->next)
		if (*p == ops)
			break;

	if (*p != ops)
		return -1;

	*p = (*p)->next;

	if (ftrace_enabled) {
		/* If we only have one func left, then call that directly */
		if (ftrace_list->next == &ftrace_list_end) {
			ftrace_func_t func = ftrace_list->func;

			if (!list_empty(&ftrace_pids)) {
				set_ftrace_pid_function(func);
				func = ftrace_pid_func;
			}
#ifdef CONFIG_HAVE_FUNCTION_TRACE_MCOUNT_TEST
			ftrace_trace_function = func;
#else
			__ftrace_trace_function = func;
#endif
		}
	}

	return 0;
}

static void ftrace_update_pid_func(void)
{
	ftrace_func_t func;

	if (ftrace_trace_function == ftrace_stub)
		return;

#ifdef CONFIG_HAVE_FUNCTION_TRACE_MCOUNT_TEST
	func = ftrace_trace_function;
#else
	func = __ftrace_trace_function;
#endif

	if (!list_empty(&ftrace_pids)) {
		set_ftrace_pid_function(func);
		func = ftrace_pid_func;
	} else {
		if (func == ftrace_pid_func)
			func = ftrace_pid_function;
	}

#ifdef CONFIG_HAVE_FUNCTION_TRACE_MCOUNT_TEST
	ftrace_trace_function = func;
#else
	__ftrace_trace_function = func;
#endif
}

#ifdef CONFIG_FUNCTION_PROFILER
struct ftrace_profile {
	struct hlist_node		node;
	unsigned long			ip;
	unsigned long			counter;
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	unsigned long long		time;
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

static int ftrace_profile_bits __read_mostly;
static int ftrace_profile_enabled __read_mostly;

/* ftrace_profile_lock - synchronize the enable and disable of the profiler */
static DEFINE_MUTEX(ftrace_profile_lock);

static DEFINE_PER_CPU(struct ftrace_profile_stat, ftrace_profile_stats);

#define FTRACE_PROFILE_HASH_SIZE 1024 /* must be power of 2 */

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
		   "Hit    Time            Avg\n"
		      "  --------                               "
		   "---    ----            ---\n");
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
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	static DEFINE_MUTEX(mutex);
	static struct trace_seq s;
	unsigned long long avg;
#endif

	kallsyms_lookup(rec->ip, NULL, NULL, NULL, str);
	seq_printf(m, "  %-30.30s  %10lu", str, rec->counter);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	seq_printf(m, "    ");
	avg = rec->time;
	do_div(avg, rec->counter);

	mutex_lock(&mutex);
	trace_seq_init(&s);
	trace_print_graph_duration(rec->time, &s);
	trace_seq_puts(&s, "    ");
	trace_print_graph_duration(avg, &s);
	trace_print_seq(m, &s);
	mutex_unlock(&mutex);
#endif
	seq_putc(m, '\n');

	return 0;
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

	for (i = 0; i < pages; i++) {
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

	free_page((unsigned long)stat->pages);
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

	if (!ftrace_profile_bits) {
		size--;

		for (; size; size >>= 1)
			ftrace_profile_bits++;
	}

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

	for_each_online_cpu(cpu) {
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
	struct hlist_node *n;
	unsigned long key;

	key = hash_long(ip, ftrace_profile_bits);
	hhd = &stat->hash[key];

	if (hlist_empty(hhd))
		return NULL;

	hlist_for_each_entry_rcu(rec, n, hhd, node) {
		if (rec->ip == ip)
			return rec;
	}

	return NULL;
}

static void ftrace_add_profile(struct ftrace_profile_stat *stat,
			       struct ftrace_profile *rec)
{
	unsigned long key;

	key = hash_long(rec->ip, ftrace_profile_bits);
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
function_profile_call(unsigned long ip, unsigned long parent_ip)
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
	function_profile_call(trace->func, 0);
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
	if (rec)
		rec->time += calltime;

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
static struct ftrace_ops ftrace_profile_ops __read_mostly =
{
	.func		= function_profile_call,
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
	char buf[64];		/* big enough to hold a number */
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
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
	struct rcu_head		rcu;
};

enum {
	FTRACE_ENABLE_CALLS		= (1 << 0),
	FTRACE_DISABLE_CALLS		= (1 << 1),
	FTRACE_UPDATE_TRACE_FUNC	= (1 << 2),
	FTRACE_ENABLE_MCOUNT		= (1 << 3),
	FTRACE_DISABLE_MCOUNT		= (1 << 4),
	FTRACE_START_FUNC_RET		= (1 << 5),
	FTRACE_STOP_FUNC_RET		= (1 << 6),
};

static int ftrace_filtered;

static struct dyn_ftrace *ftrace_new_addrs;

static DEFINE_MUTEX(ftrace_regex_lock);

struct ftrace_page {
	struct ftrace_page	*next;
	int			index;
	struct dyn_ftrace	records[];
};

#define ENTRIES_PER_PAGE \
  ((PAGE_SIZE - sizeof(struct ftrace_page)) / sizeof(struct dyn_ftrace))

/* estimate from running different kernels */
#define NR_TO_INIT		10000

static struct ftrace_page	*ftrace_pages_start;
static struct ftrace_page	*ftrace_pages;

static struct dyn_ftrace *ftrace_free_records;

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

static void ftrace_free_rec(struct dyn_ftrace *rec)
{
	rec->freelist = ftrace_free_records;
	ftrace_free_records = rec;
	rec->flags |= FTRACE_FL_FREE;
}

static struct dyn_ftrace *ftrace_alloc_dyn_node(unsigned long ip)
{
	struct dyn_ftrace *rec;

	/* First check for freed records */
	if (ftrace_free_records) {
		rec = ftrace_free_records;

		if (unlikely(!(rec->flags & FTRACE_FL_FREE))) {
			FTRACE_WARN_ON_ONCE(1);
			ftrace_free_records = NULL;
			return NULL;
		}

		ftrace_free_records = rec->freelist;
		memset(rec, 0, sizeof(*rec));
		return rec;
	}

	if (ftrace_pages->index == ENTRIES_PER_PAGE) {
		if (!ftrace_pages->next) {
			/* allocate another page */
			ftrace_pages->next =
				(void *)get_zeroed_page(GFP_KERNEL);
			if (!ftrace_pages->next)
				return NULL;
		}
		ftrace_pages = ftrace_pages->next;
	}

	return &ftrace_pages->records[ftrace_pages->index++];
}

static struct dyn_ftrace *
ftrace_record_ip(unsigned long ip)
{
	struct dyn_ftrace *rec;

	if (ftrace_disabled)
		return NULL;

	rec = ftrace_alloc_dyn_node(ip);
	if (!rec)
		return NULL;

	rec->ip = ip;
	rec->newlist = ftrace_new_addrs;
	ftrace_new_addrs = rec;

	return rec;
}

static void print_ip_ins(const char *fmt, unsigned char *p)
{
	int i;

	printk(KERN_CONT "%s", fmt);

	for (i = 0; i < MCOUNT_INSN_SIZE; i++)
		printk(KERN_CONT "%s%02x", i ? ":" : "", p[i]);
}

static void ftrace_bug(int failed, unsigned long ip)
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


/* Return 1 if the address range is reserved for ftrace */
int ftrace_text_reserved(void *start, void *end)
{
	struct dyn_ftrace *rec;
	struct ftrace_page *pg;

	do_for_each_ftrace_rec(pg, rec) {
		if (rec->ip <= (unsigned long)end &&
		    rec->ip + MCOUNT_INSN_SIZE > (unsigned long)start)
			return 1;
	} while_for_each_ftrace_rec();
	return 0;
}


static int
__ftrace_replace_code(struct dyn_ftrace *rec, int enable)
{
	unsigned long ftrace_addr;
	unsigned long flag = 0UL;

	ftrace_addr = (unsigned long)FTRACE_ADDR;

	/*
	 * If this record is not to be traced or we want to disable it,
	 * then disable it.
	 *
	 * If we want to enable it and filtering is off, then enable it.
	 *
	 * If we want to enable it and filtering is on, enable it only if
	 * it's filtered
	 */
	if (enable && !(rec->flags & FTRACE_FL_NOTRACE)) {
		if (!ftrace_filtered || (rec->flags & FTRACE_FL_FILTER))
			flag = FTRACE_FL_ENABLED;
	}

	/* If the state of this record hasn't changed, then do nothing */
	if ((rec->flags & FTRACE_FL_ENABLED) == flag)
		return 0;

	if (flag) {
		rec->flags |= FTRACE_FL_ENABLED;
		return ftrace_make_call(rec, ftrace_addr);
	}

	rec->flags &= ~FTRACE_FL_ENABLED;
	return ftrace_make_nop(NULL, rec, ftrace_addr);
}

static void ftrace_replace_code(int enable)
{
	struct dyn_ftrace *rec;
	struct ftrace_page *pg;
	int failed;

	do_for_each_ftrace_rec(pg, rec) {
		/*
		 * Skip over free records, records that have
		 * failed and not converted.
		 */
		if (rec->flags & FTRACE_FL_FREE ||
		    rec->flags & FTRACE_FL_FAILED ||
		    !(rec->flags & FTRACE_FL_CONVERTED))
			continue;

		failed = __ftrace_replace_code(rec, enable);
		if (failed) {
			rec->flags |= FTRACE_FL_FAILED;
			ftrace_bug(failed, rec->ip);
			/* Stop processing */
			return;
		}
	} while_for_each_ftrace_rec();
}

static int
ftrace_code_disable(struct module *mod, struct dyn_ftrace *rec)
{
	unsigned long ip;
	int ret;

	ip = rec->ip;

	ret = ftrace_make_nop(mod, rec, MCOUNT_ADDR);
	if (ret) {
		ftrace_bug(ret, ip);
		rec->flags |= FTRACE_FL_FAILED;
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

static int __ftrace_modify_code(void *data)
{
	int *command = data;

	if (*command & FTRACE_ENABLE_CALLS)
		ftrace_replace_code(1);
	else if (*command & FTRACE_DISABLE_CALLS)
		ftrace_replace_code(0);

	if (*command & FTRACE_UPDATE_TRACE_FUNC)
		ftrace_update_ftrace_func(ftrace_trace_function);

	if (*command & FTRACE_START_FUNC_RET)
		ftrace_enable_ftrace_graph_caller();
	else if (*command & FTRACE_STOP_FUNC_RET)
		ftrace_disable_ftrace_graph_caller();

	return 0;
}

static void ftrace_run_update_code(int command)
{
	int ret;

	ret = ftrace_arch_code_modify_prepare();
	FTRACE_WARN_ON(ret);
	if (ret)
		return;

	stop_machine(__ftrace_modify_code, &command, NULL);

	ret = ftrace_arch_code_modify_post_process();
	FTRACE_WARN_ON(ret);
}

static ftrace_func_t saved_ftrace_func;
static int ftrace_start_up;

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

static void ftrace_startup(int command)
{
	if (unlikely(ftrace_disabled))
		return;

	ftrace_start_up++;
	command |= FTRACE_ENABLE_CALLS;

	ftrace_startup_enable(command);
}

static void ftrace_shutdown(int command)
{
	if (unlikely(ftrace_disabled))
		return;

	ftrace_start_up--;
	/*
	 * Just warn in case of unbalance, no need to kill ftrace, it's not
	 * critical but the ftrace_call callers may be never nopped again after
	 * further ftrace uses.
	 */
	WARN_ON_ONCE(ftrace_start_up < 0);

	if (!ftrace_start_up)
		command |= FTRACE_DISABLE_CALLS;

	if (saved_ftrace_func != ftrace_trace_function) {
		saved_ftrace_func = ftrace_trace_function;
		command |= FTRACE_UPDATE_TRACE_FUNC;
	}

	if (!command || !ftrace_enabled)
		return;

	ftrace_run_update_code(command);
}

static void ftrace_startup_sysctl(void)
{
	int command = FTRACE_ENABLE_MCOUNT;

	if (unlikely(ftrace_disabled))
		return;

	/* Force update next time */
	saved_ftrace_func = NULL;
	/* ftrace_start_up is true if we want ftrace running */
	if (ftrace_start_up)
		command |= FTRACE_ENABLE_CALLS;

	ftrace_run_update_code(command);
}

static void ftrace_shutdown_sysctl(void)
{
	int command = FTRACE_DISABLE_MCOUNT;

	if (unlikely(ftrace_disabled))
		return;

	/* ftrace_start_up is true if ftrace is running */
	if (ftrace_start_up)
		command |= FTRACE_DISABLE_CALLS;

	ftrace_run_update_code(command);
}

static cycle_t		ftrace_update_time;
static unsigned long	ftrace_update_cnt;
unsigned long		ftrace_update_tot_cnt;

static int ftrace_update_code(struct module *mod)
{
	struct dyn_ftrace *p;
	cycle_t start, stop;

	start = ftrace_now(raw_smp_processor_id());
	ftrace_update_cnt = 0;

	while (ftrace_new_addrs) {

		/* If something went wrong, bail without enabling anything */
		if (unlikely(ftrace_disabled))
			return -1;

		p = ftrace_new_addrs;
		ftrace_new_addrs = p->newlist;
		p->flags = 0L;

		/*
		 * Do the initial record convertion from mcount jump
		 * to the NOP instructions.
		 */
		if (!ftrace_code_disable(mod, p)) {
			ftrace_free_rec(p);
			continue;
		}

		p->flags |= FTRACE_FL_CONVERTED;
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
		if (ftrace_start_up) {
			int failed = __ftrace_replace_code(p, 1);
			if (failed) {
				ftrace_bug(failed, p->ip);
				ftrace_free_rec(p);
			}
		}
	}

	stop = ftrace_now(raw_smp_processor_id());
	ftrace_update_time = stop - start;
	ftrace_update_tot_cnt += ftrace_update_cnt;

	return 0;
}

static int __init ftrace_dyn_table_alloc(unsigned long num_to_init)
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

	cnt = num_to_init / ENTRIES_PER_PAGE;
	pr_info("ftrace: allocating %ld entries in %d pages\n",
		num_to_init, cnt + 1);

	for (i = 0; i < cnt; i++) {
		pg->next = (void *)get_zeroed_page(GFP_KERNEL);

		/* If we fail, we'll try later anyway */
		if (!pg->next)
			break;

		pg = pg->next;
	}

	return 0;
}

enum {
	FTRACE_ITER_FILTER	= (1 << 0),
	FTRACE_ITER_NOTRACE	= (1 << 1),
	FTRACE_ITER_FAILURES	= (1 << 2),
	FTRACE_ITER_PRINTALL	= (1 << 3),
	FTRACE_ITER_HASH	= (1 << 4),
};

#define FTRACE_BUFF_MAX (KSYM_SYMBOL_LEN+4) /* room for wildcards */

struct ftrace_iterator {
	struct ftrace_page	*pg;
	int			hidx;
	int			idx;
	unsigned		flags;
	struct trace_parser	parser;
};

static void *
t_hash_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	struct hlist_node *hnd = v;
	struct hlist_head *hhd;

	WARN_ON(!(iter->flags & FTRACE_ITER_HASH));

	(*pos)++;

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

	return hnd;
}

static void *t_hash_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	void *p = NULL;
	loff_t l;

	if (!(iter->flags & FTRACE_ITER_HASH))
		*pos = 0;

	iter->flags |= FTRACE_ITER_HASH;

	iter->hidx = 0;
	for (l = 0; l <= *pos; ) {
		p = t_hash_next(m, p, &l);
		if (!p)
			break;
	}
	return p;
}

static int t_hash_show(struct seq_file *m, void *v)
{
	struct ftrace_func_probe *rec;
	struct hlist_node *hnd = v;

	rec = hlist_entry(hnd, struct ftrace_func_probe, node);

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
	struct dyn_ftrace *rec = NULL;

	if (iter->flags & FTRACE_ITER_HASH)
		return t_hash_next(m, v, pos);

	(*pos)++;

	if (iter->flags & FTRACE_ITER_PRINTALL)
		return NULL;

 retry:
	if (iter->idx >= iter->pg->index) {
		if (iter->pg->next) {
			iter->pg = iter->pg->next;
			iter->idx = 0;
			goto retry;
		}
	} else {
		rec = &iter->pg->records[iter->idx++];
		if ((rec->flags & FTRACE_FL_FREE) ||

		    (!(iter->flags & FTRACE_ITER_FAILURES) &&
		     (rec->flags & FTRACE_FL_FAILED)) ||

		    ((iter->flags & FTRACE_ITER_FAILURES) &&
		     !(rec->flags & FTRACE_FL_FAILED)) ||

		    ((iter->flags & FTRACE_ITER_FILTER) &&
		     !(rec->flags & FTRACE_FL_FILTER)) ||

		    ((iter->flags & FTRACE_ITER_NOTRACE) &&
		     !(rec->flags & FTRACE_FL_NOTRACE))) {
			rec = NULL;
			goto retry;
		}
	}

	return rec;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	void *p = NULL;
	loff_t l;

	mutex_lock(&ftrace_lock);
	/*
	 * For set_ftrace_filter reading, if we have the filter
	 * off, we can short cut and just print out that all
	 * functions are enabled.
	 */
	if (iter->flags & FTRACE_ITER_FILTER && !ftrace_filtered) {
		if (*pos > 0)
			return t_hash_start(m, pos);
		iter->flags |= FTRACE_ITER_PRINTALL;
		return iter;
	}

	if (iter->flags & FTRACE_ITER_HASH)
		return t_hash_start(m, pos);

	iter->pg = ftrace_pages_start;
	iter->idx = 0;
	for (l = 0; l <= *pos; ) {
		p = t_next(m, p, &l);
		if (!p)
			break;
	}

	if (!p && iter->flags & FTRACE_ITER_FILTER)
		return t_hash_start(m, pos);

	return p;
}

static void t_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&ftrace_lock);
}

static int t_show(struct seq_file *m, void *v)
{
	struct ftrace_iterator *iter = m->private;
	struct dyn_ftrace *rec = v;

	if (iter->flags & FTRACE_ITER_HASH)
		return t_hash_show(m, v);

	if (iter->flags & FTRACE_ITER_PRINTALL) {
		seq_printf(m, "#### all functions enabled ####\n");
		return 0;
	}

	if (!rec)
		return 0;

	seq_printf(m, "%ps\n", (void *)rec->ip);

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
	int ret;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	iter->pg = ftrace_pages_start;

	ret = seq_open(file, &show_ftrace_seq_ops);
	if (!ret) {
		struct seq_file *m = file->private_data;

		m->private = iter;
	} else {
		kfree(iter);
	}

	return ret;
}

static int
ftrace_failures_open(struct inode *inode, struct file *file)
{
	int ret;
	struct seq_file *m;
	struct ftrace_iterator *iter;

	ret = ftrace_avail_open(inode, file);
	if (!ret) {
		m = (struct seq_file *)file->private_data;
		iter = (struct ftrace_iterator *)m->private;
		iter->flags = FTRACE_ITER_FAILURES;
	}

	return ret;
}


static void ftrace_filter_reset(int enable)
{
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	unsigned long type = enable ? FTRACE_FL_FILTER : FTRACE_FL_NOTRACE;

	mutex_lock(&ftrace_lock);
	if (enable)
		ftrace_filtered = 0;
	do_for_each_ftrace_rec(pg, rec) {
		if (rec->flags & FTRACE_FL_FAILED)
			continue;
		rec->flags &= ~type;
	} while_for_each_ftrace_rec();
	mutex_unlock(&ftrace_lock);
}

static int
ftrace_regex_open(struct inode *inode, struct file *file, int enable)
{
	struct ftrace_iterator *iter;
	int ret = 0;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	if (trace_parser_get_init(&iter->parser, FTRACE_BUFF_MAX)) {
		kfree(iter);
		return -ENOMEM;
	}

	mutex_lock(&ftrace_regex_lock);
	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC))
		ftrace_filter_reset(enable);

	if (file->f_mode & FMODE_READ) {
		iter->pg = ftrace_pages_start;
		iter->flags = enable ? FTRACE_ITER_FILTER :
			FTRACE_ITER_NOTRACE;

		ret = seq_open(file, &show_ftrace_seq_ops);
		if (!ret) {
			struct seq_file *m = file->private_data;
			m->private = iter;
		} else {
			trace_parser_put(&iter->parser);
			kfree(iter);
		}
	} else
		file->private_data = iter;
	mutex_unlock(&ftrace_regex_lock);

	return ret;
}

static int
ftrace_filter_open(struct inode *inode, struct file *file)
{
	return ftrace_regex_open(inode, file, 1);
}

static int
ftrace_notrace_open(struct inode *inode, struct file *file)
{
	return ftrace_regex_open(inode, file, 0);
}

static loff_t
ftrace_regex_lseek(struct file *file, loff_t offset, int origin)
{
	loff_t ret;

	if (file->f_mode & FMODE_READ)
		ret = seq_lseek(file, offset, origin);
	else
		file->f_pos = ret = 1;

	return ret;
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
ftrace_match_record(struct dyn_ftrace *rec, char *regex, int len, int type)
{
	char str[KSYM_SYMBOL_LEN];

	kallsyms_lookup(rec->ip, NULL, NULL, NULL, str);
	return ftrace_match(str, regex, len, type);
}

static int ftrace_match_records(char *buff, int len, int enable)
{
	unsigned int search_len;
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	unsigned long flag;
	char *search;
	int type;
	int not;
	int found = 0;

	flag = enable ? FTRACE_FL_FILTER : FTRACE_FL_NOTRACE;
	type = filter_parse_regex(buff, len, &search, &not);

	search_len = strlen(search);

	mutex_lock(&ftrace_lock);
	do_for_each_ftrace_rec(pg, rec) {

		if (rec->flags & FTRACE_FL_FAILED)
			continue;

		if (ftrace_match_record(rec, search, search_len, type)) {
			if (not)
				rec->flags &= ~flag;
			else
				rec->flags |= flag;
			found = 1;
		}
		/*
		 * Only enable filtering if we have a function that
		 * is filtered on.
		 */
		if (enable && (rec->flags & FTRACE_FL_FILTER))
			ftrace_filtered = 1;
	} while_for_each_ftrace_rec();
	mutex_unlock(&ftrace_lock);

	return found;
}

static int
ftrace_match_module_record(struct dyn_ftrace *rec, char *mod,
			   char *regex, int len, int type)
{
	char str[KSYM_SYMBOL_LEN];
	char *modname;

	kallsyms_lookup(rec->ip, NULL, NULL, &modname, str);

	if (!modname || strcmp(modname, mod))
		return 0;

	/* blank search means to match all funcs in the mod */
	if (len)
		return ftrace_match(str, regex, len, type);
	else
		return 1;
}

static int ftrace_match_module_records(char *buff, char *mod, int enable)
{
	unsigned search_len = 0;
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	int type = MATCH_FULL;
	char *search = buff;
	unsigned long flag;
	int not = 0;
	int found = 0;

	flag = enable ? FTRACE_FL_FILTER : FTRACE_FL_NOTRACE;

	/* blank or '*' mean the same */
	if (strcmp(buff, "*") == 0)
		buff[0] = 0;

	/* handle the case of 'dont filter this module' */
	if (strcmp(buff, "!") == 0 || strcmp(buff, "!*") == 0) {
		buff[0] = 0;
		not = 1;
	}

	if (strlen(buff)) {
		type = filter_parse_regex(buff, strlen(buff), &search, &not);
		search_len = strlen(search);
	}

	mutex_lock(&ftrace_lock);
	do_for_each_ftrace_rec(pg, rec) {

		if (rec->flags & FTRACE_FL_FAILED)
			continue;

		if (ftrace_match_module_record(rec, mod,
					       search, search_len, type)) {
			if (not)
				rec->flags &= ~flag;
			else
				rec->flags |= flag;
			found = 1;
		}
		if (enable && (rec->flags & FTRACE_FL_FILTER))
			ftrace_filtered = 1;

	} while_for_each_ftrace_rec();
	mutex_unlock(&ftrace_lock);

	return found;
}

/*
 * We register the module command as a template to show others how
 * to register the a command as well.
 */

static int
ftrace_mod_callback(char *func, char *cmd, char *param, int enable)
{
	char *mod;

	/*
	 * cmd == 'mod' because we only registered this func
	 * for the 'mod' ftrace_func_command.
	 * But if you register one func with multiple commands,
	 * you can tell which command was used by the cmd
	 * parameter.
	 */

	/* we must have a module name */
	if (!param)
		return -EINVAL;

	mod = strsep(&param, ":");
	if (!strlen(mod))
		return -EINVAL;

	if (ftrace_match_module_records(func, mod, enable))
		return 0;
	return -EINVAL;
}

static struct ftrace_func_command ftrace_mod_cmd = {
	.name			= "mod",
	.func			= ftrace_mod_callback,
};

static int __init ftrace_mod_cmd_init(void)
{
	return register_ftrace_command(&ftrace_mod_cmd);
}
device_initcall(ftrace_mod_cmd_init);

static void
function_trace_probe_call(unsigned long ip, unsigned long parent_ip)
{
	struct ftrace_func_probe *entry;
	struct hlist_head *hhd;
	struct hlist_node *n;
	unsigned long key;
	int resched;

	key = hash_long(ip, FTRACE_HASH_BITS);

	hhd = &ftrace_func_hash[key];

	if (hlist_empty(hhd))
		return;

	/*
	 * Disable preemption for these calls to prevent a RCU grace
	 * period. This syncs the hash iteration and freeing of items
	 * on the hash. rcu_read_lock is too dangerous here.
	 */
	resched = ftrace_preempt_disable();
	hlist_for_each_entry_rcu(entry, n, hhd, node) {
		if (entry->ip == ip)
			entry->ops->func(ip, parent_ip, &entry->data);
	}
	ftrace_preempt_enable(resched);
}

static struct ftrace_ops trace_probe_ops __read_mostly =
{
	.func		= function_trace_probe_call,
};

static int ftrace_probe_registered;

static void __enable_ftrace_function_probe(void)
{
	int i;

	if (ftrace_probe_registered)
		return;

	for (i = 0; i < FTRACE_FUNC_HASHSIZE; i++) {
		struct hlist_head *hhd = &ftrace_func_hash[i];
		if (hhd->first)
			break;
	}
	/* Nothing registered? */
	if (i == FTRACE_FUNC_HASHSIZE)
		return;

	__register_ftrace_function(&trace_probe_ops);
	ftrace_startup(0);
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
	__unregister_ftrace_function(&trace_probe_ops);
	ftrace_shutdown(0);
	ftrace_probe_registered = 0;
}


static void ftrace_free_entry_rcu(struct rcu_head *rhp)
{
	struct ftrace_func_probe *entry =
		container_of(rhp, struct ftrace_func_probe, rcu);

	if (entry->ops->free)
		entry->ops->free(&entry->data);
	kfree(entry);
}


int
register_ftrace_function_probe(char *glob, struct ftrace_probe_ops *ops,
			      void *data)
{
	struct ftrace_func_probe *entry;
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	int type, len, not;
	unsigned long key;
	int count = 0;
	char *search;

	type = filter_parse_regex(glob, strlen(glob), &search, &not);
	len = strlen(search);

	/* we do not support '!' for function probes */
	if (WARN_ON(not))
		return -EINVAL;

	mutex_lock(&ftrace_lock);
	do_for_each_ftrace_rec(pg, rec) {

		if (rec->flags & FTRACE_FL_FAILED)
			continue;

		if (!ftrace_match_record(rec, search, len, type))
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
		if (ops->callback) {
			if (ops->callback(rec->ip, &entry->data) < 0) {
				/* caller does not like this func */
				kfree(entry);
				continue;
			}
		}

		entry->ops = ops;
		entry->ip = rec->ip;

		key = hash_long(entry->ip, FTRACE_HASH_BITS);
		hlist_add_head_rcu(&entry->node, &ftrace_func_hash[key]);

	} while_for_each_ftrace_rec();
	__enable_ftrace_function_probe();

 out_unlock:
	mutex_unlock(&ftrace_lock);

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
	struct ftrace_func_probe *entry;
	struct hlist_node *n, *tmp;
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

	mutex_lock(&ftrace_lock);
	for (i = 0; i < FTRACE_FUNC_HASHSIZE; i++) {
		struct hlist_head *hhd = &ftrace_func_hash[i];

		hlist_for_each_entry_safe(entry, n, tmp, hhd, node) {

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

			hlist_del(&entry->node);
			call_rcu(&entry->rcu, ftrace_free_entry_rcu);
		}
	}
	__disable_ftrace_function_probe();
	mutex_unlock(&ftrace_lock);
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

int register_ftrace_command(struct ftrace_func_command *cmd)
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

int unregister_ftrace_command(struct ftrace_func_command *cmd)
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

static int ftrace_process_regex(char *buff, int len, int enable)
{
	char *func, *command, *next = buff;
	struct ftrace_func_command *p;
	int ret = -EINVAL;

	func = strsep(&next, ":");

	if (!next) {
		if (ftrace_match_records(func, len, enable))
			return 0;
		return ret;
	}

	/* command found */

	command = strsep(&next, ":");

	mutex_lock(&ftrace_cmd_mutex);
	list_for_each_entry(p, &ftrace_commands, list) {
		if (strcmp(p->name, command) == 0) {
			ret = p->func(func, command, next, enable);
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

	mutex_lock(&ftrace_regex_lock);

	if (file->f_mode & FMODE_READ) {
		struct seq_file *m = file->private_data;
		iter = m->private;
	} else
		iter = file->private_data;

	parser = &iter->parser;
	read = trace_get_user(parser, ubuf, cnt, ppos);

	if (read >= 0 && trace_parser_loaded(parser) &&
	    !trace_parser_cont(parser)) {
		ret = ftrace_process_regex(parser->buffer,
					   parser->idx, enable);
		trace_parser_clear(parser);
		if (ret)
			goto out_unlock;
	}

	ret = read;
out_unlock:
	mutex_unlock(&ftrace_regex_lock);

	return ret;
}

static ssize_t
ftrace_filter_write(struct file *file, const char __user *ubuf,
		    size_t cnt, loff_t *ppos)
{
	return ftrace_regex_write(file, ubuf, cnt, ppos, 1);
}

static ssize_t
ftrace_notrace_write(struct file *file, const char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	return ftrace_regex_write(file, ubuf, cnt, ppos, 0);
}

static void
ftrace_set_regex(unsigned char *buf, int len, int reset, int enable)
{
	if (unlikely(ftrace_disabled))
		return;

	mutex_lock(&ftrace_regex_lock);
	if (reset)
		ftrace_filter_reset(enable);
	if (buf)
		ftrace_match_records(buf, len, enable);
	mutex_unlock(&ftrace_regex_lock);
}

/**
 * ftrace_set_filter - set a function to filter on in ftrace
 * @buf - the string that holds the function filter text.
 * @len - the length of the string.
 * @reset - non zero to reset all filters before applying this filter.
 *
 * Filters denote which functions should be enabled when tracing is enabled.
 * If @buf is NULL and reset is set, all functions will be enabled for tracing.
 */
void ftrace_set_filter(unsigned char *buf, int len, int reset)
{
	ftrace_set_regex(buf, len, reset, 1);
}

/**
 * ftrace_set_notrace - set a function to not trace in ftrace
 * @buf - the string that holds the function notrace text.
 * @len - the length of the string.
 * @reset - non zero to reset all filters before applying this filter.
 *
 * Notrace Filters denote which functions should not be enabled when tracing
 * is enabled. If @buf is NULL and reset is set, all functions will be enabled
 * for tracing.
 */
void ftrace_set_notrace(unsigned char *buf, int len, int reset)
{
	ftrace_set_regex(buf, len, reset, 0);
}

/*
 * command line interface to allow users to set filters on boot up.
 */
#define FTRACE_FILTER_SIZE		COMMAND_LINE_SIZE
static char ftrace_notrace_buf[FTRACE_FILTER_SIZE] __initdata;
static char ftrace_filter_buf[FTRACE_FILTER_SIZE] __initdata;

static int __init set_ftrace_notrace(char *str)
{
	strncpy(ftrace_notrace_buf, str, FTRACE_FILTER_SIZE);
	return 1;
}
__setup("ftrace_notrace=", set_ftrace_notrace);

static int __init set_ftrace_filter(char *str)
{
	strncpy(ftrace_filter_buf, str, FTRACE_FILTER_SIZE);
	return 1;
}
__setup("ftrace_filter=", set_ftrace_filter);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static char ftrace_graph_buf[FTRACE_FILTER_SIZE] __initdata;
static int ftrace_set_func(unsigned long *array, int *idx, char *buffer);

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
				      func);
		if (ret)
			printk(KERN_DEBUG "ftrace: function %s not "
					  "traceable\n", func);
	}
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

static void __init set_ftrace_early_filter(char *buf, int enable)
{
	char *func;

	while (buf) {
		func = strsep(&buf, ",");
		ftrace_set_regex(func, strlen(func), 0, enable);
	}
}

static void __init set_ftrace_early_filters(void)
{
	if (ftrace_filter_buf[0])
		set_ftrace_early_filter(ftrace_filter_buf, 1);
	if (ftrace_notrace_buf[0])
		set_ftrace_early_filter(ftrace_notrace_buf, 0);
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (ftrace_graph_buf[0])
		set_ftrace_early_graph(ftrace_graph_buf);
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
}

static int
ftrace_regex_release(struct inode *inode, struct file *file, int enable)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct ftrace_iterator *iter;
	struct trace_parser *parser;

	mutex_lock(&ftrace_regex_lock);
	if (file->f_mode & FMODE_READ) {
		iter = m->private;

		seq_release(inode, file);
	} else
		iter = file->private_data;

	parser = &iter->parser;
	if (trace_parser_loaded(parser)) {
		parser->buffer[parser->idx] = 0;
		ftrace_match_records(parser->buffer, parser->idx, enable);
	}

	mutex_lock(&ftrace_lock);
	if (ftrace_start_up && ftrace_enabled)
		ftrace_run_update_code(FTRACE_ENABLE_CALLS);
	mutex_unlock(&ftrace_lock);

	trace_parser_put(parser);
	kfree(iter);

	mutex_unlock(&ftrace_regex_lock);
	return 0;
}

static int
ftrace_filter_release(struct inode *inode, struct file *file)
{
	return ftrace_regex_release(inode, file, 1);
}

static int
ftrace_notrace_release(struct inode *inode, struct file *file)
{
	return ftrace_regex_release(inode, file, 0);
}

static const struct file_operations ftrace_avail_fops = {
	.open = ftrace_avail_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

static const struct file_operations ftrace_failures_fops = {
	.open = ftrace_failures_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

static const struct file_operations ftrace_filter_fops = {
	.open = ftrace_filter_open,
	.read = seq_read,
	.write = ftrace_filter_write,
	.llseek = ftrace_regex_lseek,
	.release = ftrace_filter_release,
};

static const struct file_operations ftrace_notrace_fops = {
	.open = ftrace_notrace_open,
	.read = seq_read,
	.write = ftrace_notrace_write,
	.llseek = ftrace_regex_lseek,
	.release = ftrace_notrace_release,
};

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

static DEFINE_MUTEX(graph_lock);

int ftrace_graph_count;
int ftrace_graph_filter_enabled;
unsigned long ftrace_graph_funcs[FTRACE_GRAPH_MAX_FUNCS] __read_mostly;

static void *
__g_next(struct seq_file *m, loff_t *pos)
{
	if (*pos >= ftrace_graph_count)
		return NULL;
	return &ftrace_graph_funcs[*pos];
}

static void *
g_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return __g_next(m, pos);
}

static void *g_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&graph_lock);

	/* Nothing, tell g_show to print all functions are enabled */
	if (!ftrace_graph_filter_enabled && !*pos)
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
ftrace_graph_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	mutex_lock(&graph_lock);
	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC)) {
		ftrace_graph_filter_enabled = 0;
		ftrace_graph_count = 0;
		memset(ftrace_graph_funcs, 0, sizeof(ftrace_graph_funcs));
	}
	mutex_unlock(&graph_lock);

	if (file->f_mode & FMODE_READ)
		ret = seq_open(file, &ftrace_graph_seq_ops);

	return ret;
}

static int
ftrace_graph_release(struct inode *inode, struct file *file)
{
	if (file->f_mode & FMODE_READ)
		seq_release(inode, file);
	return 0;
}

static int
ftrace_set_func(unsigned long *array, int *idx, char *buffer)
{
	struct dyn_ftrace *rec;
	struct ftrace_page *pg;
	int search_len;
	int fail = 1;
	int type, not;
	char *search;
	bool exists;
	int i;

	if (ftrace_disabled)
		return -ENODEV;

	/* decode regex */
	type = filter_parse_regex(buffer, strlen(buffer), &search, &not);
	if (!not && *idx >= FTRACE_GRAPH_MAX_FUNCS)
		return -EBUSY;

	search_len = strlen(search);

	mutex_lock(&ftrace_lock);
	do_for_each_ftrace_rec(pg, rec) {

		if (rec->flags & (FTRACE_FL_FAILED | FTRACE_FL_FREE))
			continue;

		if (ftrace_match_record(rec, search, search_len, type)) {
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
					if (*idx >= FTRACE_GRAPH_MAX_FUNCS)
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

	ftrace_graph_filter_enabled = 1;
	return 0;
}

static ssize_t
ftrace_graph_write(struct file *file, const char __user *ubuf,
		   size_t cnt, loff_t *ppos)
{
	struct trace_parser parser;
	ssize_t read, ret;

	if (!cnt)
		return 0;

	mutex_lock(&graph_lock);

	if (trace_parser_get_init(&parser, FTRACE_BUFF_MAX)) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	read = trace_get_user(&parser, ubuf, cnt, ppos);

	if (read >= 0 && trace_parser_loaded((&parser))) {
		parser.buffer[parser.idx] = 0;

		/* we allow only one expression at a time */
		ret = ftrace_set_func(ftrace_graph_funcs, &ftrace_graph_count,
					parser.buffer);
		if (ret)
			goto out_free;
	}

	ret = read;

out_free:
	trace_parser_put(&parser);
out_unlock:
	mutex_unlock(&graph_lock);

	return ret;
}

static const struct file_operations ftrace_graph_fops = {
	.open		= ftrace_graph_open,
	.read		= seq_read,
	.write		= ftrace_graph_write,
	.release	= ftrace_graph_release,
};
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

static __init int ftrace_init_dyn_debugfs(struct dentry *d_tracer)
{

	trace_create_file("available_filter_functions", 0444,
			d_tracer, NULL, &ftrace_avail_fops);

	trace_create_file("failures", 0444,
			d_tracer, NULL, &ftrace_failures_fops);

	trace_create_file("set_ftrace_filter", 0644, d_tracer,
			NULL, &ftrace_filter_fops);

	trace_create_file("set_ftrace_notrace", 0644, d_tracer,
				    NULL, &ftrace_notrace_fops);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	trace_create_file("set_graph_function", 0444, d_tracer,
				    NULL,
				    &ftrace_graph_fops);
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

	return 0;
}

static int ftrace_process_locs(struct module *mod,
			       unsigned long *start,
			       unsigned long *end)
{
	unsigned long *p;
	unsigned long addr;
	unsigned long flags;

	mutex_lock(&ftrace_lock);
	p = start;
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
		ftrace_record_ip(addr);
	}

	/* disable interrupts to prevent kstop machine */
	local_irq_save(flags);
	ftrace_update_code(mod);
	local_irq_restore(flags);
	mutex_unlock(&ftrace_lock);

	return 0;
}

#ifdef CONFIG_MODULES
void ftrace_release_mod(struct module *mod)
{
	struct dyn_ftrace *rec;
	struct ftrace_page *pg;

	if (ftrace_disabled)
		return;

	mutex_lock(&ftrace_lock);
	do_for_each_ftrace_rec(pg, rec) {
		if (within_module_core(rec->ip, mod)) {
			/*
			 * rec->ip is changed in ftrace_free_rec()
			 * It should not between s and e if record was freed.
			 */
			FTRACE_WARN_ON(rec->flags & FTRACE_FL_FREE);
			ftrace_free_rec(rec);
		}
	} while_for_each_ftrace_rec();
	mutex_unlock(&ftrace_lock);
}

static void ftrace_init_module(struct module *mod,
			       unsigned long *start, unsigned long *end)
{
	if (ftrace_disabled || start == end)
		return;
	ftrace_process_locs(mod, start, end);
}

static int ftrace_module_notify(struct notifier_block *self,
				unsigned long val, void *data)
{
	struct module *mod = data;

	switch (val) {
	case MODULE_STATE_COMING:
		ftrace_init_module(mod, mod->ftrace_callsites,
				   mod->ftrace_callsites +
				   mod->num_ftrace_callsites);
		break;
	case MODULE_STATE_GOING:
		ftrace_release_mod(mod);
		break;
	}

	return 0;
}
#else
static int ftrace_module_notify(struct notifier_block *self,
				unsigned long val, void *data)
{
	return 0;
}
#endif /* CONFIG_MODULES */

struct notifier_block ftrace_module_nb = {
	.notifier_call = ftrace_module_notify,
	.priority = 0,
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

	ret = register_module_notifier(&ftrace_module_nb);
	if (ret)
		pr_warning("Failed to register trace ftrace module notifier\n");

	set_ftrace_early_filters();

	return;
 failed:
	ftrace_disabled = 1;
}

#else

static int __init ftrace_nodyn_init(void)
{
	ftrace_enabled = 1;
	return 0;
}
device_initcall(ftrace_nodyn_init);

static inline int ftrace_init_dyn_debugfs(struct dentry *d_tracer) { return 0; }
static inline void ftrace_startup_enable(int command) { }
/* Keep as macros so we do not need to define the commands */
# define ftrace_startup(command)	do { } while (0)
# define ftrace_shutdown(command)	do { } while (0)
# define ftrace_startup_sysctl()	do { } while (0)
# define ftrace_shutdown_sysctl()	do { } while (0)
#endif /* CONFIG_DYNAMIC_FTRACE */

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

	ret = strict_strtol(tmp, 10, &val);
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
	.llseek		= seq_lseek,
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

	if (unlikely(ftrace_disabled))
		return -1;

	mutex_lock(&ftrace_lock);

	ret = __register_ftrace_function(ops);
	ftrace_startup(0);

	mutex_unlock(&ftrace_lock);
	return ret;
}

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
	ret = __unregister_ftrace_function(ops);
	ftrace_shutdown(0);
	mutex_unlock(&ftrace_lock);

	return ret;
}

int
ftrace_enable_sysctl(struct ctl_table *table, int write,
		     void __user *buffer, size_t *lenp,
		     loff_t *ppos)
{
	int ret;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	mutex_lock(&ftrace_lock);

	ret  = proc_dointvec(table, write, buffer, lenp, ppos);

	if (ret || !write || (last_ftrace_enabled == !!ftrace_enabled))
		goto out;

	last_ftrace_enabled = !!ftrace_enabled;

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
ftrace_graph_probe_sched_switch(struct rq *__rq, struct task_struct *prev,
				struct task_struct *next)
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
			ftrace_graph_init_task(idle_task(cpu));
	}

	do {
		ret = alloc_retstack_tasklist(ret_stack_list);
	} while (ret == -EAGAIN);

	if (!ret) {
		ret = register_trace_sched_switch(ftrace_graph_probe_sched_switch);
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
	ftrace_graph_entry = entryfunc;

	ftrace_startup(FTRACE_START_FUNC_RET);

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
	unregister_trace_sched_switch(ftrace_graph_probe_sched_switch);
	ftrace_graph_return = (trace_func_graph_ret_t)ftrace_stub;
	ftrace_graph_entry = ftrace_graph_entry_stub;
	ftrace_shutdown(FTRACE_STOP_FUNC_RET);
	unregister_pm_notifier(&ftrace_suspend_notifier);

 out:
	mutex_unlock(&ftrace_lock);
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
		atomic_set(&t->tracing_graph_pause, 0);
		atomic_set(&t->trace_overrun, 0);
		t->ftrace_timestamp = 0;
		/* make curr_ret_stack visable before we add the ret_stack */
		smp_wmb();
		t->ret_stack = ret_stack;
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
