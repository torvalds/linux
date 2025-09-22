/*	$OpenBSD: capture.c,v 1.7 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: capture.c,v 1.3 1995/04/22 10:58:32 cgd Exp $	*/

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
**  Ask a Klingon To Surrender
**
**	(Fat chance)
**
**	The Subspace Radio is needed to ask a Klingon if he will kindly
**	surrender.  A random Klingon from the ones in the quadrant is
**	chosen.
**
**	The Klingon is requested to surrender.  The probability of this
**	is a function of that Klingon's remaining power, our power,
**	etc.
*/
void
capture(int v)
{
	int		i;
	struct kling	*k;
	double		x;

	/* check for not cloaked */
	if (Ship.cloaked)
	{
		printf("Ship-ship communications out when cloaked\n");
		return;
	}
	if (damaged(SSRADIO))
	{
		out(SSRADIO);
		return;
	}
	/* find out if there are any at all */
	if (Etc.nkling <= 0)
	{
		printf("Uhura: Getting no response, sir\n");
		return;
	}

	/* if there is more than one Klingon, find out which one */
	k = selectklingon();
	Move.free = 0;
	Move.time = 0.05;

	/* check out that Klingon */
	k->srndreq++;
	x = Param.klingpwr;
	x *= Ship.energy;
	x /= k->power * Etc.nkling;
	x *= Param.srndrprob;
	i = x;
#	ifdef xTRACE
	if (Trace)
		printf("Prob = %d (%.4f)\n", i, x);
#	endif
	if (i > ranf(100))
	{
		/* guess what, he surrendered!!! */
		printf("Klingon at %d,%d surrenders\n", k->x, k->y);
		i = ranf(Param.klingcrew);
		if ( i > 0 )
			printf("%d klingons commit suicide rather than be taken captive\n", Param.klingcrew - i);
		if (i > Ship.brigfree)
			i = Ship.brigfree;
		Ship.brigfree -= i;
		printf("%d captives taken\n", i);
		killk(k->x, k->y);
		return;
	}

	/* big surprise, he refuses to surrender */
	printf("Fat chance, captain\n");
	return;
}


/*
**  SELECT A KLINGON
**
**	Cruddy, just takes one at random.  Should ask the captain.
*/

struct kling *
selectklingon(void)
{
	int		i;

	if (Etc.nkling < 2)
		i = 0;
	else
		i = ranf(Etc.nkling);
	return (&Etc.klingon[i]);
}
