#ifndef __LINUX_STACKTRACE_H
#define __LINUX_STACKTRACE_H

#ifdef CONFIG_STACKTRACE
struct stack_trace {
	unsigned int nr_entries, max_entries;
	unsigned long *entries;
	int skip;	/* input argument: How many entries to skip */
};

extern void save_stack_trace(struct stack_trace *trace);

extern void print_stack_trace(struct stack_trace *trace, int spaces);
#else
# define save_stack_trace(trace)			do { } while (0)
# define print_stack_trace(trace, spaces)		do { } while (0)
#endif

#endif
