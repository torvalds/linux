#ifndef __LINUX_STACKTRACE_H
#define __LINUX_STACKTRACE_H

#ifdef CONFIG_STACKTRACE
struct stack_trace {
	unsigned int nr_entries, max_entries;
	unsigned long *entries;
};

extern void save_stack_trace(struct stack_trace *trace,
			     struct task_struct *task, int all_contexts,
			     unsigned int skip);

extern void print_stack_trace(struct stack_trace *trace, int spaces);
#else
# define save_stack_trace(trace, task, all, skip)	do { } while (0)
# define print_stack_trace(trace)			do { } while (0)
#endif

#endif
