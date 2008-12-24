/*
 * trace_output.c
 *
 * Copyright (C) 2008 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/ftrace.h>

#include "trace_output.h"

/* must be a power of 2 */
#define EVENT_HASHSIZE	128

static DEFINE_MUTEX(trace_event_mutex);
static struct hlist_head event_hash[EVENT_HASHSIZE] __read_mostly;

static int next_event_type = __TRACE_LAST_TYPE + 1;

/**
 * trace_seq_printf - sequence printing of trace information
 * @s: trace sequence descriptor
 * @fmt: printf format string
 *
 * The tracer may use either sequence operations or its own
 * copy to user routines. To simplify formating of a trace
 * trace_seq_printf is used to store strings into a special
 * buffer (@s). Then the output may be either used by
 * the sequencer or pulled into another buffer.
 */
int
trace_seq_printf(struct trace_seq *s, const char *fmt, ...)
{
	int len = (PAGE_SIZE - 1) - s->len;
	va_list ap;
	int ret;

	if (!len)
		return 0;

	va_start(ap, fmt);
	ret = vsnprintf(s->buffer + s->len, len, fmt, ap);
	va_end(ap);

	/* If we can't write it all, don't bother writing anything */
	if (ret >= len)
		return 0;

	s->len += ret;

	return len;
}

/**
 * trace_seq_puts - trace sequence printing of simple string
 * @s: trace sequence descriptor
 * @str: simple string to record
 *
 * The tracer may use either the sequence operations or its own
 * copy to user routines. This function records a simple string
 * into a special buffer (@s) for later retrieval by a sequencer
 * or other mechanism.
 */
int trace_seq_puts(struct trace_seq *s, const char *str)
{
	int len = strlen(str);

	if (len > ((PAGE_SIZE - 1) - s->len))
		return 0;

	memcpy(s->buffer + s->len, str, len);
	s->len += len;

	return len;
}

int trace_seq_putc(struct trace_seq *s, unsigned char c)
{
	if (s->len >= (PAGE_SIZE - 1))
		return 0;

	s->buffer[s->len++] = c;

	return 1;
}

int trace_seq_putmem(struct trace_seq *s, void *mem, size_t len)
{
	if (len > ((PAGE_SIZE - 1) - s->len))
		return 0;

	memcpy(s->buffer + s->len, mem, len);
	s->len += len;

	return len;
}

int trace_seq_putmem_hex(struct trace_seq *s, void *mem, size_t len)
{
	unsigned char hex[HEX_CHARS];
	unsigned char *data = mem;
	int i, j;

#ifdef __BIG_ENDIAN
	for (i = 0, j = 0; i < len; i++) {
#else
	for (i = len-1, j = 0; i >= 0; i--) {
#endif
		hex[j++] = hex_asc_hi(data[i]);
		hex[j++] = hex_asc_lo(data[i]);
	}
	hex[j++] = ' ';

	return trace_seq_putmem(s, hex, j);
}

int trace_seq_path(struct trace_seq *s, struct path *path)
{
	unsigned char *p;

	if (s->len >= (PAGE_SIZE - 1))
		return 0;
	p = d_path(path, s->buffer + s->len, PAGE_SIZE - s->len);
	if (!IS_ERR(p)) {
		p = mangle_path(s->buffer + s->len, p, "\n");
		if (p) {
			s->len = p - s->buffer;
			return 1;
		}
	} else {
		s->buffer[s->len++] = '?';
		return 1;
	}

	return 0;
}

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

static int
seq_print_sym_short(struct trace_seq *s, const char *fmt, unsigned long address)
{
#ifdef CONFIG_KALLSYMS
	char str[KSYM_SYMBOL_LEN];
	const char *name;

	kallsyms_lookup(address, NULL, NULL, NULL, str);

	name = kretprobed(str);

	return trace_seq_printf(s, fmt, name);
#endif
	return 1;
}

static int
seq_print_sym_offset(struct trace_seq *s, const char *fmt,
		     unsigned long address)
{
#ifdef CONFIG_KALLSYMS
	char str[KSYM_SYMBOL_LEN];
	const char *name;

	sprint_symbol(str, address);
	name = kretprobed(str);

	return trace_seq_printf(s, fmt, name);
#endif
	return 1;
}

#ifndef CONFIG_64BIT
# define IP_FMT "%08lx"
#else
# define IP_FMT "%016lx"
#endif

int seq_print_user_ip(struct trace_seq *s, struct mm_struct *mm,
		      unsigned long ip, unsigned long sym_flags)
{
	struct file *file = NULL;
	unsigned long vmstart = 0;
	int ret = 1;

	if (mm) {
		const struct vm_area_struct *vma;

		down_read(&mm->mmap_sem);
		vma = find_vma(mm, ip);
		if (vma) {
			file = vma->vm_file;
			vmstart = vma->vm_start;
		}
		if (file) {
			ret = trace_seq_path(s, &file->f_path);
			if (ret)
				ret = trace_seq_printf(s, "[+0x%lx]",
						       ip - vmstart);
		}
		up_read(&mm->mmap_sem);
	}
	if (ret && ((sym_flags & TRACE_ITER_SYM_ADDR) || !file))
		ret = trace_seq_printf(s, " <" IP_FMT ">", ip);
	return ret;
}

int
seq_print_userip_objs(const struct userstack_entry *entry, struct trace_seq *s,
		      unsigned long sym_flags)
{
	struct mm_struct *mm = NULL;
	int ret = 1;
	unsigned int i;

	if (trace_flags & TRACE_ITER_SYM_USEROBJ) {
		struct task_struct *task;
		/*
		 * we do the lookup on the thread group leader,
		 * since individual threads might have already quit!
		 */
		rcu_read_lock();
		task = find_task_by_vpid(entry->ent.tgid);
		if (task)
			mm = get_task_mm(task);
		rcu_read_unlock();
	}

	for (i = 0; i < FTRACE_STACK_ENTRIES; i++) {
		unsigned long ip = entry->caller[i];

		if (ip == ULONG_MAX || !ret)
			break;
		if (i && ret)
			ret = trace_seq_puts(s, " <- ");
		if (!ip) {
			if (ret)
				ret = trace_seq_puts(s, "??");
			continue;
		}
		if (!ret)
			break;
		if (ret)
			ret = seq_print_user_ip(s, mm, ip, sym_flags);
	}

	if (mm)
		mmput(mm);
	return ret;
}

int
seq_print_ip_sym(struct trace_seq *s, unsigned long ip, unsigned long sym_flags)
{
	int ret;

	if (!ip)
		return trace_seq_printf(s, "0");

	if (sym_flags & TRACE_ITER_SYM_OFFSET)
		ret = seq_print_sym_offset(s, "%s", ip);
	else
		ret = seq_print_sym_short(s, "%s", ip);

	if (!ret)
		return 0;

	if (sym_flags & TRACE_ITER_SYM_ADDR)
		ret = trace_seq_printf(s, " <" IP_FMT ">", ip);
	return ret;
}

/**
 * ftrace_find_event - find a registered event
 * @type: the type of event to look for
 *
 * Returns an event of type @type otherwise NULL
 */
struct trace_event *ftrace_find_event(int type)
{
	struct trace_event *event;
	struct hlist_node *n;
	unsigned key;

	key = type & (EVENT_HASHSIZE - 1);

	hlist_for_each_entry_rcu(event, n, &event_hash[key], node) {
		if (event->type == type)
			return event;
	}

	return NULL;
}

/**
 * register_ftrace_event - register output for an event type
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
int register_ftrace_event(struct trace_event *event)
{
	unsigned key;
	int ret = 0;

	mutex_lock(&trace_event_mutex);

	if (!event->type)
		event->type = next_event_type++;
	else if (event->type > __TRACE_LAST_TYPE) {
		printk(KERN_WARNING "Need to add type to trace.h\n");
		WARN_ON(1);
	}

	if (ftrace_find_event(event->type))
		goto out;

	key = event->type & (EVENT_HASHSIZE - 1);

	hlist_add_head_rcu(&event->node, &event_hash[key]);

	ret = event->type;
 out:
	mutex_unlock(&trace_event_mutex);

	return ret;
}

/**
 * unregister_ftrace_event - remove a no longer used event
 * @event: the event to remove
 */
int unregister_ftrace_event(struct trace_event *event)
{
	mutex_lock(&trace_event_mutex);
	hlist_del(&event->node);
	mutex_unlock(&trace_event_mutex);

	return 0;
}
