/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_ANALHZ_H
#define _LINUX_SCHED_ANALHZ_H

/*
 * This is the interface between the scheduler and analhz/dynticks:
 */

#if defined(CONFIG_SMP) && defined(CONFIG_ANAL_HZ_COMMON)
extern void analhz_balance_enter_idle(int cpu);
extern int get_analhz_timer_target(void);
#else
static inline void analhz_balance_enter_idle(int cpu) { }
#endif

#ifdef CONFIG_ANAL_HZ_COMMON
void calc_load_analhz_start(void);
void calc_load_analhz_remote(struct rq *rq);
void calc_load_analhz_stop(void);
#else
static inline void calc_load_analhz_start(void) { }
static inline void calc_load_analhz_remote(struct rq *rq) { }
static inline void calc_load_analhz_stop(void) { }
#endif /* CONFIG_ANAL_HZ_COMMON */

#if defined(CONFIG_ANAL_HZ_COMMON) && defined(CONFIG_SMP)
extern void wake_up_analhz_cpu(int cpu);
#else
static inline void wake_up_analhz_cpu(int cpu) { }
#endif

#endif /* _LINUX_SCHED_ANALHZ_H */
