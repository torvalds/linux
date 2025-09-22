/*	$OpenBSD: help.c,v 1.8 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: help.c,v 1.3 1995/04/22 10:59:01 cgd Exp $	*/

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

#include <math.h>
#include <stdio.h>
#include <unistd.h>

#include "trek.h"

/*
**  call starbase for help
**
**	First, the closest starbase is selected.  If there is a
**	a starbase in your own quadrant, you are in good shape.
**	This distance takes quadrant distances into account only.
**
**	A magic number is computed based on the distance which acts
**	as the probability that you will be rematerialized.  You
**	get three tries.
**
**	When it is determined that you should be able to be remater-
**	ialized (i.e., when the probability thing mentioned above
**	comes up positive), you are put into that quadrant (anywhere).
**	Then, we try to see if there is a spot adjacent to the star-
**	base.  If not, you can't be rematerialized!!!  Otherwise,
**	it drops you there.  It only tries five times to find a spot
**	to drop you.  After that, it's your problem.
*/

const char	*const Cntvect[3] =
{"first", "second", "third"};

void
help(int v)
{
	double		dist, x;
	int		dx = 0, dy = 0;
	int		i, j, l = 0;

	/* check to see if calling for help is reasonable ... */
	if (Ship.cond == DOCKED)
	{
		printf("Uhura: But Captain, we're already docked\n");
		return;
	}

	/* or possible */
	if (damaged(SSRADIO))
	{
		out(SSRADIO);
		return;
	}
	if (Now.bases <= 0)
	{
		printf("Uhura: I'm not getting any response from starbase\n");
		return;
	}

	/* tut tut, there goes the score */
	Game.helps += 1;

	/* find the closest base */
	dist = 1e50;
	if (Quad[Ship.quadx][Ship.quady].bases <= 0)
	{
		/* there isn't one in this quadrant */
		for (i = 0; i < Now.bases; i++)
		{
			/* compute distance */
			dx = Now.base[i].x - Ship.quadx;
			dy = Now.base[i].y - Ship.quady;
			x = dx * dx + dy * dy;
			x = sqrt(x);

			/* see if better than what we already have */
			if (x < dist)
			{
				dist = x;
				l = i;
			}
		}

		/* go to that quadrant */
		Ship.quadx = Now.base[l].x;
		Ship.quady = Now.base[l].y;
		initquad(1);
	}
	else
	{
		dist = 0.0;
	}

	/* dematerialize the Enterprise */
	Sect[Ship.sectx][Ship.secty] = EMPTY;
	printf("Starbase in %d,%d responds\n", Ship.quadx, Ship.quady);

	/* this next thing acts as a probability that it will work */
	x = pow(1.0 - pow(0.94, dist), 0.3333333);

	/* attempt to rematerialize */
	for (i = 0; i < 3; i++)
	{
		sleep(2);
		printf("%s attempt to rematerialize ", Cntvect[i]);
		if (franf() > x)
		{
			/* ok, that's good.  let's see if we can set her down */
			for (j = 0; j < 5; j++)
			{
				dx = Etc.starbase.x + ranf(3) - 1;
				if (dx < 0 || dx >= NSECTS)
					continue;
				dy = Etc.starbase.y + ranf(3) - 1;
				if (dy < 0 || dy >= NSECTS || Sect[dx][dy] != EMPTY)
					continue;
				break;
			}
			if (j < 5)
			{
				/* found an empty spot */
				printf("succeeds\n");
				Ship.sectx = dx;
				Ship.secty = dy;
				Sect[dx][dy] = Ship.ship;
				dock(0);
				compkldist(0);
				return;
			}
			/* the starbase must have been surrounded */
		}
		printf("fails\n");
	}

	/* one, two, three strikes, you're out */
	lose(L_NOHELP);
}
