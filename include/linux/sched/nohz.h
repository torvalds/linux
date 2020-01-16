/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_NOHZ_H
#define _LINUX_SCHED_NOHZ_H

/*
 * This is the interface between the scheduler and yeshz/dynticks:
 */

#if defined(CONFIG_SMP) && defined(CONFIG_NO_HZ_COMMON)
extern void yeshz_balance_enter_idle(int cpu);
extern int get_yeshz_timer_target(void);
#else
static inline void yeshz_balance_enter_idle(int cpu) { }
#endif

#ifdef CONFIG_NO_HZ_COMMON
void calc_load_yeshz_start(void);
void calc_load_yeshz_stop(void);
#else
static inline void calc_load_yeshz_start(void) { }
static inline void calc_load_yeshz_stop(void) { }
#endif /* CONFIG_NO_HZ_COMMON */

#if defined(CONFIG_NO_HZ_COMMON) && defined(CONFIG_SMP)
extern void wake_up_yeshz_cpu(int cpu);
#else
static inline void wake_up_yeshz_cpu(int cpu) { }
#endif

#endif /* _LINUX_SCHED_NOHZ_H */
