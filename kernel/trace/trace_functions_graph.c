// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Function graph tracer.
 * Copyright (c) 2008-2009 Frederic Weisbecker <fweisbec@gmail.com>
 * Mostly borrowed from function tracer which
 * is Copyright (c) Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/fs.h>

#include "trace.h"
#include "trace_output.h"

/* When set, irq functions will be ignored */
static int ftrace_graph_skip_irqs;

struct fgraph_cpu_data {
	pid_t		last_pid;
	int		depth;
	int		depth_irq;
	int		ignore;
	unsigned long	enter_funcs[FTRACE_RETFUNC_DEPTH];
};

struct fgraph_ent_args {
	struct ftrace_graph_ent_entry	ent;
	/* Force the sizeof of args[] to have FTRACE_REGS_MAX_ARGS entries */
	unsigned long			args[FTRACE_REGS_MAX_ARGS];
};

struct fgraph_data {
	struct fgraph_cpu_data __percpu *cpu_data;

	/* Place to preserve last processed entry. */
	union {
		struct fgraph_ent_args		ent;
		/* TODO allow retaddr to have args */
		struct fgraph_retaddr_ent_entry	rent;
	};
	struct ftrace_graph_ret_entry	ret;
	int				failed;
	int				cpu;
};

#define TRACE_GRAPH_INDENT	2

unsigned int fgraph_max_depth;

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
	/* Display interrupts */
	{ TRACER_OPT(funcgraph-irqs, TRACE_GRAPH_PRINT_IRQS) },
	/* Display function name after trailing } */
	{ TRACER_OPT(funcgraph-tail, TRACE_GRAPH_PRINT_TAIL) },
#ifdef CONFIG_FUNCTION_GRAPH_RETVAL
	/* Display function return value ? */
	{ TRACER_OPT(funcgraph-retval, TRACE_GRAPH_PRINT_RETVAL) },
	/* Display function return value in hexadecimal format ? */
	{ TRACER_OPT(funcgraph-retval-hex, TRACE_GRAPH_PRINT_RETVAL_HEX) },
#endif
#ifdef CONFIG_FUNCTION_GRAPH_RETADDR
	/* Display function return address ? */
	{ TRACER_OPT(funcgraph-retaddr, TRACE_GRAPH_PRINT_RETADDR) },
#endif
#ifdef CONFIG_FUNCTION_TRACE_ARGS
	/* Display function arguments ? */
	{ TRACER_OPT(funcgraph-args, TRACE_GRAPH_ARGS) },
#endif
	/* Include sleep time (scheduled out) between entry and return */
	{ TRACER_OPT(sleep-time, TRACE_GRAPH_SLEEP_TIME) },

#ifdef CONFIG_FUNCTION_PROFILER
	/* Include time within nested functions */
	{ TRACER_OPT(graph-time, TRACE_GRAPH_GRAPH_TIME) },
#endif

	{ } /* Empty entry */
};

static struct tracer_flags tracer_flags = {
	/* Don't display overruns, proc, or tail by default */
	.val = TRACE_GRAPH_PRINT_CPU | TRACE_GRAPH_PRINT_OVERHEAD |
	       TRACE_GRAPH_PRINT_DURATION | TRACE_GRAPH_PRINT_IRQS |
	       TRACE_GRAPH_SLEEP_TIME | TRACE_GRAPH_GRAPH_TIME,
	.opts = trace_opts
};

static bool tracer_flags_is_set(u32 flags)
{
	return (tracer_flags.val & flags) == flags;
}

/*
 * DURATION column is being also used to display IRQ signs,
 * following values are used by print_graph_irq and others
 * to fill in space into DURATION column.
 */
enum {
	FLAGS_FILL_FULL  = 1 << TRACE_GRAPH_PRINT_FILL_SHIFT,
	FLAGS_FILL_START = 2 << TRACE_GRAPH_PRINT_FILL_SHIFT,
	FLAGS_FILL_END   = 3 << TRACE_GRAPH_PRINT_FILL_SHIFT,
};

static void
print_graph_duration(struct trace_array *tr, unsigned long long duration,
		     struct trace_seq *s, u32 flags);

static int __graph_entry(struct trace_array *tr, struct ftrace_graph_ent *trace,
			 unsigned int trace_ctx, struct ftrace_regs *fregs)
{
	struct ring_buffer_event *event;
	struct trace_buffer *buffer = tr->array_buffer.buffer;
	struct ftrace_graph_ent_entry *entry;
	int size;

	/* If fregs is defined, add FTRACE_REGS_MAX_ARGS long size words */
	size = sizeof(*entry) + (FTRACE_REGS_MAX_ARGS * !!fregs * sizeof(long));

	event = trace_buffer_lock_reserve(buffer, TRACE_GRAPH_ENT, size, trace_ctx);
	if (!event)
		return 0;

	entry = ring_buffer_event_data(event);
	entry->graph_ent = *trace;

#ifdef CONFIG_HAVE_FUNCTION_ARG_ACCESS_API
	if (fregs) {
		for (int i = 0; i < FTRACE_REGS_MAX_ARGS; i++)
			entry->args[i] = ftrace_regs_get_argument(fregs, i);
	}
#endif

	trace_buffer_unlock_commit_nostack(buffer, event);

	return 1;
}

int __trace_graph_entry(struct trace_array *tr,
				struct ftrace_graph_ent *trace,
				unsigned int trace_ctx)
{
	return __graph_entry(tr, trace, trace_ctx, NULL);
}

#ifdef CONFIG_FUNCTION_GRAPH_RETADDR
int __trace_graph_retaddr_entry(struct trace_array *tr,
				struct ftrace_graph_ent *trace,
				unsigned int trace_ctx,
				unsigned long retaddr)
{
	struct ring_buffer_event *event;
	struct trace_buffer *buffer = tr->array_buffer.buffer;
	struct fgraph_retaddr_ent_entry *entry;

	event = trace_buffer_lock_reserve(buffer, TRACE_GRAPH_RETADDR_ENT,
					  sizeof(*entry), trace_ctx);
	if (!event)
		return 0;
	entry	= ring_buffer_event_data(event);
	entry->graph_ent.func = trace->func;
	entry->graph_ent.depth = trace->depth;
	entry->graph_ent.retaddr = retaddr;
	trace_buffer_unlock_commit_nostack(buffer, event);

	return 1;
}
#else
int __trace_graph_retaddr_entry(struct trace_array *tr,
				struct ftrace_graph_ent *trace,
				unsigned int trace_ctx,
				unsigned long retaddr)
{
	return 1;
}
#endif

static inline int ftrace_graph_ignore_irqs(void)
{
	if (!ftrace_graph_skip_irqs || trace_recursion_test(TRACE_IRQ_BIT))
		return 0;

	return in_hardirq();
}

struct fgraph_times {
	unsigned long long		calltime;
	unsigned long long		sleeptime; /* may be optional! */
};

static int graph_entry(struct ftrace_graph_ent *trace,
		       struct fgraph_ops *gops,
		       struct ftrace_regs *fregs)
{
	unsigned long *task_var = fgraph_get_task_var(gops);
	struct trace_array *tr = gops->private;
	struct fgraph_times *ftimes;
	unsigned int trace_ctx;
	int ret = 0;

	if (*task_var & TRACE_GRAPH_NOTRACE)
		return 0;

	/*
	 * Do not trace a function if it's filtered by set_graph_notrace.
	 * Make the index of ret stack negative to indicate that it should
	 * ignore further functions.  But it needs its own ret stack entry
	 * to recover the original index in order to continue tracing after
	 * returning from the function.
	 */
	if (ftrace_graph_notrace_addr(trace->func)) {
		*task_var |= TRACE_GRAPH_NOTRACE;
		/*
		 * Need to return 1 to have the return called
		 * that will clear the NOTRACE bit.
		 */
		return 1;
	}

	if (!ftrace_trace_task(tr))
		return 0;

	if (ftrace_graph_ignore_func(gops, trace))
		return 0;

	if (ftrace_graph_ignore_irqs())
		return 0;

	if (fgraph_sleep_time) {
		/* Only need to record the calltime */
		ftimes = fgraph_reserve_data(gops->idx, sizeof(ftimes->calltime));
	} else {
		ftimes = fgraph_reserve_data(gops->idx, sizeof(*ftimes));
		if (ftimes)
			ftimes->sleeptime = current->ftrace_sleeptime;
	}
	if (!ftimes)
		return 0;

	ftimes->calltime = trace_clock_local();

	/*
	 * Stop here if tracing_threshold is set. We only write function return
	 * events to the ring buffer.
	 */
	if (tracing_thresh)
		return 1;

	trace_ctx = tracing_gen_ctx();
	if (IS_ENABLED(CONFIG_FUNCTION_GRAPH_RETADDR) &&
	    tracer_flags_is_set(TRACE_GRAPH_PRINT_RETADDR)) {
		unsigned long retaddr = ftrace_graph_top_ret_addr(current);
		ret = __trace_graph_retaddr_entry(tr, trace, trace_ctx, retaddr);
	} else {
		ret = __graph_entry(tr, trace, trace_ctx, fregs);
	}

	return ret;
}

int trace_graph_entry(struct ftrace_graph_ent *trace,
		      struct fgraph_ops *gops,
		      struct ftrace_regs *fregs)
{
	return graph_entry(trace, gops, NULL);
}

static int trace_graph_entry_args(struct ftrace_graph_ent *trace,
				  struct fgraph_ops *gops,
				  struct ftrace_regs *fregs)
{
	return graph_entry(trace, gops, fregs);
}

static void
__trace_graph_function(struct trace_array *tr,
		unsigned long ip, unsigned int trace_ctx)
{
	u64 time = trace_clock_local();
	struct ftrace_graph_ent ent = {
		.func  = ip,
		.depth = 0,
	};
	struct ftrace_graph_ret ret = {
		.func     = ip,
		.depth    = 0,
	};

	__trace_graph_entry(tr, &ent, trace_ctx);
	__trace_graph_return(tr, &ret, trace_ctx, time, time);
}

void
trace_graph_function(struct trace_array *tr,
		unsigned long ip, unsigned long parent_ip,
		unsigned int trace_ctx)
{
	__trace_graph_function(tr, ip, trace_ctx);
}

void __trace_graph_return(struct trace_array *tr,
			  struct ftrace_graph_ret *trace,
			  unsigned int trace_ctx,
			  u64 calltime, u64 rettime)
{
	struct ring_buffer_event *event;
	struct trace_buffer *buffer = tr->array_buffer.buffer;
	struct ftrace_graph_ret_entry *entry;

	event = trace_buffer_lock_reserve(buffer, TRACE_GRAPH_RET,
					  sizeof(*entry), trace_ctx);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->ret				= *trace;
	entry->calltime				= calltime;
	entry->rettime				= rettime;
	trace_buffer_unlock_commit_nostack(buffer, event);
}

static void handle_nosleeptime(struct ftrace_graph_ret *trace,
			       struct fgraph_times *ftimes,
			       int size)
{
	if (fgraph_sleep_time || size < sizeof(*ftimes))
		return;

	ftimes->calltime += current->ftrace_sleeptime - ftimes->sleeptime;
}

void trace_graph_return(struct ftrace_graph_ret *trace,
			struct fgraph_ops *gops, struct ftrace_regs *fregs)
{
	unsigned long *task_var = fgraph_get_task_var(gops);
	struct trace_array *tr = gops->private;
	struct fgraph_times *ftimes;
	unsigned int trace_ctx;
	u64 calltime, rettime;
	int size;

	rettime = trace_clock_local();

	ftrace_graph_addr_finish(gops, trace);

	if (*task_var & TRACE_GRAPH_NOTRACE) {
		*task_var &= ~TRACE_GRAPH_NOTRACE;
		return;
	}

	ftimes = fgraph_retrieve_data(gops->idx, &size);
	if (!ftimes)
		return;

	handle_nosleeptime(trace, ftimes, size);

	calltime = ftimes->calltime;

	trace_ctx = tracing_gen_ctx();
	__trace_graph_return(tr, trace, trace_ctx, calltime, rettime);
}

static void trace_graph_thresh_return(struct ftrace_graph_ret *trace,
				      struct fgraph_ops *gops,
				      struct ftrace_regs *fregs)
{
	struct fgraph_times *ftimes;
	int size;

	ftrace_graph_addr_finish(gops, trace);

	if (trace_recursion_test(TRACE_GRAPH_NOTRACE_BIT)) {
		trace_recursion_clear(TRACE_GRAPH_NOTRACE_BIT);
		return;
	}

	ftimes = fgraph_retrieve_data(gops->idx, &size);
	if (!ftimes)
		return;

	handle_nosleeptime(trace, ftimes, size);

	if (tracing_thresh &&
	    (trace_clock_local() - ftimes->calltime < tracing_thresh))
		return;
	else
		trace_graph_return(trace, gops, fregs);
}

static struct fgraph_ops funcgraph_ops = {
	.entryfunc = &trace_graph_entry,
	.retfunc = &trace_graph_return,
};

int allocate_fgraph_ops(struct trace_array *tr, struct ftrace_ops *ops)
{
	struct fgraph_ops *gops;

	gops = kzalloc(sizeof(*gops), GFP_KERNEL);
	if (!gops)
		return -ENOMEM;

	gops->entryfunc = &trace_graph_entry;
	gops->retfunc = &trace_graph_return;

	tr->gops = gops;
	gops->private = tr;

	fgraph_init_ops(&gops->ops, ops);

	return 0;
}

void free_fgraph_ops(struct trace_array *tr)
{
	kfree(tr->gops);
}

__init void init_array_fgraph_ops(struct trace_array *tr, struct ftrace_ops *ops)
{
	tr->gops = &funcgraph_ops;
	funcgraph_ops.private = tr;
	fgraph_init_ops(&tr->gops->ops, ops);
}

static int graph_trace_init(struct trace_array *tr)
{
	int ret;

	if (tracer_flags_is_set(TRACE_GRAPH_ARGS))
		tr->gops->entryfunc = trace_graph_entry_args;
	else
		tr->gops->entryfunc = trace_graph_entry;

	if (tracing_thresh)
		tr->gops->retfunc = trace_graph_thresh_return;
	else
		tr->gops->retfunc = trace_graph_return;

	/* Make gops functions visible before we start tracing */
	smp_mb();

	ret = register_ftrace_graph(tr->gops);
	if (ret)
		return ret;
	tracing_start_cmdline_record();

	return 0;
}

static struct tracer graph_trace;

static int ftrace_graph_trace_args(struct trace_array *tr, int set)
{
	trace_func_graph_ent_t entry;

	/* Do nothing if the current tracer is not this tracer */
	if (tr->current_trace != &graph_trace)
		return 0;

	if (set)
		entry = trace_graph_entry_args;
	else
		entry = trace_graph_entry;

	/* See if there's any changes */
	if (tr->gops->entryfunc == entry)
		return 0;

	unregister_ftrace_graph(tr->gops);

	tr->gops->entryfunc = entry;

	/* Make gops functions visible before we start tracing */
	smp_mb();
	return register_ftrace_graph(tr->gops);
}

static void graph_trace_reset(struct trace_array *tr)
{
	tracing_stop_cmdline_record();
	unregister_ftrace_graph(tr->gops);
}

static int graph_trace_update_thresh(struct trace_array *tr)
{
	graph_trace_reset(tr);
	return graph_trace_init(tr);
}

static int max_bytes_for_cpu;

static void print_graph_cpu(struct trace_seq *s, int cpu)
{
	/*
	 * Start with a space character - to make it stand out
	 * to the right a bit when trace output is pasted into
	 * email:
	 */
	trace_seq_printf(s, " %*d) ", max_bytes_for_cpu, cpu);
}

#define TRACE_GRAPH_PROCINFO_LENGTH	14

static void print_graph_proc(struct trace_seq *s, pid_t pid)
{
	char comm[TASK_COMM_LEN];
	/* sign + log10(MAX_INT) + '\0' */
	char pid_str[12];
	int spaces = 0;
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
	for (i = 0; i < spaces / 2; i++)
		trace_seq_putc(s, ' ');

	trace_seq_printf(s, "%s-%s", comm, pid_str);

	/* Last spaces to align center */
	for (i = 0; i < spaces - (spaces / 2); i++)
		trace_seq_putc(s, ' ');
}


static void print_graph_lat_fmt(struct trace_seq *s, struct trace_entry *entry)
{
	trace_seq_putc(s, ' ');
	trace_print_lat_fmt(s, entry);
	trace_seq_puts(s, " | ");
}

/* If the pid changed since the last trace, output this event */
static void
verif_pid(struct trace_seq *s, pid_t pid, int cpu, struct fgraph_data *data)
{
	pid_t prev_pid;
	pid_t *last_pid;

	if (!data)
		return;

	last_pid = &(per_cpu_ptr(data->cpu_data, cpu)->last_pid);

	if (*last_pid == pid)
		return;

	prev_pid = *last_pid;
	*last_pid = pid;

	if (prev_pid == -1)
		return;
/*
 * Context-switch trace line:

 ------------------------------------------
 | 1)  migration/0--1  =>  sshd-1755
 ------------------------------------------

 */
	trace_seq_puts(s, " ------------------------------------------\n");
	print_graph_cpu(s, cpu);
	print_graph_proc(s, prev_pid);
	trace_seq_puts(s, " => ");
	print_graph_proc(s, pid);
	trace_seq_puts(s, "\n ------------------------------------------\n\n");
}

static struct ftrace_graph_ret_entry *
get_return_for_leaf(struct trace_iterator *iter,
		struct ftrace_graph_ent_entry *curr)
{
	struct fgraph_data *data = iter->private;
	struct ring_buffer_iter *ring_iter = NULL;
	struct ring_buffer_event *event;
	struct ftrace_graph_ret_entry *next;

	/*
	 * If the previous output failed to write to the seq buffer,
	 * then we just reuse the data from before.
	 */
	if (data && data->failed) {
		curr = &data->ent.ent;
		next = &data->ret;
	} else {

		ring_iter = trace_buffer_iter(iter, iter->cpu);

		/* First peek to compare current entry and the next one */
		if (ring_iter)
			event = ring_buffer_iter_peek(ring_iter, NULL);
		else {
			/*
			 * We need to consume the current entry to see
			 * the next one.
			 */
			ring_buffer_consume(iter->array_buffer->buffer, iter->cpu,
					    NULL, NULL);
			event = ring_buffer_peek(iter->array_buffer->buffer, iter->cpu,
						 NULL, NULL);
		}

		if (!event)
			return NULL;

		next = ring_buffer_event_data(event);

		if (data) {
			/*
			 * Save current and next entries for later reference
			 * if the output fails.
			 */
			if (unlikely(curr->ent.type == TRACE_GRAPH_RETADDR_ENT)) {
				data->rent = *(struct fgraph_retaddr_ent_entry *)curr;
			} else {
				int size = min((int)sizeof(data->ent), (int)iter->ent_size);

				memcpy(&data->ent, curr, size);
			}
			/*
			 * If the next event is not a return type, then
			 * we only care about what type it is. Otherwise we can
			 * safely copy the entire event.
			 */
			if (next->ent.type == TRACE_GRAPH_RET)
				data->ret = *next;
			else
				data->ret.ent.type = next->ent.type;
		}
	}

	if (next->ent.type != TRACE_GRAPH_RET)
		return NULL;

	if (curr->ent.pid != next->ent.pid ||
			curr->graph_ent.func != next->ret.func)
		return NULL;

	/* this is a leaf, now advance the iterator */
	if (ring_iter)
		ring_buffer_iter_advance(ring_iter);

	return next;
}

static void print_graph_abs_time(u64 t, struct trace_seq *s)
{
	unsigned long usecs_rem;

	usecs_rem = do_div(t, NSEC_PER_SEC);
	usecs_rem /= 1000;

	trace_seq_printf(s, "%5lu.%06lu |  ",
			 (unsigned long)t, usecs_rem);
}

static void
print_graph_rel_time(struct trace_iterator *iter, struct trace_seq *s)
{
	unsigned long long usecs;

	usecs = iter->ts - iter->array_buffer->time_start;
	do_div(usecs, NSEC_PER_USEC);

	trace_seq_printf(s, "%9llu us |  ", usecs);
}

static void
print_graph_irq(struct trace_iterator *iter, unsigned long addr,
		enum trace_type type, int cpu, pid_t pid, u32 flags)
{
	struct trace_array *tr = iter->tr;
	struct trace_seq *s = &iter->seq;
	struct trace_entry *ent = iter->ent;

	addr += iter->tr->text_delta;

	if (addr < (unsigned long)__irqentry_text_start ||
		addr >= (unsigned long)__irqentry_text_end)
		return;

	if (tr->trace_flags & TRACE_ITER_CONTEXT_INFO) {
		/* Absolute time */
		if (flags & TRACE_GRAPH_PRINT_ABS_TIME)
			print_graph_abs_time(iter->ts, s);

		/* Relative time */
		if (flags & TRACE_GRAPH_PRINT_REL_TIME)
			print_graph_rel_time(iter, s);

		/* Cpu */
		if (flags & TRACE_GRAPH_PRINT_CPU)
			print_graph_cpu(s, cpu);

		/* Proc */
		if (flags & TRACE_GRAPH_PRINT_PROC) {
			print_graph_proc(s, pid);
			trace_seq_puts(s, " | ");
		}

		/* Latency format */
		if (tr->trace_flags & TRACE_ITER_LATENCY_FMT)
			print_graph_lat_fmt(s, ent);
	}

	/* No overhead */
	print_graph_duration(tr, 0, s, flags | FLAGS_FILL_START);

	if (type == TRACE_GRAPH_ENT)
		trace_seq_puts(s, "==========>");
	else
		trace_seq_puts(s, "<==========");

	print_graph_duration(tr, 0, s, flags | FLAGS_FILL_END);
	trace_seq_putc(s, '\n');
}

void
trace_print_graph_duration(unsigned long long duration, struct trace_seq *s)
{
	unsigned long nsecs_rem = do_div(duration, 1000);
	/* log10(ULONG_MAX) + '\0' */
	char usecs_str[21];
	char nsecs_str[5];
	int len;
	int i;

	sprintf(usecs_str, "%lu", (unsigned long) duration);

	/* Print msecs */
	trace_seq_printf(s, "%s", usecs_str);

	len = strlen(usecs_str);

	/* Print nsecs (we don't want to exceed 7 numbers) */
	if (len < 7) {
		size_t slen = min_t(size_t, sizeof(nsecs_str), 8UL - len);

		snprintf(nsecs_str, slen, "%03lu", nsecs_rem);
		trace_seq_printf(s, ".%s", nsecs_str);
		len += strlen(nsecs_str) + 1;
	}

	trace_seq_puts(s, " us ");

	/* Print remaining spaces to fit the row's width */
	for (i = len; i < 8; i++)
		trace_seq_putc(s, ' ');
}

static void
print_graph_duration(struct trace_array *tr, unsigned long long duration,
		     struct trace_seq *s, u32 flags)
{
	if (!(flags & TRACE_GRAPH_PRINT_DURATION) ||
	    !(tr->trace_flags & TRACE_ITER_CONTEXT_INFO))
		return;

	/* No real adata, just filling the column with spaces */
	switch (flags & TRACE_GRAPH_PRINT_FILL_MASK) {
	case FLAGS_FILL_FULL:
		trace_seq_puts(s, "              |  ");
		return;
	case FLAGS_FILL_START:
		trace_seq_puts(s, "  ");
		return;
	case FLAGS_FILL_END:
		trace_seq_puts(s, " |");
		return;
	}

	/* Signal a overhead of time execution to the output */
	if (flags & TRACE_GRAPH_PRINT_OVERHEAD)
		trace_seq_printf(s, "%c ", trace_find_mark(duration));
	else
		trace_seq_puts(s, "  ");

	trace_print_graph_duration(duration, s);
	trace_seq_puts(s, "|  ");
}

#ifdef CONFIG_FUNCTION_GRAPH_RETVAL
#define __TRACE_GRAPH_PRINT_RETVAL TRACE_GRAPH_PRINT_RETVAL
#else
#define __TRACE_GRAPH_PRINT_RETVAL 0
#endif

#ifdef CONFIG_FUNCTION_GRAPH_RETADDR
#define __TRACE_GRAPH_PRINT_RETADDR TRACE_GRAPH_PRINT_RETADDR
static void print_graph_retaddr(struct trace_seq *s, struct fgraph_retaddr_ent_entry *entry,
				u32 trace_flags, bool comment)
{
	if (comment)
		trace_seq_puts(s, " /*");

	trace_seq_puts(s, " <-");
	seq_print_ip_sym(s, entry->graph_ent.retaddr, trace_flags | TRACE_ITER_SYM_OFFSET);

	if (comment)
		trace_seq_puts(s, " */");
}
#else
#define __TRACE_GRAPH_PRINT_RETADDR 0
#define print_graph_retaddr(_seq, _entry, _tflags, _comment)		do { } while (0)
#endif

#if defined(CONFIG_FUNCTION_GRAPH_RETVAL) || defined(CONFIG_FUNCTION_GRAPH_RETADDR)

static void print_graph_retval(struct trace_seq *s, struct ftrace_graph_ent_entry *entry,
				struct ftrace_graph_ret *graph_ret, void *func,
				u32 opt_flags, u32 trace_flags, int args_size)
{
	unsigned long err_code = 0;
	unsigned long retval = 0;
	bool print_retaddr = false;
	bool print_retval = false;
	bool hex_format = !!(opt_flags & TRACE_GRAPH_PRINT_RETVAL_HEX);

#ifdef CONFIG_FUNCTION_GRAPH_RETVAL
	retval = graph_ret->retval;
	print_retval = !!(opt_flags & TRACE_GRAPH_PRINT_RETVAL);
#endif

#ifdef CONFIG_FUNCTION_GRAPH_RETADDR
	print_retaddr = !!(opt_flags & TRACE_GRAPH_PRINT_RETADDR);
#endif

	if (print_retval && retval && !hex_format) {
		/* Check if the return value matches the negative format */
		if (IS_ENABLED(CONFIG_64BIT) && (retval & BIT(31)) &&
			(((u64)retval) >> 32) == 0) {
			err_code = sign_extend64(retval, 31);
		} else {
			err_code = retval;
		}

		if (!IS_ERR_VALUE(err_code))
			err_code = 0;
	}

	if (entry) {
		if (entry->ent.type != TRACE_GRAPH_RETADDR_ENT)
			print_retaddr = false;

		trace_seq_printf(s, "%ps", func);

		if (args_size >= FTRACE_REGS_MAX_ARGS * sizeof(long)) {
			print_function_args(s, entry->args, (unsigned long)func);
			trace_seq_putc(s, ';');
		} else
			trace_seq_puts(s, "();");

		if (print_retval || print_retaddr)
			trace_seq_puts(s, " /*");
	} else {
		print_retaddr = false;
		trace_seq_printf(s, "} /* %ps", func);
	}

	if (print_retaddr)
		print_graph_retaddr(s, (struct fgraph_retaddr_ent_entry *)entry,
				    trace_flags, false);

	if (print_retval) {
		if (hex_format || (err_code == 0))
			trace_seq_printf(s, " ret=0x%lx", retval);
		else
			trace_seq_printf(s, " ret=%ld", err_code);
	}

	if (!entry || print_retval || print_retaddr)
		trace_seq_puts(s, " */");
}

#else

#define print_graph_retval(_seq, _ent, _ret, _func, _opt_flags, _trace_flags, args_size) \
	do {} while (0)

#endif

/* Case of a leaf function on its call entry */
static enum print_line_t
print_graph_entry_leaf(struct trace_iterator *iter,
		struct ftrace_graph_ent_entry *entry,
		struct ftrace_graph_ret_entry *ret_entry,
		struct trace_seq *s, u32 flags)
{
	struct fgraph_data *data = iter->private;
	struct trace_array *tr = iter->tr;
	struct ftrace_graph_ret *graph_ret;
	struct ftrace_graph_ent *call;
	unsigned long long duration;
	unsigned long ret_func;
	int args_size;
	int cpu = iter->cpu;
	int i;

	args_size = iter->ent_size - offsetof(struct ftrace_graph_ent_entry, args);

	graph_ret = &ret_entry->ret;
	call = &entry->graph_ent;
	duration = ret_entry->rettime - ret_entry->calltime;

	if (data) {
		struct fgraph_cpu_data *cpu_data;

		cpu_data = per_cpu_ptr(data->cpu_data, cpu);

		/*
		 * Comments display at + 1 to depth. Since
		 * this is a leaf function, keep the comments
		 * equal to this depth.
		 */
		cpu_data->depth = call->depth - 1;

		/* No need to keep this function around for this depth */
		if (call->depth < FTRACE_RETFUNC_DEPTH &&
		    !WARN_ON_ONCE(call->depth < 0))
			cpu_data->enter_funcs[call->depth] = 0;
	}

	/* Overhead and duration */
	print_graph_duration(tr, duration, s, flags);

	/* Function */
	for (i = 0; i < call->depth * TRACE_GRAPH_INDENT; i++)
		trace_seq_putc(s, ' ');

	ret_func = graph_ret->func + iter->tr->text_delta;

	/*
	 * Write out the function return value or return address
	 */
	if (flags & (__TRACE_GRAPH_PRINT_RETVAL | __TRACE_GRAPH_PRINT_RETADDR)) {
		print_graph_retval(s, entry, graph_ret,
				   (void *)graph_ret->func + iter->tr->text_delta,
				   flags, tr->trace_flags, args_size);
	} else {
		trace_seq_printf(s, "%ps", (void *)ret_func);

		if (args_size >= FTRACE_REGS_MAX_ARGS * sizeof(long)) {
			print_function_args(s, entry->args, ret_func);
			trace_seq_putc(s, ';');
		} else
			trace_seq_puts(s, "();");
	}
	trace_seq_putc(s, '\n');

	print_graph_irq(iter, graph_ret->func, TRACE_GRAPH_RET,
			cpu, iter->ent->pid, flags);

	return trace_handle_return(s);
}

static enum print_line_t
print_graph_entry_nested(struct trace_iterator *iter,
			 struct ftrace_graph_ent_entry *entry,
			 struct trace_seq *s, int cpu, u32 flags)
{
	struct ftrace_graph_ent *call = &entry->graph_ent;
	struct fgraph_data *data = iter->private;
	struct trace_array *tr = iter->tr;
	unsigned long func;
	int args_size;
	int i;

	if (data) {
		struct fgraph_cpu_data *cpu_data;
		int cpu = iter->cpu;

		cpu_data = per_cpu_ptr(data->cpu_data, cpu);
		cpu_data->depth = call->depth;

		/* Save this function pointer to see if the exit matches */
		if (call->depth < FTRACE_RETFUNC_DEPTH &&
		    !WARN_ON_ONCE(call->depth < 0))
			cpu_data->enter_funcs[call->depth] = call->func;
	}

	/* No time */
	print_graph_duration(tr, 0, s, flags | FLAGS_FILL_FULL);

	/* Function */
	for (i = 0; i < call->depth * TRACE_GRAPH_INDENT; i++)
		trace_seq_putc(s, ' ');

	func = call->func + iter->tr->text_delta;

	trace_seq_printf(s, "%ps", (void *)func);

	args_size = iter->ent_size - offsetof(struct ftrace_graph_ent_entry, args);

	if (args_size >= FTRACE_REGS_MAX_ARGS * sizeof(long))
		print_function_args(s, entry->args, func);
	else
		trace_seq_puts(s, "()");

	trace_seq_puts(s, " {");

	if (flags & __TRACE_GRAPH_PRINT_RETADDR  &&
		entry->ent.type == TRACE_GRAPH_RETADDR_ENT)
		print_graph_retaddr(s, (struct fgraph_retaddr_ent_entry *)entry,
			tr->trace_flags, true);
	trace_seq_putc(s, '\n');

	if (trace_seq_has_overflowed(s))
		return TRACE_TYPE_PARTIAL_LINE;

	/*
	 * we already consumed the current entry to check the next one
	 * and see if this is a leaf.
	 */
	return TRACE_TYPE_NO_CONSUME;
}

static void
print_graph_prologue(struct trace_iterator *iter, struct trace_seq *s,
		     int type, unsigned long addr, u32 flags)
{
	struct fgraph_data *data = iter->private;
	struct trace_entry *ent = iter->ent;
	struct trace_array *tr = iter->tr;
	int cpu = iter->cpu;

	/* Pid */
	verif_pid(s, ent->pid, cpu, data);

	if (type)
		/* Interrupt */
		print_graph_irq(iter, addr, type, cpu, ent->pid, flags);

	if (!(tr->trace_flags & TRACE_ITER_CONTEXT_INFO))
		return;

	/* Absolute time */
	if (flags & TRACE_GRAPH_PRINT_ABS_TIME)
		print_graph_abs_time(iter->ts, s);

	/* Relative time */
	if (flags & TRACE_GRAPH_PRINT_REL_TIME)
		print_graph_rel_time(iter, s);

	/* Cpu */
	if (flags & TRACE_GRAPH_PRINT_CPU)
		print_graph_cpu(s, cpu);

	/* Proc */
	if (flags & TRACE_GRAPH_PRINT_PROC) {
		print_graph_proc(s, ent->pid);
		trace_seq_puts(s, " | ");
	}

	/* Latency format */
	if (tr->trace_flags & TRACE_ITER_LATENCY_FMT)
		print_graph_lat_fmt(s, ent);

	return;
}

/*
 * Entry check for irq code
 *
 * returns 1 if
 *  - we are inside irq code
 *  - we just entered irq code
 *
 * returns 0 if
 *  - funcgraph-interrupts option is set
 *  - we are not inside irq code
 */
static int
check_irq_entry(struct trace_iterator *iter, u32 flags,
		unsigned long addr, int depth)
{
	int cpu = iter->cpu;
	int *depth_irq;
	struct fgraph_data *data = iter->private;

	addr += iter->tr->text_delta;

	/*
	 * If we are either displaying irqs, or we got called as
	 * a graph event and private data does not exist,
	 * then we bypass the irq check.
	 */
	if ((flags & TRACE_GRAPH_PRINT_IRQS) ||
	    (!data))
		return 0;

	depth_irq = &(per_cpu_ptr(data->cpu_data, cpu)->depth_irq);

	/*
	 * We are inside the irq code
	 */
	if (*depth_irq >= 0)
		return 1;

	if ((addr < (unsigned long)__irqentry_text_start) ||
	    (addr >= (unsigned long)__irqentry_text_end))
		return 0;

	/*
	 * We are entering irq code.
	 */
	*depth_irq = depth;
	return 1;
}

/*
 * Return check for irq code
 *
 * returns 1 if
 *  - we are inside irq code
 *  - we just left irq code
 *
 * returns 0 if
 *  - funcgraph-interrupts option is set
 *  - we are not inside irq code
 */
static int
check_irq_return(struct trace_iterator *iter, u32 flags, int depth)
{
	int cpu = iter->cpu;
	int *depth_irq;
	struct fgraph_data *data = iter->private;

	/*
	 * If we are either displaying irqs, or we got called as
	 * a graph event and private data does not exist,
	 * then we bypass the irq check.
	 */
	if ((flags & TRACE_GRAPH_PRINT_IRQS) ||
	    (!data))
		return 0;

	depth_irq = &(per_cpu_ptr(data->cpu_data, cpu)->depth_irq);

	/*
	 * We are not inside the irq code.
	 */
	if (*depth_irq == -1)
		return 0;

	/*
	 * We are inside the irq code, and this is returning entry.
	 * Let's not trace it and clear the entry depth, since
	 * we are out of irq code.
	 *
	 * This condition ensures that we 'leave the irq code' once
	 * we are out of the entry depth. Thus protecting us from
	 * the RETURN entry loss.
	 */
	if (*depth_irq >= depth) {
		*depth_irq = -1;
		return 1;
	}

	/*
	 * We are inside the irq code, and this is not the entry.
	 */
	return 1;
}

static enum print_line_t
print_graph_entry(struct ftrace_graph_ent_entry *field, struct trace_seq *s,
			struct trace_iterator *iter, u32 flags)
{
	struct fgraph_data *data = iter->private;
	struct ftrace_graph_ent *call;
	struct ftrace_graph_ret_entry *leaf_ret;
	static enum print_line_t ret;
	int cpu = iter->cpu;
	/*
	 * print_graph_entry() may consume the current event,
	 * thus @field may become invalid, so we need to save it.
	 * sizeof(struct ftrace_graph_ent_entry) is very small,
	 * it can be safely saved at the stack.
	 */
	struct ftrace_graph_ent_entry *entry;
	u8 save_buf[sizeof(*entry) + FTRACE_REGS_MAX_ARGS * sizeof(long)];

	/* The ent_size is expected to be as big as the entry */
	if (iter->ent_size > sizeof(save_buf))
		iter->ent_size = sizeof(save_buf);

	entry = (void *)save_buf;
	memcpy(entry, field, iter->ent_size);

	call = &entry->graph_ent;

	if (check_irq_entry(iter, flags, call->func, call->depth))
		return TRACE_TYPE_HANDLED;

	print_graph_prologue(iter, s, TRACE_GRAPH_ENT, call->func, flags);

	leaf_ret = get_return_for_leaf(iter, entry);
	if (leaf_ret)
		ret = print_graph_entry_leaf(iter, entry, leaf_ret, s, flags);
	else
		ret = print_graph_entry_nested(iter, entry, s, cpu, flags);

	if (data) {
		/*
		 * If we failed to write our output, then we need to make
		 * note of it. Because we already consumed our entry.
		 */
		if (s->full) {
			data->failed = 1;
			data->cpu = cpu;
		} else
			data->failed = 0;
	}

	return ret;
}

static enum print_line_t
print_graph_return(struct ftrace_graph_ret_entry *retentry, struct trace_seq *s,
		   struct trace_entry *ent, struct trace_iterator *iter,
		   u32 flags)
{
	struct ftrace_graph_ret *trace = &retentry->ret;
	u64 calltime = retentry->calltime;
	u64 rettime = retentry->rettime;
	unsigned long long duration = rettime - calltime;
	struct fgraph_data *data = iter->private;
	struct trace_array *tr = iter->tr;
	unsigned long func;
	pid_t pid = ent->pid;
	int cpu = iter->cpu;
	int func_match = 1;
	int i;

	func = trace->func + iter->tr->text_delta;

	if (check_irq_return(iter, flags, trace->depth))
		return TRACE_TYPE_HANDLED;

	if (data) {
		struct fgraph_cpu_data *cpu_data;
		int cpu = iter->cpu;

		cpu_data = per_cpu_ptr(data->cpu_data, cpu);

		/*
		 * Comments display at + 1 to depth. This is the
		 * return from a function, we now want the comments
		 * to display at the same level of the bracket.
		 */
		cpu_data->depth = trace->depth - 1;

		if (trace->depth < FTRACE_RETFUNC_DEPTH &&
		    !WARN_ON_ONCE(trace->depth < 0)) {
			if (cpu_data->enter_funcs[trace->depth] != trace->func)
				func_match = 0;
			cpu_data->enter_funcs[trace->depth] = 0;
		}
	}

	print_graph_prologue(iter, s, 0, 0, flags);

	/* Overhead and duration */
	print_graph_duration(tr, duration, s, flags);

	/* Closing brace */
	for (i = 0; i < trace->depth * TRACE_GRAPH_INDENT; i++)
		trace_seq_putc(s, ' ');

	/*
	 * Always write out the function name and its return value if the
	 * funcgraph-retval option is enabled.
	 */
	if (flags & __TRACE_GRAPH_PRINT_RETVAL) {
		print_graph_retval(s, NULL, trace, (void *)func, flags,
				   tr->trace_flags, 0);
	} else {
		/*
		 * If the return function does not have a matching entry,
		 * then the entry was lost. Instead of just printing
		 * the '}' and letting the user guess what function this
		 * belongs to, write out the function name. Always do
		 * that if the funcgraph-tail option is enabled.
		 */
		if (func_match && !(flags & TRACE_GRAPH_PRINT_TAIL))
			trace_seq_puts(s, "}");
		else
			trace_seq_printf(s, "} /* %ps */", (void *)func);
	}
	trace_seq_putc(s, '\n');

	/* Overrun */
	if (flags & TRACE_GRAPH_PRINT_OVERRUN)
		trace_seq_printf(s, " (Overruns: %u)\n",
				 trace->overrun);

	print_graph_irq(iter, trace->func, TRACE_GRAPH_RET,
			cpu, pid, flags);

	return trace_handle_return(s);
}

static enum print_line_t
print_graph_comment(struct trace_seq *s, struct trace_entry *ent,
		    struct trace_iterator *iter, u32 flags)
{
	struct trace_array *tr = iter->tr;
	unsigned long sym_flags = (tr->trace_flags & TRACE_ITER_SYM_MASK);
	struct fgraph_data *data = iter->private;
	struct trace_event *event;
	int depth = 0;
	int ret;
	int i;

	if (data)
		depth = per_cpu_ptr(data->cpu_data, iter->cpu)->depth;

	print_graph_prologue(iter, s, 0, 0, flags);

	/* No time */
	print_graph_duration(tr, 0, s, flags | FLAGS_FILL_FULL);

	/* Indentation */
	if (depth > 0)
		for (i = 0; i < (depth + 1) * TRACE_GRAPH_INDENT; i++)
			trace_seq_putc(s, ' ');

	/* The comment */
	trace_seq_puts(s, "/* ");

	switch (iter->ent->type) {
	case TRACE_BPUTS:
		ret = trace_print_bputs_msg_only(iter);
		if (ret != TRACE_TYPE_HANDLED)
			return ret;
		break;
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

		ret = event->funcs->trace(iter, sym_flags, event);
		if (ret != TRACE_TYPE_HANDLED)
			return ret;
	}

	if (trace_seq_has_overflowed(s))
		goto out;

	/* Strip ending newline */
	if (s->buffer[s->seq.len - 1] == '\n') {
		s->buffer[s->seq.len - 1] = '\0';
		s->seq.len--;
	}

	trace_seq_puts(s, " */\n");
 out:
	return trace_handle_return(s);
}


enum print_line_t
print_graph_function_flags(struct trace_iterator *iter, u32 flags)
{
	struct ftrace_graph_ent_entry *field;
	struct fgraph_data *data = iter->private;
	struct trace_entry *entry = iter->ent;
	struct trace_seq *s = &iter->seq;
	int cpu = iter->cpu;
	int ret;

	if (data && per_cpu_ptr(data->cpu_data, cpu)->ignore) {
		per_cpu_ptr(data->cpu_data, cpu)->ignore = 0;
		return TRACE_TYPE_HANDLED;
	}

	/*
	 * If the last output failed, there's a possibility we need
	 * to print out the missing entry which would never go out.
	 */
	if (data && data->failed) {
		field = &data->ent.ent;
		iter->cpu = data->cpu;
		ret = print_graph_entry(field, s, iter, flags);
		if (ret == TRACE_TYPE_HANDLED && iter->cpu != cpu) {
			per_cpu_ptr(data->cpu_data, iter->cpu)->ignore = 1;
			ret = TRACE_TYPE_NO_CONSUME;
		}
		iter->cpu = cpu;
		return ret;
	}

	switch (entry->type) {
	case TRACE_GRAPH_ENT: {
		trace_assign_type(field, entry);
		return print_graph_entry(field, s, iter, flags);
	}
#ifdef CONFIG_FUNCTION_GRAPH_RETADDR
	case TRACE_GRAPH_RETADDR_ENT: {
		struct fgraph_retaddr_ent_entry saved;
		struct fgraph_retaddr_ent_entry *rfield;

		trace_assign_type(rfield, entry);
		saved = *rfield;
		return print_graph_entry((struct ftrace_graph_ent_entry *)&saved, s, iter, flags);
	}
#endif
	case TRACE_GRAPH_RET: {
		struct ftrace_graph_ret_entry *field;
		trace_assign_type(field, entry);
		return print_graph_return(field, s, entry, iter, flags);
	}
	case TRACE_STACK:
	case TRACE_FN:
		/* dont trace stack and functions as comments */
		return TRACE_TYPE_UNHANDLED;

	default:
		return print_graph_comment(s, entry, iter, flags);
	}

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t
print_graph_function(struct trace_iterator *iter)
{
	return print_graph_function_flags(iter, tracer_flags.val);
}

static enum print_line_t
print_graph_function_event(struct trace_iterator *iter, int flags,
			   struct trace_event *event)
{
	return print_graph_function(iter);
}

static void print_lat_header(struct seq_file *s, u32 flags)
{
	static const char spaces[] = "                "	/* 16 spaces */
		"    "					/* 4 spaces */
		"                 ";			/* 17 spaces */
	int size = 0;

	if (flags & TRACE_GRAPH_PRINT_ABS_TIME)
		size += 16;
	if (flags & TRACE_GRAPH_PRINT_REL_TIME)
		size += 16;
	if (flags & TRACE_GRAPH_PRINT_CPU)
		size += 4;
	if (flags & TRACE_GRAPH_PRINT_PROC)
		size += 17;

	seq_printf(s, "#%.*s  _-----=> irqs-off        \n", size, spaces);
	seq_printf(s, "#%.*s / _----=> need-resched    \n", size, spaces);
	seq_printf(s, "#%.*s| / _---=> hardirq/softirq \n", size, spaces);
	seq_printf(s, "#%.*s|| / _--=> preempt-depth   \n", size, spaces);
	seq_printf(s, "#%.*s||| /                      \n", size, spaces);
}

static void __print_graph_headers_flags(struct trace_array *tr,
					struct seq_file *s, u32 flags)
{
	int lat = tr->trace_flags & TRACE_ITER_LATENCY_FMT;

	if (lat)
		print_lat_header(s, flags);

	/* 1st line */
	seq_putc(s, '#');
	if (flags & TRACE_GRAPH_PRINT_ABS_TIME)
		seq_puts(s, "     TIME       ");
	if (flags & TRACE_GRAPH_PRINT_REL_TIME)
		seq_puts(s, "   REL TIME     ");
	if (flags & TRACE_GRAPH_PRINT_CPU)
		seq_puts(s, " CPU");
	if (flags & TRACE_GRAPH_PRINT_PROC)
		seq_puts(s, "  TASK/PID       ");
	if (lat)
		seq_puts(s, "||||   ");
	if (flags & TRACE_GRAPH_PRINT_DURATION)
		seq_puts(s, "  DURATION   ");
	seq_puts(s, "               FUNCTION CALLS\n");

	/* 2nd line */
	seq_putc(s, '#');
	if (flags & TRACE_GRAPH_PRINT_ABS_TIME)
		seq_puts(s, "      |         ");
	if (flags & TRACE_GRAPH_PRINT_REL_TIME)
		seq_puts(s, "      |         ");
	if (flags & TRACE_GRAPH_PRINT_CPU)
		seq_puts(s, " |  ");
	if (flags & TRACE_GRAPH_PRINT_PROC)
		seq_puts(s, "   |    |        ");
	if (lat)
		seq_puts(s, "||||   ");
	if (flags & TRACE_GRAPH_PRINT_DURATION)
		seq_puts(s, "   |   |      ");
	seq_puts(s, "               |   |   |   |\n");
}

static void print_graph_headers(struct seq_file *s)
{
	print_graph_headers_flags(s, tracer_flags.val);
}

void print_graph_headers_flags(struct seq_file *s, u32 flags)
{
	struct trace_iterator *iter = s->private;
	struct trace_array *tr = iter->tr;

	if (!(tr->trace_flags & TRACE_ITER_CONTEXT_INFO))
		return;

	if (tr->trace_flags & TRACE_ITER_LATENCY_FMT) {
		/* print nothing if the buffers are empty */
		if (trace_empty(iter))
			return;

		print_trace_header(s, iter);
	}

	__print_graph_headers_flags(tr, s, flags);
}

void graph_trace_open(struct trace_iterator *iter)
{
	/* pid and depth on the last trace processed */
	struct fgraph_data *data;
	gfp_t gfpflags;
	int cpu;

	iter->private = NULL;

	/* We can be called in atomic context via ftrace_dump() */
	gfpflags = (in_atomic() || irqs_disabled()) ? GFP_ATOMIC : GFP_KERNEL;

	data = kzalloc(sizeof(*data), gfpflags);
	if (!data)
		goto out_err;

	data->cpu_data = alloc_percpu_gfp(struct fgraph_cpu_data, gfpflags);
	if (!data->cpu_data)
		goto out_err_free;

	for_each_possible_cpu(cpu) {
		pid_t *pid = &(per_cpu_ptr(data->cpu_data, cpu)->last_pid);
		int *depth = &(per_cpu_ptr(data->cpu_data, cpu)->depth);
		int *ignore = &(per_cpu_ptr(data->cpu_data, cpu)->ignore);
		int *depth_irq = &(per_cpu_ptr(data->cpu_data, cpu)->depth_irq);

		*pid = -1;
		*depth = 0;
		*ignore = 0;
		*depth_irq = -1;
	}

	iter->private = data;

	return;

 out_err_free:
	kfree(data);
 out_err:
	pr_warn("function graph tracer: not enough memory\n");
}

void graph_trace_close(struct trace_iterator *iter)
{
	struct fgraph_data *data = iter->private;

	if (data) {
		free_percpu(data->cpu_data);
		kfree(data);
		iter->private = NULL;
	}
}

static int
func_graph_set_flag(struct trace_array *tr, u32 old_flags, u32 bit, int set)
{
	if (bit == TRACE_GRAPH_PRINT_IRQS)
		ftrace_graph_skip_irqs = !set;

	if (bit == TRACE_GRAPH_SLEEP_TIME)
		ftrace_graph_sleep_time_control(set);

	if (bit == TRACE_GRAPH_GRAPH_TIME)
		ftrace_graph_graph_time_control(set);

	if (bit == TRACE_GRAPH_ARGS)
		return ftrace_graph_trace_args(tr, set);

	return 0;
}

static struct trace_event_functions graph_functions = {
	.trace		= print_graph_function_event,
};

static struct trace_event graph_trace_entry_event = {
	.type		= TRACE_GRAPH_ENT,
	.funcs		= &graph_functions,
};

#ifdef CONFIG_FUNCTION_GRAPH_RETADDR
static struct trace_event graph_trace_retaddr_entry_event = {
	.type		= TRACE_GRAPH_RETADDR_ENT,
	.funcs		= &graph_functions,
};
#endif

static struct trace_event graph_trace_ret_event = {
	.type		= TRACE_GRAPH_RET,
	.funcs		= &graph_functions
};

static struct tracer graph_trace __tracer_data = {
	.name		= "function_graph",
	.update_thresh	= graph_trace_update_thresh,
	.open		= graph_trace_open,
	.pipe_open	= graph_trace_open,
	.close		= graph_trace_close,
	.pipe_close	= graph_trace_close,
	.init		= graph_trace_init,
	.reset		= graph_trace_reset,
	.print_line	= print_graph_function,
	.print_header	= print_graph_headers,
	.flags		= &tracer_flags,
	.set_flag	= func_graph_set_flag,
	.allow_instances = true,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest	= trace_selftest_startup_function_graph,
#endif
};


static ssize_t
graph_depth_write(struct file *filp, const char __user *ubuf, size_t cnt,
		  loff_t *ppos)
{
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	fgraph_max_depth = val;

	*ppos += cnt;

	return cnt;
}

static ssize_t
graph_depth_read(struct file *filp, char __user *ubuf, size_t cnt,
		 loff_t *ppos)
{
	char buf[15]; /* More than enough to hold UINT_MAX + "\n"*/
	int n;

	n = sprintf(buf, "%d\n", fgraph_max_depth);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, n);
}

static const struct file_operations graph_depth_fops = {
	.open		= tracing_open_generic,
	.write		= graph_depth_write,
	.read		= graph_depth_read,
	.llseek		= generic_file_llseek,
};

static __init int init_graph_tracefs(void)
{
	int ret;

	ret = tracing_init_dentry();
	if (ret)
		return 0;

	trace_create_file("max_graph_depth", TRACE_MODE_WRITE, NULL,
			  NULL, &graph_depth_fops);

	return 0;
}
fs_initcall(init_graph_tracefs);

static __init int init_graph_trace(void)
{
	max_bytes_for_cpu = snprintf(NULL, 0, "%u", nr_cpu_ids - 1);

	if (!register_trace_event(&graph_trace_entry_event)) {
		pr_warn("Warning: could not register graph trace events\n");
		return 1;
	}

#ifdef CONFIG_FUNCTION_GRAPH_RETADDR
	if (!register_trace_event(&graph_trace_retaddr_entry_event)) {
		pr_warn("Warning: could not register graph trace retaddr events\n");
		return 1;
	}
#endif

	if (!register_trace_event(&graph_trace_ret_event)) {
		pr_warn("Warning: could not register graph trace events\n");
		return 1;
	}

	return register_tracer(&graph_trace);
}

core_initcall(init_graph_trace);
