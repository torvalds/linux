/*	$OpenBSD: hack.wield.c,v 1.7 2016/01/09 18:33:15 mestre Exp $	*/

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

extern struct obj zeroobj;

void
setuwep(struct obj *obj)
{
	setworn(obj, W_WEP);
}

int
dowield(void)
{
	struct obj *wep;
	int res = 0;

	multi = 0;
	if(!(wep = getobj("#-)", "wield"))) /* nothing */;
	else if(uwep == wep)
		pline("You are already wielding that!");
	else if(uwep && uwep->cursed)
		pline("The %s welded to your hand!",
			aobjnam(uwep, "are"));
	else if(wep == &zeroobj) {
		if(uwep == 0){
			pline("You are already empty handed.");
		} else {
			setuwep((struct obj *) 0);
			res++;
			pline("You are empty handed.");
		}
	} else if(uarms && wep->otyp == TWO_HANDED_SWORD)
	pline("You cannot wield a two-handed sword and wear a shield.");
	else if(wep->owornmask & (W_ARMOR | W_RING))
		pline("You cannot wield that!");
	else {
		setuwep(wep);
		res++;
		if(uwep->cursed)
		    pline("The %s %s to your hand!",
			aobjnam(uwep, "weld"),
			(uwep->quan == 1) ? "itself" : "themselves"); /* a3 */
		else prinv(uwep);
	}
	return(res);
}

void
corrode_weapon(void)
{
	if(!uwep || uwep->olet != WEAPON_SYM) return;	/* %% */
	if(uwep->rustfree)
		pline("Your %s not affected.", aobjnam(uwep, "are"));
	else {
		pline("Your %s!", aobjnam(uwep, "corrode"));
		uwep->spe--;
	}
}

int
chwepon(struct obj *otmp, int amount)
{
	char *color = (amount < 0) ? "black" : "green";
	char *time;

	if(!uwep || uwep->olet != WEAPON_SYM) {
		strange_feeling(otmp,
			(amount > 0) ? "Your hands twitch."
				     : "Your hands itch.");
		return(0);
	}

	if(uwep->otyp == WORM_TOOTH && amount > 0) {
		uwep->otyp = CRYSKNIFE;
		pline("Your weapon seems sharper now.");
		uwep->cursed = 0;
		return(1);
	}

	if(uwep->otyp == CRYSKNIFE && amount < 0) {
		uwep->otyp = WORM_TOOTH;
		pline("Your weapon looks duller now.");
		return(1);
	}

	/* there is a (soft) upper limit to uwep->spe */
	if(amount > 0 && uwep->spe > 5 && rn2(3)) {
	    pline("Your %s violently green for a while and then evaporate%s.",
		aobjnam(uwep, "glow"), plur(uwep->quan));
	    while(uwep)		/* let all of them disappear */
				/* note: uwep->quan = 1 is nogood if unpaid */
		useup(uwep);
	    return(1);
	}
	if(!rn2(6)) amount *= 2;
	time = (amount*amount == 1) ? "moment" : "while";
	pline("Your %s %s for a %s.",
		aobjnam(uwep, "glow"), color, time);
	uwep->spe += amount;
	if(amount > 0) uwep->cursed = 0;
	return(1);
}
