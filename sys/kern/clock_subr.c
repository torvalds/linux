/*	$OpenBSD: clock_subr.c,v 1.6 2016/08/26 07:09:56 guenther Exp $	*/
/*	$NetBSD: clock_subr.c,v 1.3 1997/03/15 18:11:16 is Exp $	*/

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1982, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 * from: Utah $Hdr: clock.c 1.18 91/01/21$
 *
 *	@(#)clock.c	8.2 (Berkeley) 1/12/94
 */

/*
 * Generic routines to convert between a POSIX date
 * (seconds since 1/1/1970) and yr/mo/day/hr/min/sec
 * Derived from arch/hp300/hp300/clock.c
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/systm.h>

static inline int leapyear(int year);
#define FEBRUARY	2
#define	days_in_year(a) 	(leapyear(a) ? 366 : 365)
#define	days_in_month(a) 	(month_days[(a) - 1])

static const int month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 * This inline avoids some unnecessary modulo operations
 * as compared with the usual macro:
 *   ( ((year % 4) == 0 &&
 *      (year % 100) != 0) ||
 *     ((year % 400) == 0) )
 * It is otherwise equivalent.
 */
static inline int
leapyear(int year)
{
	int rv = 0;

	if ((year & 3) == 0) {
		rv = 1;
		if ((year % 100) == 0) {
			rv = 0;
			if ((year % 400) == 0)
				rv = 1;
		}
	}
	return (rv);
}

time_t
clock_ymdhms_to_secs(struct clock_ymdhms *dt)
{
	time_t secs;
	int i, year, days;

	year = dt->dt_year;

	/*
	 * Compute days since start of time.
	 * First from years, then from months.
	 */
	days = 0;
	for (i = POSIX_BASE_YEAR; i < year; i++)
		days += days_in_year(i);
	if (leapyear(year) && dt->dt_mon > FEBRUARY)
		days++;

	/* Months */
	for (i = 1; i < dt->dt_mon; i++)
	  	days += days_in_month(i);
	days += (dt->dt_day - 1);

	/* Add hours, minutes, seconds. */
	secs = (time_t)((days
	    * 24 + dt->dt_hour)
	    * 60 + dt->dt_min)
	    * 60 + dt->dt_sec;

	return (secs);
}

/* This function uses a copy of month_days[] */
#undef	days_in_month
#define	days_in_month(a) 	(mthdays[(a) - 1])

void
clock_secs_to_ymdhms(time_t secs, struct clock_ymdhms *dt)
{
	int mthdays[12];
	int i, days;
	int rsec;	/* remainder seconds */

	memcpy(mthdays, month_days, sizeof(mthdays));

	days = secs / SECDAY;
	rsec = secs % SECDAY;

	/* Day of week (Note: 1/1/1970 was a Thursday) */
	dt->dt_wday = (days + 4) % 7;

	/* Subtract out whole years, counting them in i. */
	for (i = POSIX_BASE_YEAR; days >= days_in_year(i); i++)
		days -= days_in_year(i);
	dt->dt_year = i;

	/* Subtract out whole months, counting them in i. */
	if (leapyear(i))
		days_in_month(FEBRUARY) = 29;
	for (i = 1; days >= days_in_month(i); i++)
		days -= days_in_month(i);
	dt->dt_mon = i;

	/* Days are what is left over (+1) from all that. */
	dt->dt_day = days + 1;

	/* Hours, minutes, seconds are easy */
	dt->dt_hour = rsec / 3600;
	rsec = rsec % 3600;
	dt->dt_min  = rsec / 60;
	rsec = rsec % 60;
	dt->dt_sec  = rsec;
}
