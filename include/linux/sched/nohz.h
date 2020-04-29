/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_NOHZ_H
#define _LINUX_SCHED_NOHZ_H

/*
 * This is the interface between the scheduler and nohz/dynticks:
 */

#if defined(CONFIG_SMP) && defined(CONFIG_NO_HZ_COMMON)
extern void nohz_balance_enter_idle(int cpu);
extern int get_nohz_timer_target(void);
#else
static inline void nohz_balance_enter_idle(int cpu) { }
#endif

#ifdef CONFIG_NO_HZ_COMMON
void calc_load_nohz_start(void);
void calc_load_nohz_remote(struct rq *rq);
void calc_load_nohz_stop(void);
#else
static inline void calc_load_nohz_start(void) { }
static inline void calc_load_nohz_remote(struct rq *rq) { }
static inline void calc_load_nohz_stop(void) { }
#endif /* CONFIG_NO_HZ_COMMON */

#if defined(CONFIG_NO_HZ_COMMON) && defined(CONFIG_SMP)
extern void wake_up_nohz_cpu(int cpu);
#else
static inline void wake_up_nohz_cpu(int cpu) { }
#endif

#endif /* _LINUX_SCHED_NOHZ_H */
