/*	$OpenBSD: dock.c,v 1.7 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: dock.c,v 1.3 1995/04/22 10:58:45 cgd Exp $	*/

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
**  DOCK TO STARBASE
**
**	The starship is docked to a starbase.  For this to work you
**	must be adjacent to a starbase.
**
**	You get your supplies replenished and your captives are
**	disembarked.  Note that your score is updated now, not when
**	you actually take the captives.
**
**	Any repairs that need to be done are rescheduled to take
**	place sooner.  This provides for the faster repairs when you
**	are docked.
*/

void
dock(int v)
{
	int		i, j;
	int		ok;
	struct event	*e;

	if (Ship.cond == DOCKED)
	{
		printf("Chekov: But captain, we are already docked\n");
		return;
	}
	/* check for ok to dock, i.e., adjacent to a starbase */
	ok = 0;
	for (i = Ship.sectx - 1; i <= Ship.sectx + 1 && !ok; i++)
	{
		if (i < 0 || i >= NSECTS)
			continue;
		for (j = Ship.secty - 1; j <= Ship.secty + 1; j++)
		{
			if (j  < 0 || j >= NSECTS)
				continue;
			if (Sect[i][j] == BASE)
			{
				ok++;
				break;
			}
		}
	}
	if (!ok)
	{
		printf("Chekov: But captain, we are not adjacent to a starbase.\n");
		return;
	}

	/* restore resources */
	Ship.energy = Param.energy;
	Ship.torped = Param.torped;
	Ship.shield = Param.shield;
	Ship.crew = Param.crew;
	Game.captives += Param.brigfree - Ship.brigfree;
	Ship.brigfree = Param.brigfree;

	/* reset ship's defenses */
	Ship.shldup = 0;
	Ship.cloaked = 0;
	Ship.cond = DOCKED;
	Ship.reserves = Param.reserves;

	/* recalibrate space inertial navigation system */
	Ship.sinsbad = 0;

	/* output any saved radio messages */
	dumpssradio();

	/* reschedule any device repairs */
	for (i = 0; i < MAXEVENTS; i++)
	{
		e = &Event[i];
		if (e->evcode != E_FIXDV)
			continue;
		reschedule(e, (e->date - Now.date) * Param.dockfac);
	}
}


/*
**  LEAVE A STARBASE
**
**	This is the inverse of dock().  The main function it performs
**	is to reschedule any damages so that they will take longer.
*/

void
undock(int v)
{
	struct event	*e;
	int		i;

	if (Ship.cond != DOCKED)
	{
		printf("Sulu: Pardon me captain, but we are not docked.\n");
		return;
	}
	Ship.cond = GREEN;
	Move.free = 0;

	/* reschedule device repair times (again) */
	for (i = 0; i < MAXEVENTS; i++)
	{
		e = &Event[i];
		if (e->evcode != E_FIXDV)
			continue;
		reschedule(e, (e->date - Now.date) / Param.dockfac);
	}
}
