/*
 * nop tracer
 *
 * Copyright (C) 2008 Steven Noonan <steven@uplinklabs.net>
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/ftrace.h>

#include "trace.h"

static struct trace_array	*ctx_trace;

static void start_nop_trace(struct trace_array *tr)
{
	/* Nothing to do! */
}

static void stop_nop_trace(struct trace_array *tr)
{
	/* Nothing to do! */
}

static void nop_trace_init(struct trace_array *tr)
{
	int cpu;
	ctx_trace = tr;

	for_each_online_cpu(cpu)
		tracing_reset(tr, cpu);

	if (tr->ctrl)
		start_nop_trace(tr);
}

static void nop_trace_reset(struct trace_array *tr)
{
	if (tr->ctrl)
		stop_nop_trace(tr);
}

static void nop_trace_ctrl_update(struct trace_array *tr)
{
	/* When starting a new trace, reset the buffers */
	if (tr->ctrl)
		start_nop_trace(tr);
	else
		stop_nop_trace(tr);
}

struct tracer nop_trace __read_mostly =
{
	.name		= "nop",
	.init		= nop_trace_init,
	.reset		= nop_trace_reset,
	.ctrl_update	= nop_trace_ctrl_update,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest	= trace_selftest_startup_nop,
#endif
};

