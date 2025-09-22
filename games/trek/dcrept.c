/*	$OpenBSD: dcrept.c,v 1.7 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: dcrept.c,v 1.3 1995/04/22 10:58:43 cgd Exp $	*/

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

#include <stdio.h>

#include "trek.h"

/*
**  damage control report
**
**	Print damages and time to fix.  This is taken from the event
**	list.  A couple of factors are set up, based on whether or not
**	we are docked.  (One of these factors will always be 1.0.)
**	The event list is then scanned for damage fix events, the
**	time until they occur is determined, and printed out.  The
**	magic number DAMFAC is used to tell how much faster you can
**	fix things if you are docked.
*/

void
dcrept(int v)
{
	int		i, f;
	double		x;
	double		m1, m2;
	struct event	*e;

	/* set up the magic factors to output the time till fixed */
	if (Ship.cond == DOCKED)
	{
		m1 = 1.0 / Param.dockfac;
		m2 = 1.0;
	}
	else
	{
		m1 = 1.0;
		m2 = Param.dockfac;
	}
	printf("Damage control report:\n");
	f = 1;

	/* scan for damages */
	for (i = 0; i < MAXEVENTS; i++)
	{
		e = &Event[i];
		if (e->evcode != E_FIXDV)
			continue;

		/* output the title first time */
		if (f)
		{
			printf("\t\t\t  repair times\n");
			printf("device\t\t\tin flight  docked\n");
			f = 0;
		}

		/* compute time till fixed, then adjust by the magic factors */
		x = e->date - Now.date;
		printf("%-24s%7.2f  %7.2f\n",
			Device[e->systemname].name, x * m1 + 0.005, x * m2 + 0.005);

		/* do a little consistancy checking */
	}

	/* if everything was ok, reassure the nervous captain */
	if (f)
		printf("All devices functional\n");
}
