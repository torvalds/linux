/*	$OpenBSD: hack.steal.c,v 1.7 2016/01/09 18:33:15 mestre Exp $	*/

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

#include <stdlib.h>

#include "hack.h"

extern void (*afternmv)(void);

/* actually returns something that fits in an int */
long
somegold(void)
{
	return( (u.ugold < 100) ? u.ugold :
		(u.ugold > 10000) ? rnd(10000) : rnd((int) u.ugold) );
}

void
stealgold(struct monst *mtmp)
{
	struct gold *gold = g_at(u.ux, u.uy);
	long tmp;

	if(gold && ( !u.ugold || gold->amount > u.ugold || !rn2(5))) {
		mtmp->mgold += gold->amount;
		freegold(gold);
		if(Invisible) newsym(u.ux, u.uy);
		pline("%s quickly snatches some gold from between your feet!",
			Monnam(mtmp));
		if(!u.ugold || !rn2(5)) {
			rloc(mtmp);
			mtmp->mflee = 1;
		}
	} else if(u.ugold) {
		u.ugold -= (tmp = somegold());
		pline("Your purse feels lighter.");
		mtmp->mgold += tmp;
		rloc(mtmp);
		mtmp->mflee = 1;
		flags.botl = 1;
	}
}

/* steal armor after he finishes taking it off */
static unsigned stealoid;		/* object to be stolen */
static unsigned stealmid;		/* monster doing the stealing */

void
stealarm(void)
{
	struct monst *mtmp;
	struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj)
	  if(otmp->o_id == stealoid) {
	    for(mtmp = fmon; mtmp; mtmp = mtmp->nmon)
	      if(mtmp->m_id == stealmid) {
		if(dist(mtmp->mx,mtmp->my) < 3) {
		  freeinv(otmp);
		  pline("%s steals %s!", Monnam(mtmp), doname(otmp));
		  mpickobj(mtmp,otmp);
		  mtmp->mflee = 1;
		  rloc(mtmp);
		}
		break;
	      }
	    break;
	  }
	stealoid = 0;
}

/* returns 1 when something was stolen */
/* (or at least, when N should flee now) */
/* avoid stealing the object stealoid */
int
steal(struct monst *mtmp)
{
	struct obj *otmp;
	int tmp;
	int named = 0;

	if(!invent){
	    if(Blind)
	      pline("Somebody tries to rob you, but finds nothing to steal.");
	    else
	      pline("%s tries to rob you, but she finds nothing to steal!",
		Monnam(mtmp));
	    return(1);	/* let her flee */
	}
	tmp = 0;
	for(otmp = invent; otmp; otmp = otmp->nobj) if(otmp != uarm2)
		tmp += ((otmp->owornmask & (W_ARMOR | W_RING)) ? 5 : 1);
	tmp = rn2(tmp);
	for(otmp = invent; otmp; otmp = otmp->nobj) if(otmp != uarm2)
		if((tmp -= ((otmp->owornmask & (W_ARMOR | W_RING)) ? 5 : 1))
			< 0) break;
	if(!otmp) {
		impossible("Steal fails!");
		return(0);
	}
	if(otmp->o_id == stealoid)
		return(0);
	if((otmp->owornmask & (W_ARMOR | W_RING))){
		switch(otmp->olet) {
		case RING_SYM:
			ringoff(otmp);
			break;
		case ARMOR_SYM:
			if(multi < 0 || otmp == uarms){
			  setworn((struct obj *) 0, otmp->owornmask & W_ARMOR);
			  break;
			}
		{ int curssv = otmp->cursed;
			otmp->cursed = 0;
			stop_occupation();
			pline("%s seduces you and %s off your %s.",
				Amonnam(mtmp, Blind ? "gentle" : "beautiful"),
				otmp->cursed ? "helps you to take"
					    : "you start taking",
				(otmp == uarmg) ? "gloves" :
				(otmp == uarmh) ? "helmet" : "armor");
			named++;
			(void) armoroff(otmp);
			otmp->cursed = curssv;
			if(multi < 0){
				stealoid = otmp->o_id;
				stealmid = mtmp->m_id;
				afternmv = stealarm;
				return(0);
			}
			break;
		}
		default:
			impossible("Tried to steal a strange worn thing.");
		}
	}
	else if(otmp == uwep)
		setuwep((struct obj *) 0);
	if(otmp->olet == CHAIN_SYM) {
		impossible("How come you are carrying that chain?");
	}
	if(Punished && otmp == uball){
		Punished = 0;
		freeobj(uchain);
		free(uchain);
		uchain = (struct obj *) 0;
		uball->spe = 0;
		uball = (struct obj *) 0;	/* superfluous */
	}
	freeinv(otmp);
	pline("%s stole %s.", named ? "She" : Monnam(mtmp), doname(otmp));
	mpickobj(mtmp,otmp);
	return((multi < 0) ? 0 : 1);
}

void
mpickobj(struct monst *mtmp, struct obj *otmp)
{
	otmp->nobj = mtmp->minvent;
	mtmp->minvent = otmp;
}

int
stealamulet(struct monst *mtmp)
{
	struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
	    if(otmp->olet == AMULET_SYM) {
		/* might be an imitation one */
		if(otmp == uwep) setuwep((struct obj *) 0);
		freeinv(otmp);
		mpickobj(mtmp,otmp);
		pline("%s stole %s!", Monnam(mtmp), doname(otmp));
		return(1);
	    }
	}
	return(0);
}

/* release the objects the killed animal has stolen */
void
relobj(struct monst *mtmp, int show)
{
	struct obj *otmp, *otmp2;

	for(otmp = mtmp->minvent; otmp; otmp = otmp2){
		otmp->ox = mtmp->mx;
		otmp->oy = mtmp->my;
		otmp2 = otmp->nobj;
		otmp->nobj = fobj;
		fobj = otmp;
		stackobj(fobj);
		if(show & cansee(mtmp->mx,mtmp->my))
			atl(otmp->ox,otmp->oy,otmp->olet);
	}
	mtmp->minvent = (struct obj *) 0;
	if(mtmp->mgold || mtmp->data->mlet == 'L') {
		long tmp;

		tmp = (mtmp->mgold > 10000) ? 10000 : mtmp->mgold;
		mkgold((long)(tmp + d(dlevel,30)), mtmp->mx, mtmp->my);
		if(show & cansee(mtmp->mx,mtmp->my))
			atl(mtmp->mx,mtmp->my,'$');
	}
}
