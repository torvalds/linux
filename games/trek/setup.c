/*	$OpenBSD: setup.c,v 1.13 2017/05/26 19:19:23 tedu Exp $	*/
/*	$NetBSD: setup.c,v 1.4 1995/04/24 12:26:06 cgd Exp $	*/

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

#include <err.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "getpar.h"
#include "trek.h"

/*
**  INITIALIZE THE GAME
**
**	The length, skill, and password are read, and the game
**	is initialized.  It is far too difficult to describe all
**	that goes on in here, but it is all straight-line code;
**	give it a look.
**
**	Tournament games are handled here.
*/

const struct cvntab	Lentab[] =
{
	{ "s",		"hort",		(cmdfun)1,	0 },
	{ "m",		"edium",	(cmdfun)2,	0 },
	{ "l",		"ong",		(cmdfun)4,	0 },
	{ NULL,		NULL,		NULL,		0 }
};

const struct cvntab	Skitab[] =
{
	{ "n",		"ovice",	(cmdfun)1,	0 },
	{ "f",		"air",		(cmdfun)2,	0 },
	{ "g",		"ood",		(cmdfun)3,	0 },
	{ "e",		"xpert",	(cmdfun)4,	0 },
	{ "c",		"ommodore",	(cmdfun)5,	0 },
	{ "i",		"mpossible",	(cmdfun)6,	0 },
	{ NULL,		NULL,		NULL,		0 }
};

void
setup(void)
{
	const struct cvntab	*r;
	int			i, j;
	double			f;
	int			d;
	int			klump;
	int			ix, iy;
	struct quad		*q;
	struct event		*e;

	r = getcodpar("What length game", Lentab);
	Game.length = (long) r->value;
	r = getcodpar("What skill game", Skitab);
	Game.skill = (long) r->value;
	Game.tourn = 0;
	getstrpar("Enter a password", Game.passwd, 14, 0);
	if (strcmp(Game.passwd, "tournament") == 0)
	{
		getstrpar("Enter tournament code", Game.passwd, 14, 0);
		Game.tourn = 1;
		d = 0;
		for (i = 0; Game.passwd[i]; i++)
			d += Game.passwd[i] << i;
		srandom_deterministic(d);
	}
	Param.bases = Now.bases = ranf(6 - Game.skill) + 2;
	if (Game.skill == 6)
		Param.bases = Now.bases = 1;
	Param.time = Now.time = 6.0 * Game.length + 2.0;
	i = Game.skill;
	j = Game.length;
	Param.klings = Now.klings = i * j * 3.5 * (franf() + 0.75);
	if (Param.klings < i * j * 5)
		Param.klings = Now.klings = i * j * 5;
	if (Param.klings <= i)		/* numerical overflow problems */
		Param.klings = Now.klings = 127;
	Param.energy = Ship.energy = 5000;
	Param.torped = Ship.torped = 10;
	Ship.ship = ENTERPRISE;
	Ship.shipname = "Enterprise";
	Param.shield = Ship.shield = 1500;
	Param.resource = Now.resource = Param.klings * Param.time;
	Param.reserves = Ship.reserves = (6 - Game.skill) * 2.0;
	Param.crew = Ship.crew = 387;
	Param.brigfree = Ship.brigfree = 400;
	Ship.shldup = 1;
	Ship.cond = GREEN;
	Ship.warp = 5.0;
	Ship.warp2 = 25.0;
	Ship.warp3 = 125.0;
	Ship.sinsbad = 0;
	Ship.cloaked = 0;
	Param.date = Now.date = (ranf(20) + 20) * 100;
	f = Game.skill;
	f = log(f + 0.5);
	for (i = 0; i < NDEV; i++)
		if (Device[i].name[0] == '*')
			Param.damfac[i] = 0;
		else
			Param.damfac[i] = f;
	/* these probabilities must sum to 1000 */
	Param.damprob[WARP] = 70;	/* warp drive		 7.0% */
	Param.damprob[SRSCAN] = 110;	/* short range scanners	11.0% */
	Param.damprob[LRSCAN] = 110;	/* long range scanners	11.0% */
	Param.damprob[PHASER] = 125;	/* phasers		12.5% */
	Param.damprob[TORPED] = 125;	/* photon torpedoes	12.5% */
	Param.damprob[IMPULSE] = 75;	/* impulse engines	 7.5% */
	Param.damprob[SHIELD] = 150;	/* shield control	15.0% */
	Param.damprob[COMPUTER] = 20;	/* computer		 2.0% */
	Param.damprob[SSRADIO] = 35;	/* subspace radio	 3.5% */
	Param.damprob[LIFESUP] = 30;	/* life support		 3.0% */
	Param.damprob[SINS] = 20;	/* navigation system	 2.0% */
	Param.damprob[CLOAK] = 50;	/* cloaking device	 5.0% */
	Param.damprob[XPORTER] = 80;	/* transporter		 8.0% */
	/* check to see that I didn't blow it */
	for (i = j = 0; i < NDEV; i++)
		j += Param.damprob[i];
	if (j != 1000)
		errx(1, "Device probabilities sum to %d", j);
	Param.dockfac = 0.5;
	Param.regenfac = (5 - Game.skill) * 0.05;
	if (Param.regenfac < 0.0)
		Param.regenfac = 0.0;
	Param.warptime = 10;
	Param.stopengy = 50;
	Param.shupengy = 40;
	i = Game.skill;
	Param.klingpwr = 100 + 150 * i;
	if (i >= 6)
		Param.klingpwr += 150;
	Param.phasfac = 0.8;
	Param.hitfac = 0.5;
	Param.klingcrew = 200;
	Param.srndrprob = 0.0035;
	Param.moveprob[KM_OB] = 45;
	Param.movefac[KM_OB] = .09;
	Param.moveprob[KM_OA] = 40;
	Param.movefac[KM_OA] = -0.05;
	Param.moveprob[KM_EB] = 40;
	Param.movefac[KM_EB] = 0.075;
	Param.moveprob[KM_EA] = 25 + 5 * Game.skill;
	Param.movefac[KM_EA] = -0.06 * Game.skill;
	Param.moveprob[KM_LB] = 0;
	Param.movefac[KM_LB] = 0.0;
	Param.moveprob[KM_LA] = 10 + 10 * Game.skill;
	Param.movefac[KM_LA] = 0.25;
	Param.eventdly[E_SNOVA] = 0.5;
	Param.eventdly[E_LRTB] = 25.0;
	Param.eventdly[E_KATSB] = 1.0;
	Param.eventdly[E_KDESB] = 3.0;
	Param.eventdly[E_ISSUE] = 1.0;
	Param.eventdly[E_SNAP] = 0.5;
	Param.eventdly[E_ENSLV] = 0.5;
	Param.eventdly[E_REPRO] = 2.0;
	Param.navigcrud[0] = 1.50;
	Param.navigcrud[1] = 0.75;
	Param.cloakenergy = 1000;
	Param.energylow = 1000;
	for (i = 0; i < MAXEVENTS; i++)
	{
		e = &Event[i];
		e->date = 1e50;
		e->evcode = 0;
	}
	xsched(E_SNOVA, 1, 0, 0, 0);
	xsched(E_LRTB, Param.klings, 0, 0, 0);
	xsched(E_KATSB, 1, 0, 0, 0);
	xsched(E_ISSUE, 1, 0, 0, 0);
	xsched(E_SNAP, 1, 0, 0, 0);
	Ship.sectx = ranf(NSECTS);
	Ship.secty = ranf(NSECTS);
	Game.killk = Game.kills = Game.killb = 0;
	Game.deaths = Game.negenbar = 0;
	Game.captives = 0;
	Game.killinhab = 0;
	Game.helps = 0;
	Game.killed = 0;
	Game.snap = 0;
	Move.endgame = 0;

	/* setup stars */
	for (i = 0; i < NQUADS; i++)
		for (j = 0; j < NQUADS; j++)
		{
			q = &Quad[i][j];
			q->klings = q->bases = 0;
			q->scanned = -1;
			q->stars = ranf(9) + 1;
			q->holes = ranf(3) - q->stars / 5;
			if (q->holes < 0)
				q->holes = 0;
			q->qsystemname = 0;
		}

	/* select inhabited starsystems */
	for (d = 1; d < NINHAB; d++)
	{
		do
		{
			i = ranf(NQUADS);
			j = ranf(NQUADS);
			q = &Quad[i][j];
		} while (q->qsystemname);
		q->qsystemname = d;
	}

	/* position starbases */
	for (i = 0; i < Param.bases; i++)
	{
		while (1)
		{
			ix = ranf(NQUADS);
			iy = ranf(NQUADS);
			q = &Quad[ix][iy];
			if (q->bases > 0)
				continue;
			break;
		}
		q->bases = 1;
		Now.base[i].x = ix;
		Now.base[i].y = iy;
		q->scanned = 1001;
		/* start the Enterprise near starbase */
		if (i == 0)
		{
			Ship.quadx = ix;
			Ship.quady = iy;
		}
	}

	/* position klingons */
	for (i = Param.klings; i > 0; )
	{
		klump = ranf(4) + 1;
		if (klump > i)
			klump = i;
		while (1)
		{
			ix = ranf(NQUADS);
			iy = ranf(NQUADS);
			q = &Quad[ix][iy];
			if (q->klings + klump > MAXKLQUAD)
				continue;
			q->klings += klump;
			i -= klump;
			break;
		}
	}

	/* initialize this quadrant */
	printf("%d Klingons\n%d starbase", Param.klings, Param.bases);
	if (Param.bases > 1)
		printf("s");
	printf(" at %d,%d", Now.base[0].x, Now.base[0].y);
	for (i = 1; i < Param.bases; i++)
		printf(", %d,%d", Now.base[i].x, Now.base[i].y);
	printf("\nIt takes %d units to kill a Klingon\n", Param.klingpwr);
	Move.free = 0;
	initquad(0);
	srscan(1);
	attack(0);
}
