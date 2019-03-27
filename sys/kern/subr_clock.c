/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	from: Utah $Hdr: clock.c 1.18 91/01/21$
 *	from: @(#)clock.c	8.2 (Berkeley) 1/12/94
 *	from: NetBSD: clock_subr.c,v 1.6 2001/07/07 17:04:02 thorpej Exp
 *	and
 *	from: src/sys/i386/isa/clock.c,v 1.176 2001/09/04
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/limits.h>
#include <sys/sysctl.h>
#include <sys/timetc.h>

/*
 * The adjkerntz and wall_cmos_clock sysctls are in the "machdep" sysctl
 * namespace because they were misplaced there originally.
 */
static int adjkerntz;
static int
sysctl_machdep_adjkerntz(SYSCTL_HANDLER_ARGS)
{
	int error;
	error = sysctl_handle_int(oidp, oidp->oid_arg1, oidp->oid_arg2, req);
	if (!error && req->newptr)
		resettodr();
	return (error);
}
SYSCTL_PROC(_machdep, OID_AUTO, adjkerntz, CTLTYPE_INT | CTLFLAG_RW |
    CTLFLAG_MPSAFE, &adjkerntz, 0, sysctl_machdep_adjkerntz, "I",
    "Local offset from UTC in seconds");

static int ct_debug;
SYSCTL_INT(_debug, OID_AUTO, clocktime, CTLFLAG_RWTUN,
    &ct_debug, 0, "Enable printing of clocktime debugging");

static int wall_cmos_clock;
SYSCTL_INT(_machdep, OID_AUTO, wall_cmos_clock, CTLFLAG_RW,
    &wall_cmos_clock, 0, "Enables application of machdep.adjkerntz");

/*--------------------------------------------------------------------*
 * Generic routines to convert between a POSIX date
 * (seconds since 1/1/1970) and yr/mo/day/hr/min/sec
 * Derived from NetBSD arch/hp300/hp300/clock.c
 */


#define	FEBRUARY	2
#define	days_in_year(y) 	(leapyear(y) ? 366 : 365)
#define	days_in_month(y, m) \
	(month_days[(m) - 1] + (m == FEBRUARY ? leapyear(y) : 0))
/* Day of week. Days are counted from 1/1/1970, which was a Thursday */
#define	day_of_week(days)	(((days) + 4) % 7)

static const int month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 * Optimization: using a precomputed count of days between POSIX_BASE_YEAR and
 * some recent year avoids lots of unnecessary loop iterations in conversion.
 * recent_base_days is the number of days before the start of recent_base_year.
 */
static const int recent_base_year = 2017;
static const int recent_base_days = 17167;

/*
 * Table to 'calculate' pow(10, 9 - nsdigits) via lookup of nsdigits.
 * Before doing the lookup, the code asserts 0 <= nsdigits <= 9.
 */
static u_int nsdivisors[] = {
    1000000000, 100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1
};

/*
 * This inline avoids some unnecessary modulo operations
 * as compared with the usual macro:
 *   ( ((year % 4) == 0 &&
 *      (year % 100) != 0) ||
 *     ((year % 400) == 0) )
 * It is otherwise equivalent.
 */
static int
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

int
clock_ct_to_ts(const struct clocktime *ct, struct timespec *ts)
{
	int i, year, days;

	if (ct_debug) {
		printf("ct_to_ts([");
		clock_print_ct(ct, 9);
		printf("])");
	}

	/*
	 * Many realtime clocks store the year as 2-digit BCD; pivot on 70 to
	 * determine century.  Some clocks have a "century bit" and drivers do
	 * year += 100, so interpret values between 70-199 as relative to 1900.
	 */
	year = ct->year;
	if (year < 70)
		year += 2000;
	else if (year < 200)
		year += 1900;

	/* Sanity checks. */
	if (ct->mon < 1 || ct->mon > 12 || ct->day < 1 ||
	    ct->day > days_in_month(year, ct->mon) ||
	    ct->hour > 23 ||  ct->min > 59 || ct->sec > 59 || year < 1970 ||
	    (sizeof(time_t) == 4 && year > 2037)) {	/* time_t overflow */
		if (ct_debug)
			printf(" = EINVAL\n");
		return (EINVAL);
	}

	/*
	 * Compute days since start of time
	 * First from years, then from months.
	 */
	if (year >= recent_base_year) {
		i = recent_base_year;
		days = recent_base_days;
	} else {
		i = POSIX_BASE_YEAR;
		days = 0;
	}
	for (; i < year; i++)
		days += days_in_year(i);

	/* Months */
	for (i = 1; i < ct->mon; i++)
	  	days += days_in_month(year, i);
	days += (ct->day - 1);

	ts->tv_sec = (((time_t)days * 24 + ct->hour) * 60 + ct->min) * 60 +
	    ct->sec;
	ts->tv_nsec = ct->nsec;

	if (ct_debug)
		printf(" = %jd.%09ld\n", (intmax_t)ts->tv_sec, ts->tv_nsec);
	return (0);
}

int
clock_bcd_to_ts(const struct bcd_clocktime *bct, struct timespec *ts, bool ampm)
{
	struct clocktime ct;
	int bcent, byear;

	/*
	 * Year may come in as 2-digit or 4-digit BCD.  Split the value into
	 * separate BCD century and year values for validation and conversion.
	 */
	bcent = bct->year >> 8;
	byear = bct->year & 0xff;

	/*
	 * Ensure that all values are valid BCD numbers, to avoid assertions in
	 * the BCD-to-binary conversion routines.  clock_ct_to_ts() will further
	 * validate the field ranges (such as 0 <= min <= 59) during conversion.
	 */
	if (!validbcd(bcent) || !validbcd(byear) || !validbcd(bct->mon) ||
	    !validbcd(bct->day) || !validbcd(bct->hour) ||
	    !validbcd(bct->min) || !validbcd(bct->sec)) {
		if (ct_debug)
			printf("clock_bcd_to_ts: bad BCD: "
			    "[%04x-%02x-%02x %02x:%02x:%02x]\n",
			    bct->year, bct->mon, bct->day,
			    bct->hour, bct->min, bct->sec);
		return (EINVAL);
	}

	ct.year = FROMBCD(byear) + FROMBCD(bcent) * 100;
	ct.mon  = FROMBCD(bct->mon);
	ct.day  = FROMBCD(bct->day);
	ct.hour = FROMBCD(bct->hour);
	ct.min  = FROMBCD(bct->min);
	ct.sec  = FROMBCD(bct->sec);
	ct.dow  = bct->dow;
	ct.nsec = bct->nsec;

	/* If asked to handle am/pm, convert from 12hr+pmflag to 24hr. */
	if (ampm) {
		if (ct.hour == 12)
			ct.hour = 0;
		if (bct->ispm)
			ct.hour += 12;
	}

	return (clock_ct_to_ts(&ct, ts));
}

void
clock_ts_to_ct(const struct timespec *ts, struct clocktime *ct)
{
	time_t i, year, days;
	time_t rsec;	/* remainder seconds */
	time_t secs;

	secs = ts->tv_sec;
	days = secs / SECDAY;
	rsec = secs % SECDAY;

	ct->dow = day_of_week(days);

	/* Subtract out whole years. */
	if (days >= recent_base_days) {
		year = recent_base_year;
		days -= recent_base_days;
	} else {
		year = POSIX_BASE_YEAR;
	}
	for (; days >= days_in_year(year); year++)
		days -= days_in_year(year);
	ct->year = year;

	/* Subtract out whole months, counting them in i. */
	for (i = 1; days >= days_in_month(year, i); i++)
		days -= days_in_month(year, i);
	ct->mon = i;

	/* Days are what is left over (+1) from all that. */
	ct->day = days + 1;

	/* Hours, minutes, seconds are easy */
	ct->hour = rsec / 3600;
	rsec = rsec % 3600;
	ct->min  = rsec / 60;
	rsec = rsec % 60;
	ct->sec  = rsec;
	ct->nsec = ts->tv_nsec;
	if (ct_debug) {
		printf("ts_to_ct(%jd.%09ld) = [",
		    (intmax_t)ts->tv_sec, ts->tv_nsec);
		clock_print_ct(ct, 9);
		printf("]\n");
	}

	KASSERT(ct->year >= 0 && ct->year < 10000,
	    ("year %d isn't a 4 digit year", ct->year));
	KASSERT(ct->mon >= 1 && ct->mon <= 12,
	    ("month %d not in 1-12", ct->mon));
	KASSERT(ct->day >= 1 && ct->day <= 31,
	    ("day %d not in 1-31", ct->day));
	KASSERT(ct->hour >= 0 && ct->hour <= 23,
	    ("hour %d not in 0-23", ct->hour));
	KASSERT(ct->min >= 0 && ct->min <= 59,
	    ("minute %d not in 0-59", ct->min));
	/* Not sure if this interface needs to handle leapseconds or not. */
	KASSERT(ct->sec >= 0 && ct->sec <= 60,
	    ("seconds %d not in 0-60", ct->sec));
}

void
clock_ts_to_bcd(const struct timespec *ts, struct bcd_clocktime *bct, bool ampm)
{
	struct clocktime ct;

	clock_ts_to_ct(ts, &ct);

	/* If asked to handle am/pm, convert from 24hr to 12hr+pmflag. */
	bct->ispm = false;
	if (ampm) {
		if (ct.hour >= 12) {
			ct.hour -= 12;
			bct->ispm = true;
		}
		if (ct.hour == 0)
			ct.hour = 12;
	}

	bct->year = TOBCD(ct.year % 100) | (TOBCD(ct.year / 100) << 8);
	bct->mon  = TOBCD(ct.mon);
	bct->day  = TOBCD(ct.day);
	bct->hour = TOBCD(ct.hour);
	bct->min  = TOBCD(ct.min);
	bct->sec  = TOBCD(ct.sec);
	bct->dow  = ct.dow;
	bct->nsec = ct.nsec;
}

void
clock_print_bcd(const struct bcd_clocktime *bct, int nsdigits)
{

	KASSERT(nsdigits >= 0 && nsdigits <= 9, ("bad nsdigits %d", nsdigits));

	if (nsdigits > 0) {
		printf("%4.4x-%2.2x-%2.2x %2.2x:%2.2x:%2.2x.%*.*ld",
		    bct->year, bct->mon, bct->day,
		    bct->hour, bct->min, bct->sec,
		    nsdigits, nsdigits, bct->nsec / nsdivisors[nsdigits]);
	} else {
		printf("%4.4x-%2.2x-%2.2x %2.2x:%2.2x:%2.2x",
		    bct->year, bct->mon, bct->day,
		    bct->hour, bct->min, bct->sec);
	}
}

void
clock_print_ct(const struct clocktime *ct, int nsdigits)
{

	KASSERT(nsdigits >= 0 && nsdigits <= 9, ("bad nsdigits %d", nsdigits));

	if (nsdigits > 0) {
		printf("%04d-%02d-%02d %02d:%02d:%02d.%*.*ld",
		    ct->year, ct->mon, ct->day,
		    ct->hour, ct->min, ct->sec,
		    nsdigits, nsdigits, ct->nsec / nsdivisors[nsdigits]);
	} else {
		printf("%04d-%02d-%02d %02d:%02d:%02d",
		    ct->year, ct->mon, ct->day,
		    ct->hour, ct->min, ct->sec);
	}
}

void
clock_print_ts(const struct timespec *ts, int nsdigits)
{
	struct clocktime ct;

	clock_ts_to_ct(ts, &ct);
	clock_print_ct(&ct, nsdigits);
}

int
utc_offset(void)
{

	return (wall_cmos_clock ? adjkerntz : 0);
}
