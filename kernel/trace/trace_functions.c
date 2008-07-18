/*
 * ring buffer based function tracer
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
 *
 * Based on code from the latency_tracer, that is:
 *
 *  Copyright (C) 2004-2006 Ingo Molnar
 *  Copyright (C) 2004 William Lee Irwin III
 */
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/fs.h>

#include "trace.h"

static void function_reset(struct trace_array *tr)
{
	int cpu;

	tr->time_start = ftrace_now(tr->cpu);

	for_each_online_cpu(cpu)
		tracing_reset(tr->data[cpu]);
}

static void start_function_trace(struct trace_array *tr)
{
	tr->cpu = get_cpu();
	function_reset(tr);
	put_cpu();

	tracing_start_cmdline_record();
	tracing_start_function_trace();
}

static void stop_function_trace(struct trace_array *tr)
{
	tracing_stop_function_trace();
	tracing_stop_cmdline_record();
}

static void function_trace_init(struct trace_array *tr)
{
	if (tr->ctrl)
		start_function_trace(tr);
}

static void function_trace_reset(struct trace_array *tr)
{
	if (tr->ctrl)
		stop_function_trace(tr);
}

static void function_trace_ctrl_update(struct trace_array *tr)
{
	if (tr->ctrl)
		start_function_trace(tr);
	else
		stop_function_trace(tr);
}

static struct tracer function_trace __read_mostly =
{
	.name	     = "ftrace",
	.init	     = function_trace_init,
	.reset	     = function_trace_reset,
	.ctrl_update = function_trace_ctrl_update,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_function,
#endif
};

static __init int init_function_trace(void)
{
	return register_tracer(&function_trace);
}

device_initcall(init_function_trace);
