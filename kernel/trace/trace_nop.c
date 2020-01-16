// SPDX-License-Identifier: GPL-2.0
/*
 * yesp tracer
 *
 * Copyright (C) 2008 Steven Noonan <steven@uplinklabs.net>
 *
 */

#include <linux/module.h>
#include <linux/ftrace.h>

#include "trace.h"

/* Our two options */
enum {
	TRACE_NOP_OPT_ACCEPT = 0x1,
	TRACE_NOP_OPT_REFUSE = 0x2
};

/* Options for the tracer (see trace_options file) */
static struct tracer_opt yesp_opts[] = {
	/* Option that will be accepted by set_flag callback */
	{ TRACER_OPT(test_yesp_accept, TRACE_NOP_OPT_ACCEPT) },
	/* Option that will be refused by set_flag callback */
	{ TRACER_OPT(test_yesp_refuse, TRACE_NOP_OPT_REFUSE) },
	{ } /* Always set a last empty entry */
};

static struct tracer_flags yesp_flags = {
	/* You can check your flags value here when you want. */
	.val = 0, /* By default: all flags disabled */
	.opts = yesp_opts
};

static struct trace_array	*ctx_trace;

static void start_yesp_trace(struct trace_array *tr)
{
	/* Nothing to do! */
}

static void stop_yesp_trace(struct trace_array *tr)
{
	/* Nothing to do! */
}

static int yesp_trace_init(struct trace_array *tr)
{
	ctx_trace = tr;
	start_yesp_trace(tr);
	return 0;
}

static void yesp_trace_reset(struct trace_array *tr)
{
	stop_yesp_trace(tr);
}

/* It only serves as a signal handler and a callback to
 * accept or refuse the setting of a flag.
 * If you don't implement it, then the flag setting will be
 * automatically accepted.
 */
static int yesp_set_flag(struct trace_array *tr, u32 old_flags, u32 bit, int set)
{
	/*
	 * Note that you don't need to update yesp_flags.val yourself.
	 * The tracing Api will do it automatically if you return 0
	 */
	if (bit == TRACE_NOP_OPT_ACCEPT) {
		printk(KERN_DEBUG "yesp_test_accept flag set to %d: we accept."
			" Now cat trace_options to see the result\n",
			set);
		return 0;
	}

	if (bit == TRACE_NOP_OPT_REFUSE) {
		printk(KERN_DEBUG "yesp_test_refuse flag set to %d: we refuse."
			" Now cat trace_options to see the result\n",
			set);
		return -EINVAL;
	}

	return 0;
}


struct tracer yesp_trace __read_mostly =
{
	.name		= "yesp",
	.init		= yesp_trace_init,
	.reset		= yesp_trace_reset,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest	= trace_selftest_startup_yesp,
#endif
	.flags		= &yesp_flags,
	.set_flag	= yesp_set_flag,
	.allow_instances = true,
};

