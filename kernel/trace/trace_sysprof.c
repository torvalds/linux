/*
 * trace stack traces
 *
 * Copyright (C) 2007 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
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

static notrace void stack_reset(struct trace_array *tr)
{
	int cpu;

	tr->time_start = ftrace_now(tr->cpu);

	for_each_online_cpu(cpu)
		tracing_reset(tr->data[cpu]);
}

static notrace void start_stack_trace(struct trace_array *tr)
{
	stack_reset(tr);
	tracer_enabled = 1;
}

static notrace void stop_stack_trace(struct trace_array *tr)
{
	tracer_enabled = 0;
}

static notrace void stack_trace_init(struct trace_array *tr)
{
	ctx_trace = tr;

	if (tr->ctrl)
		start_stack_trace(tr);
}

static notrace void stack_trace_reset(struct trace_array *tr)
{
	if (tr->ctrl)
		stop_stack_trace(tr);
}

static void stack_trace_ctrl_update(struct trace_array *tr)
{
	/* When starting a new trace, reset the buffers */
	if (tr->ctrl)
		start_stack_trace(tr);
	else
		stop_stack_trace(tr);
}

static struct tracer stack_trace __read_mostly =
{
	.name		= "sysprof",
	.init		= stack_trace_init,
	.reset		= stack_trace_reset,
	.ctrl_update	= stack_trace_ctrl_update,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_stack,
#endif
};

__init static int init_stack_trace(void)
{
	return register_tracer(&stack_trace);
}
device_initcall(init_stack_trace);
