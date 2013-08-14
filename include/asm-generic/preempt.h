#ifndef __ASM_PREEMPT_H
#define __ASM_PREEMPT_H

#include <linux/thread_info.h>

/*
 * We mask the PREEMPT_NEED_RESCHED bit so as not to confuse all current users
 * that think a non-zero value indicates we cannot preempt.
 */
static __always_inline int preempt_count(void)
{
	return current_thread_info()->preempt_count & ~PREEMPT_NEED_RESCHED;
}

static __always_inline int *preempt_count_ptr(void)
{
	return &current_thread_info()->preempt_count;
}

/*
 * We now loose PREEMPT_NEED_RESCHED and cause an extra reschedule; however the
 * alternative is loosing a reschedule. Better schedule too often -- also this
 * should be a very rare operation.
 */
static __always_inline void preempt_count_set(int pc)
{
	*preempt_count_ptr() = pc;
}

/*
 * must be macros to avoid header recursion hell
 */
#define task_preempt_count(p) \
	(task_thread_info(p)->preempt_count & ~PREEMPT_NEED_RESCHED)

#define init_task_preempt_count(p) do { \
	task_thread_info(p)->preempt_count = PREEMPT_DISABLED; \
} while (0)

#define init_idle_preempt_count(p, cpu) do { \
	task_thread_info(p)->preempt_count = PREEMPT_ENABLED; \
} while (0)

/*
 * We fold the NEED_RESCHED bit into the preempt count such that
 * preempt_enable() can decrement and test for needing to reschedule with a
 * single instruction.
 *
 * We invert the actual bit, so that when the decrement hits 0 we know we both
 * need to resched (the bit is cleared) and can resched (no preempt count).
 */

static __always_inline void set_preempt_need_resched(void)
{
	*preempt_count_ptr() &= ~PREEMPT_NEED_RESCHED;
}

static __always_inline void clear_preempt_need_resched(void)
{
	*preempt_count_ptr() |= PREEMPT_NEED_RESCHED;
}

static __always_inline bool test_preempt_need_resched(void)
{
	return !(*preempt_count_ptr() & PREEMPT_NEED_RESCHED);
}

#endif /* __ASM_PREEMPT_H */
