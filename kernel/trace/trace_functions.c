/*
 * ring buffer based function tracer
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
 *
 * Based on code from the latency_tracer, that is:
 *
 *  Copyright (C) 2004-2006 Ingo Molnar
 *  Copyright (C) 2004 Nadia Yvette Chambers
 */
#include <linux/ring_buffer.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/slab.h>
#include <linux/fs.h>

#include "trace.h"

static void tracing_start_function_trace(struct trace_array *tr);
static void tracing_stop_function_trace(struct trace_array *tr);
static void
function_trace_call(unsigned long ip, unsigned long parent_ip,
		    struct ftrace_ops *op, struct pt_regs *pt_regs);
static void
function_stack_trace_call(unsigned long ip, unsigned long parent_ip,
			  struct ftrace_ops *op, struct pt_regs *pt_regs);
static struct tracer_flags func_flags;

/* Our option */
enum {
	TRACE_FUNC_OPT_STACK	= 0x1,
};

static int allocate_ftrace_ops(struct trace_array *tr)
{
	struct ftrace_ops *ops;

	ops = kzalloc(sizeof(*ops), GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	/* Currently only the non stack verision is supported */
	ops->func = function_trace_call;
	ops->flags = FTRACE_OPS_FL_RECURSION_SAFE;

	tr->ops = ops;
	ops->private = tr;
	return 0;
}


int ftrace_create_function_files(struct trace_array *tr,
				 struct dentry *parent)
{
	int ret;

	/*
	 * The top level array uses the "global_ops", and the files are
	 * created on boot up.
	 */
	if (tr->flags & TRACE_ARRAY_FL_GLOBAL)
		return 0;

	ret = allocate_ftrace_ops(tr);
	if (ret)
		return ret;

	ftrace_create_filter_files(tr->ops, parent);

	return 0;
}

void ftrace_destroy_function_files(struct trace_array *tr)
{
	ftrace_destroy_filter_files(tr->ops);
	kfree(tr->ops);
	tr->ops = NULL;
}

static int function_trace_init(struct trace_array *tr)
{
	ftrace_func_t func;

	/*
	 * Instance trace_arrays get their ops allocated
	 * at instance creation. Unless it failed
	 * the allocation.
	 */
	if (!tr->ops)
		return -ENOMEM;

	/* Currently only the global instance can do stack tracing */
	if (tr->flags & TRACE_ARRAY_FL_GLOBAL &&
	    func_flags.val & TRACE_FUNC_OPT_STACK)
		func = function_stack_trace_call;
	else
		func = function_trace_call;

	ftrace_init_array_ops(tr, func);

	tr->trace_buffer.cpu = get_cpu();
	put_cpu();

	tracing_start_cmdline_record();
	tracing_start_function_trace(tr);
	return 0;
}

static void function_trace_reset(struct trace_array *tr)
{
	tracing_stop_function_trace(tr);
	tracing_stop_cmdline_record();
	ftrace_reset_array_ops(tr);
}

static void function_trace_start(struct trace_array *tr)
{
	tracing_reset_online_cpus(&tr->trace_buffer);
}

static void
function_trace_call(unsigned long ip, unsigned long parent_ip,
		    struct ftrace_ops *op, struct pt_regs *pt_regs)
{
	struct trace_array *tr = op->private;
	struct trace_array_cpu *data;
	unsigned long flags;
	int bit;
	int cpu;
	int pc;

	if (unlikely(!tr->function_enabled))
		return;

	pc = preempt_count();
	preempt_disable_notrace();

	bit = trace_test_and_set_recursion(TRACE_FTRACE_START, TRACE_FTRACE_MAX);
	if (bit < 0)
		goto out;

	cpu = smp_processor_id();
	data = per_cpu_ptr(tr->trace_buffer.data, cpu);
	if (!atomic_read(&data->disabled)) {
		local_save_flags(flags);
		trace_function(tr, ip, parent_ip, flags, pc);
	}
	trace_clear_recursion(bit);

 out:
	preempt_enable_notrace();
}

static void
function_stack_trace_call(unsigned long ip, unsigned long parent_ip,
			  struct ftrace_ops *op, struct pt_regs *pt_regs)
{
	struct trace_array *tr = op->private;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;
	int pc;

	if (unlikely(!tr->function_enabled))
		return;

	/*
	 * Need to use raw, since this must be called before the
	 * recursive protection is performed.
	 */
	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = per_cpu_ptr(tr->trace_buffer.data, cpu);
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

static void tracing_start_function_trace(struct trace_array *tr)
{
	tr->function_enabled = 0;
	register_ftrace_function(tr->ops);
	tr->function_enabled = 1;
}

static void tracing_stop_function_trace(struct trace_array *tr)
{
	tr->function_enabled = 0;
	unregister_ftrace_function(tr->ops);
}

static int
func_set_flag(struct trace_array *tr, u32 old_flags, u32 bit, int set)
{
	switch (bit) {
	case TRACE_FUNC_OPT_STACK:
		/* do nothing if already set */
		if (!!set == !!(func_flags.val & TRACE_FUNC_OPT_STACK))
			break;

		unregister_ftrace_function(tr->ops);

		if (set) {
			tr->ops->func = function_stack_trace_call;
			register_ftrace_function(tr->ops);
		} else {
			tr->ops->func = function_trace_call;
			register_ftrace_function(tr->ops);
		}

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct tracer function_trace __tracer_data =
{
	.name		= "function",
	.init		= function_trace_init,
	.reset		= function_trace_reset,
	.start		= function_trace_start,
	.flags		= &func_flags,
	.set_flag	= func_set_flag,
	.allow_instances = true,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest	= trace_selftest_startup_function,
#endif
};

#ifdef CONFIG_DYNAMIC_FTRACE
static void update_traceon_count(void **data, bool on)
{
	long *count = (long *)data;
	long old_count = *count;

	/*
	 * Tracing gets disabled (or enabled) once per count.
	 * This function can be called at the same time on multiple CPUs.
	 * It is fine if both disable (or enable) tracing, as disabling
	 * (or enabling) the second time doesn't do anything as the
	 * state of the tracer is already disabled (or enabled).
	 * What needs to be synchronized in this case is that the count
	 * only gets decremented once, even if the tracer is disabled
	 * (or enabled) twice, as the second one is really a nop.
	 *
	 * The memory barriers guarantee that we only decrement the
	 * counter once. First the count is read to a local variable
	 * and a read barrier is used to make sure that it is loaded
	 * before checking if the tracer is in the state we want.
	 * If the tracer is not in the state we want, then the count
	 * is guaranteed to be the old count.
	 *
	 * Next the tracer is set to the state we want (disabled or enabled)
	 * then a write memory barrier is used to make sure that
	 * the new state is visible before changing the counter by
	 * one minus the old counter. This guarantees that another CPU
	 * executing this code will see the new state before seeing
	 * the new counter value, and would not do anything if the new
	 * counter is seen.
	 *
	 * Note, there is no synchronization between this and a user
	 * setting the tracing_on file. But we currently don't care
	 * about that.
	 */
	if (!old_count)
		return;

	/* Make sure we see count before checking tracing state */
	smp_rmb();

	if (on == !!tracing_is_on())
		return;

	if (on)
		tracing_on();
	else
		tracing_off();

	/* unlimited? */
	if (old_count == -1)
		return;

	/* Make sure tracing state is visible before updating count */
	smp_wmb();

	*count = old_count - 1;
}

static void
ftrace_traceon_count(unsigned long ip, unsigned long parent_ip, void **data)
{
	update_traceon_count(data, 1);
}

static void
ftrace_traceoff_count(unsigned long ip, unsigned long parent_ip, void **data)
{
	update_traceon_count(data, 0);
}

static void
ftrace_traceon(unsigned long ip, unsigned long parent_ip, void **data)
{
	if (tracing_is_on())
		return;

	tracing_on();
}

static void
ftrace_traceoff(unsigned long ip, unsigned long parent_ip, void **data)
{
	if (!tracing_is_on())
		return;

	tracing_off();
}

/*
 * Skip 4:
 *   ftrace_stacktrace()
 *   function_trace_probe_call()
 *   ftrace_ops_list_func()
 *   ftrace_call()
 */
#define STACK_SKIP 4

static void
ftrace_stacktrace(unsigned long ip, unsigned long parent_ip, void **data)
{
	trace_dump_stack(STACK_SKIP);
}

static void
ftrace_stacktrace_count(unsigned long ip, unsigned long parent_ip, void **data)
{
	long *count = (long *)data;
	long old_count;
	long new_count;

	/*
	 * Stack traces should only execute the number of times the
	 * user specified in the counter.
	 */
	do {

		if (!tracing_is_on())
			return;

		old_count = *count;

		if (!old_count)
			return;

		/* unlimited? */
		if (old_count == -1) {
			trace_dump_stack(STACK_SKIP);
			return;
		}

		new_count = old_count - 1;
		new_count = cmpxchg(count, old_count, new_count);
		if (new_count == old_count)
			trace_dump_stack(STACK_SKIP);

	} while (new_count != old_count);
}

static int update_count(void **data)
{
	unsigned long *count = (long *)data;

	if (!*count)
		return 0;

	if (*count != -1)
		(*count)--;

	return 1;
}

static void
ftrace_dump_probe(unsigned long ip, unsigned long parent_ip, void **data)
{
	if (update_count(data))
		ftrace_dump(DUMP_ALL);
}

/* Only dump the current CPU buffer. */
static void
ftrace_cpudump_probe(unsigned long ip, unsigned long parent_ip, void **data)
{
	if (update_count(data))
		ftrace_dump(DUMP_ORIG);
}

static int
ftrace_probe_print(const char *name, struct seq_file *m,
		   unsigned long ip, void *data)
{
	long count = (long)data;

	seq_printf(m, "%ps:%s", (void *)ip, name);

	if (count == -1)
		seq_puts(m, ":unlimited\n");
	else
		seq_printf(m, ":count=%ld\n", count);

	return 0;
}

static int
ftrace_traceon_print(struct seq_file *m, unsigned long ip,
			 struct ftrace_probe_ops *ops, void *data)
{
	return ftrace_probe_print("traceon", m, ip, data);
}

static int
ftrace_traceoff_print(struct seq_file *m, unsigned long ip,
			 struct ftrace_probe_ops *ops, void *data)
{
	return ftrace_probe_print("traceoff", m, ip, data);
}

static int
ftrace_stacktrace_print(struct seq_file *m, unsigned long ip,
			struct ftrace_probe_ops *ops, void *data)
{
	return ftrace_probe_print("stacktrace", m, ip, data);
}

static int
ftrace_dump_print(struct seq_file *m, unsigned long ip,
			struct ftrace_probe_ops *ops, void *data)
{
	return ftrace_probe_print("dump", m, ip, data);
}

static int
ftrace_cpudump_print(struct seq_file *m, unsigned long ip,
			struct ftrace_probe_ops *ops, void *data)
{
	return ftrace_probe_print("cpudump", m, ip, data);
}

static struct ftrace_probe_ops traceon_count_probe_ops = {
	.func			= ftrace_traceon_count,
	.print			= ftrace_traceon_print,
};

static struct ftrace_probe_ops traceoff_count_probe_ops = {
	.func			= ftrace_traceoff_count,
	.print			= ftrace_traceoff_print,
};

static struct ftrace_probe_ops stacktrace_count_probe_ops = {
	.func			= ftrace_stacktrace_count,
	.print			= ftrace_stacktrace_print,
};

static struct ftrace_probe_ops dump_probe_ops = {
	.func			= ftrace_dump_probe,
	.print			= ftrace_dump_print,
};

static struct ftrace_probe_ops cpudump_probe_ops = {
	.func			= ftrace_cpudump_probe,
	.print			= ftrace_cpudump_print,
};

static struct ftrace_probe_ops traceon_probe_ops = {
	.func			= ftrace_traceon,
	.print			= ftrace_traceon_print,
};

static struct ftrace_probe_ops traceoff_probe_ops = {
	.func			= ftrace_traceoff,
	.print			= ftrace_traceoff_print,
};

static struct ftrace_probe_ops stacktrace_probe_ops = {
	.func			= ftrace_stacktrace,
	.print			= ftrace_stacktrace_print,
};

static int
ftrace_trace_probe_callback(struct ftrace_probe_ops *ops,
			    struct ftrace_hash *hash, char *glob,
			    char *cmd, char *param, int enable)
{
	void *count = (void *)-1;
	char *number;
	int ret;

	/* hash funcs only work with set_ftrace_filter */
	if (!enable)
		return -EINVAL;

	if (glob[0] == '!') {
		unregister_ftrace_function_probe_func(glob+1, ops);
		return 0;
	}

	if (!param)
		goto out_reg;

	number = strsep(&param, ":");

	if (!strlen(number))
		goto out_reg;

	/*
	 * We use the callback data field (which is a pointer)
	 * as our counter.
	 */
	ret = kstrtoul(number, 0, (unsigned long *)&count);
	if (ret)
		return ret;

 out_reg:
	ret = register_ftrace_function_probe(glob, ops, count);

	return ret < 0 ? ret : 0;
}

static int
ftrace_trace_onoff_callback(struct ftrace_hash *hash,
			    char *glob, char *cmd, char *param, int enable)
{
	struct ftrace_probe_ops *ops;

	/* we register both traceon and traceoff to this callback */
	if (strcmp(cmd, "traceon") == 0)
		ops = param ? &traceon_count_probe_ops : &traceon_probe_ops;
	else
		ops = param ? &traceoff_count_probe_ops : &traceoff_probe_ops;

	return ftrace_trace_probe_callback(ops, hash, glob, cmd,
					   param, enable);
}

static int
ftrace_stacktrace_callback(struct ftrace_hash *hash,
			   char *glob, char *cmd, char *param, int enable)
{
	struct ftrace_probe_ops *ops;

	ops = param ? &stacktrace_count_probe_ops : &stacktrace_probe_ops;

	return ftrace_trace_probe_callback(ops, hash, glob, cmd,
					   param, enable);
}

static int
ftrace_dump_callback(struct ftrace_hash *hash,
			   char *glob, char *cmd, char *param, int enable)
{
	struct ftrace_probe_ops *ops;

	ops = &dump_probe_ops;

	/* Only dump once. */
	return ftrace_trace_probe_callback(ops, hash, glob, cmd,
					   "1", enable);
}

static int
ftrace_cpudump_callback(struct ftrace_hash *hash,
			   char *glob, char *cmd, char *param, int enable)
{
	struct ftrace_probe_ops *ops;

	ops = &cpudump_probe_ops;

	/* Only dump once. */
	return ftrace_trace_probe_callback(ops, hash, glob, cmd,
					   "1", enable);
}

static struct ftrace_func_command ftrace_traceon_cmd = {
	.name			= "traceon",
	.func			= ftrace_trace_onoff_callback,
};

static struct ftrace_func_command ftrace_traceoff_cmd = {
	.name			= "traceoff",
	.func			= ftrace_trace_onoff_callback,
};

static struct ftrace_func_command ftrace_stacktrace_cmd = {
	.name			= "stacktrace",
	.func			= ftrace_stacktrace_callback,
};

static struct ftrace_func_command ftrace_dump_cmd = {
	.name			= "dump",
	.func			= ftrace_dump_callback,
};

static struct ftrace_func_command ftrace_cpudump_cmd = {
	.name			= "cpudump",
	.func			= ftrace_cpudump_callback,
};

static int __init init_func_cmd_traceon(void)
{
	int ret;

	ret = register_ftrace_command(&ftrace_traceoff_cmd);
	if (ret)
		return ret;

	ret = register_ftrace_command(&ftrace_traceon_cmd);
	if (ret)
		goto out_free_traceoff;

	ret = register_ftrace_command(&ftrace_stacktrace_cmd);
	if (ret)
		goto out_free_traceon;

	ret = register_ftrace_command(&ftrace_dump_cmd);
	if (ret)
		goto out_free_stacktrace;

	ret = register_ftrace_command(&ftrace_cpudump_cmd);
	if (ret)
		goto out_free_dump;

	return 0;

 out_free_dump:
	unregister_ftrace_command(&ftrace_dump_cmd);
 out_free_stacktrace:
	unregister_ftrace_command(&ftrace_stacktrace_cmd);
 out_free_traceon:
	unregister_ftrace_command(&ftrace_traceon_cmd);
 out_free_traceoff:
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
core_initcall(init_function_trace);
