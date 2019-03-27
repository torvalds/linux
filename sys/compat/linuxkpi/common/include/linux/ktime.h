/*-
 * Copyright (c) 2018 Limelight Networks, Inc.
 * Copyright (c) 2014-2018 Mellanox Technologies, Ltd.
 * Copyright (c) 2015 François Tigeot
 * All rights reserved.
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

#ifndef _LINUX_KTIME_H
#define	_LINUX_KTIME_H

#include <linux/types.h>
#include <linux/time.h>
#include <linux/jiffies.h>

#define	ktime_get_ts(x) getnanouptime(x)

/* time values in nanoseconds */
typedef s64 ktime_t;

#define	KTIME_MAX			((s64)~((u64)1 << 63))
#define	KTIME_SEC_MAX			(KTIME_MAX / NSEC_PER_SEC)

static inline int64_t
ktime_to_ns(ktime_t kt)
{
	return (kt);
}

static inline ktime_t
ns_to_ktime(uint64_t nsec)
{
	return (nsec);
}

static inline int64_t
ktime_divns(const ktime_t kt, int64_t div)
{
	return (kt / div);
}

static inline int64_t
ktime_to_us(ktime_t kt)
{
	return (ktime_divns(kt, NSEC_PER_USEC));
}

static inline int64_t
ktime_to_ms(ktime_t kt)
{
	return (ktime_divns(kt, NSEC_PER_MSEC));
}

static inline struct timeval
ktime_to_timeval(ktime_t kt)
{
	return (ns_to_timeval(kt));
}

static inline ktime_t
ktime_add_ns(ktime_t kt, int64_t ns)
{
	return (kt + ns);
}

static inline ktime_t
ktime_add_ms(ktime_t kt, int64_t ms)
{

	return (ktime_add_ns(kt, ms * NSEC_PER_MSEC));
}

static inline ktime_t
ktime_sub_ns(ktime_t kt, int64_t ns)
{
	return (kt - ns);
}

static inline ktime_t
ktime_set(const long secs, const unsigned long nsecs)
{
	ktime_t retval = {(s64) secs * NSEC_PER_SEC + (s64) nsecs};

	return (retval);
}

static inline ktime_t
ktime_sub(ktime_t lhs, ktime_t rhs)
{
	return (lhs - rhs);
}

static inline int64_t
ktime_us_delta(ktime_t later, ktime_t earlier)
{
	ktime_t diff = ktime_sub(later, earlier);

	return (ktime_to_us(diff));
}

static inline int64_t
ktime_ms_delta(ktime_t later, ktime_t earlier)
{
	ktime_t diff = ktime_sub(later, earlier);

	return (ktime_to_ms(diff));
}

static inline ktime_t
ktime_add(ktime_t lhs, ktime_t rhs)
{
	return (lhs + rhs);
}

static inline int
ktime_compare(const ktime_t cmp1, const ktime_t cmp2)
{

	if (cmp1 > cmp2)
		return (1);
	else if (cmp1 < cmp2)
		return (-1);
	else
		return (0);
}

static inline bool
ktime_after(const ktime_t cmp1, const ktime_t cmp2)
{

	return (ktime_compare(cmp1, cmp2) > 0);
}

static inline bool
ktime_before(const ktime_t cmp1, const ktime_t cmp2)
{

	return (ktime_compare(cmp1, cmp2) < 0);
}

static inline ktime_t
timespec_to_ktime(struct timespec ts)
{
	return (ktime_set(ts.tv_sec, ts.tv_nsec));
}

static inline ktime_t
timeval_to_ktime(struct timeval tv)
{
	return (ktime_set(tv.tv_sec, tv.tv_usec * NSEC_PER_USEC));
}

#define	ktime_to_timespec(kt)		ns_to_timespec(kt)
#define	ktime_to_timespec64(kt)		ns_to_timespec(kt)
#define	ktime_to_timeval(kt)		ns_to_timeval(kt)
#define	ktime_to_ns(kt)			(kt)
#define	ktime_get_ts64(ts)		ktime_get_ts(ts)

static inline int64_t
ktime_get_ns(void)
{
	struct timespec ts;

	ktime_get_ts(&ts);

	return (ktime_to_ns(timespec_to_ktime(ts)));
}

static inline ktime_t
ktime_get(void)
{
	struct timespec ts;

	ktime_get_ts(&ts);
	return (timespec_to_ktime(ts));
}

static inline ktime_t
ktime_get_boottime(void)
{
	struct timespec ts;

	nanouptime(&ts);
	return (timespec_to_ktime(ts));
}

static inline ktime_t
ktime_get_real(void)
{
	struct timespec ts;

	nanotime(&ts);
	return (timespec_to_ktime(ts));
}

static inline ktime_t
ktime_get_real_seconds(void)
{
	struct timespec ts;

	nanotime(&ts);
	return (ts.tv_sec);
}

static inline ktime_t
ktime_get_raw(void)
{
	struct timespec ts;

	nanotime(&ts);
	return (timespec_to_ktime(ts));
}

static inline u64
ktime_get_raw_ns(void)
{
	struct timespec ts;

	nanouptime(&ts);
	return (ktime_to_ns(timespec_to_ktime(ts)));
}

#endif /* _LINUX_KTIME_H */
