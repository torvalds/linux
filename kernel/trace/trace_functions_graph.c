/*
 *
 * Function graph tracer.
 * Copyright (c) 2008-2009 Frederic Weisbecker <fweisbec@gmail.com>
 * Mostly borrowed from function tracer which
 * is Copyright (c) Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/fs.h>

#include "trace.h"
#include "trace_output.h"

struct fgraph_data {
	pid_t		last_pid;
	int		depth;
};

#define TRACE_GRAPH_INDENT	2

/* Flag options */
#define TRACE_GRAPH_PRINT_OVERRUN	0x1
#define TRACE_GRAPH_PRINT_CPU		0x2
#define TRACE_GRAPH_PRINT_OVERHEAD	0x4
#define TRACE_GRAPH_PRINT_PROC		0x8
#define TRACE_GRAPH_PRINT_DURATION	0x10
#define TRACE_GRAPH_PRINT_ABS_TIME	0X20

static struct tracer_opt trace_opts[] = {
	/* Display overruns? (for self-debug purpose) */
	{ TRACER_OPT(funcgraph-overrun, TRACE_GRAPH_PRINT_OVERRUN) },
	/* Display CPU ? */
	{ TRACER_OPT(funcgraph-cpu, TRACE_GRAPH_PRINT_CPU) },
	/* Display Overhead ? */
	{ TRACER_OPT(funcgraph-overhead, TRACE_GRAPH_PRINT_OVERHEAD) },
	/* Display proc name/pid */
	{ TRACER_OPT(funcgraph-proc, TRACE_GRAPH_PRINT_PROC) },
	/* Display duration of execution */
	{ TRACER_OPT(funcgraph-duration, TRACE_GRAPH_PRINT_DURATION) },
	/* Display absolute time of an entry */
	{ TRACER_OPT(funcgraph-abstime, TRACE_GRAPH_PRINT_ABS_TIME) },
	{ } /* Empty entry */
};

static struct tracer_flags tracer_flags = {
	/* Don't display overruns and proc by default */
	.val = TRACE_GRAPH_PRINT_CPU | TRACE_GRAPH_PRINT_OVERHEAD |
	       TRACE_GRAPH_PRINT_DURATION,
	.opts = trace_opts
};

/* pid on the last trace processed */


/* Add a function return address to the trace stack on thread info.*/
int
ftrace_push_return_trace(unsigned long ret, unsigned long func, int *depth)
{
	unsigned long long calltime;
	int index;

	if (!current->ret_stack)
		return -EBUSY;

	/* The return trace stack is full */
	if (current->curr_ret_stack == FTRACE_RETFUNC_DEPTH - 1) {
		atomic_inc(&current->trace_overrun);
		return -EBUSY;
	}

	calltime = trace_clock_local();

	index = ++current->curr_ret_stack;
	barrier();
	current->ret_stack[index].ret = ret;
	current->ret_stack[index].func = func;
	current->ret_stack[index].calltime = calltime;
	current->ret_stack[index].subtime = 0;
	*depth = index;

	return 0;
}

/* Retrieve a function return address to the trace stack on thread info.*/
static void
ftrace_pop_return_trace(struct ftrace_graph_ret *trace, unsigned long *ret)
{
	int index;

	index = current->curr_ret_stack;

	if (unlikely(index < 0)) {
		ftrace_graph_stop();
		WARN_ON(1);
		/* Might as well panic, otherwise we have no where to go */
		*ret = (unsigned long)panic;
		return;
	}

	*ret = current->ret_stack[index].ret;
	trace->func = current->ret_stack[index].func;
	trace->calltime = current->ret_stack[index].calltime;
	trace->overrun = atomic_read(&current->trace_overrun);
	trace->depth = index;
}

/*
 * Send the trace to the ring-buffer.
 * @return the original return address.
 */
unsigned long ftrace_return_to_handler(void)
{
	struct ftrace_graph_ret trace;
	unsigned long ret;

	ftrace_pop_return_trace(&trace, &ret);
	trace.rettime = trace_clock_local();
	ftrace_graph_return(&trace);
	barrier();
	current->curr_ret_stack--;

	if (unlikely(!ret)) {
		ftrace_graph_stop();
		WARN_ON(1);
		/* Might as well panic. What else to do? */
		ret = (unsigned long)panic;
	}

	return ret;
}

static int graph_trace_init(struct trace_array *tr)
{
	int ret = register_ftrace_graph(&trace_graph_return,
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
	char comm[TASK_COMM_LEN];
	/* sign + log10(MAX_INT) + '\0' */
	char pid_str[11];
	int spaces = 0;
	int ret;
	int len;
	int i;

	trace_find_cmdline(pid, comm);
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
verif_pid(struct trace_seq *s, pid_t pid, int cpu, struct fgraph_data *data)
{
	pid_t prev_pid;
	pid_t *last_pid;
	int ret;

	if (!data)
		return TRACE_TYPE_HANDLED;

	last_pid = &(per_cpu_ptr(data, cpu)->last_pid);

	if (*last_pid == pid)
		return TRACE_TYPE_HANDLED;

	prev_pid = *last_pid;
	*last_pid = pid;

	if (prev_pid == -1)
		return TRACE_TYPE_HANDLED;
/*
 * Context-switch trace line:

 ------------------------------------------
 | 1)  migration/0--1  =>  sshd-1755
 ------------------------------------------

 */
	ret = trace_seq_printf(s,
		" ------------------------------------------\n");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	ret = print_graph_cpu(s, cpu);
	if (ret == TRACE_TYPE_PARTIAL_LINE)
		return TRACE_TYPE_PARTIAL_LINE;

	ret = print_graph_proc(s, prev_pid);
	if (ret == TRACE_TYPE_PARTIAL_LINE)
		return TRACE_TYPE_PARTIAL_LINE;

	ret = trace_seq_printf(s, " => ");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	ret = print_graph_proc(s, pid);
	if (ret == TRACE_TYPE_PARTIAL_LINE)
		return TRACE_TYPE_PARTIAL_LINE;

	ret = trace_seq_printf(s,
		"\n ------------------------------------------\n\n");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

static struct ftrace_graph_ret_entry *
get_return_for_leaf(struct trace_iterator *iter,
		struct ftrace_graph_ent_entry *curr)
{
	struct ring_buffer_iter *ring_iter;
	struct ring_buffer_event *event;
	struct ftrace_graph_ret_entry *next;

	ring_iter = iter->buffer_iter[iter->cpu];

	/* First peek to compare current entry and the next one */
	if (ring_iter)
		event = ring_buffer_iter_peek(ring_iter, NULL);
	else {
	/* We need to consume the current entry to see the next one */
		ring_buffer_consume(iter->tr->buffer, iter->cpu, NULL);
		event = ring_buffer_peek(iter->tr->buffer, iter->cpu,
					NULL);
	}

	if (!event)
		return NULL;

	next = ring_buffer_event_data(event);

	if (next->ent.type != TRACE_GRAPH_RET)
		return NULL;

	if (curr->ent.pid != next->ent.pid ||
			curr->graph_ent.func != next->ret.func)
		return NULL;

	/* this is a leaf, now advance the iterator */
	if (ring_iter)
		ring_buffer_read(ring_iter, NULL);

	return next;
}

/* Signal a overhead of time execution to the output */
static int
print_graph_overhead(unsigned long long duration, struct trace_seq *s)
{
	/* If duration disappear, we don't need anything */
	if (!(tracer_flags.val & TRACE_GRAPH_PRINT_DURATION))
		return 1;

	/* Non nested entry or return */
	if (duration == -1)
		return trace_seq_printf(s, "  ");

	if (tracer_flags.val & TRACE_GRAPH_PRINT_OVERHEAD) {
		/* Duration exceeded 100 msecs */
		if (duration > 100000ULL)
			return trace_seq_printf(s, "! ");

		/* Duration exceeded 10 msecs */
		if (duration > 10000ULL)
			return trace_seq_printf(s, "+ ");
	}

	return trace_seq_printf(s, "  ");
}

static int print_graph_abs_time(u64 t, struct trace_seq *s)
{
	unsigned long usecs_rem;

	usecs_rem = do_div(t, NSEC_PER_SEC);
	usecs_rem /= 1000;

	return trace_seq_printf(s, "%5lu.%06lu |  ",
			(unsigned long)t, usecs_rem);
}

static enum print_line_t
print_graph_irq(struct trace_iterator *iter, unsigned long addr,
		enum trace_type type, int cpu, pid_t pid)
{
	int ret;
	struct trace_seq *s = &iter->seq;

	if (addr < (unsigned long)__irqentry_text_start ||
		addr >= (unsigned long)__irqentry_text_end)
		return TRACE_TYPE_UNHANDLED;

	/* Absolute time */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_ABS_TIME) {
		ret = print_graph_abs_time(iter->ts, s);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

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
	ret = print_graph_overhead(-1, s);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	if (type == TRACE_GRAPH_ENT)
		ret = trace_seq_printf(s, "==========>");
	else
		ret = trace_seq_printf(s, "<==========");

	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Don't close the duration column if haven't one */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_DURATION)
		trace_seq_printf(s, " |");
	ret = trace_seq_printf(s, "\n");

	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;
	return TRACE_TYPE_HANDLED;
}

enum print_line_t
trace_print_graph_duration(unsigned long long duration, struct trace_seq *s)
{
	unsigned long nsecs_rem = do_div(duration, 1000);
	/* log10(ULONG_MAX) + '\0' */
	char msecs_str[21];
	char nsecs_str[5];
	int ret, len;
	int i;

	sprintf(msecs_str, "%lu", (unsigned long) duration);

	/* Print msecs */
	ret = trace_seq_printf(s, "%s", msecs_str);
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
	return TRACE_TYPE_HANDLED;
}

static enum print_line_t
print_graph_duration(unsigned long long duration, struct trace_seq *s)
{
	int ret;

	ret = trace_print_graph_duration(duration, s);
	if (ret != TRACE_TYPE_HANDLED)
		return ret;

	ret = trace_seq_printf(s, "|  ");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

/* Case of a leaf function on its call entry */
static enum print_line_t
print_graph_entry_leaf(struct trace_iterator *iter,
		struct ftrace_graph_ent_entry *entry,
		struct ftrace_graph_ret_entry *ret_entry, struct trace_seq *s)
{
	struct fgraph_data *data = iter->private;
	struct ftrace_graph_ret *graph_ret;
	struct ftrace_graph_ent *call;
	unsigned long long duration;
	int ret;
	int i;

	graph_ret = &ret_entry->ret;
	call = &entry->graph_ent;
	duration = graph_ret->rettime - graph_ret->calltime;

	if (data) {
		int cpu = iter->cpu;
		int *depth = &(per_cpu_ptr(data, cpu)->depth);

		/*
		 * Comments display at + 1 to depth. Since
		 * this is a leaf function, keep the comments
		 * equal to this depth.
		 */
		*depth = call->depth - 1;
	}

	/* Overhead */
	ret = print_graph_overhead(duration, s);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Duration */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_DURATION) {
		ret = print_graph_duration(duration, s);
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

	ret = trace_seq_printf(s, "();\n");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t
print_graph_entry_nested(struct trace_iterator *iter,
			 struct ftrace_graph_ent_entry *entry,
			 struct trace_seq *s, int cpu)
{
	struct ftrace_graph_ent *call = &entry->graph_ent;
	struct fgraph_data *data = iter->private;
	int ret;
	int i;

	if (data) {
		int cpu = iter->cpu;
		int *depth = &(per_cpu_ptr(data, cpu)->depth);

		*depth = call->depth;
	}

	/* No overhead */
	ret = print_graph_overhead(-1, s);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* No time */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_DURATION) {
		ret = trace_seq_printf(s, "            |  ");
		if (!ret)
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

	/*
	 * we already consumed the current entry to check the next one
	 * and see if this is a leaf.
	 */
	return TRACE_TYPE_NO_CONSUME;
}

static enum print_line_t
print_graph_prologue(struct trace_iterator *iter, struct trace_seq *s,
		     int type, unsigned long addr)
{
	struct fgraph_data *data = iter->private;
	struct trace_entry *ent = iter->ent;
	int cpu = iter->cpu;
	int ret;

	/* Pid */
	if (verif_pid(s, ent->pid, cpu, data) == TRACE_TYPE_PARTIAL_LINE)
		return TRACE_TYPE_PARTIAL_LINE;

	if (type) {
		/* Interrupt */
		ret = print_graph_irq(iter, addr, type, cpu, ent->pid);
		if (ret == TRACE_TYPE_PARTIAL_LINE)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	/* Absolute time */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_ABS_TIME) {
		ret = print_graph_abs_time(iter->ts, s);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

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

	return 0;
}

static enum print_line_t
print_graph_entry(struct ftrace_graph_ent_entry *field, struct trace_seq *s,
			struct trace_iterator *iter)
{
	int cpu = iter->cpu;
	struct ftrace_graph_ent *call = &field->graph_ent;
	struct ftrace_graph_ret_entry *leaf_ret;

	if (print_graph_prologue(iter, s, TRACE_GRAPH_ENT, call->func))
		return TRACE_TYPE_PARTIAL_LINE;

	leaf_ret = get_return_for_leaf(iter, field);
	if (leaf_ret)
		return print_graph_entry_leaf(iter, field, leaf_ret, s);
	else
		return print_graph_entry_nested(iter, field, s, cpu);

}

static enum print_line_t
print_graph_return(struct ftrace_graph_ret *trace, struct trace_seq *s,
		   struct trace_entry *ent, struct trace_iterator *iter)
{
	unsigned long long duration = trace->rettime - trace->calltime;
	struct fgraph_data *data = iter->private;
	pid_t pid = ent->pid;
	int cpu = iter->cpu;
	int ret;
	int i;

	if (data) {
		int cpu = iter->cpu;
		int *depth = &(per_cpu_ptr(data, cpu)->depth);

		/*
		 * Comments display at + 1 to depth. This is the
		 * return from a function, we now want the comments
		 * to display at the same level of the bracket.
		 */
		*depth = trace->depth - 1;
	}

	if (print_graph_prologue(iter, s, 0, 0))
		return TRACE_TYPE_PARTIAL_LINE;

	/* Overhead */
	ret = print_graph_overhead(duration, s);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Duration */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_DURATION) {
		ret = print_graph_duration(duration, s);
		if (ret == TRACE_TYPE_PARTIAL_LINE)
			return TRACE_TYPE_PARTIAL_LINE;
	}

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

	ret = print_graph_irq(iter, trace->func, TRACE_GRAPH_RET, cpu, pid);
	if (ret == TRACE_TYPE_PARTIAL_LINE)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t
print_graph_comment(struct trace_seq *s,  struct trace_entry *ent,
		    struct trace_iterator *iter)
{
	unsigned long sym_flags = (trace_flags & TRACE_ITER_SYM_MASK);
	struct fgraph_data *data = iter->private;
	struct trace_event *event;
	int depth = 0;
	int ret;
	int i;

	if (data)
		depth = per_cpu_ptr(data, iter->cpu)->depth;

	if (print_graph_prologue(iter, s, 0, 0))
		return TRACE_TYPE_PARTIAL_LINE;

	/* No overhead */
	ret = print_graph_overhead(-1, s);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* No time */
	if (tracer_flags.val & TRACE_GRAPH_PRINT_DURATION) {
		ret = trace_seq_printf(s, "            |  ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	/* Indentation */
	if (depth > 0)
		for (i = 0; i < (depth + 1) * TRACE_GRAPH_INDENT; i++) {
			ret = trace_seq_printf(s, " ");
			if (!ret)
				return TRACE_TYPE_PARTIAL_LINE;
		}

	/* The comment */
	ret = trace_seq_printf(s, "/* ");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	switch (iter->ent->type) {
	case TRACE_BPRINT:
		ret = trace_print_bprintk_msg_only(iter);
		if (ret != TRACE_TYPE_HANDLED)
			return ret;
		break;
	case TRACE_PRINT:
		ret = trace_print_printk_msg_only(iter);
		if (ret != TRACE_TYPE_HANDLED)
			return ret;
		break;
	default:
		event = ftrace_find_event(ent->type);
		if (!event)
			return TRACE_TYPE_UNHANDLED;

		ret = event->trace(iter, sym_flags);
		if (ret != TRACE_TYPE_HANDLED)
			return ret;
	}

	/* Strip ending newline */
	if (s->buffer[s->len - 1] == '\n') {
		s->buffer[s->len - 1] = '\0';
		s->len--;
	}

	ret = trace_seq_printf(s, " */\n");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}


enum print_line_t
print_graph_function(struct trace_iterator *iter)
{
	struct trace_entry *entry = iter->ent;
	struct trace_seq *s = &iter->seq;

	switch (entry->type) {
	case TRACE_GRAPH_ENT: {
		struct ftrace_graph_ent_entry *field;
		trace_assign_type(field, entry);
		return print_graph_entry(field, s, iter);
	}
	case TRACE_GRAPH_RET: {
		struct ftrace_graph_ret_entry *field;
		trace_assign_type(field, entry);
		return print_graph_return(&field->ret, s, entry, iter);
	}
	default:
		return print_graph_comment(s, entry, iter);
	}

	return TRACE_TYPE_HANDLED;
}

static void print_graph_headers(struct seq_file *s)
{
	/* 1st line */
	seq_printf(s, "# ");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_ABS_TIME)
		seq_printf(s, "     TIME       ");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_CPU)
		seq_printf(s, "CPU");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_PROC)
		seq_printf(s, "  TASK/PID      ");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_DURATION)
		seq_printf(s, "  DURATION   ");
	seq_printf(s, "               FUNCTION CALLS\n");

	/* 2nd line */
	seq_printf(s, "# ");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_ABS_TIME)
		seq_printf(s, "      |         ");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_CPU)
		seq_printf(s, "|  ");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_PROC)
		seq_printf(s, "  |    |        ");
	if (tracer_flags.val & TRACE_GRAPH_PRINT_DURATION)
		seq_printf(s, "   |   |      ");
	seq_printf(s, "               |   |   |   |\n");
}

static void graph_trace_open(struct trace_iterator *iter)
{
	/* pid and depth on the last trace processed */
	struct fgraph_data *data = alloc_percpu(struct fgraph_data);
	int cpu;

	if (!data)
		pr_warning("function graph tracer: not enough memory\n");
	else
		for_each_possible_cpu(cpu) {
			pid_t *pid = &(per_cpu_ptr(data, cpu)->last_pid);
			int *depth = &(per_cpu_ptr(data, cpu)->depth);
			*pid = -1;
			*depth = 0;
		}

	iter->private = data;
}

static void graph_trace_close(struct trace_iterator *iter)
{
	free_percpu(iter->private);
}

static struct tracer graph_trace __read_mostly = {
	.name		= "function_graph",
	.open		= graph_trace_open,
	.close		= graph_trace_close,
	.wait_pipe	= poll_wait_pipe,
	.init		= graph_trace_init,
	.reset		= graph_trace_reset,
	.print_line	= print_graph_function,
	.print_header	= print_graph_headers,
	.flags		= &tracer_flags,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest	= trace_selftest_startup_function_graph,
#endif
};

static __init int init_graph_trace(void)
{
	return register_tracer(&graph_trace);
}

device_initcall(init_graph_trace);
