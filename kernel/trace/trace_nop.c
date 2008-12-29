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

/* Our two options */
enum {
	TRACE_NOP_OPT_ACCEPT = 0x1,
	TRACE_NOP_OPT_REFUSE = 0x2
};

/* Options for the tracer (see trace_options file) */
static struct tracer_opt nop_opts[] = {
	/* Option that will be accepted by set_flag callback */
	{ TRACER_OPT(test_nop_accept, TRACE_NOP_OPT_ACCEPT) },
	/* Option that will be refused by set_flag callback */
	{ TRACER_OPT(test_nop_refuse, TRACE_NOP_OPT_REFUSE) },
	{ } /* Always set a last empty entry */
};

static struct tracer_flags nop_flags = {
	/* You can check your flags value here when you want. */
	.val = 0, /* By default: all flags disabled */
	.opts = nop_opts
};

static struct trace_array	*ctx_trace;

static void start_nop_trace(struct trace_array *tr)
{
	/* Nothing to do! */
}

static void stop_nop_trace(struct trace_array *tr)
{
	/* Nothing to do! */
}

static int nop_trace_init(struct trace_array *tr)
{
	int cpu;
	ctx_trace = tr;

	for_each_online_cpu(cpu)
		tracing_reset(tr, cpu);

	start_nop_trace(tr);
	return 0;
}

static void nop_trace_reset(struct trace_array *tr)
{
	stop_nop_trace(tr);
}

/* It only serves as a signal handler and a callback to
 * accept or refuse tthe setting of a flag.
 * If you don't implement it, then the flag setting will be
 * automatically accepted.
 */
static int nop_set_flag(u32 old_flags, u32 bit, int set)
{
	/*
	 * Note that you don't need to update nop_flags.val yourself.
	 * The tracing Api will do it automatically if you return 0
	 */
	if (bit == TRACE_NOP_OPT_ACCEPT) {
		printk(KERN_DEBUG "nop_test_accept flag set to %d: we accept."
			" Now cat trace_options to see the result\n",
			set);
		return 0;
	}

	if (bit == TRACE_NOP_OPT_REFUSE) {
		printk(KERN_DEBUG "nop_test_refuse flag set to %d: we refuse."
			"Now cat trace_options to see the result\n",
			set);
		return -EINVAL;
	}

	return 0;
}


struct tracer nop_trace __read_mostly =
{
	.name		= "nop",
	.init		= nop_trace_init,
	.reset		= nop_trace_reset,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest	= trace_selftest_startup_nop,
#endif
	.flags		= &nop_flags,
	.set_flag	= nop_set_flag
};

