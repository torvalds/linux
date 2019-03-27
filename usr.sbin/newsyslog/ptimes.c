/*-
 * ------+---------+---------+---------+---------+---------+---------+---------*
 * Initial version of parse8601 was originally added to newsyslog.c in
 *     FreeBSD on Jan 22, 1999 by Garrett Wollman <wollman@FreeBSD.org>.
 * Initial version of parseDWM was originally added to newsyslog.c in
 *     FreeBSD on Apr  4, 2000 by Hellmuth Michaelis <hm@FreeBSD.org>.
 *
 * Copyright (c) 2003  - Garance Alistair Drosehn <gad@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the FreeBSD Project.
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 * This is intended to be a set of general-purpose routines to process times.
 * Right now it probably still has a number of assumptions in it, such that
 * it works fine for newsyslog but might not work for other uses.
 * ------+---------+---------+---------+---------+---------+---------+---------*
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "extern.h"

#define	SECS_PER_HOUR	3600

/*
 * Bit-values which indicate which components of time were specified
 * by the string given to parse8601 or parseDWM.  These are needed to
 * calculate what time-in-the-future will match that string.
 */
#define	TSPEC_YEAR		0x0001
#define	TSPEC_MONTHOFYEAR	0x0002
#define	TSPEC_LDAYOFMONTH	0x0004
#define	TSPEC_DAYOFMONTH	0x0008
#define	TSPEC_DAYOFWEEK		0x0010
#define	TSPEC_HOUROFDAY		0x0020

#define	TNYET_ADJ4DST		-10	/* DST has "not yet" been adjusted */

struct ptime_data {
	time_t		 basesecs;	/* Base point for relative times */
	time_t		 tsecs;		/* Time in seconds */
	struct tm	 basetm;	/* Base Time expanded into fields */
	struct tm	 tm;		/* Time expanded into fields */
	int		 did_adj4dst;	/* Track calls to ptime_adjust4dst */
	int		 parseopts;	/* Options given for parsing */
	int		 tmspec;	/* Indicates which time fields had
					 * been specified by the user */
};

static int	 days_pmonth(int month, int year);
static int	 parse8601(struct ptime_data *ptime, const char *str);
static int	 parseDWM(struct ptime_data *ptime, const char *str);

/*
 * Simple routine to calculate the number of days in a given month.
 */
static int
days_pmonth(int month, int year)
{
	static const int mtab[] = {31, 28, 31, 30, 31, 30, 31, 31,
	    30, 31, 30, 31};
	int ndays;

	ndays = mtab[month];

	if (month == 1) {
		/*
		 * We are usually called with a 'tm-year' value
		 * (ie, the value = the number of years past 1900).
		 */
		if (year < 1900)
			year += 1900;
		if (year % 4 == 0) {
			/*
			 * This is a leap year, as long as it is not a
			 * multiple of 100, or if it is a multiple of
			 * both 100 and 400.
			 */
			if (year % 100 != 0)
				ndays++;	/* not multiple of 100 */
			else if (year % 400 == 0)
				ndays++;	/* is multiple of 100 and 400 */
		}
	}
	return (ndays);
}

/*-
 * Parse a limited subset of ISO 8601. The specific format is as follows:
 *
 * [CC[YY[MM[DD]]]][THH[MM[SS]]]	(where `T' is the literal letter)
 *
 * We don't accept a timezone specification; missing fields (including timezone)
 * are defaulted to the current date but time zero.
 */
static int
parse8601(struct ptime_data *ptime, const char *s)
{
	char *t;
	long l;
	struct tm tm;

	l = strtol(s, &t, 10);
	if (l < 0 || l >= INT_MAX || (*t != '\0' && *t != 'T'))
		return (-1);

	/*
	 * Now t points either to the end of the string (if no time was
	 * provided) or to the letter `T' which separates date and time in
	 * ISO 8601.  The pointer arithmetic is the same for either case.
	 */
	tm = ptime->tm;
	ptime->tmspec = TSPEC_HOUROFDAY;
	switch (t - s) {
	case 8:
		tm.tm_year = ((l / 1000000) - 19) * 100;
		l = l % 1000000;
	case 6:
		ptime->tmspec |= TSPEC_YEAR;
		tm.tm_year -= tm.tm_year % 100;
		tm.tm_year += l / 10000;
		l = l % 10000;
	case 4:
		ptime->tmspec |= TSPEC_MONTHOFYEAR;
		tm.tm_mon = (l / 100) - 1;
		l = l % 100;
	case 2:
		ptime->tmspec |= TSPEC_DAYOFMONTH;
		tm.tm_mday = l;
	case 0:
		break;
	default:
		return (-1);
	}

	/* sanity check */
	if (tm.tm_year < 70 || tm.tm_mon < 0 || tm.tm_mon > 12
	    || tm.tm_mday < 1 || tm.tm_mday > 31)
		return (-1);

	if (*t != '\0') {
		s = ++t;
		l = strtol(s, &t, 10);
		if (l < 0 || l >= INT_MAX || (*t != '\0' && !isspace(*t)))
			return (-1);

		switch (t - s) {
		case 6:
			tm.tm_sec = l % 100;
			l /= 100;
		case 4:
			tm.tm_min = l % 100;
			l /= 100;
		case 2:
			ptime->tmspec |= TSPEC_HOUROFDAY;
			tm.tm_hour = l;
		case 0:
			break;
		default:
			return (-1);
		}

		/* sanity check */
		if (tm.tm_sec < 0 || tm.tm_sec > 60 || tm.tm_min < 0
		    || tm.tm_min > 59 || tm.tm_hour < 0 || tm.tm_hour > 23)
			return (-1);
	}

	ptime->tm = tm;
	return (0);
}

/*-
 * Parse a cyclic time specification, the format is as follows:
 *
 *	[Dhh] or [Wd[Dhh]] or [Mdd[Dhh]]
 *
 * to rotate a logfile cyclic at
 *
 *	- every day (D) within a specific hour (hh)	(hh = 0...23)
 *	- once a week (W) at a specific day (d)     OR	(d = 0..6, 0 = Sunday)
 *	- once a month (M) at a specific day (d)	(d = 1..31,l|L)
 *
 * We don't accept a timezone specification; missing fields
 * are defaulted to the current date but time zero.
 */
static int
parseDWM(struct ptime_data *ptime, const char *s)
{
	int daysmon, Dseen, WMseen;
	const char *endval;
	char *tmp;
	long l;
	struct tm tm;

	/* Save away the number of days in this month */
	tm = ptime->tm;
	daysmon = days_pmonth(tm.tm_mon, tm.tm_year);

	WMseen = Dseen = 0;
	ptime->tmspec = TSPEC_HOUROFDAY;
	for (;;) {
		endval = NULL;
		switch (*s) {
		case 'D':
			if (Dseen)
				return (-1);
			Dseen++;
			ptime->tmspec |= TSPEC_HOUROFDAY;
			s++;
			l = strtol(s, &tmp, 10);
			if (l < 0 || l > 23)
				return (-1);
			endval = tmp;
			tm.tm_hour = l;
			break;

		case 'W':
			if (WMseen)
				return (-1);
			WMseen++;
			ptime->tmspec |= TSPEC_DAYOFWEEK;
			s++;
			l = strtol(s, &tmp, 10);
			if (l < 0 || l > 6)
				return (-1);
			endval = tmp;
			if (l != tm.tm_wday) {
				int save;

				if (l < tm.tm_wday) {
					save = 6 - tm.tm_wday;
					save += (l + 1);
				} else {
					save = l - tm.tm_wday;
				}

				tm.tm_mday += save;

				if (tm.tm_mday > daysmon) {
					tm.tm_mon++;
					tm.tm_mday = tm.tm_mday - daysmon;
				}
			}
			break;

		case 'M':
			if (WMseen)
				return (-1);
			WMseen++;
			ptime->tmspec |= TSPEC_DAYOFMONTH;
			s++;
			if (tolower(*s) == 'l') {
				/* User wants the last day of the month. */
				ptime->tmspec |= TSPEC_LDAYOFMONTH;
				tm.tm_mday = daysmon;
				endval = s + 1;
			} else {
				l = strtol(s, &tmp, 10);
				if (l < 1 || l > 31)
					return (-1);

				if (l > daysmon)
					return (-1);
				endval = tmp;
				tm.tm_mday = l;
			}
			break;

		default:
			return (-1);
			break;
		}

		if (endval == NULL)
			return (-1);
		else if (*endval == '\0' || isspace(*endval))
			break;
		else
			s = endval;
	}

	ptime->tm = tm;
	return (0);
}

/*
 * Initialize a new ptime-related data area.
 */
struct ptime_data *
ptime_init(const struct ptime_data *optsrc)
{
	struct ptime_data *newdata;

	newdata = malloc(sizeof(struct ptime_data));
	if (optsrc != NULL) {
		memcpy(newdata, optsrc, sizeof(struct ptime_data));
	} else {
		memset(newdata, '\0', sizeof(struct ptime_data));
		newdata->did_adj4dst = TNYET_ADJ4DST;
	}

	return (newdata);
}

/*
 * Adjust a given time if that time is in a different timezone than
 * some other time.
 */
int
ptime_adjust4dst(struct ptime_data *ptime, const struct ptime_data *dstsrc)
{
	struct ptime_data adjtime;

	if (ptime == NULL)
		return (-1);

	/*
	 * Changes are not made to the given time until after all
	 * of the calculations have been successful.
	 */
	adjtime = *ptime;

	/* Check to see if this adjustment was already made */
	if ((adjtime.did_adj4dst != TNYET_ADJ4DST) &&
	    (adjtime.did_adj4dst == dstsrc->tm.tm_isdst))
		return (0);		/* yes, so don't make it twice */

	/* See if daylight-saving has changed between the two times. */
	if (dstsrc->tm.tm_isdst != adjtime.tm.tm_isdst) {
		if (adjtime.tm.tm_isdst == 1)
			adjtime.tsecs -= SECS_PER_HOUR;
		else if (adjtime.tm.tm_isdst == 0)
			adjtime.tsecs += SECS_PER_HOUR;
		adjtime.tm = *(localtime(&adjtime.tsecs));
		/* Remember that this adjustment has been made */
		adjtime.did_adj4dst = dstsrc->tm.tm_isdst;
		/*
		 * XXX - Should probably check to see if changing the
		 *	hour also changed the value of is_dst.  What
		 *	should we do in that case?
		 */
	}

	*ptime = adjtime;
	return (0);
}

int
ptime_relparse(struct ptime_data *ptime, int parseopts, time_t basetime,
    const char *str)
{
	int dpm, pres;
	struct tm temp_tm;

	ptime->parseopts = parseopts;
	ptime->basesecs = basetime;
	ptime->basetm = *(localtime(&ptime->basesecs));
	ptime->tm = ptime->basetm;
	ptime->tm.tm_hour = ptime->tm.tm_min = ptime->tm.tm_sec = 0;

	/*
	 * Call a routine which sets ptime.tm and ptime.tspecs based
	 * on the given string and parsing-options.  Note that the
	 * routine should not call mktime to set ptime.tsecs.
	 */
	if (parseopts & PTM_PARSE_DWM)
		pres = parseDWM(ptime, str);
	else
		pres = parse8601(ptime, str);
	if (pres < 0) {
		ptime->tsecs = (time_t)pres;
		return (pres);
	}

	/*
	 * Before calling mktime, check to see if we ended up with a
	 * "day-of-month" that does not exist in the selected month.
	 * If we did call mktime with that info, then mktime will
	 * make it look like the user specifically requested a day
	 * in the following month (eg: Feb 31 turns into Mar 3rd).
	 */
	dpm = days_pmonth(ptime->tm.tm_mon, ptime->tm.tm_year);
	if ((parseopts & PTM_PARSE_MATCHDOM) &&
	    (ptime->tmspec & TSPEC_DAYOFMONTH) &&
	    (ptime->tm.tm_mday> dpm)) {
		/*
		 * ptime_nxtime() will want a ptime->tsecs value,
		 * but we need to avoid mktime resetting all the
		 * ptime->tm values.
		 */
		if (verbose && dbg_at_times > 1)
			fprintf(stderr,
			    "\t-- dom fixed: %4d/%02d/%02d %02d:%02d (%02d)",
			    ptime->tm.tm_year, ptime->tm.tm_mon,
			    ptime->tm.tm_mday, ptime->tm.tm_hour,
			    ptime->tm.tm_min, dpm);
		temp_tm = ptime->tm;
		ptime->tsecs = mktime(&temp_tm);
		if (ptime->tsecs > (time_t)-1)
			ptimeset_nxtime(ptime);
		if (verbose && dbg_at_times > 1)
			fprintf(stderr,
			    " to: %4d/%02d/%02d %02d:%02d\n",
			    ptime->tm.tm_year, ptime->tm.tm_mon,
			    ptime->tm.tm_mday, ptime->tm.tm_hour,
			    ptime->tm.tm_min);
	}

	/*
	 * Convert the ptime.tm into standard time_t seconds.  Check
	 * for invalid times, which includes things like the hour lost
	 * when switching from "standard time" to "daylight saving".
	 */
	ptime->tsecs = mktime(&ptime->tm);
	if (ptime->tsecs == (time_t)-1) {
		ptime->tsecs = (time_t)-2;
		return (-2);
	}

	return (0);
}

int
ptime_free(struct ptime_data *ptime)
{

	if (ptime == NULL)
		return (-1);

	free(ptime);
	return (0);
}

/*
 * Some trivial routines so ptime_data can remain a completely
 * opaque type.
 */
const char *
ptimeget_ctime(const struct ptime_data *ptime)
{

	if (ptime == NULL)
		return ("Null time in ptimeget_ctime()\n");

	return (ctime(&ptime->tsecs));
}

/*
 * Generate a time of day string in an RFC5424 compatible format. Return a
 * pointer to the buffer with the timestamp string or NULL if an error. If the
 * time is not supplied, cannot be converted to local time, or the resulting
 * string would overflow the buffer, the returned string will be the RFC5424
 * NILVALUE.
 */
char *
ptimeget_ctime_rfc5424(const struct ptime_data *ptime,
    char *timebuf, size_t bufsize)
{
	static const char NILVALUE[] = {"-"};	/* RFC5424 specified NILVALUE */
	int chars;
	struct tm tm;
	int tz_hours;
	int tz_mins;
	long tz_offset;
	char tz_sign;

	if (timebuf == NULL) {
		return (NULL);
	}

	if (bufsize < sizeof(NILVALUE)) {
		return (NULL);
	}

	/*
	 * Convert to localtime. RFC5424 mandates the use of the NILVALUE if
	 * the time cannot be obtained, so use that if there is an error in the
	 * conversion.
	 */
	if (ptime == NULL || localtime_r(&(ptime->tsecs), &tm) == NULL) {
		strlcpy(timebuf, NILVALUE, bufsize);
		return (timebuf);
	}

	/*
	 * Convert the time to a string in RFC5424 format. The conversion
	 * cannot be done with strftime() because it cannot produce the correct
	 * timezone offset format.
	 */
	if (tm.tm_gmtoff < 0) {
		tz_sign = '-';
		tz_offset = -tm.tm_gmtoff;
	} else {
		tz_sign = '+';
		tz_offset = tm.tm_gmtoff;
	}

	tz_hours = tz_offset / 3600;
	tz_mins = (tz_offset % 3600) / 60;

	chars = snprintf(timebuf, bufsize,
	    "%04d-%02d-%02d"	/* date */
	    "T%02d:%02d:%02d"	/* time */
	    "%c%02d:%02d",	/* time zone offset */
	    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	    tm.tm_hour, tm.tm_min, tm.tm_sec,
	    tz_sign, tz_hours, tz_mins);

	/* If the timestamp is too big for timebuf, return the NILVALUE. */
	if (chars >= (int)bufsize) {
		strlcpy(timebuf, NILVALUE, bufsize);
	}

	return (timebuf);
}

double
ptimeget_diff(const struct ptime_data *minuend, const struct
    ptime_data *subtrahend)
{

	/* Just like difftime(), we have no good error-return */
	if (minuend == NULL || subtrahend == NULL)
		return (0.0);

	return (difftime(minuend->tsecs, subtrahend->tsecs));
}

time_t
ptimeget_secs(const struct ptime_data *ptime)
{

	if (ptime == NULL)
		return (-1);

	return (ptime->tsecs);
}

/*
 * Generate an approximate timestamp for the next event, based on
 * what parts of time were specified by the original parameter to
 * ptime_relparse(). The result may be -1 if there is no obvious
 * "next time" which will work.
 */
int
ptimeset_nxtime(struct ptime_data *ptime)
{
	int moredays, tdpm, tmon, tyear;
	struct ptime_data nextmatch;

	if (ptime == NULL)
		return (-1);

	/*
	 * Changes are not made to the given time until after all
	 * of the calculations have been successful.
	 */
	nextmatch = *ptime;
	/*
	 * If the user specified a year and we're already past that
	 * time, then there will never be another one!
	 */
	if (ptime->tmspec & TSPEC_YEAR)
		return (-1);

	/*
	 * The caller gave us a time in the past.  Calculate how much
	 * time is needed to go from that valid rotate time to the
	 * next valid rotate time.  We only need to get to the nearest
	 * hour, because newsyslog is only run once per hour.
	 */
	moredays = 0;
	if (ptime->tmspec & TSPEC_MONTHOFYEAR) {
		/* Special case: Feb 29th does not happen every year. */
		if (ptime->tm.tm_mon == 1 && ptime->tm.tm_mday == 29) {
			nextmatch.tm.tm_year += 4;
			if (days_pmonth(1, nextmatch.tm.tm_year) < 29)
				nextmatch.tm.tm_year += 4;
		} else {
			nextmatch.tm.tm_year += 1;
		}
		nextmatch.tm.tm_isdst = -1;
		nextmatch.tsecs = mktime(&nextmatch.tm);

	} else if (ptime->tmspec & TSPEC_LDAYOFMONTH) {
		/*
		 * Need to get to the last day of next month.  Origtm is
		 * already at the last day of this month, so just add to
		 * it number of days in the next month.
		 */
		if (ptime->tm.tm_mon < 11)
			moredays = days_pmonth(ptime->tm.tm_mon + 1,
			    ptime->tm.tm_year);
		else
			moredays = days_pmonth(0, ptime->tm.tm_year + 1);

	} else if (ptime->tmspec & TSPEC_DAYOFMONTH) {
		/* Jump to the same day in the next month */
		moredays = days_pmonth(ptime->tm.tm_mon, ptime->tm.tm_year);
		/*
		 * In some cases, the next month may not *have* the
		 * desired day-of-the-month.  If that happens, then
		 * move to the next month that does have enough days.
		 */
		tmon = ptime->tm.tm_mon;
		tyear = ptime->tm.tm_year;
		for (;;) {
			if (tmon < 11)
				tmon += 1;
			else {
				tmon = 0;
				tyear += 1;
			}
			tdpm = days_pmonth(tmon, tyear);
			if (tdpm >= ptime->tm.tm_mday)
				break;
			moredays += tdpm;
		}

	} else if (ptime->tmspec & TSPEC_DAYOFWEEK) {
		moredays = 7;
	} else if (ptime->tmspec & TSPEC_HOUROFDAY) {
		moredays = 1;
	}

	if (moredays != 0) {
		nextmatch.tsecs += SECS_PER_HOUR * 24 * moredays;
		nextmatch.tm = *(localtime(&nextmatch.tsecs));
	}

	/*
	 * The new time will need to be adjusted if the setting of
	 * daylight-saving has changed between the two times.
	 */
	ptime_adjust4dst(&nextmatch, ptime);

	/* Everything worked.  Update the given time and return. */
	*ptime = nextmatch;
	return (0);
}

int
ptimeset_time(struct ptime_data *ptime, time_t secs)
{

	if (ptime == NULL)
		return (-1);

	ptime->tsecs = secs;
	ptime->tm = *(localtime(&ptime->tsecs));
	ptime->parseopts = 0;
	/* ptime->tmspec = ? */
	return (0);
}
