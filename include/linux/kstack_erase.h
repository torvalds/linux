/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KSTACK_ERASE_H
#define _LINUX_KSTACK_ERASE_H

#include <linux/sched.h>
#include <linux/sched/task_stack.h>

/*
 * Check that the poison value points to the unused hole in the
 * virtual memory map for your platform.
 */
#define KSTACK_ERASE_POISON -0xBEEF
#define KSTACK_ERASE_SEARCH_DEPTH 128

#ifdef CONFIG_KSTACK_ERASE
#include <asm/stacktrace.h>
#include <linux/linkage.h>

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
	const unsigned int depth = KSTACK_ERASE_SEARCH_DEPTH / sizeof(unsigned long);
	unsigned int poison_count = 0;
	unsigned long poison_high = high;
	unsigned long sp = high;

	while (sp > low && poison_count < depth) {
		sp -= sizeof(unsigned long);

		if (*(unsigned long *)sp == KSTACK_ERASE_POISON) {
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
# ifdef CONFIG_KSTACK_ERASE_METRICS
	t->prev_lowest_stack = t->lowest_stack;
# endif
}

asmlinkage void noinstr stackleak_erase(void);
asmlinkage void noinstr stackleak_erase_on_task_stack(void);
asmlinkage void noinstr stackleak_erase_off_task_stack(void);
void __no_caller_saved_registers noinstr __sanitizer_cov_stack_depth(void);

#else /* !CONFIG_KSTACK_ERASE */
static inline void stackleak_task_init(struct task_struct *t) { }
#endif

#endif
