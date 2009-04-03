/*
 * trace task wakeup timings
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
 *
 * Based on code from the latency_tracer, that is:
 *
 *  Copyright (C) 2004-2006 Ingo Molnar
 *  Copyright (C) 2004 William Lee Irwin III
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <trace/sched.h>

#include "trace.h"

static struct trace_array	*wakeup_trace;
static int __read_mostly	tracer_enabled;

static struct task_struct	*wakeup_task;
static int			wakeup_cpu;
static unsigned			wakeup_prio = -1;
static int			wakeup_rt;

static raw_spinlock_t wakeup_lock =
	(raw_spinlock_t)__RAW_SPIN_LOCK_UNLOCKED;

static void __wakeup_reset(struct trace_array *tr);

static int save_lat_flag;

#ifdef CONFIG_FUNCTION_TRACER
/*
 * irqsoff uses its own tracer function to keep the overhead down:
 */
static void
wakeup_tracer_call(unsigned long ip, unsigned long parent_ip)
{
	struct trace_array *tr = wakeup_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int resched;
	int cpu;
	int pc;

	if (likely(!wakeup_task))
		return;

	pc = preempt_count();
	resched = ftrace_preempt_disable();

	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);
	if (unlikely(disabled != 1))
		goto out;

	local_irq_save(flags);
	__raw_spin_lock(&wakeup_lock);

	if (unlikely(!wakeup_task))
		goto unlock;

	/*
	 * The task can't disappear because it needs to
	 * wake up first, and we have the wakeup_lock.
	 */
	if (task_cpu(wakeup_task) != cpu)
		goto unlock;

	trace_function(tr, ip, parent_ip, flags, pc);

 unlock:
	__raw_spin_unlock(&wakeup_lock);
	local_irq_restore(flags);

 out:
	atomic_dec(&data->disabled);

	ftrace_preempt_enable(resched);
}

static struct ftrace_ops trace_ops __read_mostly =
{
	.func = wakeup_tracer_call,
};
#endif /* CONFIG_FUNCTION_TRACER */

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

static void notrace
probe_wakeup_sched_switch(struct rq *rq, struct task_struct *prev,
	struct task_struct *next)
{
	unsigned long latency = 0, t0 = 0, t1 = 0;
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

	/* The task we are waiting for is waking up */
	data = wakeup_trace->data[wakeup_cpu];

	/* disable local data, not wakeup_cpu data */
	cpu = raw_smp_processor_id();
	disabled = atomic_inc_return(&wakeup_trace->data[cpu]->disabled);
	if (likely(disabled != 1))
		goto out;

	local_irq_save(flags);
	__raw_spin_lock(&wakeup_lock);

	/* We could race with grabbing wakeup_lock */
	if (unlikely(!tracer_enabled || next != wakeup_task))
		goto out_unlock;

	trace_function(wakeup_trace, CALLER_ADDR0, CALLER_ADDR1, flags, pc);
	tracing_sched_switch_trace(wakeup_trace, prev, next, flags, pc);

	/*
	 * usecs conversion is slow so we try to delay the conversion
	 * as long as possible:
	 */
	T0 = data->preempt_timestamp;
	T1 = ftrace_now(cpu);
	delta = T1-T0;

	if (!report_latency(delta))
		goto out_unlock;

	latency = nsecs_to_usecs(delta);

	tracing_max_latency = delta;
	t0 = nsecs_to_usecs(T0);
	t1 = nsecs_to_usecs(T1);

	update_max_tr(wakeup_trace, wakeup_task, wakeup_cpu);

out_unlock:
	__wakeup_reset(wakeup_trace);
	__raw_spin_unlock(&wakeup_lock);
	local_irq_restore(flags);
out:
	atomic_dec(&wakeup_trace->data[cpu]->disabled);
}

static void __wakeup_reset(struct trace_array *tr)
{
	int cpu;

	for_each_possible_cpu(cpu)
		tracing_reset(tr, cpu);

	wakeup_cpu = -1;
	wakeup_prio = -1;

	if (wakeup_task)
		put_task_struct(wakeup_task);

	wakeup_task = NULL;
}

static void wakeup_reset(struct trace_array *tr)
{
	unsigned long flags;

	local_irq_save(flags);
	__raw_spin_lock(&wakeup_lock);
	__wakeup_reset(tr);
	__raw_spin_unlock(&wakeup_lock);
	local_irq_restore(flags);
}

static void
probe_wakeup(struct rq *rq, struct task_struct *p, int success)
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
	__raw_spin_lock(&wakeup_lock);

	/* check for races. */
	if (!tracer_enabled || p->prio >= wakeup_prio)
		goto out_locked;

	/* reset the trace */
	__wakeup_reset(wakeup_trace);

	wakeup_cpu = task_cpu(p);
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
	trace_function(wakeup_trace, CALLER_ADDR1, CALLER_ADDR2, flags, pc);

out_locked:
	__raw_spin_unlock(&wakeup_lock);
out:
	atomic_dec(&wakeup_trace->data[cpu]->disabled);
}

static void start_wakeup_tracer(struct trace_array *tr)
{
	int ret;

	ret = register_trace_sched_wakeup(probe_wakeup);
	if (ret) {
		pr_info("wakeup trace: Couldn't activate tracepoint"
			" probe to kernel_sched_wakeup\n");
		return;
	}

	ret = register_trace_sched_wakeup_new(probe_wakeup);
	if (ret) {
		pr_info("wakeup trace: Couldn't activate tracepoint"
			" probe to kernel_sched_wakeup_new\n");
		goto fail_deprobe;
	}

	ret = register_trace_sched_switch(probe_wakeup_sched_switch);
	if (ret) {
		pr_info("sched trace: Couldn't activate tracepoint"
			" probe to kernel_sched_switch\n");
		goto fail_deprobe_wake_new;
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

	register_ftrace_function(&trace_ops);

	if (tracing_is_enabled())
		tracer_enabled = 1;
	else
		tracer_enabled = 0;

	return;
fail_deprobe_wake_new:
	unregister_trace_sched_wakeup_new(probe_wakeup);
fail_deprobe:
	unregister_trace_sched_wakeup(probe_wakeup);
}

static void stop_wakeup_tracer(struct trace_array *tr)
{
	tracer_enabled = 0;
	unregister_ftrace_function(&trace_ops);
	unregister_trace_sched_switch(probe_wakeup_sched_switch);
	unregister_trace_sched_wakeup_new(probe_wakeup);
	unregister_trace_sched_wakeup(probe_wakeup);
}

static int __wakeup_tracer_init(struct trace_array *tr)
{
	save_lat_flag = trace_flags & TRACE_ITER_LATENCY_FMT;
	trace_flags |= TRACE_ITER_LATENCY_FMT;

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
	stop_wakeup_tracer(tr);
	/* make sure we put back any tasks we are tracing */
	wakeup_reset(tr);

	if (!save_lat_flag)
		trace_flags &= ~TRACE_ITER_LATENCY_FMT;
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
	.print_max	= 1,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_wakeup,
#endif
};

static struct tracer wakeup_rt_tracer __read_mostly =
{
	.name		= "wakeup_rt",
	.init		= wakeup_rt_tracer_init,
	.reset		= wakeup_tracer_reset,
	.start		= wakeup_tracer_start,
	.stop		= wakeup_tracer_stop,
	.wait_pipe	= poll_wait_pipe,
	.print_max	= 1,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_wakeup,
#endif
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
device_initcall(init_wakeup_tracer);
