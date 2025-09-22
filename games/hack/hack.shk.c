/*	$OpenBSD: hack.shk.c,v 1.13 2016/01/09 18:33:15 mestre Exp $	*/

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
#include <stdlib.h>

#include "hack.h"

#ifdef QUEST
int shlevel = 0;
struct monst *shopkeeper = 0;
struct obj *billobjs = 0;

void
obfree(struct obj *obj, struct obj *merge)
{
	free(obj);
}

int
inshop(void)
{
	return(0);
}

void
shopdig(int a)
{}

void
addtobill(struct obj *ign)
{}

void
subfrombill(struct obj *ign)
{}

void
splitbill(struct obj *ign, struct obj *ign2)
{}

int
dopay(void)
{
	return(0);
}

void
paybill(void)
{}

int
doinvbill(int a)
{
	return(0);
}

void
shkdead(struct monst *ign)
{}

int
shkcatch(struct obj *ign)
{
	return(0);
}

int
shk_move(struct monst *ign)
{
	return(0);
}

void
replshk(struct monst *mtmp, struct monst *mtmp2)
{}

char *
shkname(struct monst *ign)
{
	return("");
}

#else /* QUEST */
#include "hack.mfndpos.h"
#include "def.eshk.h"

#define	ESHK(mon)	((struct eshk *)(&(mon->mextra[0])))
#define	NOTANGRY(mon)	mon->mpeaceful
#define	ANGRY(mon)	!NOTANGRY(mon)

extern char plname[];

/* Descriptor of current shopkeeper. Note that the bill need not be
 * per-shopkeeper, since it is valid only when in a shop.
 */
static struct monst *shopkeeper = 0;
static struct bill_x *bill;
static int shlevel = 0;	/* level of this shopkeeper */
struct obj *billobjs;	/* objects on bill with bp->useup */
				/* only accessed here and by save & restore */
static long int total;		/* filled by addupbill() */
static long int followmsg;	/* last time of follow message */

/*
 *	invariants: obj->unpaid iff onbill(obj) [unless bp->useup]
 *		obj->quan <= bp->bquan
 */


char shtypes[] = {	/* 8 shoptypes: 7 specialized, 1 mixed */
	RING_SYM, WAND_SYM, WEAPON_SYM, FOOD_SYM, SCROLL_SYM,
	POTION_SYM, ARMOR_SYM, 0
};

static char *shopnam[] = {
	"engagement ring", "walking cane", "antique weapon",
	"delicatessen", "second hand book", "liquor",
	"used armor", "assorted antiques"
};

static void setpaid(void);
static void addupbill(void);
static void findshk(int);
static struct bill_x *onbill(struct obj *);
static void pay(long, struct monst *);
static int  dopayobj(struct bill_x *);
static struct obj *bp_to_obj(struct bill_x *);
static int getprice(struct obj *);
static int realhunger(void);

/* called in do_name.c */
char *
shkname(struct monst *mtmp)
{
	return(ESHK(mtmp)->shknam);
}

void
shkdead(struct monst *mtmp)				/* called in mon.c */
{
	struct eshk *eshk = ESHK(mtmp);

	if(eshk->shoplevel == dlevel)
		rooms[eshk->shoproom].rtype = 0;
	if(mtmp == shopkeeper) {
		setpaid();
		shopkeeper = 0;
		bill = (struct bill_x *) -1000;	/* dump core when referenced */
	}
}

void
replshk(struct monst *mtmp, struct monst *mtmp2)
{
	if(mtmp == shopkeeper) {
		shopkeeper = mtmp2;
		bill = &(ESHK(shopkeeper)->bill[0]);
	}
}

/* caller has checked that shopkeeper exists */
/* either we paid or left the shop or he just died */
static void
setpaid(void)
{
	struct obj *obj;
	struct monst *mtmp;

	for(obj = invent; obj; obj = obj->nobj)
		obj->unpaid = 0;
	for(obj = fobj; obj; obj = obj->nobj)
		obj->unpaid = 0;
	for(obj = fcobj; obj; obj = obj->nobj)
		obj->unpaid = 0;
	for(mtmp = fmon; mtmp; mtmp = mtmp->nmon)
		for(obj = mtmp->minvent; obj; obj = obj->nobj)
			obj->unpaid = 0;
	for(mtmp = fallen_down; mtmp; mtmp = mtmp->nmon)
		for(obj = mtmp->minvent; obj; obj = obj->nobj)
			obj->unpaid = 0;
	while ((obj = billobjs)) {
		billobjs = obj->nobj;
		free(obj);
	}
	ESHK(shopkeeper)->billct = 0;
}

/* delivers result in total */
/* caller has checked that shopkeeper exists */
static void
addupbill(void)
{
	int ct = ESHK(shopkeeper)->billct;
	struct bill_x *bp = bill;

	total = 0;
	while(ct--){
		total += bp->price * bp->bquan;
		bp++;
	}
}

int
inshop(void)
{
	int roomno = inroom(u.ux,u.uy);

	/* Did we just leave a shop? */
	if(u.uinshop &&
	    (u.uinshop != roomno + 1 || shlevel != dlevel || !shopkeeper)) {
		if(shopkeeper) {
		    if(ESHK(shopkeeper)->billct) {
 			if(inroom(shopkeeper->mx, shopkeeper->my) 
 			    == u.uinshop - 1)	/* ab@unido */
 			    pline("Somehow you escaped the shop without paying!");
			addupbill();
			pline("You stole for a total worth of %ld zorkmids.",
				total);
			ESHK(shopkeeper)->robbed += total;
			setpaid();
			if((rooms[ESHK(shopkeeper)->shoproom].rtype == GENERAL)
			    == (rn2(3) == 0))
			    ESHK(shopkeeper)->following = 1;
		    }
		    shopkeeper = 0;
		    shlevel = 0;
		}
 		u.uinshop = 0;
	}

	/* Did we just enter a zoo of some kind? */
	if(roomno >= 0) {
		int rt = rooms[roomno].rtype;
		struct monst *mtmp;
		if(rt == ZOO) {
			pline("Welcome to David's treasure zoo!");
		} else
		if(rt == SWAMP) {
			pline("It looks rather muddy down here.");
		} else
		if(rt == MORGUE) {
			if(midnight())
				pline("Go away! Go away!");
			else
				pline("You get an uncanny feeling ...");
		} else
			rt = 0;
		if(rt != 0) {
			rooms[roomno].rtype = 0;
			for(mtmp = fmon; mtmp; mtmp = mtmp->nmon)
				if(rt != ZOO || !rn2(3))
					mtmp->msleep = 0;
		}
	}

	/* Did we just enter a shop? */
	if(roomno >= 0 && rooms[roomno].rtype >= 8) {
	    if(shlevel != dlevel || !shopkeeper
				 || ESHK(shopkeeper)->shoproom != roomno)
		findshk(roomno);
	    if(!shopkeeper) {
		rooms[roomno].rtype = 0;
		u.uinshop = 0;
	    } else if(!u.uinshop){
		if(!ESHK(shopkeeper)->visitct ||
		    strncmp(ESHK(shopkeeper)->customer, plname, PL_NSIZ)){

		    /* He seems to be new here */
		    ESHK(shopkeeper)->visitct = 0;
		    ESHK(shopkeeper)->following = 0;
		    (void) strlcpy(ESHK(shopkeeper)->customer,plname,PL_NSIZ);
		    NOTANGRY(shopkeeper) = 1;
		}
		if(!ESHK(shopkeeper)->following) {
		    boolean box, pick;

		    pline("Hello %s! Welcome%s to %s's %s shop!",
			plname,
			ESHK(shopkeeper)->visitct++ ? " again" : "",
			shkname(shopkeeper),
			shopnam[rooms[ESHK(shopkeeper)->shoproom].rtype - 8] );
		    box = carrying(ICE_BOX);
		    pick = carrying(PICK_AXE);
		    if(box || pick) {
			if(dochug(shopkeeper)) {
				u.uinshop = 0;	/* he died moving */
				return(0);
			}
			pline("Will you please leave your %s outside?",
			    (box && pick) ? "box and pick-axe" :
			    box ? "box" : "pick-axe");
		    }
		}
		u.uinshop = roomno + 1;
	    }
	}
	return(u.uinshop);
}

static void
findshk(int roomno)
{
	struct monst *mtmp;

	for(mtmp = fmon; mtmp; mtmp = mtmp->nmon)
	    if(mtmp->isshk && ESHK(mtmp)->shoproom == roomno
			   && ESHK(mtmp)->shoplevel == dlevel) {
		shopkeeper = mtmp;
		bill = &(ESHK(shopkeeper)->bill[0]);
		shlevel = dlevel;
		if(ANGRY(shopkeeper) &&
		   strncmp(ESHK(shopkeeper)->customer,plname,PL_NSIZ))
			NOTANGRY(shopkeeper) = 1;
		/* billobjs = 0; -- this is wrong if we save in a shop */
		/* (and it is harmless to have too many things in billobjs) */
		return;
	}
	shopkeeper = 0;
	shlevel = 0;
	bill = (struct bill_x *) -1000;	/* dump core when referenced */
}

static struct bill_x *
onbill(struct obj *obj)
{
	struct bill_x *bp;

	if(!shopkeeper) return(NULL);
	for(bp = bill; bp < &bill[ESHK(shopkeeper)->billct]; bp++)
		if(bp->bo_id == obj->o_id) {
			if(!obj->unpaid) pline("onbill: paid obj on bill?");
			return(bp);
		}
	if(obj->unpaid) pline("onbill: unpaid obj not on bill?");
	return(NULL);
}

/* called with two args on merge */
void
obfree(struct obj *obj, struct obj *merge)
{
	struct bill_x *bp = onbill(obj);
	struct bill_x *bpm;

	if(bp) {
		if(!merge){
			bp->useup = 1;
			obj->unpaid = 0;	/* only for doinvbill */
			obj->nobj = billobjs;
			billobjs = obj;
			return;
		}
		bpm = onbill(merge);
		if(!bpm){
			/* this used to be a rename */
			impossible("obfree: not on bill??");
			return;
		} else {
			/* this was a merger */
			bpm->bquan += bp->bquan;
			ESHK(shopkeeper)->billct--;
			*bp = bill[ESHK(shopkeeper)->billct];
		}
	}
	free(obj);
}

static void
pay(long tmp, struct monst *shkp)
{
	long robbed = ESHK(shkp)->robbed;

	u.ugold -= tmp;
	shkp->mgold += tmp;
	flags.botl = 1;
	if(robbed) {
		robbed -= tmp;
		if(robbed < 0) robbed = 0;
		ESHK(shkp)->robbed = robbed;
	}
}

int
dopay(void)
{
	long ltmp;
	struct bill_x *bp;
	struct monst *shkp;
	int pass, tmp;

	multi = 0;
	(void) inshop();
	for(shkp = fmon; shkp; shkp = shkp->nmon)
		if(shkp->isshk && dist(shkp->mx,shkp->my) < 3)
			break;
	if(!shkp && u.uinshop &&
	   inroom(shopkeeper->mx,shopkeeper->my) == ESHK(shopkeeper)->shoproom)
		shkp = shopkeeper;

	if(!shkp) {
		pline("There is nobody here to receive your payment.");
		return(0);
	}
	ltmp = ESHK(shkp)->robbed;
	if(shkp != shopkeeper && NOTANGRY(shkp)) {
		if(!ltmp) {
			pline("You do not owe %s anything.", monnam(shkp));
		} else
		if(!u.ugold) {
			pline("You have no money.");
		} else {
		    long ugold = u.ugold;

		    if(u.ugold > ltmp) {
			pline("You give %s the %ld gold pieces he asked for.",
				monnam(shkp), ltmp);
			pay(ltmp, shkp);
		    } else {
			pline("You give %s all your gold.", monnam(shkp));
			pay(u.ugold, shkp);
		    }
		    if(ugold < ltmp/2) {
			pline("Unfortunately, he doesn't look satisfied.");
		    } else {
			ESHK(shkp)->robbed = 0;
			ESHK(shkp)->following = 0;
			if(ESHK(shkp)->shoplevel != dlevel) {
			/* For convenience's sake, let him disappear */
			    shkp->minvent = 0;		/* %% */
			    shkp->mgold = 0;
			    mondead(shkp);
			}
		    }
		}
		return(1);
	}
		
	if(!ESHK(shkp)->billct){
		pline("You do not owe %s anything.", monnam(shkp));
		if(!u.ugold){
			pline("Moreover, you have no money.");
			return(1);
		}
		if(ESHK(shkp)->robbed){
#define min(a,b)	((a<b)?a:b)
		    pline("But since his shop has been robbed recently,");
		    pline("you %srepay %s's expenses.",
		      (u.ugold < ESHK(shkp)->robbed) ? "partially " : "",
		      monnam(shkp));
		    pay(min(u.ugold, ESHK(shkp)->robbed), shkp);
		    ESHK(shkp)->robbed = 0;
		    return(1);
		}
		if(ANGRY(shkp)){
			pline("But in order to appease %s,",
				amonnam(shkp, "angry"));
			if(u.ugold >= 1000){
				ltmp = 1000;
				pline(" you give him 1000 gold pieces.");
			} else {
				ltmp = u.ugold;
				pline(" you give him all your money.");
			}
			pay(ltmp, shkp);
			if(strncmp(ESHK(shkp)->customer, plname, PL_NSIZ)
			   || rn2(3)){
				pline("%s calms down.", Monnam(shkp));
				NOTANGRY(shkp) = 1;
			} else	pline("%s is as angry as ever.",
					Monnam(shkp));
		}
		return(1);
	}
	if(shkp != shopkeeper) {
		impossible("dopay: not to shopkeeper?");
		if(shopkeeper) setpaid();
		return(0);
	}
	for(pass = 0; pass <= 1; pass++) {
		tmp = 0;
		while(tmp < ESHK(shopkeeper)->billct) {
			bp = &bill[tmp];
			if(!pass && !bp->useup) {
				tmp++;
				continue;
			}
			if(!dopayobj(bp)) return(1);
			bill[tmp] = bill[--ESHK(shopkeeper)->billct];
		}
	}
	pline("Thank you for shopping in %s's %s store!",
		shkname(shopkeeper),
		shopnam[rooms[ESHK(shopkeeper)->shoproom].rtype - 8]);
	NOTANGRY(shopkeeper) = 1;
	return(1);
}

/* return 1 if paid successfully */
/*	  0 if not enough money */
/*	 -1 if object could not be found (but was paid) */
static int
dopayobj(struct bill_x *bp)
{
	struct obj *obj;
	long ltmp;

	/* find the object on one of the lists */
	obj = bp_to_obj(bp);

	if(!obj) {
		impossible("Shopkeeper administration out of order.");
		setpaid();	/* be nice to the player */
		return(0);
	}

	if(!obj->unpaid && !bp->useup){
		impossible("Paid object on bill??");
		return(1);
	}
	obj->unpaid = 0;
	ltmp = bp->price * bp->bquan;
	if(ANGRY(shopkeeper)) ltmp += ltmp/3;
	if(u.ugold < ltmp){
		pline("You don't have gold enough to pay %s.",
			doname(obj));
		obj->unpaid = 1;
		return(0);
	}
	pay(ltmp, shopkeeper);
	pline("You bought %s for %ld gold piece%s.",
		doname(obj), ltmp, plur(ltmp));
	if(bp->useup) {
		struct obj *otmp = billobjs;
		if(obj == billobjs)
			billobjs = obj->nobj;
		else {
			while(otmp && otmp->nobj != obj) otmp = otmp->nobj;
			if(otmp) otmp->nobj = obj->nobj;
			else pline("Error in shopkeeper administration.");
		}
		free(obj);
	}
	return(1);
}

/* routine called after dying (or quitting) with nonempty bill */
void
paybill(void)
{
	if(shlevel == dlevel && shopkeeper && ESHK(shopkeeper)->billct){
		addupbill();
		if(total > u.ugold){
			shopkeeper->mgold += u.ugold;
			u.ugold = 0;
		pline("%s comes and takes all your possessions.",
			Monnam(shopkeeper));
		} else {
			u.ugold -= total;
			shopkeeper->mgold += total;
	pline("%s comes and takes the %ld zorkmids you owed him.",
		Monnam(shopkeeper), total);
		}
		setpaid();	/* in case we create bones */
	}
}

/* find obj on one of the lists */
static struct obj *
bp_to_obj(struct bill_x *bp)
{
	struct obj *obj;
	struct monst *mtmp;
	unsigned id = bp->bo_id;

	if(bp->useup)
		obj = o_on(id, billobjs);
	else if(!(obj = o_on(id, invent)) &&
		!(obj = o_on(id, fobj)) &&
		!(obj = o_on(id, fcobj))) {
		    for(mtmp = fmon; mtmp; mtmp = mtmp->nmon)
			if ((obj = o_on(id, mtmp->minvent)))
			    break;
		    for(mtmp = fallen_down; mtmp; mtmp = mtmp->nmon)
			if ((obj = o_on(id, mtmp->minvent)))
			    break;
		}
	return(obj);
}

/* called in hack.c when we pickup an object */
void
addtobill(struct obj *obj)
{
	struct bill_x *bp;

	if(!inshop() ||
	(u.ux == ESHK(shopkeeper)->shk.x && u.uy == ESHK(shopkeeper)->shk.y) ||
	(u.ux == ESHK(shopkeeper)->shd.x && u.uy == ESHK(shopkeeper)->shd.y) ||
		onbill(obj) /* perhaps we threw it away earlier */
	  ) return;
	if(ESHK(shopkeeper)->billct == BILLSZ){
		pline("You got that for free!");
		return;
	}
	bp = &bill[ESHK(shopkeeper)->billct];
	bp->bo_id = obj->o_id;
	bp->bquan = obj->quan;
	bp->useup = 0;
	bp->price = getprice(obj);
	ESHK(shopkeeper)->billct++;
	obj->unpaid = 1;
}

void
splitbill(struct obj *obj, struct obj *otmp)
{
	/* otmp has been split off from obj */
struct bill_x *bp;
int tmp;
	bp = onbill(obj);
	if(!bp) {
		impossible("splitbill: not on bill?");
		return;
	}
	if(bp->bquan < otmp->quan) {
		impossible("Negative quantity on bill??");
	}
	if(bp->bquan == otmp->quan) {
		impossible("Zero quantity on bill??");
	}
	bp->bquan -= otmp->quan;

	/* addtobill(otmp); */
	if(ESHK(shopkeeper)->billct == BILLSZ) otmp->unpaid = 0;
	else {
		tmp = bp->price;
		bp = &bill[ESHK(shopkeeper)->billct];
		bp->bo_id = otmp->o_id;
		bp->bquan = otmp->quan;
		bp->useup = 0;
		bp->price = tmp;
		ESHK(shopkeeper)->billct++;
	}
}

void
subfrombill(struct obj *obj)
{
	long ltmp;
	int tmp;
	struct obj *otmp;
	struct bill_x *bp;

	if(!inshop() || (u.ux == ESHK(shopkeeper)->shk.x && u.uy == ESHK(shopkeeper)->shk.y) ||
		(u.ux == ESHK(shopkeeper)->shd.x && u.uy == ESHK(shopkeeper)->shd.y))
		return;
	if((bp = onbill(obj)) != 0){
		obj->unpaid = 0;
		if(bp->bquan > obj->quan){
			otmp = newobj(0);
			*otmp = *obj;
			bp->bo_id = otmp->o_id = flags.ident++;
			otmp->quan = (bp->bquan -= obj->quan);
			otmp->owt = 0;	/* superfluous */
			otmp->onamelth = 0;
			bp->useup = 1;
			otmp->nobj = billobjs;
			billobjs = otmp;
			return;
		}
		ESHK(shopkeeper)->billct--;
		*bp = bill[ESHK(shopkeeper)->billct];
		return;
	}
	if(obj->unpaid){
		pline("%s didn't notice.", Monnam(shopkeeper));
		obj->unpaid = 0;
		return;		/* %% */
	}
	/* he dropped something of his own - probably wants to sell it */
	if(shopkeeper->msleep || shopkeeper->mfroz ||
		inroom(shopkeeper->mx,shopkeeper->my) != ESHK(shopkeeper)->shoproom)
		return;
	if(ESHK(shopkeeper)->billct == BILLSZ ||
	  ((tmp = shtypes[rooms[ESHK(shopkeeper)->shoproom].rtype-8]) && tmp != obj->olet)
	  || strchr("_0", obj->olet)) {
		pline("%s seems not interested.", Monnam(shopkeeper));
		return;
	}
	ltmp = getprice(obj) * obj->quan;
	if(ANGRY(shopkeeper)) {
		ltmp /= 3;
		NOTANGRY(shopkeeper) = 1;
	} else	ltmp /= 2;
	if(ESHK(shopkeeper)->robbed){
		if((ESHK(shopkeeper)->robbed -= ltmp) < 0)
			ESHK(shopkeeper)->robbed = 0;
pline("Thank you for your contribution to restock this recently plundered shop.");
		return;
	}
	if(ltmp > shopkeeper->mgold)
		ltmp = shopkeeper->mgold;
	pay(-ltmp, shopkeeper);
	if(!ltmp)
	pline("%s gladly accepts %s but cannot pay you at present.",
		Monnam(shopkeeper), doname(obj));
	else
	pline("You sold %s and got %ld gold piece%s.", doname(obj), ltmp,
		plur(ltmp));
}

/* int mode;		0: deliver count 1: paged */
int
doinvbill(int mode)
{
	struct bill_x *bp;
	struct obj *obj;
	long totused, thisused;
	char buf[BUFSZ];

	if(mode == 0) {
	    int cnt = 0;

	    if(shopkeeper)
		for(bp = bill; bp - bill < ESHK(shopkeeper)->billct; bp++)
		    if(bp->useup ||
		      ((obj = bp_to_obj(bp)) && obj->quan < bp->bquan))
			cnt++;
	    return(cnt);
	}

	if(!shopkeeper) {
		impossible("doinvbill: no shopkeeper?");
		return(0);
	}

	set_pager(0);
	if(page_line("Unpaid articles already used up:") || page_line(""))
	    goto quit;

	totused = 0;
	for(bp = bill; bp - bill < ESHK(shopkeeper)->billct; bp++) {
	    obj = bp_to_obj(bp);
	    if(!obj) {
		impossible("Bad shopkeeper administration.");
		goto quit;
	    }
	    if(bp->useup || bp->bquan > obj->quan) {
		int cnt, oquan, uquan;

		oquan = obj->quan;
		uquan = (bp->useup ? bp->bquan : bp->bquan - oquan);
		thisused = bp->price * uquan;
		totused += thisused;
		obj->quan = uquan;		/* cheat doname */
		(void) snprintf(buf, sizeof buf, "x -  %s", doname(obj));
		obj->quan = oquan;		/* restore value */
		for(cnt = 0; buf[cnt]; cnt++);
		while(cnt < 50)
			buf[cnt++] = ' ';
		(void) snprintf(&buf[cnt], sizeof buf - cnt,
		    " %5ld zorkmids", thisused);
		if(page_line(buf))
			goto quit;
	    }
	}
	(void) snprintf(buf, sizeof buf, "Total:%50ld zorkmids", totused);
	if(page_line("") || page_line(buf))
		goto quit;
	set_pager(1);
	return(0);
quit:
	set_pager(2);
	return(0);
}

static int
getprice(struct obj *obj)
{
	int tmp, ac;

	switch(obj->olet){
	case AMULET_SYM:
		tmp = 10*rnd(500);
		break;
	case TOOL_SYM:
		tmp = 10*rnd((obj->otyp == EXPENSIVE_CAMERA) ? 150 : 30);
		break;
	case RING_SYM:
		tmp = 10*rnd(100);
		break;
	case WAND_SYM:
		tmp = 10*rnd(100);
		break;
	case SCROLL_SYM:
		tmp = 10*rnd(50);
#ifdef MAIL
		if(obj->otyp == SCR_MAIL)
			tmp = rnd(5);
#endif /* MAIL */
		break;
	case POTION_SYM:
		tmp = 10*rnd(50);
		break;
	case FOOD_SYM:
		tmp = 10*rnd(5 + (2000/realhunger()));
		break;
	case GEM_SYM:
		tmp = 10*rnd(20);
		break;
	case ARMOR_SYM:
		ac = ARM_BONUS(obj);
		if(ac <= -10)		/* probably impossible */
			ac = -9;
		tmp = 100 + ac*ac*rnd(10+ac);
		break;
	case WEAPON_SYM:
		if(obj->otyp < BOOMERANG)
			tmp = 5*rnd(10);
		else if(obj->otyp == LONG_SWORD ||
			obj->otyp == TWO_HANDED_SWORD)
			tmp = 10*rnd(150);
		else	tmp = 10*rnd(75);
		break;
	case CHAIN_SYM:
		pline("Strange ..., carrying a chain?");
	case BALL_SYM:
		tmp = 10;
		break;
	default:
		tmp = 10000;
	}
	return(tmp);
}

/* not completely foolproof */
static int
realhunger(void)
{
	int tmp = u.uhunger;
	struct obj *otmp = invent;

	while(otmp){
		if(otmp->olet == FOOD_SYM && !otmp->unpaid)
			tmp += objects[otmp->otyp].nutrition;
		otmp = otmp->nobj;
	}
	return((tmp <= 0) ? 1 : tmp);
}

int
shkcatch(struct obj *obj)
{
	struct monst *shkp = shopkeeper;

	if(u.uinshop && shkp && !shkp->mfroz && !shkp->msleep &&
	    u.dx && u.dy &&
	    inroom(u.ux+u.dx, u.uy+u.dy) + 1 == u.uinshop &&
	    shkp->mx == ESHK(shkp)->shk.x && shkp->my == ESHK(shkp)->shk.y &&
	    u.ux == ESHK(shkp)->shd.x && u.uy == ESHK(shkp)->shd.y) {
		pline("%s nimbly catches the %s.", Monnam(shkp), xname(obj));
		obj->nobj = shkp->minvent;
		shkp->minvent = obj;
		return(1);
	}
	return(0);
}

/*
 * shk_move: return 1: he moved  0: he didnt  -1: let m_move do it
 */
int
shk_move(struct monst *shkp)
{
	struct monst *mtmp;
	struct permonst *mdat = shkp->data;
	xchar gx,gy,omx,omy,nx,ny,nix,niy;
	schar appr,i;
	int udist;
	int z;
	schar shkroom,chi,chcnt,cnt;
	boolean uondoor, satdoor, avoid, badinv;
	coord poss[9];
	int info[9];
	struct obj *ib = 0;

	omx = shkp->mx;
	omy = shkp->my;

	if((udist = dist(omx,omy)) < 3) {
		if(ANGRY(shkp)) {
			(void) hitu(shkp, d(mdat->damn, mdat->damd)+1);
			return(0);
		}
		if(ESHK(shkp)->following) {
			if(strncmp(ESHK(shkp)->customer, plname, PL_NSIZ)){
				pline("Hello %s! I was looking for %s.",
					plname, ESHK(shkp)->customer);
				ESHK(shkp)->following = 0;
				return(0);
			}
			if(!ESHK(shkp)->robbed) {	/* impossible? */
				ESHK(shkp)->following = 0;
				return(0);
			}
			if(moves > followmsg+4) {
				pline("Hello %s! Didn't you forget to pay?",
					plname);
				followmsg = moves;
			}
			if(udist < 2)
				return(0);
		}
	}

	shkroom = inroom(omx,omy);
	appr = 1;
	gx = ESHK(shkp)->shk.x;
	gy = ESHK(shkp)->shk.y;
	satdoor = (gx == omx && gy == omy);
	if(ESHK(shkp)->following || ((z = holetime()) >= 0 && z*z <= udist)){
		gx = u.ux;
		gy = u.uy;
		if(shkroom < 0 || shkroom != inroom(u.ux,u.uy))
		    if(udist > 4)
			return(-1);	/* leave it to m_move */
	} else if(ANGRY(shkp)) {
		long saveBlind = Blind;
		Blind = 0;
		if(shkp->mcansee && !Invis && cansee(omx,omy)) {
			gx = u.ux;
			gy = u.uy;
		}
		Blind = saveBlind;
		avoid = FALSE;
	} else {
#define	GDIST(x,y)	((x-gx)*(x-gx)+(y-gy)*(y-gy))
		if(Invis)
		  avoid = FALSE;
		else {
		  uondoor = (u.ux == ESHK(shkp)->shd.x &&
				u.uy == ESHK(shkp)->shd.y);
		  if(uondoor) {
		    if(ESHK(shkp)->billct)
			pline("Hello %s! Will you please pay before leaving?",
				plname);
		    badinv = (carrying(PICK_AXE) || carrying(ICE_BOX));
		    if(satdoor && badinv)
			return(0);
		    avoid = !badinv;
		  } else {
		    avoid = (u.uinshop && dist(gx,gy) > 8);
		    badinv = FALSE;
		  }

		  if(((!ESHK(shkp)->robbed && !ESHK(shkp)->billct) || avoid)
		  	&& GDIST(omx,omy) < 3){
		  	if(!badinv && !online(omx,omy))
				return(0);
		  	if(satdoor)
		  		appr = gx = gy = 0;
		  }
		}
	}
	if(omx == gx && omy == gy)
		return(0);
	if(shkp->mconf) {
		avoid = FALSE;
		appr = 0;
	}
	nix = omx;
	niy = omy;
	cnt = mfndpos(shkp,poss,info,ALLOW_SSM);
	if(avoid && uondoor) {		/* perhaps we cannot avoid him */
		for(i=0; i<cnt; i++)
			if(!(info[(int)i] & NOTONL)) goto notonl_ok;
		avoid = FALSE;
	notonl_ok:
		;
	}
	chi = -1;
	chcnt = 0;
	for(i=0; i<cnt; i++){
		nx = poss[(int)i].x;
		ny = poss[(int)i].y;
	   	if(levl[(int)nx][(int)ny].typ == ROOM
		|| shkroom != ESHK(shkp)->shoproom
		|| ESHK(shkp)->following) {
#ifdef STUPID
		    /* cater for stupid compilers */
		    int zz;
#endif /* STUPID */
		    if(uondoor && (ib = sobj_at(ICE_BOX, nx, ny))) {
			nix = nx; niy = ny; chi = i; break;
		    }
		    if(avoid && (info[(int)i] & NOTONL))
			continue;
		    if((!appr && !rn2(++chcnt)) ||
#ifdef STUPID
			(appr && (zz = GDIST(nix,niy)) && zz > GDIST(nx,ny))
#else
			(appr && GDIST(nx,ny) < GDIST(nix,niy))
#endif /* STUPID */
			) {
			    nix = nx;
			    niy = ny;
			    chi = i;
		    }
		}
	}
	if(nix != omx || niy != omy){
		if(info[(int)chi] & ALLOW_M){
			mtmp = m_at(nix,niy);
			if(hitmm(shkp,mtmp) == 1 && rn2(3) &&
			   hitmm(mtmp,shkp) == 2) return(2);
			return(0);
		} else if(info[(int)chi] & ALLOW_U){
			(void) hitu(shkp, d(mdat->damn, mdat->damd)+1);
			return(0);
		}
		shkp->mx = nix;
		shkp->my = niy;
		pmon(shkp);
		if(ib) {
			freeobj(ib);
			mpickobj(shkp, ib);
		}
		return(1);
	}
	return(0);
}

/* He is digging in the shop. */
void
shopdig(int fall)
{
    if(!fall) {
	if(u.utraptype == TT_PIT)
	    pline("\"Be careful, sir, or you might fall through the floor.\"");
	else
	    pline("\"Please, do not damage the floor here.\"");
    } else if(dist(shopkeeper->mx, shopkeeper->my) < 3) {
	struct obj *obj, *obj2;

	pline("%s grabs your backpack!", shkname(shopkeeper));
	for(obj = invent; obj; obj = obj2) {
		obj2 = obj->nobj;
		if(obj->owornmask) continue;
		freeinv(obj);
		obj->nobj = shopkeeper->minvent;
		shopkeeper->minvent = obj;
		if(obj->unpaid)
			subfrombill(obj);
	}
    }
}
#endif /* QUEST */

int
online(int x, int y)
{
	return(x==u.ux || y==u.uy ||
		(x-u.ux)*(x-u.ux) == (y-u.uy)*(y-u.uy));
}

/* Does this monster follow me downstairs? */
int
follower(struct monst *mtmp)
{
	return( mtmp->mtame || strchr("1TVWZi&, ", mtmp->data->mlet)
#ifndef QUEST
		|| (mtmp->isshk && ESHK(mtmp)->following)
#endif /* QUEST */
		);
}
