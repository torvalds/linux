#include <trace/syscall.h>
#include <linux/kernel.h>
#include <linux/ftrace.h>
#include <asm/syscall.h>

#include "trace_output.h"
#include "trace.h"

static DEFINE_MUTEX(syscall_trace_lock);
static int sys_refcount_enter;
static int sys_refcount_exit;
static DECLARE_BITMAP(enabled_enter_syscalls, FTRACE_SYSCALL_MAX);
static DECLARE_BITMAP(enabled_exit_syscalls, FTRACE_SYSCALL_MAX);

/* Option to display the parameters types */
enum {
	TRACE_SYSCALLS_OPT_TYPES = 0x1,
};

static struct tracer_opt syscalls_opts[] = {
	{ TRACER_OPT(syscall_arg_type, TRACE_SYSCALLS_OPT_TYPES) },
	{ }
};

static struct tracer_flags syscalls_flags = {
	.val = 0, /* By default: no parameters types */
	.opts = syscalls_opts
};

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
		if (syscalls_flags.val & TRACE_SYSCALLS_OPT_TYPES) {
			ret = trace_seq_printf(s, "%s ", entry->types[i]);
			if (!ret)
				return TRACE_TYPE_PARTIAL_LINE;
		}
		/* parameter values */
		ret = trace_seq_printf(s, "%s: %lx%s ", entry->args[i],
				       trace->args[i],
				       i == entry->nb_args - 1 ? ")" : ",");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
	}

end:
	trace_seq_printf(s, "\n");
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

void ftrace_syscall_enter(struct pt_regs *regs, long id)
{
	struct syscall_trace_enter *entry;
	struct syscall_metadata *sys_data;
	struct ring_buffer_event *event;
	int size;
	int syscall_nr;

	syscall_nr = syscall_get_nr(current, regs);
	if (!test_bit(syscall_nr, enabled_enter_syscalls))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	size = sizeof(*entry) + sizeof(unsigned long) * sys_data->nb_args;

	event = trace_current_buffer_lock_reserve(sys_data->enter_id, size,
							0, 0);
	if (!event)
		return;

	entry = ring_buffer_event_data(event);
	entry->nr = syscall_nr;
	syscall_get_arguments(current, regs, 0, sys_data->nb_args, entry->args);

	trace_current_buffer_unlock_commit(event, 0, 0);
	trace_wake_up();
}

void ftrace_syscall_exit(struct pt_regs *regs, long ret)
{
	struct syscall_trace_exit *entry;
	struct syscall_metadata *sys_data;
	struct ring_buffer_event *event;
	int syscall_nr;

	syscall_nr = syscall_get_nr(current, regs);
	if (!test_bit(syscall_nr, enabled_exit_syscalls))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	event = trace_current_buffer_lock_reserve(sys_data->exit_id,
				sizeof(*entry), 0, 0);
	if (!event)
		return;

	entry = ring_buffer_event_data(event);
	entry->nr = syscall_nr;
	entry->ret = syscall_get_return_value(current, regs);

	trace_current_buffer_unlock_commit(event, 0, 0);
	trace_wake_up();
}

int reg_event_syscall_enter(void *ptr)
{
	int ret = 0;
	int num;
	char *name;

	name = (char *)ptr;
	num = syscall_name_to_nr(name);
	if (num < 0 || num >= FTRACE_SYSCALL_MAX)
		return -ENOSYS;
	mutex_lock(&syscall_trace_lock);
	if (!sys_refcount_enter)
		ret = register_trace_syscall_enter(ftrace_syscall_enter);
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
	if (num < 0 || num >= FTRACE_SYSCALL_MAX)
		return;
	mutex_lock(&syscall_trace_lock);
	sys_refcount_enter--;
	clear_bit(num, enabled_enter_syscalls);
	if (!sys_refcount_enter)
		unregister_trace_syscall_enter(ftrace_syscall_enter);
	mutex_unlock(&syscall_trace_lock);
}

int reg_event_syscall_exit(void *ptr)
{
	int ret = 0;
	int num;
	char *name;

	name = (char *)ptr;
	num = syscall_name_to_nr(name);
	if (num < 0 || num >= FTRACE_SYSCALL_MAX)
		return -ENOSYS;
	mutex_lock(&syscall_trace_lock);
	if (!sys_refcount_exit)
		ret = register_trace_syscall_exit(ftrace_syscall_exit);
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
	if (num < 0 || num >= FTRACE_SYSCALL_MAX)
		return;
	mutex_lock(&syscall_trace_lock);
	sys_refcount_exit--;
	clear_bit(num, enabled_exit_syscalls);
	if (!sys_refcount_exit)
		unregister_trace_syscall_exit(ftrace_syscall_exit);
	mutex_unlock(&syscall_trace_lock);
}

struct trace_event event_syscall_enter = {
	.trace			= print_syscall_enter,
};

struct trace_event event_syscall_exit = {
	.trace			= print_syscall_exit,
};
