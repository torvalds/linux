/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _linux_POSIX_TIMERS_TYPES_H
#define _linux_POSIX_TIMERS_TYPES_H

#include <linux/mutex_types.h>
#include <linux/timerqueue_types.h>
#include <linux/types.h>

/*
 * Bit fields within a clockid:
 *
 * The most significant 29 bits hold either a pid or a file descriptor.
 *
 * Bit 2 indicates whether a cpu clock refers to a thread or a process.
 *
 * Bits 1 and 0 give the type: PROF=0, VIRT=1, SCHED=2, or FD=3.
 *
 * A clockid is invalid if bits 2, 1, and 0 are all set.
 */
#define CPUCLOCK_PID(clock)		((pid_t) ~((clock) >> 3))
#define CPUCLOCK_PERTHREAD(clock) \
	(((clock) & (clockid_t) CPUCLOCK_PERTHREAD_MASK) != 0)

#define CPUCLOCK_PERTHREAD_MASK	4
#define CPUCLOCK_WHICH(clock)	((clock) & (clockid_t) CPUCLOCK_CLOCK_MASK)
#define CPUCLOCK_CLOCK_MASK	3
#define CPUCLOCK_PROF		0
#define CPUCLOCK_VIRT		1
#define CPUCLOCK_SCHED		2
#define CPUCLOCK_MAX		3
#define CLOCKFD			CPUCLOCK_MAX
#define CLOCKFD_MASK		(CPUCLOCK_PERTHREAD_MASK|CPUCLOCK_CLOCK_MASK)

#ifdef CONFIG_POSIX_TIMERS

/**
 * posix_cputimer_base - Container per posix CPU clock
 * @nextevt:		Earliest-expiration cache
 * @tqhead:		timerqueue head for cpu_timers
 */
struct posix_cputimer_base {
	u64			nextevt;
	struct timerqueue_head	tqhead;
};

/**
 * posix_cputimers - Container for posix CPU timer related data
 * @bases:		Base container for posix CPU clocks
 * @timers_active:	Timers are queued.
 * @expiry_active:	Timer expiry is active. Used for
 *			process wide timers to avoid multiple
 *			task trying to handle expiry concurrently
 *
 * Used in task_struct and signal_struct
 */
struct posix_cputimers {
	struct posix_cputimer_base	bases[CPUCLOCK_MAX];
	unsigned int			timers_active;
	unsigned int			expiry_active;
};

/**
 * posix_cputimers_work - Container for task work based posix CPU timer expiry
 * @work:	The task work to be scheduled
 * @mutex:	Mutex held around expiry in context of this task work
 * @scheduled:  @work has been scheduled already, no further processing
 */
struct posix_cputimers_work {
	struct callback_head	work;
	struct mutex		mutex;
	unsigned int		scheduled;
};

#else /* CONFIG_POSIX_TIMERS */

struct posix_cputimers { };

#endif /* CONFIG_POSIX_TIMERS */

#endif /* _linux_POSIX_TIMERS_TYPES_H */
