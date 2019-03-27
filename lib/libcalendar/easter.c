/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Wolfgang Helbig
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "calendar.h"

typedef struct date date;

static int	 easterodn(int y);

/* Compute Easter Sunday in Gregorian Calendar */
date *
easterg(int y, date *dt)
{
	int c, i, j, k, l, n;

	n = y % 19;
	c = y / 100;
	k = (c - 17) / 25;
	i = (c - c/4 -(c-k)/3 + 19 * n + 15) % 30;
	i = i -(i/28) * (1 - (i/28) * (29/(i + 1)) * ((21 - n)/11));
	j = (y + y/4 + i + 2 - c + c/4) % 7;
	l = i - j;
	dt->m = 3 + (l + 40) / 44;
	dt->d = l + 28 - 31*(dt->m / 4);
	dt->y = y;
	return (dt);
}

/* Compute the Gregorian date of Easter Sunday in Julian Calendar */
date *
easterog(int y, date *dt)
{

	return (gdate(easterodn(y), dt));
}

/* Compute the Julian date of Easter Sunday in Julian Calendar */
date   *
easteroj(int y, date * dt)
{

	return (jdate(easterodn(y), dt));
}

/* Compute the day number of Easter Sunday in Julian Calendar */
static int
easterodn(int y)
{
	/*
	 * Table for the easter limits in one metonic (19-year) cycle. 21
	 * to 31 is in March, 1 through 18 in April. Easter is the first
	 * sunday after the easter limit.
	 */
	int     mc[] = {5, 25, 13, 2, 22, 10, 30, 18, 7, 27, 15, 4,
		    24, 12, 1, 21, 9, 29, 17};

	/* Offset from a weekday to next sunday */
	int     ns[] = {6, 5, 4, 3, 2, 1, 7};
	date	dt;
	int     dn;

	/* Assign the easter limit of y to dt */
	dt.d = mc[y % 19];

	if (dt.d < 21)
		dt.m = 4;
	else
		dt.m = 3;

	dt.y = y;

	/* Return the next sunday after the easter limit */
	dn = ndaysj(&dt);
	return (dn + ns[weekday(dn)]);
}
