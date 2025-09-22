/*	$OpenBSD: initquad.c,v 1.7 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: initquad.c,v 1.3 1995/04/22 10:59:04 cgd Exp $	*/

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
**  Paramize Quadrant Upon Entering
**
**	A quadrant is initialized from the information held in the
**	Quad matrix.  Basically, everything is just initialized
**	randomly, except for the starship, which goes into a fixed
**	sector.
**
**	If there are Klingons in the quadrant, the captain is informed
**	that the condition is RED, and he is given a chance to put
**	his shields up if the computer is working.
**
**	The flag `f' is set to disable the check for condition red.
**	This mode is used in situations where you know you are going
**	to be docked, i.e., abandon() and help().
*/

void
initquad(int f)
{
	int		i, j;
	int		rx, ry;
	int		nbases, nstars;
	struct quad	*q;
	int		nholes;

	q = &Quad[Ship.quadx][Ship.quady];

	/* ignored supernova'ed quadrants (this is checked again later anyway */
	if (q->stars < 0)
		return;
	Etc.nkling = q->klings;
	nbases = q->bases;
	nstars = q->stars;
	nholes = q->holes;

	/* have we blundered into a battle zone w/ shields down? */
	if (Etc.nkling > 0 && !f)
	{
		printf("Condition RED\n");
		Ship.cond = RED;
		if (!damaged(COMPUTER))
			shield(1);
	}

	/* clear out the quadrant */
	for (i = 0; i < NSECTS; i++)
		for (j = 0; j < NSECTS; j++)
			Sect[i][j] = EMPTY;

	/* initialize Enterprise */
	Sect[Ship.sectx][Ship.secty] = Ship.ship;

	/* initialize Klingons */
	for (i = 0; i < Etc.nkling; i++)
	{
		sector(&rx, &ry);
		Sect[rx][ry] = KLINGON;
		Etc.klingon[i].x = rx;
		Etc.klingon[i].y = ry;
		Etc.klingon[i].power = Param.klingpwr;
		Etc.klingon[i].srndreq = 0;
	}
	compkldist(1);

	/* initialize star base */
	if (nbases > 0)
	{
		sector(&rx, &ry);
		Sect[rx][ry] = BASE;
		Etc.starbase.x = rx;
		Etc.starbase.y = ry;
	}

	/* initialize inhabited starsystem */
	if (q->qsystemname != 0)
	{
		sector(&rx, &ry);
		Sect[rx][ry] = INHABIT;
		nstars -= 1;
	}

	/* initialize black holes */
	for (i = 0; i < nholes; i++)
	{
		sector(&rx, &ry);
		Sect[rx][ry] = HOLE;
	}

	/* initialize stars */
	for (i = 0; i < nstars; i++)
	{
		sector(&rx, &ry);
		Sect[rx][ry] = STAR;
	}
	Move.newquad = 1;
}

void
sector(int *x, int *y)
{
	int		i, j;

	do
	{
		i = ranf(NSECTS);
		j = ranf(NSECTS);
	} while (Sect[i][j] != EMPTY);
	*x = i;
	*y = j;
}
