/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_STACKTRACE_H
#define __LINUX_STACKTRACE_H

#include <linux/types.h>
#include <asm/errno.h>

struct task_struct;
struct pt_regs;

#ifdef CONFIG_STACKTRACE
void stack_trace_print(unsigned long *trace, unsigned int nr_entries,
		       int spaces);
int stack_trace_snprint(char *buf, size_t size, unsigned long *entries,
			unsigned int nr_entries, int spaces);
unsigned int stack_trace_save(unsigned long *store, unsigned int size,
			      unsigned int skipnr);
unsigned int stack_trace_save_tsk(struct task_struct *task,
				  unsigned long *store, unsigned int size,
				  unsigned int skipnr);
unsigned int stack_trace_save_regs(struct pt_regs *regs, unsigned long *store,
				   unsigned int size, unsigned int skipnr);
unsigned int stack_trace_save_user(unsigned long *store, unsigned int size);

/* Internal interfaces. Do not use in generic code */
struct stack_trace {
	unsigned int nr_entries, max_entries;
	unsigned long *entries;
	int skip;	/* input argument: How many entries to skip */
};

extern void save_stack_trace(struct stack_trace *trace);
extern void save_stack_trace_regs(struct pt_regs *regs,
				  struct stack_trace *trace);
extern void save_stack_trace_tsk(struct task_struct *tsk,
				struct stack_trace *trace);
extern int save_stack_trace_tsk_reliable(struct task_struct *tsk,
					 struct stack_trace *trace);

extern void print_stack_trace(struct stack_trace *trace, int spaces);
extern int snprint_stack_trace(char *buf, size_t size,
			struct stack_trace *trace, int spaces);

#ifdef CONFIG_USER_STACKTRACE_SUPPORT
extern void save_stack_trace_user(struct stack_trace *trace);
#else
# define save_stack_trace_user(trace)              do { } while (0)
#endif

#else /* !CONFIG_STACKTRACE */
# define save_stack_trace(trace)			do { } while (0)
# define save_stack_trace_tsk(tsk, trace)		do { } while (0)
# define save_stack_trace_user(trace)			do { } while (0)
# define print_stack_trace(trace, spaces)		do { } while (0)
# define snprint_stack_trace(buf, size, trace, spaces)	do { } while (0)
# define save_stack_trace_tsk_reliable(tsk, trace)	({ -ENOSYS; })
#endif /* CONFIG_STACKTRACE */

#if defined(CONFIG_STACKTRACE) && defined(CONFIG_HAVE_RELIABLE_STACKTRACE)
int stack_trace_save_tsk_reliable(struct task_struct *tsk, unsigned long *store,
				  unsigned int size);
#else
static inline int stack_trace_save_tsk_reliable(struct task_struct *tsk,
						unsigned long *store,
						unsigned int size)
{
	return -ENOSYS;
}
#endif

#endif /* __LINUX_STACKTRACE_H */
