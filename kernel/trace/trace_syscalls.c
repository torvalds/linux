#include <trace/syscall.h>
#include <trace/events/syscalls.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>	/* for MODULE_NAME_LEN via KSYM_SYMBOL_LEN */
#include <linux/ftrace.h>
#include <linux/perf_event.h>
#include <asm/syscall.h>

#include "trace_output.h"
#include "trace.h"

static DEFINE_MUTEX(syscall_trace_lock);

static int syscall_enter_register(struct ftrace_event_call *event,
				 enum trace_reg type, void *data);
static int syscall_exit_register(struct ftrace_event_call *event,
				 enum trace_reg type, void *data);

static struct list_head *
syscall_get_enter_fields(struct ftrace_event_call *call)
{
	struct syscall_metadata *entry = call->data;

	return &entry->enter_fields;
}

extern struct syscall_metadata *__start_syscalls_metadata[];
extern struct syscall_metadata *__stop_syscalls_metadata[];

static struct syscall_metadata **syscalls_metadata;

#ifndef ARCH_HAS_SYSCALL_MATCH_SYM_NAME
static inline bool arch_syscall_match_sym_name(const char *sym, const char *name)
{
	/*
	 * Only compare after the "sys" prefix. Archs that use
	 * syscall wrappers may have syscalls symbols aliases prefixed
	 * with ".SyS" or ".sys" instead of "sys", leading to an unwanted
	 * mismatch.
	 */
	return !strcmp(sym + 3, name + 3);
}
#endif

#ifdef ARCH_TRACE_IGNORE_COMPAT_SYSCALLS
/*
 * Some architectures that allow for 32bit applications
 * to run on a 64bit kernel, do not map the syscalls for
 * the 32bit tasks the same as they do for 64bit tasks.
 *
 *     *cough*x86*cough*
 *
 * In such a case, instead of reporting the wrong syscalls,
 * simply ignore them.
 *
 * For an arch to ignore the compat syscalls it needs to
 * define ARCH_TRACE_IGNORE_COMPAT_SYSCALLS as well as
 * define the function arch_trace_is_compat_syscall() to let
 * the tracing system know that it should ignore it.
 */
static int
trace_get_syscall_nr(struct task_struct *task, struct pt_regs *regs)
{
	if (unlikely(arch_trace_is_compat_syscall(regs)))
		return -1;

	return syscall_get_nr(task, regs);
}
#else
static inline int
trace_get_syscall_nr(struct task_struct *task, struct pt_regs *regs)
{
	return syscall_get_nr(task, regs);
}
#endif /* ARCH_TRACE_IGNORE_COMPAT_SYSCALLS */

static __init struct syscall_metadata *
find_syscall_meta(unsigned long syscall)
{
	struct syscall_metadata **start;
	struct syscall_metadata **stop;
	char str[KSYM_SYMBOL_LEN];


	start = __start_syscalls_metadata;
	stop = __stop_syscalls_metadata;
	kallsyms_lookup(syscall, NULL, NULL, NULL, str);

	if (arch_syscall_match_sym_name(str, "sys_ni_syscall"))
		return NULL;

	for ( ; start < stop; start++) {
		if ((*start)->name && arch_syscall_match_sym_name(str, (*start)->name))
			return *start;
	}
	return NULL;
}

static struct syscall_metadata *syscall_nr_to_meta(int nr)
{
	if (!syscalls_metadata || nr >= NR_syscalls || nr < 0)
		return NULL;

	return syscalls_metadata[nr];
}

static enum print_line_t
print_syscall_enter(struct trace_iterator *iter, int flags,
		    struct trace_event *event)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *ent = iter->ent;
	struct syscall_trace_enter *trace;
	struct syscall_metadata *entry;
	int i, syscall;

	trace = (typeof(trace))ent;
	syscall = trace->nr;
	entry = syscall_nr_to_meta(syscall);

	if (!entry)
		goto end;

	if (entry->enter_event->event.type != ent->type) {
		WARN_ON_ONCE(1);
		goto end;
	}

	trace_seq_printf(s, "%s(", entry->name);

	for (i = 0; i < entry->nb_args; i++) {

		if (trace_seq_has_overflowed(s))
			goto end;

		/* parameter types */
		if (trace_flags & TRACE_ITER_VERBOSE)
			trace_seq_printf(s, "%s ", entry->types[i]);

		/* parameter values */
		trace_seq_printf(s, "%s: %lx%s", entry->args[i],
				 trace->args[i],
				 i == entry->nb_args - 1 ? "" : ", ");
	}

	trace_seq_putc(s, ')');
end:
	trace_seq_putc(s, '\n');

	return trace_handle_return(s);
}

static enum print_line_t
print_syscall_exit(struct trace_iterator *iter, int flags,
		   struct trace_event *event)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *ent = iter->ent;
	struct syscall_trace_exit *trace;
	int syscall;
	struct syscall_metadata *entry;

	trace = (typeof(trace))ent;
	syscall = trace->nr;
	entry = syscall_nr_to_meta(syscall);

	if (!entry) {
		trace_seq_putc(s, '\n');
		goto out;
	}

	if (entry->exit_event->event.type != ent->type) {
		WARN_ON_ONCE(1);
		return TRACE_TYPE_UNHANDLED;
	}

	trace_seq_printf(s, "%s -> 0x%lx\n", entry->name,
				trace->ret);

 out:
	return trace_handle_return(s);
}

extern char *__bad_type_size(void);

#define SYSCALL_FIELD(type, name)					\
	sizeof(type) != sizeof(trace.name) ?				\
		__bad_type_size() :					\
		#type, #name, offsetof(typeof(trace), name),		\
		sizeof(trace.name), is_signed_type(type)

static int __init
__set_enter_print_fmt(struct syscall_metadata *entry, char *buf, int len)
{
	int i;
	int pos = 0;

	/* When len=0, we just calculate the needed length */
#define LEN_OR_ZERO (len ? len - pos : 0)

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"");
	for (i = 0; i < entry->nb_args; i++) {
		pos += snprintf(buf + pos, LEN_OR_ZERO, "%s: 0x%%0%zulx%s",
				entry->args[i], sizeof(unsigned long),
				i == entry->nb_args - 1 ? "" : ", ");
	}
	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"");

	for (i = 0; i < entry->nb_args; i++) {
		pos += snprintf(buf + pos, LEN_OR_ZERO,
				", ((unsigned long)(REC->%s))", entry->args[i]);
	}

#undef LEN_OR_ZERO

	/* return the length of print_fmt */
	return pos;
}

static int __init set_syscall_print_fmt(struct ftrace_event_call *call)
{
	char *print_fmt;
	int len;
	struct syscall_metadata *entry = call->data;

	if (entry->enter_event != call) {
		call->print_fmt = "\"0x%lx\", REC->ret";
		return 0;
	}

	/* First: called with 0 length to calculate the needed length */
	len = __set_enter_print_fmt(entry, NULL, 0);

	print_fmt = kmalloc(len + 1, GFP_KERNEL);
	if (!print_fmt)
		return -ENOMEM;

	/* Second: actually write the @print_fmt */
	__set_enter_print_fmt(entry, print_fmt, len + 1);
	call->print_fmt = print_fmt;

	return 0;
}

static void __init free_syscall_print_fmt(struct ftrace_event_call *call)
{
	struct syscall_metadata *entry = call->data;

	if (entry->enter_event == call)
		kfree(call->print_fmt);
}

static int __init syscall_enter_define_fields(struct ftrace_event_call *call)
{
	struct syscall_trace_enter trace;
	struct syscall_metadata *meta = call->data;
	int ret;
	int i;
	int offset = offsetof(typeof(trace), args);

	ret = trace_define_field(call, SYSCALL_FIELD(int, nr), FILTER_OTHER);
	if (ret)
		return ret;

	for (i = 0; i < meta->nb_args; i++) {
		ret = trace_define_field(call, meta->types[i],
					 meta->args[i], offset,
					 sizeof(unsigned long), 0,
					 FILTER_OTHER);
		offset += sizeof(unsigned long);
	}

	return ret;
}

static int __init syscall_exit_define_fields(struct ftrace_event_call *call)
{
	struct syscall_trace_exit trace;
	int ret;

	ret = trace_define_field(call, SYSCALL_FIELD(int, nr), FILTER_OTHER);
	if (ret)
		return ret;

	ret = trace_define_field(call, SYSCALL_FIELD(long, ret),
				 FILTER_OTHER);

	return ret;
}

static void ftrace_syscall_enter(void *data, struct pt_regs *regs, long id)
{
	struct trace_array *tr = data;
	struct ftrace_event_file *ftrace_file;
	struct syscall_trace_enter *entry;
	struct syscall_metadata *sys_data;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	unsigned long irq_flags;
	int pc;
	int syscall_nr;
	int size;

	syscall_nr = trace_get_syscall_nr(current, regs);
	if (syscall_nr < 0 || syscall_nr >= NR_syscalls)
		return;

	/* Here we're inside tp handler's rcu_read_lock_sched (__DO_TRACE) */
	ftrace_file = rcu_dereference_sched(tr->enter_syscall_files[syscall_nr]);
	if (!ftrace_file)
		return;

	if (ftrace_trigger_soft_disabled(ftrace_file))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	size = sizeof(*entry) + sizeof(unsigned long) * sys_data->nb_args;

	local_save_flags(irq_flags);
	pc = preempt_count();

	buffer = tr->trace_buffer.buffer;
	event = trace_buffer_lock_reserve(buffer,
			sys_data->enter_event->event.type, size, irq_flags, pc);
	if (!event)
		return;

	entry = ring_buffer_event_data(event);
	entry->nr = syscall_nr;
	syscall_get_arguments(current, regs, 0, sys_data->nb_args, entry->args);

	event_trigger_unlock_commit(ftrace_file, buffer, event, entry,
				    irq_flags, pc);
}

static void ftrace_syscall_exit(void *data, struct pt_regs *regs, long ret)
{
	struct trace_array *tr = data;
	struct ftrace_event_file *ftrace_file;
	struct syscall_trace_exit *entry;
	struct syscall_metadata *sys_data;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	unsigned long irq_flags;
	int pc;
	int syscall_nr;

	syscall_nr = trace_get_syscall_nr(current, regs);
	if (syscall_nr < 0 || syscall_nr >= NR_syscalls)
		return;

	/* Here we're inside tp handler's rcu_read_lock_sched (__DO_TRACE()) */
	ftrace_file = rcu_dereference_sched(tr->exit_syscall_files[syscall_nr]);
	if (!ftrace_file)
		return;

	if (ftrace_trigger_soft_disabled(ftrace_file))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	local_save_flags(irq_flags);
	pc = preempt_count();

	buffer = tr->trace_buffer.buffer;
	event = trace_buffer_lock_reserve(buffer,
			sys_data->exit_event->event.type, sizeof(*entry),
			irq_flags, pc);
	if (!event)
		return;

	entry = ring_buffer_event_data(event);
	entry->nr = syscall_nr;
	entry->ret = syscall_get_return_value(current, regs);

	event_trigger_unlock_commit(ftrace_file, buffer, event, entry,
				    irq_flags, pc);
}

static int reg_event_syscall_enter(struct ftrace_event_file *file,
				   struct ftrace_event_call *call)
{
	struct trace_array *tr = file->tr;
	int ret = 0;
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;
	if (WARN_ON_ONCE(num < 0 || num >= NR_syscalls))
		return -ENOSYS;
	mutex_lock(&syscall_trace_lock);
	if (!tr->sys_refcount_enter)
		ret = register_trace_sys_enter(ftrace_syscall_enter, tr);
	if (!ret) {
		rcu_assign_pointer(tr->enter_syscall_files[num], file);
		tr->sys_refcount_enter++;
	}
	mutex_unlock(&syscall_trace_lock);
	return ret;
}

static void unreg_event_syscall_enter(struct ftrace_event_file *file,
				      struct ftrace_event_call *call)
{
	struct trace_array *tr = file->tr;
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;
	if (WARN_ON_ONCE(num < 0 || num >= NR_syscalls))
		return;
	mutex_lock(&syscall_trace_lock);
	tr->sys_refcount_enter--;
	RCU_INIT_POINTER(tr->enter_syscall_files[num], NULL);
	if (!tr->sys_refcount_enter)
		unregister_trace_sys_enter(ftrace_syscall_enter, tr);
	mutex_unlock(&syscall_trace_lock);
}

static int reg_event_syscall_exit(struct ftrace_event_file *file,
				  struct ftrace_event_call *call)
{
	struct trace_array *tr = file->tr;
	int ret = 0;
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;
	if (WARN_ON_ONCE(num < 0 || num >= NR_syscalls))
		return -ENOSYS;
	mutex_lock(&syscall_trace_lock);
	if (!tr->sys_refcount_exit)
		ret = register_trace_sys_exit(ftrace_syscall_exit, tr);
	if (!ret) {
		rcu_assign_pointer(tr->exit_syscall_files[num], file);
		tr->sys_refcount_exit++;
	}
	mutex_unlock(&syscall_trace_lock);
	return ret;
}

static void unreg_event_syscall_exit(struct ftrace_event_file *file,
				     struct ftrace_event_call *call)
{
	struct trace_array *tr = file->tr;
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;
	if (WARN_ON_ONCE(num < 0 || num >= NR_syscalls))
		return;
	mutex_lock(&syscall_trace_lock);
	tr->sys_refcount_exit--;
	RCU_INIT_POINTER(tr->exit_syscall_files[num], NULL);
	if (!tr->sys_refcount_exit)
		unregister_trace_sys_exit(ftrace_syscall_exit, tr);
	mutex_unlock(&syscall_trace_lock);
}

static int __init init_syscall_trace(struct ftrace_event_call *call)
{
	int id;
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;
	if (num < 0 || num >= NR_syscalls) {
		pr_debug("syscall %s metadata not mapped, disabling ftrace event\n",
				((struct syscall_metadata *)call->data)->name);
		return -ENOSYS;
	}

	if (set_syscall_print_fmt(call) < 0)
		return -ENOMEM;

	id = trace_event_raw_init(call);

	if (id < 0) {
		free_syscall_print_fmt(call);
		return id;
	}

	return id;
}

struct trace_event_functions enter_syscall_print_funcs = {
	.trace		= print_syscall_enter,
};

struct trace_event_functions exit_syscall_print_funcs = {
	.trace		= print_syscall_exit,
};

struct ftrace_event_class __refdata event_class_syscall_enter = {
	.system		= "syscalls",
	.reg		= syscall_enter_register,
	.define_fields	= syscall_enter_define_fields,
	.get_fields	= syscall_get_enter_fields,
	.raw_init	= init_syscall_trace,
};

struct ftrace_event_class __refdata event_class_syscall_exit = {
	.system		= "syscalls",
	.reg		= syscall_exit_register,
	.define_fields	= syscall_exit_define_fields,
	.fields		= LIST_HEAD_INIT(event_class_syscall_exit.fields),
	.raw_init	= init_syscall_trace,
};

unsigned long __init __weak arch_syscall_addr(int nr)
{
	return (unsigned long)sys_call_table[nr];
}

void __init init_ftrace_syscalls(void)
{
	struct syscall_metadata *meta;
	unsigned long addr;
	int i;

	syscalls_metadata = kcalloc(NR_syscalls, sizeof(*syscalls_metadata),
				    GFP_KERNEL);
	if (!syscalls_metadata) {
		WARN_ON(1);
		return;
	}

	for (i = 0; i < NR_syscalls; i++) {
		addr = arch_syscall_addr(i);
		meta = find_syscall_meta(addr);
		if (!meta)
			continue;

		meta->syscall_nr = i;
		syscalls_metadata[i] = meta;
	}
}

#ifdef CONFIG_PERF_EVENTS

static DECLARE_BITMAP(enabled_perf_enter_syscalls, NR_syscalls);
static DECLARE_BITMAP(enabled_perf_exit_syscalls, NR_syscalls);
static int sys_perf_refcount_enter;
static int sys_perf_refcount_exit;

static void perf_syscall_enter(void *ignore, struct pt_regs *regs, long id)
{
	struct syscall_metadata *sys_data;
	struct syscall_trace_enter *rec;
	struct hlist_head *head;
	int syscall_nr;
	int rctx;
	int size;

	syscall_nr = trace_get_syscall_nr(current, regs);
	if (syscall_nr < 0 || syscall_nr >= NR_syscalls)
		return;
	if (!test_bit(syscall_nr, enabled_perf_enter_syscalls))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	head = this_cpu_ptr(sys_data->enter_event->perf_events);
	if (hlist_empty(head))
		return;

	/* get the size after alignment with the u32 buffer size field */
	size = sizeof(unsigned long) * sys_data->nb_args + sizeof(*rec);
	size = ALIGN(size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);

	rec = (struct syscall_trace_enter *)perf_trace_buf_prepare(size,
				sys_data->enter_event->event.type, NULL, &rctx);
	if (!rec)
		return;

	rec->nr = syscall_nr;
	syscall_get_arguments(current, regs, 0, sys_data->nb_args,
			       (unsigned long *)&rec->args);
	perf_trace_buf_submit(rec, size, rctx, 0, 1, regs, head, NULL);
}

static int perf_sysenter_enable(struct ftrace_event_call *call)
{
	int ret = 0;
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;

	mutex_lock(&syscall_trace_lock);
	if (!sys_perf_refcount_enter)
		ret = register_trace_sys_enter(perf_syscall_enter, NULL);
	if (ret) {
		pr_info("event trace: Could not activate"
				"syscall entry trace point");
	} else {
		set_bit(num, enabled_perf_enter_syscalls);
		sys_perf_refcount_enter++;
	}
	mutex_unlock(&syscall_trace_lock);
	return ret;
}

static void perf_sysenter_disable(struct ftrace_event_call *call)
{
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;

	mutex_lock(&syscall_trace_lock);
	sys_perf_refcount_enter--;
	clear_bit(num, enabled_perf_enter_syscalls);
	if (!sys_perf_refcount_enter)
		unregister_trace_sys_enter(perf_syscall_enter, NULL);
	mutex_unlock(&syscall_trace_lock);
}

static void perf_syscall_exit(void *ignore, struct pt_regs *regs, long ret)
{
	struct syscall_metadata *sys_data;
	struct syscall_trace_exit *rec;
	struct hlist_head *head;
	int syscall_nr;
	int rctx;
	int size;

	syscall_nr = trace_get_syscall_nr(current, regs);
	if (syscall_nr < 0 || syscall_nr >= NR_syscalls)
		return;
	if (!test_bit(syscall_nr, enabled_perf_exit_syscalls))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	head = this_cpu_ptr(sys_data->exit_event->perf_events);
	if (hlist_empty(head))
		return;

	/* We can probably do that at build time */
	size = ALIGN(sizeof(*rec) + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);

	rec = (struct syscall_trace_exit *)perf_trace_buf_prepare(size,
				sys_data->exit_event->event.type, NULL, &rctx);
	if (!rec)
		return;

	rec->nr = syscall_nr;
	rec->ret = syscall_get_return_value(current, regs);
	perf_trace_buf_submit(rec, size, rctx, 0, 1, regs, head, NULL);
}

static int perf_sysexit_enable(struct ftrace_event_call *call)
{
	int ret = 0;
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;

	mutex_lock(&syscall_trace_lock);
	if (!sys_perf_refcount_exit)
		ret = register_trace_sys_exit(perf_syscall_exit, NULL);
	if (ret) {
		pr_info("event trace: Could not activate"
				"syscall exit trace point");
	} else {
		set_bit(num, enabled_perf_exit_syscalls);
		sys_perf_refcount_exit++;
	}
	mutex_unlock(&syscall_trace_lock);
	return ret;
}

static void perf_sysexit_disable(struct ftrace_event_call *call)
{
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;

	mutex_lock(&syscall_trace_lock);
	sys_perf_refcount_exit--;
	clear_bit(num, enabled_perf_exit_syscalls);
	if (!sys_perf_refcount_exit)
		unregister_trace_sys_exit(perf_syscall_exit, NULL);
	mutex_unlock(&syscall_trace_lock);
}

#endif /* CONFIG_PERF_EVENTS */

static int syscall_enter_register(struct ftrace_event_call *event,
				 enum trace_reg type, void *data)
{
	struct ftrace_event_file *file = data;

	switch (type) {
	case TRACE_REG_REGISTER:
		return reg_event_syscall_enter(file, event);
	case TRACE_REG_UNREGISTER:
		unreg_event_syscall_enter(file, event);
		return 0;

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		return perf_sysenter_enable(event);
	case TRACE_REG_PERF_UNREGISTER:
		perf_sysenter_disable(event);
		return 0;
	case TRACE_REG_PERF_OPEN:
	case TRACE_REG_PERF_CLOSE:
	case TRACE_REG_PERF_ADD:
	case TRACE_REG_PERF_DEL:
		return 0;
#endif
	}
	return 0;
}

static int syscall_exit_register(struct ftrace_event_call *event,
				 enum trace_reg type, void *data)
{
	struct ftrace_event_file *file = data;

	switch (type) {
	case TRACE_REG_REGISTER:
		return reg_event_syscall_exit(file, event);
	case TRACE_REG_UNREGISTER:
		unreg_event_syscall_exit(file, event);
		return 0;

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		return perf_sysexit_enable(event);
	case TRACE_REG_PERF_UNREGISTER:
		perf_sysexit_disable(event);
		return 0;
	case TRACE_REG_PERF_OPEN:
	case TRACE_REG_PERF_CLOSE:
	case TRACE_REG_PERF_ADD:
	case TRACE_REG_PERF_DEL:
		return 0;
#endif
	}
	return 0;
}
