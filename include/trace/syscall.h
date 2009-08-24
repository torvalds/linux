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
 * @nb_args: number of parameters it takes
 * @types: list of types as strings
 * @args: list of args as strings (args[i] matches types[i])
 * @enter_id: associated ftrace enter event id
 * @exit_id: associated ftrace exit event id
 * @enter_event: associated syscall_enter trace event
 * @exit_event: associated syscall_exit trace event
 */
struct syscall_metadata {
	const char	*name;
	int		nb_args;
	const char	**types;
	const char	**args;
	int		enter_id;
	int		exit_id;

	struct ftrace_event_call *enter_event;
	struct ftrace_event_call *exit_event;
};

#ifdef CONFIG_FTRACE_SYSCALLS
extern struct syscall_metadata *syscall_nr_to_meta(int nr);
extern int syscall_name_to_nr(char *name);
void set_syscall_enter_id(int num, int id);
void set_syscall_exit_id(int num, int id);
extern struct trace_event event_syscall_enter;
extern struct trace_event event_syscall_exit;
extern int reg_event_syscall_enter(void *ptr);
extern void unreg_event_syscall_enter(void *ptr);
extern int reg_event_syscall_exit(void *ptr);
extern void unreg_event_syscall_exit(void *ptr);
extern int syscall_enter_format(struct ftrace_event_call *call,
				struct trace_seq *s);
extern int syscall_exit_format(struct ftrace_event_call *call,
				struct trace_seq *s);
extern int syscall_enter_define_fields(struct ftrace_event_call *call);
extern int syscall_exit_define_fields(struct ftrace_event_call *call);
enum print_line_t print_syscall_enter(struct trace_iterator *iter, int flags);
enum print_line_t print_syscall_exit(struct trace_iterator *iter, int flags);
#endif
#ifdef CONFIG_EVENT_PROFILE
int reg_prof_syscall_enter(char *name);
void unreg_prof_syscall_enter(char *name);
int reg_prof_syscall_exit(char *name);
void unreg_prof_syscall_exit(char *name);

#endif

#endif /* _TRACE_SYSCALL_H */
