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

static void start_function_trace(struct trace_array *tr)
{
	tr->cpu = get_cpu();
	tracing_reset_online_cpus(tr);
	put_cpu();

	tracing_start_cmdline_record();
	tracing_start_function_trace();
}

static void stop_function_trace(struct trace_array *tr)
{
	tracing_stop_function_trace();
	tracing_stop_cmdline_record();
}

static int function_trace_init(struct trace_array *tr)
{
	start_function_trace(tr);
	return 0;
}

static void function_trace_reset(struct trace_array *tr)
{
	stop_function_trace(tr);
}

static void function_trace_start(struct trace_array *tr)
{
	tracing_reset_online_cpus(tr);
}

static struct tracer function_trace __read_mostly =
{
	.name	     = "function",
	.init	     = function_trace_init,
	.reset	     = function_trace_reset,
	.start	     = function_trace_start,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_function,
#endif
};

static __init int init_function_trace(void)
{
	return register_tracer(&function_trace);
}

device_initcall(init_function_trace);
