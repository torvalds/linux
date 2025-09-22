/*	$OpenBSD: rest.c,v 1.7 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: rest.c,v 1.3 1995/04/22 10:59:22 cgd Exp $	*/

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
**  REST FOR REPAIRS
**
**	You sit around and wait for repairs to happen.  Actually, you
**	sit around and wait for anything to happen.  I do want to point
**	out however, that Klingons are not as patient as you are, and
**	they tend to attack you while you are resting.
**
**	You can never rest through a long range tractor beam.
**
**	In events() you will be given an opportunity to cancel the
**	rest period if anything momentous happens.
*/

void
rest(int v)
{
	double			t;
	int		percent;

	/* get the time to rest */
	t = getfltpar("How long");
	if (t <= 0.0)
		return;
	percent = 100 * t / Now.time + 0.5;
	if (percent >= 70)
	{
		printf("Spock: That would take %d%% of our remaining time.\n",
			percent);
		if (!getynpar("Are you really certain that is wise"))
			return;
	}
	Move.time = t;

	/* boundary condition is the LRTB */
	t = Now.eventptr[E_LRTB]->date - Now.date;
	if (Ship.cond != DOCKED && Move.time > t)
		Move.time = t + 0.0001;
	Move.free = 0;
	Move.resting = 1;
}
