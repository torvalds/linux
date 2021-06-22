// SPDX-License-Identifier: LGPL-2.0+
/*
 * Copyright (C) 1993, 1994, 1995, 1996, 1997 Free Software Foundation, Inc.
 * This file is part of the GNU C Library.
 * Contributed by Paul Eggert (eggert@twinsun.com).
 *
 * The GNU C Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The GNU C Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the GNU C Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Converts the calendar time to broken-down time representation
 *
 * 2009-7-14:
 *   Moved from glibc-2.6 to kernel by Zhaolei<zhaolei@cn.fujitsu.com>
 * 2021-06-02:
 *   Reimplemented by Cassio Neri <cassio.neri@gmail.com>
 */

#include <linux/time.h>
#include <linux/module.h>
#include <linux/kernel.h>

#define SECS_PER_HOUR	(60 * 60)
#define SECS_PER_DAY	(SECS_PER_HOUR * 24)

/**
 * time64_to_tm - converts the calendar time to local broken-down time
 *
 * @totalsecs:	the number of seconds elapsed since 00:00:00 on January 1, 1970,
 *		Coordinated Universal Time (UTC).
 * @offset:	offset seconds adding to totalsecs.
 * @result:	pointer to struct tm variable to receive broken-down time
 */
void time64_to_tm(time64_t totalsecs, int offset, struct tm *result)
{
	u32 u32tmp, day_of_century, year_of_century, day_of_year, month, day;
	u64 u64tmp, udays, century, year;
	bool is_Jan_or_Feb, is_leap_year;
	long days, rem;
	int remainder;

	days = div_s64_rem(totalsecs, SECS_PER_DAY, &remainder);
	rem = remainder;
	rem += offset;
	while (rem < 0) {
		rem += SECS_PER_DAY;
		--days;
	}
	while (rem >= SECS_PER_DAY) {
		rem -= SECS_PER_DAY;
		++days;
	}

	result->tm_hour = rem / SECS_PER_HOUR;
	rem %= SECS_PER_HOUR;
	result->tm_min = rem / 60;
	result->tm_sec = rem % 60;

	/* January 1, 1970 was a Thursday. */
	result->tm_wday = (4 + days) % 7;
	if (result->tm_wday < 0)
		result->tm_wday += 7;

	/*
	 * The following algorithm is, basically, Proposition 6.3 of Neri
	 * and Schneider [1]. In a few words: it works on the computational
	 * (fictitious) calendar where the year starts in March, month = 2
	 * (*), and finishes in February, month = 13. This calendar is
	 * mathematically convenient because the day of the year does not
	 * depend on whether the year is leap or not. For instance:
	 *
	 * March 1st		0-th day of the year;
	 * ...
	 * April 1st		31-st day of the year;
	 * ...
	 * January 1st		306-th day of the year; (Important!)
	 * ...
	 * February 28th	364-th day of the year;
	 * February 29th	365-th day of the year (if it exists).
	 *
	 * After having worked out the date in the computational calendar
	 * (using just arithmetics) it's easy to convert it to the
	 * corresponding date in the Gregorian calendar.
	 *
	 * [1] "Euclidean Affine Functions and Applications to Calendar
	 * Algorithms". https://arxiv.org/abs/2102.06959
	 *
	 * (*) The numbering of months follows tm more closely and thus,
	 * is slightly different from [1].
	 */

	udays	= ((u64) days) + 2305843009213814918ULL;

	u64tmp		= 4 * udays + 3;
	century		= div64_u64_rem(u64tmp, 146097, &u64tmp);
	day_of_century	= (u32) (u64tmp / 4);

	u32tmp		= 4 * day_of_century + 3;
	u64tmp		= 2939745ULL * u32tmp;
	year_of_century	= upper_32_bits(u64tmp);
	day_of_year	= lower_32_bits(u64tmp) / 2939745 / 4;

	year		= 100 * century + year_of_century;
	is_leap_year	= year_of_century ? !(year_of_century % 4) : !(century % 4);

	u32tmp		= 2141 * day_of_year + 132377;
	month		= u32tmp >> 16;
	day		= ((u16) u32tmp) / 2141;

	/*
	 * Recall that January 1st is the 306-th day of the year in the
	 * computational (not Gregorian) calendar.
	 */
	is_Jan_or_Feb	= day_of_year >= 306;

	/* Convert to the Gregorian calendar and adjust to Unix time. */
	year		= year + is_Jan_or_Feb - 6313183731940000ULL;
	month		= is_Jan_or_Feb ? month - 12 : month;
	day		= day + 1;
	day_of_year	+= is_Jan_or_Feb ? -306 : 31 + 28 + is_leap_year;

	/* Convert to tm's format. */
	result->tm_year = (long) (year - 1900);
	result->tm_mon  = (int) month;
	result->tm_mday = (int) day;
	result->tm_yday = (int) day_of_year;
}
EXPORT_SYMBOL(time64_to_tm);
