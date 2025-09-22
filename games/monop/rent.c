/*	$OpenBSD: rent.c,v 1.7 2016/01/08 18:20:33 mestre Exp $	*/
/*	$NetBSD: rent.c,v 1.3 1995/03/23 08:35:11 cgd Exp $	*/

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

#include "monop.ext"

/*
 *	This routine has the player pay rent
 */
void
rent(SQUARE *sqp)
{
	int	rnt;
	PROP	*pp;
	PLAY	*plp;

	plp = &play[(int)sqp->owner];
	printf("Owned by %s\n", plp->name);
	if (sqp->desc->morg) {
		lucky("The thing is mortgaged.  ");
		return;
	}
	switch (sqp->type) {
	case PRPTY:
		pp = sqp->desc;
		if (pp->monop) {
			if (pp->houses == 0)
				printf("rent is %d\n", rnt = pp->rent[0] * 2);
			else if (pp->houses < 5)
				printf("with %d house%s, rent is %d\n",
				    pp->houses, pp->houses == 1 ? "" : "s",
				    rnt = pp->rent[(int)pp->houses]);
			else
				printf("with a hotel, rent is %d\n",
				    rnt = pp->rent[(int)pp->houses]);
		} else
			printf("rent is %d\n", rnt = pp->rent[0]);
		break;
	case RR:
		rnt = 25;
		rnt <<= (plp->num_rr - 1);
		if (spec)
			rnt <<= 1;
		printf("rent is %d\n", rnt);
		break;
	case UTIL:
		rnt = roll(2, 6);
		if (plp->num_util == 2 || spec) {
			printf("rent is 10 * roll (%d) = %d\n", rnt, rnt * 10);
			rnt *= 10;
		}
		else {
			printf("rent is 4 * roll (%d) = %d\n", rnt, rnt * 4);
			rnt *= 4;
		}
		break;
	default:	/* Should never be reached */
		rnt = 0;
		printf("Warning:  rent() property %d\n", sqp->type);
		break;
	}
	cur_p->money -= rnt;
	plp->money += rnt;
}
