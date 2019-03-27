/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1992-2009 Edwin Groothuis <edwin@FreeBSD.org>.
 * All rights reserved.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <time.h>

#include "calendar.h"

struct cal_year {
	int year;	/* 19xx, 20xx, 21xx */
	int easter;	/* Julian day */
	int paskha;	/* Julian day */
	int cny;	/* Julian day */
	int firstdayofweek; /* 0 .. 6 */
	struct cal_month *months;
	struct cal_year	*nextyear;
};

struct cal_month {
	int month;			/* 01 .. 12 */
	int firstdayjulian;		/* 000 .. 366 */
	int firstdayofweek;		/* 0 .. 6 */
	struct cal_year *year;		/* points back */
	struct cal_day *days;
	struct cal_month *nextmonth;
};

struct cal_day {
	int dayofmonth;			/* 01 .. 31 */
	int julianday;			/* 000 .. 366 */
	int dayofweek;			/* 0 .. 6 */
	struct cal_day *nextday;
	struct cal_month *month;	/* points back */
	struct cal_year	*year;		/* points back */
	struct event *events;
};

int debug_remember = 0;
static struct cal_year *hyear = NULL;

/* 1-based month, 0-based days, cumulative */
int	cumdaytab[][14] = {
	{0, -1, 30, 58, 89, 119, 150, 180, 211, 242, 272, 303, 333, 364},
	{0, -1, 30, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
};
/* 1-based month, individual */
static int *monthdays;
int	monthdaytab[][14] = {
	{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 30},
	{0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 30},
};

static struct cal_day *	find_day(int yy, int mm, int dd);

static void
createdate(int y, int m, int d)
{
	struct cal_year *py, *pyp;
	struct cal_month *pm, *pmp;
	struct cal_day *pd, *pdp;
	int *cumday;

	pyp = NULL;
	py = hyear;
	while (py != NULL) {
		if (py->year == y + 1900)
			break;
		pyp = py;
		py = py->nextyear;
	}

	if (py == NULL) {
		struct tm td;
		time_t t;
		py = (struct cal_year *)calloc(1, sizeof(struct cal_year));
		py->year = y + 1900;
		py->easter = easter(y);
		py->paskha = paskha(y);

		td = tm0;
		td.tm_year = y;
		td.tm_mday = 1;
		t = mktime(&td);
		localtime_r(&t, &td);
		py->firstdayofweek = td.tm_wday;

		if (pyp != NULL)
			pyp->nextyear = py;
	}
	if (pyp == NULL) {
		/* The very very very first one */
		hyear = py;
	}

	pmp = NULL;
	pm = py->months;
	while (pm != NULL) {
		if (pm->month == m)
			break;
		pmp = pm;
		pm = pm->nextmonth;
	}

	if (pm == NULL) {
		pm = (struct cal_month *)calloc(1, sizeof(struct cal_month));
		pm->year = py;
		pm->month = m;
		cumday = cumdaytab[isleap(y)];
		pm->firstdayjulian = cumday[m] + 2;
		pm->firstdayofweek =
		    (py->firstdayofweek + pm->firstdayjulian -1) % 7;
		if (pmp != NULL)
			pmp->nextmonth = pm;
	}
	if (pmp == NULL)
		py->months = pm;

	pdp = NULL;
	pd = pm->days;
	while (pd != NULL) {
		pdp = pd;
		pd = pd->nextday;
	}

	if (pd == NULL) {	/* Always true */
		pd = (struct cal_day *)calloc(1, sizeof(struct cal_day));
		pd->month = pm;
		pd->year = py;
		pd->dayofmonth = d;
		pd->julianday = pm->firstdayjulian + d - 1;
		pd->dayofweek = (pm->firstdayofweek + d - 1) % 7;
		if (pdp != NULL)
			pdp->nextday = pd;
	}
	if (pdp == NULL)
		pm->days = pd;
}

void
generatedates(struct tm *tp1, struct tm *tp2)
{
	int y1, m1, d1;
	int y2, m2, d2;
	int y, m, d;

	y1 = tp1->tm_year;
	m1 = tp1->tm_mon + 1;
	d1 = tp1->tm_mday;
	y2 = tp2->tm_year;
	m2 = tp2->tm_mon + 1;
	d2 = tp2->tm_mday;

	if (y1 == y2) {
		if (m1 == m2) {
			/* Same year, same month. Easy! */
			for (d = d1; d <= d2; d++)
				createdate(y1, m1, d);
			return;
		}
		/*
		 * Same year, different month.
		 * - Take the leftover days from m1
		 * - Take all days from <m1 .. m2>
		 * - Take the first days from m2
		 */
		monthdays = monthdaytab[isleap(y1)];
		for (d = d1; d <= monthdays[m1]; d++)
			createdate(y1, m1, d);
		for (m = m1 + 1; m < m2; m++)
			for (d = 1; d <= monthdays[m]; d++)
				createdate(y1, m, d);
		for (d = 1; d <= d2; d++)
			createdate(y1, m2, d);
		return;
	}
	/*
	 * Different year, different month.
	 * - Take the leftover days from y1-m1
	 * - Take all days from y1-<m1 .. 12]
	 * - Take all days from <y1 .. y2>
	 * - Take all days from y2-[1 .. m2>
	 * - Take the first days of y2-m2
	 */
	monthdays = monthdaytab[isleap(y1)];
	for (d = d1; d <= monthdays[m1]; d++)
		createdate(y1, m1, d);
	for (m = m1 + 1; m <= 12; m++)
		for (d = 1; d <= monthdays[m]; d++)
			createdate(y1, m, d);
	for (y = y1 + 1; y < y2; y++) {
		monthdays = monthdaytab[isleap(y)];
		for (m = 1; m <= 12; m++)
			for (d = 1; d <= monthdays[m]; d++)
				createdate(y, m, d);
	}
	monthdays = monthdaytab[isleap(y2)];
	for (m = 1; m < m2; m++)
		for (d = 1; d <= monthdays[m]; d++)
			createdate(y2, m, d);
	for (d = 1; d <= d2; d++)
		createdate(y2, m2, d);
}

void
dumpdates(void)
{
	struct cal_year *y;
	struct cal_month *m;
	struct cal_day *d;

	y = hyear;
	while (y != NULL) {
		printf("%-5d (wday:%d)\n", y->year, y->firstdayofweek);
		m = y->months;
		while (m != NULL) {
			printf("-- %-5d (julian:%d, dow:%d)\n", m->month,
			    m->firstdayjulian, m->firstdayofweek);
			d = m->days;
			while (d != NULL) {
				printf("  -- %-5d (julian:%d, dow:%d)\n",
				    d->dayofmonth, d->julianday, d->dayofweek);
				d = d->nextday;
			}
			m = m->nextmonth;
		}
		y = y->nextyear;
	}
}

int
remember_ymd(int yy, int mm, int dd)
{
	struct cal_year *y;
	struct cal_month *m;
	struct cal_day *d;

	if (debug_remember)
		printf("remember_ymd: %d - %d - %d\n", yy, mm, dd);

	y = hyear;
	while (y != NULL) {
		if (y->year != yy) {
			y = y->nextyear;
			continue;
		}
		m = y->months;
		while (m != NULL) {
			if (m->month != mm) {
				m = m->nextmonth;
				continue;
			}
			d = m->days;
			while (d != NULL) {
				if (d->dayofmonth == dd)
					return (1);
				d = d->nextday;
				continue;
			}
			return (0);
		}
		return (0);
	}
	return (0);
}

int
remember_yd(int yy, int dd, int *rm, int *rd)
{
	struct cal_year *y;
	struct cal_month *m;
	struct cal_day *d;

	if (debug_remember)
		printf("remember_yd: %d - %d\n", yy, dd);

	y = hyear;
	while (y != NULL) {
		if (y->year != yy) {
			y = y->nextyear;
			continue;
		}
		m = y->months;
		while (m != NULL) {
			d = m->days;
			while (d != NULL) {
				if (d->julianday == dd) {
					*rm = m->month;
					*rd = d->dayofmonth;
					return (1);
				}
				d = d->nextday;
			}
			m = m->nextmonth;
		}
		return (0);
	}
	return (0);
}

int
first_dayofweek_of_year(int yy)
{
	struct cal_year *y;

	y = hyear;
	while (y != NULL) {
		if (y->year == yy)
			return (y->firstdayofweek);
		y = y->nextyear;
	}

	/* Should not happen */
	return (-1);
}

int
first_dayofweek_of_month(int yy, int mm)
{
	struct cal_year *y;
	struct cal_month *m;

	y = hyear;
	while (y != NULL) {
		if (y->year != yy) {
			y = y->nextyear;
			continue;
		}
		m = y->months;
		while (m != NULL) {
			if (m->month == mm)
				return (m->firstdayofweek);
			m = m->nextmonth;
		}
		/* No data for this month */
		return (-1);
	}

	/* No data for this year.  Error? */
        return (-1);
}

int
walkthrough_dates(struct event **e)
{
	static struct cal_year *y = NULL;
	static struct cal_month *m = NULL;
	static struct cal_day *d = NULL;

	if (y == NULL) {
		y = hyear;
		m = y->months;
		d = m->days;
		*e = d->events;
		return (1);
	}
	if (d->nextday != NULL) {
		d = d->nextday;
		*e = d->events;
		return (1);
	}
	if (m->nextmonth != NULL) {
		m = m->nextmonth;
		d = m->days;
		*e = d->events;
		return (1);
	}
	if (y->nextyear != NULL) {
		y = y->nextyear;
		m = y->months;
		d = m->days;
		*e = d->events;
		return (1);
	}

	return (0);
}

static struct cal_day *
find_day(int yy, int mm, int dd)
{
	struct cal_year *y;
	struct cal_month *m;
	struct cal_day *d;

	if (debug_remember)
		printf("remember_ymd: %d - %d - %d\n", yy, mm, dd);

	y = hyear;
	while (y != NULL) {
		if (y->year != yy) {
			y = y->nextyear;
			continue;
		}
		m = y->months;
		while (m != NULL) {
			if (m->month != mm) {
				m = m->nextmonth;
				continue;
			}
			d = m->days;
			while (d != NULL) {
				if (d->dayofmonth == dd)
					return (d);
				d = d->nextday;
				continue;
			}
			return (NULL);
		}
		return (NULL);
	}
	return (NULL);
}

void
addtodate(struct event *e, int year, int month, int day)
{
	struct cal_day *d;

	d = find_day(year, month, day);
	e->next = d->events;
	d->events = e;
}
