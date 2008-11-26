/*
 *
 * Function graph tracer.
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

#define TRACE_GRAPH_INDENT	2

#define TRACE_GRAPH_PRINT_OVERRUN	0x1
static struct tracer_opt trace_opts[] = {
	/* Display overruns or not */
	{ TRACER_OPT(overrun, TRACE_GRAPH_PRINT_OVERRUN) },
	{ } /* Empty entry */
};

static struct tracer_flags tracer_flags = {
	.val = 0, /* Don't display overruns by default */
	.opts = trace_opts
};

/* pid on the last trace processed */
static pid_t last_pid = -1;

static int graph_trace_init(struct trace_array *tr)
{
	int cpu, ret;

	for_each_online_cpu(cpu)
		tracing_reset(tr, cpu);

	ret = register_ftrace_graph(&trace_graph_return,
					&trace_graph_entry);
	if (ret)
		return ret;
	tracing_start_cmdline_record();

	return 0;
}

static void graph_trace_reset(struct trace_array *tr)
{
	tracing_stop_cmdline_record();
	unregister_ftrace_graph();
}

/* If the pid changed since the last trace, output this event */
static int verif_pid(struct trace_seq *s, pid_t pid)
{
	char *comm;

	if (last_pid != -1 && last_pid == pid)
		return 1;

	last_pid = pid;
	comm = trace_find_cmdline(pid);

	return trace_seq_printf(s, "\n------------8<---------- thread %s-%d"
				    " ------------8<----------\n\n",
				    comm, pid);
}

static enum print_line_t
print_graph_entry(struct ftrace_graph_ent *call, struct trace_seq *s,
		struct trace_entry *ent)
{
	int i;
	int ret;

	if (!verif_pid(s, ent->pid))
		return TRACE_TYPE_PARTIAL_LINE;

	for (i = 0; i < call->depth * TRACE_GRAPH_INDENT; i++) {
		ret = trace_seq_printf(s, " ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	ret = seq_print_ip_sym(s, call->func, 0);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	ret = trace_seq_printf(s, "() {\n");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;
	return TRACE_TYPE_HANDLED;
}

static enum print_line_t
print_graph_return(struct ftrace_graph_ret *trace, struct trace_seq *s,
		   struct trace_entry *ent)
{
	int i;
	int ret;

	if (!verif_pid(s, ent->pid))
		return TRACE_TYPE_PARTIAL_LINE;

	for (i = 0; i < trace->depth * TRACE_GRAPH_INDENT; i++) {
		ret = trace_seq_printf(s, " ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	ret = trace_seq_printf(s, "} ");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	ret = trace_seq_printf(s, "%llu\n", trace->rettime - trace->calltime);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	if (tracer_flags.val & TRACE_GRAPH_PRINT_OVERRUN) {
		ret = trace_seq_printf(s, " (Overruns: %lu)\n",
					trace->overrun);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}
	return TRACE_TYPE_HANDLED;
}

enum print_line_t
print_graph_function(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry = iter->ent;

	switch (entry->type) {
	case TRACE_GRAPH_ENT: {
		struct ftrace_graph_ent_entry *field;
		trace_assign_type(field, entry);
		return print_graph_entry(&field->graph_ent, s, entry);
	}
	case TRACE_GRAPH_RET: {
		struct ftrace_graph_ret_entry *field;
		trace_assign_type(field, entry);
		return print_graph_return(&field->ret, s, entry);
	}
	default:
		return TRACE_TYPE_UNHANDLED;
	}
}

static struct tracer graph_trace __read_mostly = {
	.name	     = "function-graph",
	.init	     = graph_trace_init,
	.reset	     = graph_trace_reset,
	.print_line = print_graph_function,
	.flags		= &tracer_flags,
};

static __init int init_graph_trace(void)
{
	return register_tracer(&graph_trace);
}

device_initcall(init_graph_trace);
