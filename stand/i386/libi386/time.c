/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
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

#include <stand.h>
#include <btxv86.h>
#include "bootstrap.h"
#include "libi386.h"

time_t		getsecs(void);
static int	bios_seconds(void);

/*
 * Return the BIOS time-of-day value.
 *
 * XXX uses undocumented BCD support from libstand.
 */
static int
bios_seconds(void)
{
    int			hr, minute, sec;
    
    v86.ctl = 0;
    v86.addr = 0x1a;		/* int 0x1a, function 2 */
    v86.eax = 0x0200;
    v86int();

    hr = bcd2bin((v86.ecx & 0xff00) >> 8);	/* hour in %ch */
    minute = bcd2bin(v86.ecx & 0xff);		/* minute in %cl */
    sec = bcd2bin((v86.edx & 0xff00) >> 8);	/* second in %dh */
    
    return (hr * 3600 + minute * 60 + sec);
}

/*
 * Return the time in seconds since the beginning of the day.
 *
 * Some BIOSes (notably qemu) don't correctly read the RTC
 * registers in an atomic way, sometimes returning bogus values.
 * Therefore we "debounce" the reading by accepting it only when
 * we got 8 identical values in succession.
 *
 * If we pass midnight, don't wrap back to 0.
 */
time_t
time(time_t *t)
{
    static time_t lasttime;
    time_t now, check;
    int same, try;

    same = try = 0;
    check = bios_seconds();
    do {
	now = check;
	check = bios_seconds();
	if (check != now)
	    same = 0;
    } while (++same < 8 && ++try < 1000);

    if (now < lasttime)
	now += 24 * 3600;
    lasttime = now;
    
    if (t != NULL)
	*t = now;
    return(now);
}

time_t
getsecs(void)
{
	time_t n = 0;
	time(&n);
	return n;
}

/*
 * Use the BIOS Wait function to pause for (period) microseconds.
 *
 * Resolution of this function is variable, but typically around
 * 1ms.
 */
void
delay(int period)
{
    v86.ctl = 0;
    v86.addr = 0x15;		/* int 0x15, function 0x86 */
    v86.eax = 0x8600;
    v86.ecx = period >> 16;
    v86.edx = period & 0xffff;
    v86int();
}
