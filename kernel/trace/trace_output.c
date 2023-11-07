// SPDX-License-Identifier: GPL-2.0
/*
 * trace_output.c
 *
 * Copyright (C) 2008 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/ftrace.h>
#include <linux/sched/clock.h>
#include <linux/sched/mm.h>

#include "trace_output.h"

/* must be a power of 2 */
#define EVENT_HASHSIZE	128

DECLARE_RWSEM(trace_event_sem);

static struct hlist_head event_hash[EVENT_HASHSIZE] __read_mostly;

static int next_event_type = __TRACE_LAST_TYPE;

enum print_line_t trace_print_bputs_msg_only(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry = iter->ent;
	struct bputs_entry *field;

	trace_assign_type(field, entry);

	trace_seq_puts(s, field->str);

	return trace_handle_return(s);
}

enum print_line_t trace_print_bprintk_msg_only(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry = iter->ent;
	struct bprint_entry *field;

	trace_assign_type(field, entry);

	trace_seq_bprintf(s, field->fmt, field->buf);

	return trace_handle_return(s);
}

enum print_line_t trace_print_printk_msg_only(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry = iter->ent;
	struct print_entry *field;

	trace_assign_type(field, entry);

	trace_seq_puts(s, field->buf);

	return trace_handle_return(s);
}

const char *
trace_print_flags_seq(struct trace_seq *p, const char *delim,
		      unsigned long flags,
		      const struct trace_print_flags *flag_array)
{
	unsigned long mask;
	const char *str;
	const char *ret = trace_seq_buffer_ptr(p);
	int i, first = 1;

	for (i = 0;  flag_array[i].name && flags; i++) {

		mask = flag_array[i].mask;
		if ((flags & mask) != mask)
			continue;

		str = flag_array[i].name;
		flags &= ~mask;
		if (!first && delim)
			trace_seq_puts(p, delim);
		else
			first = 0;
		trace_seq_puts(p, str);
	}

	/* check for left over flags */
	if (flags) {
		if (!first && delim)
			trace_seq_puts(p, delim);
		trace_seq_printf(p, "0x%lx", flags);
	}

	trace_seq_putc(p, 0);

	return ret;
}
EXPORT_SYMBOL(trace_print_flags_seq);

const char *
trace_print_symbols_seq(struct trace_seq *p, unsigned long val,
			const struct trace_print_flags *symbol_array)
{
	int i;
	const char *ret = trace_seq_buffer_ptr(p);

	for (i = 0;  symbol_array[i].name; i++) {

		if (val != symbol_array[i].mask)
			continue;

		trace_seq_puts(p, symbol_array[i].name);
		break;
	}

	if (ret == (const char *)(trace_seq_buffer_ptr(p)))
		trace_seq_printf(p, "0x%lx", val);

	trace_seq_putc(p, 0);

	return ret;
}
EXPORT_SYMBOL(trace_print_symbols_seq);

#if BITS_PER_LONG == 32
const char *
trace_print_flags_seq_u64(struct trace_seq *p, const char *delim,
		      unsigned long long flags,
		      const struct trace_print_flags_u64 *flag_array)
{
	unsigned long long mask;
	const char *str;
	const char *ret = trace_seq_buffer_ptr(p);
	int i, first = 1;

	for (i = 0;  flag_array[i].name && flags; i++) {

		mask = flag_array[i].mask;
		if ((flags & mask) != mask)
			continue;

		str = flag_array[i].name;
		flags &= ~mask;
		if (!first && delim)
			trace_seq_puts(p, delim);
		else
			first = 0;
		trace_seq_puts(p, str);
	}

	/* check for left over flags */
	if (flags) {
		if (!first && delim)
			trace_seq_puts(p, delim);
		trace_seq_printf(p, "0x%llx", flags);
	}

	trace_seq_putc(p, 0);

	return ret;
}
EXPORT_SYMBOL(trace_print_flags_seq_u64);

const char *
trace_print_symbols_seq_u64(struct trace_seq *p, unsigned long long val,
			 const struct trace_print_flags_u64 *symbol_array)
{
	int i;
	const char *ret = trace_seq_buffer_ptr(p);

	for (i = 0;  symbol_array[i].name; i++) {

		if (val != symbol_array[i].mask)
			continue;

		trace_seq_puts(p, symbol_array[i].name);
		break;
	}

	if (ret == (const char *)(trace_seq_buffer_ptr(p)))
		trace_seq_printf(p, "0x%llx", val);

	trace_seq_putc(p, 0);

	return ret;
}
EXPORT_SYMBOL(trace_print_symbols_seq_u64);
#endif

const char *
trace_print_bitmask_seq(struct trace_seq *p, void *bitmask_ptr,
			unsigned int bitmask_size)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_bitmask(p, bitmask_ptr, bitmask_size * 8);
	trace_seq_putc(p, 0);

	return ret;
}
EXPORT_SYMBOL_GPL(trace_print_bitmask_seq);

/**
 * trace_print_hex_seq - print buffer as hex sequence
 * @p: trace seq struct to write to
 * @buf: The buffer to print
 * @buf_len: Length of @buf in bytes
 * @concatenate: Print @buf as single hex string or with spacing
 *
 * Prints the passed buffer as a hex sequence either as a whole,
 * single hex string if @concatenate is true or with spacing after
 * each byte in case @concatenate is false.
 */
const char *
trace_print_hex_seq(struct trace_seq *p, const unsigned char *buf, int buf_len,
		    bool concatenate)
{
	int i;
	const char *ret = trace_seq_buffer_ptr(p);
	const char *fmt = concatenate ? "%*phN" : "%*ph";

	for (i = 0; i < buf_len; i += 16)
		trace_seq_printf(p, fmt, min(buf_len - i, 16), &buf[i]);
	trace_seq_putc(p, 0);

	return ret;
}
EXPORT_SYMBOL(trace_print_hex_seq);

const char *
trace_print_array_seq(struct trace_seq *p, const void *buf, int count,
		      size_t el_size)
{
	const char *ret = trace_seq_buffer_ptr(p);
	const char *prefix = "";
	void *ptr = (void *)buf;
	size_t buf_len = count * el_size;

	trace_seq_putc(p, '{');

	while (ptr < buf + buf_len) {
		switch (el_size) {
		case 1:
			trace_seq_printf(p, "%s0x%x", prefix,
					 *(u8 *)ptr);
			break;
		case 2:
			trace_seq_printf(p, "%s0x%x", prefix,
					 *(u16 *)ptr);
			break;
		case 4:
			trace_seq_printf(p, "%s0x%x", prefix,
					 *(u32 *)ptr);
			break;
		case 8:
			trace_seq_printf(p, "%s0x%llx", prefix,
					 *(u64 *)ptr);
			break;
		default:
			trace_seq_printf(p, "BAD SIZE:%zu 0x%x", el_size,
					 *(u8 *)ptr);
			el_size = 1;
		}
		prefix = ",";
		ptr += el_size;
	}

	trace_seq_putc(p, '}');
	trace_seq_putc(p, 0);

	return ret;
}
EXPORT_SYMBOL(trace_print_array_seq);

const char *
trace_print_hex_dump_seq(struct trace_seq *p, const char *prefix_str,
			 int prefix_type, int rowsize, int groupsize,
			 const void *buf, size_t len, bool ascii)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_putc(p, '\n');
	trace_seq_hex_dump(p, prefix_str, prefix_type,
			   rowsize, groupsize, buf, len, ascii);
	trace_seq_putc(p, 0);
	return ret;
}
EXPORT_SYMBOL(trace_print_hex_dump_seq);

int trace_raw_output_prep(struct trace_iterator *iter,
			  struct trace_event *trace_event)
{
	struct trace_event_call *event;
	struct trace_seq *s = &iter->seq;
	struct trace_seq *p = &iter->tmp_seq;
	struct trace_entry *entry;

	event = container_of(trace_event, struct trace_event_call, event);
	entry = iter->ent;

	if (entry->type != event->event.type) {
		WARN_ON_ONCE(1);
		return TRACE_TYPE_UNHANDLED;
	}

	trace_seq_init(p);
	trace_seq_printf(s, "%s: ", trace_event_name(event));

	return trace_handle_return(s);
}
EXPORT_SYMBOL(trace_raw_output_prep);

static int trace_output_raw(struct trace_iterator *iter, char *name,
			    char *fmt, va_list ap)
{
	struct trace_seq *s = &iter->seq;

	trace_seq_printf(s, "%s: ", name);
	trace_seq_vprintf(s, fmt, ap);

	return trace_handle_return(s);
}

int trace_output_call(struct trace_iterator *iter, char *name, char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = trace_output_raw(iter, name, fmt, ap);
	va_end(ap);

	return ret;
}
EXPORT_SYMBOL_GPL(trace_output_call);

#ifdef CONFIG_KRETPROBES
static inline const char *kretprobed(const char *name)
{
	static const char tramp_name[] = "kretprobe_trampoline";
	int size = sizeof(tramp_name);

	if (strncmp(tramp_name, name, size) == 0)
		return "[unknown/kretprobe'd]";
	return name;
}
#else
static inline const char *kretprobed(const char *name)
{
	return name;
}
#endif /* CONFIG_KRETPROBES */

static void
seq_print_sym(struct trace_seq *s, unsigned long address, bool offset)
{
#ifdef CONFIG_KALLSYMS
	char str[KSYM_SYMBOL_LEN];
	const char *name;

	if (offset)
		sprint_symbol(str, address);
	else
		kallsyms_lookup(address, NULL, NULL, NULL, str);
	name = kretprobed(str);

	if (name && strlen(name)) {
		trace_seq_puts(s, name);
		return;
	}
#endif
	trace_seq_printf(s, "0x%08lx", address);
}

#ifndef CONFIG_64BIT
# define IP_FMT "%08lx"
#else
# define IP_FMT "%016lx"
#endif

static int seq_print_user_ip(struct trace_seq *s, struct mm_struct *mm,
			     unsigned long ip, unsigned long sym_flags)
{
	struct file *file = NULL;
	unsigned long vmstart = 0;
	int ret = 1;

	if (s->full)
		return 0;

	if (mm) {
		const struct vm_area_struct *vma;

		mmap_read_lock(mm);
		vma = find_vma(mm, ip);
		if (vma) {
			file = vma->vm_file;
			vmstart = vma->vm_start;
		}
		if (file) {
			ret = trace_seq_path(s, &file->f_path);
			if (ret)
				trace_seq_printf(s, "[+0x%lx]",
						 ip - vmstart);
		}
		mmap_read_unlock(mm);
	}
	if (ret && ((sym_flags & TRACE_ITER_SYM_ADDR) || !file))
		trace_seq_printf(s, " <" IP_FMT ">", ip);
	return !trace_seq_has_overflowed(s);
}

int
seq_print_ip_sym(struct trace_seq *s, unsigned long ip, unsigned long sym_flags)
{
	if (!ip) {
		trace_seq_putc(s, '0');
		goto out;
	}

	seq_print_sym(s, ip, sym_flags & TRACE_ITER_SYM_OFFSET);

	if (sym_flags & TRACE_ITER_SYM_ADDR)
		trace_seq_printf(s, " <" IP_FMT ">", ip);

 out:
	return !trace_seq_has_overflowed(s);
}

/**
 * trace_print_lat_fmt - print the irq, preempt and lockdep fields
 * @s: trace seq struct to write to
 * @entry: The trace entry field from the ring buffer
 *
 * Prints the generic fields of irqs off, in hard or softirq, preempt
 * count.
 */
int trace_print_lat_fmt(struct trace_seq *s, struct trace_entry *entry)
{
	char hardsoft_irq;
	char need_resched;
	char irqs_off;
	int hardirq;
	int softirq;
	int nmi;

	nmi = entry->flags & TRACE_FLAG_NMI;
	hardirq = entry->flags & TRACE_FLAG_HARDIRQ;
	softirq = entry->flags & TRACE_FLAG_SOFTIRQ;

	irqs_off =
		(entry->flags & TRACE_FLAG_IRQS_OFF) ? 'd' :
		(entry->flags & TRACE_FLAG_IRQS_NOSUPPORT) ? 'X' :
		'.';

	switch (entry->flags & (TRACE_FLAG_NEED_RESCHED |
				TRACE_FLAG_PREEMPT_RESCHED)) {
	case TRACE_FLAG_NEED_RESCHED | TRACE_FLAG_PREEMPT_RESCHED:
		need_resched = 'N';
		break;
	case TRACE_FLAG_NEED_RESCHED:
		need_resched = 'n';
		break;
	case TRACE_FLAG_PREEMPT_RESCHED:
		need_resched = 'p';
		break;
	default:
		need_resched = '.';
		break;
	}

	hardsoft_irq =
		(nmi && hardirq)     ? 'Z' :
		nmi                  ? 'z' :
		(hardirq && softirq) ? 'H' :
		hardirq              ? 'h' :
		softirq              ? 's' :
		                       '.' ;

	trace_seq_printf(s, "%c%c%c",
			 irqs_off, need_resched, hardsoft_irq);

	if (entry->preempt_count)
		trace_seq_printf(s, "%x", entry->preempt_count);
	else
		trace_seq_putc(s, '.');

	return !trace_seq_has_overflowed(s);
}

static int
lat_print_generic(struct trace_seq *s, struct trace_entry *entry, int cpu)
{
	char comm[TASK_COMM_LEN];

	trace_find_cmdline(entry->pid, comm);

	trace_seq_printf(s, "%8.8s-%-7d %3d",
			 comm, entry->pid, cpu);

	return trace_print_lat_fmt(s, entry);
}

#undef MARK
#define MARK(v, s) {.val = v, .sym = s}
/* trace overhead mark */
static const struct trace_mark {
	unsigned long long	val; /* unit: nsec */
	char			sym;
} mark[] = {
	MARK(1000000000ULL	, '$'), /* 1 sec */
	MARK(100000000ULL	, '@'), /* 100 msec */
	MARK(10000000ULL	, '*'), /* 10 msec */
	MARK(1000000ULL		, '#'), /* 1000 usecs */
	MARK(100000ULL		, '!'), /* 100 usecs */
	MARK(10000ULL		, '+'), /* 10 usecs */
};
#undef MARK

char trace_find_mark(unsigned long long d)
{
	int i;
	int size = ARRAY_SIZE(mark);

	for (i = 0; i < size; i++) {
		if (d > mark[i].val)
			break;
	}

	return (i == size) ? ' ' : mark[i].sym;
}

static int
lat_print_timestamp(struct trace_iterator *iter, u64 next_ts)
{
	struct trace_array *tr = iter->tr;
	unsigned long verbose = tr->trace_flags & TRACE_ITER_VERBOSE;
	unsigned long in_ns = iter->iter_flags & TRACE_FILE_TIME_IN_NS;
	unsigned long long abs_ts = iter->ts - iter->array_buffer->time_start;
	unsigned long long rel_ts = next_ts - iter->ts;
	struct trace_seq *s = &iter->seq;

	if (in_ns) {
		abs_ts = ns2usecs(abs_ts);
		rel_ts = ns2usecs(rel_ts);
	}

	if (verbose && in_ns) {
		unsigned long abs_usec = do_div(abs_ts, USEC_PER_MSEC);
		unsigned long abs_msec = (unsigned long)abs_ts;
		unsigned long rel_usec = do_div(rel_ts, USEC_PER_MSEC);
		unsigned long rel_msec = (unsigned long)rel_ts;

		trace_seq_printf(
			s, "[%08llx] %ld.%03ldms (+%ld.%03ldms): ",
			ns2usecs(iter->ts),
			abs_msec, abs_usec,
			rel_msec, rel_usec);

	} else if (verbose && !in_ns) {
		trace_seq_printf(
			s, "[%016llx] %lld (+%lld): ",
			iter->ts, abs_ts, rel_ts);

	} else if (!verbose && in_ns) {
		trace_seq_printf(
			s, " %4lldus%c: ",
			abs_ts,
			trace_find_mark(rel_ts * NSEC_PER_USEC));

	} else { /* !verbose && !in_ns */
		trace_seq_printf(s, " %4lld: ", abs_ts);
	}

	return !trace_seq_has_overflowed(s);
}

int trace_print_context(struct trace_iterator *iter)
{
	struct trace_array *tr = iter->tr;
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry = iter->ent;
	unsigned long long t;
	unsigned long secs, usec_rem;
	char comm[TASK_COMM_LEN];

	trace_find_cmdline(entry->pid, comm);

	trace_seq_printf(s, "%16s-%-7d ", comm, entry->pid);

	if (tr->trace_flags & TRACE_ITER_RECORD_TGID) {
		unsigned int tgid = trace_find_tgid(entry->pid);

		if (!tgid)
			trace_seq_printf(s, "(-------) ");
		else
			trace_seq_printf(s, "(%7d) ", tgid);
	}

	trace_seq_printf(s, "[%03d] ", iter->cpu);

	if (tr->trace_flags & TRACE_ITER_IRQ_INFO)
		trace_print_lat_fmt(s, entry);

	if (iter->iter_flags & TRACE_FILE_TIME_IN_NS) {
		t = ns2usecs(iter->ts);
		usec_rem = do_div(t, USEC_PER_SEC);
		secs = (unsigned long)t;
		trace_seq_printf(s, " %5lu.%06lu: ", secs, usec_rem);
	} else
		trace_seq_printf(s, " %12llu: ", iter->ts);

	return !trace_seq_has_overflowed(s);
}

int trace_print_lat_context(struct trace_iterator *iter)
{
	struct trace_entry *entry, *next_entry;
	struct trace_array *tr = iter->tr;
	struct trace_seq *s = &iter->seq;
	unsigned long verbose = (tr->trace_flags & TRACE_ITER_VERBOSE);
	u64 next_ts;

	next_entry = trace_find_next_entry(iter, NULL, &next_ts);
	if (!next_entry)
		next_ts = iter->ts;

	/* trace_find_next_entry() may change iter->ent */
	entry = iter->ent;

	if (verbose) {
		char comm[TASK_COMM_LEN];

		trace_find_cmdline(entry->pid, comm);

		trace_seq_printf(
			s, "%16s %7d %3d %d %08x %08lx ",
			comm, entry->pid, iter->cpu, entry->flags,
			entry->preempt_count, iter->idx);
	} else {
		lat_print_generic(s, entry, iter->cpu);
	}

	lat_print_timestamp(iter, next_ts);

	return !trace_seq_has_overflowed(s);
}

/**
 * ftrace_find_event - find a registered event
 * @type: the type of event to look for
 *
 * Returns an event of type @type otherwise NULL
 * Called with trace_event_read_lock() held.
 */
struct trace_event *ftrace_find_event(int type)
{
	struct trace_event *event;
	unsigned key;

	key = type & (EVENT_HASHSIZE - 1);

	hlist_for_each_entry(event, &event_hash[key], node) {
		if (event->type == type)
			return event;
	}

	return NULL;
}

static LIST_HEAD(ftrace_event_list);

static int trace_search_list(struct list_head **list)
{
	struct trace_event *e;
	int next = __TRACE_LAST_TYPE;

	if (list_empty(&ftrace_event_list)) {
		*list = &ftrace_event_list;
		return next;
	}

	/*
	 * We used up all possible max events,
	 * lets see if somebody freed one.
	 */
	list_for_each_entry(e, &ftrace_event_list, list) {
		if (e->type != next)
			break;
		next++;
	}

	/* Did we used up all 65 thousand events??? */
	if (next > TRACE_EVENT_TYPE_MAX)
		return 0;

	*list = &e->list;
	return next;
}

void trace_event_read_lock(void)
{
	down_read(&trace_event_sem);
}

void trace_event_read_unlock(void)
{
	up_read(&trace_event_sem);
}

/**
 * register_trace_event - register output for an event type
 * @event: the event type to register
 *
 * Event types are stored in a hash and this hash is used to
 * find a way to print an event. If the @event->type is set
 * then it will use that type, otherwise it will assign a
 * type to use.
 *
 * If you assign your own type, please make sure it is added
 * to the trace_type enum in trace.h, to avoid collisions
 * with the dynamic types.
 *
 * Returns the event type number or zero on error.
 */
int register_trace_event(struct trace_event *event)
{
	unsigned key;
	int ret = 0;

	down_write(&trace_event_sem);

	if (WARN_ON(!event))
		goto out;

	if (WARN_ON(!event->funcs))
		goto out;

	INIT_LIST_HEAD(&event->list);

	if (!event->type) {
		struct list_head *list = NULL;

		if (next_event_type > TRACE_EVENT_TYPE_MAX) {

			event->type = trace_search_list(&list);
			if (!event->type)
				goto out;

		} else {

			event->type = next_event_type++;
			list = &ftrace_event_list;
		}

		if (WARN_ON(ftrace_find_event(event->type)))
			goto out;

		list_add_tail(&event->list, list);

	} else if (event->type > __TRACE_LAST_TYPE) {
		printk(KERN_WARNING "Need to add type to trace.h\n");
		WARN_ON(1);
		goto out;
	} else {
		/* Is this event already used */
		if (ftrace_find_event(event->type))
			goto out;
	}

	if (event->funcs->trace == NULL)
		event->funcs->trace = trace_nop_print;
	if (event->funcs->raw == NULL)
		event->funcs->raw = trace_nop_print;
	if (event->funcs->hex == NULL)
		event->funcs->hex = trace_nop_print;
	if (event->funcs->binary == NULL)
		event->funcs->binary = trace_nop_print;

	key = event->type & (EVENT_HASHSIZE - 1);

	hlist_add_head(&event->node, &event_hash[key]);

	ret = event->type;
 out:
	up_write(&trace_event_sem);

	return ret;
}
EXPORT_SYMBOL_GPL(register_trace_event);

/*
 * Used by module code with the trace_event_sem held for write.
 */
int __unregister_trace_event(struct trace_event *event)
{
	hlist_del(&event->node);
	list_del(&event->list);
	return 0;
}

/**
 * unregister_trace_event - remove a no longer used event
 * @event: the event to remove
 */
int unregister_trace_event(struct trace_event *event)
{
	down_write(&trace_event_sem);
	__unregister_trace_event(event);
	up_write(&trace_event_sem);

	return 0;
}
EXPORT_SYMBOL_GPL(unregister_trace_event);

/*
 * Standard events
 */

enum print_line_t trace_nop_print(struct trace_iterator *iter, int flags,
				  struct trace_event *event)
{
	trace_seq_printf(&iter->seq, "type: %d\n", iter->ent->type);

	return trace_handle_return(&iter->seq);
}

/* TRACE_FN */
static enum print_line_t trace_fn_trace(struct trace_iterator *iter, int flags,
					struct trace_event *event)
{
	struct ftrace_entry *field;
	struct trace_seq *s = &iter->seq;

	trace_assign_type(field, iter->ent);

	seq_print_ip_sym(s, field->ip, flags);

	if ((flags & TRACE_ITER_PRINT_PARENT) && field->parent_ip) {
		trace_seq_puts(s, " <-");
		seq_print_ip_sym(s, field->parent_ip, flags);
	}

	trace_seq_putc(s, '\n');

	return trace_handle_return(s);
}

static enum print_line_t trace_fn_raw(struct trace_iterator *iter, int flags,
				      struct trace_event *event)
{
	struct ftrace_entry *field;

	trace_assign_type(field, iter->ent);

	trace_seq_printf(&iter->seq, "%lx %lx\n",
			 field->ip,
			 field->parent_ip);

	return trace_handle_return(&iter->seq);
}

static enum print_line_t trace_fn_hex(struct trace_iterator *iter, int flags,
				      struct trace_event *event)
{
	struct ftrace_entry *field;
	struct trace_seq *s = &iter->seq;

	trace_assign_type(field, iter->ent);

	SEQ_PUT_HEX_FIELD(s, field->ip);
	SEQ_PUT_HEX_FIELD(s, field->parent_ip);

	return trace_handle_return(s);
}

static enum print_line_t trace_fn_bin(struct trace_iterator *iter, int flags,
				      struct trace_event *event)
{
	struct ftrace_entry *field;
	struct trace_seq *s = &iter->seq;

	trace_assign_type(field, iter->ent);

	SEQ_PUT_FIELD(s, field->ip);
	SEQ_PUT_FIELD(s, field->parent_ip);

	return trace_handle_return(s);
}

static struct trace_event_functions trace_fn_funcs = {
	.trace		= trace_fn_trace,
	.raw		= trace_fn_raw,
	.hex		= trace_fn_hex,
	.binary		= trace_fn_bin,
};

static struct trace_event trace_fn_event = {
	.type		= TRACE_FN,
	.funcs		= &trace_fn_funcs,
};

/* TRACE_CTX an TRACE_WAKE */
static enum print_line_t trace_ctxwake_print(struct trace_iterator *iter,
					     char *delim)
{
	struct ctx_switch_entry *field;
	char comm[TASK_COMM_LEN];
	int S, T;


	trace_assign_type(field, iter->ent);

	T = task_index_to_char(field->next_state);
	S = task_index_to_char(field->prev_state);
	trace_find_cmdline(field->next_pid, comm);
	trace_seq_printf(&iter->seq,
			 " %7d:%3d:%c %s [%03d] %7d:%3d:%c %s\n",
			 field->prev_pid,
			 field->prev_prio,
			 S, delim,
			 field->next_cpu,
			 field->next_pid,
			 field->next_prio,
			 T, comm);

	return trace_handle_return(&iter->seq);
}

static enum print_line_t trace_ctx_print(struct trace_iterator *iter, int flags,
					 struct trace_event *event)
{
	return trace_ctxwake_print(iter, "==>");
}

static enum print_line_t trace_wake_print(struct trace_iterator *iter,
					  int flags, struct trace_event *event)
{
	return trace_ctxwake_print(iter, "  +");
}

static int trace_ctxwake_raw(struct trace_iterator *iter, char S)
{
	struct ctx_switch_entry *field;
	int T;

	trace_assign_type(field, iter->ent);

	if (!S)
		S = task_index_to_char(field->prev_state);
	T = task_index_to_char(field->next_state);
	trace_seq_printf(&iter->seq, "%d %d %c %d %d %d %c\n",
			 field->prev_pid,
			 field->prev_prio,
			 S,
			 field->next_cpu,
			 field->next_pid,
			 field->next_prio,
			 T);

	return trace_handle_return(&iter->seq);
}

static enum print_line_t trace_ctx_raw(struct trace_iterator *iter, int flags,
				       struct trace_event *event)
{
	return trace_ctxwake_raw(iter, 0);
}

static enum print_line_t trace_wake_raw(struct trace_iterator *iter, int flags,
					struct trace_event *event)
{
	return trace_ctxwake_raw(iter, '+');
}


static int trace_ctxwake_hex(struct trace_iterator *iter, char S)
{
	struct ctx_switch_entry *field;
	struct trace_seq *s = &iter->seq;
	int T;

	trace_assign_type(field, iter->ent);

	if (!S)
		S = task_index_to_char(field->prev_state);
	T = task_index_to_char(field->next_state);

	SEQ_PUT_HEX_FIELD(s, field->prev_pid);
	SEQ_PUT_HEX_FIELD(s, field->prev_prio);
	SEQ_PUT_HEX_FIELD(s, S);
	SEQ_PUT_HEX_FIELD(s, field->next_cpu);
	SEQ_PUT_HEX_FIELD(s, field->next_pid);
	SEQ_PUT_HEX_FIELD(s, field->next_prio);
	SEQ_PUT_HEX_FIELD(s, T);

	return trace_handle_return(s);
}

static enum print_line_t trace_ctx_hex(struct trace_iterator *iter, int flags,
				       struct trace_event *event)
{
	return trace_ctxwake_hex(iter, 0);
}

static enum print_line_t trace_wake_hex(struct trace_iterator *iter, int flags,
					struct trace_event *event)
{
	return trace_ctxwake_hex(iter, '+');
}

static enum print_line_t trace_ctxwake_bin(struct trace_iterator *iter,
					   int flags, struct trace_event *event)
{
	struct ctx_switch_entry *field;
	struct trace_seq *s = &iter->seq;

	trace_assign_type(field, iter->ent);

	SEQ_PUT_FIELD(s, field->prev_pid);
	SEQ_PUT_FIELD(s, field->prev_prio);
	SEQ_PUT_FIELD(s, field->prev_state);
	SEQ_PUT_FIELD(s, field->next_cpu);
	SEQ_PUT_FIELD(s, field->next_pid);
	SEQ_PUT_FIELD(s, field->next_prio);
	SEQ_PUT_FIELD(s, field->next_state);

	return trace_handle_return(s);
}

static struct trace_event_functions trace_ctx_funcs = {
	.trace		= trace_ctx_print,
	.raw		= trace_ctx_raw,
	.hex		= trace_ctx_hex,
	.binary		= trace_ctxwake_bin,
};

static struct trace_event trace_ctx_event = {
	.type		= TRACE_CTX,
	.funcs		= &trace_ctx_funcs,
};

static struct trace_event_functions trace_wake_funcs = {
	.trace		= trace_wake_print,
	.raw		= trace_wake_raw,
	.hex		= trace_wake_hex,
	.binary		= trace_ctxwake_bin,
};

static struct trace_event trace_wake_event = {
	.type		= TRACE_WAKE,
	.funcs		= &trace_wake_funcs,
};

/* TRACE_STACK */

static enum print_line_t trace_stack_print(struct trace_iterator *iter,
					   int flags, struct trace_event *event)
{
	struct stack_entry *field;
	struct trace_seq *s = &iter->seq;
	unsigned long *p;
	unsigned long *end;

	trace_assign_type(field, iter->ent);
	end = (unsigned long *)((long)iter->ent + iter->ent_size);

	trace_seq_puts(s, "<stack trace>\n");

	for (p = field->caller; p && p < end && *p != ULONG_MAX; p++) {

		if (trace_seq_has_overflowed(s))
			break;

		trace_seq_puts(s, " => ");
		seq_print_ip_sym(s, *p, flags);
		trace_seq_putc(s, '\n');
	}

	return trace_handle_return(s);
}

static struct trace_event_functions trace_stack_funcs = {
	.trace		= trace_stack_print,
};

static struct trace_event trace_stack_event = {
	.type		= TRACE_STACK,
	.funcs		= &trace_stack_funcs,
};

/* TRACE_USER_STACK */
static enum print_line_t trace_user_stack_print(struct trace_iterator *iter,
						int flags, struct trace_event *event)
{
	struct trace_array *tr = iter->tr;
	struct userstack_entry *field;
	struct trace_seq *s = &iter->seq;
	struct mm_struct *mm = NULL;
	unsigned int i;

	trace_assign_type(field, iter->ent);

	trace_seq_puts(s, "<user stack trace>\n");

	if (tr->trace_flags & TRACE_ITER_SYM_USEROBJ) {
		struct task_struct *task;
		/*
		 * we do the lookup on the thread group leader,
		 * since individual threads might have already quit!
		 */
		rcu_read_lock();
		task = find_task_by_vpid(field->tgid);
		if (task)
			mm = get_task_mm(task);
		rcu_read_unlock();
	}

	for (i = 0; i < FTRACE_STACK_ENTRIES; i++) {
		unsigned long ip = field->caller[i];

		if (!ip || trace_seq_has_overflowed(s))
			break;

		trace_seq_puts(s, " => ");
		seq_print_user_ip(s, mm, ip, flags);
		trace_seq_putc(s, '\n');
	}

	if (mm)
		mmput(mm);

	return trace_handle_return(s);
}

static struct trace_event_functions trace_user_stack_funcs = {
	.trace		= trace_user_stack_print,
};

static struct trace_event trace_user_stack_event = {
	.type		= TRACE_USER_STACK,
	.funcs		= &trace_user_stack_funcs,
};

/* TRACE_HWLAT */
static enum print_line_t
trace_hwlat_print(struct trace_iterator *iter, int flags,
		  struct trace_event *event)
{
	struct trace_entry *entry = iter->ent;
	struct trace_seq *s = &iter->seq;
	struct hwlat_entry *field;

	trace_assign_type(field, entry);

	trace_seq_printf(s, "#%-5u inner/outer(us): %4llu/%-5llu ts:%lld.%09ld count:%d",
			 field->seqnum,
			 field->duration,
			 field->outer_duration,
			 (long long)field->timestamp.tv_sec,
			 field->timestamp.tv_nsec, field->count);

	if (field->nmi_count) {
		/*
		 * The generic sched_clock() is not NMI safe, thus
		 * we only record the count and not the time.
		 */
		if (!IS_ENABLED(CONFIG_GENERIC_SCHED_CLOCK))
			trace_seq_printf(s, " nmi-total:%llu",
					 field->nmi_total_ts);
		trace_seq_printf(s, " nmi-count:%u",
				 field->nmi_count);
	}

	trace_seq_putc(s, '\n');

	return trace_handle_return(s);
}


static enum print_line_t
trace_hwlat_raw(struct trace_iterator *iter, int flags,
		struct trace_event *event)
{
	struct hwlat_entry *field;
	struct trace_seq *s = &iter->seq;

	trace_assign_type(field, iter->ent);

	trace_seq_printf(s, "%llu %lld %lld %09ld %u\n",
			 field->duration,
			 field->outer_duration,
			 (long long)field->timestamp.tv_sec,
			 field->timestamp.tv_nsec,
			 field->seqnum);

	return trace_handle_return(s);
}

static struct trace_event_functions trace_hwlat_funcs = {
	.trace		= trace_hwlat_print,
	.raw		= trace_hwlat_raw,
};

static struct trace_event trace_hwlat_event = {
	.type		= TRACE_HWLAT,
	.funcs		= &trace_hwlat_funcs,
};

/* TRACE_BPUTS */
static enum print_line_t
trace_bputs_print(struct trace_iterator *iter, int flags,
		   struct trace_event *event)
{
	struct trace_entry *entry = iter->ent;
	struct trace_seq *s = &iter->seq;
	struct bputs_entry *field;

	trace_assign_type(field, entry);

	seq_print_ip_sym(s, field->ip, flags);
	trace_seq_puts(s, ": ");
	trace_seq_puts(s, field->str);

	return trace_handle_return(s);
}


static enum print_line_t
trace_bputs_raw(struct trace_iterator *iter, int flags,
		struct trace_event *event)
{
	struct bputs_entry *field;
	struct trace_seq *s = &iter->seq;

	trace_assign_type(field, iter->ent);

	trace_seq_printf(s, ": %lx : ", field->ip);
	trace_seq_puts(s, field->str);

	return trace_handle_return(s);
}

static struct trace_event_functions trace_bputs_funcs = {
	.trace		= trace_bputs_print,
	.raw		= trace_bputs_raw,
};

static struct trace_event trace_bputs_event = {
	.type		= TRACE_BPUTS,
	.funcs		= &trace_bputs_funcs,
};

/* TRACE_BPRINT */
static enum print_line_t
trace_bprint_print(struct trace_iterator *iter, int flags,
		   struct trace_event *event)
{
	struct trace_entry *entry = iter->ent;
	struct trace_seq *s = &iter->seq;
	struct bprint_entry *field;

	trace_assign_type(field, entry);

	seq_print_ip_sym(s, field->ip, flags);
	trace_seq_puts(s, ": ");
	trace_seq_bprintf(s, field->fmt, field->buf);

	return trace_handle_return(s);
}


static enum print_line_t
trace_bprint_raw(struct trace_iterator *iter, int flags,
		 struct trace_event *event)
{
	struct bprint_entry *field;
	struct trace_seq *s = &iter->seq;

	trace_assign_type(field, iter->ent);

	trace_seq_printf(s, ": %lx : ", field->ip);
	trace_seq_bprintf(s, field->fmt, field->buf);

	return trace_handle_return(s);
}

static struct trace_event_functions trace_bprint_funcs = {
	.trace		= trace_bprint_print,
	.raw		= trace_bprint_raw,
};

static struct trace_event trace_bprint_event = {
	.type		= TRACE_BPRINT,
	.funcs		= &trace_bprint_funcs,
};

/* TRACE_PRINT */
static enum print_line_t trace_print_print(struct trace_iterator *iter,
					   int flags, struct trace_event *event)
{
	struct print_entry *field;
	struct trace_seq *s = &iter->seq;

	trace_assign_type(field, iter->ent);

	seq_print_ip_sym(s, field->ip, flags);
	trace_seq_printf(s, ": %s", field->buf);

	return trace_handle_return(s);
}

static enum print_line_t trace_print_raw(struct trace_iterator *iter, int flags,
					 struct trace_event *event)
{
	struct print_entry *field;

	trace_assign_type(field, iter->ent);

	trace_seq_printf(&iter->seq, "# %lx %s", field->ip, field->buf);

	return trace_handle_return(&iter->seq);
}

static struct trace_event_functions trace_print_funcs = {
	.trace		= trace_print_print,
	.raw		= trace_print_raw,
};

static struct trace_event trace_print_event = {
	.type	 	= TRACE_PRINT,
	.funcs		= &trace_print_funcs,
};

static enum print_line_t trace_raw_data(struct trace_iterator *iter, int flags,
					 struct trace_event *event)
{
	struct raw_data_entry *field;
	int i;

	trace_assign_type(field, iter->ent);

	trace_seq_printf(&iter->seq, "# %x buf:", field->id);

	for (i = 0; i < iter->ent_size - offsetof(struct raw_data_entry, buf); i++)
		trace_seq_printf(&iter->seq, " %02x",
				 (unsigned char)field->buf[i]);

	trace_seq_putc(&iter->seq, '\n');

	return trace_handle_return(&iter->seq);
}

static struct trace_event_functions trace_raw_data_funcs = {
	.trace		= trace_raw_data,
	.raw		= trace_raw_data,
};

static struct trace_event trace_raw_data_event = {
	.type	 	= TRACE_RAW_DATA,
	.funcs		= &trace_raw_data_funcs,
};


static struct trace_event *events[] __initdata = {
	&trace_fn_event,
	&trace_ctx_event,
	&trace_wake_event,
	&trace_stack_event,
	&trace_user_stack_event,
	&trace_bputs_event,
	&trace_bprint_event,
	&trace_print_event,
	&trace_hwlat_event,
	&trace_raw_data_event,
	NULL
};

__init int init_events(void)
{
	struct trace_event *event;
	int i, ret;

	for (i = 0; events[i]; i++) {
		event = events[i];

		ret = register_trace_event(event);
		if (!ret) {
			printk(KERN_WARNING "event %d failed to register\n",
			       event->type);
			WARN_ON_ONCE(1);
		}
	}

	return 0;
}
