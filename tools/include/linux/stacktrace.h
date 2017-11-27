/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LIBLOCKDEP_LINUX_STACKTRACE_H_
#define _LIBLOCKDEP_LINUX_STACKTRACE_H_

#include <execinfo.h>

struct stack_trace {
	unsigned int nr_entries, max_entries;
	unsigned long *entries;
	int skip;
};

static inline void print_stack_trace(struct stack_trace *trace, int spaces)
{
	backtrace_symbols_fd((void **)trace->entries, trace->nr_entries, 1);
}

#define save_stack_trace(trace)	\
	((trace)->nr_entries =	\
		backtrace((void **)(trace)->entries, (trace)->max_entries))

static inline int dump_stack(void)
{
	void *array[64];
	size_t size;

	size = backtrace(array, 64);
	backtrace_symbols_fd(array, size, 1);

	return 0;
}

#endif
