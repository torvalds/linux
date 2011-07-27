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
#include <linux/ring_buffer.h>
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

static int function_trace_init(struct trace_array *tr)
{
	func_trace = tr;
	tr->cpu = get_cpu();
	put_cpu();

	tracing_start_cmdline_record();
	tracing_start_function_trace();
	return 0;
}

static void function_trace_reset(struct trace_array *tr)
{
	tracing_stop_function_trace();
	tracing_stop_cmdline_record();
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
	int cpu;
	int pc;

	if (unlikely(!ftrace_function_enabled))
		return;

	pc = preempt_count();
	preempt_disable_notrace();
	local_save_flags(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		trace_function(tr, ip, parent_ip, flags, pc);

	atomic_dec(&data->disabled);
	preempt_enable_notrace();
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
		trace_function(tr, ip, parent_ip, flags, pc);
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
		trace_function(tr, ip, parent_ip, flags, pc);
		/*
		 * skip over 5 funcs:
		 *    __ftrace_trace_stack,
		 *    __trace_stack,
		 *    function_stack_trace_call
		 *    ftrace_list_func
		 *    ftrace_call
		 */
		__trace_stack(tr, flags, 5, pc);
	}

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}


static struct ftrace_ops trace_ops __read_mostly =
{
	.func = function_trace_call,
	.flags = FTRACE_OPS_FL_GLOBAL,
};

static struct ftrace_ops trace_stack_ops __read_mostly =
{
	.func = function_stack_trace_call,
	.flags = FTRACE_OPS_FL_GLOBAL,
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

	if (func_flags.val & TRACE_FUNC_OPT_STACK)
		unregister_ftrace_function(&trace_stack_ops);
	else
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
	.wait_pipe	= poll_wait_pipe,
	.flags		= &func_flags,
	.set_flag	= func_set_flag,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest	= trace_selftest_startup_function,
#endif
};

#ifdef CONFIG_DYNAMIC_FTRACE
static void
ftrace_traceon(unsigned long ip, unsigned long parent_ip, void **data)
{
	long *count = (long *)data;

	if (tracing_is_on())
		return;

	if (!*count)
		return;

	if (*count != -1)
		(*count)--;

	tracing_on();
}

static void
ftrace_traceoff(unsigned long ip, unsigned long parent_ip, void **data)
{
	long *count = (long *)data;

	if (!tracing_is_on())
		return;

	if (!*count)
		return;

	if (*count != -1)
		(*count)--;

	tracing_off();
}

static int
ftrace_trace_onoff_print(struct seq_file *m, unsigned long ip,
			 struct ftrace_probe_ops *ops, void *data);

static struct ftrace_probe_ops traceon_probe_ops = {
	.func			= ftrace_traceon,
	.print			= ftrace_trace_onoff_print,
};

static struct ftrace_probe_ops traceoff_probe_ops = {
	.func			= ftrace_traceoff,
	.print			= ftrace_trace_onoff_print,
};

static int
ftrace_trace_onoff_print(struct seq_file *m, unsigned long ip,
			 struct ftrace_probe_ops *ops, void *data)
{
	long count = (long)data;

	seq_printf(m, "%ps:", (void *)ip);

	if (ops == &traceon_probe_ops)
		seq_printf(m, "traceon");
	else
		seq_printf(m, "traceoff");

	if (count == -1)
		seq_printf(m, ":unlimited\n");
	else
		seq_printf(m, ":count=%ld\n", count);

	return 0;
}

static int
ftrace_trace_onoff_unreg(char *glob, char *cmd, char *param)
{
	struct ftrace_probe_ops *ops;

	/* we register both traceon and traceoff to this callback */
	if (strcmp(cmd, "traceon") == 0)
		ops = &traceon_probe_ops;
	else
		ops = &traceoff_probe_ops;

	unregister_ftrace_function_probe_func(glob, ops);

	return 0;
}

static int
ftrace_trace_onoff_callback(char *glob, char *cmd, char *param, int enable)
{
	struct ftrace_probe_ops *ops;
	void *count = (void *)-1;
	char *number;
	int ret;

	/* hash funcs only work with set_ftrace_filter */
	if (!enable)
		return -EINVAL;

	if (glob[0] == '!')
		return ftrace_trace_onoff_unreg(glob+1, cmd, param);

	/* we register both traceon and traceoff to this callback */
	if (strcmp(cmd, "traceon") == 0)
		ops = &traceon_probe_ops;
	else
		ops = &traceoff_probe_ops;

	if (!param)
		goto out_reg;

	number = strsep(&param, ":");

	if (!strlen(number))
		goto out_reg;

	/*
	 * We use the callback data field (which is a pointer)
	 * as our counter.
	 */
	ret = strict_strtoul(number, 0, (unsigned long *)&count);
	if (ret)
		return ret;

 out_reg:
	ret = register_ftrace_function_probe(glob, ops, count);

	return ret < 0 ? ret : 0;
}

static struct ftrace_func_command ftrace_traceon_cmd = {
	.name			= "traceon",
	.func			= ftrace_trace_onoff_callback,
};

static struct ftrace_func_command ftrace_traceoff_cmd = {
	.name			= "traceoff",
	.func			= ftrace_trace_onoff_callback,
};

static int __init init_func_cmd_traceon(void)
{
	int ret;

	ret = register_ftrace_command(&ftrace_traceoff_cmd);
	if (ret)
		return ret;

	ret = register_ftrace_command(&ftrace_traceon_cmd);
	if (ret)
		unregister_ftrace_command(&ftrace_traceoff_cmd);
	return ret;
}
#else
static inline int init_func_cmd_traceon(void)
{
	return 0;
}
#endif /* CONFIG_DYNAMIC_FTRACE */

static __init int init_function_trace(void)
{
	init_func_cmd_traceon();
	return register_tracer(&function_trace);
}
device_initcall(init_function_trace);

