// SPDX-License-Identifier: GPL-2.0
#ifndef _LINUX_HRTIMER_REARM_H
#define _LINUX_HRTIMER_REARM_H

#ifdef CONFIG_HRTIMER_REARM_DEFERRED
static __always_inline void __hrtimer_rearm_deferred(void) { }
static __always_inline void hrtimer_rearm_deferred(void) { }
static __always_inline void hrtimer_rearm_deferred_tif(unsigned long tif_work) { }
static __always_inline bool
hrtimer_rearm_deferred_user_irq(unsigned long *tif_work, const unsigned long tif_mask) { return false; }
static __always_inline bool hrtimer_test_and_clear_rearm_deferred(void) { return false; }
#else  /* CONFIG_HRTIMER_REARM_DEFERRED */
static __always_inline void __hrtimer_rearm_deferred(void) { }
static __always_inline void hrtimer_rearm_deferred(void) { }
static __always_inline void hrtimer_rearm_deferred_tif(unsigned long tif_work) { }
static __always_inline bool
hrtimer_rearm_deferred_user_irq(unsigned long *tif_work, const unsigned long tif_mask) { return false; }
static __always_inline bool hrtimer_test_and_clear_rearm_deferred(void) { return false; }
#endif  /* !CONFIG_HRTIMER_REARM_DEFERRED */

#endif
