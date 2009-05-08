/* Copyright (C) 1993, 1994, 1995, 1996, 1997 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Paul Eggert (eggert@twinsun.com).

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/*
 * dgb 10/02/98: ripped this from glibc source to help convert timestamps
 *               to unix time
 *     10/04/98: added new table-based lookup after seeing how ugly
 *               the gnu code is
 * blf 09/27/99: ripped out all the old code and inserted new table from
 *		 John Brockmeyer (without leap second corrections)
 *		 rewrote udf_stamp_to_time and fixed timezone accounting in
 *		 udf_time_to_stamp.
 */

/*
 * We don't take into account leap seconds. This may be correct or incorrect.
 * For more NIST information (especially dealing with leap seconds), see:
 * http://www.boulder.nist.gov/timefreq/pubs/bulletin/leapsecond.htm
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include "udfdecl.h"

#define EPOCH_YEAR 1970

#ifndef __isleap
/* Nonzero if YEAR is a leap year (every 4 years,
   except every 100th isn't, and every 400th is).  */
#define	__isleap(year)	\
  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))
#endif

/* How many days come before each month (0-12).  */
static const unsigned short int __mon_yday[2][13] = {
	/* Normal years.  */
	{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
	/* Leap years.  */
	{0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366}
};

#define MAX_YEAR_SECONDS	69
#define SPD			0x15180	/*3600*24 */
#define SPY(y, l, s)		(SPD * (365 * y + l) + s)

static time_t year_seconds[MAX_YEAR_SECONDS] = {
/*1970*/ SPY(0,   0, 0), SPY(1,   0, 0), SPY(2,   0, 0), SPY(3,   1, 0),
/*1974*/ SPY(4,   1, 0), SPY(5,   1, 0), SPY(6,   1, 0), SPY(7,   2, 0),
/*1978*/ SPY(8,   2, 0), SPY(9,   2, 0), SPY(10,  2, 0), SPY(11,  3, 0),
/*1982*/ SPY(12,  3, 0), SPY(13,  3, 0), SPY(14,  3, 0), SPY(15,  4, 0),
/*1986*/ SPY(16,  4, 0), SPY(17,  4, 0), SPY(18,  4, 0), SPY(19,  5, 0),
/*1990*/ SPY(20,  5, 0), SPY(21,  5, 0), SPY(22,  5, 0), SPY(23,  6, 0),
/*1994*/ SPY(24,  6, 0), SPY(25,  6, 0), SPY(26,  6, 0), SPY(27,  7, 0),
/*1998*/ SPY(28,  7, 0), SPY(29,  7, 0), SPY(30,  7, 0), SPY(31,  8, 0),
/*2002*/ SPY(32,  8, 0), SPY(33,  8, 0), SPY(34,  8, 0), SPY(35,  9, 0),
/*2006*/ SPY(36,  9, 0), SPY(37,  9, 0), SPY(38,  9, 0), SPY(39, 10, 0),
/*2010*/ SPY(40, 10, 0), SPY(41, 10, 0), SPY(42, 10, 0), SPY(43, 11, 0),
/*2014*/ SPY(44, 11, 0), SPY(45, 11, 0), SPY(46, 11, 0), SPY(47, 12, 0),
/*2018*/ SPY(48, 12, 0), SPY(49, 12, 0), SPY(50, 12, 0), SPY(51, 13, 0),
/*2022*/ SPY(52, 13, 0), SPY(53, 13, 0), SPY(54, 13, 0), SPY(55, 14, 0),
/*2026*/ SPY(56, 14, 0), SPY(57, 14, 0), SPY(58, 14, 0), SPY(59, 15, 0),
/*2030*/ SPY(60, 15, 0), SPY(61, 15, 0), SPY(62, 15, 0), SPY(63, 16, 0),
/*2034*/ SPY(64, 16, 0), SPY(65, 16, 0), SPY(66, 16, 0), SPY(67, 17, 0),
/*2038*/ SPY(68, 17, 0)
};

extern struct timezone sys_tz;

#define SECS_PER_HOUR	(60 * 60)
#define SECS_PER_DAY	(SECS_PER_HOUR * 24)

struct timespec *
udf_disk_stamp_to_time(struct timespec *dest, struct timestamp src)
{
	int yday;
	u16 typeAndTimezone = le16_to_cpu(src.typeAndTimezone);
	u16 year = le16_to_cpu(src.year);
	uint8_t type = typeAndTimezone >> 12;
	int16_t offset;

	if (type == 1) {
		offset = typeAndTimezone << 4;
		/* sign extent offset */
		offset = (offset >> 4);
		if (offset == -2047) /* unspecified offset */
			offset = 0;
	} else
		offset = 0;

	if ((year < EPOCH_YEAR) ||
	    (year >= EPOCH_YEAR + MAX_YEAR_SECONDS)) {
		return NULL;
	}
	dest->tv_sec = year_seconds[year - EPOCH_YEAR];
	dest->tv_sec -= offset * 60;

	yday = ((__mon_yday[__isleap(year)][src.month - 1]) + src.day - 1);
	dest->tv_sec += (((yday * 24) + src.hour) * 60 + src.minute) * 60 + src.second;
	dest->tv_nsec = 1000 * (src.centiseconds * 10000 +
			src.hundredsOfMicroseconds * 100 + src.microseconds);
	return dest;
}

struct timestamp *
udf_time_to_disk_stamp(struct timestamp *dest, struct timespec ts)
{
	long int days, rem, y;
	const unsigned short int *ip;
	int16_t offset;

	offset = -sys_tz.tz_minuteswest;

	if (!dest)
		return NULL;

	dest->typeAndTimezone = cpu_to_le16(0x1000 | (offset & 0x0FFF));

	ts.tv_sec += offset * 60;
	days = ts.tv_sec / SECS_PER_DAY;
	rem = ts.tv_sec % SECS_PER_DAY;
	dest->hour = rem / SECS_PER_HOUR;
	rem %= SECS_PER_HOUR;
	dest->minute = rem / 60;
	dest->second = rem % 60;
	y = 1970;

#define DIV(a, b) ((a) / (b) - ((a) % (b) < 0))
#define LEAPS_THRU_END_OF(y) (DIV (y, 4) - DIV (y, 100) + DIV (y, 400))

	while (days < 0 || days >= (__isleap(y) ? 366 : 365)) {
		long int yg = y + days / 365 - (days % 365 < 0);

		/* Adjust DAYS and Y to match the guessed year.  */
		days -= ((yg - y) * 365
			 + LEAPS_THRU_END_OF(yg - 1)
			 - LEAPS_THRU_END_OF(y - 1));
		y = yg;
	}
	dest->year = cpu_to_le16(y);
	ip = __mon_yday[__isleap(y)];
	for (y = 11; days < (long int)ip[y]; --y)
		continue;
	days -= ip[y];
	dest->month = y + 1;
	dest->day = days + 1;

	dest->centiseconds = ts.tv_nsec / 10000000;
	dest->hundredsOfMicroseconds = (ts.tv_nsec / 1000 -
					dest->centiseconds * 10000) / 100;
	dest->microseconds = (ts.tv_nsec / 1000 - dest->centiseconds * 10000 -
			      dest->hundredsOfMicroseconds * 100);
	return dest;
}

/* EOF */
