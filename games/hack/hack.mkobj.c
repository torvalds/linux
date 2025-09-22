/*	$OpenBSD: hack.mkobj.c,v 1.7 2009/10/27 23:59:25 deraadt Exp $	*/

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

char mkobjstr[] = "))[[!!!!????%%%%/=**))[[!!!!????%%%%/=**(%";

struct obj *
mkobj_at(int let, int x, int y)
{
	struct obj *otmp = mkobj(let);
	otmp->ox = x;
	otmp->oy = y;
	otmp->nobj = fobj;
	fobj = otmp;
	return(otmp);
}

void
mksobj_at(int otyp, int x, int y)
{
	struct obj *otmp = mksobj(otyp);
	otmp->ox = x;
	otmp->oy = y;
	otmp->nobj = fobj;
	fobj = otmp;
}

struct obj *
mkobj(int let)
{
	if(!let)
		let = mkobjstr[rn2(sizeof(mkobjstr) - 1)];
	return(
	    mksobj(
		letter(let) ?
		    CORPSE + ((let > 'Z') ? (let-'a'+'Z'-'@'+1) : (let-'@'))
		:   probtype(let)
	    )
	);
}
	

struct obj zeroobj;

struct obj *
mksobj(int otyp)
{
	struct obj *otmp;
	char let = objects[otyp].oc_olet;

	otmp = newobj(0);
	*otmp = zeroobj;
	otmp->age = moves;
	otmp->o_id = flags.ident++;
	otmp->quan = 1;
	otmp->olet = let;
	otmp->otyp = otyp;
	otmp->dknown = strchr("/=!?*", let) ? 0 : 1;
	switch(let) {
	case WEAPON_SYM:
		otmp->quan = (otmp->otyp <= ROCK) ? rn1(6,6) : 1;
		if(!rn2(11)) otmp->spe = rnd(3);
		else if(!rn2(10)) {
			otmp->cursed = 1;
			otmp->spe = -rnd(3);
		}
		break;
	case FOOD_SYM:
		if(otmp->otyp >= CORPSE) break;
#ifdef NOT_YET_IMPLEMENTED
		/* if tins are to be identified, need to adapt doname() etc */
		if(otmp->otyp == TIN)
			otmp->spe = rnd(...);
#endif /* NOT_YET_IMPLEMENTED */
		/* fall into next case */
	case GEM_SYM:
		otmp->quan = rn2(6) ? 1 : 2;
	case TOOL_SYM:
	case CHAIN_SYM:
	case BALL_SYM:
	case ROCK_SYM:
	case POTION_SYM:
	case SCROLL_SYM:
	case AMULET_SYM:
		break;
	case ARMOR_SYM:
		if(!rn2(8)) otmp->cursed = 1;
		if(!rn2(10)) otmp->spe = rnd(3);
		else if(!rn2(9)) {
			otmp->spe = -rnd(3);
			otmp->cursed = 1;
		}
		break;
	case WAND_SYM:
		if(otmp->otyp == WAN_WISHING) otmp->spe = 3; else
		otmp->spe = rn1(5,
			(objects[otmp->otyp].bits & NODIR) ? 11 : 4);
		break;
	case RING_SYM:
		if(objects[otmp->otyp].bits & SPEC) {
			if(!rn2(3)) {
				otmp->cursed = 1;
				otmp->spe = -rnd(2);
			} else otmp->spe = rnd(2);
		} else if(otmp->otyp == RIN_TELEPORTATION ||
			  otmp->otyp == RIN_AGGRAVATE_MONSTER ||
			  otmp->otyp == RIN_HUNGER || !rn2(9))
			otmp->cursed = 1;
		break;
	default:
		panic("impossible mkobj");
	}
	otmp->owt = weight(otmp);
	return(otmp);
}

int
letter(int c)
{
	return(('@' <= c && c <= 'Z') || ('a' <= c && c <= 'z'));
}

int
weight(struct obj *obj)
{
	int wt = objects[obj->otyp].oc_weight;

	return(wt ? wt*obj->quan : (obj->quan + 1)/2);
}

void
mkgold(long num, int x, int y)
{
	struct gold *gold;
	long amount = (num ? num : 1 + (rnd(dlevel+2) * rnd(30)));

	if ((gold = g_at(x,y)))
		gold->amount += amount;
	else {
		gold = newgold();
		gold->ngold = fgold;
		gold->gx = x;
		gold->gy = y;
		gold->amount = amount;
		fgold = gold;
		/* do sth with display? */
	}
}
