/*	$OpenBSD: hack.potion.c,v 1.7 2016/01/09 18:33:15 mestre Exp $	*/

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

extern char *nomovemsg;
extern struct monst youmonst;
extern struct monst *makemon(struct permonst *, int, int);

static void ghost_from_bottle(void);

int
dodrink(void)
{
	struct obj *otmp,*objs;
	struct monst *mtmp;
	int unkn = 0, nothing = 0;

	otmp = getobj("!", "drink");
	if(!otmp) return(0);
	if(!strcmp(objects[otmp->otyp].oc_descr, "smoky") && !rn2(13)) {
		ghost_from_bottle();
		goto use_it;
	}
	switch(otmp->otyp){
	case POT_RESTORE_STRENGTH:
		unkn++;
		pline("Wow!  This makes you feel great!");
		if(u.ustr < u.ustrmax) {
			u.ustr = u.ustrmax;
			flags.botl = 1;
		}
		break;
	case POT_BOOZE:
		unkn++;
		pline("Ooph!  This tastes like liquid fire!");
		Confusion += d(3,8);
		/* the whiskey makes us feel better */
		if(u.uhp < u.uhpmax) losehp(-1, "bottle of whiskey");
		if(!rn2(4)) {
			pline("You pass out.");
			multi = -rnd(15);
			nomovemsg = "You awake with a headache.";
		}
		break;
	case POT_INVISIBILITY:
		if(Invis || See_invisible)
		  nothing++;
		else {
		  if(!Blind)
		    pline("Gee!  All of a sudden, you can't see yourself.");
		  else
		    pline("You feel rather airy."), unkn++;
		  newsym(u.ux,u.uy);
		}
		Invis += rn1(15,31);
		break;
	case POT_FRUIT_JUICE:
		pline("This tastes like fruit juice.");
		lesshungry(20);
		break;
	case POT_HEALING:
		pline("You begin to feel better.");
		flags.botl = 1;
		u.uhp += rnd(10);
		if(u.uhp > u.uhpmax)
			u.uhp = ++u.uhpmax;
		if(Blind) Blind = 1;	/* see on next move */
		if(Sick) Sick = 0;
		break;
	case POT_PARALYSIS:
		if(Levitation)
			pline("You are motionlessly suspended.");
		else
			pline("Your feet are frozen to the floor!");
		nomul(-(rn1(10,25)));
		break;
	case POT_MONSTER_DETECTION:
		if(!fmon) {
			strange_feeling(otmp, "You feel threatened.");
			return(1);
		} else {
			cls();
			for(mtmp = fmon; mtmp; mtmp = mtmp->nmon)
				if(mtmp->mx > 0)
				at(mtmp->mx,mtmp->my,mtmp->data->mlet);
			prme();
			pline("You sense the presence of monsters.");
			more();
			docrt();
		}
		break;
	case POT_OBJECT_DETECTION:
		if(!fobj) {
			strange_feeling(otmp, "You feel a pull downward.");
			return(1);
		} else {
		    for(objs = fobj; objs; objs = objs->nobj)
			if(objs->ox != u.ux || objs->oy != u.uy)
				goto outobjmap;
		    pline("You sense the presence of objects close nearby.");
		    break;
		outobjmap:
			cls();
			for(objs = fobj; objs; objs = objs->nobj)
				at(objs->ox,objs->oy,objs->olet);
			prme();
			pline("You sense the presence of objects.");
			more();
			docrt();
		}
		break;
	case POT_SICKNESS:
		pline("Yech! This stuff tastes like poison.");
		if(Poison_resistance)
    pline("(But in fact it was biologically contaminated orange juice.)");
		losestr(rn1(4,3));
		losehp(rnd(10), "contaminated potion");
		break;
	case POT_CONFUSION:
		if(!Confusion)
			pline("Huh, What?  Where am I?");
		else
			nothing++;
		Confusion += rn1(7,16);
		break;
	case POT_GAIN_STRENGTH:
		pline("Wow do you feel strong!");
		if(u.ustr >= 118) break;	/* > 118 is impossible */
		if(u.ustr > 17) u.ustr += rnd(118-u.ustr);
		else u.ustr++;
		if(u.ustr > u.ustrmax) u.ustrmax = u.ustr;
		flags.botl = 1;
		break;
	case POT_SPEED:
		if(Wounded_legs) {
			heal_legs();
			unkn++;
			break;
		}
		if(!(Fast & ~INTRINSIC))
			pline("You are suddenly moving much faster.");
		else
			pline("Your legs get new energy."), unkn++;
		Fast += rn1(10,100);
		break;
	case POT_BLINDNESS:
		if(!Blind)
			pline("A cloud of darkness falls upon you.");
		else
			nothing++;
		Blind += rn1(100,250);
		seeoff(0);
		break;
	case POT_GAIN_LEVEL: 
		pluslvl();
		break;
	case POT_EXTRA_HEALING:
		pline("You feel much better.");
		flags.botl = 1;
		u.uhp += d(2,20)+1;
		if(u.uhp > u.uhpmax)
			u.uhp = (u.uhpmax += 2);
		if(Blind) Blind = 1;
		if(Sick) Sick = 0;
		break;
	case POT_LEVITATION:
		if(!Levitation)
			float_up();
		else
			nothing++;
		Levitation += rnd(100);
		u.uprops[PROP(RIN_LEVITATION)].p_tofn = float_down;
		break;
	default:
		impossible("What a funny potion! (%u)", otmp->otyp);
		return(0);
	}
	if(nothing) {
	    unkn++;
	    pline("You have a peculiar feeling for a moment, then it passes.");
	}
	if(otmp->dknown && !objects[otmp->otyp].oc_name_known) {
		if(!unkn) {
			objects[otmp->otyp].oc_name_known = 1;
			more_experienced(0,10);
		} else if(!objects[otmp->otyp].oc_uname)
			docall(otmp);
	}
use_it:
	useup(otmp);
	return(1);
}

void
pluslvl(void)
{
	int num;

	pline("You feel more experienced.");
	num = rnd(10);
	u.uhpmax += num;
	u.uhp += num;
	if(u.ulevel < 14) {
		u.uexp = newuexp()+1;
		pline("Welcome to experience level %u.", ++u.ulevel);
	}
	flags.botl = 1;
}

void
strange_feeling(struct obj *obj, char *txt)
{
	if(flags.beginner)
	    pline("You have a strange feeling for a moment, then it passes.");
	else
	    pline("%s", txt);
	if(!objects[obj->otyp].oc_name_known && !objects[obj->otyp].oc_uname)
		docall(obj);
	useup(obj);
}

char *bottlenames[] = {
	"bottle", "phial", "flagon", "carafe", "flask", "jar", "vial"
};

void
potionhit(struct monst *mon, struct obj *obj)
{
	char *botlnam = bottlenames[rn2(SIZE(bottlenames))];
	boolean uclose, isyou = (mon == &youmonst);

	if(isyou) {
		uclose = TRUE;
		pline("The %s crashes on your head and breaks into shivers.",
			botlnam);
		losehp(rnd(2), "thrown potion");
	} else {
		uclose = (dist(mon->mx,mon->my) < 3);
		/* perhaps 'E' and 'a' have no head? */
		pline("The %s crashes on %s's head and breaks into shivers.",
			botlnam, monnam(mon));
		if(rn2(5) && mon->mhp > 1)
			mon->mhp--;
	}
	pline("The %s evaporates.", xname(obj));

	if(!isyou && !rn2(3)) switch(obj->otyp) {

	case POT_RESTORE_STRENGTH:
	case POT_GAIN_STRENGTH:
	case POT_HEALING:
	case POT_EXTRA_HEALING:
		if(mon->mhp < mon->mhpmax) {
			mon->mhp = mon->mhpmax;
			pline("%s looks sound and hale again!", Monnam(mon));
		}
		break;
	case POT_SICKNESS:
		if(mon->mhpmax > 3)
			mon->mhpmax /= 2;
		if(mon->mhp > 2)
			mon->mhp /= 2;
		break;
	case POT_CONFUSION:
	case POT_BOOZE:
		mon->mconf = 1;
		break;
	case POT_INVISIBILITY:
		unpmon(mon);
		mon->minvis = 1;
		pmon(mon);
		break;
	case POT_PARALYSIS:
		mon->mfroz = 1;
		break;
	case POT_SPEED:
		mon->mspeed = MFAST;
		break;
	case POT_BLINDNESS:
		mon->mblinded |= 64 + rn2(64);
		break;
/*	
	case POT_GAIN_LEVEL:
	case POT_LEVITATION:
	case POT_FRUIT_JUICE:
	case POT_MONSTER_DETECTION:
	case POT_OBJECT_DETECTION:
		break;
*/
	}
	if(uclose && rn2(5))
		potionbreathe(obj);
	obfree(obj, Null(obj));
}

void
potionbreathe(struct obj *obj)
{
	switch(obj->otyp) {
	case POT_RESTORE_STRENGTH:
	case POT_GAIN_STRENGTH:
		if(u.ustr < u.ustrmax) u.ustr++, flags.botl = 1;
		break;
	case POT_HEALING:
	case POT_EXTRA_HEALING:
		if(u.uhp < u.uhpmax) u.uhp++, flags.botl = 1;
		break;
	case POT_SICKNESS:
		if(u.uhp <= 5) u.uhp = 1; else u.uhp -= 5;
		flags.botl = 1;
		break;
	case POT_CONFUSION:
	case POT_BOOZE:
		if(!Confusion)
			pline("You feel somewhat dizzy.");
		Confusion += rnd(5);
		break;
	case POT_INVISIBILITY:
		pline("For an instant you couldn't see your right hand.");
		break;
	case POT_PARALYSIS:
		pline("Something seems to be holding you.");
		nomul(-rnd(5));
		break;
	case POT_SPEED:
		Fast += rnd(5);
		pline("Your knees seem more flexible now.");
		break;
	case POT_BLINDNESS:
		if(!Blind) pline("It suddenly gets dark.");
		Blind += rnd(5);
		seeoff(0);
		break;
/*	
	case POT_GAIN_LEVEL:
	case POT_LEVITATION:
	case POT_FRUIT_JUICE:
	case POT_MONSTER_DETECTION:
	case POT_OBJECT_DETECTION:
		break;
*/
	}
	/* note: no obfree() */
}

/*
 * -- rudimentary -- to do this correctly requires much more work
 * -- all sharp weapons get one or more qualities derived from the potions
 * -- texts on scrolls may be (partially) wiped out; do they become blank?
 * --   or does their effect change, like under Confusion?
 * -- all objects may be made invisible by POT_INVISIBILITY
 * -- If the flask is small, can one dip a large object? Does it magically
 * --   become a jug? Etc.
 */
int
dodip(void)
{
	struct obj *potion, *obj;

	if(!(obj = getobj("#", "dip")))
		return(0);
	if(!(potion = getobj("!", "dip into")))
		return(0);
	pline("Interesting...");
	if(obj->otyp == ARROW || obj->otyp == DART ||
	   obj->otyp == CROSSBOW_BOLT) {
		if(potion->otyp == POT_SICKNESS) {
			useup(potion);
			if(obj->spe < 7) obj->spe++;	/* %% */
		}
	}
	return(1);
}

static void
ghost_from_bottle(void)
{
	extern struct permonst pm_ghost;
	struct monst *mtmp;

	if(!(mtmp = makemon(PM_GHOST,u.ux,u.uy))){
		pline("This bottle turns out to be empty.");
		return;
	}
	mnexto(mtmp);
	pline("As you open the bottle, an enormous ghost emerges!");
	pline("You are frightened to death, and unable to move.");
	nomul(-3);
}
