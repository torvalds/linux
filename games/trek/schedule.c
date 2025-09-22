/*	$OpenBSD: schedule.c,v 1.8 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: schedule.c,v 1.3 1995/04/22 10:59:23 cgd Exp $	*/

/*
 * Copyright (c) 1980, 1993
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

#include <err.h>
#include <math.h>
#include <stdio.h>

#include "trek.h"

/*
**  SCHEDULE AN EVENT
**
**	An event of type 'type' is scheduled for time NOW + 'offset'
**	into the first available slot.  'x', 'y', and 'z' are
**	considered the attributes for this event.
**
**	The address of the slot is returned.
*/

struct event *
schedule(int type, double offset, int x, int y, int z)
{
	struct event	*e;
	int		i;
	double		date;

	date = Now.date + offset;
	for (i = 0; i < MAXEVENTS; i++)
	{
		e = &Event[i];
		if (e->evcode)
			continue;
		/* got a slot */
#		ifdef xTRACE
		if (Trace)
			printf("schedule: type %d @ %.2f slot %d parm %d %d %d\n",
				type, date, i, x, y, z);
#		endif
		e->evcode = type;
		e->date = date;
		e->x = x;
		e->y = y;
		e->systemname = z;
		Now.eventptr[type & E_EVENT] = e;
		return (e);
	}
	errx(1, "Cannot schedule event %d parm %d %d %d", type, x, y, z);
}


/*
**  RESCHEDULE AN EVENT
**
**	The event pointed to by 'e' is rescheduled to the current
**	time plus 'offset'.
*/

void
reschedule(struct event *e1, double offset)
{
	double		date;
	struct event	*e;

	e = e1;

	date = Now.date + offset;
	e->date = date;
#	ifdef xTRACE
	if (Trace)
		printf("reschedule: type %d parm %d %d %d @ %.2f\n",
			e->evcode, e->x, e->y, e->systemname, date);
#	endif
	return;
}


/*
**  UNSCHEDULE AN EVENT
**
**	The event at slot 'e' is deleted.
*/

void
unschedule(struct event *e1)
{
	struct event	*e;

	e = e1;

#	ifdef xTRACE
	if (Trace)
		printf("unschedule: type %d @ %.2f parm %d %d %d\n",
			e->evcode, e->date, e->x, e->y, e->systemname);
#	endif
	Now.eventptr[e->evcode & E_EVENT] = 0;
	e->date = 1e50;
	e->evcode = 0;
	return;
}


/*
**  Abreviated schedule routine
**
**	Parameters are the event index and a factor for the time
**	figure.
*/

struct event *
xsched(int ev1, int factor, int x, int y, int z)
{
	int	ev;

	ev = ev1;
	return (schedule(ev, -Param.eventdly[ev] * Param.time * log(franf()) / factor, x, y, z));
}


/*
**  Simplified reschedule routine
**
**	Parameters are the event index, the initial date, and the
**	division factor.  Look at the code to see what really happens.
*/

void
xresched(struct event *e1, int ev1, int factor)
{
	int		ev;
	struct event	*e;

	ev = ev1;
	e = e1;
	reschedule(e, -Param.eventdly[ev] * Param.time * log(franf()) / factor);
}
