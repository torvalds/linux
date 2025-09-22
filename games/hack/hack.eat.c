/*	$OpenBSD: hack.eat.c,v 1.11 2016/01/09 21:54:11 mestre Exp $	*/

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

char POISONOUS[] = "ADKSVabhks";
extern char *nomovemsg;
extern void (*afternmv)(void);
extern int (*occupation)(void);
extern char *occtxt;

/* hunger texts used on bottom line (each 8 chars long) */
#define	SATIATED	0
#define NOT_HUNGRY	1
#define	HUNGRY		2
#define	WEAK		3
#define	FAINTING	4
#define FAINTED		5
#define STARVED		6

char *hu_stat[] = {
	"Satiated",
	"        ",
	"Hungry  ",
	"Weak    ",
	"Fainting",
	"Fainted ",
	"Starved "
};

#define	TTSZ	SIZE(tintxts)
struct {
	char *txt;
	int nut;
} tintxts[] = {
	{ "It contains first quality peaches - what a surprise!",	40 },
	{ "It contains salmon - not bad!",	60 },
	{ "It contains apple juice - perhaps not what you hoped for.", 20 },
	{ "It contains some nondescript substance, tasting awfully.", 500 },
	{ "It contains rotten meat. You vomit.", -50 },
	{ "It turns out to be empty.",	0 }
};

static struct {
	struct obj *tin;
	int usedtime, reqtime;
} tin;

static void newuhs(boolean);
static int  eatcorpse(struct obj *);

void
init_uhunger(void)
{
	u.uhunger = 900;
	u.uhs = NOT_HUNGRY;
}

int
opentin(void)
{
	int r;

	if(!carried(tin.tin))		/* perhaps it was stolen? */
		return(0);		/* %% probably we should use tinoid */
	if(tin.usedtime++ >= 50) {
		pline("You give up your attempt to open the tin.");
		return(0);
	}
	if(tin.usedtime < tin.reqtime)
		return(1);		/* still busy */

	pline("You succeed in opening the tin.");
	useup(tin.tin);
	r = rn2(2*TTSZ);
	if(r < TTSZ){
	    pline("%s", tintxts[r].txt);
	    lesshungry(tintxts[r].nut);
	    if(r == 1)	/* SALMON */ {
		Glib = rnd(15);
		pline("Eating salmon made your fingers very slippery.");
	    }
	} else {
	    pline("It contains spinach - this makes you feel like Popeye!");
	    lesshungry(600);
	    if(u.ustr < 118)
		u.ustr += rnd( ((u.ustr < 17) ? 19 : 118) - u.ustr);
	    if(u.ustr > u.ustrmax) u.ustrmax = u.ustr;
	    flags.botl = 1;
	}
	return(0);
}

void
Meatdone(void)
{
	u.usym = '@';
	prme();
}

int
doeat(void)
{
	struct obj *otmp;
	struct objclass *ftmp;
	int tmp;

	/* Is there some food (probably a heavy corpse) here on the ground? */
	if(!Levitation)
	for(otmp = fobj; otmp; otmp = otmp->nobj) {
		if(otmp->ox == u.ux && otmp->oy == u.uy &&
		   otmp->olet == FOOD_SYM) {
			pline("There %s %s here; eat %s? [ny] ",
				(otmp->quan == 1) ? "is" : "are",
				doname(otmp),
				(otmp->quan == 1) ? "it" : "one");
			if(readchar() == 'y') {
				if(otmp->quan != 1)
					(void) splitobj(otmp, 1);
				freeobj(otmp);
				otmp = addinv(otmp);
				addtobill(otmp);
				goto gotit;
			}
		}
	}
	otmp = getobj("%", "eat");
	if(!otmp) return(0);
gotit:
	if(otmp->otyp == TIN){
		if(uwep) {
			switch(uwep->otyp) {
			case CAN_OPENER:
				tmp = 1;
				break;
			case DAGGER:
			case CRYSKNIFE:
				tmp = 3;
				break;
			case PICK_AXE:
			case AXE:
				tmp = 6;
				break;
			default:
				goto no_opener;
			}
			pline("Using your %s you try to open the tin.",
				aobjnam(uwep, NULL));
		} else {
		no_opener:
			pline("It is not so easy to open this tin.");
			if(Glib) {
				pline("The tin slips out of your hands.");
				if(otmp->quan > 1) {
					struct obj *obj;

					obj = splitobj(otmp, 1);
					if(otmp == uwep) setuwep(obj);
				}
				dropx(otmp);
				return(1);
			}
			tmp = 10 + rn2(1 + 500/((int)(u.ulevel + u.ustr)));
		}
		tin.reqtime = tmp;
		tin.usedtime = 0;
		tin.tin = otmp;
		occupation = opentin;
		occtxt = "opening the tin";
		return(1);
	}
	ftmp = &objects[otmp->otyp];
	multi = -ftmp->oc_delay;
	if(otmp->otyp >= CORPSE && eatcorpse(otmp)) goto eatx;
	if(!rn2(7) && otmp->otyp != FORTUNE_COOKIE) {
		pline("Blecch!  Rotten food!");
		if(!rn2(4)) {
			pline("You feel rather light headed.");
			Confusion += d(2,4);
		} else if(!rn2(4)&& !Blind) {
			pline("Everything suddenly goes dark.");
			Blind = d(2,10);
			seeoff(0);
		} else if(!rn2(3)) {
			if(Blind)
			  pline("The world spins and you slap against the floor.");
			else
			  pline("The world spins and goes dark.");
			nomul(-rnd(10));
			nomovemsg = "You are conscious again.";
		}
		lesshungry(ftmp->nutrition / 4);
	} else {
		if(u.uhunger >= 1500) {
			pline("You choke over your food.");
			pline("You die...");
			killer = ftmp->oc_name;
			done("choked");
		}
		switch(otmp->otyp){
		case FOOD_RATION:
			if(u.uhunger <= 200)
				pline("That food really hit the spot!");
			else if(u.uhunger <= 700)
				pline("That satiated your stomach!");
			else {
	pline("You're having a hard time getting all that food down.");
				multi -= 2;
			}
			lesshungry(ftmp->nutrition);
			if(multi < 0) nomovemsg = "You finished your meal.";
			break;
		case TRIPE_RATION:
			pline("Yak - dog food!");
			more_experienced(1,0);
			flags.botl = 1;
			if(rn2(2)){
				pline("You vomit.");
				morehungry(20);
				if(Sick) {
					Sick = 0;	/* David Neves */
					pline("What a relief!");
				}
			} else	lesshungry(ftmp->nutrition);
			break;
		default:
			if(otmp->otyp >= CORPSE)
			pline("That %s tasted terrible!",ftmp->oc_name);
			else
			pline("That %s was delicious!",ftmp->oc_name);
			lesshungry(ftmp->nutrition);
			if(otmp->otyp == DEAD_LIZARD && (Confusion > 2))
				Confusion = 2;
			else
#ifdef QUEST
			if(otmp->otyp == CARROT && !Blind){
				u.uhorizon++;
				setsee();
				pline("Your vision improves.");
			} else
#endif /* QUEST */
			if(otmp->otyp == FORTUNE_COOKIE) {
			  if(Blind) {
			    pline("This cookie has a scrap of paper inside!");
			    pline("What a pity, that you cannot read it!");
			  } else
			    outrumor();
			} else
			if(otmp->otyp == LUMP_OF_ROYAL_JELLY) {
				/* This stuff seems to be VERY healthy! */
				if(u.ustrmax < 118) u.ustrmax++;
				if(u.ustr < u.ustrmax) u.ustr++;
				u.uhp += rnd(20);
				if(u.uhp > u.uhpmax) {
					if(!rn2(17)) u.uhpmax++;
					u.uhp = u.uhpmax;
				}
				heal_legs();
			}
			break;
		}
	}
eatx:
	if(multi<0 && !nomovemsg){
		static char msgbuf[BUFSZ];
		(void) snprintf(msgbuf, sizeof msgbuf,
				"You finished eating the %s.",
				ftmp->oc_name);
		nomovemsg = msgbuf;
	}
	useup(otmp);
	return(1);
}

void
gethungry(void)
{
	--u.uhunger;
	if(moves % 2) {
		if(Regeneration) u.uhunger--;
		if(Hunger) u.uhunger--;
		/* a3:  if(Hunger & LEFT_RING) u.uhunger--;
			if(Hunger & RIGHT_RING) u.uhunger--;
		   etc. */
	}
	if(moves % 20 == 0) {			/* jimt@asgb */
		if(uleft) u.uhunger--;
		if(uright) u.uhunger--;
	}
	newuhs(TRUE);
}

/* called after vomiting and after performing feats of magic */
void
morehungry(int num)
{
	u.uhunger -= num;
	newuhs(TRUE);
}

/* called after eating something (and after drinking fruit juice) */
void
lesshungry(int num)
{
	u.uhunger += num;
	newuhs(FALSE);
}

void
unfaint(void)
{
	u.uhs = FAINTING;
	flags.botl = 1;
}

static void
newuhs(boolean incr)
{
	int newhs, h = u.uhunger;

	newhs = (h > 1000) ? SATIATED :
		(h > 150) ? NOT_HUNGRY :
		(h > 50) ? HUNGRY :
		(h > 0) ? WEAK : FAINTING;

	if(newhs == FAINTING) {
		if(u.uhs == FAINTED)
			newhs = FAINTED;
		if(u.uhs <= WEAK || rn2(20-u.uhunger/10) >= 19) {
			if(u.uhs != FAINTED && multi >= 0 /* %% */) {
				pline("You faint from lack of food.");
				nomul(-10+(u.uhunger/10));
				nomovemsg = "You regain consciousness.";
				afternmv = unfaint;
				newhs = FAINTED;
			}
		} else
		if(u.uhunger < -(int)(200 + 25*u.ulevel)) {
			u.uhs = STARVED;
			flags.botl = 1;
			bot();
			pline("You die from starvation.");
			done("starved");
		}
	}

	if(newhs != u.uhs) {
		if(newhs >= WEAK && u.uhs < WEAK)
			losestr(1);	/* this may kill you -- see below */
		else
		if(newhs < WEAK && u.uhs >= WEAK && u.ustr < u.ustrmax)
			losestr(-1);
		switch(newhs){
		case HUNGRY:
			pline((!incr) ? "You only feel hungry now." :
			      (u.uhunger < 145) ? "You feel hungry." :
				"You are beginning to feel hungry.");
			break;
		case WEAK:
			pline((!incr) ? "You feel weak now." :
			      (u.uhunger < 45) ? "You feel weak." :
				"You are beginning to feel weak.");
			break;
		}
		u.uhs = newhs;
		flags.botl = 1;
		if(u.uhp < 1) {
			pline("You die from hunger and exhaustion.");
			killer = "exhaustion";
			done("starved");
		}
	}
}

#define	CORPSE_I_TO_C(otyp)	(char) ((otyp >= DEAD_ACID_BLOB)\
		     ?  'a' + (otyp - DEAD_ACID_BLOB)\
		     :	'@' + (otyp - DEAD_HUMAN))
int
poisonous(struct obj *otmp)
{
	return(strchr(POISONOUS, CORPSE_I_TO_C(otmp->otyp)) != 0);
}

/* returns 1 if some text was printed */
static int
eatcorpse(struct obj *otmp)
{
	char let = CORPSE_I_TO_C(otmp->otyp);
	int tp = 0;

	if(let != 'a' && moves > otmp->age + 50 + rn2(100)) {
		tp++;
		pline("Ulch -- that meat was tainted!");
		pline("You get very sick.");
		Sick = 10 + rn2(10);
		u.usick_cause = objects[otmp->otyp].oc_name;
	} else if(strchr(POISONOUS, let) && rn2(5)){
		tp++;
		pline("Ecch -- that must have been poisonous!");
		if(!Poison_resistance){
			losestr(rnd(4));
			losehp(rnd(15), "poisonous corpse");
		} else
			pline("You don't seem affected by the poison.");
	} else if(strchr("ELNOPQRUuxz", let) && rn2(5)){
		tp++;
		pline("You feel sick.");
		losehp(rnd(8), "cadaver");
	}
	switch(let) {
	case 'L':
	case 'N':
	case 't':
		Teleportation |= INTRINSIC;
		break;
	case 'W':
		pluslvl();
		break;
	case 'n':
		u.uhp = u.uhpmax;
		flags.botl = 1;
		/* fall into next case */
	case '@':
		pline("You cannibal! You will be sorry for this!");
		/* not tp++; */
		/* fall into next case */
	case 'd':
		Aggravate_monster |= INTRINSIC;
		break;
	case 'I':
		if(!Invis) {
			Invis = 50+rn2(100);
			if(!See_invisible)
				newsym(u.ux, u.uy);
		} else {
			Invis |= INTRINSIC;
			See_invisible |= INTRINSIC;
		}
		/* fall into next case */
	case 'y':
#ifdef QUEST
		u.uhorizon++;
#endif /* QUEST */
		/* fall into next case */
	case 'B':
		Confusion = 50;
		break;
	case 'D':
		Fire_resistance |= INTRINSIC;
		break;
	case 'E':
		Telepat |= INTRINSIC;
		break;
	case 'F':
	case 'Y':
		Cold_resistance |= INTRINSIC;
		break;
	case 'k':
	case 's':
		Poison_resistance |= INTRINSIC;
		break;
	case 'c':
		pline("You turn to stone.");
		killer = "dead cockatrice";
		done("died");
	case 'a':
	  if(Stoned) {
	      pline("What a pity - you just destroyed a future piece of art!");
	      tp++;
	      Stoned = 0;
	  }
	  break;
	case 'M':
	  pline("You cannot resist the temptation to mimic a treasure chest.");
	  tp++;
	  nomul(-30);
	  afternmv = Meatdone;
	  nomovemsg = "You now again prefer mimicking a human.";
	  u.usym = '$';
	  prme();
	  break;
	}
	return(tp);
}
