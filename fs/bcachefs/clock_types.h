/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_CLOCK_TYPES_H
#define _BCACHEFS_CLOCK_TYPES_H

#include "util.h"

#define NR_IO_TIMERS		(BCH_SB_MEMBERS_MAX * 3)

/*
 * Clocks/timers in units of sectors of IO:
 *
 * Note - they use percpu batching, so they're only approximate.
 */

struct io_timer;
typedef void (*io_timer_fn)(struct io_timer *);

struct io_timer {
	io_timer_fn		fn;
	void			*fn2;
	u64			expire;
};

/* Amount to buffer up on a percpu counter */
#define IO_CLOCK_PCPU_SECTORS	128

typedef DEFINE_MIN_HEAP(struct io_timer *, io_timer_heap)	io_timer_heap;

struct io_clock {
	atomic64_t		now;
	u16 __percpu		*pcpu_buf;
	unsigned		max_slop;

	spinlock_t		timer_lock;
	io_timer_heap		timers;
};

#endif /* _BCACHEFS_CLOCK_TYPES_H */
