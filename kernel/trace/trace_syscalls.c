#include <trace/syscall.h>
#include <trace/events/syscalls.h>
#include <linux/kernel.h>
#include <linux/ftrace.h>
#include <linux/perf_event.h>
#include <asm/syscall.h>

#include "trace_output.h"
#include "trace.h"

static DEFINE_MUTEX(syscall_trace_lock);
static int sys_refcount_enter;
static int sys_refcount_exit;
static DECLARE_BITMAP(enabled_enter_syscalls, NR_syscalls);
static DECLARE_BITMAP(enabled_exit_syscalls, NR_syscalls);

enum print_line_t
print_syscall_enter(struct trace_iterator *iter, int flags)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *ent = iter->ent;
	struct syscall_trace_enter *trace;
	struct syscall_metadata *entry;
	int i, ret, syscall;

	trace = (typeof(trace))ent;
	syscall = trace->nr;
	entry = syscall_nr_to_meta(syscall);

	if (!entry)
		goto end;

	if (entry->enter_id != ent->type) {
		WARN_ON_ONCE(1);
		goto end;
	}

	ret = trace_seq_printf(s, "%s(", entry->name);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	for (i = 0; i < entry->nb_args; i++) {
		/* parameter types */
		if (trace_flags & TRACE_ITER_VERBOSE) {
			ret = trace_seq_printf(s, "%s ", entry->types[i]);
			if (!ret)
				return TRACE_TYPE_PARTIAL_LINE;
		}
		/* parameter values */
		ret = trace_seq_printf(s, "%s: %lx%s", entry->args[i],
				       trace->args[i],
				       i == entry->nb_args - 1 ? "" : ", ");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

	ret = trace_seq_putc(s, ')');
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

end:
	ret =  trace_seq_putc(s, '\n');
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

enum print_line_t
print_syscall_exit(struct trace_iterator *iter, int flags)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *ent = iter->ent;
	struct syscall_trace_exit *trace;
	int syscall;
	struct syscall_metadata *entry;
	int ret;

	trace = (typeof(trace))ent;
	syscall = trace->nr;
	entry = syscall_nr_to_meta(syscall);

	if (!entry) {
		trace_seq_printf(s, "\n");
		return TRACE_TYPE_HANDLED;
	}

	if (entry->exit_id != ent->type) {
		WARN_ON_ONCE(1);
		return TRACE_TYPE_UNHANDLED;
	}

	ret = trace_seq_printf(s, "%s -> 0x%lx\n", entry->name,
				trace->ret);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

extern char *__bad_type_size(void);

#define SYSCALL_FIELD(type, name)					\
	sizeof(type) != sizeof(trace.name) ?				\
		__bad_type_size() :					\
		#type, #name, offsetof(typeof(trace), name), sizeof(trace.name)

int syscall_enter_format(struct ftrace_event_call *call, struct trace_seq *s)
{
	int i;
	int nr;
	int ret;
	struct syscall_metadata *entry;
	struct syscall_trace_enter trace;
	int offset = offsetof(struct syscall_trace_enter, args);

	nr = syscall_name_to_nr(call->data);
	entry = syscall_nr_to_meta(nr);

	if (!entry)
		return 0;

	ret = trace_seq_printf(s, "\tfield:%s %s;\toffset:%zu;\tsize:%zu;\n",
			       SYSCALL_FIELD(int, nr));
	if (!ret)
		return 0;

	for (i = 0; i < entry->nb_args; i++) {
		ret = trace_seq_printf(s, "\tfield:%s %s;", entry->types[i],
				        entry->args[i]);
		if (!ret)
			return 0;
		ret = trace_seq_printf(s, "\toffset:%d;\tsize:%zu;\n", offset,
				       sizeof(unsigned long));
		if (!ret)
			return 0;
		offset += sizeof(unsigned long);
	}

	trace_seq_puts(s, "\nprint fmt: \"");
	for (i = 0; i < entry->nb_args; i++) {
		ret = trace_seq_printf(s, "%s: 0x%%0%zulx%s", entry->args[i],
				        sizeof(unsigned long),
					i == entry->nb_args - 1 ? "" : ", ");
		if (!ret)
			return 0;
	}
	trace_seq_putc(s, '"');

	for (i = 0; i < entry->nb_args; i++) {
		ret = trace_seq_printf(s, ", ((unsigned long)(REC->%s))",
				       entry->args[i]);
		if (!ret)
			return 0;
	}

	return trace_seq_putc(s, '\n');
}

int syscall_exit_format(struct ftrace_event_call *call, struct trace_seq *s)
{
	int ret;
	struct syscall_trace_exit trace;

	ret = trace_seq_printf(s,
			       "\tfield:%s %s;\toffset:%zu;\tsize:%zu;\n"
			       "\tfield:%s %s;\toffset:%zu;\tsize:%zu;\n",
			       SYSCALL_FIELD(int, nr),
			       SYSCALL_FIELD(long, ret));
	if (!ret)
		return 0;

	return trace_seq_printf(s, "\nprint fmt: \"0x%%lx\", REC->ret\n");
}

int syscall_enter_define_fields(struct ftrace_event_call *call)
{
	struct syscall_trace_enter trace;
	struct syscall_metadata *meta;
	int ret;
	int nr;
	int i;
	int offset = offsetof(typeof(trace), args);

	nr = syscall_name_to_nr(call->data);
	meta = syscall_nr_to_meta(nr);

	if (!meta)
		return 0;

	ret = trace_define_common_fields(call);
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

int syscall_exit_define_fields(struct ftrace_event_call *call)
{
	struct syscall_trace_exit trace;
	int ret;

	ret = trace_define_common_fields(call);
	if (ret)
		return ret;

	ret = trace_define_field(call, SYSCALL_FIELD(long, ret), 0,
				 FILTER_OTHER);

	return ret;
}

void ftrace_syscall_enter(struct pt_regs *regs, long id)
{
	struct syscall_trace_enter *entry;
	struct syscall_metadata *sys_data;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	int size;
	int syscall_nr;

	syscall_nr = syscall_get_nr(current, regs);
	if (syscall_nr < 0)
		return;
	if (!test_bit(syscall_nr, enabled_enter_syscalls))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	size = sizeof(*entry) + sizeof(unsigned long) * sys_data->nb_args;

	event = trace_current_buffer_lock_reserve(&buffer, sys_data->enter_id,
						  size, 0, 0);
	if (!event)
		return;

	entry = ring_buffer_event_data(event);
	entry->nr = syscall_nr;
	syscall_get_arguments(current, regs, 0, sys_data->nb_args, entry->args);

	if (!filter_current_check_discard(buffer, sys_data->enter_event,
					  entry, event))
		trace_current_buffer_unlock_commit(buffer, event, 0, 0);
}

void ftrace_syscall_exit(struct pt_regs *regs, long ret)
{
	struct syscall_trace_exit *entry;
	struct syscall_metadata *sys_data;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	int syscall_nr;

	syscall_nr = syscall_get_nr(current, regs);
	if (syscall_nr < 0)
		return;
	if (!test_bit(syscall_nr, enabled_exit_syscalls))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	event = trace_current_buffer_lock_reserve(&buffer, sys_data->exit_id,
				sizeof(*entry), 0, 0);
	if (!event)
		return;

	entry = ring_buffer_event_data(event);
	entry->nr = syscall_nr;
	entry->ret = syscall_get_return_value(current, regs);

	if (!filter_current_check_discard(buffer, sys_data->exit_event,
					  entry, event))
		trace_current_buffer_unlock_commit(buffer, event, 0, 0);
}

int reg_event_syscall_enter(void *ptr)
{
	int ret = 0;
	int num;
	char *name;

	name = (char *)ptr;
	num = syscall_name_to_nr(name);
	if (num < 0 || num >= NR_syscalls)
		return -ENOSYS;
	mutex_lock(&syscall_trace_lock);
	if (!sys_refcount_enter)
		ret = register_trace_sys_enter(ftrace_syscall_enter);
	if (ret) {
		pr_info("event trace: Could not activate"
				"syscall entry trace point");
	} else {
		set_bit(num, enabled_enter_syscalls);
		sys_refcount_enter++;
	}
	mutex_unlock(&syscall_trace_lock);
	return ret;
}

void unreg_event_syscall_enter(void *ptr)
{
	int num;
	char *name;

	name = (char *)ptr;
	num = syscall_name_to_nr(name);
	if (num < 0 || num >= NR_syscalls)
		return;
	mutex_lock(&syscall_trace_lock);
	sys_refcount_enter--;
	clear_bit(num, enabled_enter_syscalls);
	if (!sys_refcount_enter)
		unregister_trace_sys_enter(ftrace_syscall_enter);
	mutex_unlock(&syscall_trace_lock);
}

int reg_event_syscall_exit(void *ptr)
{
	int ret = 0;
	int num;
	char *name;

	name = (char *)ptr;
	num = syscall_name_to_nr(name);
	if (num < 0 || num >= NR_syscalls)
		return -ENOSYS;
	mutex_lock(&syscall_trace_lock);
	if (!sys_refcount_exit)
		ret = register_trace_sys_exit(ftrace_syscall_exit);
	if (ret) {
		pr_info("event trace: Could not activate"
				"syscall exit trace point");
	} else {
		set_bit(num, enabled_exit_syscalls);
		sys_refcount_exit++;
	}
	mutex_unlock(&syscall_trace_lock);
	return ret;
}

void unreg_event_syscall_exit(void *ptr)
{
	int num;
	char *name;

	name = (char *)ptr;
	num = syscall_name_to_nr(name);
	if (num < 0 || num >= NR_syscalls)
		return;
	mutex_lock(&syscall_trace_lock);
	sys_refcount_exit--;
	clear_bit(num, enabled_exit_syscalls);
	if (!sys_refcount_exit)
		unregister_trace_sys_exit(ftrace_syscall_exit);
	mutex_unlock(&syscall_trace_lock);
}

struct trace_event event_syscall_enter = {
	.trace			= print_syscall_enter,
};

struct trace_event event_syscall_exit = {
	.trace			= print_syscall_exit,
};

#ifdef CONFIG_EVENT_PROFILE

static DECLARE_BITMAP(enabled_prof_enter_syscalls, NR_syscalls);
static DECLARE_BITMAP(enabled_prof_exit_syscalls, NR_syscalls);
static int sys_prof_refcount_enter;
static int sys_prof_refcount_exit;

static void prof_syscall_enter(struct pt_regs *regs, long id)
{
	struct syscall_metadata *sys_data;
	struct syscall_trace_enter *rec;
	unsigned long flags;
	char *raw_data;
	int syscall_nr;
	int size;
	int cpu;

	syscall_nr = syscall_get_nr(current, regs);
	if (!test_bit(syscall_nr, enabled_prof_enter_syscalls))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	/* get the size after alignment with the u32 buffer size field */
	size = sizeof(unsigned long) * sys_data->nb_args + sizeof(*rec);
	size = ALIGN(size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);

	if (WARN_ONCE(size > FTRACE_MAX_PROFILE_SIZE,
		      "profile buffer not large enough"))
		return;

	/* Protect the per cpu buffer, begin the rcu read side */
	local_irq_save(flags);

	cpu = smp_processor_id();

	if (in_nmi())
		raw_data = rcu_dereference(trace_profile_buf_nmi);
	else
		raw_data = rcu_dereference(trace_profile_buf);

	if (!raw_data)
		goto end;

	raw_data = per_cpu_ptr(raw_data, cpu);

	/* zero the dead bytes from align to not leak stack to user */
	*(u64 *)(&raw_data[size - sizeof(u64)]) = 0ULL;

	rec = (struct syscall_trace_enter *) raw_data;
	tracing_generic_entry_update(&rec->ent, 0, 0);
	rec->ent.type = sys_data->enter_id;
	rec->nr = syscall_nr;
	syscall_get_arguments(current, regs, 0, sys_data->nb_args,
			       (unsigned long *)&rec->args);
	perf_tp_event(sys_data->enter_id, 0, 1, rec, size);

end:
	local_irq_restore(flags);
}

int reg_prof_syscall_enter(char *name)
{
	int ret = 0;
	int num;

	num = syscall_name_to_nr(name);
	if (num < 0 || num >= NR_syscalls)
		return -ENOSYS;

	mutex_lock(&syscall_trace_lock);
	if (!sys_prof_refcount_enter)
		ret = register_trace_sys_enter(prof_syscall_enter);
	if (ret) {
		pr_info("event trace: Could not activate"
				"syscall entry trace point");
	} else {
		set_bit(num, enabled_prof_enter_syscalls);
		sys_prof_refcount_enter++;
	}
	mutex_unlock(&syscall_trace_lock);
	return ret;
}

void unreg_prof_syscall_enter(char *name)
{
	int num;

	num = syscall_name_to_nr(name);
	if (num < 0 || num >= NR_syscalls)
		return;

	mutex_lock(&syscall_trace_lock);
	sys_prof_refcount_enter--;
	clear_bit(num, enabled_prof_enter_syscalls);
	if (!sys_prof_refcount_enter)
		unregister_trace_sys_enter(prof_syscall_enter);
	mutex_unlock(&syscall_trace_lock);
}

static void prof_syscall_exit(struct pt_regs *regs, long ret)
{
	struct syscall_metadata *sys_data;
	struct syscall_trace_exit *rec;
	unsigned long flags;
	int syscall_nr;
	char *raw_data;
	int size;
	int cpu;

	syscall_nr = syscall_get_nr(current, regs);
	if (!test_bit(syscall_nr, enabled_prof_exit_syscalls))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	/* We can probably do that at build time */
	size = ALIGN(sizeof(*rec) + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);

	/*
	 * Impossible, but be paranoid with the future
	 * How to put this check outside runtime?
	 */
	if (WARN_ONCE(size > FTRACE_MAX_PROFILE_SIZE,
		"exit event has grown above profile buffer size"))
		return;

	/* Protect the per cpu buffer, begin the rcu read side */
	local_irq_save(flags);
	cpu = smp_processor_id();

	if (in_nmi())
		raw_data = rcu_dereference(trace_profile_buf_nmi);
	else
		raw_data = rcu_dereference(trace_profile_buf);

	if (!raw_data)
		goto end;

	raw_data = per_cpu_ptr(raw_data, cpu);

	/* zero the dead bytes from align to not leak stack to user */
	*(u64 *)(&raw_data[size - sizeof(u64)]) = 0ULL;

	rec = (struct syscall_trace_exit *)raw_data;

	tracing_generic_entry_update(&rec->ent, 0, 0);
	rec->ent.type = sys_data->exit_id;
	rec->nr = syscall_nr;
	rec->ret = syscall_get_return_value(current, regs);

	perf_tp_event(sys_data->exit_id, 0, 1, rec, size);

end:
	local_irq_restore(flags);
}

int reg_prof_syscall_exit(char *name)
{
	int ret = 0;
	int num;

	num = syscall_name_to_nr(name);
	if (num < 0 || num >= NR_syscalls)
		return -ENOSYS;

	mutex_lock(&syscall_trace_lock);
	if (!sys_prof_refcount_exit)
		ret = register_trace_sys_exit(prof_syscall_exit);
	if (ret) {
		pr_info("event trace: Could not activate"
				"syscall entry trace point");
	} else {
		set_bit(num, enabled_prof_exit_syscalls);
		sys_prof_refcount_exit++;
	}
	mutex_unlock(&syscall_trace_lock);
	return ret;
}

void unreg_prof_syscall_exit(char *name)
{
	int num;

	num = syscall_name_to_nr(name);
	if (num < 0 || num >= NR_syscalls)
		return;

	mutex_lock(&syscall_trace_lock);
	sys_prof_refcount_exit--;
	clear_bit(num, enabled_prof_exit_syscalls);
	if (!sys_prof_refcount_exit)
		unregister_trace_sys_exit(prof_syscall_exit);
	mutex_unlock(&syscall_trace_lock);
}

#endif


