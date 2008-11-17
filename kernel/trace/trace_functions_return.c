/*
 *
 * Function return tracer.
 * Copyright (c) 2008 Frederic Weisbecker <fweisbec@gmail.com>
 * Mostly borrowed from function tracer which
 * is Copyright (c) Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/fs.h>

#include "trace.h"


static int return_trace_init(struct trace_array *tr)
{
	int cpu;
	for_each_online_cpu(cpu)
		tracing_reset(tr, cpu);

	return register_ftrace_return(&trace_function_return);
}

static void return_trace_reset(struct trace_array *tr)
{
		unregister_ftrace_return();
}


enum print_line_t
print_return_function(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry = iter->ent;
	struct ftrace_ret_entry *field;
	int ret;

	if (entry->type == TRACE_FN_RET) {
		trace_assign_type(field, entry);
		ret = trace_seq_printf(s, "%pF -> ", (void *)field->parent_ip);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		ret = seq_print_ip_sym(s, field->ip,
					trace_flags & TRACE_ITER_SYM_MASK);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		ret = trace_seq_printf(s, " (%llu ns)\n",
					field->rettime - field->calltime);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		else
			return TRACE_TYPE_HANDLED;
	}
	return TRACE_TYPE_UNHANDLED;
}

static struct tracer return_trace __read_mostly =
{
	.name	     = "return",
	.init	     = return_trace_init,
	.reset	     = return_trace_reset,
	.print_line = print_return_function
};

static __init int init_return_trace(void)
{
	return register_tracer(&return_trace);
}

device_initcall(init_return_trace);
