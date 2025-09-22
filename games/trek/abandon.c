/*	$OpenBSD: abandon.c,v 1.8 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: abandon.c,v 1.3 1995/04/22 10:58:24 cgd Exp $	*/

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
**  Abandon Ship
**
**	The ship is abandoned.  If your current ship is the Faire
**	Queene, or if your shuttlecraft is dead, you're out of
**	luck.  You need the shuttlecraft in order for the captain
**	(that's you!!) to escape.
**
**	Your crew can beam to an inhabited starsystem in the
**	quadrant, if there is one and if the transporter is working.
**	If there is no inhabited starsystem, or if the transporter
**	is out, they are left to die in outer space.
**
**	These currently just count as regular deaths, but they
**	should count very heavily against you.
**
**	If there are no starbases left, you are captured by the
**	Klingons, who torture you mercilessly.  However, if there
**	is at least one starbase, you are returned to the
**	Federation in a prisoner of war exchange.  Of course, this
**	can't happen unless you have taken some prisoners.
*/

void
abandon(int v)
{
	struct quad	*q;
	int		i;
	int		j;
	struct event	*e;

	if (Ship.ship == QUEENE)
	{
		printf("You may not abandon ye Faire Queene\n");
		return;
	}
	if (Ship.cond != DOCKED)
	{
		if (damaged(SHUTTLE))
		{
			out(SHUTTLE);
			return;
		}
		printf("Officers escape in shuttlecraft\n");
		/* decide on fate of crew */
		q = &Quad[Ship.quadx][Ship.quady];
		if (q->qsystemname == 0 || damaged(XPORTER))
		{
			printf("Entire crew of %d left to die in outer space\n",
				Ship.crew);
			Game.deaths += Ship.crew;
		}
		else
		{
			printf("Crew beams down to planet %s\n", systemname(q));
		}
	}
	/* see if you can be exchanged */
	if (Now.bases == 0 || Game.captives < 20 * Game.skill)
		lose(L_CAPTURED);
	/* re-outfit new ship */
	printf("You are hereby put in charge of an antiquated but still\n");
	printf("  functional ship, the Fairie Queene.\n");
	Ship.ship = QUEENE;
	Ship.shipname = "Fairie Queene";
	Param.energy = Ship.energy = 3000;
	Param.torped = Ship.torped = 6;
	Param.shield = Ship.shield = 1250;
	Ship.shldup = 0;
	Ship.cloaked = 0;
	Ship.warp = 5.0;
	Ship.warp2 = 25.0;
	Ship.warp3 = 125.0;
	Ship.cond = GREEN;
	/* clear out damages on old ship */
	for (i = 0; i < MAXEVENTS; i++)
	{
		e = &Event[i];
		if (e->evcode != E_FIXDV)
			continue;
		unschedule(e);
	}
	/* get rid of some devices and redistribute probabilities */
	i = Param.damprob[SHUTTLE] + Param.damprob[CLOAK];
	Param.damprob[SHUTTLE] = Param.damprob[CLOAK] = 0;
	while (i > 0)
		for (j = 0; j < NDEV; j++)
		{
			if (Param.damprob[j] != 0)
			{
				Param.damprob[j] += 1;
				i--;
				if (i <= 0)
					break;
			}
		}
	/* pick a starbase to restart at */
	i = ranf(Now.bases);
	Ship.quadx = Now.base[i].x;
	Ship.quady = Now.base[i].y;
	/* setup that quadrant */
	while (1)
	{
		initquad(1);
		Sect[Ship.sectx][Ship.secty] = EMPTY;
		for (i = 0; i < 5; i++)
		{
			Ship.sectx = Etc.starbase.x + ranf(3) - 1;
			if (Ship.sectx < 0 || Ship.sectx >= NSECTS)
				continue;
			Ship.secty = Etc.starbase.y + ranf(3) - 1;
			if (Ship.secty < 0 || Ship.secty >= NSECTS)
				continue;
			if (Sect[Ship.sectx][Ship.secty] == EMPTY)
			{
				Sect[Ship.sectx][Ship.secty] = QUEENE;
				dock(0);
				compkldist(0);
				return;
			}
		}
	}
}
