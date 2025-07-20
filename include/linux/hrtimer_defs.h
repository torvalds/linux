/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HRTIMER_DEFS_H
#define _LINUX_HRTIMER_DEFS_H

#include <linux/ktime.h>
#include <linux/timerqueue.h>
#include <linux/seqlock.h>

#ifdef CONFIG_HIGH_RES_TIMERS

/*
 * The resolution of the clocks. The resolution value is returned in
 * the clock_getres() system call to give application programmers an
 * idea of the (in)accuracy of timers. Timer values are rounded up to
 * this resolution values.
 */
# define HIGH_RES_NSEC		1
# define KTIME_HIGH_RES		(HIGH_RES_NSEC)
# define MONOTONIC_RES_NSEC	HIGH_RES_NSEC
# define KTIME_MONOTONIC_RES	KTIME_HIGH_RES

#else

# define MONOTONIC_RES_NSEC	LOW_RES_NSEC
# define KTIME_MONOTONIC_RES	KTIME_LOW_RES

#endif

#ifdef CONFIG_64BIT
# define __hrtimer_clock_base_align	____cacheline_aligned
#else
# define __hrtimer_clock_base_align
#endif

/**
 * struct hrtimer_clock_base - the timer base for a specific clock
 * @cpu_base:		per cpu clock base
 * @index:		clock type index for per_cpu support when moving a
 *			timer to a base on another cpu.
 * @clockid:		clock id for per_cpu support
 * @seq:		seqcount around __run_hrtimer
 * @running:		pointer to the currently running hrtimer
 * @active:		red black tree root node for the active timers
 * @get_time:		function to retrieve the current time of the clock
 * @offset:		offset of this clock to the monotonic base
 */
struct hrtimer_clock_base {
	struct hrtimer_cpu_base	*cpu_base;
	unsigned int		index;
	clockid_t		clockid;
	seqcount_raw_spinlock_t	seq;
	struct hrtimer		*running;
	struct timerqueue_head	active;
	ktime_t			(*get_time)(void);
	ktime_t			offset;
} __hrtimer_clock_base_align;

enum  hrtimer_base_type {
	HRTIMER_BASE_MONOTONIC,
	HRTIMER_BASE_REALTIME,
	HRTIMER_BASE_BOOTTIME,
	HRTIMER_BASE_TAI,
	HRTIMER_BASE_MONOTONIC_SOFT,
	HRTIMER_BASE_REALTIME_SOFT,
	HRTIMER_BASE_BOOTTIME_SOFT,
	HRTIMER_BASE_TAI_SOFT,
	HRTIMER_MAX_CLOCK_BASES,
};

/**
 * struct hrtimer_cpu_base - the per cpu clock bases
 * @lock:		lock protecting the base and associated clock bases
 *			and timers
 * @cpu:		cpu number
 * @active_bases:	Bitfield to mark bases with active timers
 * @clock_was_set_seq:	Sequence counter of clock was set events
 * @hres_active:	State of high resolution mode
 * @in_hrtirq:		hrtimer_interrupt() is currently executing
 * @hang_detected:	The last hrtimer interrupt detected a hang
 * @softirq_activated:	displays, if the softirq is raised - update of softirq
 *			related settings is not required then.
 * @nr_events:		Total number of hrtimer interrupt events
 * @nr_retries:		Total number of hrtimer interrupt retries
 * @nr_hangs:		Total number of hrtimer interrupt hangs
 * @max_hang_time:	Maximum time spent in hrtimer_interrupt
 * @softirq_expiry_lock: Lock which is taken while softirq based hrtimer are
 *			 expired
 * @online:		CPU is online from an hrtimers point of view
 * @timer_waiters:	A hrtimer_cancel() invocation waits for the timer
 *			callback to finish.
 * @expires_next:	absolute time of the next event, is required for remote
 *			hrtimer enqueue; it is the total first expiry time (hard
 *			and soft hrtimer are taken into account)
 * @next_timer:		Pointer to the first expiring timer
 * @softirq_expires_next: Time to check, if soft queues needs also to be expired
 * @softirq_next_timer: Pointer to the first expiring softirq based timer
 * @clock_base:		array of clock bases for this cpu
 *
 * Note: next_timer is just an optimization for __remove_hrtimer().
 *	 Do not dereference the pointer because it is not reliable on
 *	 cross cpu removals.
 */
struct hrtimer_cpu_base {
	raw_spinlock_t			lock;
	unsigned int			cpu;
	unsigned int			active_bases;
	unsigned int			clock_was_set_seq;
	unsigned int			hres_active		: 1,
					in_hrtirq		: 1,
					hang_detected		: 1,
					softirq_activated       : 1,
					online			: 1;
#ifdef CONFIG_HIGH_RES_TIMERS
	unsigned int			nr_events;
	unsigned short			nr_retries;
	unsigned short			nr_hangs;
	unsigned int			max_hang_time;
#endif
#ifdef CONFIG_PREEMPT_RT
	spinlock_t			softirq_expiry_lock;
	atomic_t			timer_waiters;
#endif
	ktime_t				expires_next;
	struct hrtimer			*next_timer;
	ktime_t				softirq_expires_next;
	struct hrtimer			*softirq_next_timer;
	struct hrtimer_clock_base	clock_base[HRTIMER_MAX_CLOCK_BASES];
	call_single_data_t		csd;
} ____cacheline_aligned;


#endif
