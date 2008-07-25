/*
 * trace context switch
 *
 * Copyright (C) 2007 Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/marker.h>
#include <linux/ftrace.h>

#include "trace.h"

static struct trace_array	*ctx_trace;
static int __read_mostly	tracer_enabled;
static atomic_t			sched_ref;

static void
sched_switch_func(void *private, void *__rq, struct task_struct *prev,
			struct task_struct *next)
{
	struct trace_array **ptr = private;
	struct trace_array *tr = *ptr;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;

	tracing_record_cmdline(prev);
	tracing_record_cmdline(next);

	if (!tracer_enabled)
		return;

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		tracing_sched_switch_trace(tr, data, prev, next, flags);

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

static notrace void
sched_switch_callback(void *probe_data, void *call_data,
		      const char *format, va_list *args)
{
	struct task_struct *prev;
	struct task_struct *next;
	struct rq *__rq;

	if (!atomic_read(&sched_ref))
		return;

	/* skip prev_pid %d next_pid %d prev_state %ld */
	(void)va_arg(*args, int);
	(void)va_arg(*args, int);
	(void)va_arg(*args, long);
	__rq = va_arg(*args, typeof(__rq));
	prev = va_arg(*args, typeof(prev));
	next = va_arg(*args, typeof(next));

	/*
	 * If tracer_switch_func only points to the local
	 * switch func, it still needs the ptr passed to it.
	 */
	sched_switch_func(probe_data, __rq, prev, next);
}

static void
wakeup_func(void *private, void *__rq, struct task_struct *wakee, struct
			task_struct *curr)
{
	struct trace_array **ptr = private;
	struct trace_array *tr = *ptr;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;

	if (!tracer_enabled)
		return;

	tracing_record_cmdline(curr);

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		tracing_sched_wakeup_trace(tr, data, wakee, curr, flags);

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

static notrace void
wake_up_callback(void *probe_data, void *call_data,
		 const char *format, va_list *args)
{
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

	wakeup_func(probe_data, __rq, task, curr);
}

static void sched_switch_reset(struct trace_array *tr)
{
	int cpu;

	tr->time_start = ftrace_now(tr->cpu);

	for_each_online_cpu(cpu)
		tracing_reset(tr->data[cpu]);
}

static int tracing_sched_register(void)
{
	int ret;

	ret = marker_probe_register("kernel_sched_wakeup",
			"pid %d state %ld ## rq %p task %p rq->curr %p",
			wake_up_callback,
			&ctx_trace);
	if (ret) {
		pr_info("wakeup trace: Couldn't add marker"
			" probe to kernel_sched_wakeup\n");
		return ret;
	}

	ret = marker_probe_register("kernel_sched_wakeup_new",
			"pid %d state %ld ## rq %p task %p rq->curr %p",
			wake_up_callback,
			&ctx_trace);
	if (ret) {
		pr_info("wakeup trace: Couldn't add marker"
			" probe to kernel_sched_wakeup_new\n");
		goto fail_deprobe;
	}

	ret = marker_probe_register("kernel_sched_schedule",
		"prev_pid %d next_pid %d prev_state %ld "
		"## rq %p prev %p next %p",
		sched_switch_callback,
		&ctx_trace);
	if (ret) {
		pr_info("sched trace: Couldn't add marker"
			" probe to kernel_sched_schedule\n");
		goto fail_deprobe_wake_new;
	}

	return ret;
fail_deprobe_wake_new:
	marker_probe_unregister("kernel_sched_wakeup_new",
				wake_up_callback,
				&ctx_trace);
fail_deprobe:
	marker_probe_unregister("kernel_sched_wakeup",
				wake_up_callback,
				&ctx_trace);
	return ret;
}

static void tracing_sched_unregister(void)
{
	marker_probe_unregister("kernel_sched_schedule",
				sched_switch_callback,
				&ctx_trace);
	marker_probe_unregister("kernel_sched_wakeup_new",
				wake_up_callback,
				&ctx_trace);
	marker_probe_unregister("kernel_sched_wakeup",
				wake_up_callback,
				&ctx_trace);
}

static void tracing_start_sched_switch(void)
{
	long ref;

	ref = atomic_inc_return(&sched_ref);
	if (ref == 1)
		tracing_sched_register();
}

static void tracing_stop_sched_switch(void)
{
	long ref;

	ref = atomic_dec_and_test(&sched_ref);
	if (ref)
		tracing_sched_unregister();
}

void tracing_start_cmdline_record(void)
{
	tracing_start_sched_switch();
}

void tracing_stop_cmdline_record(void)
{
	tracing_stop_sched_switch();
}

static void start_sched_trace(struct trace_array *tr)
{
	sched_switch_reset(tr);
	tracing_start_cmdline_record();
	tracer_enabled = 1;
}

static void stop_sched_trace(struct trace_array *tr)
{
	tracer_enabled = 0;
	tracing_stop_cmdline_record();
}

static void sched_switch_trace_init(struct trace_array *tr)
{
	ctx_trace = tr;

	if (tr->ctrl)
		start_sched_trace(tr);
}

static void sched_switch_trace_reset(struct trace_array *tr)
{
	if (tr->ctrl)
		stop_sched_trace(tr);
}

static void sched_switch_trace_ctrl_update(struct trace_array *tr)
{
	/* When starting a new trace, reset the buffers */
	if (tr->ctrl)
		start_sched_trace(tr);
	else
		stop_sched_trace(tr);
}

static struct tracer sched_switch_trace __read_mostly =
{
	.name		= "sched_switch",
	.init		= sched_switch_trace_init,
	.reset		= sched_switch_trace_reset,
	.ctrl_update	= sched_switch_trace_ctrl_update,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_sched_switch,
#endif
};

__init static int init_sched_switch_trace(void)
{
	int ret = 0;

	if (atomic_read(&sched_ref))
		ret = tracing_sched_register();
	if (ret) {
		pr_info("error registering scheduler trace\n");
		return ret;
	}
	return register_tracer(&sched_switch_trace);
}
device_initcall(init_sched_switch_trace);
