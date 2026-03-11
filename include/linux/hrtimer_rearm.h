// SPDX-License-Identifier: GPL-2.0
#ifndef _LINUX_HRTIMER_REARM_H
#define _LINUX_HRTIMER_REARM_H

#ifdef CONFIG_HRTIMER_REARM_DEFERRED
#include <linux/thread_info.h>

void __hrtimer_rearm_deferred(void);

/*
 * This is purely CPU local, so check the TIF bit first to avoid the overhead of
 * the atomic test_and_clear_bit() operation for the common case where the bit
 * is not set.
 */
static __always_inline bool hrtimer_test_and_clear_rearm_deferred_tif(unsigned long tif_work)
{
	lockdep_assert_irqs_disabled();

	if (unlikely(tif_work & _TIF_HRTIMER_REARM)) {
		clear_thread_flag(TIF_HRTIMER_REARM);
		return true;
	}
	return false;
}

#define TIF_REARM_MASK	(_TIF_NEED_RESCHED | _TIF_NEED_RESCHED_LAZY | _TIF_HRTIMER_REARM)

/* Invoked from the exit to user before invoking exit_to_user_mode_loop() */
static __always_inline bool
hrtimer_rearm_deferred_user_irq(unsigned long *tif_work, const unsigned long tif_mask)
{
	/* Help the compiler to optimize the function out for syscall returns */
	if (!(tif_mask & _TIF_HRTIMER_REARM))
		return false;
	/*
	 * Rearm the timer if none of the resched flags is set before going into
	 * the loop which re-enables interrupts.
	 */
	if (unlikely((*tif_work & TIF_REARM_MASK) == _TIF_HRTIMER_REARM)) {
		clear_thread_flag(TIF_HRTIMER_REARM);
		__hrtimer_rearm_deferred();
		/* Don't go into the loop if HRTIMER_REARM was the only flag */
		*tif_work &= ~TIF_HRTIMER_REARM;
		return !*tif_work;
	}
	return false;
}

/* Invoked from the time slice extension decision function */
static __always_inline void hrtimer_rearm_deferred_tif(unsigned long tif_work)
{
	if (hrtimer_test_and_clear_rearm_deferred_tif(tif_work))
		__hrtimer_rearm_deferred();
}

/*
 * This is to be called on all irqentry_exit() paths that will enable
 * interrupts.
 */
static __always_inline void hrtimer_rearm_deferred(void)
{
	hrtimer_rearm_deferred_tif(read_thread_flags());
}

/*
 * Invoked from the scheduler on entry to __schedule() so it can defer
 * rearming after the load balancing callbacks which might change hrtick.
 */
static __always_inline bool hrtimer_test_and_clear_rearm_deferred(void)
{
	return hrtimer_test_and_clear_rearm_deferred_tif(read_thread_flags());
}

#else  /* CONFIG_HRTIMER_REARM_DEFERRED */
static __always_inline void __hrtimer_rearm_deferred(void) { }
static __always_inline void hrtimer_rearm_deferred(void) { }
static __always_inline void hrtimer_rearm_deferred_tif(unsigned long tif_work) { }
static __always_inline bool
hrtimer_rearm_deferred_user_irq(unsigned long *tif_work, const unsigned long tif_mask) { return false; }
static __always_inline bool hrtimer_test_and_clear_rearm_deferred(void) { return false; }
#endif  /* !CONFIG_HRTIMER_REARM_DEFERRED */

#endif
