/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)time.h	8.3 (Berkeley) 1/21/94
 */

/*
 * $FreeBSD$
 */

#ifndef _TIME_H_
#define	_TIME_H_

#include <sys/cdefs.h>
#include <sys/_null.h>
#include <sys/_types.h>

#if __POSIX_VISIBLE > 0 && __POSIX_VISIBLE < 200112 || __BSD_VISIBLE
/*
 * Frequency of the clock ticks reported by times().  Deprecated - use
 * sysconf(_SC_CLK_TCK) instead.  (Removed in 1003.1-2001.)
 */
#define	CLK_TCK		128
#endif

/* Frequency of the clock ticks reported by clock().  */
#define	CLOCKS_PER_SEC	128

#ifndef _CLOCK_T_DECLARED
typedef	__clock_t	clock_t;
#define	_CLOCK_T_DECLARED
#endif

#ifndef _TIME_T_DECLARED
typedef	__time_t	time_t;
#define	_TIME_T_DECLARED
#endif

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

#if __POSIX_VISIBLE >= 199309
/*
 * New in POSIX 1003.1b-1993.
 */
#ifndef _CLOCKID_T_DECLARED
typedef	__clockid_t	clockid_t;
#define	_CLOCKID_T_DECLARED
#endif

#ifndef _TIMER_T_DECLARED
typedef	__timer_t	timer_t;
#define	_TIMER_T_DECLARED
#endif

#include <sys/timespec.h>
#endif /* __POSIX_VISIBLE >= 199309 */

#if __POSIX_VISIBLE >= 200112
#ifndef _PID_T_DECLARED
typedef	__pid_t		pid_t;
#define	_PID_T_DECLARED
#endif
#endif

/* These macros are also in sys/time.h. */
#if !defined(CLOCK_REALTIME) && __POSIX_VISIBLE >= 200112
#define CLOCK_REALTIME	0
#ifdef __BSD_VISIBLE
#define CLOCK_VIRTUAL	1
#define CLOCK_PROF	2
#endif
#define CLOCK_MONOTONIC	4
#define CLOCK_UPTIME	5		/* FreeBSD-specific. */
#define CLOCK_UPTIME_PRECISE	7	/* FreeBSD-specific. */
#define CLOCK_UPTIME_FAST	8	/* FreeBSD-specific. */
#define CLOCK_REALTIME_PRECISE	9	/* FreeBSD-specific. */
#define CLOCK_REALTIME_FAST	10	/* FreeBSD-specific. */
#define CLOCK_MONOTONIC_PRECISE	11	/* FreeBSD-specific. */
#define CLOCK_MONOTONIC_FAST	12	/* FreeBSD-specific. */
#define CLOCK_SECOND	13		/* FreeBSD-specific. */
#define CLOCK_THREAD_CPUTIME_ID	14
#define	CLOCK_PROCESS_CPUTIME_ID	15
#endif /* !defined(CLOCK_REALTIME) && __POSIX_VISIBLE >= 200112 */

#if !defined(TIMER_ABSTIME) && __POSIX_VISIBLE >= 200112
#if __BSD_VISIBLE
#define TIMER_RELTIME	0x0	/* relative timer */
#endif
#define TIMER_ABSTIME	0x1	/* absolute timer */
#endif /* !defined(TIMER_ABSTIME) && __POSIX_VISIBLE >= 200112 */

struct tm {
	int	tm_sec;		/* seconds after the minute [0-60] */
	int	tm_min;		/* minutes after the hour [0-59] */
	int	tm_hour;	/* hours since midnight [0-23] */
	int	tm_mday;	/* day of the month [1-31] */
	int	tm_mon;		/* months since January [0-11] */
	int	tm_year;	/* years since 1900 */
	int	tm_wday;	/* days since Sunday [0-6] */
	int	tm_yday;	/* days since January 1 [0-365] */
	int	tm_isdst;	/* Daylight Savings Time flag */
	long	tm_gmtoff;	/* offset from UTC in seconds */
	char	*tm_zone;	/* timezone abbreviation */
};

#if __POSIX_VISIBLE
extern char *tzname[];
#endif

__BEGIN_DECLS
char *asctime(const struct tm *);
clock_t clock(void);
char *ctime(const time_t *);
#ifndef _STANDALONE
double difftime(time_t, time_t);
#endif
/* XXX missing: getdate() */
struct tm *gmtime(const time_t *);
struct tm *localtime(const time_t *);
time_t mktime(struct tm *);
size_t strftime(char * __restrict, size_t, const char * __restrict,
    const struct tm * __restrict);
time_t time(time_t *);
#if __POSIX_VISIBLE >= 200112
struct sigevent;
int timer_create(clockid_t, struct sigevent *__restrict, timer_t *__restrict);
int timer_delete(timer_t);
int timer_gettime(timer_t, struct itimerspec *);
int timer_getoverrun(timer_t);
int timer_settime(timer_t, int, const struct itimerspec *__restrict,
	struct itimerspec *__restrict);
#endif
#if __POSIX_VISIBLE
void tzset(void);
#endif

#if __POSIX_VISIBLE >= 199309
int clock_getres(clockid_t, struct timespec *);
int clock_gettime(clockid_t, struct timespec *);
int clock_settime(clockid_t, const struct timespec *);
int nanosleep(const struct timespec *, struct timespec *);
#endif /* __POSIX_VISIBLE >= 199309 */

#if __POSIX_VISIBLE >= 200112
int clock_getcpuclockid(pid_t, clockid_t *);
int clock_nanosleep(clockid_t, int, const struct timespec *, struct timespec *);
#endif

#if __POSIX_VISIBLE >= 199506
char *asctime_r(const struct tm *, char *);
char *ctime_r(const time_t *, char *);
struct tm *gmtime_r(const time_t *, struct tm *);
struct tm *localtime_r(const time_t *, struct tm *);
#endif

#if __XSI_VISIBLE
char *strptime(const char * __restrict, const char * __restrict,
    struct tm * __restrict);
#endif

#if __BSD_VISIBLE
char *timezone(int, int);	/* XXX XSI conflict */
void tzsetwall(void);
time_t timelocal(struct tm * const);
time_t timegm(struct tm * const);
int timer_oshandle_np(timer_t timerid);
time_t time2posix(time_t t);
time_t posix2time(time_t t);
#endif /* __BSD_VISIBLE */

#if __POSIX_VISIBLE >= 200809 || defined(_XLOCALE_H_)
#include <xlocale/_time.h>
#endif

#if defined(__BSD_VISIBLE) || __ISO_C_VISIBLE >= 2011 || \
    (defined(cplusplus) && cplusplus >= 201703)
#include <sys/_timespec.h>
/* ISO/IEC 9899:201x 7.27.2.5 The timespec_get function */
#define TIME_UTC	1	/* time elapsed since epoch */
int timespec_get(struct timespec *ts, int base);
#endif

__END_DECLS

#endif /* !_TIME_H_ */
