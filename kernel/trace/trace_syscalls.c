#include <linux/ftrace.h>
#include <linux/kernel.h>

#include <asm/syscall.h>

#include "trace_output.h"
#include "trace.h"

static atomic_t refcount;

void start_ftrace_syscalls(void)
{
	unsigned long flags;
	struct task_struct *g, *t;

	if (atomic_inc_return(&refcount) != 1)
		goto out;

	read_lock_irqsave(&tasklist_lock, flags);

	do_each_thread(g, t) {
		set_tsk_thread_flag(t, TIF_SYSCALL_FTRACE);
	} while_each_thread(g, t);

	read_unlock_irqrestore(&tasklist_lock, flags);
out:
	atomic_dec(&refcount);
}

void stop_ftrace_syscalls(void)
{
	unsigned long flags;
	struct task_struct *g, *t;

	if (atomic_dec_return(&refcount))
		goto out;

	read_lock_irqsave(&tasklist_lock, flags);

	do_each_thread(g, t) {
		clear_tsk_thread_flag(t, TIF_SYSCALL_FTRACE);
	} while_each_thread(g, t);

	read_unlock_irqrestore(&tasklist_lock, flags);
out:
	atomic_inc(&refcount);
}

void ftrace_syscall_enter(struct pt_regs *regs)
{
	int syscall_nr;

	syscall_nr = syscall_get_nr(current, regs);

	trace_printk("syscall %d enter\n", syscall_nr);
}

void ftrace_syscall_exit(struct pt_regs *regs)
{
	int syscall_nr;

	syscall_nr = syscall_get_nr(current, regs);

	trace_printk("syscall %d exit\n", syscall_nr);
}

static int init_syscall_tracer(struct trace_array *tr)
{
	start_ftrace_syscalls();

	return 0;
}

static void reset_syscall_tracer(struct trace_array *tr)
{
	stop_ftrace_syscalls();
}

static struct trace_event syscall_enter_event = {
	.type		= TRACE_SYSCALL_ENTER,
};

static struct trace_event syscall_exit_event = {
	.type		= TRACE_SYSCALL_EXIT,
};

static struct tracer syscall_tracer __read_mostly = {
	.name		= "syscall",
	.init		= init_syscall_tracer,
	.reset		= reset_syscall_tracer
};

__init int register_ftrace_syscalls(void)
{
	int ret;

	ret = register_ftrace_event(&syscall_enter_event);
	if (!ret) {
		printk(KERN_WARNING "event %d failed to register\n",
		       syscall_enter_event.type);
		WARN_ON_ONCE(1);
	}

	ret = register_ftrace_event(&syscall_exit_event);
	if (!ret) {
		printk(KERN_WARNING "event %d failed to register\n",
		       syscall_exit_event.type);
		WARN_ON_ONCE(1);
	}

	return register_tracer(&syscall_tracer);
}
device_initcall(register_ftrace_syscalls);
