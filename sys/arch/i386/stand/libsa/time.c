/*	$OpenBSD: time.c,v 1.19 2014/09/23 17:59:25 brad Exp $	*/

/*
 * Copyright (c) 1997 Michael Shalayeff
 * Copyright (c) 1997 Tobias Weingartner
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/time.h>
#include <machine/biosvar.h>
#include <machine/pio.h>
#include "libsa.h"
#include "biosdev.h"

#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

/*
 * Convert from bcd (packed) to int
 */
static __inline u_int8_t
bcdtoint(u_int8_t c)
{
	return ((c & 0xf0) / 8) * 5 + (c & 0x0f);
}

/*
 * Quick compute of time in seconds since the Epoch
 */
const u_short monthcount[] = {
	0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365
};

static __inline time_t
compute(int year, u_int8_t month, u_int8_t day, u_int8_t hour,
    u_int8_t min, u_int8_t sec)
{
	/* Number of days per month */
	register time_t tt;

	/* Compute days */
	tt = (year - 1970) * 365 + monthcount[month] + day - 1;

	/* Compute for leap year */
	for (month <= 2 ? year-- : 0; year >= 1970; year--)
		if (isleap(year))
			tt++;

	/* Plus the time */
	tt = sec + 60 * (min + 60 * (tt * 24 + hour));

	return tt;
}

static int
bios_time_date(int f, u_int8_t *b)
{
	__asm volatile(DOINT(0x1a) "\n\t"
	    "setc %b0\n\t"
	    "movb %%ch, 0(%2)\n\t"
	    "movb %%cl, 1(%2)\n\t"
	    "movb %%dh, 2(%2)\n\t"
	    "movb %%dl, 3(%2)\n\t"
	    : "=a" (f)
	    : "0" (f), "r" (b) : "%ecx", "%edx", "cc");

	if (f & 0xff)
		return -1;
	else {
		b[0] = bcdtoint(b[0]);
		b[1] = bcdtoint(b[1]);
		b[2] = bcdtoint(b[2]);
		b[3] = bcdtoint(b[3]);
		return 0;
	}
}

static __inline int
biosdate(u_int8_t *b)
{
	return bios_time_date(4 << 8, b);
}

static __inline int
biostime(u_int8_t *b)
{
	return bios_time_date(2 << 8, b);
}

/*
 * Return time since epoch
 */
time_t
getsecs(void)
{
	u_int8_t timebuf[4], datebuf[4];

	/* Query BIOS for time & date */
	if (!biostime(timebuf) && !biosdate(datebuf)) {
#ifdef notdef
		int dst;

		dst = timebuf[3];
#endif
		/* Convert to seconds since Epoch */
		return compute(datebuf[0] * 100 + datebuf[1], datebuf[2],
		    datebuf[3], timebuf[0], timebuf[1], timebuf[2]);
	} else
		errno = EIO;

	return 1;
}

u_int
sleep(u_int i)
{
	register time_t t;

	/*
	 * Loop for the requested number of seconds, polling BIOS,
	 * so that it may handle interrupts.
	 */
	for (t = getsecs() + i; getsecs() < t; cnischar())
		;

	return 0;
}
