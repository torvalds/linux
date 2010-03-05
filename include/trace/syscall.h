#ifndef _TRACE_SYSCALL_H
#define _TRACE_SYSCALL_H

#include <linux/tracepoint.h>
#include <linux/unistd.h>
#include <linux/ftrace_event.h>

#include <asm/ptrace.h>


/*
 * A syscall entry in the ftrace syscalls array.
 *
 * @name: name of the syscall
 * @syscall_nr: number of the syscall
 * @nb_args: number of parameters it takes
 * @types: list of types as strings
 * @args: list of args as strings (args[i] matches types[i])
 * @enter_event: associated syscall_enter trace event
 * @exit_event: associated syscall_exit trace event
 */
struct syscall_metadata {
	const char	*name;
	int		syscall_nr;
	int		nb_args;
	const char	**types;
	const char	**args;

	struct ftrace_event_call *enter_event;
	struct ftrace_event_call *exit_event;
};

#ifdef CONFIG_FTRACE_SYSCALLS
extern unsigned long arch_syscall_addr(int nr);
extern int init_syscall_trace(struct ftrace_event_call *call);

extern int syscall_enter_define_fields(struct ftrace_event_call *call);
extern int syscall_exit_define_fields(struct ftrace_event_call *call);
extern int reg_event_syscall_enter(struct ftrace_event_call *call);
extern void unreg_event_syscall_enter(struct ftrace_event_call *call);
extern int reg_event_syscall_exit(struct ftrace_event_call *call);
extern void unreg_event_syscall_exit(struct ftrace_event_call *call);
extern int
ftrace_format_syscall(struct ftrace_event_call *call, struct trace_seq *s);
enum print_line_t print_syscall_enter(struct trace_iterator *iter, int flags);
enum print_line_t print_syscall_exit(struct trace_iterator *iter, int flags);
#endif

#ifdef CONFIG_PERF_EVENTS
int perf_sysenter_enable(struct ftrace_event_call *call);
void perf_sysenter_disable(struct ftrace_event_call *call);
int perf_sysexit_enable(struct ftrace_event_call *call);
void perf_sysexit_disable(struct ftrace_event_call *call);
#endif

#endif /* _TRACE_SYSCALL_H */
