// SPDX-License-Identifier: GPL-2.0
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
#include <linux/sched/task.h>
#include <linux/kallsyms.h>
#include <linux/security.h>
#include <linux/seq_file.h>
#include <linux/tracefs.h>
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
#include <linux/kprobes.h>

#include <trace/events/sched.h>

#include <asm/sections.h>
#include <asm/setup.h>

#include "ftrace_internal.h"
#include "trace_output.h"
#include "trace_stat.h"

#define FTRACE_INVALID_FUNCTION		"__ftrace_invalid_address__"

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
#define FTRACE_HASH_DEFAULT_BITS 10
#define FTRACE_HASH_MAX_BITS 12

#ifdef CONFIG_DYNAMIC_FTRACE
#define INIT_OPS_HASH(opsname)	\
	.func_hash		= &opsname.local_hash,			\
	.local_hash.regex_lock	= __MUTEX_INITIALIZER(opsname.local_hash.regex_lock),
#else
#define INIT_OPS_HASH(opsname)
#endif

enum {
	FTRACE_MODIFY_ENABLE_FL		= (1 << 0),
	FTRACE_MODIFY_MAY_SLEEP_FL	= (1 << 1),
};

struct ftrace_ops ftrace_list_end __read_mostly = {
	.func		= ftrace_stub,
	.flags		= FTRACE_OPS_FL_STUB,
	INIT_OPS_HASH(ftrace_list_end)
};

/* ftrace_enabled is a method to turn ftrace on or off */
int ftrace_enabled __read_mostly;
static int __maybe_unused last_ftrace_enabled;

/* Current function tracing op */
struct ftrace_ops *function_trace_op __read_mostly = &ftrace_list_end;
/* What to set function_trace_op to */
static struct ftrace_ops *set_function_trace_op;

static bool ftrace_pids_enabled(struct ftrace_ops *ops)
{
	struct trace_array *tr;

	if (!(ops->flags & FTRACE_OPS_FL_PID) || !ops->private)
		return false;

	tr = ops->private;

	return tr->function_pids != NULL || tr->function_no_pids != NULL;
}

static void ftrace_update_trampoline(struct ftrace_ops *ops);

/*
 * ftrace_disabled is set when an anomaly is discovered.
 * ftrace_disabled is much stronger than ftrace_enabled.
 */
static int ftrace_disabled __read_mostly;

DEFINE_MUTEX(ftrace_lock);

struct ftrace_ops __rcu *ftrace_ops_list __read_mostly = &ftrace_list_end;
ftrace_func_t ftrace_trace_function __read_mostly = ftrace_stub;
struct ftrace_ops global_ops;

/* Defined by vmlinux.lds.h see the comment above arch_ftrace_ops_list_func for details */
void ftrace_ops_list_func(unsigned long ip, unsigned long parent_ip,
			  struct ftrace_ops *op, struct ftrace_regs *fregs);

static inline void ftrace_ops_init(struct ftrace_ops *ops)
{
#ifdef CONFIG_DYNAMIC_FTRACE
	if (!(ops->flags & FTRACE_OPS_FL_INITIALIZED)) {
		mutex_init(&ops->local_hash.regex_lock);
		ops->func_hash = &ops->local_hash;
		ops->flags |= FTRACE_OPS_FL_INITIALIZED;
	}
#endif
}

static void ftrace_pid_func(unsigned long ip, unsigned long parent_ip,
			    struct ftrace_ops *op, struct ftrace_regs *fregs)
{
	struct trace_array *tr = op->private;
	int pid;

	if (tr) {
		pid = this_cpu_read(tr->array_buffer.data->ftrace_ignore_pid);
		if (pid == FTRACE_PID_IGNORE)
			return;
		if (pid != FTRACE_PID_TRACE &&
		    pid != current->pid)
			return;
	}

	op->saved_func(ip, parent_ip, op, fregs);
}

static void ftrace_sync_ipi(void *data)
{
	/* Probably not needed, but do it anyway */
	smp_rmb();
}

static ftrace_func_t ftrace_ops_get_list_func(struct ftrace_ops *ops)
{
	/*
	 * If this is a dynamic, RCU, or per CPU ops, or we force list func,
	 * then it needs to call the list anyway.
	 */
	if (ops->flags & (FTRACE_OPS_FL_DYNAMIC | FTRACE_OPS_FL_RCU) ||
	    FTRACE_FORCE_LIST_FUNC)
		return ftrace_ops_list_func;

	return ftrace_ops_get_func(ops);
}

static void update_ftrace_function(void)
{
	ftrace_func_t func;

	/*
	 * Prepare the ftrace_ops that the arch callback will use.
	 * If there's only one ftrace_ops registered, the ftrace_ops_list
	 * will point to the ops we want.
	 */
	set_function_trace_op = rcu_dereference_protected(ftrace_ops_list,
						lockdep_is_held(&ftrace_lock));

	/* If there's no ftrace_ops registered, just call the stub function */
	if (set_function_trace_op == &ftrace_list_end) {
		func = ftrace_stub;

	/*
	 * If we are at the end of the list and this ops is
	 * recursion safe and not dynamic and the arch supports passing ops,
	 * then have the mcount trampoline call the function directly.
	 */
	} else if (rcu_dereference_protected(ftrace_ops_list->next,
			lockdep_is_held(&ftrace_lock)) == &ftrace_list_end) {
		func = ftrace_ops_get_list_func(ftrace_ops_list);

	} else {
		/* Just use the default ftrace_ops */
		set_function_trace_op = &ftrace_list_end;
		func = ftrace_ops_list_func;
	}

	update_function_graph_func();

	/* If there's no change, then do nothing more here */
	if (ftrace_trace_function == func)
		return;

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
	 * we need to coordinate the setting of the function_trace_ops
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
	synchronize_rcu_tasks_rude();
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

static void add_ftrace_ops(struct ftrace_ops __rcu **list,
			   struct ftrace_ops *ops)
{
	rcu_assign_pointer(ops->next, *list);

	/*
	 * We are entering ops into the list but another
	 * CPU might be walking that list. We need to make sure
	 * the ops->next pointer is valid before another CPU sees
	 * the ops pointer included into the list.
	 */
	rcu_assign_pointer(*list, ops);
}

static int remove_ftrace_ops(struct ftrace_ops __rcu **list,
			     struct ftrace_ops *ops)
{
	struct ftrace_ops **p;

	/*
	 * If we are removing the last function, then simply point
	 * to the ftrace_stub.
	 */
	if (rcu_dereference_protected(*list,
			lockdep_is_held(&ftrace_lock)) == ops &&
	    rcu_dereference_protected(ops->next,
			lockdep_is_held(&ftrace_lock)) == &ftrace_list_end) {
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

static void ftrace_update_trampoline(struct ftrace_ops *ops);

int __register_ftrace_function(struct ftrace_ops *ops)
{
	if (ops->flags & FTRACE_OPS_FL_DELETED)
		return -EINVAL;

	if (WARN_ON(ops->flags & FTRACE_OPS_FL_ENABLED))
		return -EBUSY;

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
	if (!ftrace_enabled && (ops->flags & FTRACE_OPS_FL_PERMANENT))
		return -EBUSY;

	if (!is_kernel_core_data((unsigned long)ops))
		ops->flags |= FTRACE_OPS_FL_DYNAMIC;

	add_ftrace_ops(&ftrace_ops_list, ops);

	/* Always save the function, and reset at unregistering */
	ops->saved_func = ops->func;

	if (ftrace_pids_enabled(ops))
		ops->func = ftrace_pid_func;

	ftrace_update_trampoline(ops);

	if (ftrace_enabled)
		update_ftrace_function();

	return 0;
}

int __unregister_ftrace_function(struct ftrace_ops *ops)
{
	int ret;

	if (WARN_ON(!(ops->flags & FTRACE_OPS_FL_ENABLED)))
		return -EBUSY;

	ret = remove_ftrace_ops(&ftrace_ops_list, ops);

	if (ret < 0)
		return ret;

	if (ftrace_enabled)
		update_ftrace_function();

	ops->func = ops->saved_func;

	return 0;
}

static void ftrace_update_pid_func(void)
{
	struct ftrace_ops *op;

	/* Only do something if we are tracing something */
	if (ftrace_trace_function == ftrace_stub)
		return;

	do_for_each_ftrace_op(op, ftrace_ops_list) {
		if (op->flags & FTRACE_OPS_FL_PID) {
			op->func = ftrace_pids_enabled(op) ?
				ftrace_pid_func : op->saved_func;
			ftrace_update_trampoline(op);
		}
	} while_for_each_ftrace_op(op);

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
static int function_stat_cmp(const void *p1, const void *p2)
{
	const struct ftrace_profile *a = p1;
	const struct ftrace_profile *b = p2;

	if (a->time < b->time)
		return -1;
	if (a->time > b->time)
		return 1;
	else
		return 0;
}
#else
/* not function graph compares against hits */
static int function_stat_cmp(const void *p1, const void *p2)
{
	const struct ftrace_profile *a = p1;
	const struct ftrace_profile *b = p2;

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
	seq_puts(m, "  Function                               "
		 "Hit    Time            Avg             s^2\n"
		    "  --------                               "
		 "---    ----            ---             ---\n");
#else
	seq_puts(m, "  Function                               Hit\n"
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

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	avg = div64_ul(rec->time, rec->counter);
	if (tracing_thresh && (avg < tracing_thresh))
		goto out;
#endif

	kallsyms_lookup(rec->ip, NULL, NULL, NULL, str);
	seq_printf(m, "  %-30.30s  %10lu", str, rec->counter);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	seq_puts(m, "    ");

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
		stddev = div64_ul(stddev,
				  rec->counter * (rec->counter - 1) * 1000);
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

static int ftrace_profile_pages_init(struct ftrace_profile_stat *stat)
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

	stat->hash = kcalloc(size, sizeof(struct hlist_head), GFP_KERNEL);

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
		      struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct ftrace_profile_stat *stat;
	struct ftrace_profile *rec;
	unsigned long flags;

	if (!ftrace_profile_enabled)
		return;

	local_irq_save(flags);

	stat = this_cpu_ptr(&ftrace_profile_stats);
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
static bool fgraph_graph_time = true;

void ftrace_graph_graph_time_control(bool enable)
{
	fgraph_graph_time = enable;
}

static int profile_graph_entry(struct ftrace_graph_ent *trace)
{
	struct ftrace_ret_stack *ret_stack;

	function_profile_call(trace->func, 0, NULL, NULL);

	/* If function graph is shutting down, ret_stack can be NULL */
	if (!current->ret_stack)
		return 0;

	ret_stack = ftrace_graph_get_ret_stack(current, 0);
	if (ret_stack)
		ret_stack->subtime = 0;

	return 1;
}

static void profile_graph_return(struct ftrace_graph_ret *trace)
{
	struct ftrace_ret_stack *ret_stack;
	struct ftrace_profile_stat *stat;
	unsigned long long calltime;
	struct ftrace_profile *rec;
	unsigned long flags;

	local_irq_save(flags);
	stat = this_cpu_ptr(&ftrace_profile_stats);
	if (!stat->hash || !ftrace_profile_enabled)
		goto out;

	/* If the calltime was zero'd ignore it */
	if (!trace->calltime)
		goto out;

	calltime = trace->rettime - trace->calltime;

	if (!fgraph_graph_time) {

		/* Append this call time to the parent time to subtract */
		ret_stack = ftrace_graph_get_ret_stack(current, 1);
		if (ret_stack)
			ret_stack->subtime += calltime;

		ret_stack = ftrace_graph_get_ret_stack(current, 0);
		if (ret_stack && ret_stack->subtime < calltime)
			calltime -= ret_stack->subtime;
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

static struct fgraph_ops fprofiler_ops = {
	.entryfunc = &profile_graph_entry,
	.retfunc = &profile_graph_return,
};

static int register_ftrace_profiler(void)
{
	return register_ftrace_graph(&fprofiler_ops);
}

static void unregister_ftrace_profiler(void)
{
	unregister_ftrace_graph(&fprofiler_ops);
}
#else
static struct ftrace_ops ftrace_profile_ops __read_mostly = {
	.func		= function_profile_call,
	.flags		= FTRACE_OPS_FL_INITIALIZED,
	INIT_OPS_HASH(ftrace_profile_ops)
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
			 * so this acts like an synchronize_rcu.
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

static __init void ftrace_profile_tracefs(struct dentry *d_tracer)
{
	struct ftrace_profile_stat *stat;
	char *name;
	int ret;
	int cpu;

	for_each_possible_cpu(cpu) {
		stat = &per_cpu(ftrace_profile_stats, cpu);

		name = kasprintf(GFP_KERNEL, "function%d", cpu);
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

	trace_create_file("function_profile_enabled",
			  TRACE_MODE_WRITE, d_tracer, NULL,
			  &ftrace_profile_fops);
}

#else /* CONFIG_FUNCTION_PROFILER */
static __init void ftrace_profile_tracefs(struct dentry *d_tracer)
{
}
#endif /* CONFIG_FUNCTION_PROFILER */

#ifdef CONFIG_DYNAMIC_FTRACE

static struct ftrace_ops *removed_ops;

/*
 * Set when doing a global update, like enabling all recs or disabling them.
 * It is not set when just updating a single ftrace_ops.
 */
static bool update_all_ops;

#ifndef CONFIG_FTRACE_MCOUNT_RECORD
# error Dynamic ftrace depends on MCOUNT_RECORD
#endif

struct ftrace_func_probe {
	struct ftrace_probe_ops	*probe_ops;
	struct ftrace_ops	ops;
	struct trace_array	*tr;
	struct list_head	list;
	void			*data;
	int			ref;
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

struct ftrace_ops global_ops = {
	.func				= ftrace_stub,
	.local_hash.notrace_hash	= EMPTY_HASH,
	.local_hash.filter_hash		= EMPTY_HASH,
	INIT_OPS_HASH(global_ops)
	.flags				= FTRACE_OPS_FL_INITIALIZED |
					  FTRACE_OPS_FL_PID,
};

/*
 * Used by the stack unwinder to know about dynamic ftrace trampolines.
 */
struct ftrace_ops *ftrace_ops_trampoline(unsigned long addr)
{
	struct ftrace_ops *op = NULL;

	/*
	 * Some of the ops may be dynamically allocated,
	 * they are freed after a synchronize_rcu().
	 */
	preempt_disable_notrace();

	do_for_each_ftrace_op(op, ftrace_ops_list) {
		/*
		 * This is to check for dynamically allocated trampolines.
		 * Trampolines that are in kernel text will have
		 * core_kernel_text() return true.
		 */
		if (op->trampoline && op->trampoline_size)
			if (addr >= op->trampoline &&
			    addr < op->trampoline + op->trampoline_size) {
				preempt_enable_notrace();
				return op;
			}
	} while_for_each_ftrace_op(op);
	preempt_enable_notrace();

	return NULL;
}

/*
 * This is used by __kernel_text_address() to return true if the
 * address is on a dynamically allocated trampoline that would
 * not return true for either core_kernel_text() or
 * is_module_text_address().
 */
bool is_ftrace_trampoline(unsigned long addr)
{
	return ftrace_ops_trampoline(addr) != NULL;
}

struct ftrace_page {
	struct ftrace_page	*next;
	struct dyn_ftrace	*records;
	int			index;
	int			order;
};

#define ENTRY_SIZE sizeof(struct dyn_ftrace)
#define ENTRIES_PER_PAGE (PAGE_SIZE / ENTRY_SIZE)

static struct ftrace_page	*ftrace_pages_start;
static struct ftrace_page	*ftrace_pages;

static __always_inline unsigned long
ftrace_hash_key(struct ftrace_hash *hash, unsigned long ip)
{
	if (hash->size_bits > 0)
		return hash_long(ip, hash->size_bits);

	return 0;
}

/* Only use this function if ftrace_hash_empty() has already been tested */
static __always_inline struct ftrace_func_entry *
__ftrace_lookup_ip(struct ftrace_hash *hash, unsigned long ip)
{
	unsigned long key;
	struct ftrace_func_entry *entry;
	struct hlist_head *hhd;

	key = ftrace_hash_key(hash, ip);
	hhd = &hash->buckets[key];

	hlist_for_each_entry_rcu_notrace(entry, hhd, hlist) {
		if (entry->ip == ip)
			return entry;
	}
	return NULL;
}

/**
 * ftrace_lookup_ip - Test to see if an ip exists in an ftrace_hash
 * @hash: The hash to look at
 * @ip: The instruction pointer to test
 *
 * Search a given @hash to see if a given instruction pointer (@ip)
 * exists in it.
 *
 * Returns the entry that holds the @ip if found. NULL otherwise.
 */
struct ftrace_func_entry *
ftrace_lookup_ip(struct ftrace_hash *hash, unsigned long ip)
{
	if (ftrace_hash_empty(hash))
		return NULL;

	return __ftrace_lookup_ip(hash, ip);
}

static void __add_hash_entry(struct ftrace_hash *hash,
			     struct ftrace_func_entry *entry)
{
	struct hlist_head *hhd;
	unsigned long key;

	key = ftrace_hash_key(hash, entry->ip);
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
	hlist_del_rcu(&entry->hlist);
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

static void free_ftrace_mod(struct ftrace_mod_load *ftrace_mod)
{
	list_del(&ftrace_mod->list);
	kfree(ftrace_mod->module);
	kfree(ftrace_mod->func);
	kfree(ftrace_mod);
}

static void clear_ftrace_mod_list(struct list_head *head)
{
	struct ftrace_mod_load *p, *n;

	/* stack tracer isn't supported yet */
	if (!head)
		return;

	mutex_lock(&ftrace_lock);
	list_for_each_entry_safe(p, n, head, list)
		free_ftrace_mod(p);
	mutex_unlock(&ftrace_lock);
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
	call_rcu(&hash->rcu, __free_ftrace_hash_rcu);
}

void ftrace_free_filter(struct ftrace_ops *ops)
{
	ftrace_ops_init(ops);
	free_ftrace_hash(ops->func_hash->filter_hash);
	free_ftrace_hash(ops->func_hash->notrace_hash);
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


static int ftrace_add_mod(struct trace_array *tr,
			  const char *func, const char *module,
			  int enable)
{
	struct ftrace_mod_load *ftrace_mod;
	struct list_head *mod_head = enable ? &tr->mod_trace : &tr->mod_notrace;

	ftrace_mod = kzalloc(sizeof(*ftrace_mod), GFP_KERNEL);
	if (!ftrace_mod)
		return -ENOMEM;

	ftrace_mod->func = kstrdup(func, GFP_KERNEL);
	ftrace_mod->module = kstrdup(module, GFP_KERNEL);
	ftrace_mod->enable = enable;

	if (!ftrace_mod->func || !ftrace_mod->module)
		goto out_free;

	list_add(&ftrace_mod->list, mod_head);

	return 0;

 out_free:
	free_ftrace_mod(ftrace_mod);

	return -ENOMEM;
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

	if (hash)
		new_hash->flags = hash->flags;

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
ftrace_hash_rec_disable_modify(struct ftrace_ops *ops, int filter_hash);
static void
ftrace_hash_rec_enable_modify(struct ftrace_ops *ops, int filter_hash);

static int ftrace_hash_ipmodify_update(struct ftrace_ops *ops,
				       struct ftrace_hash *new_hash);

static struct ftrace_hash *dup_hash(struct ftrace_hash *src, int size)
{
	struct ftrace_func_entry *entry;
	struct ftrace_hash *new_hash;
	struct hlist_head *hhd;
	struct hlist_node *tn;
	int bits = 0;
	int i;

	/*
	 * Use around half the size (max bit of it), but
	 * a minimum of 2 is fine (as size of 0 or 1 both give 1 for bits).
	 */
	bits = fls(size / 2);

	/* Don't allocate too much */
	if (bits > FTRACE_HASH_MAX_BITS)
		bits = FTRACE_HASH_MAX_BITS;

	new_hash = alloc_ftrace_hash(bits);
	if (!new_hash)
		return NULL;

	new_hash->flags = src->flags;

	size = 1 << src->size_bits;
	for (i = 0; i < size; i++) {
		hhd = &src->buckets[i];
		hlist_for_each_entry_safe(entry, tn, hhd, hlist) {
			remove_hash_entry(src, entry);
			__add_hash_entry(new_hash, entry);
		}
	}
	return new_hash;
}

static struct ftrace_hash *
__ftrace_hash_move(struct ftrace_hash *src)
{
	int size = src->count;

	/*
	 * If the new source is empty, just return the empty_hash.
	 */
	if (ftrace_hash_empty(src))
		return EMPTY_HASH;

	return dup_hash(src, size);
}

static int
ftrace_hash_move(struct ftrace_ops *ops, int enable,
		 struct ftrace_hash **dst, struct ftrace_hash *src)
{
	struct ftrace_hash *new_hash;
	int ret;

	/* Reject setting notrace hash on IPMODIFY ftrace_ops */
	if (ops->flags & FTRACE_OPS_FL_IPMODIFY && !enable)
		return -EINVAL;

	new_hash = __ftrace_hash_move(src);
	if (!new_hash)
		return -ENOMEM;

	/* Make sure this can be applied if it is IPMODIFY ftrace_ops */
	if (enable) {
		/* IPMODIFY should be updated only when filter_hash updating */
		ret = ftrace_hash_ipmodify_update(ops, new_hash);
		if (ret < 0) {
			free_ftrace_hash(new_hash);
			return ret;
		}
	}

	/*
	 * Remove the current set, update the hash and add
	 * them back.
	 */
	ftrace_hash_rec_disable_modify(ops, enable);

	rcu_assign_pointer(*dst, new_hash);

	ftrace_hash_rec_enable_modify(ops, enable);

	return 0;
}

static bool hash_contains_ip(unsigned long ip,
			     struct ftrace_ops_hash *hash)
{
	/*
	 * The function record is a match if it exists in the filter
	 * hash and not in the notrace hash. Note, an empty hash is
	 * considered a match for the filter hash, but an empty
	 * notrace hash is considered not in the notrace hash.
	 */
	return (ftrace_hash_empty(hash->filter_hash) ||
		__ftrace_lookup_ip(hash->filter_hash, ip)) &&
		(ftrace_hash_empty(hash->notrace_hash) ||
		 !__ftrace_lookup_ip(hash->notrace_hash, ip));
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
 * the hashes are freed with call_rcu().
 */
int
ftrace_ops_test(struct ftrace_ops *ops, unsigned long ip, void *regs)
{
	struct ftrace_ops_hash hash;
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

	rcu_assign_pointer(hash.filter_hash, ops->func_hash->filter_hash);
	rcu_assign_pointer(hash.notrace_hash, ops->func_hash->notrace_hash);

	if (hash_contains_ip(ip, &hash))
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

static struct dyn_ftrace *lookup_rec(unsigned long start, unsigned long end)
{
	struct ftrace_page *pg;
	struct dyn_ftrace *rec = NULL;
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
			break;
	}
	return rec;
}

/**
 * ftrace_location_range - return the first address of a traced location
 *	if it touches the given ip range
 * @start: start of range to search.
 * @end: end of range to search (inclusive). @end points to the last byte
 *	to check.
 *
 * Returns rec->ip if the related ftrace location is a least partly within
 * the given address range. That is, the first address of the instruction
 * that is either a NOP or call to the function tracer. It checks the ftrace
 * internal tables to determine if the address belongs or not.
 */
unsigned long ftrace_location_range(unsigned long start, unsigned long end)
{
	struct dyn_ftrace *rec;

	rec = lookup_rec(start, end);
	if (rec)
		return rec->ip;

	return 0;
}

/**
 * ftrace_location - return the ftrace location
 * @ip: the instruction pointer to check
 *
 * If @ip matches the ftrace location, return @ip.
 * If @ip matches sym+0, return sym's ftrace location.
 * Otherwise, return 0.
 */
unsigned long ftrace_location(unsigned long ip)
{
	struct dyn_ftrace *rec;
	unsigned long offset;
	unsigned long size;

	rec = lookup_rec(ip, ip);
	if (!rec) {
		if (!kallsyms_lookup_size_offset(ip, &size, &offset))
			goto out;

		/* map sym+0 to __fentry__ */
		if (!offset)
			rec = lookup_rec(ip, ip + size - 1);
	}

	if (rec)
		return rec->ip;

out:
	return 0;
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
int ftrace_text_reserved(const void *start, const void *end)
{
	unsigned long ret;

	ret = ftrace_location_range((unsigned long)start,
				    (unsigned long)end);

	return (int)!!ret;
}

/* Test if ops registered to this rec needs regs */
static bool test_rec_ops_needs_regs(struct dyn_ftrace *rec)
{
	struct ftrace_ops *ops;
	bool keep_regs = false;

	for (ops = ftrace_ops_list;
	     ops != &ftrace_list_end; ops = ops->next) {
		/* pass rec in as regs to have non-NULL val */
		if (ftrace_ops_test(ops, rec->ip, rec)) {
			if (ops->flags & FTRACE_OPS_FL_SAVE_REGS) {
				keep_regs = true;
				break;
			}
		}
	}

	return  keep_regs;
}

static struct ftrace_ops *
ftrace_find_tramp_ops_any(struct dyn_ftrace *rec);
static struct ftrace_ops *
ftrace_find_tramp_ops_any_other(struct dyn_ftrace *rec, struct ftrace_ops *op_exclude);
static struct ftrace_ops *
ftrace_find_tramp_ops_next(struct dyn_ftrace *rec, struct ftrace_ops *ops);

static bool __ftrace_hash_rec_update(struct ftrace_ops *ops,
				     int filter_hash,
				     bool inc)
{
	struct ftrace_hash *hash;
	struct ftrace_hash *other_hash;
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	bool update = false;
	int count = 0;
	int all = false;

	/* Only update if the ops has been registered */
	if (!(ops->flags & FTRACE_OPS_FL_ENABLED))
		return false;

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
		hash = ops->func_hash->filter_hash;
		other_hash = ops->func_hash->notrace_hash;
		if (ftrace_hash_empty(hash))
			all = true;
	} else {
		inc = !inc;
		hash = ops->func_hash->notrace_hash;
		other_hash = ops->func_hash->filter_hash;
		/*
		 * If the notrace hash has no items,
		 * then there's nothing to do.
		 */
		if (ftrace_hash_empty(hash))
			return false;
	}

	do_for_each_ftrace_rec(pg, rec) {
		int in_other_hash = 0;
		int in_hash = 0;
		int match = 0;

		if (rec->flags & FTRACE_FL_DISABLED)
			continue;

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
			 * If filter_hash is set, we want to match all functions
			 * that are in the hash but not in the other hash.
			 *
			 * If filter_hash is not set, then we are decrementing.
			 * That means we match anything that is in the hash
			 * and also in the other_hash. That is, we need to turn
			 * off functions in the other hash because they are disabled
			 * by this hash.
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
			if (FTRACE_WARN_ON(ftrace_rec_count(rec) == FTRACE_REF_MAX))
				return false;

			if (ops->flags & FTRACE_OPS_FL_DIRECT)
				rec->flags |= FTRACE_FL_DIRECT;

			/*
			 * If there's only a single callback registered to a
			 * function, and the ops has a trampoline registered
			 * for it, then we can call it directly.
			 */
			if (ftrace_rec_count(rec) == 1 && ops->trampoline)
				rec->flags |= FTRACE_FL_TRAMP;
			else
				/*
				 * If we are adding another function callback
				 * to this function, and the previous had a
				 * custom trampoline in use, then we need to go
				 * back to the default trampoline.
				 */
				rec->flags &= ~FTRACE_FL_TRAMP;

			/*
			 * If any ops wants regs saved for this function
			 * then all ops will get saved regs.
			 */
			if (ops->flags & FTRACE_OPS_FL_SAVE_REGS)
				rec->flags |= FTRACE_FL_REGS;
		} else {
			if (FTRACE_WARN_ON(ftrace_rec_count(rec) == 0))
				return false;
			rec->flags--;

			/*
			 * Only the internal direct_ops should have the
			 * DIRECT flag set. Thus, if it is removing a
			 * function, then that function should no longer
			 * be direct.
			 */
			if (ops->flags & FTRACE_OPS_FL_DIRECT)
				rec->flags &= ~FTRACE_FL_DIRECT;

			/*
			 * If the rec had REGS enabled and the ops that is
			 * being removed had REGS set, then see if there is
			 * still any ops for this record that wants regs.
			 * If not, we can stop recording them.
			 */
			if (ftrace_rec_count(rec) > 0 &&
			    rec->flags & FTRACE_FL_REGS &&
			    ops->flags & FTRACE_OPS_FL_SAVE_REGS) {
				if (!test_rec_ops_needs_regs(rec))
					rec->flags &= ~FTRACE_FL_REGS;
			}

			/*
			 * The TRAMP needs to be set only if rec count
			 * is decremented to one, and the ops that is
			 * left has a trampoline. As TRAMP can only be
			 * enabled if there is only a single ops attached
			 * to it.
			 */
			if (ftrace_rec_count(rec) == 1 &&
			    ftrace_find_tramp_ops_any_other(rec, ops))
				rec->flags |= FTRACE_FL_TRAMP;
			else
				rec->flags &= ~FTRACE_FL_TRAMP;

			/*
			 * flags will be cleared in ftrace_check_record()
			 * if rec count is zero.
			 */
		}
		count++;

		/* Must match FTRACE_UPDATE_CALLS in ftrace_modify_all_code() */
		update |= ftrace_test_record(rec, true) != FTRACE_UPDATE_IGNORE;

		/* Shortcut, if we handled all records, we are done. */
		if (!all && count == hash->count)
			return update;
	} while_for_each_ftrace_rec();

	return update;
}

static bool ftrace_hash_rec_disable(struct ftrace_ops *ops,
				    int filter_hash)
{
	return __ftrace_hash_rec_update(ops, filter_hash, 0);
}

static bool ftrace_hash_rec_enable(struct ftrace_ops *ops,
				   int filter_hash)
{
	return __ftrace_hash_rec_update(ops, filter_hash, 1);
}

static void ftrace_hash_rec_update_modify(struct ftrace_ops *ops,
					  int filter_hash, int inc)
{
	struct ftrace_ops *op;

	__ftrace_hash_rec_update(ops, filter_hash, inc);

	if (ops->func_hash != &global_ops.local_hash)
		return;

	/*
	 * If the ops shares the global_ops hash, then we need to update
	 * all ops that are enabled and use this hash.
	 */
	do_for_each_ftrace_op(op, ftrace_ops_list) {
		/* Already done */
		if (op == ops)
			continue;
		if (op->func_hash == &global_ops.local_hash)
			__ftrace_hash_rec_update(op, filter_hash, inc);
	} while_for_each_ftrace_op(op);
}

static void ftrace_hash_rec_disable_modify(struct ftrace_ops *ops,
					   int filter_hash)
{
	ftrace_hash_rec_update_modify(ops, filter_hash, 0);
}

static void ftrace_hash_rec_enable_modify(struct ftrace_ops *ops,
					  int filter_hash)
{
	ftrace_hash_rec_update_modify(ops, filter_hash, 1);
}

/*
 * Try to update IPMODIFY flag on each ftrace_rec. Return 0 if it is OK
 * or no-needed to update, -EBUSY if it detects a conflict of the flag
 * on a ftrace_rec, and -EINVAL if the new_hash tries to trace all recs.
 * Note that old_hash and new_hash has below meanings
 *  - If the hash is NULL, it hits all recs (if IPMODIFY is set, this is rejected)
 *  - If the hash is EMPTY_HASH, it hits nothing
 *  - Anything else hits the recs which match the hash entries.
 */
static int __ftrace_hash_update_ipmodify(struct ftrace_ops *ops,
					 struct ftrace_hash *old_hash,
					 struct ftrace_hash *new_hash)
{
	struct ftrace_page *pg;
	struct dyn_ftrace *rec, *end = NULL;
	int in_old, in_new;

	/* Only update if the ops has been registered */
	if (!(ops->flags & FTRACE_OPS_FL_ENABLED))
		return 0;

	if (!(ops->flags & FTRACE_OPS_FL_IPMODIFY))
		return 0;

	/*
	 * Since the IPMODIFY is a very address sensitive action, we do not
	 * allow ftrace_ops to set all functions to new hash.
	 */
	if (!new_hash || !old_hash)
		return -EINVAL;

	/* Update rec->flags */
	do_for_each_ftrace_rec(pg, rec) {

		if (rec->flags & FTRACE_FL_DISABLED)
			continue;

		/* We need to update only differences of filter_hash */
		in_old = !!ftrace_lookup_ip(old_hash, rec->ip);
		in_new = !!ftrace_lookup_ip(new_hash, rec->ip);
		if (in_old == in_new)
			continue;

		if (in_new) {
			/* New entries must ensure no others are using it */
			if (rec->flags & FTRACE_FL_IPMODIFY)
				goto rollback;
			rec->flags |= FTRACE_FL_IPMODIFY;
		} else /* Removed entry */
			rec->flags &= ~FTRACE_FL_IPMODIFY;
	} while_for_each_ftrace_rec();

	return 0;

rollback:
	end = rec;

	/* Roll back what we did above */
	do_for_each_ftrace_rec(pg, rec) {

		if (rec->flags & FTRACE_FL_DISABLED)
			continue;

		if (rec == end)
			goto err_out;

		in_old = !!ftrace_lookup_ip(old_hash, rec->ip);
		in_new = !!ftrace_lookup_ip(new_hash, rec->ip);
		if (in_old == in_new)
			continue;

		if (in_new)
			rec->flags &= ~FTRACE_FL_IPMODIFY;
		else
			rec->flags |= FTRACE_FL_IPMODIFY;
	} while_for_each_ftrace_rec();

err_out:
	return -EBUSY;
}

static int ftrace_hash_ipmodify_enable(struct ftrace_ops *ops)
{
	struct ftrace_hash *hash = ops->func_hash->filter_hash;

	if (ftrace_hash_empty(hash))
		hash = NULL;

	return __ftrace_hash_update_ipmodify(ops, EMPTY_HASH, hash);
}

/* Disabling always succeeds */
static void ftrace_hash_ipmodify_disable(struct ftrace_ops *ops)
{
	struct ftrace_hash *hash = ops->func_hash->filter_hash;

	if (ftrace_hash_empty(hash))
		hash = NULL;

	__ftrace_hash_update_ipmodify(ops, hash, EMPTY_HASH);
}

static int ftrace_hash_ipmodify_update(struct ftrace_ops *ops,
				       struct ftrace_hash *new_hash)
{
	struct ftrace_hash *old_hash = ops->func_hash->filter_hash;

	if (ftrace_hash_empty(old_hash))
		old_hash = NULL;

	if (ftrace_hash_empty(new_hash))
		new_hash = NULL;

	return __ftrace_hash_update_ipmodify(ops, old_hash, new_hash);
}

static void print_ip_ins(const char *fmt, const unsigned char *p)
{
	char ins[MCOUNT_INSN_SIZE];
	int i;

	if (copy_from_kernel_nofault(ins, p, MCOUNT_INSN_SIZE)) {
		printk(KERN_CONT "%s[FAULT] %px\n", fmt, p);
		return;
	}

	printk(KERN_CONT "%s", fmt);

	for (i = 0; i < MCOUNT_INSN_SIZE; i++)
		printk(KERN_CONT "%s%02x", i ? ":" : "", ins[i]);
}

enum ftrace_bug_type ftrace_bug_type;
const void *ftrace_expected;

static void print_bug_type(void)
{
	switch (ftrace_bug_type) {
	case FTRACE_BUG_UNKNOWN:
		break;
	case FTRACE_BUG_INIT:
		pr_info("Initializing ftrace call sites\n");
		break;
	case FTRACE_BUG_NOP:
		pr_info("Setting ftrace call site to NOP\n");
		break;
	case FTRACE_BUG_CALL:
		pr_info("Setting ftrace call site to call ftrace function\n");
		break;
	case FTRACE_BUG_UPDATE:
		pr_info("Updating ftrace call site to call a different ftrace function\n");
		break;
	}
}

/**
 * ftrace_bug - report and shutdown function tracer
 * @failed: The failed type (EFAULT, EINVAL, EPERM)
 * @rec: The record that failed
 *
 * The arch code that enables or disables the function tracing
 * can call ftrace_bug() when it has detected a problem in
 * modifying the code. @failed should be one of either:
 * EFAULT - if the problem happens on reading the @ip address
 * EINVAL - if what is read at @ip is not what was expected
 * EPERM - if the problem happens on writing to the @ip address
 */
void ftrace_bug(int failed, struct dyn_ftrace *rec)
{
	unsigned long ip = rec ? rec->ip : 0;

	pr_info("------------[ ftrace bug ]------------\n");

	switch (failed) {
	case -EFAULT:
		pr_info("ftrace faulted on modifying ");
		print_ip_sym(KERN_INFO, ip);
		break;
	case -EINVAL:
		pr_info("ftrace failed to modify ");
		print_ip_sym(KERN_INFO, ip);
		print_ip_ins(" actual:   ", (unsigned char *)ip);
		pr_cont("\n");
		if (ftrace_expected) {
			print_ip_ins(" expected: ", ftrace_expected);
			pr_cont("\n");
		}
		break;
	case -EPERM:
		pr_info("ftrace faulted on writing ");
		print_ip_sym(KERN_INFO, ip);
		break;
	default:
		pr_info("ftrace faulted on unknown error ");
		print_ip_sym(KERN_INFO, ip);
	}
	print_bug_type();
	if (rec) {
		struct ftrace_ops *ops = NULL;

		pr_info("ftrace record flags: %lx\n", rec->flags);
		pr_cont(" (%ld)%s", ftrace_rec_count(rec),
			rec->flags & FTRACE_FL_REGS ? " R" : "  ");
		if (rec->flags & FTRACE_FL_TRAMP_EN) {
			ops = ftrace_find_tramp_ops_any(rec);
			if (ops) {
				do {
					pr_cont("\ttramp: %pS (%pS)",
						(void *)ops->trampoline,
						(void *)ops->func);
					ops = ftrace_find_tramp_ops_next(rec, ops);
				} while (ops);
			} else
				pr_cont("\ttramp: ERROR!");

		}
		ip = ftrace_get_addr_curr(rec);
		pr_cont("\n expected tramp: %lx\n", ip);
	}

	FTRACE_WARN_ON_ONCE(1);
}

static int ftrace_check_record(struct dyn_ftrace *rec, bool enable, bool update)
{
	unsigned long flag = 0UL;

	ftrace_bug_type = FTRACE_BUG_UNKNOWN;

	if (rec->flags & FTRACE_FL_DISABLED)
		return FTRACE_UPDATE_IGNORE;

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
	if (enable && ftrace_rec_count(rec))
		flag = FTRACE_FL_ENABLED;

	/*
	 * If enabling and the REGS flag does not match the REGS_EN, or
	 * the TRAMP flag doesn't match the TRAMP_EN, then do not ignore
	 * this record. Set flags to fail the compare against ENABLED.
	 * Same for direct calls.
	 */
	if (flag) {
		if (!(rec->flags & FTRACE_FL_REGS) !=
		    !(rec->flags & FTRACE_FL_REGS_EN))
			flag |= FTRACE_FL_REGS;

		if (!(rec->flags & FTRACE_FL_TRAMP) !=
		    !(rec->flags & FTRACE_FL_TRAMP_EN))
			flag |= FTRACE_FL_TRAMP;

		/*
		 * Direct calls are special, as count matters.
		 * We must test the record for direct, if the
		 * DIRECT and DIRECT_EN do not match, but only
		 * if the count is 1. That's because, if the
		 * count is something other than one, we do not
		 * want the direct enabled (it will be done via the
		 * direct helper). But if DIRECT_EN is set, and
		 * the count is not one, we need to clear it.
		 */
		if (ftrace_rec_count(rec) == 1) {
			if (!(rec->flags & FTRACE_FL_DIRECT) !=
			    !(rec->flags & FTRACE_FL_DIRECT_EN))
				flag |= FTRACE_FL_DIRECT;
		} else if (rec->flags & FTRACE_FL_DIRECT_EN) {
			flag |= FTRACE_FL_DIRECT;
		}
	}

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
			if (flag & FTRACE_FL_TRAMP) {
				if (rec->flags & FTRACE_FL_TRAMP)
					rec->flags |= FTRACE_FL_TRAMP_EN;
				else
					rec->flags &= ~FTRACE_FL_TRAMP_EN;
			}

			if (flag & FTRACE_FL_DIRECT) {
				/*
				 * If there's only one user (direct_ops helper)
				 * then we can call the direct function
				 * directly (no ftrace trampoline).
				 */
				if (ftrace_rec_count(rec) == 1) {
					if (rec->flags & FTRACE_FL_DIRECT)
						rec->flags |= FTRACE_FL_DIRECT_EN;
					else
						rec->flags &= ~FTRACE_FL_DIRECT_EN;
				} else {
					/*
					 * Can only call directly if there's
					 * only one callback to the function.
					 */
					rec->flags &= ~FTRACE_FL_DIRECT_EN;
				}
			}
		}

		/*
		 * If this record is being updated from a nop, then
		 *   return UPDATE_MAKE_CALL.
		 * Otherwise,
		 *   return UPDATE_MODIFY_CALL to tell the caller to convert
		 *   from the save regs, to a non-save regs function or
		 *   vice versa, or from a trampoline call.
		 */
		if (flag & FTRACE_FL_ENABLED) {
			ftrace_bug_type = FTRACE_BUG_CALL;
			return FTRACE_UPDATE_MAKE_CALL;
		}

		ftrace_bug_type = FTRACE_BUG_UPDATE;
		return FTRACE_UPDATE_MODIFY_CALL;
	}

	if (update) {
		/* If there's no more users, clear all flags */
		if (!ftrace_rec_count(rec))
			rec->flags = 0;
		else
			/*
			 * Just disable the record, but keep the ops TRAMP
			 * and REGS states. The _EN flags must be disabled though.
			 */
			rec->flags &= ~(FTRACE_FL_ENABLED | FTRACE_FL_TRAMP_EN |
					FTRACE_FL_REGS_EN | FTRACE_FL_DIRECT_EN);
	}

	ftrace_bug_type = FTRACE_BUG_NOP;
	return FTRACE_UPDATE_MAKE_NOP;
}

/**
 * ftrace_update_record - set a record that now is tracing or not
 * @rec: the record to update
 * @enable: set to true if the record is tracing, false to force disable
 *
 * The records that represent all functions that can be traced need
 * to be updated when tracing has been enabled.
 */
int ftrace_update_record(struct dyn_ftrace *rec, bool enable)
{
	return ftrace_check_record(rec, enable, true);
}

/**
 * ftrace_test_record - check if the record has been enabled or not
 * @rec: the record to test
 * @enable: set to true to check if enabled, false if it is disabled
 *
 * The arch code may need to test if a record is already set to
 * tracing to determine how to modify the function code that it
 * represents.
 */
int ftrace_test_record(struct dyn_ftrace *rec, bool enable)
{
	return ftrace_check_record(rec, enable, false);
}

static struct ftrace_ops *
ftrace_find_tramp_ops_any(struct dyn_ftrace *rec)
{
	struct ftrace_ops *op;
	unsigned long ip = rec->ip;

	do_for_each_ftrace_op(op, ftrace_ops_list) {

		if (!op->trampoline)
			continue;

		if (hash_contains_ip(ip, op->func_hash))
			return op;
	} while_for_each_ftrace_op(op);

	return NULL;
}

static struct ftrace_ops *
ftrace_find_tramp_ops_any_other(struct dyn_ftrace *rec, struct ftrace_ops *op_exclude)
{
	struct ftrace_ops *op;
	unsigned long ip = rec->ip;

	do_for_each_ftrace_op(op, ftrace_ops_list) {

		if (op == op_exclude || !op->trampoline)
			continue;

		if (hash_contains_ip(ip, op->func_hash))
			return op;
	} while_for_each_ftrace_op(op);

	return NULL;
}

static struct ftrace_ops *
ftrace_find_tramp_ops_next(struct dyn_ftrace *rec,
			   struct ftrace_ops *op)
{
	unsigned long ip = rec->ip;

	while_for_each_ftrace_op(op) {

		if (!op->trampoline)
			continue;

		if (hash_contains_ip(ip, op->func_hash))
			return op;
	}

	return NULL;
}

static struct ftrace_ops *
ftrace_find_tramp_ops_curr(struct dyn_ftrace *rec)
{
	struct ftrace_ops *op;
	unsigned long ip = rec->ip;

	/*
	 * Need to check removed ops first.
	 * If they are being removed, and this rec has a tramp,
	 * and this rec is in the ops list, then it would be the
	 * one with the tramp.
	 */
	if (removed_ops) {
		if (hash_contains_ip(ip, &removed_ops->old_hash))
			return removed_ops;
	}

	/*
	 * Need to find the current trampoline for a rec.
	 * Now, a trampoline is only attached to a rec if there
	 * was a single 'ops' attached to it. But this can be called
	 * when we are adding another op to the rec or removing the
	 * current one. Thus, if the op is being added, we can
	 * ignore it because it hasn't attached itself to the rec
	 * yet.
	 *
	 * If an ops is being modified (hooking to different functions)
	 * then we don't care about the new functions that are being
	 * added, just the old ones (that are probably being removed).
	 *
	 * If we are adding an ops to a function that already is using
	 * a trampoline, it needs to be removed (trampolines are only
	 * for single ops connected), then an ops that is not being
	 * modified also needs to be checked.
	 */
	do_for_each_ftrace_op(op, ftrace_ops_list) {

		if (!op->trampoline)
			continue;

		/*
		 * If the ops is being added, it hasn't gotten to
		 * the point to be removed from this tree yet.
		 */
		if (op->flags & FTRACE_OPS_FL_ADDING)
			continue;


		/*
		 * If the ops is being modified and is in the old
		 * hash, then it is probably being removed from this
		 * function.
		 */
		if ((op->flags & FTRACE_OPS_FL_MODIFYING) &&
		    hash_contains_ip(ip, &op->old_hash))
			return op;
		/*
		 * If the ops is not being added or modified, and it's
		 * in its normal filter hash, then this must be the one
		 * we want!
		 */
		if (!(op->flags & FTRACE_OPS_FL_MODIFYING) &&
		    hash_contains_ip(ip, op->func_hash))
			return op;

	} while_for_each_ftrace_op(op);

	return NULL;
}

static struct ftrace_ops *
ftrace_find_tramp_ops_new(struct dyn_ftrace *rec)
{
	struct ftrace_ops *op;
	unsigned long ip = rec->ip;

	do_for_each_ftrace_op(op, ftrace_ops_list) {
		/* pass rec in as regs to have non-NULL val */
		if (hash_contains_ip(ip, op->func_hash))
			return op;
	} while_for_each_ftrace_op(op);

	return NULL;
}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
/* Protected by rcu_tasks for reading, and direct_mutex for writing */
static struct ftrace_hash *direct_functions = EMPTY_HASH;
static DEFINE_MUTEX(direct_mutex);
int ftrace_direct_func_count;

/*
 * Search the direct_functions hash to see if the given instruction pointer
 * has a direct caller attached to it.
 */
unsigned long ftrace_find_rec_direct(unsigned long ip)
{
	struct ftrace_func_entry *entry;

	entry = __ftrace_lookup_ip(direct_functions, ip);
	if (!entry)
		return 0;

	return entry->direct;
}

static struct ftrace_func_entry*
ftrace_add_rec_direct(unsigned long ip, unsigned long addr,
		      struct ftrace_hash **free_hash)
{
	struct ftrace_func_entry *entry;

	if (ftrace_hash_empty(direct_functions) ||
	    direct_functions->count > 2 * (1 << direct_functions->size_bits)) {
		struct ftrace_hash *new_hash;
		int size = ftrace_hash_empty(direct_functions) ? 0 :
			direct_functions->count + 1;

		if (size < 32)
			size = 32;

		new_hash = dup_hash(direct_functions, size);
		if (!new_hash)
			return NULL;

		*free_hash = direct_functions;
		direct_functions = new_hash;
	}

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return NULL;

	entry->ip = ip;
	entry->direct = addr;
	__add_hash_entry(direct_functions, entry);
	return entry;
}

static void call_direct_funcs(unsigned long ip, unsigned long pip,
			      struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);
	unsigned long addr;

	addr = ftrace_find_rec_direct(ip);
	if (!addr)
		return;

	arch_ftrace_set_direct_caller(regs, addr);
}

struct ftrace_ops direct_ops = {
	.func		= call_direct_funcs,
	.flags		= FTRACE_OPS_FL_IPMODIFY
			  | FTRACE_OPS_FL_DIRECT | FTRACE_OPS_FL_SAVE_REGS
			  | FTRACE_OPS_FL_PERMANENT,
	/*
	 * By declaring the main trampoline as this trampoline
	 * it will never have one allocated for it. Allocated
	 * trampolines should not call direct functions.
	 * The direct_ops should only be called by the builtin
	 * ftrace_regs_caller trampoline.
	 */
	.trampoline	= FTRACE_REGS_ADDR,
};
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS */

/**
 * ftrace_get_addr_new - Get the call address to set to
 * @rec:  The ftrace record descriptor
 *
 * If the record has the FTRACE_FL_REGS set, that means that it
 * wants to convert to a callback that saves all regs. If FTRACE_FL_REGS
 * is not set, then it wants to convert to the normal callback.
 *
 * Returns the address of the trampoline to set to
 */
unsigned long ftrace_get_addr_new(struct dyn_ftrace *rec)
{
	struct ftrace_ops *ops;
	unsigned long addr;

	if ((rec->flags & FTRACE_FL_DIRECT) &&
	    (ftrace_rec_count(rec) == 1)) {
		addr = ftrace_find_rec_direct(rec->ip);
		if (addr)
			return addr;
		WARN_ON_ONCE(1);
	}

	/* Trampolines take precedence over regs */
	if (rec->flags & FTRACE_FL_TRAMP) {
		ops = ftrace_find_tramp_ops_new(rec);
		if (FTRACE_WARN_ON(!ops || !ops->trampoline)) {
			pr_warn("Bad trampoline accounting at: %p (%pS) (%lx)\n",
				(void *)rec->ip, (void *)rec->ip, rec->flags);
			/* Ftrace is shutting down, return anything */
			return (unsigned long)FTRACE_ADDR;
		}
		return ops->trampoline;
	}

	if (rec->flags & FTRACE_FL_REGS)
		return (unsigned long)FTRACE_REGS_ADDR;
	else
		return (unsigned long)FTRACE_ADDR;
}

/**
 * ftrace_get_addr_curr - Get the call address that is already there
 * @rec:  The ftrace record descriptor
 *
 * The FTRACE_FL_REGS_EN is set when the record already points to
 * a function that saves all the regs. Basically the '_EN' version
 * represents the current state of the function.
 *
 * Returns the address of the trampoline that is currently being called
 */
unsigned long ftrace_get_addr_curr(struct dyn_ftrace *rec)
{
	struct ftrace_ops *ops;
	unsigned long addr;

	/* Direct calls take precedence over trampolines */
	if (rec->flags & FTRACE_FL_DIRECT_EN) {
		addr = ftrace_find_rec_direct(rec->ip);
		if (addr)
			return addr;
		WARN_ON_ONCE(1);
	}

	/* Trampolines take precedence over regs */
	if (rec->flags & FTRACE_FL_TRAMP_EN) {
		ops = ftrace_find_tramp_ops_curr(rec);
		if (FTRACE_WARN_ON(!ops)) {
			pr_warn("Bad trampoline accounting at: %p (%pS)\n",
				(void *)rec->ip, (void *)rec->ip);
			/* Ftrace is shutting down, return anything */
			return (unsigned long)FTRACE_ADDR;
		}
		return ops->trampoline;
	}

	if (rec->flags & FTRACE_FL_REGS_EN)
		return (unsigned long)FTRACE_REGS_ADDR;
	else
		return (unsigned long)FTRACE_ADDR;
}

static int
__ftrace_replace_code(struct dyn_ftrace *rec, bool enable)
{
	unsigned long ftrace_old_addr;
	unsigned long ftrace_addr;
	int ret;

	ftrace_addr = ftrace_get_addr_new(rec);

	/* This needs to be done before we call ftrace_update_record */
	ftrace_old_addr = ftrace_get_addr_curr(rec);

	ret = ftrace_update_record(rec, enable);

	ftrace_bug_type = FTRACE_BUG_UNKNOWN;

	switch (ret) {
	case FTRACE_UPDATE_IGNORE:
		return 0;

	case FTRACE_UPDATE_MAKE_CALL:
		ftrace_bug_type = FTRACE_BUG_CALL;
		return ftrace_make_call(rec, ftrace_addr);

	case FTRACE_UPDATE_MAKE_NOP:
		ftrace_bug_type = FTRACE_BUG_NOP;
		return ftrace_make_nop(NULL, rec, ftrace_old_addr);

	case FTRACE_UPDATE_MODIFY_CALL:
		ftrace_bug_type = FTRACE_BUG_UPDATE;
		return ftrace_modify_call(rec, ftrace_old_addr, ftrace_addr);
	}

	return -1; /* unknown ftrace bug */
}

void __weak ftrace_replace_code(int mod_flags)
{
	struct dyn_ftrace *rec;
	struct ftrace_page *pg;
	bool enable = mod_flags & FTRACE_MODIFY_ENABLE_FL;
	int schedulable = mod_flags & FTRACE_MODIFY_MAY_SLEEP_FL;
	int failed;

	if (unlikely(ftrace_disabled))
		return;

	do_for_each_ftrace_rec(pg, rec) {

		if (rec->flags & FTRACE_FL_DISABLED)
			continue;

		failed = __ftrace_replace_code(rec, enable);
		if (failed) {
			ftrace_bug(failed, rec);
			/* Stop processing */
			return;
		}
		if (schedulable)
			cond_resched();
	} while_for_each_ftrace_rec();
}

struct ftrace_rec_iter {
	struct ftrace_page	*pg;
	int			index;
};

/**
 * ftrace_rec_iter_start - start up iterating over traced functions
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
 * ftrace_rec_iter_next - get the next record to process.
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
 * ftrace_rec_iter_record - get the record at the iterator location
 * @iter: The current iterator location
 *
 * Returns the record that the current @iter is at.
 */
struct dyn_ftrace *ftrace_rec_iter_record(struct ftrace_rec_iter *iter)
{
	return &iter->pg->records[iter->index];
}

static int
ftrace_nop_initialize(struct module *mod, struct dyn_ftrace *rec)
{
	int ret;

	if (unlikely(ftrace_disabled))
		return 0;

	ret = ftrace_init_nop(mod, rec);
	if (ret) {
		ftrace_bug_type = FTRACE_BUG_INIT;
		ftrace_bug(ret, rec);
		return 0;
	}
	return 1;
}

/*
 * archs can override this function if they must do something
 * before the modifying code is performed.
 */
void __weak ftrace_arch_code_modify_prepare(void)
{
}

/*
 * archs can override this function if they must do something
 * after the modifying code is performed.
 */
void __weak ftrace_arch_code_modify_post_process(void)
{
}

void ftrace_modify_all_code(int command)
{
	int update = command & FTRACE_UPDATE_TRACE_FUNC;
	int mod_flags = 0;
	int err = 0;

	if (command & FTRACE_MAY_SLEEP)
		mod_flags = FTRACE_MODIFY_MAY_SLEEP_FL;

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
	if (update) {
		err = ftrace_update_ftrace_func(ftrace_ops_list_func);
		if (FTRACE_WARN_ON(err))
			return;
	}

	if (command & FTRACE_UPDATE_CALLS)
		ftrace_replace_code(mod_flags | FTRACE_MODIFY_ENABLE_FL);
	else if (command & FTRACE_DISABLE_CALLS)
		ftrace_replace_code(mod_flags);

	if (update && ftrace_trace_function != ftrace_ops_list_func) {
		function_trace_op = set_function_trace_op;
		smp_wmb();
		/* If irqs are disabled, we are in stop machine */
		if (!irqs_disabled())
			smp_call_function(ftrace_sync_ipi, NULL, 1);
		err = ftrace_update_ftrace_func(ftrace_trace_function);
		if (FTRACE_WARN_ON(err))
			return;
	}

	if (command & FTRACE_START_FUNC_RET)
		err = ftrace_enable_ftrace_graph_caller();
	else if (command & FTRACE_STOP_FUNC_RET)
		err = ftrace_disable_ftrace_graph_caller();
	FTRACE_WARN_ON(err);
}

static int __ftrace_modify_code(void *data)
{
	int *command = data;

	ftrace_modify_all_code(*command);

	return 0;
}

/**
 * ftrace_run_stop_machine - go back to the stop machine method
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
 * arch_ftrace_update_code - modify the code to trace or not trace
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
	ftrace_arch_code_modify_prepare();

	/*
	 * By default we use stop_machine() to modify the code.
	 * But archs can do what ever they want as long as it
	 * is safe. The stop_machine() is the safest, but also
	 * produces the most overhead.
	 */
	arch_ftrace_update_code(command);

	ftrace_arch_code_modify_post_process();
}

static void ftrace_run_modify_code(struct ftrace_ops *ops, int command,
				   struct ftrace_ops_hash *old_hash)
{
	ops->flags |= FTRACE_OPS_FL_MODIFYING;
	ops->old_hash.filter_hash = old_hash->filter_hash;
	ops->old_hash.notrace_hash = old_hash->notrace_hash;
	ftrace_run_update_code(command);
	ops->old_hash.filter_hash = NULL;
	ops->old_hash.notrace_hash = NULL;
	ops->flags &= ~FTRACE_OPS_FL_MODIFYING;
}

static ftrace_func_t saved_ftrace_func;
static int ftrace_start_up;

void __weak arch_ftrace_trampoline_free(struct ftrace_ops *ops)
{
}

/* List of trace_ops that have allocated trampolines */
static LIST_HEAD(ftrace_ops_trampoline_list);

static void ftrace_add_trampoline_to_kallsyms(struct ftrace_ops *ops)
{
	lockdep_assert_held(&ftrace_lock);
	list_add_rcu(&ops->list, &ftrace_ops_trampoline_list);
}

static void ftrace_remove_trampoline_from_kallsyms(struct ftrace_ops *ops)
{
	lockdep_assert_held(&ftrace_lock);
	list_del_rcu(&ops->list);
	synchronize_rcu();
}

/*
 * "__builtin__ftrace" is used as a module name in /proc/kallsyms for symbols
 * for pages allocated for ftrace purposes, even though "__builtin__ftrace" is
 * not a module.
 */
#define FTRACE_TRAMPOLINE_MOD "__builtin__ftrace"
#define FTRACE_TRAMPOLINE_SYM "ftrace_trampoline"

static void ftrace_trampoline_free(struct ftrace_ops *ops)
{
	if (ops && (ops->flags & FTRACE_OPS_FL_ALLOC_TRAMP) &&
	    ops->trampoline) {
		/*
		 * Record the text poke event before the ksymbol unregister
		 * event.
		 */
		perf_event_text_poke((void *)ops->trampoline,
				     (void *)ops->trampoline,
				     ops->trampoline_size, NULL, 0);
		perf_event_ksymbol(PERF_RECORD_KSYMBOL_TYPE_OOL,
				   ops->trampoline, ops->trampoline_size,
				   true, FTRACE_TRAMPOLINE_SYM);
		/* Remove from kallsyms after the perf events */
		ftrace_remove_trampoline_from_kallsyms(ops);
	}

	arch_ftrace_trampoline_free(ops);
}

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

static void ftrace_startup_all(int command)
{
	update_all_ops = true;
	ftrace_startup_enable(command);
	update_all_ops = false;
}

int ftrace_startup(struct ftrace_ops *ops, int command)
{
	int ret;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	ret = __register_ftrace_function(ops);
	if (ret)
		return ret;

	ftrace_start_up++;

	/*
	 * Note that ftrace probes uses this to start up
	 * and modify functions it will probe. But we still
	 * set the ADDING flag for modification, as probes
	 * do not have trampolines. If they add them in the
	 * future, then the probes will need to distinguish
	 * between adding and updating probes.
	 */
	ops->flags |= FTRACE_OPS_FL_ENABLED | FTRACE_OPS_FL_ADDING;

	ret = ftrace_hash_ipmodify_enable(ops);
	if (ret < 0) {
		/* Rollback registration process */
		__unregister_ftrace_function(ops);
		ftrace_start_up--;
		ops->flags &= ~FTRACE_OPS_FL_ENABLED;
		if (ops->flags & FTRACE_OPS_FL_DYNAMIC)
			ftrace_trampoline_free(ops);
		return ret;
	}

	if (ftrace_hash_rec_enable(ops, 1))
		command |= FTRACE_UPDATE_CALLS;

	ftrace_startup_enable(command);

	ops->flags &= ~FTRACE_OPS_FL_ADDING;

	return 0;
}

int ftrace_shutdown(struct ftrace_ops *ops, int command)
{
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

	/* Disabling ipmodify never fails */
	ftrace_hash_ipmodify_disable(ops);

	if (ftrace_hash_rec_disable(ops, 1))
		command |= FTRACE_UPDATE_CALLS;

	ops->flags &= ~FTRACE_OPS_FL_ENABLED;

	if (saved_ftrace_func != ftrace_trace_function) {
		saved_ftrace_func = ftrace_trace_function;
		command |= FTRACE_UPDATE_TRACE_FUNC;
	}

	if (!command || !ftrace_enabled) {
		/*
		 * If these are dynamic or per_cpu ops, they still
		 * need their data freed. Since, function tracing is
		 * not currently active, we can just free them
		 * without synchronizing all CPUs.
		 */
		if (ops->flags & FTRACE_OPS_FL_DYNAMIC)
			goto free_ops;

		return 0;
	}

	/*
	 * If the ops uses a trampoline, then it needs to be
	 * tested first on update.
	 */
	ops->flags |= FTRACE_OPS_FL_REMOVING;
	removed_ops = ops;

	/* The trampoline logic checks the old hashes */
	ops->old_hash.filter_hash = ops->func_hash->filter_hash;
	ops->old_hash.notrace_hash = ops->func_hash->notrace_hash;

	ftrace_run_update_code(command);

	/*
	 * If there's no more ops registered with ftrace, run a
	 * sanity check to make sure all rec flags are cleared.
	 */
	if (rcu_dereference_protected(ftrace_ops_list,
			lockdep_is_held(&ftrace_lock)) == &ftrace_list_end) {
		struct ftrace_page *pg;
		struct dyn_ftrace *rec;

		do_for_each_ftrace_rec(pg, rec) {
			if (FTRACE_WARN_ON_ONCE(rec->flags & ~FTRACE_FL_DISABLED))
				pr_warn("  %pS flags:%lx\n",
					(void *)rec->ip, rec->flags);
		} while_for_each_ftrace_rec();
	}

	ops->old_hash.filter_hash = NULL;
	ops->old_hash.notrace_hash = NULL;

	removed_ops = NULL;
	ops->flags &= ~FTRACE_OPS_FL_REMOVING;

	/*
	 * Dynamic ops may be freed, we must make sure that all
	 * callers are done before leaving this function.
	 * The same goes for freeing the per_cpu data of the per_cpu
	 * ops.
	 */
	if (ops->flags & FTRACE_OPS_FL_DYNAMIC) {
		/*
		 * We need to do a hard force of sched synchronization.
		 * This is because we use preempt_disable() to do RCU, but
		 * the function tracers can be called where RCU is not watching
		 * (like before user_exit()). We can not rely on the RCU
		 * infrastructure to do the synchronization, thus we must do it
		 * ourselves.
		 */
		synchronize_rcu_tasks_rude();

		/*
		 * When the kernel is preemptive, tasks can be preempted
		 * while on a ftrace trampoline. Just scheduling a task on
		 * a CPU is not good enough to flush them. Calling
		 * synchronize_rcu_tasks() will wait for those tasks to
		 * execute and either schedule voluntarily or enter user space.
		 */
		if (IS_ENABLED(CONFIG_PREEMPTION))
			synchronize_rcu_tasks();

 free_ops:
		ftrace_trampoline_free(ops);
	}

	return 0;
}

static u64		ftrace_update_time;
unsigned long		ftrace_update_tot_cnt;
unsigned long		ftrace_number_of_pages;
unsigned long		ftrace_number_of_groups;

static inline int ops_traces_mod(struct ftrace_ops *ops)
{
	/*
	 * Filter_hash being empty will default to trace module.
	 * But notrace hash requires a test of individual module functions.
	 */
	return ftrace_hash_empty(ops->func_hash->filter_hash) &&
		ftrace_hash_empty(ops->func_hash->notrace_hash);
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
		return false;

	/* If ops traces all then it includes this function */
	if (ops_traces_mod(ops))
		return true;

	/* The function must be in the filter */
	if (!ftrace_hash_empty(ops->func_hash->filter_hash) &&
	    !__ftrace_lookup_ip(ops->func_hash->filter_hash, rec->ip))
		return false;

	/* If in notrace hash, we ignore it too */
	if (ftrace_lookup_ip(ops->func_hash->notrace_hash, rec->ip))
		return false;

	return true;
}

static int ftrace_update_code(struct module *mod, struct ftrace_page *new_pgs)
{
	bool init_nop = ftrace_need_init_nop();
	struct ftrace_page *pg;
	struct dyn_ftrace *p;
	u64 start, stop;
	unsigned long update_cnt = 0;
	unsigned long rec_flags = 0;
	int i;

	start = ftrace_now(raw_smp_processor_id());

	/*
	 * When a module is loaded, this function is called to convert
	 * the calls to mcount in its text to nops, and also to create
	 * an entry in the ftrace data. Now, if ftrace is activated
	 * after this call, but before the module sets its text to
	 * read-only, the modification of enabling ftrace can fail if
	 * the read-only is done while ftrace is converting the calls.
	 * To prevent this, the module's records are set as disabled
	 * and will be enabled after the call to set the module's text
	 * to read-only.
	 */
	if (mod)
		rec_flags |= FTRACE_FL_DISABLED;

	for (pg = new_pgs; pg; pg = pg->next) {

		for (i = 0; i < pg->index; i++) {

			/* If something went wrong, bail without enabling anything */
			if (unlikely(ftrace_disabled))
				return -1;

			p = &pg->records[i];
			p->flags = rec_flags;

			/*
			 * Do the initial record conversion from mcount jump
			 * to the NOP instructions.
			 */
			if (init_nop && !ftrace_nop_initialize(mod, p))
				break;

			update_cnt++;
		}
	}

	stop = ftrace_now(raw_smp_processor_id());
	ftrace_update_time = stop - start;
	ftrace_update_tot_cnt += update_cnt;

	return 0;
}

static int ftrace_allocate_records(struct ftrace_page *pg, int count)
{
	int order;
	int pages;
	int cnt;

	if (WARN_ON(!count))
		return -EINVAL;

	/* We want to fill as much as possible, with no empty pages */
	pages = DIV_ROUND_UP(count, ENTRIES_PER_PAGE);
	order = fls(pages) - 1;

 again:
	pg->records = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, order);

	if (!pg->records) {
		/* if we can't allocate this size, try something smaller */
		if (!order)
			return -ENOMEM;
		order >>= 1;
		goto again;
	}

	ftrace_number_of_pages += 1 << order;
	ftrace_number_of_groups++;

	cnt = (PAGE_SIZE << order) / ENTRY_SIZE;
	pg->order = order;

	if (cnt > count)
		cnt = count;

	return cnt;
}

static struct ftrace_page *
ftrace_allocate_pages(unsigned long num_to_init)
{
	struct ftrace_page *start_pg;
	struct ftrace_page *pg;
	int cnt;

	if (!num_to_init)
		return NULL;

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
	pg = start_pg;
	while (pg) {
		if (pg->records) {
			free_pages((unsigned long)pg->records, pg->order);
			ftrace_number_of_pages -= 1 << pg->order;
		}
		start_pg = pg->next;
		kfree(pg);
		pg = start_pg;
		ftrace_number_of_groups--;
	}
	pr_info("ftrace: FAILED to allocate memory for functions\n");
	return NULL;
}

#define FTRACE_BUFF_MAX (KSYM_SYMBOL_LEN+4) /* room for wildcards */

struct ftrace_iterator {
	loff_t				pos;
	loff_t				func_pos;
	loff_t				mod_pos;
	struct ftrace_page		*pg;
	struct dyn_ftrace		*func;
	struct ftrace_func_probe	*probe;
	struct ftrace_func_entry	*probe_entry;
	struct trace_parser		parser;
	struct ftrace_hash		*hash;
	struct ftrace_ops		*ops;
	struct trace_array		*tr;
	struct list_head		*mod_list;
	int				pidx;
	int				idx;
	unsigned			flags;
};

static void *
t_probe_next(struct seq_file *m, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	struct trace_array *tr = iter->ops->private;
	struct list_head *func_probes;
	struct ftrace_hash *hash;
	struct list_head *next;
	struct hlist_node *hnd = NULL;
	struct hlist_head *hhd;
	int size;

	(*pos)++;
	iter->pos = *pos;

	if (!tr)
		return NULL;

	func_probes = &tr->func_probes;
	if (list_empty(func_probes))
		return NULL;

	if (!iter->probe) {
		next = func_probes->next;
		iter->probe = list_entry(next, struct ftrace_func_probe, list);
	}

	if (iter->probe_entry)
		hnd = &iter->probe_entry->hlist;

	hash = iter->probe->ops.func_hash->filter_hash;

	/*
	 * A probe being registered may temporarily have an empty hash
	 * and it's at the end of the func_probes list.
	 */
	if (!hash || hash == EMPTY_HASH)
		return NULL;

	size = 1 << hash->size_bits;

 retry:
	if (iter->pidx >= size) {
		if (iter->probe->list.next == func_probes)
			return NULL;
		next = iter->probe->list.next;
		iter->probe = list_entry(next, struct ftrace_func_probe, list);
		hash = iter->probe->ops.func_hash->filter_hash;
		size = 1 << hash->size_bits;
		iter->pidx = 0;
	}

	hhd = &hash->buckets[iter->pidx];

	if (hlist_empty(hhd)) {
		iter->pidx++;
		hnd = NULL;
		goto retry;
	}

	if (!hnd)
		hnd = hhd->first;
	else {
		hnd = hnd->next;
		if (!hnd) {
			iter->pidx++;
			goto retry;
		}
	}

	if (WARN_ON_ONCE(!hnd))
		return NULL;

	iter->probe_entry = hlist_entry(hnd, struct ftrace_func_entry, hlist);

	return iter;
}

static void *t_probe_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	void *p = NULL;
	loff_t l;

	if (!(iter->flags & FTRACE_ITER_DO_PROBES))
		return NULL;

	if (iter->mod_pos > *pos)
		return NULL;

	iter->probe = NULL;
	iter->probe_entry = NULL;
	iter->pidx = 0;
	for (l = 0; l <= (*pos - iter->mod_pos); ) {
		p = t_probe_next(m, &l);
		if (!p)
			break;
	}
	if (!p)
		return NULL;

	/* Only set this if we have an item */
	iter->flags |= FTRACE_ITER_PROBE;

	return iter;
}

static int
t_probe_show(struct seq_file *m, struct ftrace_iterator *iter)
{
	struct ftrace_func_entry *probe_entry;
	struct ftrace_probe_ops *probe_ops;
	struct ftrace_func_probe *probe;

	probe = iter->probe;
	probe_entry = iter->probe_entry;

	if (WARN_ON_ONCE(!probe || !probe_entry))
		return -EIO;

	probe_ops = probe->probe_ops;

	if (probe_ops->print)
		return probe_ops->print(m, probe_entry->ip, probe_ops, probe->data);

	seq_printf(m, "%ps:%ps\n", (void *)probe_entry->ip,
		   (void *)probe_ops->func);

	return 0;
}

static void *
t_mod_next(struct seq_file *m, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	struct trace_array *tr = iter->tr;

	(*pos)++;
	iter->pos = *pos;

	iter->mod_list = iter->mod_list->next;

	if (iter->mod_list == &tr->mod_trace ||
	    iter->mod_list == &tr->mod_notrace) {
		iter->flags &= ~FTRACE_ITER_MOD;
		return NULL;
	}

	iter->mod_pos = *pos;

	return iter;
}

static void *t_mod_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	void *p = NULL;
	loff_t l;

	if (iter->func_pos > *pos)
		return NULL;

	iter->mod_pos = iter->func_pos;

	/* probes are only available if tr is set */
	if (!iter->tr)
		return NULL;

	for (l = 0; l <= (*pos - iter->func_pos); ) {
		p = t_mod_next(m, &l);
		if (!p)
			break;
	}
	if (!p) {
		iter->flags &= ~FTRACE_ITER_MOD;
		return t_probe_start(m, pos);
	}

	/* Only set this if we have an item */
	iter->flags |= FTRACE_ITER_MOD;

	return iter;
}

static int
t_mod_show(struct seq_file *m, struct ftrace_iterator *iter)
{
	struct ftrace_mod_load *ftrace_mod;
	struct trace_array *tr = iter->tr;

	if (WARN_ON_ONCE(!iter->mod_list) ||
			 iter->mod_list == &tr->mod_trace ||
			 iter->mod_list == &tr->mod_notrace)
		return -EIO;

	ftrace_mod = list_entry(iter->mod_list, struct ftrace_mod_load, list);

	if (ftrace_mod->func)
		seq_printf(m, "%s", ftrace_mod->func);
	else
		seq_putc(m, '*');

	seq_printf(m, ":mod:%s\n", ftrace_mod->module);

	return 0;
}

static void *
t_func_next(struct seq_file *m, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	struct dyn_ftrace *rec = NULL;

	(*pos)++;

 retry:
	if (iter->idx >= iter->pg->index) {
		if (iter->pg->next) {
			iter->pg = iter->pg->next;
			iter->idx = 0;
			goto retry;
		}
	} else {
		rec = &iter->pg->records[iter->idx++];
		if (((iter->flags & (FTRACE_ITER_FILTER | FTRACE_ITER_NOTRACE)) &&
		     !ftrace_lookup_ip(iter->hash, rec->ip)) ||

		    ((iter->flags & FTRACE_ITER_ENABLED) &&
		     !(rec->flags & FTRACE_FL_ENABLED))) {

			rec = NULL;
			goto retry;
		}
	}

	if (!rec)
		return NULL;

	iter->pos = iter->func_pos = *pos;
	iter->func = rec;

	return iter;
}

static void *
t_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	loff_t l = *pos; /* t_probe_start() must use original pos */
	void *ret;

	if (unlikely(ftrace_disabled))
		return NULL;

	if (iter->flags & FTRACE_ITER_PROBE)
		return t_probe_next(m, pos);

	if (iter->flags & FTRACE_ITER_MOD)
		return t_mod_next(m, pos);

	if (iter->flags & FTRACE_ITER_PRINTALL) {
		/* next must increment pos, and t_probe_start does not */
		(*pos)++;
		return t_mod_start(m, &l);
	}

	ret = t_func_next(m, pos);

	if (!ret)
		return t_mod_start(m, &l);

	return ret;
}

static void reset_iter_read(struct ftrace_iterator *iter)
{
	iter->pos = 0;
	iter->func_pos = 0;
	iter->flags &= ~(FTRACE_ITER_PRINTALL | FTRACE_ITER_PROBE | FTRACE_ITER_MOD);
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
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
	if ((iter->flags & (FTRACE_ITER_FILTER | FTRACE_ITER_NOTRACE)) &&
	    ftrace_hash_empty(iter->hash)) {
		iter->func_pos = 1; /* Account for the message */
		if (*pos > 0)
			return t_mod_start(m, pos);
		iter->flags |= FTRACE_ITER_PRINTALL;
		/* reset in case of seek/pread */
		iter->flags &= ~FTRACE_ITER_PROBE;
		return iter;
	}

	if (iter->flags & FTRACE_ITER_MOD)
		return t_mod_start(m, pos);

	/*
	 * Unfortunately, we need to restart at ftrace_pages_start
	 * every time we let go of the ftrace_mutex. This is because
	 * those pointers can change without the lock.
	 */
	iter->pg = ftrace_pages_start;
	iter->idx = 0;
	for (l = 0; l <= *pos; ) {
		p = t_func_next(m, &l);
		if (!p)
			break;
	}

	if (!p)
		return t_mod_start(m, pos);

	return iter;
}

static void t_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&ftrace_lock);
}

void * __weak
arch_ftrace_trampoline_func(struct ftrace_ops *ops, struct dyn_ftrace *rec)
{
	return NULL;
}

static void add_trampoline_func(struct seq_file *m, struct ftrace_ops *ops,
				struct dyn_ftrace *rec)
{
	void *ptr;

	ptr = arch_ftrace_trampoline_func(ops, rec);
	if (ptr)
		seq_printf(m, " ->%pS", ptr);
}

#ifdef FTRACE_MCOUNT_MAX_OFFSET
/*
 * Weak functions can still have an mcount/fentry that is saved in
 * the __mcount_loc section. These can be detected by having a
 * symbol offset of greater than FTRACE_MCOUNT_MAX_OFFSET, as the
 * symbol found by kallsyms is not the function that the mcount/fentry
 * is part of. The offset is much greater in these cases.
 *
 * Test the record to make sure that the ip points to a valid kallsyms
 * and if not, mark it disabled.
 */
static int test_for_valid_rec(struct dyn_ftrace *rec)
{
	char str[KSYM_SYMBOL_LEN];
	unsigned long offset;
	const char *ret;

	ret = kallsyms_lookup(rec->ip, NULL, &offset, NULL, str);

	/* Weak functions can cause invalid addresses */
	if (!ret || offset > FTRACE_MCOUNT_MAX_OFFSET) {
		rec->flags |= FTRACE_FL_DISABLED;
		return 0;
	}
	return 1;
}

static struct workqueue_struct *ftrace_check_wq __initdata;
static struct work_struct ftrace_check_work __initdata;

/*
 * Scan all the mcount/fentry entries to make sure they are valid.
 */
static __init void ftrace_check_work_func(struct work_struct *work)
{
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;

	mutex_lock(&ftrace_lock);
	do_for_each_ftrace_rec(pg, rec) {
		test_for_valid_rec(rec);
	} while_for_each_ftrace_rec();
	mutex_unlock(&ftrace_lock);
}

static int __init ftrace_check_for_weak_functions(void)
{
	INIT_WORK(&ftrace_check_work, ftrace_check_work_func);

	ftrace_check_wq = alloc_workqueue("ftrace_check_wq", WQ_UNBOUND, 0);

	queue_work(ftrace_check_wq, &ftrace_check_work);
	return 0;
}

static int __init ftrace_check_sync(void)
{
	/* Make sure the ftrace_check updates are finished */
	if (ftrace_check_wq)
		destroy_workqueue(ftrace_check_wq);
	return 0;
}

late_initcall_sync(ftrace_check_sync);
subsys_initcall(ftrace_check_for_weak_functions);

static int print_rec(struct seq_file *m, unsigned long ip)
{
	unsigned long offset;
	char str[KSYM_SYMBOL_LEN];
	char *modname;
	const char *ret;

	ret = kallsyms_lookup(ip, NULL, &offset, &modname, str);
	/* Weak functions can cause invalid addresses */
	if (!ret || offset > FTRACE_MCOUNT_MAX_OFFSET) {
		snprintf(str, KSYM_SYMBOL_LEN, "%s_%ld",
			 FTRACE_INVALID_FUNCTION, offset);
		ret = NULL;
	}

	seq_puts(m, str);
	if (modname)
		seq_printf(m, " [%s]", modname);
	return ret == NULL ? -1 : 0;
}
#else
static inline int test_for_valid_rec(struct dyn_ftrace *rec)
{
	return 1;
}

static inline int print_rec(struct seq_file *m, unsigned long ip)
{
	seq_printf(m, "%ps", (void *)ip);
	return 0;
}
#endif

static int t_show(struct seq_file *m, void *v)
{
	struct ftrace_iterator *iter = m->private;
	struct dyn_ftrace *rec;

	if (iter->flags & FTRACE_ITER_PROBE)
		return t_probe_show(m, iter);

	if (iter->flags & FTRACE_ITER_MOD)
		return t_mod_show(m, iter);

	if (iter->flags & FTRACE_ITER_PRINTALL) {
		if (iter->flags & FTRACE_ITER_NOTRACE)
			seq_puts(m, "#### no functions disabled ####\n");
		else
			seq_puts(m, "#### all functions enabled ####\n");
		return 0;
	}

	rec = iter->func;

	if (!rec)
		return 0;

	if (print_rec(m, rec->ip)) {
		/* This should only happen when a rec is disabled */
		WARN_ON_ONCE(!(rec->flags & FTRACE_FL_DISABLED));
		seq_putc(m, '\n');
		return 0;
	}

	if (iter->flags & FTRACE_ITER_ENABLED) {
		struct ftrace_ops *ops;

		seq_printf(m, " (%ld)%s%s%s",
			   ftrace_rec_count(rec),
			   rec->flags & FTRACE_FL_REGS ? " R" : "  ",
			   rec->flags & FTRACE_FL_IPMODIFY ? " I" : "  ",
			   rec->flags & FTRACE_FL_DIRECT ? " D" : "  ");
		if (rec->flags & FTRACE_FL_TRAMP_EN) {
			ops = ftrace_find_tramp_ops_any(rec);
			if (ops) {
				do {
					seq_printf(m, "\ttramp: %pS (%pS)",
						   (void *)ops->trampoline,
						   (void *)ops->func);
					add_trampoline_func(m, ops, rec);
					ops = ftrace_find_tramp_ops_next(rec, ops);
				} while (ops);
			} else
				seq_puts(m, "\ttramp: ERROR!");
		} else {
			add_trampoline_func(m, NULL, rec);
		}
		if (rec->flags & FTRACE_FL_DIRECT) {
			unsigned long direct;

			direct = ftrace_find_rec_direct(rec->ip);
			if (direct)
				seq_printf(m, "\n\tdirect-->%pS", (void *)direct);
		}
	}

	seq_putc(m, '\n');

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

	ret = security_locked_down(LOCKDOWN_TRACEFS);
	if (ret)
		return ret;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	iter = __seq_open_private(file, &show_ftrace_seq_ops, sizeof(*iter));
	if (!iter)
		return -ENOMEM;

	iter->pg = ftrace_pages_start;
	iter->ops = &global_ops;

	return 0;
}

static int
ftrace_enabled_open(struct inode *inode, struct file *file)
{
	struct ftrace_iterator *iter;

	/*
	 * This shows us what functions are currently being
	 * traced and by what. Not sure if we want lockdown
	 * to hide such critical information for an admin.
	 * Although, perhaps it can show information we don't
	 * want people to see, but if something is tracing
	 * something, we probably want to know about it.
	 */

	iter = __seq_open_private(file, &show_ftrace_seq_ops, sizeof(*iter));
	if (!iter)
		return -ENOMEM;

	iter->pg = ftrace_pages_start;
	iter->flags = FTRACE_ITER_ENABLED;
	iter->ops = &global_ops;

	return 0;
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
 * tracing_lseek() should be used as the lseek routine, and
 * release must call ftrace_regex_release().
 */
int
ftrace_regex_open(struct ftrace_ops *ops, int flag,
		  struct inode *inode, struct file *file)
{
	struct ftrace_iterator *iter;
	struct ftrace_hash *hash;
	struct list_head *mod_head;
	struct trace_array *tr = ops->private;
	int ret = -ENOMEM;

	ftrace_ops_init(ops);

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	if (tracing_check_open_get_tr(tr))
		return -ENODEV;

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		goto out;

	if (trace_parser_get_init(&iter->parser, FTRACE_BUFF_MAX))
		goto out;

	iter->ops = ops;
	iter->flags = flag;
	iter->tr = tr;

	mutex_lock(&ops->func_hash->regex_lock);

	if (flag & FTRACE_ITER_NOTRACE) {
		hash = ops->func_hash->notrace_hash;
		mod_head = tr ? &tr->mod_notrace : NULL;
	} else {
		hash = ops->func_hash->filter_hash;
		mod_head = tr ? &tr->mod_trace : NULL;
	}

	iter->mod_list = mod_head;

	if (file->f_mode & FMODE_WRITE) {
		const int size_bits = FTRACE_HASH_DEFAULT_BITS;

		if (file->f_flags & O_TRUNC) {
			iter->hash = alloc_ftrace_hash(size_bits);
			clear_ftrace_mod_list(mod_head);
	        } else {
			iter->hash = alloc_and_copy_ftrace_hash(size_bits, hash);
		}

		if (!iter->hash) {
			trace_parser_put(&iter->parser);
			goto out_unlock;
		}
	} else
		iter->hash = hash;

	ret = 0;

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
		}
	} else
		file->private_data = iter;

 out_unlock:
	mutex_unlock(&ops->func_hash->regex_lock);

 out:
	if (ret) {
		kfree(iter);
		if (tr)
			trace_array_put(tr);
	}

	return ret;
}

static int
ftrace_filter_open(struct inode *inode, struct file *file)
{
	struct ftrace_ops *ops = inode->i_private;

	/* Checks for tracefs lockdown */
	return ftrace_regex_open(ops,
			FTRACE_ITER_FILTER | FTRACE_ITER_DO_PROBES,
			inode, file);
}

static int
ftrace_notrace_open(struct inode *inode, struct file *file)
{
	struct ftrace_ops *ops = inode->i_private;

	/* Checks for tracefs lockdown */
	return ftrace_regex_open(ops, FTRACE_ITER_NOTRACE,
				 inode, file);
}

/* Type for quick search ftrace basic regexes (globs) from filter_parse_regex */
struct ftrace_glob {
	char *search;
	unsigned len;
	int type;
};

/*
 * If symbols in an architecture don't correspond exactly to the user-visible
 * name of what they represent, it is possible to define this function to
 * perform the necessary adjustments.
*/
char * __weak arch_ftrace_match_adjust(char *str, const char *search)
{
	return str;
}

static int ftrace_match(char *str, struct ftrace_glob *g)
{
	int matched = 0;
	int slen;

	str = arch_ftrace_match_adjust(str, g->search);

	switch (g->type) {
	case MATCH_FULL:
		if (strcmp(str, g->search) == 0)
			matched = 1;
		break;
	case MATCH_FRONT_ONLY:
		if (strncmp(str, g->search, g->len) == 0)
			matched = 1;
		break;
	case MATCH_MIDDLE_ONLY:
		if (strstr(str, g->search))
			matched = 1;
		break;
	case MATCH_END_ONLY:
		slen = strlen(str);
		if (slen >= g->len &&
		    memcmp(str + slen - g->len, g->search, g->len) == 0)
			matched = 1;
		break;
	case MATCH_GLOB:
		if (glob_match(g->search, str))
			matched = 1;
		break;
	}

	return matched;
}

static int
enter_record(struct ftrace_hash *hash, struct dyn_ftrace *rec, int clear_filter)
{
	struct ftrace_func_entry *entry;
	int ret = 0;

	entry = ftrace_lookup_ip(hash, rec->ip);
	if (clear_filter) {
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
add_rec_by_index(struct ftrace_hash *hash, struct ftrace_glob *func_g,
		 int clear_filter)
{
	long index = simple_strtoul(func_g->search, NULL, 0);
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;

	/* The index starts at 1 */
	if (--index < 0)
		return 0;

	do_for_each_ftrace_rec(pg, rec) {
		if (pg->index <= index) {
			index -= pg->index;
			/* this is a double loop, break goes to the next page */
			break;
		}
		rec = &pg->records[index];
		enter_record(hash, rec, clear_filter);
		return 1;
	} while_for_each_ftrace_rec();
	return 0;
}

#ifdef FTRACE_MCOUNT_MAX_OFFSET
static int lookup_ip(unsigned long ip, char **modname, char *str)
{
	unsigned long offset;

	kallsyms_lookup(ip, NULL, &offset, modname, str);
	if (offset > FTRACE_MCOUNT_MAX_OFFSET)
		return -1;
	return 0;
}
#else
static int lookup_ip(unsigned long ip, char **modname, char *str)
{
	kallsyms_lookup(ip, NULL, NULL, modname, str);
	return 0;
}
#endif

static int
ftrace_match_record(struct dyn_ftrace *rec, struct ftrace_glob *func_g,
		struct ftrace_glob *mod_g, int exclude_mod)
{
	char str[KSYM_SYMBOL_LEN];
	char *modname;

	if (lookup_ip(rec->ip, &modname, str)) {
		/* This should only happen when a rec is disabled */
		WARN_ON_ONCE(system_state == SYSTEM_RUNNING &&
			     !(rec->flags & FTRACE_FL_DISABLED));
		return 0;
	}

	if (mod_g) {
		int mod_matches = (modname) ? ftrace_match(modname, mod_g) : 0;

		/* blank module name to match all modules */
		if (!mod_g->len) {
			/* blank module globbing: modname xor exclude_mod */
			if (!exclude_mod != !modname)
				goto func_match;
			return 0;
		}

		/*
		 * exclude_mod is set to trace everything but the given
		 * module. If it is set and the module matches, then
		 * return 0. If it is not set, and the module doesn't match
		 * also return 0. Otherwise, check the function to see if
		 * that matches.
		 */
		if (!mod_matches == !exclude_mod)
			return 0;
func_match:
		/* blank search means to match all funcs in the mod */
		if (!func_g->len)
			return 1;
	}

	return ftrace_match(str, func_g);
}

static int
match_records(struct ftrace_hash *hash, char *func, int len, char *mod)
{
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	struct ftrace_glob func_g = { .type = MATCH_FULL };
	struct ftrace_glob mod_g = { .type = MATCH_FULL };
	struct ftrace_glob *mod_match = (mod) ? &mod_g : NULL;
	int exclude_mod = 0;
	int found = 0;
	int ret;
	int clear_filter = 0;

	if (func) {
		func_g.type = filter_parse_regex(func, len, &func_g.search,
						 &clear_filter);
		func_g.len = strlen(func_g.search);
	}

	if (mod) {
		mod_g.type = filter_parse_regex(mod, strlen(mod),
				&mod_g.search, &exclude_mod);
		mod_g.len = strlen(mod_g.search);
	}

	mutex_lock(&ftrace_lock);

	if (unlikely(ftrace_disabled))
		goto out_unlock;

	if (func_g.type == MATCH_INDEX) {
		found = add_rec_by_index(hash, &func_g, clear_filter);
		goto out_unlock;
	}

	do_for_each_ftrace_rec(pg, rec) {

		if (rec->flags & FTRACE_FL_DISABLED)
			continue;

		if (ftrace_match_record(rec, &func_g, mod_match, exclude_mod)) {
			ret = enter_record(hash, rec, clear_filter);
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
	return match_records(hash, buff, len, NULL);
}

static void ftrace_ops_update_code(struct ftrace_ops *ops,
				   struct ftrace_ops_hash *old_hash)
{
	struct ftrace_ops *op;

	if (!ftrace_enabled)
		return;

	if (ops->flags & FTRACE_OPS_FL_ENABLED) {
		ftrace_run_modify_code(ops, FTRACE_UPDATE_CALLS, old_hash);
		return;
	}

	/*
	 * If this is the shared global_ops filter, then we need to
	 * check if there is another ops that shares it, is enabled.
	 * If so, we still need to run the modify code.
	 */
	if (ops->func_hash != &global_ops.local_hash)
		return;

	do_for_each_ftrace_op(op, ftrace_ops_list) {
		if (op->func_hash == &global_ops.local_hash &&
		    op->flags & FTRACE_OPS_FL_ENABLED) {
			ftrace_run_modify_code(op, FTRACE_UPDATE_CALLS, old_hash);
			/* Only need to do this once */
			return;
		}
	} while_for_each_ftrace_op(op);
}

static int ftrace_hash_move_and_update_ops(struct ftrace_ops *ops,
					   struct ftrace_hash **orig_hash,
					   struct ftrace_hash *hash,
					   int enable)
{
	struct ftrace_ops_hash old_hash_ops;
	struct ftrace_hash *old_hash;
	int ret;

	old_hash = *orig_hash;
	old_hash_ops.filter_hash = ops->func_hash->filter_hash;
	old_hash_ops.notrace_hash = ops->func_hash->notrace_hash;
	ret = ftrace_hash_move(ops, enable, orig_hash, hash);
	if (!ret) {
		ftrace_ops_update_code(ops, &old_hash_ops);
		free_ftrace_hash_rcu(old_hash);
	}
	return ret;
}

static bool module_exists(const char *module)
{
	/* All modules have the symbol __this_module */
	static const char this_mod[] = "__this_module";
	char modname[MAX_PARAM_PREFIX_LEN + sizeof(this_mod) + 2];
	unsigned long val;
	int n;

	n = snprintf(modname, sizeof(modname), "%s:%s", module, this_mod);

	if (n > sizeof(modname) - 1)
		return false;

	val = module_kallsyms_lookup_name(modname);
	return val != 0;
}

static int cache_mod(struct trace_array *tr,
		     const char *func, char *module, int enable)
{
	struct ftrace_mod_load *ftrace_mod, *n;
	struct list_head *head = enable ? &tr->mod_trace : &tr->mod_notrace;
	int ret;

	mutex_lock(&ftrace_lock);

	/* We do not cache inverse filters */
	if (func[0] == '!') {
		func++;
		ret = -EINVAL;

		/* Look to remove this hash */
		list_for_each_entry_safe(ftrace_mod, n, head, list) {
			if (strcmp(ftrace_mod->module, module) != 0)
				continue;

			/* no func matches all */
			if (strcmp(func, "*") == 0 ||
			    (ftrace_mod->func &&
			     strcmp(ftrace_mod->func, func) == 0)) {
				ret = 0;
				free_ftrace_mod(ftrace_mod);
				continue;
			}
		}
		goto out;
	}

	ret = -EINVAL;
	/* We only care about modules that have not been loaded yet */
	if (module_exists(module))
		goto out;

	/* Save this string off, and execute it when the module is loaded */
	ret = ftrace_add_mod(tr, func, module, enable);
 out:
	mutex_unlock(&ftrace_lock);

	return ret;
}

static int
ftrace_set_regex(struct ftrace_ops *ops, unsigned char *buf, int len,
		 int reset, int enable);

#ifdef CONFIG_MODULES
static void process_mod_list(struct list_head *head, struct ftrace_ops *ops,
			     char *mod, bool enable)
{
	struct ftrace_mod_load *ftrace_mod, *n;
	struct ftrace_hash **orig_hash, *new_hash;
	LIST_HEAD(process_mods);
	char *func;

	mutex_lock(&ops->func_hash->regex_lock);

	if (enable)
		orig_hash = &ops->func_hash->filter_hash;
	else
		orig_hash = &ops->func_hash->notrace_hash;

	new_hash = alloc_and_copy_ftrace_hash(FTRACE_HASH_DEFAULT_BITS,
					      *orig_hash);
	if (!new_hash)
		goto out; /* warn? */

	mutex_lock(&ftrace_lock);

	list_for_each_entry_safe(ftrace_mod, n, head, list) {

		if (strcmp(ftrace_mod->module, mod) != 0)
			continue;

		if (ftrace_mod->func)
			func = kstrdup(ftrace_mod->func, GFP_KERNEL);
		else
			func = kstrdup("*", GFP_KERNEL);

		if (!func) /* warn? */
			continue;

		list_move(&ftrace_mod->list, &process_mods);

		/* Use the newly allocated func, as it may be "*" */
		kfree(ftrace_mod->func);
		ftrace_mod->func = func;
	}

	mutex_unlock(&ftrace_lock);

	list_for_each_entry_safe(ftrace_mod, n, &process_mods, list) {

		func = ftrace_mod->func;

		/* Grabs ftrace_lock, which is why we have this extra step */
		match_records(new_hash, func, strlen(func), mod);
		free_ftrace_mod(ftrace_mod);
	}

	if (enable && list_empty(head))
		new_hash->flags &= ~FTRACE_HASH_FL_MOD;

	mutex_lock(&ftrace_lock);

	ftrace_hash_move_and_update_ops(ops, orig_hash,
					      new_hash, enable);
	mutex_unlock(&ftrace_lock);

 out:
	mutex_unlock(&ops->func_hash->regex_lock);

	free_ftrace_hash(new_hash);
}

static void process_cached_mods(const char *mod_name)
{
	struct trace_array *tr;
	char *mod;

	mod = kstrdup(mod_name, GFP_KERNEL);
	if (!mod)
		return;

	mutex_lock(&trace_types_lock);
	list_for_each_entry(tr, &ftrace_trace_arrays, list) {
		if (!list_empty(&tr->mod_trace))
			process_mod_list(&tr->mod_trace, tr->ops, mod, true);
		if (!list_empty(&tr->mod_notrace))
			process_mod_list(&tr->mod_notrace, tr->ops, mod, false);
	}
	mutex_unlock(&trace_types_lock);

	kfree(mod);
}
#endif

/*
 * We register the module command as a template to show others how
 * to register the a command as well.
 */

static int
ftrace_mod_callback(struct trace_array *tr, struct ftrace_hash *hash,
		    char *func_orig, char *cmd, char *module, int enable)
{
	char *func;
	int ret;

	/* match_records() modifies func, and we need the original */
	func = kstrdup(func_orig, GFP_KERNEL);
	if (!func)
		return -ENOMEM;

	/*
	 * cmd == 'mod' because we only registered this func
	 * for the 'mod' ftrace_func_command.
	 * But if you register one func with multiple commands,
	 * you can tell which command was used by the cmd
	 * parameter.
	 */
	ret = match_records(hash, func, strlen(func), module);
	kfree(func);

	if (!ret)
		return cache_mod(tr, func_orig, module, enable);
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
				      struct ftrace_ops *op, struct ftrace_regs *fregs)
{
	struct ftrace_probe_ops *probe_ops;
	struct ftrace_func_probe *probe;

	probe = container_of(op, struct ftrace_func_probe, ops);
	probe_ops = probe->probe_ops;

	/*
	 * Disable preemption for these calls to prevent a RCU grace
	 * period. This syncs the hash iteration and freeing of items
	 * on the hash. rcu_read_lock is too dangerous here.
	 */
	preempt_disable_notrace();
	probe_ops->func(ip, parent_ip, probe->tr, probe_ops, probe->data);
	preempt_enable_notrace();
}

struct ftrace_func_map {
	struct ftrace_func_entry	entry;
	void				*data;
};

struct ftrace_func_mapper {
	struct ftrace_hash		hash;
};

/**
 * allocate_ftrace_func_mapper - allocate a new ftrace_func_mapper
 *
 * Returns a ftrace_func_mapper descriptor that can be used to map ips to data.
 */
struct ftrace_func_mapper *allocate_ftrace_func_mapper(void)
{
	struct ftrace_hash *hash;

	/*
	 * The mapper is simply a ftrace_hash, but since the entries
	 * in the hash are not ftrace_func_entry type, we define it
	 * as a separate structure.
	 */
	hash = alloc_ftrace_hash(FTRACE_HASH_DEFAULT_BITS);
	return (struct ftrace_func_mapper *)hash;
}

/**
 * ftrace_func_mapper_find_ip - Find some data mapped to an ip
 * @mapper: The mapper that has the ip maps
 * @ip: the instruction pointer to find the data for
 *
 * Returns the data mapped to @ip if found otherwise NULL. The return
 * is actually the address of the mapper data pointer. The address is
 * returned for use cases where the data is no bigger than a long, and
 * the user can use the data pointer as its data instead of having to
 * allocate more memory for the reference.
 */
void **ftrace_func_mapper_find_ip(struct ftrace_func_mapper *mapper,
				  unsigned long ip)
{
	struct ftrace_func_entry *entry;
	struct ftrace_func_map *map;

	entry = ftrace_lookup_ip(&mapper->hash, ip);
	if (!entry)
		return NULL;

	map = (struct ftrace_func_map *)entry;
	return &map->data;
}

/**
 * ftrace_func_mapper_add_ip - Map some data to an ip
 * @mapper: The mapper that has the ip maps
 * @ip: The instruction pointer address to map @data to
 * @data: The data to map to @ip
 *
 * Returns 0 on success otherwise an error.
 */
int ftrace_func_mapper_add_ip(struct ftrace_func_mapper *mapper,
			      unsigned long ip, void *data)
{
	struct ftrace_func_entry *entry;
	struct ftrace_func_map *map;

	entry = ftrace_lookup_ip(&mapper->hash, ip);
	if (entry)
		return -EBUSY;

	map = kmalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	map->entry.ip = ip;
	map->data = data;

	__add_hash_entry(&mapper->hash, &map->entry);

	return 0;
}

/**
 * ftrace_func_mapper_remove_ip - Remove an ip from the mapping
 * @mapper: The mapper that has the ip maps
 * @ip: The instruction pointer address to remove the data from
 *
 * Returns the data if it is found, otherwise NULL.
 * Note, if the data pointer is used as the data itself, (see
 * ftrace_func_mapper_find_ip(), then the return value may be meaningless,
 * if the data pointer was set to zero.
 */
void *ftrace_func_mapper_remove_ip(struct ftrace_func_mapper *mapper,
				   unsigned long ip)
{
	struct ftrace_func_entry *entry;
	struct ftrace_func_map *map;
	void *data;

	entry = ftrace_lookup_ip(&mapper->hash, ip);
	if (!entry)
		return NULL;

	map = (struct ftrace_func_map *)entry;
	data = map->data;

	remove_hash_entry(&mapper->hash, entry);
	kfree(entry);

	return data;
}

/**
 * free_ftrace_func_mapper - free a mapping of ips and data
 * @mapper: The mapper that has the ip maps
 * @free_func: A function to be called on each data item.
 *
 * This is used to free the function mapper. The @free_func is optional
 * and can be used if the data needs to be freed as well.
 */
void free_ftrace_func_mapper(struct ftrace_func_mapper *mapper,
			     ftrace_mapper_func free_func)
{
	struct ftrace_func_entry *entry;
	struct ftrace_func_map *map;
	struct hlist_head *hhd;
	int size, i;

	if (!mapper)
		return;

	if (free_func && mapper->hash.count) {
		size = 1 << mapper->hash.size_bits;
		for (i = 0; i < size; i++) {
			hhd = &mapper->hash.buckets[i];
			hlist_for_each_entry(entry, hhd, hlist) {
				map = (struct ftrace_func_map *)entry;
				free_func(map);
			}
		}
	}
	free_ftrace_hash(&mapper->hash);
}

static void release_probe(struct ftrace_func_probe *probe)
{
	struct ftrace_probe_ops *probe_ops;

	mutex_lock(&ftrace_lock);

	WARN_ON(probe->ref <= 0);

	/* Subtract the ref that was used to protect this instance */
	probe->ref--;

	if (!probe->ref) {
		probe_ops = probe->probe_ops;
		/*
		 * Sending zero as ip tells probe_ops to free
		 * the probe->data itself
		 */
		if (probe_ops->free)
			probe_ops->free(probe_ops, probe->tr, 0, probe->data);
		list_del(&probe->list);
		kfree(probe);
	}
	mutex_unlock(&ftrace_lock);
}

static void acquire_probe_locked(struct ftrace_func_probe *probe)
{
	/*
	 * Add one ref to keep it from being freed when releasing the
	 * ftrace_lock mutex.
	 */
	probe->ref++;
}

int
register_ftrace_function_probe(char *glob, struct trace_array *tr,
			       struct ftrace_probe_ops *probe_ops,
			       void *data)
{
	struct ftrace_func_probe *probe = NULL, *iter;
	struct ftrace_func_entry *entry;
	struct ftrace_hash **orig_hash;
	struct ftrace_hash *old_hash;
	struct ftrace_hash *hash;
	int count = 0;
	int size;
	int ret;
	int i;

	if (WARN_ON(!tr))
		return -EINVAL;

	/* We do not support '!' for function probes */
	if (WARN_ON(glob[0] == '!'))
		return -EINVAL;


	mutex_lock(&ftrace_lock);
	/* Check if the probe_ops is already registered */
	list_for_each_entry(iter, &tr->func_probes, list) {
		if (iter->probe_ops == probe_ops) {
			probe = iter;
			break;
		}
	}
	if (!probe) {
		probe = kzalloc(sizeof(*probe), GFP_KERNEL);
		if (!probe) {
			mutex_unlock(&ftrace_lock);
			return -ENOMEM;
		}
		probe->probe_ops = probe_ops;
		probe->ops.func = function_trace_probe_call;
		probe->tr = tr;
		ftrace_ops_init(&probe->ops);
		list_add(&probe->list, &tr->func_probes);
	}

	acquire_probe_locked(probe);

	mutex_unlock(&ftrace_lock);

	/*
	 * Note, there's a small window here that the func_hash->filter_hash
	 * may be NULL or empty. Need to be careful when reading the loop.
	 */
	mutex_lock(&probe->ops.func_hash->regex_lock);

	orig_hash = &probe->ops.func_hash->filter_hash;
	old_hash = *orig_hash;
	hash = alloc_and_copy_ftrace_hash(FTRACE_HASH_DEFAULT_BITS, old_hash);

	if (!hash) {
		ret = -ENOMEM;
		goto out;
	}

	ret = ftrace_match_records(hash, glob, strlen(glob));

	/* Nothing found? */
	if (!ret)
		ret = -EINVAL;

	if (ret < 0)
		goto out;

	size = 1 << hash->size_bits;
	for (i = 0; i < size; i++) {
		hlist_for_each_entry(entry, &hash->buckets[i], hlist) {
			if (ftrace_lookup_ip(old_hash, entry->ip))
				continue;
			/*
			 * The caller might want to do something special
			 * for each function we find. We call the callback
			 * to give the caller an opportunity to do so.
			 */
			if (probe_ops->init) {
				ret = probe_ops->init(probe_ops, tr,
						      entry->ip, data,
						      &probe->data);
				if (ret < 0) {
					if (probe_ops->free && count)
						probe_ops->free(probe_ops, tr,
								0, probe->data);
					probe->data = NULL;
					goto out;
				}
			}
			count++;
		}
	}

	mutex_lock(&ftrace_lock);

	if (!count) {
		/* Nothing was added? */
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = ftrace_hash_move_and_update_ops(&probe->ops, orig_hash,
					      hash, 1);
	if (ret < 0)
		goto err_unlock;

	/* One ref for each new function traced */
	probe->ref += count;

	if (!(probe->ops.flags & FTRACE_OPS_FL_ENABLED))
		ret = ftrace_startup(&probe->ops, 0);

 out_unlock:
	mutex_unlock(&ftrace_lock);

	if (!ret)
		ret = count;
 out:
	mutex_unlock(&probe->ops.func_hash->regex_lock);
	free_ftrace_hash(hash);

	release_probe(probe);

	return ret;

 err_unlock:
	if (!probe_ops->free || !count)
		goto out_unlock;

	/* Failed to do the move, need to call the free functions */
	for (i = 0; i < size; i++) {
		hlist_for_each_entry(entry, &hash->buckets[i], hlist) {
			if (ftrace_lookup_ip(old_hash, entry->ip))
				continue;
			probe_ops->free(probe_ops, tr, entry->ip, probe->data);
		}
	}
	goto out_unlock;
}

int
unregister_ftrace_function_probe_func(char *glob, struct trace_array *tr,
				      struct ftrace_probe_ops *probe_ops)
{
	struct ftrace_func_probe *probe = NULL, *iter;
	struct ftrace_ops_hash old_hash_ops;
	struct ftrace_func_entry *entry;
	struct ftrace_glob func_g;
	struct ftrace_hash **orig_hash;
	struct ftrace_hash *old_hash;
	struct ftrace_hash *hash = NULL;
	struct hlist_node *tmp;
	struct hlist_head hhd;
	char str[KSYM_SYMBOL_LEN];
	int count = 0;
	int i, ret = -ENODEV;
	int size;

	if (!glob || !strlen(glob) || !strcmp(glob, "*"))
		func_g.search = NULL;
	else {
		int not;

		func_g.type = filter_parse_regex(glob, strlen(glob),
						 &func_g.search, &not);
		func_g.len = strlen(func_g.search);

		/* we do not support '!' for function probes */
		if (WARN_ON(not))
			return -EINVAL;
	}

	mutex_lock(&ftrace_lock);
	/* Check if the probe_ops is already registered */
	list_for_each_entry(iter, &tr->func_probes, list) {
		if (iter->probe_ops == probe_ops) {
			probe = iter;
			break;
		}
	}
	if (!probe)
		goto err_unlock_ftrace;

	ret = -EINVAL;
	if (!(probe->ops.flags & FTRACE_OPS_FL_INITIALIZED))
		goto err_unlock_ftrace;

	acquire_probe_locked(probe);

	mutex_unlock(&ftrace_lock);

	mutex_lock(&probe->ops.func_hash->regex_lock);

	orig_hash = &probe->ops.func_hash->filter_hash;
	old_hash = *orig_hash;

	if (ftrace_hash_empty(old_hash))
		goto out_unlock;

	old_hash_ops.filter_hash = old_hash;
	/* Probes only have filters */
	old_hash_ops.notrace_hash = NULL;

	ret = -ENOMEM;
	hash = alloc_and_copy_ftrace_hash(FTRACE_HASH_DEFAULT_BITS, old_hash);
	if (!hash)
		goto out_unlock;

	INIT_HLIST_HEAD(&hhd);

	size = 1 << hash->size_bits;
	for (i = 0; i < size; i++) {
		hlist_for_each_entry_safe(entry, tmp, &hash->buckets[i], hlist) {

			if (func_g.search) {
				kallsyms_lookup(entry->ip, NULL, NULL,
						NULL, str);
				if (!ftrace_match(str, &func_g))
					continue;
			}
			count++;
			remove_hash_entry(hash, entry);
			hlist_add_head(&entry->hlist, &hhd);
		}
	}

	/* Nothing found? */
	if (!count) {
		ret = -EINVAL;
		goto out_unlock;
	}

	mutex_lock(&ftrace_lock);

	WARN_ON(probe->ref < count);

	probe->ref -= count;

	if (ftrace_hash_empty(hash))
		ftrace_shutdown(&probe->ops, 0);

	ret = ftrace_hash_move_and_update_ops(&probe->ops, orig_hash,
					      hash, 1);

	/* still need to update the function call sites */
	if (ftrace_enabled && !ftrace_hash_empty(hash))
		ftrace_run_modify_code(&probe->ops, FTRACE_UPDATE_CALLS,
				       &old_hash_ops);
	synchronize_rcu();

	hlist_for_each_entry_safe(entry, tmp, &hhd, hlist) {
		hlist_del(&entry->hlist);
		if (probe_ops->free)
			probe_ops->free(probe_ops, tr, entry->ip, probe->data);
		kfree(entry);
	}
	mutex_unlock(&ftrace_lock);

 out_unlock:
	mutex_unlock(&probe->ops.func_hash->regex_lock);
	free_ftrace_hash(hash);

	release_probe(probe);

	return ret;

 err_unlock_ftrace:
	mutex_unlock(&ftrace_lock);
	return ret;
}

void clear_ftrace_function_probes(struct trace_array *tr)
{
	struct ftrace_func_probe *probe, *n;

	list_for_each_entry_safe(probe, n, &tr->func_probes, list)
		unregister_ftrace_function_probe_func(NULL, tr, probe->probe_ops);
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

static int ftrace_process_regex(struct ftrace_iterator *iter,
				char *buff, int len, int enable)
{
	struct ftrace_hash *hash = iter->hash;
	struct trace_array *tr = iter->ops->private;
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
			ret = p->func(tr, hash, func, command, next, enable);
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
		ret = ftrace_process_regex(iter, parser->buffer,
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
__ftrace_match_addr(struct ftrace_hash *hash, unsigned long ip, int remove)
{
	struct ftrace_func_entry *entry;

	ip = ftrace_location(ip);
	if (!ip)
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

static int
ftrace_match_addr(struct ftrace_hash *hash, unsigned long *ips,
		  unsigned int cnt, int remove)
{
	unsigned int i;
	int err;

	for (i = 0; i < cnt; i++) {
		err = __ftrace_match_addr(hash, ips[i], remove);
		if (err) {
			/*
			 * This expects the @hash is a temporary hash and if this
			 * fails the caller must free the @hash.
			 */
			return err;
		}
	}
	return 0;
}

static int
ftrace_set_hash(struct ftrace_ops *ops, unsigned char *buf, int len,
		unsigned long *ips, unsigned int cnt,
		int remove, int reset, int enable)
{
	struct ftrace_hash **orig_hash;
	struct ftrace_hash *hash;
	int ret;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	mutex_lock(&ops->func_hash->regex_lock);

	if (enable)
		orig_hash = &ops->func_hash->filter_hash;
	else
		orig_hash = &ops->func_hash->notrace_hash;

	if (reset)
		hash = alloc_ftrace_hash(FTRACE_HASH_DEFAULT_BITS);
	else
		hash = alloc_and_copy_ftrace_hash(FTRACE_HASH_DEFAULT_BITS, *orig_hash);

	if (!hash) {
		ret = -ENOMEM;
		goto out_regex_unlock;
	}

	if (buf && !ftrace_match_records(hash, buf, len)) {
		ret = -EINVAL;
		goto out_regex_unlock;
	}
	if (ips) {
		ret = ftrace_match_addr(hash, ips, cnt, remove);
		if (ret < 0)
			goto out_regex_unlock;
	}

	mutex_lock(&ftrace_lock);
	ret = ftrace_hash_move_and_update_ops(ops, orig_hash, hash, enable);
	mutex_unlock(&ftrace_lock);

 out_regex_unlock:
	mutex_unlock(&ops->func_hash->regex_lock);

	free_ftrace_hash(hash);
	return ret;
}

static int
ftrace_set_addr(struct ftrace_ops *ops, unsigned long *ips, unsigned int cnt,
		int remove, int reset, int enable)
{
	return ftrace_set_hash(ops, NULL, 0, ips, cnt, remove, reset, enable);
}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS

struct ftrace_direct_func {
	struct list_head	next;
	unsigned long		addr;
	int			count;
};

static LIST_HEAD(ftrace_direct_funcs);

/**
 * ftrace_find_direct_func - test an address if it is a registered direct caller
 * @addr: The address of a registered direct caller
 *
 * This searches to see if a ftrace direct caller has been registered
 * at a specific address, and if so, it returns a descriptor for it.
 *
 * This can be used by architecture code to see if an address is
 * a direct caller (trampoline) attached to a fentry/mcount location.
 * This is useful for the function_graph tracer, as it may need to
 * do adjustments if it traced a location that also has a direct
 * trampoline attached to it.
 */
struct ftrace_direct_func *ftrace_find_direct_func(unsigned long addr)
{
	struct ftrace_direct_func *entry;
	bool found = false;

	/* May be called by fgraph trampoline (protected by rcu tasks) */
	list_for_each_entry_rcu(entry, &ftrace_direct_funcs, next) {
		if (entry->addr == addr) {
			found = true;
			break;
		}
	}
	if (found)
		return entry;

	return NULL;
}

static struct ftrace_direct_func *ftrace_alloc_direct_func(unsigned long addr)
{
	struct ftrace_direct_func *direct;

	direct = kmalloc(sizeof(*direct), GFP_KERNEL);
	if (!direct)
		return NULL;
	direct->addr = addr;
	direct->count = 0;
	list_add_rcu(&direct->next, &ftrace_direct_funcs);
	ftrace_direct_func_count++;
	return direct;
}

/**
 * register_ftrace_direct - Call a custom trampoline directly
 * @ip: The address of the nop at the beginning of a function
 * @addr: The address of the trampoline to call at @ip
 *
 * This is used to connect a direct call from the nop location (@ip)
 * at the start of ftrace traced functions. The location that it calls
 * (@addr) must be able to handle a direct call, and save the parameters
 * of the function being traced, and restore them (or inject new ones
 * if needed), before returning.
 *
 * Returns:
 *  0 on success
 *  -EBUSY - Another direct function is already attached (there can be only one)
 *  -ENODEV - @ip does not point to a ftrace nop location (or not supported)
 *  -ENOMEM - There was an allocation failure.
 */
int register_ftrace_direct(unsigned long ip, unsigned long addr)
{
	struct ftrace_direct_func *direct;
	struct ftrace_func_entry *entry;
	struct ftrace_hash *free_hash = NULL;
	struct dyn_ftrace *rec;
	int ret = -ENODEV;

	mutex_lock(&direct_mutex);

	ip = ftrace_location(ip);
	if (!ip)
		goto out_unlock;

	/* See if there's a direct function at @ip already */
	ret = -EBUSY;
	if (ftrace_find_rec_direct(ip))
		goto out_unlock;

	ret = -ENODEV;
	rec = lookup_rec(ip, ip);
	if (!rec)
		goto out_unlock;

	/*
	 * Check if the rec says it has a direct call but we didn't
	 * find one earlier?
	 */
	if (WARN_ON(rec->flags & FTRACE_FL_DIRECT))
		goto out_unlock;

	/* Make sure the ip points to the exact record */
	if (ip != rec->ip) {
		ip = rec->ip;
		/* Need to check this ip for a direct. */
		if (ftrace_find_rec_direct(ip))
			goto out_unlock;
	}

	ret = -ENOMEM;
	direct = ftrace_find_direct_func(addr);
	if (!direct) {
		direct = ftrace_alloc_direct_func(addr);
		if (!direct)
			goto out_unlock;
	}

	entry = ftrace_add_rec_direct(ip, addr, &free_hash);
	if (!entry)
		goto out_unlock;

	ret = ftrace_set_filter_ip(&direct_ops, ip, 0, 0);

	if (!ret && !(direct_ops.flags & FTRACE_OPS_FL_ENABLED)) {
		ret = register_ftrace_function(&direct_ops);
		if (ret)
			ftrace_set_filter_ip(&direct_ops, ip, 1, 0);
	}

	if (ret) {
		remove_hash_entry(direct_functions, entry);
		kfree(entry);
		if (!direct->count) {
			list_del_rcu(&direct->next);
			synchronize_rcu_tasks();
			kfree(direct);
			if (free_hash)
				free_ftrace_hash(free_hash);
			free_hash = NULL;
			ftrace_direct_func_count--;
		}
	} else {
		direct->count++;
	}
 out_unlock:
	mutex_unlock(&direct_mutex);

	if (free_hash) {
		synchronize_rcu_tasks();
		free_ftrace_hash(free_hash);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(register_ftrace_direct);

static struct ftrace_func_entry *find_direct_entry(unsigned long *ip,
						   struct dyn_ftrace **recp)
{
	struct ftrace_func_entry *entry;
	struct dyn_ftrace *rec;

	rec = lookup_rec(*ip, *ip);
	if (!rec)
		return NULL;

	entry = __ftrace_lookup_ip(direct_functions, rec->ip);
	if (!entry) {
		WARN_ON(rec->flags & FTRACE_FL_DIRECT);
		return NULL;
	}

	WARN_ON(!(rec->flags & FTRACE_FL_DIRECT));

	/* Passed in ip just needs to be on the call site */
	*ip = rec->ip;

	if (recp)
		*recp = rec;

	return entry;
}

int unregister_ftrace_direct(unsigned long ip, unsigned long addr)
{
	struct ftrace_direct_func *direct;
	struct ftrace_func_entry *entry;
	struct ftrace_hash *hash;
	int ret = -ENODEV;

	mutex_lock(&direct_mutex);

	ip = ftrace_location(ip);
	if (!ip)
		goto out_unlock;

	entry = find_direct_entry(&ip, NULL);
	if (!entry)
		goto out_unlock;

	hash = direct_ops.func_hash->filter_hash;
	if (hash->count == 1)
		unregister_ftrace_function(&direct_ops);

	ret = ftrace_set_filter_ip(&direct_ops, ip, 1, 0);

	WARN_ON(ret);

	remove_hash_entry(direct_functions, entry);

	direct = ftrace_find_direct_func(addr);
	if (!WARN_ON(!direct)) {
		/* This is the good path (see the ! before WARN) */
		direct->count--;
		WARN_ON(direct->count < 0);
		if (!direct->count) {
			list_del_rcu(&direct->next);
			synchronize_rcu_tasks();
			kfree(direct);
			kfree(entry);
			ftrace_direct_func_count--;
		}
	}
 out_unlock:
	mutex_unlock(&direct_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(unregister_ftrace_direct);

static struct ftrace_ops stub_ops = {
	.func		= ftrace_stub,
};

/**
 * ftrace_modify_direct_caller - modify ftrace nop directly
 * @entry: The ftrace hash entry of the direct helper for @rec
 * @rec: The record representing the function site to patch
 * @old_addr: The location that the site at @rec->ip currently calls
 * @new_addr: The location that the site at @rec->ip should call
 *
 * An architecture may overwrite this function to optimize the
 * changing of the direct callback on an ftrace nop location.
 * This is called with the ftrace_lock mutex held, and no other
 * ftrace callbacks are on the associated record (@rec). Thus,
 * it is safe to modify the ftrace record, where it should be
 * currently calling @old_addr directly, to call @new_addr.
 *
 * Safety checks should be made to make sure that the code at
 * @rec->ip is currently calling @old_addr. And this must
 * also update entry->direct to @new_addr.
 */
int __weak ftrace_modify_direct_caller(struct ftrace_func_entry *entry,
				       struct dyn_ftrace *rec,
				       unsigned long old_addr,
				       unsigned long new_addr)
{
	unsigned long ip = rec->ip;
	int ret;

	/*
	 * The ftrace_lock was used to determine if the record
	 * had more than one registered user to it. If it did,
	 * we needed to prevent that from changing to do the quick
	 * switch. But if it did not (only a direct caller was attached)
	 * then this function is called. But this function can deal
	 * with attached callers to the rec that we care about, and
	 * since this function uses standard ftrace calls that take
	 * the ftrace_lock mutex, we need to release it.
	 */
	mutex_unlock(&ftrace_lock);

	/*
	 * By setting a stub function at the same address, we force
	 * the code to call the iterator and the direct_ops helper.
	 * This means that @ip does not call the direct call, and
	 * we can simply modify it.
	 */
	ret = ftrace_set_filter_ip(&stub_ops, ip, 0, 0);
	if (ret)
		goto out_lock;

	ret = register_ftrace_function(&stub_ops);
	if (ret) {
		ftrace_set_filter_ip(&stub_ops, ip, 1, 0);
		goto out_lock;
	}

	entry->direct = new_addr;

	/*
	 * By removing the stub, we put back the direct call, calling
	 * the @new_addr.
	 */
	unregister_ftrace_function(&stub_ops);
	ftrace_set_filter_ip(&stub_ops, ip, 1, 0);

 out_lock:
	mutex_lock(&ftrace_lock);

	return ret;
}

/**
 * modify_ftrace_direct - Modify an existing direct call to call something else
 * @ip: The instruction pointer to modify
 * @old_addr: The address that the current @ip calls directly
 * @new_addr: The address that the @ip should call
 *
 * This modifies a ftrace direct caller at an instruction pointer without
 * having to disable it first. The direct call will switch over to the
 * @new_addr without missing anything.
 *
 * Returns: zero on success. Non zero on error, which includes:
 *  -ENODEV : the @ip given has no direct caller attached
 *  -EINVAL : the @old_addr does not match the current direct caller
 */
int modify_ftrace_direct(unsigned long ip,
			 unsigned long old_addr, unsigned long new_addr)
{
	struct ftrace_direct_func *direct, *new_direct = NULL;
	struct ftrace_func_entry *entry;
	struct dyn_ftrace *rec;
	int ret = -ENODEV;

	mutex_lock(&direct_mutex);

	mutex_lock(&ftrace_lock);

	ip = ftrace_location(ip);
	if (!ip)
		goto out_unlock;

	entry = find_direct_entry(&ip, &rec);
	if (!entry)
		goto out_unlock;

	ret = -EINVAL;
	if (entry->direct != old_addr)
		goto out_unlock;

	direct = ftrace_find_direct_func(old_addr);
	if (WARN_ON(!direct))
		goto out_unlock;
	if (direct->count > 1) {
		ret = -ENOMEM;
		new_direct = ftrace_alloc_direct_func(new_addr);
		if (!new_direct)
			goto out_unlock;
		direct->count--;
		new_direct->count++;
	} else {
		direct->addr = new_addr;
	}

	/*
	 * If there's no other ftrace callback on the rec->ip location,
	 * then it can be changed directly by the architecture.
	 * If there is another caller, then we just need to change the
	 * direct caller helper to point to @new_addr.
	 */
	if (ftrace_rec_count(rec) == 1) {
		ret = ftrace_modify_direct_caller(entry, rec, old_addr, new_addr);
	} else {
		entry->direct = new_addr;
		ret = 0;
	}

	if (unlikely(ret && new_direct)) {
		direct->count++;
		list_del_rcu(&new_direct->next);
		synchronize_rcu_tasks();
		kfree(new_direct);
		ftrace_direct_func_count--;
	}

 out_unlock:
	mutex_unlock(&ftrace_lock);
	mutex_unlock(&direct_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(modify_ftrace_direct);

#define MULTI_FLAGS (FTRACE_OPS_FL_IPMODIFY | FTRACE_OPS_FL_DIRECT | \
		     FTRACE_OPS_FL_SAVE_REGS)

static int check_direct_multi(struct ftrace_ops *ops)
{
	if (!(ops->flags & FTRACE_OPS_FL_INITIALIZED))
		return -EINVAL;
	if ((ops->flags & MULTI_FLAGS) != MULTI_FLAGS)
		return -EINVAL;
	return 0;
}

static void remove_direct_functions_hash(struct ftrace_hash *hash, unsigned long addr)
{
	struct ftrace_func_entry *entry, *del;
	int size, i;

	size = 1 << hash->size_bits;
	for (i = 0; i < size; i++) {
		hlist_for_each_entry(entry, &hash->buckets[i], hlist) {
			del = __ftrace_lookup_ip(direct_functions, entry->ip);
			if (del && del->direct == addr) {
				remove_hash_entry(direct_functions, del);
				kfree(del);
			}
		}
	}
}

/**
 * register_ftrace_direct_multi - Call a custom trampoline directly
 * for multiple functions registered in @ops
 * @ops: The address of the struct ftrace_ops object
 * @addr: The address of the trampoline to call at @ops functions
 *
 * This is used to connect a direct calls to @addr from the nop locations
 * of the functions registered in @ops (with by ftrace_set_filter_ip
 * function).
 *
 * The location that it calls (@addr) must be able to handle a direct call,
 * and save the parameters of the function being traced, and restore them
 * (or inject new ones if needed), before returning.
 *
 * Returns:
 *  0 on success
 *  -EINVAL  - The @ops object was already registered with this call or
 *             when there are no functions in @ops object.
 *  -EBUSY   - Another direct function is already attached (there can be only one)
 *  -ENODEV  - @ip does not point to a ftrace nop location (or not supported)
 *  -ENOMEM  - There was an allocation failure.
 */
int register_ftrace_direct_multi(struct ftrace_ops *ops, unsigned long addr)
{
	struct ftrace_hash *hash, *free_hash = NULL;
	struct ftrace_func_entry *entry, *new;
	int err = -EBUSY, size, i;

	if (ops->func || ops->trampoline)
		return -EINVAL;
	if (!(ops->flags & FTRACE_OPS_FL_INITIALIZED))
		return -EINVAL;
	if (ops->flags & FTRACE_OPS_FL_ENABLED)
		return -EINVAL;

	hash = ops->func_hash->filter_hash;
	if (ftrace_hash_empty(hash))
		return -EINVAL;

	mutex_lock(&direct_mutex);

	/* Make sure requested entries are not already registered.. */
	size = 1 << hash->size_bits;
	for (i = 0; i < size; i++) {
		hlist_for_each_entry(entry, &hash->buckets[i], hlist) {
			if (ftrace_find_rec_direct(entry->ip))
				goto out_unlock;
		}
	}

	/* ... and insert them to direct_functions hash. */
	err = -ENOMEM;
	for (i = 0; i < size; i++) {
		hlist_for_each_entry(entry, &hash->buckets[i], hlist) {
			new = ftrace_add_rec_direct(entry->ip, addr, &free_hash);
			if (!new)
				goto out_remove;
			entry->direct = addr;
		}
	}

	ops->func = call_direct_funcs;
	ops->flags = MULTI_FLAGS;
	ops->trampoline = FTRACE_REGS_ADDR;

	err = register_ftrace_function(ops);

 out_remove:
	if (err)
		remove_direct_functions_hash(hash, addr);

 out_unlock:
	mutex_unlock(&direct_mutex);

	if (free_hash) {
		synchronize_rcu_tasks();
		free_ftrace_hash(free_hash);
	}
	return err;
}
EXPORT_SYMBOL_GPL(register_ftrace_direct_multi);

/**
 * unregister_ftrace_direct_multi - Remove calls to custom trampoline
 * previously registered by register_ftrace_direct_multi for @ops object.
 * @ops: The address of the struct ftrace_ops object
 *
 * This is used to remove a direct calls to @addr from the nop locations
 * of the functions registered in @ops (with by ftrace_set_filter_ip
 * function).
 *
 * Returns:
 *  0 on success
 *  -EINVAL - The @ops object was not properly registered.
 */
int unregister_ftrace_direct_multi(struct ftrace_ops *ops, unsigned long addr)
{
	struct ftrace_hash *hash = ops->func_hash->filter_hash;
	int err;

	if (check_direct_multi(ops))
		return -EINVAL;
	if (!(ops->flags & FTRACE_OPS_FL_ENABLED))
		return -EINVAL;

	mutex_lock(&direct_mutex);
	err = unregister_ftrace_function(ops);
	remove_direct_functions_hash(hash, addr);
	mutex_unlock(&direct_mutex);

	/* cleanup for possible another register call */
	ops->func = NULL;
	ops->trampoline = 0;
	return err;
}
EXPORT_SYMBOL_GPL(unregister_ftrace_direct_multi);

/**
 * modify_ftrace_direct_multi - Modify an existing direct 'multi' call
 * to call something else
 * @ops: The address of the struct ftrace_ops object
 * @addr: The address of the new trampoline to call at @ops functions
 *
 * This is used to unregister currently registered direct caller and
 * register new one @addr on functions registered in @ops object.
 *
 * Note there's window between ftrace_shutdown and ftrace_startup calls
 * where there will be no callbacks called.
 *
 * Returns: zero on success. Non zero on error, which includes:
 *  -EINVAL - The @ops object was not properly registered.
 */
int modify_ftrace_direct_multi(struct ftrace_ops *ops, unsigned long addr)
{
	struct ftrace_hash *hash;
	struct ftrace_func_entry *entry, *iter;
	static struct ftrace_ops tmp_ops = {
		.func		= ftrace_stub,
		.flags		= FTRACE_OPS_FL_STUB,
	};
	int i, size;
	int err;

	if (check_direct_multi(ops))
		return -EINVAL;
	if (!(ops->flags & FTRACE_OPS_FL_ENABLED))
		return -EINVAL;

	mutex_lock(&direct_mutex);

	/* Enable the tmp_ops to have the same functions as the direct ops */
	ftrace_ops_init(&tmp_ops);
	tmp_ops.func_hash = ops->func_hash;

	err = register_ftrace_function(&tmp_ops);
	if (err)
		goto out_direct;

	/*
	 * Now the ftrace_ops_list_func() is called to do the direct callers.
	 * We can safely change the direct functions attached to each entry.
	 */
	mutex_lock(&ftrace_lock);

	hash = ops->func_hash->filter_hash;
	size = 1 << hash->size_bits;
	for (i = 0; i < size; i++) {
		hlist_for_each_entry(iter, &hash->buckets[i], hlist) {
			entry = __ftrace_lookup_ip(direct_functions, iter->ip);
			if (!entry)
				continue;
			entry->direct = addr;
		}
	}

	mutex_unlock(&ftrace_lock);

	/* Removing the tmp_ops will add the updated direct callers to the functions */
	unregister_ftrace_function(&tmp_ops);

 out_direct:
	mutex_unlock(&direct_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(modify_ftrace_direct_multi);
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS */

/**
 * ftrace_set_filter_ip - set a function to filter on in ftrace by address
 * @ops - the ops to set the filter with
 * @ip - the address to add to or remove from the filter.
 * @remove - non zero to remove the ip from the filter
 * @reset - non zero to reset all filters before applying this filter.
 *
 * Filters denote which functions should be enabled when tracing is enabled
 * If @ip is NULL, it fails to update filter.
 */
int ftrace_set_filter_ip(struct ftrace_ops *ops, unsigned long ip,
			 int remove, int reset)
{
	ftrace_ops_init(ops);
	return ftrace_set_addr(ops, &ip, 1, remove, reset, 1);
}
EXPORT_SYMBOL_GPL(ftrace_set_filter_ip);

/**
 * ftrace_set_filter_ips - set functions to filter on in ftrace by addresses
 * @ops - the ops to set the filter with
 * @ips - the array of addresses to add to or remove from the filter.
 * @cnt - the number of addresses in @ips
 * @remove - non zero to remove ips from the filter
 * @reset - non zero to reset all filters before applying this filter.
 *
 * Filters denote which functions should be enabled when tracing is enabled
 * If @ips array or any ip specified within is NULL , it fails to update filter.
 */
int ftrace_set_filter_ips(struct ftrace_ops *ops, unsigned long *ips,
			  unsigned int cnt, int remove, int reset)
{
	ftrace_ops_init(ops);
	return ftrace_set_addr(ops, ips, cnt, remove, reset, 1);
}
EXPORT_SYMBOL_GPL(ftrace_set_filter_ips);

/**
 * ftrace_ops_set_global_filter - setup ops to use global filters
 * @ops - the ops which will use the global filters
 *
 * ftrace users who need global function trace filtering should call this.
 * It can set the global filter only if ops were not initialized before.
 */
void ftrace_ops_set_global_filter(struct ftrace_ops *ops)
{
	if (ops->flags & FTRACE_OPS_FL_INITIALIZED)
		return;

	ftrace_ops_init(ops);
	ops->func_hash = &global_ops.local_hash;
}
EXPORT_SYMBOL_GPL(ftrace_ops_set_global_filter);

static int
ftrace_set_regex(struct ftrace_ops *ops, unsigned char *buf, int len,
		 int reset, int enable)
{
	return ftrace_set_hash(ops, buf, len, NULL, 0, 0, reset, enable);
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
 * ftrace_set_global_filter - set a function to filter on with global tracers
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
 * ftrace_set_global_notrace - set a function to not trace with global tracers
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
static char ftrace_graph_notrace_buf[FTRACE_FILTER_SIZE] __initdata;
static int ftrace_graph_set_hash(struct ftrace_hash *hash, char *buffer);

static int __init set_graph_function(char *str)
{
	strlcpy(ftrace_graph_buf, str, FTRACE_FILTER_SIZE);
	return 1;
}
__setup("ftrace_graph_filter=", set_graph_function);

static int __init set_graph_notrace_function(char *str)
{
	strlcpy(ftrace_graph_notrace_buf, str, FTRACE_FILTER_SIZE);
	return 1;
}
__setup("ftrace_graph_notrace=", set_graph_notrace_function);

static int __init set_graph_max_depth_function(char *str)
{
	if (!str)
		return 0;
	fgraph_max_depth = simple_strtoul(str, NULL, 0);
	return 1;
}
__setup("ftrace_graph_max_depth=", set_graph_max_depth_function);

static void __init set_ftrace_early_graph(char *buf, int enable)
{
	int ret;
	char *func;
	struct ftrace_hash *hash;

	hash = alloc_ftrace_hash(FTRACE_HASH_DEFAULT_BITS);
	if (MEM_FAIL(!hash, "Failed to allocate hash\n"))
		return;

	while (buf) {
		func = strsep(&buf, ",");
		/* we allow only one expression at a time */
		ret = ftrace_graph_set_hash(hash, func);
		if (ret)
			printk(KERN_DEBUG "ftrace: function %s not "
					  "traceable\n", func);
	}

	if (enable)
		ftrace_graph_hash = hash;
	else
		ftrace_graph_notrace_hash = hash;
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
		set_ftrace_early_graph(ftrace_graph_buf, 1);
	if (ftrace_graph_notrace_buf[0])
		set_ftrace_early_graph(ftrace_graph_notrace_buf, 0);
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
}

int ftrace_regex_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct ftrace_iterator *iter;
	struct ftrace_hash **orig_hash;
	struct trace_parser *parser;
	int filter_hash;

	if (file->f_mode & FMODE_READ) {
		iter = m->private;
		seq_release(inode, file);
	} else
		iter = file->private_data;

	parser = &iter->parser;
	if (trace_parser_loaded(parser)) {
		int enable = !(iter->flags & FTRACE_ITER_NOTRACE);

		ftrace_process_regex(iter, parser->buffer,
				     parser->idx, enable);
	}

	trace_parser_put(parser);

	mutex_lock(&iter->ops->func_hash->regex_lock);

	if (file->f_mode & FMODE_WRITE) {
		filter_hash = !!(iter->flags & FTRACE_ITER_FILTER);

		if (filter_hash) {
			orig_hash = &iter->ops->func_hash->filter_hash;
			if (iter->tr && !list_empty(&iter->tr->mod_trace))
				iter->hash->flags |= FTRACE_HASH_FL_MOD;
		} else
			orig_hash = &iter->ops->func_hash->notrace_hash;

		mutex_lock(&ftrace_lock);
		ftrace_hash_move_and_update_ops(iter->ops, orig_hash,
						      iter->hash, filter_hash);
		mutex_unlock(&ftrace_lock);
	} else {
		/* For read only, the hash is the ops hash */
		iter->hash = NULL;
	}

	mutex_unlock(&iter->ops->func_hash->regex_lock);
	free_ftrace_hash(iter->hash);
	if (iter->tr)
		trace_array_put(iter->tr);
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
	.llseek = tracing_lseek,
	.release = ftrace_regex_release,
};

static const struct file_operations ftrace_notrace_fops = {
	.open = ftrace_notrace_open,
	.read = seq_read,
	.write = ftrace_notrace_write,
	.llseek = tracing_lseek,
	.release = ftrace_regex_release,
};

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

static DEFINE_MUTEX(graph_lock);

struct ftrace_hash __rcu *ftrace_graph_hash = EMPTY_HASH;
struct ftrace_hash __rcu *ftrace_graph_notrace_hash = EMPTY_HASH;

enum graph_filter_type {
	GRAPH_FILTER_NOTRACE	= 0,
	GRAPH_FILTER_FUNCTION,
};

#define FTRACE_GRAPH_EMPTY	((void *)1)

struct ftrace_graph_data {
	struct ftrace_hash		*hash;
	struct ftrace_func_entry	*entry;
	int				idx;   /* for hash table iteration */
	enum graph_filter_type		type;
	struct ftrace_hash		*new_hash;
	const struct seq_operations	*seq_ops;
	struct trace_parser		parser;
};

static void *
__g_next(struct seq_file *m, loff_t *pos)
{
	struct ftrace_graph_data *fgd = m->private;
	struct ftrace_func_entry *entry = fgd->entry;
	struct hlist_head *head;
	int i, idx = fgd->idx;

	if (*pos >= fgd->hash->count)
		return NULL;

	if (entry) {
		hlist_for_each_entry_continue(entry, hlist) {
			fgd->entry = entry;
			return entry;
		}

		idx++;
	}

	for (i = idx; i < 1 << fgd->hash->size_bits; i++) {
		head = &fgd->hash->buckets[i];
		hlist_for_each_entry(entry, head, hlist) {
			fgd->entry = entry;
			fgd->idx = i;
			return entry;
		}
	}
	return NULL;
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

	if (fgd->type == GRAPH_FILTER_FUNCTION)
		fgd->hash = rcu_dereference_protected(ftrace_graph_hash,
					lockdep_is_held(&graph_lock));
	else
		fgd->hash = rcu_dereference_protected(ftrace_graph_notrace_hash,
					lockdep_is_held(&graph_lock));

	/* Nothing, tell g_show to print all functions are enabled */
	if (ftrace_hash_empty(fgd->hash) && !*pos)
		return FTRACE_GRAPH_EMPTY;

	fgd->idx = 0;
	fgd->entry = NULL;
	return __g_next(m, pos);
}

static void g_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&graph_lock);
}

static int g_show(struct seq_file *m, void *v)
{
	struct ftrace_func_entry *entry = v;

	if (!entry)
		return 0;

	if (entry == FTRACE_GRAPH_EMPTY) {
		struct ftrace_graph_data *fgd = m->private;

		if (fgd->type == GRAPH_FILTER_FUNCTION)
			seq_puts(m, "#### all functions enabled ####\n");
		else
			seq_puts(m, "#### no functions disabled ####\n");
		return 0;
	}

	seq_printf(m, "%ps\n", (void *)entry->ip);

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
	int ret;
	struct ftrace_hash *new_hash = NULL;

	ret = security_locked_down(LOCKDOWN_TRACEFS);
	if (ret)
		return ret;

	if (file->f_mode & FMODE_WRITE) {
		const int size_bits = FTRACE_HASH_DEFAULT_BITS;

		if (trace_parser_get_init(&fgd->parser, FTRACE_BUFF_MAX))
			return -ENOMEM;

		if (file->f_flags & O_TRUNC)
			new_hash = alloc_ftrace_hash(size_bits);
		else
			new_hash = alloc_and_copy_ftrace_hash(size_bits,
							      fgd->hash);
		if (!new_hash) {
			ret = -ENOMEM;
			goto out;
		}
	}

	if (file->f_mode & FMODE_READ) {
		ret = seq_open(file, &ftrace_graph_seq_ops);
		if (!ret) {
			struct seq_file *m = file->private_data;
			m->private = fgd;
		} else {
			/* Failed */
			free_ftrace_hash(new_hash);
			new_hash = NULL;
		}
	} else
		file->private_data = fgd;

out:
	if (ret < 0 && file->f_mode & FMODE_WRITE)
		trace_parser_put(&fgd->parser);

	fgd->new_hash = new_hash;

	/*
	 * All uses of fgd->hash must be taken with the graph_lock
	 * held. The graph_lock is going to be released, so force
	 * fgd->hash to be reinitialized when it is taken again.
	 */
	fgd->hash = NULL;

	return ret;
}

static int
ftrace_graph_open(struct inode *inode, struct file *file)
{
	struct ftrace_graph_data *fgd;
	int ret;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	fgd = kmalloc(sizeof(*fgd), GFP_KERNEL);
	if (fgd == NULL)
		return -ENOMEM;

	mutex_lock(&graph_lock);

	fgd->hash = rcu_dereference_protected(ftrace_graph_hash,
					lockdep_is_held(&graph_lock));
	fgd->type = GRAPH_FILTER_FUNCTION;
	fgd->seq_ops = &ftrace_graph_seq_ops;

	ret = __ftrace_graph_open(inode, file, fgd);
	if (ret < 0)
		kfree(fgd);

	mutex_unlock(&graph_lock);
	return ret;
}

static int
ftrace_graph_notrace_open(struct inode *inode, struct file *file)
{
	struct ftrace_graph_data *fgd;
	int ret;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	fgd = kmalloc(sizeof(*fgd), GFP_KERNEL);
	if (fgd == NULL)
		return -ENOMEM;

	mutex_lock(&graph_lock);

	fgd->hash = rcu_dereference_protected(ftrace_graph_notrace_hash,
					lockdep_is_held(&graph_lock));
	fgd->type = GRAPH_FILTER_NOTRACE;
	fgd->seq_ops = &ftrace_graph_seq_ops;

	ret = __ftrace_graph_open(inode, file, fgd);
	if (ret < 0)
		kfree(fgd);

	mutex_unlock(&graph_lock);
	return ret;
}

static int
ftrace_graph_release(struct inode *inode, struct file *file)
{
	struct ftrace_graph_data *fgd;
	struct ftrace_hash *old_hash, *new_hash;
	struct trace_parser *parser;
	int ret = 0;

	if (file->f_mode & FMODE_READ) {
		struct seq_file *m = file->private_data;

		fgd = m->private;
		seq_release(inode, file);
	} else {
		fgd = file->private_data;
	}


	if (file->f_mode & FMODE_WRITE) {

		parser = &fgd->parser;

		if (trace_parser_loaded((parser))) {
			ret = ftrace_graph_set_hash(fgd->new_hash,
						    parser->buffer);
		}

		trace_parser_put(parser);

		new_hash = __ftrace_hash_move(fgd->new_hash);
		if (!new_hash) {
			ret = -ENOMEM;
			goto out;
		}

		mutex_lock(&graph_lock);

		if (fgd->type == GRAPH_FILTER_FUNCTION) {
			old_hash = rcu_dereference_protected(ftrace_graph_hash,
					lockdep_is_held(&graph_lock));
			rcu_assign_pointer(ftrace_graph_hash, new_hash);
		} else {
			old_hash = rcu_dereference_protected(ftrace_graph_notrace_hash,
					lockdep_is_held(&graph_lock));
			rcu_assign_pointer(ftrace_graph_notrace_hash, new_hash);
		}

		mutex_unlock(&graph_lock);

		/*
		 * We need to do a hard force of sched synchronization.
		 * This is because we use preempt_disable() to do RCU, but
		 * the function tracers can be called where RCU is not watching
		 * (like before user_exit()). We can not rely on the RCU
		 * infrastructure to do the synchronization, thus we must do it
		 * ourselves.
		 */
		if (old_hash != EMPTY_HASH)
			synchronize_rcu_tasks_rude();

		free_ftrace_hash(old_hash);
	}

 out:
	free_ftrace_hash(fgd->new_hash);
	kfree(fgd);

	return ret;
}

static int
ftrace_graph_set_hash(struct ftrace_hash *hash, char *buffer)
{
	struct ftrace_glob func_g;
	struct dyn_ftrace *rec;
	struct ftrace_page *pg;
	struct ftrace_func_entry *entry;
	int fail = 1;
	int not;

	/* decode regex */
	func_g.type = filter_parse_regex(buffer, strlen(buffer),
					 &func_g.search, &not);

	func_g.len = strlen(func_g.search);

	mutex_lock(&ftrace_lock);

	if (unlikely(ftrace_disabled)) {
		mutex_unlock(&ftrace_lock);
		return -ENODEV;
	}

	do_for_each_ftrace_rec(pg, rec) {

		if (rec->flags & FTRACE_FL_DISABLED)
			continue;

		if (ftrace_match_record(rec, &func_g, NULL, 0)) {
			entry = ftrace_lookup_ip(hash, rec->ip);

			if (!not) {
				fail = 0;

				if (entry)
					continue;
				if (add_hash_entry(hash, rec->ip) < 0)
					goto out;
			} else {
				if (entry) {
					free_hash_entry(hash, entry);
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
	ssize_t read, ret = 0;
	struct ftrace_graph_data *fgd = file->private_data;
	struct trace_parser *parser;

	if (!cnt)
		return 0;

	/* Read mode uses seq functions */
	if (file->f_mode & FMODE_READ) {
		struct seq_file *m = file->private_data;
		fgd = m->private;
	}

	parser = &fgd->parser;

	read = trace_get_user(parser, ubuf, cnt, ppos);

	if (read >= 0 && trace_parser_loaded(parser) &&
	    !trace_parser_cont(parser)) {

		ret = ftrace_graph_set_hash(fgd->new_hash,
					    parser->buffer);
		trace_parser_clear(parser);
	}

	if (!ret)
		ret = read;

	return ret;
}

static const struct file_operations ftrace_graph_fops = {
	.open		= ftrace_graph_open,
	.read		= seq_read,
	.write		= ftrace_graph_write,
	.llseek		= tracing_lseek,
	.release	= ftrace_graph_release,
};

static const struct file_operations ftrace_graph_notrace_fops = {
	.open		= ftrace_graph_notrace_open,
	.read		= seq_read,
	.write		= ftrace_graph_write,
	.llseek		= tracing_lseek,
	.release	= ftrace_graph_release,
};
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

void ftrace_create_filter_files(struct ftrace_ops *ops,
				struct dentry *parent)
{

	trace_create_file("set_ftrace_filter", TRACE_MODE_WRITE, parent,
			  ops, &ftrace_filter_fops);

	trace_create_file("set_ftrace_notrace", TRACE_MODE_WRITE, parent,
			  ops, &ftrace_notrace_fops);
}

/*
 * The name "destroy_filter_files" is really a misnomer. Although
 * in the future, it may actually delete the files, but this is
 * really intended to make sure the ops passed in are disabled
 * and that when this function returns, the caller is free to
 * free the ops.
 *
 * The "destroy" name is only to match the "create" name that this
 * should be paired with.
 */
void ftrace_destroy_filter_files(struct ftrace_ops *ops)
{
	mutex_lock(&ftrace_lock);
	if (ops->flags & FTRACE_OPS_FL_ENABLED)
		ftrace_shutdown(ops, 0);
	ops->flags |= FTRACE_OPS_FL_DELETED;
	ftrace_free_filter(ops);
	mutex_unlock(&ftrace_lock);
}

static __init int ftrace_init_dyn_tracefs(struct dentry *d_tracer)
{

	trace_create_file("available_filter_functions", TRACE_MODE_READ,
			d_tracer, NULL, &ftrace_avail_fops);

	trace_create_file("enabled_functions", TRACE_MODE_READ,
			d_tracer, NULL, &ftrace_enabled_fops);

	ftrace_create_filter_files(&global_ops, d_tracer);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	trace_create_file("set_graph_function", TRACE_MODE_WRITE, d_tracer,
				    NULL,
				    &ftrace_graph_fops);
	trace_create_file("set_graph_notrace", TRACE_MODE_WRITE, d_tracer,
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

#ifdef CONFIG_FTRACE_SORT_STARTUP_TEST
static void test_is_sorted(unsigned long *start, unsigned long count)
{
	int i;

	for (i = 1; i < count; i++) {
		if (WARN(start[i - 1] > start[i],
			 "[%d] %pS at %lx is not sorted with %pS at %lx\n", i,
			 (void *)start[i - 1], start[i - 1],
			 (void *)start[i], start[i]))
			break;
	}
	if (i == count)
		pr_info("ftrace section at %px sorted properly\n", start);
}
#else
static void test_is_sorted(unsigned long *start, unsigned long count)
{
}
#endif

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

	/*
	 * Sorting mcount in vmlinux at build time depend on
	 * CONFIG_BUILDTIME_MCOUNT_SORT, while mcount loc in
	 * modules can not be sorted at build time.
	 */
	if (!IS_ENABLED(CONFIG_BUILDTIME_MCOUNT_SORT) || mod) {
		sort(start, count, sizeof(*start),
		     ftrace_cmp_ips, NULL);
	} else {
		test_is_sorted(start, count);
	}

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
		unsigned long end_offset;
		addr = ftrace_call_adjust(*p++);
		/*
		 * Some architecture linkers will pad between
		 * the different mcount_loc sections of different
		 * object files to satisfy alignments.
		 * Skip any NULL pointers.
		 */
		if (!addr)
			continue;

		end_offset = (pg->index+1) * sizeof(pg->records[0]);
		if (end_offset > PAGE_SIZE << pg->order) {
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
	ftrace_update_code(mod, start_pg);
	if (!mod)
		local_irq_restore(flags);
	ret = 0;
 out:
	mutex_unlock(&ftrace_lock);

	return ret;
}

struct ftrace_mod_func {
	struct list_head	list;
	char			*name;
	unsigned long		ip;
	unsigned int		size;
};

struct ftrace_mod_map {
	struct rcu_head		rcu;
	struct list_head	list;
	struct module		*mod;
	unsigned long		start_addr;
	unsigned long		end_addr;
	struct list_head	funcs;
	unsigned int		num_funcs;
};

static int ftrace_get_trampoline_kallsym(unsigned int symnum,
					 unsigned long *value, char *type,
					 char *name, char *module_name,
					 int *exported)
{
	struct ftrace_ops *op;

	list_for_each_entry_rcu(op, &ftrace_ops_trampoline_list, list) {
		if (!op->trampoline || symnum--)
			continue;
		*value = op->trampoline;
		*type = 't';
		strlcpy(name, FTRACE_TRAMPOLINE_SYM, KSYM_NAME_LEN);
		strlcpy(module_name, FTRACE_TRAMPOLINE_MOD, MODULE_NAME_LEN);
		*exported = 0;
		return 0;
	}

	return -ERANGE;
}

#ifdef CONFIG_MODULES

#define next_to_ftrace_page(p) container_of(p, struct ftrace_page, next)

static LIST_HEAD(ftrace_mod_maps);

static int referenced_filters(struct dyn_ftrace *rec)
{
	struct ftrace_ops *ops;
	int cnt = 0;

	for (ops = ftrace_ops_list; ops != &ftrace_list_end; ops = ops->next) {
		if (ops_references_rec(ops, rec)) {
			if (WARN_ON_ONCE(ops->flags & FTRACE_OPS_FL_DIRECT))
				continue;
			if (WARN_ON_ONCE(ops->flags & FTRACE_OPS_FL_IPMODIFY))
				continue;
			cnt++;
			if (ops->flags & FTRACE_OPS_FL_SAVE_REGS)
				rec->flags |= FTRACE_FL_REGS;
			if (cnt == 1 && ops->trampoline)
				rec->flags |= FTRACE_FL_TRAMP;
			else
				rec->flags &= ~FTRACE_FL_TRAMP;
		}
	}

	return cnt;
}

static void
clear_mod_from_hash(struct ftrace_page *pg, struct ftrace_hash *hash)
{
	struct ftrace_func_entry *entry;
	struct dyn_ftrace *rec;
	int i;

	if (ftrace_hash_empty(hash))
		return;

	for (i = 0; i < pg->index; i++) {
		rec = &pg->records[i];
		entry = __ftrace_lookup_ip(hash, rec->ip);
		/*
		 * Do not allow this rec to match again.
		 * Yeah, it may waste some memory, but will be removed
		 * if/when the hash is modified again.
		 */
		if (entry)
			entry->ip = 0;
	}
}

/* Clear any records from hashes */
static void clear_mod_from_hashes(struct ftrace_page *pg)
{
	struct trace_array *tr;

	mutex_lock(&trace_types_lock);
	list_for_each_entry(tr, &ftrace_trace_arrays, list) {
		if (!tr->ops || !tr->ops->func_hash)
			continue;
		mutex_lock(&tr->ops->func_hash->regex_lock);
		clear_mod_from_hash(pg, tr->ops->func_hash->filter_hash);
		clear_mod_from_hash(pg, tr->ops->func_hash->notrace_hash);
		mutex_unlock(&tr->ops->func_hash->regex_lock);
	}
	mutex_unlock(&trace_types_lock);
}

static void ftrace_free_mod_map(struct rcu_head *rcu)
{
	struct ftrace_mod_map *mod_map = container_of(rcu, struct ftrace_mod_map, rcu);
	struct ftrace_mod_func *mod_func;
	struct ftrace_mod_func *n;

	/* All the contents of mod_map are now not visible to readers */
	list_for_each_entry_safe(mod_func, n, &mod_map->funcs, list) {
		kfree(mod_func->name);
		list_del(&mod_func->list);
		kfree(mod_func);
	}

	kfree(mod_map);
}

void ftrace_release_mod(struct module *mod)
{
	struct ftrace_mod_map *mod_map;
	struct ftrace_mod_map *n;
	struct dyn_ftrace *rec;
	struct ftrace_page **last_pg;
	struct ftrace_page *tmp_page = NULL;
	struct ftrace_page *pg;

	mutex_lock(&ftrace_lock);

	if (ftrace_disabled)
		goto out_unlock;

	list_for_each_entry_safe(mod_map, n, &ftrace_mod_maps, list) {
		if (mod_map->mod == mod) {
			list_del_rcu(&mod_map->list);
			call_rcu(&mod_map->rcu, ftrace_free_mod_map);
			break;
		}
	}

	/*
	 * Each module has its own ftrace_pages, remove
	 * them from the list.
	 */
	last_pg = &ftrace_pages_start;
	for (pg = ftrace_pages_start; pg; pg = *last_pg) {
		rec = &pg->records[0];
		if (within_module_core(rec->ip, mod) ||
		    within_module_init(rec->ip, mod)) {
			/*
			 * As core pages are first, the first
			 * page should never be a module page.
			 */
			if (WARN_ON(pg == ftrace_pages_start))
				goto out_unlock;

			/* Check if we are deleting the last page */
			if (pg == ftrace_pages)
				ftrace_pages = next_to_ftrace_page(last_pg);

			ftrace_update_tot_cnt -= pg->index;
			*last_pg = pg->next;

			pg->next = tmp_page;
			tmp_page = pg;
		} else
			last_pg = &pg->next;
	}
 out_unlock:
	mutex_unlock(&ftrace_lock);

	for (pg = tmp_page; pg; pg = tmp_page) {

		/* Needs to be called outside of ftrace_lock */
		clear_mod_from_hashes(pg);

		if (pg->records) {
			free_pages((unsigned long)pg->records, pg->order);
			ftrace_number_of_pages -= 1 << pg->order;
		}
		tmp_page = pg->next;
		kfree(pg);
		ftrace_number_of_groups--;
	}
}

void ftrace_module_enable(struct module *mod)
{
	struct dyn_ftrace *rec;
	struct ftrace_page *pg;

	mutex_lock(&ftrace_lock);

	if (ftrace_disabled)
		goto out_unlock;

	/*
	 * If the tracing is enabled, go ahead and enable the record.
	 *
	 * The reason not to enable the record immediately is the
	 * inherent check of ftrace_make_nop/ftrace_make_call for
	 * correct previous instructions.  Making first the NOP
	 * conversion puts the module to the correct state, thus
	 * passing the ftrace_make_call check.
	 *
	 * We also delay this to after the module code already set the
	 * text to read-only, as we now need to set it back to read-write
	 * so that we can modify the text.
	 */
	if (ftrace_start_up)
		ftrace_arch_code_modify_prepare();

	do_for_each_ftrace_rec(pg, rec) {
		int cnt;
		/*
		 * do_for_each_ftrace_rec() is a double loop.
		 * module text shares the pg. If a record is
		 * not part of this module, then skip this pg,
		 * which the "break" will do.
		 */
		if (!within_module_core(rec->ip, mod) &&
		    !within_module_init(rec->ip, mod))
			break;

		/* Weak functions should still be ignored */
		if (!test_for_valid_rec(rec)) {
			/* Clear all other flags. Should not be enabled anyway */
			rec->flags = FTRACE_FL_DISABLED;
			continue;
		}

		cnt = 0;

		/*
		 * When adding a module, we need to check if tracers are
		 * currently enabled and if they are, and can trace this record,
		 * we need to enable the module functions as well as update the
		 * reference counts for those function records.
		 */
		if (ftrace_start_up)
			cnt += referenced_filters(rec);

		rec->flags &= ~FTRACE_FL_DISABLED;
		rec->flags += cnt;

		if (ftrace_start_up && cnt) {
			int failed = __ftrace_replace_code(rec, 1);
			if (failed) {
				ftrace_bug(failed, rec);
				goto out_loop;
			}
		}

	} while_for_each_ftrace_rec();

 out_loop:
	if (ftrace_start_up)
		ftrace_arch_code_modify_post_process();

 out_unlock:
	mutex_unlock(&ftrace_lock);

	process_cached_mods(mod->name);
}

void ftrace_module_init(struct module *mod)
{
	int ret;

	if (ftrace_disabled || !mod->num_ftrace_callsites)
		return;

	ret = ftrace_process_locs(mod, mod->ftrace_callsites,
				  mod->ftrace_callsites + mod->num_ftrace_callsites);
	if (ret)
		pr_warn("ftrace: failed to allocate entries for module '%s' functions\n",
			mod->name);
}

static void save_ftrace_mod_rec(struct ftrace_mod_map *mod_map,
				struct dyn_ftrace *rec)
{
	struct ftrace_mod_func *mod_func;
	unsigned long symsize;
	unsigned long offset;
	char str[KSYM_SYMBOL_LEN];
	char *modname;
	const char *ret;

	ret = kallsyms_lookup(rec->ip, &symsize, &offset, &modname, str);
	if (!ret)
		return;

	mod_func = kmalloc(sizeof(*mod_func), GFP_KERNEL);
	if (!mod_func)
		return;

	mod_func->name = kstrdup(str, GFP_KERNEL);
	if (!mod_func->name) {
		kfree(mod_func);
		return;
	}

	mod_func->ip = rec->ip - offset;
	mod_func->size = symsize;

	mod_map->num_funcs++;

	list_add_rcu(&mod_func->list, &mod_map->funcs);
}

static struct ftrace_mod_map *
allocate_ftrace_mod_map(struct module *mod,
			unsigned long start, unsigned long end)
{
	struct ftrace_mod_map *mod_map;

	mod_map = kmalloc(sizeof(*mod_map), GFP_KERNEL);
	if (!mod_map)
		return NULL;

	mod_map->mod = mod;
	mod_map->start_addr = start;
	mod_map->end_addr = end;
	mod_map->num_funcs = 0;

	INIT_LIST_HEAD_RCU(&mod_map->funcs);

	list_add_rcu(&mod_map->list, &ftrace_mod_maps);

	return mod_map;
}

static const char *
ftrace_func_address_lookup(struct ftrace_mod_map *mod_map,
			   unsigned long addr, unsigned long *size,
			   unsigned long *off, char *sym)
{
	struct ftrace_mod_func *found_func =  NULL;
	struct ftrace_mod_func *mod_func;

	list_for_each_entry_rcu(mod_func, &mod_map->funcs, list) {
		if (addr >= mod_func->ip &&
		    addr < mod_func->ip + mod_func->size) {
			found_func = mod_func;
			break;
		}
	}

	if (found_func) {
		if (size)
			*size = found_func->size;
		if (off)
			*off = addr - found_func->ip;
		if (sym)
			strlcpy(sym, found_func->name, KSYM_NAME_LEN);

		return found_func->name;
	}

	return NULL;
}

const char *
ftrace_mod_address_lookup(unsigned long addr, unsigned long *size,
		   unsigned long *off, char **modname, char *sym)
{
	struct ftrace_mod_map *mod_map;
	const char *ret = NULL;

	/* mod_map is freed via call_rcu() */
	preempt_disable();
	list_for_each_entry_rcu(mod_map, &ftrace_mod_maps, list) {
		ret = ftrace_func_address_lookup(mod_map, addr, size, off, sym);
		if (ret) {
			if (modname)
				*modname = mod_map->mod->name;
			break;
		}
	}
	preempt_enable();

	return ret;
}

int ftrace_mod_get_kallsym(unsigned int symnum, unsigned long *value,
			   char *type, char *name,
			   char *module_name, int *exported)
{
	struct ftrace_mod_map *mod_map;
	struct ftrace_mod_func *mod_func;
	int ret;

	preempt_disable();
	list_for_each_entry_rcu(mod_map, &ftrace_mod_maps, list) {

		if (symnum >= mod_map->num_funcs) {
			symnum -= mod_map->num_funcs;
			continue;
		}

		list_for_each_entry_rcu(mod_func, &mod_map->funcs, list) {
			if (symnum > 1) {
				symnum--;
				continue;
			}

			*value = mod_func->ip;
			*type = 'T';
			strlcpy(name, mod_func->name, KSYM_NAME_LEN);
			strlcpy(module_name, mod_map->mod->name, MODULE_NAME_LEN);
			*exported = 1;
			preempt_enable();
			return 0;
		}
		WARN_ON(1);
		break;
	}
	ret = ftrace_get_trampoline_kallsym(symnum, value, type, name,
					    module_name, exported);
	preempt_enable();
	return ret;
}

#else
static void save_ftrace_mod_rec(struct ftrace_mod_map *mod_map,
				struct dyn_ftrace *rec) { }
static inline struct ftrace_mod_map *
allocate_ftrace_mod_map(struct module *mod,
			unsigned long start, unsigned long end)
{
	return NULL;
}
int ftrace_mod_get_kallsym(unsigned int symnum, unsigned long *value,
			   char *type, char *name, char *module_name,
			   int *exported)
{
	int ret;

	preempt_disable();
	ret = ftrace_get_trampoline_kallsym(symnum, value, type, name,
					    module_name, exported);
	preempt_enable();
	return ret;
}
#endif /* CONFIG_MODULES */

struct ftrace_init_func {
	struct list_head list;
	unsigned long ip;
};

/* Clear any init ips from hashes */
static void
clear_func_from_hash(struct ftrace_init_func *func, struct ftrace_hash *hash)
{
	struct ftrace_func_entry *entry;

	entry = ftrace_lookup_ip(hash, func->ip);
	/*
	 * Do not allow this rec to match again.
	 * Yeah, it may waste some memory, but will be removed
	 * if/when the hash is modified again.
	 */
	if (entry)
		entry->ip = 0;
}

static void
clear_func_from_hashes(struct ftrace_init_func *func)
{
	struct trace_array *tr;

	mutex_lock(&trace_types_lock);
	list_for_each_entry(tr, &ftrace_trace_arrays, list) {
		if (!tr->ops || !tr->ops->func_hash)
			continue;
		mutex_lock(&tr->ops->func_hash->regex_lock);
		clear_func_from_hash(func, tr->ops->func_hash->filter_hash);
		clear_func_from_hash(func, tr->ops->func_hash->notrace_hash);
		mutex_unlock(&tr->ops->func_hash->regex_lock);
	}
	mutex_unlock(&trace_types_lock);
}

static void add_to_clear_hash_list(struct list_head *clear_list,
				   struct dyn_ftrace *rec)
{
	struct ftrace_init_func *func;

	func = kmalloc(sizeof(*func), GFP_KERNEL);
	if (!func) {
		MEM_FAIL(1, "alloc failure, ftrace filter could be stale\n");
		return;
	}

	func->ip = rec->ip;
	list_add(&func->list, clear_list);
}

void ftrace_free_mem(struct module *mod, void *start_ptr, void *end_ptr)
{
	unsigned long start = (unsigned long)(start_ptr);
	unsigned long end = (unsigned long)(end_ptr);
	struct ftrace_page **last_pg = &ftrace_pages_start;
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	struct dyn_ftrace key;
	struct ftrace_mod_map *mod_map = NULL;
	struct ftrace_init_func *func, *func_next;
	struct list_head clear_hash;

	INIT_LIST_HEAD(&clear_hash);

	key.ip = start;
	key.flags = end;	/* overload flags, as it is unsigned long */

	mutex_lock(&ftrace_lock);

	/*
	 * If we are freeing module init memory, then check if
	 * any tracer is active. If so, we need to save a mapping of
	 * the module functions being freed with the address.
	 */
	if (mod && ftrace_ops_list != &ftrace_list_end)
		mod_map = allocate_ftrace_mod_map(mod, start, end);

	for (pg = ftrace_pages_start; pg; last_pg = &pg->next, pg = *last_pg) {
		if (end < pg->records[0].ip ||
		    start >= (pg->records[pg->index - 1].ip + MCOUNT_INSN_SIZE))
			continue;
 again:
		rec = bsearch(&key, pg->records, pg->index,
			      sizeof(struct dyn_ftrace),
			      ftrace_cmp_recs);
		if (!rec)
			continue;

		/* rec will be cleared from hashes after ftrace_lock unlock */
		add_to_clear_hash_list(&clear_hash, rec);

		if (mod_map)
			save_ftrace_mod_rec(mod_map, rec);

		pg->index--;
		ftrace_update_tot_cnt--;
		if (!pg->index) {
			*last_pg = pg->next;
			if (pg->records) {
				free_pages((unsigned long)pg->records, pg->order);
				ftrace_number_of_pages -= 1 << pg->order;
			}
			ftrace_number_of_groups--;
			kfree(pg);
			pg = container_of(last_pg, struct ftrace_page, next);
			if (!(*last_pg))
				ftrace_pages = pg;
			continue;
		}
		memmove(rec, rec + 1,
			(pg->index - (rec - pg->records)) * sizeof(*rec));
		/* More than one function may be in this block */
		goto again;
	}
	mutex_unlock(&ftrace_lock);

	list_for_each_entry_safe(func, func_next, &clear_hash, list) {
		clear_func_from_hashes(func);
		kfree(func);
	}
}

void __init ftrace_free_init_mem(void)
{
	void *start = (void *)(&__init_begin);
	void *end = (void *)(&__init_end);

	ftrace_boot_snapshot();

	ftrace_free_mem(NULL, start, end);
}

int __init __weak ftrace_dyn_arch_init(void)
{
	return 0;
}

void __init ftrace_init(void)
{
	extern unsigned long __start_mcount_loc[];
	extern unsigned long __stop_mcount_loc[];
	unsigned long count, flags;
	int ret;

	local_irq_save(flags);
	ret = ftrace_dyn_arch_init();
	local_irq_restore(flags);
	if (ret)
		goto failed;

	count = __stop_mcount_loc - __start_mcount_loc;
	if (!count) {
		pr_info("ftrace: No functions to be traced?\n");
		goto failed;
	}

	pr_info("ftrace: allocating %ld entries in %ld pages\n",
		count, count / ENTRIES_PER_PAGE + 1);

	ret = ftrace_process_locs(NULL,
				  __start_mcount_loc,
				  __stop_mcount_loc);
	if (ret) {
		pr_warn("ftrace: failed to allocate entries for functions\n");
		goto failed;
	}

	pr_info("ftrace: allocated %ld pages with %ld groups\n",
		ftrace_number_of_pages, ftrace_number_of_groups);

	last_ftrace_enabled = ftrace_enabled = 1;

	set_ftrace_early_filters();

	return;
 failed:
	ftrace_disabled = 1;
}

/* Do nothing if arch does not support this */
void __weak arch_ftrace_update_trampoline(struct ftrace_ops *ops)
{
}

static void ftrace_update_trampoline(struct ftrace_ops *ops)
{
	unsigned long trampoline = ops->trampoline;

	arch_ftrace_update_trampoline(ops);
	if (ops->trampoline && ops->trampoline != trampoline &&
	    (ops->flags & FTRACE_OPS_FL_ALLOC_TRAMP)) {
		/* Add to kallsyms before the perf events */
		ftrace_add_trampoline_to_kallsyms(ops);
		perf_event_ksymbol(PERF_RECORD_KSYMBOL_TYPE_OOL,
				   ops->trampoline, ops->trampoline_size, false,
				   FTRACE_TRAMPOLINE_SYM);
		/*
		 * Record the perf text poke event after the ksymbol register
		 * event.
		 */
		perf_event_text_poke((void *)ops->trampoline, NULL, 0,
				     (void *)ops->trampoline,
				     ops->trampoline_size);
	}
}

void ftrace_init_trace_array(struct trace_array *tr)
{
	INIT_LIST_HEAD(&tr->func_probes);
	INIT_LIST_HEAD(&tr->mod_trace);
	INIT_LIST_HEAD(&tr->mod_notrace);
}
#else

struct ftrace_ops global_ops = {
	.func			= ftrace_stub,
	.flags			= FTRACE_OPS_FL_INITIALIZED |
				  FTRACE_OPS_FL_PID,
};

static int __init ftrace_nodyn_init(void)
{
	ftrace_enabled = 1;
	return 0;
}
core_initcall(ftrace_nodyn_init);

static inline int ftrace_init_dyn_tracefs(struct dentry *d_tracer) { return 0; }
static inline void ftrace_startup_all(int command) { }

static void ftrace_update_trampoline(struct ftrace_ops *ops)
{
}

#endif /* CONFIG_DYNAMIC_FTRACE */

__init void ftrace_init_global_array_ops(struct trace_array *tr)
{
	tr->ops = &global_ops;
	tr->ops->private = tr;
	ftrace_init_trace_array(tr);
}

void ftrace_init_array_ops(struct trace_array *tr, ftrace_func_t func)
{
	/* If we filter on pids, update to use the pid function */
	if (tr->flags & TRACE_ARRAY_FL_GLOBAL) {
		if (WARN_ON(tr->ops->func != ftrace_stub))
			printk("ftrace ops had %pS for function\n",
			       tr->ops->func);
	}
	tr->ops->func = func;
	tr->ops->private = tr;
}

void ftrace_reset_array_ops(struct trace_array *tr)
{
	tr->ops->func = ftrace_stub;
}

static nokprobe_inline void
__ftrace_ops_list_func(unsigned long ip, unsigned long parent_ip,
		       struct ftrace_ops *ignored, struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);
	struct ftrace_ops *op;
	int bit;

	/*
	 * The ftrace_test_and_set_recursion() will disable preemption,
	 * which is required since some of the ops may be dynamically
	 * allocated, they must be freed after a synchronize_rcu().
	 */
	bit = trace_test_and_set_recursion(ip, parent_ip, TRACE_LIST_START);
	if (bit < 0)
		return;

	do_for_each_ftrace_op(op, ftrace_ops_list) {
		/* Stub functions don't need to be called nor tested */
		if (op->flags & FTRACE_OPS_FL_STUB)
			continue;
		/*
		 * Check the following for each ops before calling their func:
		 *  if RCU flag is set, then rcu_is_watching() must be true
		 *  if PER_CPU is set, then ftrace_function_local_disable()
		 *                          must be false
		 *  Otherwise test if the ip matches the ops filter
		 *
		 * If any of the above fails then the op->func() is not executed.
		 */
		if ((!(op->flags & FTRACE_OPS_FL_RCU) || rcu_is_watching()) &&
		    ftrace_ops_test(op, ip, regs)) {
			if (FTRACE_WARN_ON(!op->func)) {
				pr_warn("op=%p %pS\n", op, op);
				goto out;
			}
			op->func(ip, parent_ip, op, fregs);
		}
	} while_for_each_ftrace_op(op);
out:
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
 * set the ARCH_SUPPORTS_FTRACE_OPS.
 *
 * In vmlinux.lds.h, ftrace_ops_list_func() is defined to be
 * arch_ftrace_ops_list_func.
 */
#if ARCH_SUPPORTS_FTRACE_OPS
void arch_ftrace_ops_list_func(unsigned long ip, unsigned long parent_ip,
			       struct ftrace_ops *op, struct ftrace_regs *fregs)
{
	__ftrace_ops_list_func(ip, parent_ip, NULL, fregs);
}
#else
void arch_ftrace_ops_list_func(unsigned long ip, unsigned long parent_ip)
{
	__ftrace_ops_list_func(ip, parent_ip, NULL, NULL);
}
#endif
NOKPROBE_SYMBOL(arch_ftrace_ops_list_func);

/*
 * If there's only one function registered but it does not support
 * recursion, needs RCU protection and/or requires per cpu handling, then
 * this function will be called by the mcount trampoline.
 */
static void ftrace_ops_assist_func(unsigned long ip, unsigned long parent_ip,
				   struct ftrace_ops *op, struct ftrace_regs *fregs)
{
	int bit;

	bit = trace_test_and_set_recursion(ip, parent_ip, TRACE_LIST_START);
	if (bit < 0)
		return;

	if (!(op->flags & FTRACE_OPS_FL_RCU) || rcu_is_watching())
		op->func(ip, parent_ip, op, fregs);

	trace_clear_recursion(bit);
}
NOKPROBE_SYMBOL(ftrace_ops_assist_func);

/**
 * ftrace_ops_get_func - get the function a trampoline should call
 * @ops: the ops to get the function for
 *
 * Normally the mcount trampoline will call the ops->func, but there
 * are times that it should not. For example, if the ops does not
 * have its own recursion protection, then it should call the
 * ftrace_ops_assist_func() instead.
 *
 * Returns the function that the trampoline should call for @ops.
 */
ftrace_func_t ftrace_ops_get_func(struct ftrace_ops *ops)
{
	/*
	 * If the function does not handle recursion or needs to be RCU safe,
	 * then we need to call the assist handler.
	 */
	if (ops->flags & (FTRACE_OPS_FL_RECURSION |
			  FTRACE_OPS_FL_RCU))
		return ftrace_ops_assist_func;

	return ops->func;
}

static void
ftrace_filter_pid_sched_switch_probe(void *data, bool preempt,
				     struct task_struct *prev,
				     struct task_struct *next,
				     unsigned int prev_state)
{
	struct trace_array *tr = data;
	struct trace_pid_list *pid_list;
	struct trace_pid_list *no_pid_list;

	pid_list = rcu_dereference_sched(tr->function_pids);
	no_pid_list = rcu_dereference_sched(tr->function_no_pids);

	if (trace_ignore_this_task(pid_list, no_pid_list, next))
		this_cpu_write(tr->array_buffer.data->ftrace_ignore_pid,
			       FTRACE_PID_IGNORE);
	else
		this_cpu_write(tr->array_buffer.data->ftrace_ignore_pid,
			       next->pid);
}

static void
ftrace_pid_follow_sched_process_fork(void *data,
				     struct task_struct *self,
				     struct task_struct *task)
{
	struct trace_pid_list *pid_list;
	struct trace_array *tr = data;

	pid_list = rcu_dereference_sched(tr->function_pids);
	trace_filter_add_remove_task(pid_list, self, task);

	pid_list = rcu_dereference_sched(tr->function_no_pids);
	trace_filter_add_remove_task(pid_list, self, task);
}

static void
ftrace_pid_follow_sched_process_exit(void *data, struct task_struct *task)
{
	struct trace_pid_list *pid_list;
	struct trace_array *tr = data;

	pid_list = rcu_dereference_sched(tr->function_pids);
	trace_filter_add_remove_task(pid_list, NULL, task);

	pid_list = rcu_dereference_sched(tr->function_no_pids);
	trace_filter_add_remove_task(pid_list, NULL, task);
}

void ftrace_pid_follow_fork(struct trace_array *tr, bool enable)
{
	if (enable) {
		register_trace_sched_process_fork(ftrace_pid_follow_sched_process_fork,
						  tr);
		register_trace_sched_process_free(ftrace_pid_follow_sched_process_exit,
						  tr);
	} else {
		unregister_trace_sched_process_fork(ftrace_pid_follow_sched_process_fork,
						    tr);
		unregister_trace_sched_process_free(ftrace_pid_follow_sched_process_exit,
						    tr);
	}
}

static void clear_ftrace_pids(struct trace_array *tr, int type)
{
	struct trace_pid_list *pid_list;
	struct trace_pid_list *no_pid_list;
	int cpu;

	pid_list = rcu_dereference_protected(tr->function_pids,
					     lockdep_is_held(&ftrace_lock));
	no_pid_list = rcu_dereference_protected(tr->function_no_pids,
						lockdep_is_held(&ftrace_lock));

	/* Make sure there's something to do */
	if (!pid_type_enabled(type, pid_list, no_pid_list))
		return;

	/* See if the pids still need to be checked after this */
	if (!still_need_pid_events(type, pid_list, no_pid_list)) {
		unregister_trace_sched_switch(ftrace_filter_pid_sched_switch_probe, tr);
		for_each_possible_cpu(cpu)
			per_cpu_ptr(tr->array_buffer.data, cpu)->ftrace_ignore_pid = FTRACE_PID_TRACE;
	}

	if (type & TRACE_PIDS)
		rcu_assign_pointer(tr->function_pids, NULL);

	if (type & TRACE_NO_PIDS)
		rcu_assign_pointer(tr->function_no_pids, NULL);

	/* Wait till all users are no longer using pid filtering */
	synchronize_rcu();

	if ((type & TRACE_PIDS) && pid_list)
		trace_pid_list_free(pid_list);

	if ((type & TRACE_NO_PIDS) && no_pid_list)
		trace_pid_list_free(no_pid_list);
}

void ftrace_clear_pids(struct trace_array *tr)
{
	mutex_lock(&ftrace_lock);

	clear_ftrace_pids(tr, TRACE_PIDS | TRACE_NO_PIDS);

	mutex_unlock(&ftrace_lock);
}

static void ftrace_pid_reset(struct trace_array *tr, int type)
{
	mutex_lock(&ftrace_lock);
	clear_ftrace_pids(tr, type);

	ftrace_update_pid_func();
	ftrace_startup_all(0);

	mutex_unlock(&ftrace_lock);
}

/* Greater than any max PID */
#define FTRACE_NO_PIDS		(void *)(PID_MAX_LIMIT + 1)

static void *fpid_start(struct seq_file *m, loff_t *pos)
	__acquires(RCU)
{
	struct trace_pid_list *pid_list;
	struct trace_array *tr = m->private;

	mutex_lock(&ftrace_lock);
	rcu_read_lock_sched();

	pid_list = rcu_dereference_sched(tr->function_pids);

	if (!pid_list)
		return !(*pos) ? FTRACE_NO_PIDS : NULL;

	return trace_pid_start(pid_list, pos);
}

static void *fpid_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct trace_array *tr = m->private;
	struct trace_pid_list *pid_list = rcu_dereference_sched(tr->function_pids);

	if (v == FTRACE_NO_PIDS) {
		(*pos)++;
		return NULL;
	}
	return trace_pid_next(pid_list, v, pos);
}

static void fpid_stop(struct seq_file *m, void *p)
	__releases(RCU)
{
	rcu_read_unlock_sched();
	mutex_unlock(&ftrace_lock);
}

static int fpid_show(struct seq_file *m, void *v)
{
	if (v == FTRACE_NO_PIDS) {
		seq_puts(m, "no pid\n");
		return 0;
	}

	return trace_pid_show(m, v);
}

static const struct seq_operations ftrace_pid_sops = {
	.start = fpid_start,
	.next = fpid_next,
	.stop = fpid_stop,
	.show = fpid_show,
};

static void *fnpid_start(struct seq_file *m, loff_t *pos)
	__acquires(RCU)
{
	struct trace_pid_list *pid_list;
	struct trace_array *tr = m->private;

	mutex_lock(&ftrace_lock);
	rcu_read_lock_sched();

	pid_list = rcu_dereference_sched(tr->function_no_pids);

	if (!pid_list)
		return !(*pos) ? FTRACE_NO_PIDS : NULL;

	return trace_pid_start(pid_list, pos);
}

static void *fnpid_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct trace_array *tr = m->private;
	struct trace_pid_list *pid_list = rcu_dereference_sched(tr->function_no_pids);

	if (v == FTRACE_NO_PIDS) {
		(*pos)++;
		return NULL;
	}
	return trace_pid_next(pid_list, v, pos);
}

static const struct seq_operations ftrace_no_pid_sops = {
	.start = fnpid_start,
	.next = fnpid_next,
	.stop = fpid_stop,
	.show = fpid_show,
};

static int pid_open(struct inode *inode, struct file *file, int type)
{
	const struct seq_operations *seq_ops;
	struct trace_array *tr = inode->i_private;
	struct seq_file *m;
	int ret = 0;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC))
		ftrace_pid_reset(tr, type);

	switch (type) {
	case TRACE_PIDS:
		seq_ops = &ftrace_pid_sops;
		break;
	case TRACE_NO_PIDS:
		seq_ops = &ftrace_no_pid_sops;
		break;
	default:
		trace_array_put(tr);
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	ret = seq_open(file, seq_ops);
	if (ret < 0) {
		trace_array_put(tr);
	} else {
		m = file->private_data;
		/* copy tr over to seq ops */
		m->private = tr;
	}

	return ret;
}

static int
ftrace_pid_open(struct inode *inode, struct file *file)
{
	return pid_open(inode, file, TRACE_PIDS);
}

static int
ftrace_no_pid_open(struct inode *inode, struct file *file)
{
	return pid_open(inode, file, TRACE_NO_PIDS);
}

static void ignore_task_cpu(void *data)
{
	struct trace_array *tr = data;
	struct trace_pid_list *pid_list;
	struct trace_pid_list *no_pid_list;

	/*
	 * This function is called by on_each_cpu() while the
	 * event_mutex is held.
	 */
	pid_list = rcu_dereference_protected(tr->function_pids,
					     mutex_is_locked(&ftrace_lock));
	no_pid_list = rcu_dereference_protected(tr->function_no_pids,
						mutex_is_locked(&ftrace_lock));

	if (trace_ignore_this_task(pid_list, no_pid_list, current))
		this_cpu_write(tr->array_buffer.data->ftrace_ignore_pid,
			       FTRACE_PID_IGNORE);
	else
		this_cpu_write(tr->array_buffer.data->ftrace_ignore_pid,
			       current->pid);
}

static ssize_t
pid_write(struct file *filp, const char __user *ubuf,
	  size_t cnt, loff_t *ppos, int type)
{
	struct seq_file *m = filp->private_data;
	struct trace_array *tr = m->private;
	struct trace_pid_list *filtered_pids;
	struct trace_pid_list *other_pids;
	struct trace_pid_list *pid_list;
	ssize_t ret;

	if (!cnt)
		return 0;

	mutex_lock(&ftrace_lock);

	switch (type) {
	case TRACE_PIDS:
		filtered_pids = rcu_dereference_protected(tr->function_pids,
					     lockdep_is_held(&ftrace_lock));
		other_pids = rcu_dereference_protected(tr->function_no_pids,
					     lockdep_is_held(&ftrace_lock));
		break;
	case TRACE_NO_PIDS:
		filtered_pids = rcu_dereference_protected(tr->function_no_pids,
					     lockdep_is_held(&ftrace_lock));
		other_pids = rcu_dereference_protected(tr->function_pids,
					     lockdep_is_held(&ftrace_lock));
		break;
	default:
		ret = -EINVAL;
		WARN_ON_ONCE(1);
		goto out;
	}

	ret = trace_pid_write(filtered_pids, &pid_list, ubuf, cnt);
	if (ret < 0)
		goto out;

	switch (type) {
	case TRACE_PIDS:
		rcu_assign_pointer(tr->function_pids, pid_list);
		break;
	case TRACE_NO_PIDS:
		rcu_assign_pointer(tr->function_no_pids, pid_list);
		break;
	}


	if (filtered_pids) {
		synchronize_rcu();
		trace_pid_list_free(filtered_pids);
	} else if (pid_list && !other_pids) {
		/* Register a probe to set whether to ignore the tracing of a task */
		register_trace_sched_switch(ftrace_filter_pid_sched_switch_probe, tr);
	}

	/*
	 * Ignoring of pids is done at task switch. But we have to
	 * check for those tasks that are currently running.
	 * Always do this in case a pid was appended or removed.
	 */
	on_each_cpu(ignore_task_cpu, tr, 1);

	ftrace_update_pid_func();
	ftrace_startup_all(0);
 out:
	mutex_unlock(&ftrace_lock);

	if (ret > 0)
		*ppos += ret;

	return ret;
}

static ssize_t
ftrace_pid_write(struct file *filp, const char __user *ubuf,
		 size_t cnt, loff_t *ppos)
{
	return pid_write(filp, ubuf, cnt, ppos, TRACE_PIDS);
}

static ssize_t
ftrace_no_pid_write(struct file *filp, const char __user *ubuf,
		    size_t cnt, loff_t *ppos)
{
	return pid_write(filp, ubuf, cnt, ppos, TRACE_NO_PIDS);
}

static int
ftrace_pid_release(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;

	trace_array_put(tr);

	return seq_release(inode, file);
}

static const struct file_operations ftrace_pid_fops = {
	.open		= ftrace_pid_open,
	.write		= ftrace_pid_write,
	.read		= seq_read,
	.llseek		= tracing_lseek,
	.release	= ftrace_pid_release,
};

static const struct file_operations ftrace_no_pid_fops = {
	.open		= ftrace_no_pid_open,
	.write		= ftrace_no_pid_write,
	.read		= seq_read,
	.llseek		= tracing_lseek,
	.release	= ftrace_pid_release,
};

void ftrace_init_tracefs(struct trace_array *tr, struct dentry *d_tracer)
{
	trace_create_file("set_ftrace_pid", TRACE_MODE_WRITE, d_tracer,
			    tr, &ftrace_pid_fops);
	trace_create_file("set_ftrace_notrace_pid", TRACE_MODE_WRITE,
			  d_tracer, tr, &ftrace_no_pid_fops);
}

void __init ftrace_init_tracefs_toplevel(struct trace_array *tr,
					 struct dentry *d_tracer)
{
	/* Only the top level directory has the dyn_tracefs and profile */
	WARN_ON(!(tr->flags & TRACE_ARRAY_FL_GLOBAL));

	ftrace_init_dyn_tracefs(d_tracer);
	ftrace_profile_tracefs(d_tracer);
}

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
	ftrace_trace_function = ftrace_stub;
}

/**
 * ftrace_is_dead - Test if ftrace is dead or not.
 *
 * Returns 1 if ftrace is "dead", zero otherwise.
 */
int ftrace_is_dead(void)
{
	return ftrace_disabled;
}

/**
 * register_ftrace_function - register a function for profiling
 * @ops:	ops structure that holds the function for profiling.
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

	ftrace_ops_init(ops);

	mutex_lock(&ftrace_lock);

	ret = ftrace_startup(ops, 0);

	mutex_unlock(&ftrace_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(register_ftrace_function);

/**
 * unregister_ftrace_function - unregister a function for profiling.
 * @ops:	ops structure that holds the function to unregister
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

static int symbols_cmp(const void *a, const void *b)
{
	const char **str_a = (const char **) a;
	const char **str_b = (const char **) b;

	return strcmp(*str_a, *str_b);
}

struct kallsyms_data {
	unsigned long *addrs;
	const char **syms;
	size_t cnt;
	size_t found;
};

static int kallsyms_callback(void *data, const char *name,
			     struct module *mod, unsigned long addr)
{
	struct kallsyms_data *args = data;
	const char **sym;
	int idx;

	sym = bsearch(&name, args->syms, args->cnt, sizeof(*args->syms), symbols_cmp);
	if (!sym)
		return 0;

	idx = sym - args->syms;
	if (args->addrs[idx])
		return 0;

	addr = ftrace_location(addr);
	if (!addr)
		return 0;

	args->addrs[idx] = addr;
	args->found++;
	return args->found == args->cnt ? 1 : 0;
}

/**
 * ftrace_lookup_symbols - Lookup addresses for array of symbols
 *
 * @sorted_syms: array of symbols pointers symbols to resolve,
 * must be alphabetically sorted
 * @cnt: number of symbols/addresses in @syms/@addrs arrays
 * @addrs: array for storing resulting addresses
 *
 * This function looks up addresses for array of symbols provided in
 * @syms array (must be alphabetically sorted) and stores them in
 * @addrs array, which needs to be big enough to store at least @cnt
 * addresses.
 *
 * This function returns 0 if all provided symbols are found,
 * -ESRCH otherwise.
 */
int ftrace_lookup_symbols(const char **sorted_syms, size_t cnt, unsigned long *addrs)
{
	struct kallsyms_data args;
	int err;

	memset(addrs, 0, sizeof(*addrs) * cnt);
	args.addrs = addrs;
	args.syms = sorted_syms;
	args.cnt = cnt;
	args.found = 0;
	err = kallsyms_on_each_symbol(kallsyms_callback, &args);
	if (err < 0)
		return err;
	return args.found == args.cnt ? 0 : -ESRCH;
}

#ifdef CONFIG_SYSCTL

#ifdef CONFIG_DYNAMIC_FTRACE
static void ftrace_startup_sysctl(void)
{
	int command;

	if (unlikely(ftrace_disabled))
		return;

	/* Force update next time */
	saved_ftrace_func = NULL;
	/* ftrace_start_up is true if we want ftrace running */
	if (ftrace_start_up) {
		command = FTRACE_UPDATE_CALLS;
		if (ftrace_graph_active)
			command |= FTRACE_START_FUNC_RET;
		ftrace_startup_enable(command);
	}
}

static void ftrace_shutdown_sysctl(void)
{
	int command;

	if (unlikely(ftrace_disabled))
		return;

	/* ftrace_start_up is true if ftrace is running */
	if (ftrace_start_up) {
		command = FTRACE_DISABLE_CALLS;
		if (ftrace_graph_active)
			command |= FTRACE_STOP_FUNC_RET;
		ftrace_run_update_code(command);
	}
}
#else
# define ftrace_startup_sysctl()       do { } while (0)
# define ftrace_shutdown_sysctl()      do { } while (0)
#endif /* CONFIG_DYNAMIC_FTRACE */

static bool is_permanent_ops_registered(void)
{
	struct ftrace_ops *op;

	do_for_each_ftrace_op(op, ftrace_ops_list) {
		if (op->flags & FTRACE_OPS_FL_PERMANENT)
			return true;
	} while_for_each_ftrace_op(op);

	return false;
}

static int
ftrace_enable_sysctl(struct ctl_table *table, int write,
		     void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret = -ENODEV;

	mutex_lock(&ftrace_lock);

	if (unlikely(ftrace_disabled))
		goto out;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	if (ret || !write || (last_ftrace_enabled == !!ftrace_enabled))
		goto out;

	if (ftrace_enabled) {

		/* we are starting ftrace again */
		if (rcu_dereference_protected(ftrace_ops_list,
			lockdep_is_held(&ftrace_lock)) != &ftrace_list_end)
			update_ftrace_function();

		ftrace_startup_sysctl();

	} else {
		if (is_permanent_ops_registered()) {
			ftrace_enabled = true;
			ret = -EBUSY;
			goto out;
		}

		/* stopping ftrace calls (just send to ftrace_stub) */
		ftrace_trace_function = ftrace_stub;

		ftrace_shutdown_sysctl();
	}

	last_ftrace_enabled = !!ftrace_enabled;
 out:
	mutex_unlock(&ftrace_lock);
	return ret;
}

static struct ctl_table ftrace_sysctls[] = {
	{
		.procname       = "ftrace_enabled",
		.data           = &ftrace_enabled,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = ftrace_enable_sysctl,
	},
	{}
};

static int __init ftrace_sysctl_init(void)
{
	register_sysctl_init("kernel", ftrace_sysctls);
	return 0;
}
late_initcall(ftrace_sysctl_init);
#endif
