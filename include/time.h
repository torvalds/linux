/*	$OpenBSD: time.h,v 1.32 2022/10/25 16:30:30 millert Exp $	*/
/*	$NetBSD: time.h,v 1.9 1994/10/26 00:56:35 cgd Exp $	*/

/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
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
 *	@(#)time.h	5.12 (Berkeley) 3/9/91
 */

#ifndef _TIME_H_
#define	_TIME_H_

#include <sys/cdefs.h>
#include <sys/_null.h>
#include <sys/_types.h>
#include <sys/_time.h>

#ifndef	_CLOCK_T_DEFINED_
#define	_CLOCK_T_DEFINED_
typedef	__clock_t	clock_t;
#endif

#ifndef	_TIME_T_DEFINED_
#define	_TIME_T_DEFINED_
typedef	__time_t	time_t;
#endif

#ifndef	_SIZE_T_DEFINED_
#define	_SIZE_T_DEFINED_
typedef	__size_t	size_t;
#endif

#if __POSIX_VISIBLE > 0 && __POSIX_VISIBLE < 200112 || __BSD_VISIBLE
/*
 * Frequency of the clock ticks reported by times().  Deprecated - use
 * sysconf(_SC_CLK_TCK) instead.  (Removed in 1003.1-2001.)
 */
#define CLK_TCK		100
#endif

#if __POSIX_VISIBLE >= 199309
#ifndef	_CLOCKID_T_DEFINED_
#define	_CLOCKID_T_DEFINED_
typedef	__clockid_t	clockid_t;
#endif

#ifndef	_TIMER_T_DEFINED_
#define	_TIMER_T_DEFINED_
typedef	__timer_t	timer_t;
#endif
#endif

#if __POSIX_VISIBLE >= 200112
#ifndef	_PID_T_DEFINED_
#define	_PID_T_DEFINED_
typedef	__pid_t		pid_t;
#endif
#endif

#if __POSIX_VISIBLE >= 200809
#ifndef	_LOCALE_T_DEFINED_
#define	_LOCALE_T_DEFINED_
typedef void	*locale_t;
#endif
#endif

struct tm {
	int	tm_sec;		/* seconds after the minute [0-60] */
	int	tm_min;		/* minutes after the hour [0-59] */
	int	tm_hour;	/* hours since midnight [0-23] */
	int	tm_mday;	/* day of the month [1-31] */
	int	tm_mon;		/* months since January [0-11] */
	int	tm_year;	/* years since 1900 */
	int	tm_wday;	/* days since Sunday [0-6] */
	int	tm_yday;	/* days since January 1 [0-365] */
	int	tm_isdst;	/* Daylight Saving Time flag */
	long	tm_gmtoff;	/* offset from UTC in seconds */
	char	*tm_zone;	/* timezone abbreviation */
};

__BEGIN_DECLS
char *asctime(const struct tm *);
clock_t clock(void);
char *ctime(const time_t *);
double difftime(time_t, time_t);
struct tm *gmtime(const time_t *);
struct tm *localtime(const time_t *);
time_t mktime(struct tm *);
size_t strftime(char *__restrict, size_t, const char *__restrict,
		const struct tm *__restrict)
		__attribute__ ((__bounded__(__string__,1,2)));
time_t time(time_t *);

#if __XPG_VISIBLE >= 300
extern int daylight;
extern long timezone;
#endif
#if __XPG_VISIBLE >= 400
char *strptime(const char *__restrict, const char *__restrict,
		struct tm *__restrict);
#endif

#if __POSIX_VISIBLE >= 199506
char *asctime_r(const struct tm *__restrict, char *__restrict)
		__attribute__ ((__bounded__(__minbytes__,2,26)));
char *ctime_r(const time_t *, char *)
		__attribute__ ((__bounded__(__minbytes__,2,26)));
struct tm *gmtime_r(const time_t *__restrict, struct tm *__restrict);
struct tm *localtime_r(const time_t *__restrict, struct tm *__restrict);
#endif

#if __POSIX_VISIBLE
extern char *tzname[2];
void tzset(void);
#endif

#if __POSIX_VISIBLE >= 199309
int clock_getres(clockid_t, struct timespec *);
int clock_gettime(clockid_t, struct timespec *);
int clock_settime(clockid_t, const struct timespec *);
int nanosleep(const struct timespec *, struct timespec *);
#endif

#if __POSIX_VISIBLE >= 200112
int clock_getcpuclockid(pid_t, clockid_t *);
#endif

#if __POSIX_VISIBLE >= 200809
size_t strftime_l(char *__restrict, size_t, const char *__restrict,
		const struct tm *__restrict, locale_t)
		__attribute__ ((__bounded__(__string__,1,2)));
#endif

#if __ISO_C_VISIBLE >= 2011
#define TIME_UTC	1	/* time elapsed since epoch */
int timespec_get(struct timespec *_ts, int _base);
#endif

#if __BSD_VISIBLE
void tzsetwall(void);
time_t timelocal(struct tm *);
time_t timegm(struct tm *);
time_t timeoff(struct tm *, const long);
#endif
__END_DECLS

#endif /* !_TIME_H_ */
