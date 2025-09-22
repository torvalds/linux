/*	$OpenBSD: attack.c,v 1.8 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: attack.c,v 1.3 1995/04/22 10:58:26 cgd Exp $	*/

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

#include "trek.h"

/*
**  Klingon Attack Routine
**
**	This routine performs the Klingon attack provided that
**	(1) Something happened this move (i.e., not free), and
**	(2) You are not cloaked.  Note that if you issue the
**	cloak command, you are not considered cloaked until you
**	expend some time.
**
**	Klingons are permitted to move both before and after the
**	attack.  They will tend to move toward you before the
**	attack and away from you after the attack.
**
**	Under certain conditions you can get a critical hit.  This
**	sort of hit damages devices.  The probability that a given
**	device is damaged depends on the device.  Well protected
**	devices (such as the computer, which is in the core of the
**	ship and has considerable redundancy) almost never get
**	damaged, whereas devices which are exposed (such as the
**	warp engines) or which are particularly delicate (such as
**	the transporter) have a much higher probability of being
**	damaged.
**
**	The actual amount of damage (i.e., how long it takes to fix
**	it) depends on the amount of the hit and the "damfac[]"
**	entry for the particular device.
**
**	Casualties can also occur.
*/

/* argument is set if attack while resting */
void
attack(int resting)
{
	int	hit, i, l;
	int	maxhit, tothit, shldabsb;
	double	chgfac, propor, extradm;
	double	dustfac, tothe;
	int	cas;
	int	hitflag;

	if (Move.free)
		return;
	if (Etc.nkling <= 0 || Quad[Ship.quadx][Ship.quady].stars < 0)
		return;
	if (Ship.cloaked && Ship.cloakgood)
		return;
	/* move before attack */
	klmove(0);
	if (Ship.cond == DOCKED)
	{
		if (!resting)
			printf("Starbase shields protect the %s\n", Ship.shipname);
		return;
	}
	/* setup shield effectiveness */
	chgfac = 1.0;
	if (Move.shldchg)
		chgfac = 0.25 + 0.50 * franf();
	maxhit = tothit = 0;
	hitflag = 0;

	/* let each Klingon do his damndest */
	for (i = 0; i < Etc.nkling; i++)
	{
		/* if he's low on power he won't attack */
		if (Etc.klingon[i].power < 20)
			continue;
		if (!hitflag)
		{
			printf("\nStardate %.2f: Klingon attack:\n",
				Now.date);
			hitflag++;
		}
		/* complete the hit */
		dustfac = 0.90 + 0.01 * franf();
		tothe = Etc.klingon[i].avgdist;
		hit = Etc.klingon[i].power * pow(dustfac, tothe) * Param.hitfac;
		/* deplete his energy */
		dustfac = Etc.klingon[i].power;
		Etc.klingon[i].power = dustfac * Param.phasfac * (1.0 + (franf() - 0.5) * 0.2);
		/* see how much of hit shields will absorb */
		shldabsb = 0;
		if (Ship.shldup || Move.shldchg)
		{
			propor = Ship.shield;
			propor /= Param.shield;
			shldabsb = propor * chgfac * hit;
			if (shldabsb > Ship.shield)
				shldabsb = Ship.shield;
			Ship.shield -= shldabsb;
		}
		/* actually do the hit */
		printf("\aHIT: %d units", hit);
		if (!damaged(SRSCAN))
			printf(" from %d,%d", Etc.klingon[i].x, Etc.klingon[i].y);
		cas = (shldabsb * 100) / hit;
		hit -= shldabsb;
		if (shldabsb > 0)
			printf(", shields absorb %d%%, effective hit %d\n",
				cas, hit);
		else
			printf("\n");
		tothit += hit;
		if (hit > maxhit)
			maxhit = hit;
		Ship.energy -= hit;
		/* see if damages occurred */
		if (hit >= (15 - Game.skill) * (25 - ranf(12)))
		{
			printf("\aCRITICAL HIT!!!\a\n");
			/* select a device from probability vector */
			cas = ranf(1000);
			for (l = 0; cas >= 0; l++)
				cas -= Param.damprob[l];
			l -= 1;
			/* compute amount of damage */
			extradm = (hit * Param.damfac[l]) / (75 + ranf(25)) + 0.5;
			/* damage the device */
			damage(l, extradm);
			if (damaged(SHIELD))
			{
				if (Ship.shldup)
					printf("Sulu: Shields knocked down, captain.\n");
				Ship.shldup = 0;
				Move.shldchg = 0;
			}
		}
		if (Ship.energy <= 0)
			lose(L_DSTRYD);
	}

	/* see what our casualities are like */
	if (maxhit >= 200 || tothit >= 500)
	{
		cas = tothit * 0.015 * franf();
		if (cas >= 2)
		{
			printf("McCoy: we suffered %d casualties in that attack.\n",
				cas);
			Game.deaths += cas;
			Ship.crew -= cas;
		}
	}

	/* allow Klingons to move after attacking */
	klmove(1);
}
