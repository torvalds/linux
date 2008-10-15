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
#include <linux/marker.h>

#include "trace.h"

static struct trace_array	*wakeup_trace;
static int __read_mostly	tracer_enabled;

static struct task_struct	*wakeup_task;
static int			wakeup_cpu;
static unsigned			wakeup_prio = -1;

static raw_spinlock_t wakeup_lock =
	(raw_spinlock_t)__RAW_SPIN_LOCK_UNLOCKED;

static void __wakeup_reset(struct trace_array *tr);

#ifdef CONFIG_FTRACE
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

	if (likely(!wakeup_task))
		return;

	resched = need_resched();
	preempt_disable_notrace();

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

	trace_function(tr, data, ip, parent_ip, flags);

 unlock:
	__raw_spin_unlock(&wakeup_lock);
	local_irq_restore(flags);

 out:
	atomic_dec(&data->disabled);

	/*
	 * To prevent recursion from the scheduler, if the
	 * resched flag was set before we entered, then
	 * don't reschedule.
	 */
	if (resched)
		preempt_enable_no_resched_notrace();
	else
		preempt_enable_notrace();
}

static struct ftrace_ops trace_ops __read_mostly =
{
	.func = wakeup_tracer_call,
};
#endif /* CONFIG_FTRACE */

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
wakeup_sched_switch(void *private, void *rq, struct task_struct *prev,
	struct task_struct *next)
{
	unsigned long latency = 0, t0 = 0, t1 = 0;
	struct trace_array **ptr = private;
	struct trace_array *tr = *ptr;
	struct trace_array_cpu *data;
	cycle_t T0, T1, delta;
	unsigned long flags;
	long disabled;
	int cpu;

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

	/* The task we are waiting for is waking up */
	data = tr->data[wakeup_cpu];

	/* disable local data, not wakeup_cpu data */
	cpu = raw_smp_processor_id();
	disabled = atomic_inc_return(&tr->data[cpu]->disabled);
	if (likely(disabled != 1))
		goto out;

	local_irq_save(flags);
	__raw_spin_lock(&wakeup_lock);

	/* We could race with grabbing wakeup_lock */
	if (unlikely(!tracer_enabled || next != wakeup_task))
		goto out_unlock;

	trace_function(tr, data, CALLER_ADDR1, CALLER_ADDR2, flags);

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

	update_max_tr(tr, wakeup_task, wakeup_cpu);

out_unlock:
	__wakeup_reset(tr);
	__raw_spin_unlock(&wakeup_lock);
	local_irq_restore(flags);
out:
	atomic_dec(&tr->data[cpu]->disabled);
}

static notrace void
sched_switch_callback(void *probe_data, void *call_data,
		      const char *format, va_list *args)
{
	struct task_struct *prev;
	struct task_struct *next;
	struct rq *__rq;

	/* skip prev_pid %d next_pid %d prev_state %ld */
	(void)va_arg(*args, int);
	(void)va_arg(*args, int);
	(void)va_arg(*args, long);
	__rq = va_arg(*args, typeof(__rq));
	prev = va_arg(*args, typeof(prev));
	next = va_arg(*args, typeof(next));

	tracing_record_cmdline(prev);

	/*
	 * If tracer_switch_func only points to the local
	 * switch func, it still needs the ptr passed to it.
	 */
	wakeup_sched_switch(probe_data, __rq, prev, next);
}

static void __wakeup_reset(struct trace_array *tr)
{
	struct trace_array_cpu *data;
	int cpu;

	for_each_possible_cpu(cpu) {
		data = tr->data[cpu];
		tracing_reset(data);
	}

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
wakeup_check_start(struct trace_array *tr, struct task_struct *p,
		   struct task_struct *curr)
{
	int cpu = smp_processor_id();
	unsigned long flags;
	long disabled;

	if (likely(!rt_task(p)) ||
			p->prio >= wakeup_prio ||
			p->prio >= curr->prio)
		return;

	disabled = atomic_inc_return(&tr->data[cpu]->disabled);
	if (unlikely(disabled != 1))
		goto out;

	/* interrupts should be off from try_to_wake_up */
	__raw_spin_lock(&wakeup_lock);

	/* check for races. */
	if (!tracer_enabled || p->prio >= wakeup_prio)
		goto out_locked;

	/* reset the trace */
	__wakeup_reset(tr);

	wakeup_cpu = task_cpu(p);
	wakeup_prio = p->prio;

	wakeup_task = p;
	get_task_struct(wakeup_task);

	local_save_flags(flags);

	tr->data[wakeup_cpu]->preempt_timestamp = ftrace_now(cpu);
	trace_function(tr, tr->data[wakeup_cpu],
		       CALLER_ADDR1, CALLER_ADDR2, flags);

out_locked:
	__raw_spin_unlock(&wakeup_lock);
out:
	atomic_dec(&tr->data[cpu]->disabled);
}

static notrace void
wake_up_callback(void *probe_data, void *call_data,
		 const char *format, va_list *args)
{
	struct trace_array **ptr = probe_data;
	struct trace_array *tr = *ptr;
	struct task_struct *curr;
	struct task_struct *task;
	struct rq *__rq;

	if (likely(!tracer_enabled))
		return;

	/* Skip pid %d state %ld */
	(void)va_arg(*args, int);
	(void)va_arg(*args, long);
	/* now get the meat: "rq %p task %p rq->curr %p" */
	__rq = va_arg(*args, typeof(__rq));
	task = va_arg(*args, typeof(task));
	curr = va_arg(*args, typeof(curr));

	tracing_record_cmdline(task);
	tracing_record_cmdline(curr);

	wakeup_check_start(tr, task, curr);
}

static void start_wakeup_tracer(struct trace_array *tr)
{
	int ret;

	ret = marker_probe_register("kernel_sched_wakeup",
			"pid %d state %ld ## rq %p task %p rq->curr %p",
			wake_up_callback,
			&wakeup_trace);
	if (ret) {
		pr_info("wakeup trace: Couldn't add marker"
			" probe to kernel_sched_wakeup\n");
		return;
	}

	ret = marker_probe_register("kernel_sched_wakeup_new",
			"pid %d state %ld ## rq %p task %p rq->curr %p",
			wake_up_callback,
			&wakeup_trace);
	if (ret) {
		pr_info("wakeup trace: Couldn't add marker"
			" probe to kernel_sched_wakeup_new\n");
		goto fail_deprobe;
	}

	ret = marker_probe_register("kernel_sched_schedule",
		"prev_pid %d next_pid %d prev_state %ld "
		"## rq %p prev %p next %p",
		sched_switch_callback,
		&wakeup_trace);
	if (ret) {
		pr_info("sched trace: Couldn't add marker"
			" probe to kernel_sched_schedule\n");
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

	tracer_enabled = 1;

	return;
fail_deprobe_wake_new:
	marker_probe_unregister("kernel_sched_wakeup_new",
				wake_up_callback,
				&wakeup_trace);
fail_deprobe:
	marker_probe_unregister("kernel_sched_wakeup",
				wake_up_callback,
				&wakeup_trace);
}

static void stop_wakeup_tracer(struct trace_array *tr)
{
	tracer_enabled = 0;
	unregister_ftrace_function(&trace_ops);
	marker_probe_unregister("kernel_sched_schedule",
				sched_switch_callback,
				&wakeup_trace);
	marker_probe_unregister("kernel_sched_wakeup_new",
				wake_up_callback,
				&wakeup_trace);
	marker_probe_unregister("kernel_sched_wakeup",
				wake_up_callback,
				&wakeup_trace);
}

static void wakeup_tracer_init(struct trace_array *tr)
{
	wakeup_trace = tr;

	if (tr->ctrl)
		start_wakeup_tracer(tr);
}

static void wakeup_tracer_reset(struct trace_array *tr)
{
	if (tr->ctrl) {
		stop_wakeup_tracer(tr);
		/* make sure we put back any tasks we are tracing */
		wakeup_reset(tr);
	}
}

static void wakeup_tracer_ctrl_update(struct trace_array *tr)
{
	if (tr->ctrl)
		start_wakeup_tracer(tr);
	else
		stop_wakeup_tracer(tr);
}

static void wakeup_tracer_open(struct trace_iterator *iter)
{
	/* stop the trace while dumping */
	if (iter->tr->ctrl)
		stop_wakeup_tracer(iter->tr);
}

static void wakeup_tracer_close(struct trace_iterator *iter)
{
	/* forget about any processes we were recording */
	if (iter->tr->ctrl)
		start_wakeup_tracer(iter->tr);
}

static struct tracer wakeup_tracer __read_mostly =
{
	.name		= "wakeup",
	.init		= wakeup_tracer_init,
	.reset		= wakeup_tracer_reset,
	.open		= wakeup_tracer_open,
	.close		= wakeup_tracer_close,
	.ctrl_update	= wakeup_tracer_ctrl_update,
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

	return 0;
}
device_initcall(init_wakeup_tracer);
