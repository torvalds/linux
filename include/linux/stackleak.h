/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STACKLEAK_H
#define _LINUX_STACKLEAK_H

#include <linux/sched.h>
#include <linux/sched/task_stack.h>

/*
 * Check that the poison value points to the unused hole in the
 * virtual memory map for your platform.
 */
#define STACKLEAK_POISON -0xBEEF
#define STACKLEAK_SEARCH_DEPTH 128

#ifdef CONFIG_GCC_PLUGIN_STACKLEAK
#include <asm/stacktrace.h>

/*
 * The lowest address on tsk's stack which we can plausibly erase.
 */
static __always_inline unsigned long
stackleak_task_low_bound(const struct task_struct *tsk)
{
	/*
	 * The lowest unsigned long on the task stack contains STACK_END_MAGIC,
	 * which we must not corrupt.
	 */
	return (unsigned long)end_of_stack(tsk) + sizeof(unsigned long);
}

/*
 * The address immediately after the highest address on tsk's stack which we
 * can plausibly erase.
 */
static __always_inline unsigned long
stackleak_task_high_bound(const struct task_struct *tsk)
{
	/*
	 * The task's pt_regs lives at the top of the task stack and will be
	 * overwritten by exception entry, so there's no need to erase them.
	 */
	return (unsigned long)task_pt_regs(tsk);
}

/*
 * Find the address immediately above the poisoned region of the stack, where
 * that region falls between 'low' (inclusive) and 'high' (exclusive).
 */
static __always_inline unsigned long
stackleak_find_top_of_poison(const unsigned long low, const unsigned long high)
{
	const unsigned int depth = STACKLEAK_SEARCH_DEPTH / sizeof(unsigned long);
	unsigned int poison_count = 0;
	unsigned long poison_high = high;
	unsigned long sp = high;

	while (sp > low && poison_count < depth) {
		sp -= sizeof(unsigned long);

		if (*(unsigned long *)sp == STACKLEAK_POISON) {
			poison_count++;
		} else {
			poison_count = 0;
			poison_high = sp;
		}
	}

	return poison_high;
}

static inline void stackleak_task_init(struct task_struct *t)
{
	t->lowest_stack = stackleak_task_low_bound(t);
# ifdef CONFIG_STACKLEAK_METRICS
	t->prev_lowest_stack = t->lowest_stack;
# endif
}

#else /* !CONFIG_GCC_PLUGIN_STACKLEAK */
static inline void stackleak_task_init(struct task_struct *t) { }
#endif

#endif
