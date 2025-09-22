/*	$OpenBSD: hack.mhitu.c,v 1.8 2016/01/09 18:33:15 mestre Exp $	*/

/*
 * Copyright (c) 1985, Stichting Centrum voor Wiskunde en Informatica,
 * Amsterdam
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of the Stichting Centrum voor Wiskunde en
 * Informatica, nor the names of its contributors may be used to endorse or
 * promote products derived from this software without specific prior
 * written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982 Jay Fenlason <hack@gnu.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "hack.h"

extern struct monst *makemon(struct permonst *, int, int);

/*
 * mhitu: monster hits you
 *	  returns 1 if monster dies (e.g. 'y', 'F'), 0 otherwise
 */
int
mhitu(struct monst *mtmp)
{
	struct permonst *mdat = mtmp->data;
	int tmp, ctmp;

	nomul(0);

	/* If swallowed, can only be affected by hissers and by u.ustuck */
	if(u.uswallow) {
		if(mtmp != u.ustuck) {
			if(mdat->mlet == 'c' && !rn2(13)) {
				pline("Outside, you hear %s's hissing!",
					monnam(mtmp));
				pline("%s gets turned to stone!",
					Monnam(u.ustuck));
				pline("And the same fate befalls you.");
				done_in_by(mtmp);
				/* "notreached": not return(1); */
			}
			return(0);
		}
		switch(mdat->mlet) {	/* now mtmp == u.ustuck */
		case ',':
			youswld(mtmp, (u.uac > 0) ? u.uac+4 : 4,
				5, "The trapper");
			break;
		case '\'':
			youswld(mtmp,rnd(6),7,"The lurker above");
			break;
		case 'P':
			youswld(mtmp,d(2,4),12,"The purple worm");
			break;
		default:
			/* This is not impossible! */
			pline("The mysterious monster totally digests you.");
			u.uhp = 0;
		}
		if(u.uhp < 1) done_in_by(mtmp);
		return(0);
	}

	if(mdat->mlet == 'c' && Stoned)
		return(0);

	/* make eels visible the moment they hit/miss us */
	if(mdat->mlet == ';' && mtmp->minvis && cansee(mtmp->mx,mtmp->my)){
		mtmp->minvis = 0;
		pmon(mtmp);
	}
	if(!strchr("1&DuxynNF",mdat->mlet))
		tmp = hitu(mtmp,d(mdat->damn,mdat->damd));
	else
		tmp = 0;
	if(strchr(UNDEAD, mdat->mlet) && midnight())
		tmp += hitu(mtmp,d(mdat->damn,mdat->damd));

	ctmp = tmp && !mtmp->mcan &&
	  (!uarm || objects[uarm->otyp].a_can < rnd(3) || !rn2(50));
	switch(mdat->mlet) {
	case '1':
		if(wiz_hit(mtmp)) return(1);	/* he disappeared */
		break;
	case '&':
		if(!mtmp->cham && !mtmp->mcan && !rn2(13)) {
			(void) makemon(PM_DEMON,u.ux,u.uy);
		} else {
			(void) hitu(mtmp,d(2,6));
			(void) hitu(mtmp,d(2,6));
			(void) hitu(mtmp,rnd(3));
			(void) hitu(mtmp,rnd(3));
			(void) hitu(mtmp,rn1(4,2));
		}
		break;
	case ',':
		if(tmp) justswld(mtmp,"The trapper");
		break;
	case '\'':
		if(tmp) justswld(mtmp, "The lurker above");
		break;
	case ';':
		if(ctmp) {
			if(!u.ustuck && !rn2(10)) {
				pline("%s swings itself around you!",
					Monnam(mtmp));
				u.ustuck = mtmp;
			} else if(u.ustuck == mtmp &&
			    levl[(int)mtmp->mx][(int)mtmp->my].typ == POOL) {
				pline("%s drowns you ...", Monnam(mtmp));
				done("drowned");
			}
		}
		break;
	case 'A':
		if(ctmp && rn2(2)) {
		    if(Poison_resistance)
			pline("The sting doesn't seem to affect you.");
		    else {
			pline("You feel weaker!");
			losestr(1);
		    }
		}
		break;
	case 'C':
		(void) hitu(mtmp,rnd(6));
		break;
	case 'c':
		if(!rn2(5)) {
			pline("You hear %s's hissing!", monnam(mtmp));
			if(ctmp || !rn2(20) || (flags.moonphase == NEW_MOON
			    && !carrying(DEAD_LIZARD))) {
				Stoned = 5;
				/* pline("You get turned to stone!"); */
				/* done_in_by(mtmp); */
			}
		}
		break;
	case 'D':
		if(rn2(6) || mtmp->mcan) {
			(void) hitu(mtmp,d(3,10));
			(void) hitu(mtmp,rnd(8));
			(void) hitu(mtmp,rnd(8));
			break;
		}
		kludge("%s breathes fire!","The dragon");
		buzz(-1,mtmp->mx,mtmp->my,u.ux-mtmp->mx,u.uy-mtmp->my);
		break;
	case 'd':
		(void) hitu(mtmp,d(2, (flags.moonphase == FULL_MOON) ? 3 : 4));
		break;
	case 'e':
		(void) hitu(mtmp,d(3,6));
		break;
	case 'F':
		if(mtmp->mcan) break;
		kludge("%s explodes!","The freezing sphere");
		if(Cold_resistance) pline("You don't seem affected by it.");
		else {
			xchar dn;
			if(17-(u.ulevel/2) > rnd(20)) {
				pline("You get blasted!");
				dn = 6;
			} else {
				pline("You duck the blast...");
				dn = 3;
			}
			losehp_m(d(dn,6), mtmp);
		}
		mondead(mtmp);
		return(1);
	case 'g':
		if(ctmp && multi >= 0 && !rn2(3)) {
			kludge("You are frozen by %ss juices","the cube'");
			nomul(-rnd(10));
		}
		break;
	case 'h':
		if(ctmp && multi >= 0 && !rn2(5)) {
			nomul(-rnd(10));
			kludge("You are put to sleep by %ss bite!",
				"the homunculus'");
		}
		break;
	case 'j':
		tmp = hitu(mtmp,rnd(3));
		tmp &= hitu(mtmp,rnd(3));
		if(tmp){
			(void) hitu(mtmp,rnd(4));
			(void) hitu(mtmp,rnd(4));
		}
		break;
	case 'k':
		if((hitu(mtmp,rnd(4)) || !rn2(3)) && ctmp){
			poisoned("bee's sting",mdat->mname);
		}
		break;
	case 'L':
		if(tmp) stealgold(mtmp);
		break;
	case 'N':
		if(mtmp->mcan && !Blind) {
	pline("%s tries to seduce you, but you seem not interested.",
			Amonnam(mtmp, "plain"));
			if(rn2(3)) rloc(mtmp);
		} else if(steal(mtmp)) {
			rloc(mtmp);
			mtmp->mflee = 1;
		}
		break;
	case 'n':
		if(!uwep && !uarm && !uarmh && !uarms && !uarmg) {
		    pline("%s hits! (I hope you don't mind)",
			Monnam(mtmp));
			u.uhp += rnd(7);
			if(!rn2(7)) u.uhpmax++;
			if(u.uhp > u.uhpmax) u.uhp = u.uhpmax;
			flags.botl = 1;
			if(!rn2(50)) rloc(mtmp);
		} else {
			(void) hitu(mtmp,d(2,6));
			(void) hitu(mtmp,d(2,6));
		}
		break;
	case 'o':
		tmp = hitu(mtmp,rnd(6));
		if(hitu(mtmp,rnd(6)) && tmp &&	/* hits with both paws */
		    !u.ustuck && rn2(2)) {
			u.ustuck = mtmp;
			kludge("%s has grabbed you!","The owlbear");
			u.uhp -= d(2,8);
		} else if(u.ustuck == mtmp) {
			u.uhp -= d(2,8);
			pline("You are being crushed.");
		}
		break;
	case 'P':
		if(ctmp && !rn2(4))
			justswld(mtmp,"The purple worm");
		else
			(void) hitu(mtmp,d(2,4));
		break;
	case 'Q':
		(void) hitu(mtmp,rnd(2));
		(void) hitu(mtmp,rnd(2));
		break;
	case 'R':
		if(tmp && uarmh && !uarmh->rustfree &&
		    (int) uarmh->spe >= -1) {
			pline("Your helmet rusts!");
			uarmh->spe--;
		} else
		if(ctmp && uarm && !uarm->rustfree &&	/* Mike Newton */
		 uarm->otyp < STUDDED_LEATHER_ARMOR &&
		 (int) uarm->spe >= -1) {
			pline("Your armor rusts!");
			uarm->spe--;
		}
		break;
	case 'S':
		if(ctmp && !rn2(8)) {
			poisoned("snake's bite",mdat->mname);
		}
		break;
	case 's':
		if(tmp && !rn2(8)) {
			poisoned("scorpion's sting",mdat->mname);
		}
		(void) hitu(mtmp,rnd(8));
		(void) hitu(mtmp,rnd(8));
		break;
	case 'T':
		(void) hitu(mtmp,rnd(6));
		(void) hitu(mtmp,rnd(6));
		break;
	case 't':
		if(!rn2(5)) rloc(mtmp);
		break;
	case 'u':
		mtmp->mflee = 1;
		break;
	case 'U':
		(void) hitu(mtmp,d(3,4));
		(void) hitu(mtmp,d(3,4));
		break;
	case 'v':
		if(ctmp && !u.ustuck) u.ustuck = mtmp;
		break;
	case 'V':
		if(tmp) u.uhp -= 4;
		if(ctmp) losexp();
		break;
	case 'W':
		if(ctmp) losexp();
		break;
#ifndef NOWORM
	case 'w':
		if(tmp) wormhit(mtmp);
#endif /* NOWORM */
		break;
	case 'X':
		(void) hitu(mtmp,rnd(5));
		(void) hitu(mtmp,rnd(5));
		(void) hitu(mtmp,rnd(5));
		break;
	case 'x':
		{ long side = rn2(2) ? RIGHT_SIDE : LEFT_SIDE;
		  pline("%s pricks in your %s leg!",
			Monnam(mtmp), (side == RIGHT_SIDE) ? "right" : "left");
		  set_wounded_legs(side, rnd(50));
		  losehp_m(2, mtmp);
		  break;
		}
	case 'y':
		if(mtmp->mcan) break;
		mondead(mtmp);
		if(!Blind) {
			pline("You are blinded by a blast of light!");
			Blind = d(4,12);
			seeoff(0);
		}
		return(1);
	case 'Y':
		(void) hitu(mtmp,rnd(6));
		break;
	}
	if(u.uhp < 1) done_in_by(mtmp);
	return(0);
}

int
hitu(struct monst *mtmp, int dam)
{
	int tmp, res;

	nomul(0);
	if(u.uswallow) return(0);

	if(mtmp->mhide && mtmp->mundetected) {
		mtmp->mundetected = 0;
		if(!Blind) {
			struct obj *obj;
			if ((obj = o_at(mtmp->mx,mtmp->my)))
				pline("%s was hidden under %s!",
					Xmonnam(mtmp), doname(obj));
		}
	}

	tmp = u.uac;
	/* give people with Ac = -10 at least some vulnerability */
	if(tmp < 0) {
		dam += tmp;		/* decrease damage */
		if(dam <= 0) dam = 1;
		tmp = -rn2(-tmp);
	}
	tmp += mtmp->data->mlevel;
	if(multi < 0) tmp += 4;
	if((Invis && mtmp->data->mlet != 'I') || !mtmp->mcansee) tmp -= 2;
	if(mtmp->mtrapped) tmp -= 2;
	if(tmp <= rnd(20)) {
		if(Blind) pline("It misses.");
		else pline("%s misses.",Monnam(mtmp));
		res = 0;
	} else {
		if(Blind) pline("It hits!");
		else pline("%s hits!",Monnam(mtmp));
		losehp_m(dam, mtmp);
		res = 1;
	}
	stop_occupation();
	return(res);
}
