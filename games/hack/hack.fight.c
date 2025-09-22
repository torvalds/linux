/*	$OpenBSD: hack.fight.c,v 1.10 2016/01/09 18:33:15 mestre Exp $	*/

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

#include <stdio.h>

#include "hack.h"

extern struct permonst li_dog, dog, la_dog;

static boolean far_noise;
static long noisetime;

static void monstone(struct monst *);

/* hitmm returns 0 (miss), 1 (hit), or 2 (kill) */
int
hitmm(struct monst * magr, struct monst * mdef)
{
	struct permonst *pa = magr->data, *pd = mdef->data;
	int hit;
	schar tmp;
	boolean vis;

	if(strchr("Eauy", pa->mlet)) return(0);
	if(magr->mfroz) return(0);		/* riv05!a3 */
	tmp = pd->ac + pa->mlevel;
	if(mdef->mconf || mdef->mfroz || mdef->msleep){
		tmp += 4;
		if(mdef->msleep) mdef->msleep = 0;
	}
	hit = (tmp > rnd(20));
	if(hit) mdef->msleep = 0;
	vis = (cansee(magr->mx,magr->my) && cansee(mdef->mx,mdef->my));
	if(vis){
		char buf[BUFSZ];
		if(mdef->mimic) seemimic(mdef);
		if(magr->mimic) seemimic(magr);
		(void) snprintf(buf,sizeof buf,"%s %s", Monnam(magr),
			hit ? "hits" : "misses");
		pline("%s %s.", buf, monnam(mdef));
	} else {
		boolean far = (dist(magr->mx, magr->my) > 15);
		if(far != far_noise || moves-noisetime > 10) {
			far_noise = far;
			noisetime = moves;
			pline("You hear some noises%s.",
				far ? " in the distance" : "");
		}
	}
	if(hit){
		if(magr->data->mlet == 'c' && !magr->cham) {
			magr->mhpmax += 3;
			if(vis) pline("%s is turned to stone!", Monnam(mdef));
			else if(mdef->mtame)
     pline("You have a peculiarly sad feeling for a moment, then it passes.");
			monstone(mdef);
			hit = 2;
		} else
		if((mdef->mhp -= d(pa->damn,pa->damd)) < 1) {
			magr->mhpmax += 1 + rn2(pd->mlevel+1);
			if(magr->mtame && magr->mhpmax > 8*pa->mlevel){
				if(pa == &li_dog) magr->data = pa = &dog;
				else if(pa == &dog) magr->data = pa = &la_dog;
			}
			if(vis) pline("%s is killed!", Monnam(mdef));
			else if(mdef->mtame)
		pline("You have a sad feeling for a moment, then it passes.");
			mondied(mdef);
			hit = 2;
		}
	}
	return(hit);
}

/* drop (perhaps) a cadaver and remove monster */
void
mondied(struct monst *mdef)
{
	struct permonst *pd = mdef->data;

	if(letter(pd->mlet) && rn2(3)){
		(void) mkobj_at(pd->mlet,mdef->mx,mdef->my);
		if(cansee(mdef->mx,mdef->my)){
			unpmon(mdef);
			atl(mdef->mx,mdef->my,fobj->olet);
		}
		stackobj(fobj);
	}
	mondead(mdef);
}

/* drop a rock and remove monster */
static void
monstone(struct monst *mdef)
{
	extern char mlarge[];
	if(strchr(mlarge, mdef->data->mlet))
		mksobj_at(ENORMOUS_ROCK, mdef->mx, mdef->my);
	else
		mksobj_at(ROCK, mdef->mx, mdef->my);
	if(cansee(mdef->mx, mdef->my)){
		unpmon(mdef);
		atl(mdef->mx,mdef->my,fobj->olet);
	}
	mondead(mdef);
}

int
fightm(struct monst *mtmp)
{
	struct monst *mon;

	for(mon = fmon; mon; mon = mon->nmon) if(mon != mtmp) {
		if(DIST(mon->mx,mon->my,mtmp->mx,mtmp->my) < 3)
		    if(rn2(4))
			return(hitmm(mtmp,mon));
	}
	return(-1);
}

/* u is hit by sth, but not a monster */
int
thitu(int tlev, int dam, char *name)
{
	char buf[BUFSZ];

	setan(name,buf,sizeof buf);
	if(u.uac + tlev <= rnd(20)) {
		if(Blind) pline("It misses.");
		else pline("You are almost hit by %s!", buf);
		return(0);
	} else {
		if(Blind) pline("You are hit!");
		else pline("You are hit by %s!", buf);
		losehp(dam,name);
		return(1);
	}
}

char mlarge[] = "bCDdegIlmnoPSsTUwY',&";

/* return TRUE if mon still alive */
boolean
hmon(struct monst *mon, struct obj *obj, int thrown)
{
	int tmp;
	boolean hittxt = FALSE;

	if(!obj){
		tmp = rnd(2);	/* attack with bare hands */
		if(mon->data->mlet == 'c' && !uarmg){
			pline("You hit the cockatrice with your bare hands.");
			pline("You turn to stone ...");
			done_in_by(mon);
		}
	} else if(obj->olet == WEAPON_SYM || obj->otyp == PICK_AXE) {
	    if(obj == uwep && (obj->otyp > SPEAR || obj->otyp < BOOMERANG))
		tmp = rnd(2);
	    else {
		if(strchr(mlarge, mon->data->mlet)) {
			tmp = rnd(objects[obj->otyp].wldam);
			if(obj->otyp == TWO_HANDED_SWORD) tmp += d(2,6);
			else if(obj->otyp == FLAIL) tmp += rnd(4);
		} else {
			tmp = rnd(objects[obj->otyp].wsdam);
		}
		tmp += obj->spe;
		if(!thrown && obj == uwep && obj->otyp == BOOMERANG
		 && !rn2(3)){
		  pline("As you hit %s, the boomerang breaks into splinters.",
				monnam(mon));
			freeinv(obj);
			setworn((struct obj *) 0, obj->owornmask);
			obfree(obj, (struct obj *) 0);
			tmp++;
		}
	    }
	    if(mon->data->mlet == 'O' && obj->otyp == TWO_HANDED_SWORD &&
		!strcmp(ONAME(obj), "Orcrist"))
		tmp += rnd(10);
	} else	switch(obj->otyp) {
		case HEAVY_IRON_BALL:
			tmp = rnd(25); break;
		case EXPENSIVE_CAMERA:
	pline("You succeed in destroying your camera. Congratulations!");
			freeinv(obj);
			if(obj->owornmask)
				setworn((struct obj *) 0, obj->owornmask);
			obfree(obj, (struct obj *) 0);
			return(TRUE);
		case DEAD_COCKATRICE:
			pline("You hit %s with the cockatrice corpse.",
				monnam(mon));
			if(mon->data->mlet == 'c') {
				tmp = 1;
				hittxt = TRUE;
				break;
			}
			pline("%s is turned to stone!", Monnam(mon));
			killed(mon);
			return(FALSE);
		case CLOVE_OF_GARLIC:		/* no effect against demons */
			if(strchr(UNDEAD, mon->data->mlet))
				mon->mflee = 1;
			tmp = 1;
			break;
		default:
			/* non-weapons can damage because of their weight */
			/* (but not too much) */
			tmp = obj->owt/10;
			if(tmp < 1) tmp = 1;
			else tmp = rnd(tmp);
			if(tmp > 6) tmp = 6;
		}

	/****** NOTE: perhaps obj is undefined!! (if !thrown && BOOMERANG) */

	tmp += u.udaminc + dbon();
	if(u.uswallow) {
		if((tmp -= u.uswldtim) <= 0) {
			pline("Your arms are no longer able to hit.");
			return(TRUE);
		}
	}
	if(tmp < 1) tmp = 1;
	mon->mhp -= tmp;
	if(mon->mhp < 1) {
		killed(mon);
		return(FALSE);
	}
	if(mon->mtame && (!mon->mflee || mon->mfleetim)) {
		mon->mflee = 1;			/* Rick Richardson */
		mon->mfleetim += 10*rnd(tmp);
	}

	if(!hittxt) {
		if(thrown)
			/* this assumes that we cannot throw plural things */
			hit( xname(obj)  /* or: objects[obj->otyp].oc_name */,
				mon, exclam(tmp) );
		else if(Blind)
			pline("You hit it.");
		else
			pline("You hit %s%s", monnam(mon), exclam(tmp));
	}

	if(u.umconf && !thrown) {
		if(!Blind) {
			pline("Your hands stop glowing blue.");
			if(!mon->mfroz && !mon->msleep)
				pline("%s appears confused.",Monnam(mon));
		}
		mon->mconf = 1;
		u.umconf = 0;
	}
	return(TRUE);	/* mon still alive */
}

/* try to attack; return FALSE if monster evaded */
/* u.dx and u.dy must be set */
boolean
attack(struct monst *mtmp)
{
	schar tmp;
	boolean malive = TRUE;
	struct permonst *mdat;
	mdat = mtmp->data;

	u_wipe_engr(3);   /* andrew@orca: prevent unlimited pick-axe attacks */

	if(mdat->mlet == 'L' && !mtmp->mfroz && !mtmp->msleep &&
	   !mtmp->mconf && mtmp->mcansee && !rn2(7) &&
	   (m_move(mtmp, 0) == 2 /* he died */ || /* he moved: */
		mtmp->mx != u.ux+u.dx || mtmp->my != u.uy+u.dy))
		return(FALSE);

	if(mtmp->mimic){
		if(!u.ustuck && !mtmp->mflee) u.ustuck = mtmp;
		switch(levl[u.ux+u.dx][u.uy+u.dy].scrsym){
		case '+':
			pline("The door actually was a Mimic.");
			break;
		case '$':
			pline("The chest was a Mimic!");
			break;
		default:
			pline("Wait! That's a Mimic!");
		}
		wakeup(mtmp);	/* clears mtmp->mimic */
		return(TRUE);
	}

	wakeup(mtmp);

	if(mtmp->mhide && mtmp->mundetected){
		struct obj *obj;

		mtmp->mundetected = 0;
		if((obj = o_at(mtmp->mx,mtmp->my)) && !Blind)
			pline("Wait! There's a %s hiding under %s!",
				mdat->mname, doname(obj));
		return(TRUE);
	}

	tmp = u.uluck + u.ulevel + mdat->ac + abon();
	if(uwep) {
		if(uwep->olet == WEAPON_SYM || uwep->otyp == PICK_AXE)
			tmp += uwep->spe;
		if(uwep->otyp == TWO_HANDED_SWORD) tmp -= 1;
		else if(uwep->otyp == DAGGER) tmp += 2;
		else if(uwep->otyp == CRYSKNIFE) tmp += 3;
		else if(uwep->otyp == SPEAR &&
			strchr("XDne", mdat->mlet)) tmp += 2;
	}
	if(mtmp->msleep) {
		mtmp->msleep = 0;
		tmp += 2;
	}
	if(mtmp->mfroz) {
		tmp += 4;
		if(!rn2(10)) mtmp->mfroz = 0;
	}
	if(mtmp->mflee) tmp += 2;
	if(u.utrap) tmp -= 3;

	/* with a lot of luggage, your agility diminishes */
	tmp -= (inv_weight() + 40)/20;

	if(tmp <= rnd(20) && !u.uswallow){
		if(Blind) pline("You miss it.");
		else pline("You miss %s.",monnam(mtmp));
	} else {
		/* we hit the monster; be careful: it might die! */

		if((malive = hmon(mtmp,uwep,0)) == TRUE) {
		/* monster still alive */
			if(!rn2(25) && mtmp->mhp < mtmp->mhpmax/2) {
				mtmp->mflee = 1;
				if(!rn2(3)) mtmp->mfleetim = rnd(100);
				if(u.ustuck == mtmp && !u.uswallow)
					u.ustuck = 0;
			}
#ifndef NOWORM
			if(mtmp->wormno)
				cutworm(mtmp, u.ux+u.dx, u.uy+u.dy,
					uwep ? uwep->otyp : 0);
#endif /* NOWORM */
		}
		if(mdat->mlet == 'a') {
			if(rn2(2)) {
				pline("You are splashed by the blob's acid!");
				losehp_m(rnd(6), mtmp);
				if(!rn2(30)) corrode_armor();
			}
			if(!rn2(6)) corrode_weapon();
		}
	}
	if(malive && mdat->mlet == 'E' && canseemon(mtmp)
	   && !mtmp->mcan && rn2(3)) {
	    if(mtmp->mcansee) {
	      pline("You are frozen by the floating eye's gaze!");
	      nomul((u.ulevel > 6 || rn2(4)) ? rn1(20,-21) : -200);
	    } else {
	      pline("The blinded floating eye cannot defend itself.");
	      if(!rn2(500)) if((int)u.uluck > LUCKMIN) u.uluck--;
	    }
	}
	return(TRUE);
}
