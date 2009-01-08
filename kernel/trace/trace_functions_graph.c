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

/* Flag options */
#define TRACE_GRAPH_PRINT_OVERRUN	0x1
#define TRACE_GRAPH_PRINT_CPU		0x2
#define TRACE_GRAPH_PRINT_OVERHEAD	0x4
#define TRACE_GRAPH_PRINT_PROC		0x8

static struct tracer_opt trace_opts[] = {
	/* Display overruns ? */
	{ TRACER_OPT(funcgraph-overrun, TRACE_GRAPH_PRINT_OVERRUN) },
	/* Display CPU ? */
	{ TRACER_OPT(funcgraph-cpu, TRACE_GRAPH_PRINT_CPU) },
	/* Display Overhead ? */
	{ TRACER_OPT(funcgraph-overhead, TRACE_GRAPH_PRINT_OVERHEAD) },
	/* Display proc name/pid */
	{ TRACER_OPT(funcgraph-proc, TRACE_GRAPH_PRINT_PROC) },
	{ } /* Empty entry */
};

static struct tracer_flags tracer_flags = {
	/* Don't display overruns and proc by default */
	.val = TRACE_GRAPH_PRINT_CPU | TRACE_GRAPH_PRINT_OVERHEAD,
	.opts = trace_opts
};

/* pid on the last trace processed */
static pid_t last_pid[NR_CPUS] = { [0 ... NR_CPUS-1] = -1 };

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

static inline int log10_cpu(int nb)
{
	if (nb / 100)
		return 3;
	if (nb / 10)
		return 2;
	return 1;
}

static enum print_line_t
print_graph_cpu(struct trace_seq *s, int cpu)
{
	int i;
	int ret;
	int log10_this = log10_cpu(cpu);
	int log10_all = log10_cpu(cpumask_weight(cpu_online_mask));


	/*
	 * Start with a space character - to make it stand out
	 * to the right a bit when trace output is pasted into
	 * email:
	 */
	ret = trace_seq_printf(s, " ");

	/*
	 * Tricky - we space the CPU field according to the max
	 * number of online CPUs. On a 2-cpu system it would take
	 * a maximum of 1 digit - on a 128 cpu system it would
	 * take up to 3 digits:
	 */
	for (i = 0; i < log10_all - log10_this; i++) {
		ret = trace_seq_printf(s, " ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}
	ret = trace_seq_printf(s, "%d) ", cpu);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

#define TRACE_GRAPH_PROCINFO_LENGTH	14

static enum print_line_t
print_graph_proc(struct trace_seq *s, pid_t pid)
{
	int i;
	int ret;
	int len;
	char comm[8];
	int spaces = 0;
	/* sign + log10(MAX_INT) + '\0' */
	char pid_str[11];

	strncpy(comm, trace_find_cmdline(pid), 7);
	comm[7] = '\0';
	sprintf(pid_str, "%d", pid);

	/* 1 stands for the "-" character */
	len = strlen(comm) + strlen(pid_str) + 1;

	if (len < TRACE_GRAPH_PROCINFO_LENGTH)
		spaces = TRACE_GRAPH_PROCINFO_LENGTH - len;

	/* First spaces to align center */
	for (i = 0; i < spaces / 2; i++) {
		ret = trace_seq_printf(s, " ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	ret = trace_seq_printf(s, "%s-%s", comm, pid_str);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Last spaces to align center */
	for (i = 0; i < spaces - (spaces / 2); i++) {
		ret = trace_seq_printf(s, " ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}
	return TRACE_TYPE_HANDLED;
}


/* If the pid changed since the last trace, output this event */
static enum print_line_t
verif_pid(struct trace_seq *s, pid_t pid, int cpu)
{
	pid_t prev_pid;
	int ret;

	if (last_pid[cpu] != -1 && last_pid[cpu] == pid)
		return TRACE_TYPE_HANDLED;

	prev_pid = last_pid[cpu];
	last_pid[cpu] = pid;

/*
 * Context-switch trace line:

 ------------------------------------------
 | 1)  migration/0--1  =>  sshd-1755
 ------------------------------------------

 */
	ret = trace_seq_printf(s,
		" ------------------------------------------\n");
	if (!ret)
		TRACE_TYPE_PARTIAL_LINE;

	ret = print_graph_cpu(s, cpu);
	if (ret == TRACE_TYPE_PARTIAL_LINE)
		TRACE_TYPE_PARTIAL_LINE;

	ret = print_graph_proc(s, prev_pid);
	if (ret == TRACE_TYPE_PARTIAL_LINE)
		TRACE_TYPE_PARTIAL_LINE;

	ret = trace_seq_printf(s, " => ");
	if (!ret)
		TRACE_TYPE_PARTIAL_LINE;

	ret = print_graph_proc(s, pid);
	if (ret == TRACE_TYPE_PARTIAL_LINE)
		TRACE_TYPE_PARTIAL_LINE;

	ret = trace_seq_printf(s,
		"\n ------------------------------------------\n\n");
	if (!ret)
		TRACE_TYPE_PARTIAL_LINE;

	return ret;
}

static bool
trace_branch_is_leaf(struct trace_iterator *iter,
		struct ftrace_graph_ent_entry *curr)
{
	struct ring_buffer_iter *ring_iter;
	struct ring_buffer_event *event;
	struct ftrace_graph_ret_entry *next;

	ring_iter = iter->buffer_iter[iter->cpu];

	if (!ring_iter)
		return false;

	event = ring_buffer_iter_peek(ring_iter, NULL);

	if (!event)
		return false;

	next = ring_buffer_event_data(event);

	if (next->ent.type != TRACE_GRAPH_RET)
		return false;

	if (curr->ent.pid != next->ent.pid ||
			curr->graph_ent.func != next->ret.func)
		return false;

	return true;
}

static enum print_line_t
print_graph_irq(struct trace_seq *s, unsigned long addr,
				enum trace_type type, int cpu, pid_t pid)
{
	int ret;

	if (addr < (unsigned long)__irqentry_text_start ||
		addr >= (unsigned long)__irqentry_text_end)
		return TRACE_TYPE_UNHANDLED;

	if (type == TRACE_GRAPH_ENT) {
		ret = trace_seq_printf(s, "==========> |  ");
	} else {
		/* Cpu */
		if (tracer_flags.val & TRACE_GRAPH_PRINT_CPU) {
			ret = print_graph_cpu(s, cpu);
			if (ret == TRACE_TYPE_PARTIAL_LINE)
				return TRACE_TYPE_PARTIAL_LINE;
		}
		/* Proc */
		if (tracer_flags.val & TRACE_GRAPH_PRINT_PROC) {
			ret = print_graph_proc(s, pid);
			if (ret == TRACE_TYPE_PARTIAL_LINE)
				return TRACE_TYPE_PARTIAL_LINE;

			ret = trace_seq_printf(s, " | ");
			if (!ret)
				return TRACE_TYPE_PARTIAL_LINE;
		}

		/* No overhead */
		if (tracer_flags.val & TRACE_GRAPH_PRINT_OVERHEAD) {
			ret = trace_seq_printf(s, "  ");
			if (!ret)
				return TRACE_TYPE_PARTIAL_LINE;
		}

		ret = trace_seq_printf(s, "<========== |\n");
	}
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;
	return TRACE_TYPE_HANDLED;
}

static enum print_line_t
print_graph_duration(unsigned long long duration, struct trace_seq *s)
{
	unsigned long nsecs_rem = do_div(duration, 1000);
	/* log10(ULONG_MAX) + '\0' */
	char msecs_str[21];
	char nsecs_str[5];
	int ret, len;
	int i;

	sprintf(msecs_str, "%lu", (unsigned long) duration);

	/* Print msecs */
	ret = trace_seq_printf(s, msecs_str);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	len = strlen(msecs_str);

	/* Print nsecs (we don't want to exceed 7 numbers) */
	if (len < 7) {
		snprintf(nsecs_str, 8 - len, "%03lu", nsecs_rem);
		ret = trace_seq_printf(s, ".%s", nsecs_str);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		len += strlen(nsecs_str);
	}

	ret = trace_seq_printf(s, " us ");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Print remaining spaces to fit the row's width */
	for (i = len; i < 7; i++) {
		ret = trace_seq_printf(s, " ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	ret = trace_seq_printf(s, "|  ");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;
	return TRACE_TYPE_HANDLED;

}

/* Signal a overhead of time execution to the output */
static int
print_graph_overhead(unsigned long long duration, struct trace_seq *s)
{
	/* Duration exceeded 100 msecs */
	if (duration > 100000ULL)
		return trace_seq_printf(s, "! ");

	/* Duration exceeded 10 msecs */
	if (duration > 10000ULL)
		return trace_seq_printf(s, "+ ");

	return trace_seq_printf(s, "  ");
}

/* Case of a leaf function on its call entry */
static enum print_line_t
print_graph_entry_leaf(struct trace_iterator *iter,
		struct ftrace_graph_ent_entry *entry, struct trace_seq *s)
{
	struct ftrace_graph_ret_entry *ret_entry;
	struct ftrace_graph_ret *graph_ret;
	struct ring_buffer_event *event;
	struct ftrace_graph_ent *call;
	unsigned long long duration;
	int ret;
	int i;

	event = ring_buffer_read(iter->buffer_iter[iter->cpu], NULL);
	ret_entry = ring_buffer_event_data(event);
	graph_ret = &ret_entry->ret;
	call = &entry->graph_ent;
	duration = graph_ret->rettime - graph_ret->calltime;

	/* Overhead */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_OVERHEAD) {
		ret = print_graph_overhead(duration, s);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	/* Duration */
	ret = print_graph_duration(duration, s);
	if (ret == TRACE_TYPE_PARTIAL_LINE)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Function */
	for (i = 0; i < call->depth * TRACE_GRAPH_INDENT; i++) {
		ret = trace_seq_printf(s, " ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	ret = seq_print_ip_sym(s, call->func, 0);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	ret = trace_seq_printf(s, "();\n");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t
print_graph_entry_nested(struct ftrace_graph_ent_entry *entry,
			struct trace_seq *s, pid_t pid, int cpu)
{
	int i;
	int ret;
	struct ftrace_graph_ent *call = &entry->graph_ent;

	/* No overhead */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_OVERHEAD) {
		ret = trace_seq_printf(s, "  ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	/* Interrupt */
	ret = print_graph_irq(s, call->func, TRACE_GRAPH_ENT, cpu, pid);
	if (ret == TRACE_TYPE_UNHANDLED) {
		/* No time */
		ret = trace_seq_printf(s, "            |  ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	} else {
		if (ret == TRACE_TYPE_PARTIAL_LINE)
			return TRACE_TYPE_PARTIAL_LINE;
	}


	/* Function */
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
print_graph_entry(struct ftrace_graph_ent_entry *field, struct trace_seq *s,
			struct trace_iterator *iter, int cpu)
{
	int ret;
	struct trace_entry *ent = iter->ent;

	/* Pid */
	if (verif_pid(s, ent->pid, cpu) == TRACE_TYPE_PARTIAL_LINE)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Cpu */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_CPU) {
		ret = print_graph_cpu(s, cpu);
		if (ret == TRACE_TYPE_PARTIAL_LINE)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	/* Proc */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_PROC) {
		ret = print_graph_proc(s, ent->pid);
		if (ret == TRACE_TYPE_PARTIAL_LINE)
			return TRACE_TYPE_PARTIAL_LINE;

		ret = trace_seq_printf(s, " | ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	if (trace_branch_is_leaf(iter, field))
		return print_graph_entry_leaf(iter, field, s);
	else
		return print_graph_entry_nested(field, s, iter->ent->pid, cpu);

}

static enum print_line_t
print_graph_return(struct ftrace_graph_ret *trace, struct trace_seq *s,
		   struct trace_entry *ent, int cpu)
{
	int i;
	int ret;
	unsigned long long duration = trace->rettime - trace->calltime;

	/* Pid */
	if (verif_pid(s, ent->pid, cpu) == TRACE_TYPE_PARTIAL_LINE)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Cpu */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_CPU) {
		ret = print_graph_cpu(s, cpu);
		if (ret == TRACE_TYPE_PARTIAL_LINE)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	/* Proc */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_PROC) {
		ret = print_graph_proc(s, ent->pid);
		if (ret == TRACE_TYPE_PARTIAL_LINE)
			return TRACE_TYPE_PARTIAL_LINE;

		ret = trace_seq_printf(s, " | ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	/* Overhead */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_OVERHEAD) {
		ret = print_graph_overhead(duration, s);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	/* Duration */
	ret = print_graph_duration(duration, s);
	if (ret == TRACE_TYPE_PARTIAL_LINE)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Closing brace */
	for (i = 0; i < trace->depth * TRACE_GRAPH_INDENT; i++) {
		ret = trace_seq_printf(s, " ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	ret = trace_seq_printf(s, "}\n");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Overrun */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_OVERRUN) {
		ret = trace_seq_printf(s, " (Overruns: %lu)\n",
					trace->overrun);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	ret = print_graph_irq(s, trace->func, TRACE_GRAPH_RET, cpu, ent->pid);
	if (ret == TRACE_TYPE_PARTIAL_LINE)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t
print_graph_comment(struct print_entry *trace, struct trace_seq *s,
		   struct trace_entry *ent, struct trace_iterator *iter)
{
	int i;
	int ret;

	/* Pid */
	if (verif_pid(s, ent->pid, iter->cpu) == TRACE_TYPE_PARTIAL_LINE)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Cpu */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_CPU) {
		ret = print_graph_cpu(s, iter->cpu);
		if (ret == TRACE_TYPE_PARTIAL_LINE)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	/* Proc */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_PROC) {
		ret = print_graph_proc(s, ent->pid);
		if (ret == TRACE_TYPE_PARTIAL_LINE)
			return TRACE_TYPE_PARTIAL_LINE;

		ret = trace_seq_printf(s, " | ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	/* No overhead */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_OVERHEAD) {
		ret = trace_seq_printf(s, "  ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	/* No time */
	ret = trace_seq_printf(s, "            |  ");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Indentation */
	if (trace->depth > 0)
		for (i = 0; i < (trace->depth + 1) * TRACE_GRAPH_INDENT; i++) {
			ret = trace_seq_printf(s, " ");
			if (!ret)
				return TRACE_TYPE_PARTIAL_LINE;
		}

	/* The comment */
	ret = trace_seq_printf(s, "/* %s", trace->buf);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	if (ent->flags & TRACE_FLAG_CONT)
		trace_seq_print_cont(s, iter);

	ret = trace_seq_printf(s, " */\n");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

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
		return print_graph_entry(field, s, iter,
					 iter->cpu);
	}
	case TRACE_GRAPH_RET: {
		struct ftrace_graph_ret_entry *field;
		trace_assign_type(field, entry);
		return print_graph_return(&field->ret, s, entry, iter->cpu);
	}
	case TRACE_PRINT: {
		struct print_entry *field;
		trace_assign_type(field, entry);
		return print_graph_comment(field, s, entry, iter);
	}
	default:
		return TRACE_TYPE_UNHANDLED;
	}
}

static void print_graph_headers(struct seq_file *s)
{
	/* 1st line */
	seq_printf(s, "# ");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_CPU)
		seq_printf(s, "CPU ");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_PROC)
		seq_printf(s, "TASK/PID     ");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_OVERHEAD)
		seq_printf(s, "OVERHEAD/");
	seq_printf(s, "DURATION            FUNCTION CALLS\n");

	/* 2nd line */
	seq_printf(s, "# ");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_CPU)
		seq_printf(s, "|   ");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_PROC)
		seq_printf(s, "|      |     ");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_OVERHEAD) {
		seq_printf(s, "|        ");
		seq_printf(s, "|                   |   |   |   |\n");
	} else
		seq_printf(s, "    |               |   |   |   |\n");
}
static struct tracer graph_trace __read_mostly = {
	.name	     	= "function_graph",
	.init	     	= graph_trace_init,
	.reset	     	= graph_trace_reset,
	.print_line	= print_graph_function,
	.print_header	= print_graph_headers,
	.flags		= &tracer_flags,
};

static __init int init_graph_trace(void)
{
	return register_tracer(&graph_trace);
}

device_initcall(init_graph_trace);
