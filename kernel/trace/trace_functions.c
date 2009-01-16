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

/* function tracing enabled */
static int			ftrace_function_enabled;

static struct trace_array	*func_trace;

static void tracing_start_function_trace(void);
static void tracing_stop_function_trace(void);

static void start_function_trace(struct trace_array *tr)
{
	func_trace = tr;
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

static void
function_trace_call_preempt_only(unsigned long ip, unsigned long parent_ip)
{
	struct trace_array *tr = func_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu, resched;
	int pc;

	if (unlikely(!ftrace_function_enabled))
		return;

	pc = preempt_count();
	resched = ftrace_preempt_disable();
	local_save_flags(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		trace_function(tr, data, ip, parent_ip, flags, pc);

	atomic_dec(&data->disabled);
	ftrace_preempt_enable(resched);
}

static void
function_trace_call(unsigned long ip, unsigned long parent_ip)
{
	struct trace_array *tr = func_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;
	int pc;

	if (unlikely(!ftrace_function_enabled))
		return;

	/*
	 * Need to use raw, since this must be called before the
	 * recursive protection is performed.
	 */
	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1)) {
		pc = preempt_count();
		trace_function(tr, data, ip, parent_ip, flags, pc);
	}

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

static void
function_stack_trace_call(unsigned long ip, unsigned long parent_ip)
{
	struct trace_array *tr = func_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;
	int pc;

	if (unlikely(!ftrace_function_enabled))
		return;

	/*
	 * Need to use raw, since this must be called before the
	 * recursive protection is performed.
	 */
	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1)) {
		pc = preempt_count();
		trace_function(tr, data, ip, parent_ip, flags, pc);
		/*
		 * skip over 5 funcs:
		 *    __ftrace_trace_stack,
		 *    __trace_stack,
		 *    function_stack_trace_call
		 *    ftrace_list_func
		 *    ftrace_call
		 */
		__trace_stack(tr, data, flags, 5, pc);
	}

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}


static struct ftrace_ops trace_ops __read_mostly =
{
	.func = function_trace_call,
};

static struct ftrace_ops trace_stack_ops __read_mostly =
{
	.func = function_stack_trace_call,
};

/* Our two options */
enum {
	TRACE_FUNC_OPT_STACK = 0x1,
};

static struct tracer_opt func_opts[] = {
#ifdef CONFIG_STACKTRACE
	{ TRACER_OPT(func_stack_trace, TRACE_FUNC_OPT_STACK) },
#endif
	{ } /* Always set a last empty entry */
};

static struct tracer_flags func_flags = {
	.val = 0, /* By default: all flags disabled */
	.opts = func_opts
};

static void tracing_start_function_trace(void)
{
	ftrace_function_enabled = 0;

	if (trace_flags & TRACE_ITER_PREEMPTONLY)
		trace_ops.func = function_trace_call_preempt_only;
	else
		trace_ops.func = function_trace_call;

	if (func_flags.val & TRACE_FUNC_OPT_STACK)
		register_ftrace_function(&trace_stack_ops);
	else
		register_ftrace_function(&trace_ops);

	ftrace_function_enabled = 1;
}

static void tracing_stop_function_trace(void)
{
	ftrace_function_enabled = 0;
	/* OK if they are not registered */
	unregister_ftrace_function(&trace_stack_ops);
	unregister_ftrace_function(&trace_ops);
}

static int func_set_flag(u32 old_flags, u32 bit, int set)
{
	if (bit == TRACE_FUNC_OPT_STACK) {
		/* do nothing if already set */
		if (!!set == !!(func_flags.val & TRACE_FUNC_OPT_STACK))
			return 0;

		if (set) {
			unregister_ftrace_function(&trace_ops);
			register_ftrace_function(&trace_stack_ops);
		} else {
			unregister_ftrace_function(&trace_stack_ops);
			register_ftrace_function(&trace_ops);
		}

		return 0;
	}

	return -EINVAL;
}

static struct tracer function_trace __read_mostly =
{
	.name		= "function",
	.init		= function_trace_init,
	.reset		= function_trace_reset,
	.start		= function_trace_start,
	.flags		= &func_flags,
	.set_flag	= func_set_flag,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest	= trace_selftest_startup_function,
#endif
};

static __init int init_function_trace(void)
{
	return register_tracer(&function_trace);
}

device_initcall(init_function_trace);
