/*	$OpenBSD: klmove.c,v 1.7 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: klmove.c,v 1.3 1995/04/22 10:59:07 cgd Exp $	*/

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
**  Move Klingons Around
**
**	This is a largely incomprehensible block of code that moves
**	Klingons around in a quadrant.  It was written in a very
**	"program as you go" fashion, and is a prime candidate for
**	rewriting.
**
**	The flag `fl' is zero before an attack, one after an attack,
**	and two if you are leaving a quadrant.  This serves to
**	change the probability and distance that it moves.
**
**	Basically, what it will try to do is to move a certain number
**	of steps either toward you or away from you.  It will avoid
**	stars whenever possible.  Nextx and nexty are the next
**	sector to move to on a per-Klingon basis; they are roughly
**	equivalent to Ship.sectx and Ship.secty for the starship.  Lookx and
**	looky are the sector that you are going to look at to see
**	if you can move their.  Dx and dy are the increment.  Fudgex
**	and fudgey are the things you change around to change your
**	course around stars.
*/

void
klmove(int fl)
{
	int		n;
	struct kling	*k;
	double		dx, dy;
	int		nextx, nexty;
	int		lookx, looky;
	int		motion;
	int		fudgex, fudgey;
	int		qx, qy;
	double		bigger;
	int		i;

#	ifdef xTRACE
	if (Trace)
		printf("klmove: fl = %d, Etc.nkling = %d\n", fl, Etc.nkling);
#	endif
	for (n = 0; n < Etc.nkling; n++)
	{
		k = &Etc.klingon[n];
		i = 100;
		if (fl)
			i = 100.0 * k->power / Param.klingpwr;
		if (ranf(i) >= Param.moveprob[2 * Move.newquad + fl])
			continue;
		/* compute distance to move */
		motion = ranf(75) - 25;
		motion *= k->avgdist * Param.movefac[2 * Move.newquad + fl];
		/* compute direction */
		dx = Ship.sectx - k->x + ranf(3) - 1;
		dy = Ship.secty - k->y + ranf(3) - 1;
		bigger = dx;
		if (dy > bigger)
			bigger = dy;
		if (bigger == 0.0)
			bigger = 1.0;
		dx = dx / bigger + 0.5;
		dy = dy / bigger + 0.5;
		if (motion < 0)
		{
			motion = -motion;
			dx = -dx;
			dy = -dy;
		}
		fudgex = fudgey = 1;
		/* try to move the klingon */
		nextx = k->x;
		nexty = k->y;
		for (; motion > 0; motion--)
		{
			lookx = nextx + dx;
			looky = nexty + dy;
			if (lookx < 0 || lookx >= NSECTS || looky < 0 || looky >= NSECTS)
			{
				/* new quadrant */
				qx = Ship.quadx;
				qy = Ship.quady;
				if (lookx < 0)
					qx -= 1;
				else
					if (lookx >= NSECTS)
						qx += 1;
				if (looky < 0)
					qy -= 1;
				else
					if (looky >= NSECTS)
						qy += 1;
				if (qx < 0 || qx >= NQUADS || qy < 0 || qy >= NQUADS ||
						Quad[qx][qy].stars < 0 || Quad[qx][qy].klings > MAXKLQUAD - 1)
					break;
				if (!damaged(SRSCAN))
				{
					printf("Klingon at %d,%d escapes to quadrant %d,%d\n",
						k->x, k->y, qx, qy);
					motion = Quad[qx][qy].scanned;
					if (motion >= 0 && motion < 1000)
						Quad[qx][qy].scanned += 100;
					motion = Quad[Ship.quadx][Ship.quady].scanned;
					if (motion >= 0 && motion < 1000)
						Quad[Ship.quadx][Ship.quady].scanned -= 100;
				}
				Sect[k->x][k->y] = EMPTY;
				Quad[qx][qy].klings += 1;
				Etc.nkling -= 1;
				*k = Etc.klingon[Etc.nkling];
				Quad[Ship.quadx][Ship.quady].klings -= 1;
				k = 0;
				break;
			}
			if (Sect[lookx][looky] != EMPTY)
			{
				lookx = nextx + fudgex;
				if (lookx < 0 || lookx >= NSECTS)
					lookx = nextx + dx;
				if (Sect[lookx][looky] != EMPTY)
				{
					fudgex = -fudgex;
					looky = nexty + fudgey;
					if (looky < 0 || looky >= NSECTS || Sect[lookx][looky] != EMPTY)
					{
						fudgey = -fudgey;
						break;
					}
				}
			}
			nextx = lookx;
			nexty = looky;
		}
		if (k && (k->x != nextx || k->y != nexty))
		{
			if (!damaged(SRSCAN))
				printf("Klingon at %d,%d moves to %d,%d\n",
					k->x, k->y, nextx, nexty);
			Sect[k->x][k->y] = EMPTY;
			Sect[k->x = nextx][k->y = nexty] = KLINGON;
		}
	}
	compkldist(0);
}
