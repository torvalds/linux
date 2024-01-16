/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TRACE_SYSCALL_H
#define _TRACE_SYSCALL_H

#include <linux/tracepoint.h>
#include <linux/unistd.h>
#include <linux/trace_events.h>
#include <linux/thread_info.h>

#include <asm/ptrace.h>


/*
 * A syscall entry in the ftrace syscalls array.
 *
 * @name: name of the syscall
 * @syscall_nr: number of the syscall
 * @nb_args: number of parameters it takes
 * @types: list of types as strings
 * @args: list of args as strings (args[i] matches types[i])
 * @enter_fields: list of fields for syscall_enter trace event
 * @enter_event: associated syscall_enter trace event
 * @exit_event: associated syscall_exit trace event
 */
struct syscall_metadata {
	const char	*name;
	int		syscall_nr;
	int		nb_args;
	const char	**types;
	const char	**args;
	struct list_head enter_fields;

	struct trace_event_call *enter_event;
	struct trace_event_call *exit_event;
};

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_HAVE_SYSCALL_TRACEPOINTS)
static inline void syscall_tracepoint_update(struct task_struct *p)
{
	if (test_syscall_work(SYSCALL_TRACEPOINT))
		set_task_syscall_work(p, SYSCALL_TRACEPOINT);
	else
		clear_task_syscall_work(p, SYSCALL_TRACEPOINT);
}
#else
static inline void syscall_tracepoint_update(struct task_struct *p)
{
}
#endif

#endif /* _TRACE_SYSCALL_H */
