/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_STACKTRACE_H
#define __LINUX_STACKTRACE_H

#include <linux/types.h>
#include <asm/errno.h>

struct task_struct;
struct pt_regs;

#ifdef CONFIG_STACKTRACE
void stack_trace_print(const unsigned long *trace, unsigned int nr_entries,
		       int spaces);
int stack_trace_snprint(char *buf, size_t size, const unsigned long *entries,
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
#ifdef CONFIG_ARCH_STACKWALK

/**
 * stack_trace_consume_fn - Callback for arch_stack_walk()
 * @cookie:	Caller supplied pointer handed back by arch_stack_walk()
 * @addr:	The stack entry address to consume
 * @reliable:	True when the stack entry is reliable. Required by
 *		some printk based consumers.
 *
 * Return:	True, if the entry was consumed or skipped
 *		False, if there is no space left to store
 */
typedef bool (*stack_trace_consume_fn)(void *cookie, unsigned long addr,
				       bool reliable);
/**
 * arch_stack_walk - Architecture specific function to walk the stack
 * @consume_entry:	Callback which is invoked by the architecture code for
 *			each entry.
 * @cookie:		Caller supplied pointer which is handed back to
 *			@consume_entry
 * @task:		Pointer to a task struct, can be NULL
 * @regs:		Pointer to registers, can be NULL
 *
 * ============ ======= ============================================
 * task	        regs
 * ============ ======= ============================================
 * task		NULL	Stack trace from task (can be current)
 * current	regs	Stack trace starting on regs->stackpointer
 * ============ ======= ============================================
 */
void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs);
int arch_stack_walk_reliable(stack_trace_consume_fn consume_entry, void *cookie,
			     struct task_struct *task);
void arch_stack_walk_user(stack_trace_consume_fn consume_entry, void *cookie,
			  const struct pt_regs *regs);

#else /* CONFIG_ARCH_STACKWALK */
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
extern void save_stack_trace_user(struct stack_trace *trace);
#endif /* !CONFIG_ARCH_STACKWALK */
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
