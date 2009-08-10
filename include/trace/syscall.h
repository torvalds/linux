#ifndef _TRACE_SYSCALL_H
#define _TRACE_SYSCALL_H

#include <linux/tracepoint.h>
#include <linux/unistd.h>
#include <linux/ftrace_event.h>

#include <asm/ptrace.h>


extern void syscall_regfunc(void);
extern void syscall_unregfunc(void);

DECLARE_TRACE_WITH_CALLBACK(syscall_enter,
	TP_PROTO(struct pt_regs *regs, long id),
	TP_ARGS(regs, id),
	syscall_regfunc,
	syscall_unregfunc
);

DECLARE_TRACE_WITH_CALLBACK(syscall_exit,
	TP_PROTO(struct pt_regs *regs, long ret),
	TP_ARGS(regs, ret),
	syscall_regfunc,
	syscall_unregfunc
);

/*
 * A syscall entry in the ftrace syscalls array.
 *
 * @name: name of the syscall
 * @nb_args: number of parameters it takes
 * @types: list of types as strings
 * @args: list of args as strings (args[i] matches types[i])
 */
struct syscall_metadata {
	const char	*name;
	int		nb_args;
	const char	**types;
	const char	**args;
};

#ifdef CONFIG_FTRACE_SYSCALLS
extern struct syscall_metadata *syscall_nr_to_meta(int nr);
extern int syscall_name_to_nr(char *name);
extern struct trace_event event_syscall_enter;
extern struct trace_event event_syscall_exit;
extern int reg_event_syscall_enter(void *ptr);
extern void unreg_event_syscall_enter(void *ptr);
extern int reg_event_syscall_exit(void *ptr);
extern void unreg_event_syscall_exit(void *ptr);
#endif

#endif /* _TRACE_SYSCALL_H */
