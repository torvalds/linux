/*	$OpenBSD: compkl.c,v 1.8 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: compkl.c,v 1.3 1995/04/22 10:58:38 cgd Exp $	*/

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

#include "trek.h"

/*
**  compute klingon distances
**
**	The klingon list has the distances for all klingons recomputed
**	and sorted.  The parameter is a Boolean flag which is set if
**	we have just entered a new quadrant.
**
**	This routine is used every time the Enterprise or the Klingons
**	move.
*/

static void sortkl(void);

/* argument is set if new quadrant */
void
compkldist(int f)
{
	int		i, dx, dy;
	double		d;
	double		temp;

	if (Etc.nkling == 0)
		return;
	for (i = 0; i < Etc.nkling; i++)
	{
		/* compute distance to the Klingon */
		dx = Ship.sectx - Etc.klingon[i].x;
		dy = Ship.secty - Etc.klingon[i].y;
		d = dx * dx + dy * dy;
		d = sqrt(d);

		/* compute average of new and old distances to Klingon */
		if (!f)
		{
			temp = Etc.klingon[i].dist;
			Etc.klingon[i].avgdist = 0.5 * (temp + d);
		}
		else
		{
			/* new quadrant: average is current */
			Etc.klingon[i].avgdist = d;
		}
		Etc.klingon[i].dist = d;
	}

	/* leave them sorted */
	sortkl();
}


/*
**  sort klingons
**
**	bubble sort on ascending distance
*/

static void
sortkl(void)
{
	struct kling	t;
	int		f, i, m;

	m = Etc.nkling - 1;
	f = 1;
	while (f)
	{
		f = 0;
		for (i = 0; i < m; i++)
			if (Etc.klingon[i].dist > Etc.klingon[i+1].dist)
			{
				t = Etc.klingon[i];
				Etc.klingon[i] = Etc.klingon[i + 1];
				Etc.klingon[i + 1] = t;
				f = 1;
			}
	}
}
