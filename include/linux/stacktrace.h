#ifndef __LINUX_STACKTRACE_H
#define __LINUX_STACKTRACE_H

#ifdef CONFIG_STACKTRACE
struct stack_trace {
	unsigned int nr_entries, max_entries;
	unsigned long *entries;
	int skip;	/* input argument: How many entries to skip */
	int all_contexts; /* input argument: if true do than one stack */
};

extern void save_stack_trace(struct stack_trace *trace,
			     struct task_struct *task);

extern void print_stack_trace(struct stack_trace *trace, int spaces);
#else
# define save_stack_trace(trace, task)			do { } while (0)
# define print_stack_trace(trace)			do { } while (0)
#endif

#endif
