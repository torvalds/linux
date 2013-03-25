/*
 * trace task wakeup timings
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
 *
 * Based on code from the latency_tracer, that is:
 *
 *  Copyright (C) 2004-2006 Ingo Molnar
 *  Copyright (C) 2004 Nadia Yvette Chambers
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/sched/rt.h>
#include <trace/events/sched.h>
#include "trace.h"

static struct trace_array	*wakeup_trace;
static int __read_mostly	tracer_enabled;

static struct task_struct	*wakeup_task;
static int			wakeup_cpu;
static int			wakeup_current_cpu;
static unsigned			wakeup_prio = -1;
static int			wakeup_rt;

static arch_spinlock_t wakeup_lock =
	(arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;

static void wakeup_reset(struct trace_array *tr);
static void __wakeup_reset(struct trace_array *tr);
static int wakeup_graph_entry(struct ftrace_graph_ent *trace);
static void wakeup_graph_return(struct ftrace_graph_ret *trace);

static int save_flags;

#define TRACE_DISPLAY_GRAPH     1

static struct tracer_opt trace_opts[] = {
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* display latency trace as call graph */
	{ TRACER_OPT(display-graph, TRACE_DISPLAY_GRAPH) },
#endif
	{ } /* Empty entry */
};

static struct tracer_flags tracer_flags = {
	.val  = 0,
	.opts = trace_opts,
};

#define is_graph() (tracer_flags.val & TRACE_DISPLAY_GRAPH)

#ifdef CONFIG_FUNCTION_TRACER

/*
 * Prologue for the wakeup function tracers.
 *
 * Returns 1 if it is OK to continue, and preemption
 *            is disabled and data->disabled is incremented.
 *         0 if the trace is to be ignored, and preemption
 *            is not disabled and data->disabled is
 *            kept the same.
 *
 * Note, this function is also used outside this ifdef but
 *  inside the #ifdef of the function graph tracer below.
 *  This is OK, since the function graph tracer is
 *  dependent on the function tracer.
 */
static int
func_prolog_preempt_disable(struct trace_array *tr,
			    struct trace_array_cpu **data,
			    int *pc)
{
	long disabled;
	int cpu;

	if (likely(!wakeup_task))
		return 0;

	*pc = preempt_count();
	preempt_disable_notrace();

	cpu = raw_smp_processor_id();
	if (cpu != wakeup_current_cpu)
		goto out_enable;

	*data = tr->data[cpu];
	disabled = atomic_inc_return(&(*data)->disabled);
	if (unlikely(disabled != 1))
		goto out;

	return 1;

out:
	atomic_dec(&(*data)->disabled);

out_enable:
	preempt_enable_notrace();
	return 0;
}

/*
 * wakeup uses its own tracer function to keep the overhead down:
 */
static void
wakeup_tracer_call(unsigned long ip, unsigned long parent_ip,
		   struct ftrace_ops *op, struct pt_regs *pt_regs)
{
	struct trace_array *tr = wakeup_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	int pc;

	if (!func_prolog_preempt_disable(tr, &data, &pc))
		return;

	local_irq_save(flags);
	trace_function(tr, ip, parent_ip, flags, pc);
	local_irq_restore(flags);

	atomic_dec(&data->disabled);
	preempt_enable_notrace();
}

static struct ftrace_ops trace_ops __read_mostly =
{
	.func = wakeup_tracer_call,
	.flags = FTRACE_OPS_FL_GLOBAL | FTRACE_OPS_FL_RECURSION_SAFE,
};
#endif /* CONFIG_FUNCTION_TRACER */

static int start_func_tracer(int graph)
{
	int ret;

	if (!graph)
		ret = register_ftrace_function(&trace_ops);
	else
		ret = register_ftrace_graph(&wakeup_graph_return,
					    &wakeup_graph_entry);

	if (!ret && tracing_is_enabled())
		tracer_enabled = 1;
	else
		tracer_enabled = 0;

	return ret;
}

static void stop_func_tracer(int graph)
{
	tracer_enabled = 0;

	if (!graph)
		unregister_ftrace_function(&trace_ops);
	else
		unregister_ftrace_graph();
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static int wakeup_set_flag(u32 old_flags, u32 bit, int set)
{

	if (!(bit & TRACE_DISPLAY_GRAPH))
		return -EINVAL;

	if (!(is_graph() ^ set))
		return 0;

	stop_func_tracer(!set);

	wakeup_reset(wakeup_trace);
	tracing_max_latency = 0;

	return start_func_tracer(set);
}

static int wakeup_graph_entry(struct ftrace_graph_ent *trace)
{
	struct trace_array *tr = wakeup_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	int pc, ret = 0;

	if (!func_prolog_preempt_disable(tr, &data, &pc))
		return 0;

	local_save_flags(flags);
	ret = __trace_graph_entry(tr, trace, flags, pc);
	atomic_dec(&data->disabled);
	preempt_enable_notrace();

	return ret;
}

static void wakeup_graph_return(struct ftrace_graph_ret *trace)
{
	struct trace_array *tr = wakeup_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	int pc;

	if (!func_prolog_preempt_disable(tr, &data, &pc))
		return;

	local_save_flags(flags);
	__trace_graph_return(tr, trace, flags, pc);
	atomic_dec(&data->disabled);

	preempt_enable_notrace();
	return;
}

static void wakeup_trace_open(struct trace_iterator *iter)
{
	if (is_graph())
		graph_trace_open(iter);
}

static void wakeup_trace_close(struct trace_iterator *iter)
{
	if (iter->private)
		graph_trace_close(iter);
}

#define GRAPH_TRACER_FLAGS (TRACE_GRAPH_PRINT_PROC | \
			    TRACE_GRAPH_PRINT_ABS_TIME | \
			    TRACE_GRAPH_PRINT_DURATION)

static enum print_line_t wakeup_print_line(struct trace_iterator *iter)
{
	/*
	 * In graph mode call the graph tracer output function,
	 * otherwise go with the TRACE_FN event handler
	 */
	if (is_graph())
		return print_graph_function_flags(iter, GRAPH_TRACER_FLAGS);

	return TRACE_TYPE_UNHANDLED;
}

static void wakeup_print_header(struct seq_file *s)
{
	if (is_graph())
		print_graph_headers_flags(s, GRAPH_TRACER_FLAGS);
	else
		trace_default_header(s);
}

static void
__trace_function(struct trace_array *tr,
		 unsigned long ip, unsigned long parent_ip,
		 unsigned long flags, int pc)
{
	if (is_graph())
		trace_graph_function(tr, ip, parent_ip, flags, pc);
	else
		trace_function(tr, ip, parent_ip, flags, pc);
}
#else
#define __trace_function trace_function

static int wakeup_set_flag(u32 old_flags, u32 bit, int set)
{
	return -EINVAL;
}

static int wakeup_graph_entry(struct ftrace_graph_ent *trace)
{
	return -1;
}

static enum print_line_t wakeup_print_line(struct trace_iterator *iter)
{
	return TRACE_TYPE_UNHANDLED;
}

static void wakeup_graph_return(struct ftrace_graph_ret *trace) { }
static void wakeup_trace_open(struct trace_iterator *iter) { }
static void wakeup_trace_close(struct trace_iterator *iter) { }

#ifdef CONFIG_FUNCTION_TRACER
static void wakeup_print_header(struct seq_file *s)
{
	trace_default_header(s);
}
#else
static void wakeup_print_header(struct seq_file *s)
{
	trace_latency_header(s);
}
#endif /* CONFIG_FUNCTION_TRACER */
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

/*
 * Should this new latency be reported/recorded?
 */
static int report_latency(cycle_t delta)
{
	if (tracing_thresh) {
		if (delta < tracing_thresh)
			return 0;
	} else {
		if (delta <= tracing_max_latency)
			return 0;
	}
	return 1;
}

static void
probe_wakeup_migrate_task(void *ignore, struct task_struct *task, int cpu)
{
	if (task != wakeup_task)
		return;

	wakeup_current_cpu = cpu;
}

static void notrace
probe_wakeup_sched_switch(void *ignore,
			  struct task_struct *prev, struct task_struct *next)
{
	struct trace_array_cpu *data;
	cycle_t T0, T1, delta;
	unsigned long flags;
	long disabled;
	int cpu;
	int pc;

	tracing_record_cmdline(prev);

	if (unlikely(!tracer_enabled))
		return;

	/*
	 * When we start a new trace, we set wakeup_task to NULL
	 * and then set tracer_enabled = 1. We want to make sure
	 * that another CPU does not see the tracer_enabled = 1
	 * and the wakeup_task with an older task, that might
	 * actually be the same as next.
	 */
	smp_rmb();

	if (next != wakeup_task)
		return;

	pc = preempt_count();

	/* disable local data, not wakeup_cpu data */
	cpu = raw_smp_processor_id();
	disabled = atomic_inc_return(&wakeup_trace->data[cpu]->disabled);
	if (likely(disabled != 1))
		goto out;

	local_irq_save(flags);
	arch_spin_lock(&wakeup_lock);

	/* We could race with grabbing wakeup_lock */
	if (unlikely(!tracer_enabled || next != wakeup_task))
		goto out_unlock;

	/* The task we are waiting for is waking up */
	data = wakeup_trace->data[wakeup_cpu];

	__trace_function(wakeup_trace, CALLER_ADDR0, CALLER_ADDR1, flags, pc);
	tracing_sched_switch_trace(wakeup_trace, prev, next, flags, pc);

	T0 = data->preempt_timestamp;
	T1 = ftrace_now(cpu);
	delta = T1-T0;

	if (!report_latency(delta))
		goto out_unlock;

	if (likely(!is_tracing_stopped())) {
		tracing_max_latency = delta;
		update_max_tr(wakeup_trace, wakeup_task, wakeup_cpu);
	}

out_unlock:
	__wakeup_reset(wakeup_trace);
	arch_spin_unlock(&wakeup_lock);
	local_irq_restore(flags);
out:
	atomic_dec(&wakeup_trace->data[cpu]->disabled);
}

static void __wakeup_reset(struct trace_array *tr)
{
	wakeup_cpu = -1;
	wakeup_prio = -1;

	if (wakeup_task)
		put_task_struct(wakeup_task);

	wakeup_task = NULL;
}

static void wakeup_reset(struct trace_array *tr)
{
	unsigned long flags;

	tracing_reset_online_cpus(tr);

	local_irq_save(flags);
	arch_spin_lock(&wakeup_lock);
	__wakeup_reset(tr);
	arch_spin_unlock(&wakeup_lock);
	local_irq_restore(flags);
}

static void
probe_wakeup(void *ignore, struct task_struct *p, int success)
{
	struct trace_array_cpu *data;
	int cpu = smp_processor_id();
	unsigned long flags;
	long disabled;
	int pc;

	if (likely(!tracer_enabled))
		return;

	tracing_record_cmdline(p);
	tracing_record_cmdline(current);

	if ((wakeup_rt && !rt_task(p)) ||
			p->prio >= wakeup_prio ||
			p->prio >= current->prio)
		return;

	pc = preempt_count();
	disabled = atomic_inc_return(&wakeup_trace->data[cpu]->disabled);
	if (unlikely(disabled != 1))
		goto out;

	/* interrupts should be off from try_to_wake_up */
	arch_spin_lock(&wakeup_lock);

	/* check for races. */
	if (!tracer_enabled || p->prio >= wakeup_prio)
		goto out_locked;

	/* reset the trace */
	__wakeup_reset(wakeup_trace);

	wakeup_cpu = task_cpu(p);
	wakeup_current_cpu = wakeup_cpu;
	wakeup_prio = p->prio;

	wakeup_task = p;
	get_task_struct(wakeup_task);

	local_save_flags(flags);

	data = wakeup_trace->data[wakeup_cpu];
	data->preempt_timestamp = ftrace_now(cpu);
	tracing_sched_wakeup_trace(wakeup_trace, p, current, flags, pc);

	/*
	 * We must be careful in using CALLER_ADDR2. But since wake_up
	 * is not called by an assembly function  (where as schedule is)
	 * it should be safe to use it here.
	 */
	__trace_function(wakeup_trace, CALLER_ADDR1, CALLER_ADDR2, flags, pc);

out_locked:
	arch_spin_unlock(&wakeup_lock);
out:
	atomic_dec(&wakeup_trace->data[cpu]->disabled);
}

static void start_wakeup_tracer(struct trace_array *tr)
{
	int ret;

	ret = register_trace_sched_wakeup(probe_wakeup, NULL);
	if (ret) {
		pr_info("wakeup trace: Couldn't activate tracepoint"
			" probe to kernel_sched_wakeup\n");
		return;
	}

	ret = register_trace_sched_wakeup_new(probe_wakeup, NULL);
	if (ret) {
		pr_info("wakeup trace: Couldn't activate tracepoint"
			" probe to kernel_sched_wakeup_new\n");
		goto fail_deprobe;
	}

	ret = register_trace_sched_switch(probe_wakeup_sched_switch, NULL);
	if (ret) {
		pr_info("sched trace: Couldn't activate tracepoint"
			" probe to kernel_sched_switch\n");
		goto fail_deprobe_wake_new;
	}

	ret = register_trace_sched_migrate_task(probe_wakeup_migrate_task, NULL);
	if (ret) {
		pr_info("wakeup trace: Couldn't activate tracepoint"
			" probe to kernel_sched_migrate_task\n");
		return;
	}

	wakeup_reset(tr);

	/*
	 * Don't let the tracer_enabled = 1 show up before
	 * the wakeup_task is reset. This may be overkill since
	 * wakeup_reset does a spin_unlock after setting the
	 * wakeup_task to NULL, but I want to be safe.
	 * This is a slow path anyway.
	 */
	smp_wmb();

	if (start_func_tracer(is_graph()))
		printk(KERN_ERR "failed to start wakeup tracer\n");

	return;
fail_deprobe_wake_new:
	unregister_trace_sched_wakeup_new(probe_wakeup, NULL);
fail_deprobe:
	unregister_trace_sched_wakeup(probe_wakeup, NULL);
}

static void stop_wakeup_tracer(struct trace_array *tr)
{
	tracer_enabled = 0;
	stop_func_tracer(is_graph());
	unregister_trace_sched_switch(probe_wakeup_sched_switch, NULL);
	unregister_trace_sched_wakeup_new(probe_wakeup, NULL);
	unregister_trace_sched_wakeup(probe_wakeup, NULL);
	unregister_trace_sched_migrate_task(probe_wakeup_migrate_task, NULL);
}

static int __wakeup_tracer_init(struct trace_array *tr)
{
	save_flags = trace_flags;

	/* non overwrite screws up the latency tracers */
	set_tracer_flag(TRACE_ITER_OVERWRITE, 1);
	set_tracer_flag(TRACE_ITER_LATENCY_FMT, 1);

	tracing_max_latency = 0;
	wakeup_trace = tr;
	start_wakeup_tracer(tr);
	return 0;
}

static int wakeup_tracer_init(struct trace_array *tr)
{
	wakeup_rt = 0;
	return __wakeup_tracer_init(tr);
}

static int wakeup_rt_tracer_init(struct trace_array *tr)
{
	wakeup_rt = 1;
	return __wakeup_tracer_init(tr);
}

static void wakeup_tracer_reset(struct trace_array *tr)
{
	int lat_flag = save_flags & TRACE_ITER_LATENCY_FMT;
	int overwrite_flag = save_flags & TRACE_ITER_OVERWRITE;

	stop_wakeup_tracer(tr);
	/* make sure we put back any tasks we are tracing */
	wakeup_reset(tr);

	set_tracer_flag(TRACE_ITER_LATENCY_FMT, lat_flag);
	set_tracer_flag(TRACE_ITER_OVERWRITE, overwrite_flag);
}

static void wakeup_tracer_start(struct trace_array *tr)
{
	wakeup_reset(tr);
	tracer_enabled = 1;
}

static void wakeup_tracer_stop(struct trace_array *tr)
{
	tracer_enabled = 0;
}

static struct tracer wakeup_tracer __read_mostly =
{
	.name		= "wakeup",
	.init		= wakeup_tracer_init,
	.reset		= wakeup_tracer_reset,
	.start		= wakeup_tracer_start,
	.stop		= wakeup_tracer_stop,
	.print_max	= true,
	.print_header	= wakeup_print_header,
	.print_line	= wakeup_print_line,
	.flags		= &tracer_flags,
	.set_flag	= wakeup_set_flag,
	.flag_changed	= trace_keep_overwrite,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_wakeup,
#endif
	.open		= wakeup_trace_open,
	.close		= wakeup_trace_close,
	.use_max_tr	= true,
};

static struct tracer wakeup_rt_tracer __read_mostly =
{
	.name		= "wakeup_rt",
	.init		= wakeup_rt_tracer_init,
	.reset		= wakeup_tracer_reset,
	.start		= wakeup_tracer_start,
	.stop		= wakeup_tracer_stop,
	.wait_pipe	= poll_wait_pipe,
	.print_max	= true,
	.print_header	= wakeup_print_header,
	.print_line	= wakeup_print_line,
	.flags		= &tracer_flags,
	.set_flag	= wakeup_set_flag,
	.flag_changed	= trace_keep_overwrite,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_wakeup,
#endif
	.open		= wakeup_trace_open,
	.close		= wakeup_trace_close,
	.use_max_tr	= true,
};

__init static int init_wakeup_tracer(void)
{
	int ret;

	ret = register_tracer(&wakeup_tracer);
	if (ret)
		return ret;

	ret = register_tracer(&wakeup_rt_tracer);
	if (ret)
		return ret;

	return 0;
}
core_initcall(init_wakeup_tracer);
