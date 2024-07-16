/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KERNEL_TIME_TIMEKEEPING_H
#define _KERNEL_TIME_TIMEKEEPING_H
/*
 * Internal interfaces for kernel/time/
 */
extern ktime_t ktime_get_update_offsets_now(unsigned int *cwsseq,
					    ktime_t *offs_real,
					    ktime_t *offs_boot,
					    ktime_t *offs_tai);

extern int timekeeping_valid_for_hres(void);
extern u64 timekeeping_max_deferment(void);
extern void timekeeping_warp_clock(void);
extern int timekeeping_suspend(void);
extern void timekeeping_resume(void);
#ifdef CONFIG_GENERIC_SCHED_CLOCK
extern int sched_clock_suspend(void);
extern void sched_clock_resume(void);
#else
static inline int sched_clock_suspend(void) { return 0; }
static inline void sched_clock_resume(void) { }
#endif

extern void update_process_times(int user);
extern void do_timer(unsigned long ticks);
extern void update_wall_time(void);

extern raw_spinlock_t jiffies_lock;
extern seqcount_raw_spinlock_t jiffies_seq;

#define CS_NAME_LEN	32

#endif
