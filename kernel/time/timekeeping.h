/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KERNEL_TIME_TIMEKEEPING_H
#define _KERNEL_TIME_TIMEKEEPING_H
/*
 * Internal interfaces for kernel/time/
 */
extern ktime_t ktime_get_update_offsets_now(unsigned int *cwsseq,
					    ktime_t *offs_real,
					    ktime_t *offs_tai);

extern int timekeeping_valid_for_hres(void);
extern u64 timekeeping_max_deferment(void);
extern void timekeeping_warp_clock(void);
extern int timekeeping_suspend(void);
extern void timekeeping_resume(void);

extern void do_timer(unsigned long ticks);
extern void update_wall_time(void);

extern seqlock_t jiffies_lock;

#define CS_NAME_LEN	32

#endif
