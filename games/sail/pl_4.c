/*	$OpenBSD: pl_4.c,v 1.7 2016/01/08 20:26:33 mestre Exp $	*/
/*	$NetBSD: pl_4.c,v 1.4 1995/04/24 12:25:17 cgd Exp $	*/

/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 */

#include <ctype.h>

#include "extern.h"
#include "player.h"

void
changesail(void)
{
	int rig, full;

	rig = mc->rig1;
	full = mf->FS;
	if (windspeed == 6 || (windspeed == 5 && mc->class > 4))
		rig = 0;
	if (mc->crew3 && rig) {
		if (!full) {
			if (sgetch("Increase to Full sails? ",
				(struct ship *)0, 1) == 'y') {
				changed = 1;
				Write(W_FS, ms, 1, 0, 0, 0);
			}
		} else {
			if (sgetch("Reduce to Battle sails? ",
				(struct ship *)0, 1) == 'y') {
				Write(W_FS, ms, 0, 0, 0, 0);
				changed = 1;
			}
		}
	} else if (!rig)
		Msg("Sails rent to pieces");
}

void
acceptsignal(void)
{
	char buf[60];
	char *p = buf;

	*p++ = '"';
	sgetstr("Message? ", p, sizeof buf - 2);
	while (*p++)
		;
	p[-1] = '"';
	*p = 0;
	Writestr(W_SIGNAL, ms, buf);
}

void
lookout(void)
{
	struct ship *sp;
	char buf[3];
	char c;

	sgetstr("What ship? ", buf, sizeof buf);
	foreachship(sp) {
		c = *countryname[sp->nationality];
		if ((c == *buf || tolower((unsigned char)c) == *buf || colours(sp) == *buf)
		    && (sp->file->stern == buf[1] || sterncolour(sp) == buf[1]
			|| buf[1] == '?')) {
			eyeball(sp);
		}
	}
}

const char *
saywhat(struct ship *sp, int flag)
{
	if (sp->file->captain[0])
		return sp->file->captain;
	else if (sp->file->struck)
		return "(struck)";
	else if (sp->file->captured != 0)
		return "(captured)";
	else if (flag)
		return "(available)";
	else
		return "(computer)";
}

void
eyeball(struct ship *ship)
{
	int i;

	if (ship->file->dir != 0) {
		Msg("Sail ho! (range %d, %s)",
		    range(ms, ship), saywhat(ship, 0));
		i = portside(ms, ship, 1) - mf->dir;
		if (i <= 0)
			i += 8;
		Signal("$$ %s %s %s.",
			ship, countryname[ship->nationality],
			classname[ship->specs->class], directionname[i]);
	}
}
