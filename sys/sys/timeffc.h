/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 The University of Melbourne
 * All rights reserved.
 *
 * This software was developed by Julien Ridoux at the University of Melbourne
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_TIMEFF_H_
#define _SYS_TIMEFF_H_

#include <sys/_ffcounter.h>

/*
 * Feed-forward clock estimate
 * Holds time mark as a ffcounter and conversion to bintime based on current
 * timecounter period and offset estimate passed by the synchronization daemon.
 * Provides time of last daemon update, clock status and bound on error.
 */
struct ffclock_estimate {
	struct bintime	update_time;	/* Time of last estimates update. */
	ffcounter	update_ffcount;	/* Counter value at last update. */
	ffcounter	leapsec_next;	/* Counter value of next leap second. */
	uint64_t	period;		/* Estimate of counter period. */
	uint32_t	errb_abs;	/* Bound on absolute clock error [ns]. */
	uint32_t	errb_rate;	/* Bound on counter rate error [ps/s]. */
	uint32_t	status;		/* Clock status. */
	int16_t		leapsec_total;	/* All leap seconds seen so far. */
	int8_t		leapsec;	/* Next leap second (in {-1,0,1}). */
};

#if __BSD_VISIBLE
#ifdef _KERNEL

/* Define the kern.sysclock sysctl tree. */
SYSCTL_DECL(_kern_sysclock);

/* Define the kern.sysclock.ffclock sysctl tree. */
SYSCTL_DECL(_kern_sysclock_ffclock);

/*
 * Index into the sysclocks array for obtaining the ASCII name of a particular
 * sysclock.
 */
#define	SYSCLOCK_FBCK	0
#define	SYSCLOCK_FFWD	1
extern int sysclock_active;

/*
 * Parameters of counter characterisation required by feed-forward algorithms.
 */
#define	FFCLOCK_SKM_SCALE	1024

/*
 * Feed-forward clock status
 */
#define	FFCLOCK_STA_UNSYNC	1
#define	FFCLOCK_STA_WARMUP	2

/*
 * Flags for use by sysclock_snap2bintime() and various ffclock_ functions to
 * control how the timecounter hardware is read and how the hardware snapshot is
 * converted into absolute time.
 * {FB|FF}CLOCK_FAST:	Do not read the hardware counter, instead using the
 *			value at last tick. The time returned has a resolution
 *			of the kernel tick timer (1/hz [s]).
 * FFCLOCK_LERP:	Linear interpolation of ffclock time to guarantee
 *			monotonic time.
 * FFCLOCK_LEAPSEC:	Include leap seconds.
 * {FB|FF}CLOCK_UPTIME:	Time stamp should be relative to system boot, not epoch.
 */
#define	FFCLOCK_FAST		0x00000001
#define	FFCLOCK_LERP		0x00000002
#define	FFCLOCK_LEAPSEC		0x00000004
#define	FFCLOCK_UPTIME		0x00000008
#define	FFCLOCK_MASK		0x0000ffff

#define	FBCLOCK_FAST		0x00010000 /* Currently unused. */
#define	FBCLOCK_UPTIME		0x00020000
#define	FBCLOCK_MASK		0xffff0000

/*
 * Feedback clock specific info structure. The feedback clock's estimation of
 * clock error is an absolute figure determined by the NTP algorithm. The status
 * is determined by the userland daemon.
 */
struct fbclock_info {
	struct bintime		error;
	struct bintime		tick_time;
	uint64_t		th_scale;
	int			status;
};

/*
 * Feed-forward clock specific info structure. The feed-forward clock's
 * estimation of clock error is an upper bound, which although potentially
 * looser than the feedback clock equivalent, is much more reliable. The status
 * is determined by the userland daemon.
 */
struct ffclock_info {
	struct bintime		error;
	struct bintime		tick_time;
	struct bintime		tick_time_lerp;
	uint64_t		period;
	uint64_t		period_lerp;
	int			leapsec_adjustment;
	int			status;
};

/*
 * Snapshot of system clocks and related information. Holds time read from each
 * clock based on a single read of the active hardware timecounter, as well as
 * respective clock information such as error estimates and the ffcounter value
 * at the time of the read.
 */
struct sysclock_snap {
	struct fbclock_info	fb_info;
	struct ffclock_info	ff_info;
	ffcounter		ffcount;
	unsigned int		delta;
	int			sysclock_active;
};

/* Take a snapshot of the system clocks and related information. */
void sysclock_getsnapshot(struct sysclock_snap *clock_snap, int fast);

/* Convert a timestamp from the selected system clock into bintime. */
int sysclock_snap2bintime(struct sysclock_snap *cs, struct bintime *bt,
    int whichclock, uint32_t flags);

/* Resets feed-forward clock from RTC */
void ffclock_reset_clock(struct timespec *ts);

/*
 * Return the current value of the feed-forward clock counter. Essential to
 * measure time interval in counter units. If a fast timecounter is used by the
 * system, may also allow fast but accurate timestamping.
 */
void ffclock_read_counter(ffcounter *ffcount);

/*
 * Retrieve feed-forward counter value and time of last kernel tick. This
 * accepts the FFCLOCK_LERP flag.
 */
void ffclock_last_tick(ffcounter *ffcount, struct bintime *bt, uint32_t flags);

/*
 * Low level routines to convert a counter timestamp into absolute time and a
 * counter timestamp interval into an interval in seconds. The absolute time
 * conversion accepts the FFCLOCK_LERP flag.
 */
void ffclock_convert_abs(ffcounter ffcount, struct bintime *bt, uint32_t flags);
void ffclock_convert_diff(ffcounter ffdelta, struct bintime *bt);

/*
 * Feed-forward clock routines.
 *
 * These functions rely on the timecounters and ffclock_estimates stored in
 * fftimehands. Note that the error_bound parameter is not the error of the
 * clock but an upper bound on the error of the absolute time or time interval
 * returned.
 *
 * ffclock_abstime(): retrieves current time as counter value and convert this
 *     timestamp in seconds. The value (in seconds) of the converted timestamp
 *     depends on the flags passed: for a given counter value, different
 *     conversions are possible. Different clock models can be selected by
 *     combining flags (for example (FFCLOCK_LERP|FFCLOCK_UPTIME) produces
 *     linearly interpolated uptime).
 * ffclock_difftime(): computes a time interval in seconds based on an interval
 *     measured in ffcounter units. This should be the preferred way to measure
 *     small time intervals very accurately.
 */
void ffclock_abstime(ffcounter *ffcount, struct bintime *bt,
    struct bintime *error_bound, uint32_t flags);
void ffclock_difftime(ffcounter ffdelta, struct bintime *bt,
    struct bintime *error_bound);

/*
 * Wrapper routines to return current absolute time using the feed-forward
 * clock. These functions are named after those defined in <sys/time.h>, which
 * contains a description of the original ones.
 */
void ffclock_bintime(struct bintime *bt);
void ffclock_nanotime(struct timespec *tsp);
void ffclock_microtime(struct timeval *tvp);

void ffclock_getbintime(struct bintime *bt);
void ffclock_getnanotime(struct timespec *tsp);
void ffclock_getmicrotime(struct timeval *tvp);

void ffclock_binuptime(struct bintime *bt);
void ffclock_nanouptime(struct timespec *tsp);
void ffclock_microuptime(struct timeval *tvp);

void ffclock_getbinuptime(struct bintime *bt);
void ffclock_getnanouptime(struct timespec *tsp);
void ffclock_getmicrouptime(struct timeval *tvp);

/*
 * Wrapper routines to convert a time interval specified in ffcounter units into
 * seconds using the current feed-forward clock estimates.
 */
void ffclock_bindifftime(ffcounter ffdelta, struct bintime *bt);
void ffclock_nanodifftime(ffcounter ffdelta, struct timespec *tsp);
void ffclock_microdifftime(ffcounter ffdelta, struct timeval *tvp);

/*
 * When FFCLOCK is enabled in the kernel, [get]{bin,nano,micro}[up]time() become
 * wrappers around equivalent feedback or feed-forward functions. Provide access
 * outside of kern_tc.c to the feedback clock equivalent functions for
 * specialised use i.e. these are not for general consumption.
 */
void fbclock_bintime(struct bintime *bt);
void fbclock_nanotime(struct timespec *tsp);
void fbclock_microtime(struct timeval *tvp);

void fbclock_getbintime(struct bintime *bt);
void fbclock_getnanotime(struct timespec *tsp);
void fbclock_getmicrotime(struct timeval *tvp);

void fbclock_binuptime(struct bintime *bt);
void fbclock_nanouptime(struct timespec *tsp);
void fbclock_microuptime(struct timeval *tvp);

void fbclock_getbinuptime(struct bintime *bt);
void fbclock_getnanouptime(struct timespec *tsp);
void fbclock_getmicrouptime(struct timeval *tvp);

/*
 * Public system clock wrapper API which allows consumers to select which clock
 * to obtain time from, independent of the current default system clock. These
 * wrappers should be used instead of directly calling the underlying fbclock_
 * or ffclock_ functions.
 */
static inline void
bintime_fromclock(struct bintime *bt, int whichclock)
{

	if (whichclock == SYSCLOCK_FFWD)
		ffclock_bintime(bt);
	else
		fbclock_bintime(bt);
}

static inline void
nanotime_fromclock(struct timespec *tsp, int whichclock)
{

	if (whichclock == SYSCLOCK_FFWD)
		ffclock_nanotime(tsp);
	else
		fbclock_nanotime(tsp);
}

static inline void
microtime_fromclock(struct timeval *tvp, int whichclock)
{

	if (whichclock == SYSCLOCK_FFWD)
		ffclock_microtime(tvp);
	else
		fbclock_microtime(tvp);
}

static inline void
getbintime_fromclock(struct bintime *bt, int whichclock)
{

	if (whichclock == SYSCLOCK_FFWD)
		ffclock_getbintime(bt);
	else
		fbclock_getbintime(bt);
}

static inline void
getnanotime_fromclock(struct timespec *tsp, int whichclock)
{

	if (whichclock == SYSCLOCK_FFWD)
		ffclock_getnanotime(tsp);
	else
		fbclock_getnanotime(tsp);
}

static inline void
getmicrotime_fromclock(struct timeval *tvp, int whichclock)
{

	if (whichclock == SYSCLOCK_FFWD)
		ffclock_getmicrotime(tvp);
	else
		fbclock_getmicrotime(tvp);
}

static inline void
binuptime_fromclock(struct bintime *bt, int whichclock)
{

	if (whichclock == SYSCLOCK_FFWD)
		ffclock_binuptime(bt);
	else
		fbclock_binuptime(bt);
}

static inline void
nanouptime_fromclock(struct timespec *tsp, int whichclock)
{

	if (whichclock == SYSCLOCK_FFWD)
		ffclock_nanouptime(tsp);
	else
		fbclock_nanouptime(tsp);
}

static inline void
microuptime_fromclock(struct timeval *tvp, int whichclock)
{

	if (whichclock == SYSCLOCK_FFWD)
		ffclock_microuptime(tvp);
	else
		fbclock_microuptime(tvp);
}

static inline void
getbinuptime_fromclock(struct bintime *bt, int whichclock)
{

	if (whichclock == SYSCLOCK_FFWD)
		ffclock_getbinuptime(bt);
	else
		fbclock_getbinuptime(bt);
}

static inline void
getnanouptime_fromclock(struct timespec *tsp, int whichclock)
{

	if (whichclock == SYSCLOCK_FFWD)
		ffclock_getnanouptime(tsp);
	else
		fbclock_getnanouptime(tsp);
}

static inline void
getmicrouptime_fromclock(struct timeval *tvp, int whichclock)
{

	if (whichclock == SYSCLOCK_FFWD)
		ffclock_getmicrouptime(tvp);
	else
		fbclock_getmicrouptime(tvp);
}

#else /* !_KERNEL */

/* Feed-Forward Clock system calls. */
__BEGIN_DECLS
int ffclock_getcounter(ffcounter *ffcount);
int ffclock_getestimate(struct ffclock_estimate *cest);
int ffclock_setestimate(struct ffclock_estimate *cest);
__END_DECLS

#endif /* _KERNEL */
#endif /* __BSD_VISIBLE */
#endif /* _SYS_TIMEFF_H_ */
