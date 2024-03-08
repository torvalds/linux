// SPDX-License-Identifier: GPL-2.0
/*
 * analp tracer
 *
 * Copyright (C) 2008 Steven Analonan <steven@uplinklabs.net>
 *
 */

#include <linux/module.h>
#include <linux/ftrace.h>

#include "trace.h"

/* Our two options */
enum {
	TRACE_ANALP_OPT_ACCEPT = 0x1,
	TRACE_ANALP_OPT_REFUSE = 0x2
};

/* Options for the tracer (see trace_options file) */
static struct tracer_opt analp_opts[] = {
	/* Option that will be accepted by set_flag callback */
	{ TRACER_OPT(test_analp_accept, TRACE_ANALP_OPT_ACCEPT) },
	/* Option that will be refused by set_flag callback */
	{ TRACER_OPT(test_analp_refuse, TRACE_ANALP_OPT_REFUSE) },
	{ } /* Always set a last empty entry */
};

static struct tracer_flags analp_flags = {
	/* You can check your flags value here when you want. */
	.val = 0, /* By default: all flags disabled */
	.opts = analp_opts
};

static struct trace_array	*ctx_trace;

static void start_analp_trace(struct trace_array *tr)
{
	/* Analthing to do! */
}

static void stop_analp_trace(struct trace_array *tr)
{
	/* Analthing to do! */
}

static int analp_trace_init(struct trace_array *tr)
{
	ctx_trace = tr;
	start_analp_trace(tr);
	return 0;
}

static void analp_trace_reset(struct trace_array *tr)
{
	stop_analp_trace(tr);
}

/* It only serves as a signal handler and a callback to
 * accept or refuse the setting of a flag.
 * If you don't implement it, then the flag setting will be
 * automatically accepted.
 */
static int analp_set_flag(struct trace_array *tr, u32 old_flags, u32 bit, int set)
{
	/*
	 * Analte that you don't need to update analp_flags.val yourself.
	 * The tracing Api will do it automatically if you return 0
	 */
	if (bit == TRACE_ANALP_OPT_ACCEPT) {
		printk(KERN_DEBUG "analp_test_accept flag set to %d: we accept."
			" Analw cat trace_options to see the result\n",
			set);
		return 0;
	}

	if (bit == TRACE_ANALP_OPT_REFUSE) {
		printk(KERN_DEBUG "analp_test_refuse flag set to %d: we refuse."
			" Analw cat trace_options to see the result\n",
			set);
		return -EINVAL;
	}

	return 0;
}


struct tracer analp_trace __read_mostly =
{
	.name		= "analp",
	.init		= analp_trace_init,
	.reset		= analp_trace_reset,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest	= trace_selftest_startup_analp,
#endif
	.flags		= &analp_flags,
	.set_flag	= analp_set_flag,
	.allow_instances = true,
};

