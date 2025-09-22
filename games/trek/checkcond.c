/*	$OpenBSD: checkcond.c,v 1.5 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: checkcond.c,v 1.3 1995/04/22 10:58:37 cgd Exp $	*/

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

#include "trek.h"

/*
**  Check for Condition After a Move
**
**	Various ship conditions are checked.  First we check
**	to see if we have already lost the game, due to running
**	out of life support reserves, running out of energy,
**	or running out of crew members.  The check for running
**	out of time is in events().
**
**	If we are in automatic override mode (Etc.nkling < 0), we
**	don't want to do anything else, lest we call autover
**	recursively.
**
**	In the normal case, if there is a supernova, we call
**	autover() to help us escape.  If after calling autover()
**	we are still in the grips of a supernova, we get burnt
**	up.
**
**	If there are no Klingons in this quadrant, we nullify any
**	distress calls which might exist.
**
**	We then set the condition code, based on the energy level
**	and battle conditions.
*/

void
checkcond(void)
{
	/* see if we are still alive and well */
	if (Ship.reserves < 0.0)
		lose(L_NOLIFE);
	if (Ship.energy <= 0)
		lose(L_NOENGY);
	if (Ship.crew <= 0)
		lose(L_NOCREW);
	/* if in auto override mode, ignore the rest */
	if (Etc.nkling < 0)
		return;
	/* call in automatic override if appropriate */
	if (Quad[Ship.quadx][Ship.quady].stars < 0)
		autover();
	if (Quad[Ship.quadx][Ship.quady].stars < 0)
		lose(L_SNOVA);
	/* nullify distress call if appropriate */
	if (Etc.nkling <= 0)
		killd(Ship.quadx, Ship.quady, 1);

	/* set condition code */
	if (Ship.cond == DOCKED)
		return;

	if (Etc.nkling > 0)
	{
		Ship.cond = RED;
		return;
	}
	if (Ship.energy < Param.energylow)
	{
		Ship.cond = YELLOW;
		return;
	}
	Ship.cond = GREEN;
}
