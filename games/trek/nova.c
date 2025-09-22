/*	$OpenBSD: nova.c,v 1.7 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: nova.c,v 1.3 1995/04/22 10:59:14 cgd Exp $	*/

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
**  CAUSE A NOVA TO OCCUR
**
**	A nova occurs.  It is the result of having a star hit with
**	a photon torpedo.  There are several things which may happen.
**	The star may not be affected.  It may go nova.  It may turn
**	into a black hole.  Any (yummy) it may go supernova.
**
**	Stars that go nova cause stars which surround them to undergo
**	the same probabilistic process.  Klingons next to them are
**	destroyed.  And if the starship is next to it, it gets zapped.
**	If the zap is too much, it gets destroyed.
*/

void
nova(int x, int y)
{
	int		i, j;
	int		se;

	if (Sect[x][y] != STAR || Quad[Ship.quadx][Ship.quady].stars < 0)
		return;
	if (ranf(100) < 15)
	{
		printf("Spock: Star at %d,%d failed to nova.\n", x, y);
		return;
	}
	if (ranf(100) < 5)
	{
		snova(x, y);
		return;
	}
	printf("Spock: Star at %d,%d gone nova\n", x, y);

	if (ranf(4) != 0)
		Sect[x][y] = EMPTY;
	else
	{
		Sect[x][y] = HOLE;
		Quad[Ship.quadx][Ship.quady].holes += 1;
	}
	Quad[Ship.quadx][Ship.quady].stars -= 1;
	Game.kills += 1;
	for (i = x - 1; i <= x + 1; i++)
	{
		if (i < 0 || i >= NSECTS)
			continue;
		for (j = y - 1; j <= y + 1; j++)
		{
			if (j < 0 || j >= NSECTS)
				continue;
			se = Sect[i][j];
			switch (se)
			{

			  case EMPTY:
			  case HOLE:
				break;

			  case KLINGON:
				killk(i, j);
				break;

			  case STAR:
				nova(i, j);
				break;

			  case INHABIT:
				kills(i, j, -1);
				break;

			  case BASE:
				killb(i, j);
				Game.killb += 1;
				break;

			  case ENTERPRISE:
			  case QUEENE:
				se = 2000;
				if (Ship.shldup)
				{
					if (Ship.shield >= se)
					{
						Ship.shield -= se;
						se = 0;
					}
					else
					{
						se -= Ship.shield;
						Ship.shield = 0;
					}
				}
				Ship.energy -= se;
				if (Ship.energy <= 0)
					lose(L_SUICID);
				break;

			  default:
				printf("Unknown object %c at %d,%d destroyed\n",
					se, i, j);
				Sect[i][j] = EMPTY;
				break;
			}
		}
	}
}
