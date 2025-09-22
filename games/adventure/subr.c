/*	$OpenBSD: subr.c,v 1.11 2016/03/08 10:48:39 mestre Exp $	*/
/*	$NetBSD: subr.c,v 1.2 1995/03/21 12:05:11 cgd Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * The game adventure was originally written in Fortran by Will Crowther
 * and Don Woods.  It was later translated to C and enhanced by Jim
 * Gillogly.  This code is derived from software contributed to Berkeley
 * by Jim Gillogly at The Rand Corporation.
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

/*	Re-coding of advent in C: subroutines from main			*/

#include <stdio.h>
#include <stdlib.h>

#include "extern.h"
#include "hdr.h"

/*		Statement functions	*/
int
toting(int objj)
{
	if (place[objj] == -1)
		return (TRUE);
	return (FALSE);
}

int
here(int objj)
{
	if (place[objj] == loc || toting(objj))
		return (TRUE);
	return (FALSE);
}

int
at(int objj)
{
	if (place[objj] == loc || fixed[objj] == loc)
		return (TRUE);
	else
		return (FALSE);
}

int
liq2(int pbotl)
{
	return ((1 - pbotl) * water + (pbotl / 2) * (water + oil));
}

int
liq(void)
{
	int     i;

	i = prop[bottle];
	if (i > -1 - i)
		return (liq2(i));
	return (liq2(-1 - i));
}

int
liqloc(int locc)	/* may want to clean this one up a bit */
{
	int     i, j, l;

	i = cond[locc] / 2;
	j = ((i * 2) % 8) - 5;
	l = cond[locc] / 4;
	l = l % 2;
	return (liq2(j * l + 1));
}

int
bitset(int l, int n)
{
	if (cond[l] & setbit[n])
		return (TRUE);
	return (FALSE);
}

int
forced(int locc)
{
	if (cond[locc] == 2)
		return (TRUE);
	return (FALSE);
}

int
dark(void)
{
	if ((cond[loc] % 2) == 0 && (prop[lamp] == 0 || !here(lamp)))
		return (TRUE);
	return (FALSE);
}

int
pct(int n)
{
	if (ran(100) < n)
		return (TRUE);
	return (FALSE);
}


int
fdwarf(void)	/* 71 */
{
	int     i, j;
	struct travlist *kk;

	if (newloc != loc && !forced(loc) && !bitset(loc, 3)) {
		for (i = 1; i <= 5; i++) {
			if (odloc[i] != newloc || !dseen[i])
				continue;
			newloc = loc;
			rspeak(2);
			break;
		}
	}
	loc = newloc;			/* 74 */
	if (loc == 0 || forced(loc) || bitset(newloc, 3))
		return (2000);
	if (dflag == 0) {
		if (loc >= 15)
			dflag = 1;
		return (2000);
	}
	if (dflag == 1)	{	/* 6000 */
		if (loc < 15 || pct(95))
			return (2000);
		dflag = 2;
		for (i = 1; i <= 2; i++) {
			j = 1 + ran(5);
			if (pct(50) && saved == -1)
				dloc[j] = 0;	/* 6001 */
		}
		for (i = 1; i <= 5; i++) {
			if (dloc[i] == loc)
				dloc[i] = daltlc;
			odloc[i] = dloc[i];	/* 6002 */
		}
		rspeak(3);
		drop(axe, loc);
		return (2000);
	}
	dtotal = attack = stick = 0;		/* 6010 */
	for (i = 1; i <= 6; i++) {		/* loop to 6030 */
		if (dloc[i] == 0)
			continue;
		j = 1;
		for (kk = travel[dloc[i]]; kk != 0; kk = kk->next) {
			newloc = kk->tloc;
			if (newloc > 300 || newloc < 15 || newloc == odloc[i]
			    || (j > 1 && newloc == tk[j-1]) || j >= 20
			    || newloc == dloc[i] || forced(newloc)
			    || (i == 6 && bitset(newloc, 3))
			    || kk->conditions == 100)
				continue;
			tk[j++] = newloc;
		}
		tk[j] = odloc[i];		/* 6016 */
		if (j >= 2)
			j--;
		j = 1 + ran(j);
		odloc[i] = dloc[i];
		dloc[i] = tk[j];
		dseen[i] = (dseen[i] && loc >= 15) || (dloc[i] == loc || odloc[i] == loc);
		if (!dseen[i])
			continue;	/* i.e. goto 6030 */
		dloc[i] = loc;
		if (i == 6) {		/* pirate's spotted him */
			if (loc == chloc || prop[chest] >= 0)
				continue;
			k = 0;
			for (j = 50; j <= maxtrs; j++) {	/* loop to 6020 */
				if (j == pyram && (loc == plac[pyram]
				    || loc == plac[emrald]))
					goto l6020;
				if (toting(j))
					goto l6022;
l6020:				if (here(j))
					k = 1;
			}				/* 6020 */
			if (tally == tally2 + 1 && k == 0 && place[chest] == 0
			     && here(lamp) && prop[lamp] == 1)
				goto l6025;
			if (odloc[6] != dloc[6] && pct(20))
				rspeak(127);
			continue;	/* to 6030 */
l6022:		rspeak(128);
			if (place[messag] == 0)
				move(chest, chloc);
			move(messag, chloc2);
			for (j = 50; j <= maxtrs; j++) { /* loop to 6023 */
				if (j == pyram && (loc == plac[pyram]
				    || loc == plac[emrald]))
					continue;
				if (at(j) && fixed[j] == 0)
					carry(j, loc);
				if (toting(j))
					drop(j, chloc);
			}
l6024:			dloc[6] = odloc[6] = chloc;
			dseen[6] = FALSE;
			continue;
l6025:			rspeak(186);
			move(chest, chloc);
			move(messag, chloc2);
			goto l6024;
		}
		dtotal++;			/* 6027 */
		if (odloc[i] != dloc[i])
			continue;
		attack++;
		if (knfloc >= 0)
			knfloc = loc;
		if (ran(1000) < 95 * (dflag - 2))
			stick++;
	}					/* 6030 */
	if (dtotal == 0)
		return (2000);
	if (dtotal != 1) {
		printf("There are %d threatening little dwarves ", dtotal);
		printf("in the room with you.\n");
	}
	else
		rspeak(4);
	if (attack == 0)
		return (2000);
	if (dflag == 2)
		dflag = 3;
	if (saved != -1)
		dflag = 20;
	if (attack != 1) {
		printf("%d of them throw knives at you!\n", attack);
		k = 6;
l82:		if (stick <= 1)	{		/* 82 */
			rspeak(k + stick);
			if (stick == 0)
				return (2000);
		} else
			printf("%d of them get you!\n", stick);	/* 83 */
		oldlc2 = loc;
		return (99);
	}
	rspeak(5);
	k = 52;
	goto l82;
}


int
march(void)			/* label 8	*/
{
	int     ll1, ll2;

	if ((tkk = travel[newloc = loc]) == 0)
		bug(26);
	if (k == null)
		return (2);
	if (k == cave) {			/* 40			*/
		if (loc < 8)
			rspeak(57);
		if (loc >= 8)
			rspeak(58);
		return (2);
	}
	if (k == look) {			/* 30			*/
		if (detail++ < 3)
			rspeak(15);
		wzdark = FALSE;
		abb[loc] = 0;
		return (2);
	}
	if (k == back) {			/* 20			*/
		switch(mback()) {
		case 2: return (2);
		case 9: goto l9;
		default: bug(100);
		}
	}
	oldlc2 = oldloc;
	oldloc = loc;
l9:
	for (; tkk != 0; tkk = tkk->next)
		if (tkk->tverb == 1 || tkk->tverb == k)
			break;
	if (tkk == 0) {
		badmove();
		return (2);
	}
l11:	ll1 = tkk->conditions;			/* 11			*/
	ll2 = tkk->tloc;
	newloc = ll1;				/* newloc = conditions	*/
	k = newloc % 100;			/* k used for prob	*/
	if (newloc <= 300) {
		if (newloc <= 100) {		/* 13			*/
			if (newloc != 0 && !pct(newloc))
				goto l12;	/* 14			*/
l16:			newloc = ll2;		/* newloc = location	*/
			if (newloc <= 300)
				return (2);
			if (newloc <= 500)
				switch (specials()) { /* to 30000		*/
				case 2: return (2);
				case 12: goto l12;
				case 99: return (99);
				default: bug(101);
				}
			rspeak(newloc - 500);
			newloc = loc;
			return (2);
		}
		if (toting(k) || (newloc > 200 && at(k)))
			goto l16;
		goto l12;
	}
	if (prop[k] != (newloc / 100) - 3)
		goto l16;	/* newloc still conditions	*/
l12:	/* alternative to probability move	*/
	for (; tkk != 0; tkk = tkk->next)
		if (tkk->tloc != ll2 || tkk->conditions != ll1)
			break;
	if (tkk == 0)
		bug(25);
	goto l11;
}


int
mback(void)			/* 20			*/
{
	struct travlist *tk2,*j;
	int     ll;

	if (forced(k = oldloc))
		k = oldlc2;	/* k = location		*/
	oldlc2 = oldloc;
	oldloc = loc;
	tk2 = 0;
	if (k == loc) {
		rspeak(91);
		return (2);
	}
	for (; tkk != 0; tkk = tkk->next) {	/* 21			*/
		ll = tkk->tloc;
		if (ll == k) {
			k = tkk->tverb;		/* k back to verb	*/
			tkk = travel[loc];
			return (9);
		}
		if (ll <= 300) {
			j = travel[loc];
			if (forced(ll) && k == j->tloc)
				tk2 = tkk;
		}
	}
	tkk = tk2;				/* 23			*/
	if (tkk != 0) {
		k = tkk->tverb;
		tkk = travel[loc];
		return (9);
	}
	rspeak(140);
	return (2);
}


int
specials(void)			/* 30000		*/
{
	switch(newloc -= 300) {
	case 1:			/* 30100		*/
		newloc = 99 + 100 - loc;
		if (holdng == 0 || (holdng == 1 && toting(emrald)))
			return (2);
		newloc = loc;
		rspeak(117);
		return (2);
	case 2:			/* 30200		*/
		drop(emrald, loc);
		return (12);
	case 3:			/* to 30300		*/
		return (trbridge());
	default:
		bug(29);
	}
}


int
trbridge(void)			/* 30300		*/
{
	if (prop[troll] == 1) {
		pspeak(troll, 1);
		prop[troll] = 0;
		move(troll2, 0);
		move(troll2 + 100, 0);
		move(troll, plac[troll]);
		move(troll + 100, fixd[troll]);
		juggle(chasm);
		newloc = loc;
		return (2);
	}
	newloc = plac[troll] + fixd[troll] - loc;	/* 30310		*/
	if (prop[troll] == 0)
		prop[troll] = 1;
	if (!toting(bear))
		return (2);
	rspeak(162);
	prop[chasm] = 1;
	prop[troll] = 2;
	drop(bear, newloc);
	fixed[bear] = -1;
	prop[bear] = 3;
	if (prop[spices] < 0)
		tally2++;
	oldlc2 = newloc;
	return (99);
}


void
badmove(void)					/* 20			*/
{
	spk = 12;
	if (k >= 43 && k <= 50)
		spk = 9;
	if (k == 29 || k == 30)
		spk = 9;
	if (k == 7 || k == 36 || k == 37)
		spk = 10;
	if (k == 11 || k == 19)
		spk = 11;
	if (verb == find || verb == invent)
		spk = 59;
	if (k == 62 || k == 65)
		spk = 42;
	if (k == 17)
		spk = 80;
	rspeak(spk);
}

void
bug(int n)
{
/*	printf("Please tell jim@rand.org that fatal bug %d happened.\n",n); */
	fprintf(stderr,
	    "Please use sendbug to report that bug %d happened in adventure.\n", n);
	exit(n);
}


void
checkhints(void)				/* 2600 &c		*/
{
	int     hint;

	for (hint = 4; hint <= hntmax; hint++) {
		if (hinted[hint])
			continue;
		if (!bitset(loc, hint))
			hintlc[hint] = -1;
		hintlc[hint]++;
		if (hintlc[hint] < hints[hint][1])
			continue;
		switch (hint) {
		case 4:		/* 40400 */
			if (prop[grate] == 0 && !here(keys))
				goto l40010;
			goto l40020;
		case 5:		/* 40500 */
			if (here(bird) && toting(rod) && obj == bird)
				goto l40010;
			continue;      /* i.e. goto l40030 */
		case 6:		/* 40600 */
			if (here(snake) && !here(bird))
				goto l40010;
			goto l40020;
		case 7:		/* 40700 */
			if (atloc[loc] == 0 && atloc[oldloc] == 0
			    && atloc[oldlc2] == 0 && holdng > 1)
				goto l40010;
			goto l40020;
		case 8:		/* 40800 */
			if (prop[emrald] !=  -1 && prop[pyram] == -1)
				goto l40010;
			goto l40020;
		case 9:
			goto l40010;	/* 40900 */
		default:
			bug(27);
		}
l40010:		hintlc[hint] = 0;
		if (!yes(hints[hint][3], 0, 54))
			continue;
		printf("I am prepared to give you a hint, but it will ");
		printf("cost you %d points.\n", hints[hint][2]);
		hinted[hint] = yes(175, hints[hint][4], 54);
l40020:		hintlc[hint] = 0;
	}
}


int
trsay(void)			/* 9030			*/
{
	int i;

	if (wd2[0] != 0)
		strlcpy(wd1, wd2, sizeof(wd1));
	i = vocab(wd1, -1, 0);
	if (i == 62 || i == 65 || i == 71 || i == 2025) {
		wd2[0] = 0;
		obj = 0;
		return (2630);
	}
	printf("\nOkay, \"%s\".\n", wd2);
	return (2012);
}


int
trtake(void)			/* 9010			*/
{
	if (toting(obj))
		return (2011);	/* 9010 */
	spk = 25;
	if (obj == plant && prop[plant] <= 0)
		spk = 115;
	if (obj == bear && prop[bear] == 1)
		spk = 169;
	if (obj == chain && prop[bear] != 0)
		spk = 170;
	if (fixed[obj] != 0)
		return (2011);
	if (obj == water || obj == oil) {
		if (here(bottle) && liq() == obj) {
			obj = bottle;
			goto l9017;
		}
		obj = bottle;
		if (toting(bottle) && prop[bottle] == 1)
			return (9220);
		if (prop[bottle] != 1)
			spk = 105;
		if (!toting(bottle))
			spk = 104;
		return (2011);
	}
l9017:	if (holdng >= 7) {
		rspeak(92);
		return (2012);
	}
	if (obj == bird) {
		if (prop[bird] != 0)
			goto l9014;
		if (toting(rod)) {
			rspeak(26);
			return (2012);
		}
		if (!toting(cage)) {	/* 9013 */
			rspeak(27);
			return (2012);
		}
		prop[bird] = 1;		/* 9015 */
	}
l9014:	if ((obj == bird || obj == cage) && prop[bird] != 0)
		carry(bird + cage - obj, loc);
	carry(obj, loc);
	k = liq();
	if (obj == bottle && k != 0)
		place[k] = -1;
	return (2009);
}


int
dropper(void)			/* 9021			*/
{
	k = liq();
	if (k == obj)
		obj = bottle;
	if (obj == bottle && k != 0)
		place[k] = 0;
	if (obj == cage && prop[bird] != 0)
		drop(bird, loc);
	if (obj == bird)
		prop[bird] = 0;
	drop(obj, loc);
	return (2012);
}

int
trdrop(void)			/* 9020			*/
{
	if (toting(rod2) && obj == rod && !toting(rod))
		obj = rod2;
	if (!toting(obj))
		return (2011);
	if (obj == bird && here(snake)) {
		rspeak(30);
		if (closed)
			return (19000);
		dstroy(snake);
		prop[snake] = 1;
		return (dropper());
	}
	if (obj == coins && here(vend))	{	/* 9024			*/
		dstroy(coins);
		drop(batter, loc);
		pspeak(batter, 0);
		return (2012);
	}
	if (obj == bird && at(dragon) && prop[dragon] == 0) {	/* 9025	*/
		rspeak(154);
		dstroy(bird);
		prop[bird] = 0;
		if (place[snake] == plac[snake])
			tally2--;
		return (2012);
	}
	if (obj == bear && at(troll)) {		/* 9026		*/
		rspeak(163);
		move(troll, 0);
		move(troll + 100, 0);
		move(troll2, plac[troll]);
		move(troll2 + 100, fixd[troll]);
		juggle(chasm);
		prop[troll] = 2;
		return (dropper());
	}
	if (obj != vase || loc == plac[pillow]) {	/* 9027	*/
		rspeak(54);
		return (dropper());
	}
	prop[vase] = 2;				/* 9028		*/
	if (at(pillow))
		prop[vase] = 0;
	pspeak(vase, prop[vase] + 1);
	if (prop[vase] != 0)
		fixed[vase] = -1;
	return (dropper());
}


int
tropen(void)					/* 9040			*/
{
	if (obj == clam || obj == oyster) {
		k = 0;				/* 9046			*/
		if (obj == oyster)
			k = 1;
		spk = 124 + k;
		if (toting(obj))
			spk = 120 + k;
		if (!toting(tridnt))
			spk = 122 + k;
		if (verb == lock)
			spk = 61;
		if (spk != 124)
			return (2011);
		dstroy(clam);
		drop(oyster, loc);
		drop(pearl, 105);
		return (2011);
	}
	if (obj == door)
		spk = 111;
	if (obj == door && prop[door] == 1)
		spk = 54;
	if (obj == cage)
		spk = 32;
	if (obj == keys)
		spk = 55;
	if (obj == grate || obj == chain)
		spk = 31;
	if (spk != 31||!here(keys))
		return (2011);
	if (obj == chain) {
		if (verb == lock) {
			spk = 172;		/* 9049: lock		*/
			if (prop[chain] != 0)
				spk = 34;
			if (loc != plac[chain])
				spk = 173;
			if (spk != 172)
				return (2011);
			prop[chain] = 2;
			if (toting(chain))
				drop(chain, loc);
			fixed[chain] = -1;
			return (2011);
		}
		spk = 171;
		if (prop[bear] == 0)
			spk = 41;
		if (prop[chain] == 0)
			spk = 37;
		if (spk != 171)
			return (2011);
		prop[chain] = 0;
		fixed[chain] = 0;
		if (prop[bear] != 3)
			prop[bear] = 2;
		fixed[bear] = 2 - prop[bear];
		return (2011);
	}
	if (closng) {
		k = 130;
		if (!panic)
			clock2 = 15;
		panic = TRUE;
		return (2010);
	}
	k = 34 + prop[grate];			/* 9043			*/
	prop[grate] = 1;
	if (verb == lock)
		prop[grate] = 0;
	k = k + 2 * prop[grate];
	return (2010);
}


int
trkill(void)				/* 9120				*/
{
	int i;

	for (i = 1; i <= 5; i++)
		if (dloc[i] == loc && dflag >= 2)
			break;
	if (i == 6)
		i = 0;
	if (obj == 0) {			/* 9122				*/
		if (i != 0)
			obj = dwarf;
		if (here(snake))
			obj = obj * 100 + snake;
		if (at(dragon) && prop[dragon] == 0)
			obj = obj * 100 + dragon;
		if (at(troll))
			obj = obj * 100 + troll;
		if (here(bear) && prop[bear] == 0)
			obj = obj * 100 + bear;
		if (obj > 100)
			return (8000);
		if (obj == 0) {
			if (here(bird) && verb != throw)
				obj = bird;
			if (here(clam) || here(oyster))
				obj = 100 * obj + clam;
			if (obj > 100)
				return (8000);
		}
	}
	if (obj == bird) {		/* 9124				*/
		spk = 137;
		if (closed)
			return (2011);
		dstroy(bird);
		prop[bird] = 0;
		if (place[snake] == plac[snake])
			tally2++;
		spk = 45;
	}
	if (obj == 0)
		spk = 44;		/* 9125				*/
	if (obj == clam || obj == oyster)
		spk = 150;
	if (obj == snake)
		spk = 46;
	if (obj == dwarf)
		spk = 49;
	if (obj == dwarf && closed)
		return (19000);
	if (obj == dragon)
		spk = 147;
	if (obj == troll)
		spk = 157;
	if (obj == bear)
		spk = 165 + (prop[bear] + 1) / 2;
	if (obj != dragon || prop[dragon] != 0)
		return (2011);
	rspeak(49);
	verb = 0;
	obj = 0;
	getin(wd1, sizeof(wd1), wd2, sizeof(wd2));
	if (!weq(wd1, "y") && !weq(wd1, "yes"))
		return (2608);
	pspeak(dragon, 1);
	prop[dragon] = 2;
	prop[rug] = 0;
	k = (plac[dragon] + fixd[dragon]) / 2;
	move(dragon + 100, -1);
	move(rug + 100, 0);
	move(dragon, k);
	move(rug, k);
	for (obj = 1; obj <= 100; obj++)
		if (place[obj] == plac[dragon] || place[obj] == fixd[dragon])
			move(obj, k);
	loc = k;
	k = null;
	return (8);
}


int
trtoss(void)				/* 9170: throw			*/
{
	int i;

	if (toting(rod2) && obj == rod && !toting(rod))
		obj = rod2;
	if (!toting(obj))
		return (2011);
	if (obj >= 50 && obj <= maxtrs && at(troll)) {
		spk = 159;			/* 9178			*/
		drop(obj, 0);
		move(troll, 0);
		move(troll + 100, 0);
		drop(troll2, plac[troll]);
		drop(troll2 + 100, fixd[troll]);
		juggle(chasm);
		return (2011);
	}
	if (obj == food && here(bear)) {
		obj = bear;			/* 9177			*/
		return (9210);
	}
	if (obj != axe)
		return (9020);
	for (i = 1; i <= 5; i++) {
		if (dloc[i] == loc) {
			spk = 48;		/* 9172			*/
			if (ran(3) == 0 || saved != -1) {
l9175:
				rspeak(spk);
				drop(axe, loc);
				k = null;
				return (8);
			}
			dseen[i] = FALSE;
			dloc[i] = 0;
			spk = 47;
			dkill++;
			if (dkill == 1)
				spk = 149;
			goto l9175;
		}
	}
	spk = 152;
	if (at(dragon) && prop[dragon] == 0)
		goto l9175;
	spk = 158;
	if (at(troll))
		goto l9175;
	if (here(bear) && prop[bear] == 0) {
		spk = 164;
		drop(axe, loc);
		fixed[axe] = -1;
		prop[axe] = 1;
		juggle(bear);
		return (2011);
	}
	obj = 0;
	return (9120);
}


int
trfeed(void)					/* 9210			*/
{
	if (obj == bird) {
		spk = 100;
		return (2011);
	}
	if (obj == snake || obj == dragon || obj == troll) {
		spk = 102;
		if (obj == dragon && prop[dragon] != 0)
			spk = 110;
		if (obj == troll)
			spk = 182;
		if (obj != snake || closed || !here(bird))
			return (2011);
		spk = 101;
		dstroy(bird);
		prop[bird] = 0;
		tally2++;
		return (2011);
	}
	if (obj == dwarf) {
		if (!here(food))
			return (2011);
		spk = 103;
		dflag++;
		return (2011);
	}
	if (obj == bear) {
		if (prop[bear] == 0)
			spk = 102;
		if (prop[bear] == 3)
			spk = 110;
		if (!here(food))
			return (2011);
		dstroy(food);
		prop[bear] = 1;
		fixed[axe] = 0;
		prop[axe] = 0;
		spk = 168;
		return (2011);
	}
	spk = 14;
	return (2011);
}


int
trfill(void)					/* 9220 */
{
	if (obj == vase) {
		spk = 29;
		if (liqloc(loc) == 0)
			spk = 144;
		if (liqloc(loc) == 0 || !toting(vase))
			return (2011);
		rspeak(145);
		prop[vase] = 2;
		fixed[vase] = -1;
		return (9020);		/* advent/10 goes to 9024 */
	}
	if (obj != 0 && obj != bottle)
		return (2011);
	if (obj == 0 && !here(bottle))
		return (8000);
	spk = 107;
	if (liqloc(loc) == 0)
		spk = 106;
	if (liq() != 0)
		spk = 105;
	if (spk != 107)
		return (2011);
	prop[bottle] = ((cond[loc] % 4) / 2) * 2;
	k = liq();
	if (toting(bottle))
		place[k] = -1;
	if (k == oil)
		spk = 108;
	return (2011);
}


void
closing(void)				/* 10000 */
{
	int i;

	prop[grate] = prop[fissur] = 0;
	for (i = 1; i <= 6; i++) {
		dseen[i] = FALSE;
		dloc[i] = 0;
	}
	move(troll, 0);
	move(troll + 100, 0);
	move(troll2, plac[troll]);
	move(troll2 + 100, fixd[troll]);
	juggle(chasm);
	if (prop[bear] != 3)
		dstroy(bear);
	prop[chain] = 0;
	fixed[chain] = 0;
	prop[axe] = 0;
	fixed[axe] = 0;
	rspeak(129);
	clock1 = -1;
	closng = TRUE;
}


void
caveclose(void)				/* 11000 */
{
	int i;

	prop[bottle] = put(bottle, 115, 1);
	prop[plant] = put(plant, 115, 0);
	prop[oyster] = put(oyster, 115, 0);
	prop[lamp] = put(lamp, 115, 0);
	prop[rod] = put(rod, 115, 0);
	prop[dwarf] = put(dwarf, 115, 0);
	loc = 115;
	oldloc = 115;
	newloc = 115;

	put(grate, 116, 0);
	prop[snake] = put(snake, 116, 1);
	prop[bird] = put(bird, 116, 1);
	prop[cage] = put(cage, 116, 0);
	prop[rod2] = put(rod2, 116, 0);
	prop[pillow] = put(pillow, 116, 0);

	prop[mirror] = put(mirror, 115, 0);
	fixed[mirror] = 116;

	for (i = 1; i <= 100; i++)
		if (toting(i))
			dstroy(i);
	rspeak(132);
	closed = TRUE;
}
