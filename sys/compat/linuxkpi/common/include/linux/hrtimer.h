/*-
 * Copyright (c) 2017 Mark Johnston <markj@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _LINUX_HRTIMER_H_
#define	_LINUX_HRTIMER_H_

#include <sys/_callout.h>
#include <sys/_mutex.h>

#include <linux/ktime.h>
#include <linux/timer.h>

enum hrtimer_mode {
	HRTIMER_MODE_REL,
	HRTIMER_MODE_REL_PINNED = HRTIMER_MODE_REL,
};

enum hrtimer_restart {
	HRTIMER_RESTART,
	HRTIMER_NORESTART,
};

struct hrtimer {
	enum hrtimer_restart (*function)(struct hrtimer *);
	struct mtx mtx;
	struct callout callout;
	s64 expires;	/* relative time in nanoseconds */
	s64 precision;	/* in nanoseconds */
};

#define	hrtimer_active(hrtimer)	linux_hrtimer_active(hrtimer)
#define	hrtimer_cancel(hrtimer)	linux_hrtimer_cancel(hrtimer)

#define	hrtimer_init(hrtimer, clock, mode) do {			\
	CTASSERT((clock) == CLOCK_MONOTONIC);			\
	CTASSERT((mode) == HRTIMER_MODE_REL);			\
	linux_hrtimer_init(hrtimer);				\
} while (0)

#define	hrtimer_set_expires(hrtimer, time)			\
	linux_hrtimer_set_expires(hrtimer, time)

#define	hrtimer_start(hrtimer, time, mode) do {			\
	CTASSERT((mode) == HRTIMER_MODE_REL);			\
	linux_hrtimer_start(hrtimer, time);			\
} while (0)

#define	hrtimer_start_range_ns(hrtimer, time, prec, mode) do {	\
	CTASSERT((mode) == HRTIMER_MODE_REL);			\
	linux_hrtimer_start_range_ns(hrtimer, time, prec);	\
} while (0)

#define	hrtimer_forward_now(hrtimer, interval) do {		\
	linux_hrtimer_forward_now(hrtimer, interval);		\
} while (0)

bool	linux_hrtimer_active(struct hrtimer *);
int	linux_hrtimer_cancel(struct hrtimer *);
void	linux_hrtimer_init(struct hrtimer *);
void	linux_hrtimer_set_expires(struct hrtimer *, ktime_t);
void	linux_hrtimer_start(struct hrtimer *, ktime_t);
void	linux_hrtimer_start_range_ns(struct hrtimer *, ktime_t, int64_t);
void	linux_hrtimer_forward_now(struct hrtimer *, ktime_t);

#endif /* _LINUX_HRTIMER_H_ */
