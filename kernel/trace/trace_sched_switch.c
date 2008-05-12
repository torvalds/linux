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

static void notrace
ctx_switch_func(struct task_struct *prev, struct task_struct *next)
{
	struct trace_array *tr = ctx_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;

	if (!tracer_enabled)
		return;

	raw_local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		tracing_sched_switch_trace(tr, data, prev, next, flags);

	atomic_dec(&data->disabled);
	raw_local_irq_restore(flags);
}

void ftrace_ctx_switch(struct task_struct *prev, struct task_struct *next)
{
	tracing_record_cmdline(prev);

	/*
	 * If tracer_switch_func only points to the local
	 * switch func, it still needs the ptr passed to it.
	 */
	ctx_switch_func(prev, next);

	/*
	 * Chain to the wakeup tracer (this is a NOP if disabled):
	 */
	wakeup_sched_switch(prev, next);
}

static notrace void sched_switch_reset(struct trace_array *tr)
{
	int cpu;

	tr->time_start = now(tr->cpu);

	for_each_online_cpu(cpu)
		tracing_reset(tr->data[cpu]);
}

static notrace void start_sched_trace(struct trace_array *tr)
{
	sched_switch_reset(tr);
	tracer_enabled = 1;
}

static notrace void stop_sched_trace(struct trace_array *tr)
{
	tracer_enabled = 0;
}

static notrace void sched_switch_trace_init(struct trace_array *tr)
{
	ctx_trace = tr;

	if (tr->ctrl)
		start_sched_trace(tr);
}

static notrace void sched_switch_trace_reset(struct trace_array *tr)
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
};

__init static int init_sched_switch_trace(void)
{
	return register_tracer(&sched_switch_trace);
}
device_initcall(init_sched_switch_trace);
