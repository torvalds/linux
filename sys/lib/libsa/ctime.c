/*	$OpenBSD: ctime.c,v 1.6 2018/05/23 16:23:48 cheloha Exp $	*/

/*
 * Copyright (c) 1998 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/time.h>
#include "stand.h"

#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

char *
ctime(const time_t *clock)
{
	static const char wdays[][4] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static const char months[][4] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	static const u_int monthcnt[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};
	static char buf[64];
	int ss, mm, hh, wday, month, year;
	time_t tt = *clock;

	ss = tt % 60;
	tt /= 60;	/* minutes */
	mm = tt % 60;
	tt /= 60;	/* hours */
	hh = tt % 24;
	tt /= 24;	/* days */
	wday = (4 + tt) % 7;	/* weekday, 'twas thursday when time started */

	for (year = 1970; tt >= 365; year++)
		tt -= isleap(year)? 366: 365;

	tt++;	/* days are 1-based */

	for (month = 0; tt > monthcnt[month]; month++)
		tt -= monthcnt[month];

	if (month > 2 && isleap(year))
		tt--;

	snprintf(buf, sizeof buf, "%s %s%3d %02d:%02d:%02d %d\n",
	    ((wday  < 0 || wday  >=  7)? "???": wdays[wday]),
	    ((month < 0 || month >= 12)? "???": months[month]),
	    (int)tt, hh, mm, ss, year);
	return buf;
}
