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

static void
ctx_switch_func(void *__rq, struct task_struct *prev, struct task_struct *next)
{
	struct trace_array *tr = ctx_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;

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

static void
wakeup_func(void *__rq, struct task_struct *wakee, struct task_struct *curr)
{
	struct trace_array *tr = ctx_trace;
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

void
ftrace_ctx_switch(void *__rq, struct task_struct *prev,
		  struct task_struct *next)
{
	if (unlikely(atomic_read(&trace_record_cmdline_enabled)))
		tracing_record_cmdline(prev);

	/*
	 * If tracer_switch_func only points to the local
	 * switch func, it still needs the ptr passed to it.
	 */
	ctx_switch_func(__rq, prev, next);

	/*
	 * Chain to the wakeup tracer (this is a NOP if disabled):
	 */
	wakeup_sched_switch(prev, next);
}

void
ftrace_wake_up_task(void *__rq, struct task_struct *wakee,
		    struct task_struct *curr)
{
	wakeup_func(__rq, wakee, curr);

	/*
	 * Chain to the wakeup tracer (this is a NOP if disabled):
	 */
	wakeup_sched_wakeup(wakee, curr);
}

void
ftrace_special(unsigned long arg1, unsigned long arg2, unsigned long arg3)
{
	struct trace_array *tr = ctx_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;

	if (!tracer_enabled)
		return;

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		__trace_special(tr, data, arg1, arg2, arg3);

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

static void sched_switch_reset(struct trace_array *tr)
{
	int cpu;

	tr->time_start = ftrace_now(tr->cpu);

	for_each_online_cpu(cpu)
		tracing_reset(tr->data[cpu]);
}

static void start_sched_trace(struct trace_array *tr)
{
	sched_switch_reset(tr);
	atomic_inc(&trace_record_cmdline_enabled);
	tracer_enabled = 1;
}

static void stop_sched_trace(struct trace_array *tr)
{
	atomic_dec(&trace_record_cmdline_enabled);
	tracer_enabled = 0;
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
	return register_tracer(&sched_switch_trace);
}
device_initcall(init_sched_switch_trace);
