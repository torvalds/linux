/*	$OpenBSD: score.c,v 1.7 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: score.c,v 1.3 1995/04/22 10:59:24 cgd Exp $	*/

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

#include "getpar.h"
#include "trek.h"

/*
**  PRINT OUT THE CURRENT SCORE
*/

long
score(void)
{
	int	u, t;
	long		s;
	double		r;

	printf("\n*** Your score:\n");
	s = t = Param.klingpwr / 4 * (u = Game.killk);
	if (t != 0)
		printf("%d Klingons killed\t\t\t%6d\n", u, t);
	r = Now.date - Param.date;
	if (r < 1.0)
		r = 1.0;
	r = Game.killk / r;
	s += (t = 400 * r);
	if (t != 0)
		printf("Kill rate %.2f Klingons/stardate  \t%6d\n", r, t);
	r = Now.klings;
	r /= Game.killk + 1;
	s += (t = -400 * r);
	if (t != 0)
		printf("Penalty for %d klingons remaining\t%6d\n", Now.klings, t);
	if (Move.endgame > 0)
	{
		s += (t = 100 * (u = Game.skill));
		printf("Bonus for winning a %s%s game\t\t%6d\n", Skitab[u - 1].abrev, Skitab[u - 1].full, t);
	}
	if (Game.killed)
	{
		s -= 500;
		printf("Penalty for getting killed\t\t  -500\n");
	}
	s += (t = -100 * (u = Game.killb));
	if (t != 0)
		printf("%d starbases killed\t\t\t%6d\n", u, t);
	s += (t = -100 * (u = Game.helps));
	if (t != 0)
		printf("%d calls for help\t\t\t%6d\n", u, t);
	s += (t = -5 * (u = Game.kills));
	if (t != 0)
		printf("%d stars destroyed\t\t\t%6d\n", u, t);
	s += (t = -150 * (u = Game.killinhab));
	if (t != 0)
		printf("%d inhabited starsystems destroyed\t%6d\n", u, t);
	if (Ship.ship != ENTERPRISE)
	{
		s -= 200;
		printf("penalty for abandoning ship\t\t  -200\n");
	}
	s += (t = 3 * (u = Game.captives));
	if (t != 0)
		printf("%d Klingons captured\t\t\t%6d\n", u, t);
	s += (t = -(u = Game.deaths));
	if (t != 0)
		printf("%d casualties\t\t\t\t%6d\n", u, t);
	printf("\n***  TOTAL\t\t\t%14ld\n", s);
	return (s);
}
