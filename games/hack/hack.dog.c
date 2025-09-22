/*	$OpenBSD: hack.dog.c,v 1.9 2016/01/09 18:33:15 mestre Exp $	*/

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

#include "def.edog.h"
#include "hack.h"
#include "hack.mfndpos.h"

extern struct monst *makemon(struct permonst *, int, int);

static void initedog(struct monst *);
static int  dogfood(struct obj *);

struct permonst li_dog =
	{ "little dog", 'd',2,18,6,1,6,sizeof(struct edog) };
struct permonst dog =
	{ "dog", 'd',4,16,5,1,6,sizeof(struct edog) };
struct permonst la_dog =
	{ "large dog", 'd',6,15,4,2,4,sizeof(struct edog) };


void
makedog(void)
{
	struct monst *mtmp = makemon(&li_dog,u.ux,u.uy);

	if(!mtmp) return; /* dogs were genocided */
	initedog(mtmp);
}

void
initedog(struct monst *mtmp)
{
	mtmp->mtame = mtmp->mpeaceful = 1;
	EDOG(mtmp)->hungrytime = 1000 + moves;
	EDOG(mtmp)->eattime = 0;
	EDOG(mtmp)->droptime = 0;
	EDOG(mtmp)->dropdist = 10000;
	EDOG(mtmp)->apport = 10;
	EDOG(mtmp)->whistletime = 0;
}

/* attach the monsters that went down (or up) together with @ */
struct monst *mydogs = 0;
struct monst *fallen_down = 0;	/* monsters that fell through a trapdoor */
	/* they will appear on the next level @ goes to, even if he goes up! */

void
losedogs(void)
{
	struct monst *mtmp;

	while ((mtmp = mydogs)) {
		mydogs = mtmp->nmon;
		mtmp->nmon = fmon;
		fmon = mtmp;
		mnexto(mtmp);
	}
	while ((mtmp = fallen_down) ){
		fallen_down = mtmp->nmon;
		mtmp->nmon = fmon;
		fmon = mtmp;
		rloc(mtmp);
	}
}

void
keepdogs(void)
{
	struct monst *mtmp;

	for(mtmp = fmon; mtmp; mtmp = mtmp->nmon)
	    if(dist(mtmp->mx,mtmp->my) < 3 && follower(mtmp)
		&& !mtmp->msleep && !mtmp->mfroz) {
		relmon(mtmp);
		mtmp->nmon = mydogs;
		mydogs = mtmp;
		unpmon(mtmp);
		keepdogs();	/* we destroyed the link, so use recursion */
		return;		/* (admittedly somewhat primitive) */
	}
}

void
fall_down(struct monst *mtmp)
{
	relmon(mtmp);
	mtmp->nmon = fallen_down;
	fallen_down = mtmp;
	unpmon(mtmp);
	mtmp->mtame = 0;
}

/* return quality of food; the lower the better */
#define	DOGFOOD	0
#define	CADAVER	1
#define	ACCFOOD	2
#define	MANFOOD	3
#define	APPORT	4
#define	POISON	5
#define	UNDEF	6
static int
dogfood(struct obj *obj)
{
	switch(obj->olet) {
	case FOOD_SYM:
	    return(
		(obj->otyp == TRIPE_RATION) ? DOGFOOD :
		(obj->otyp < CARROT) ? ACCFOOD :
		(obj->otyp < CORPSE) ? MANFOOD :
		(poisonous(obj) || obj->age + 50 <= moves ||
		    obj->otyp == DEAD_COCKATRICE)
			? POISON : CADAVER
	    );
	default:
	    if(!obj->cursed) return(APPORT);
	    /* fall into next case */
	case BALL_SYM:
	case CHAIN_SYM:
	case ROCK_SYM:
	    return(UNDEF);
	}
}

/* return 0 (no move), 1 (move) or 2 (dead) */
int
dog_move(struct monst *mtmp, int after)
{
	int nx, ny, omx, omy, appr, nearer, j;
	int udist, chi, i, whappr;
	struct monst *mtmp2;
	struct permonst *mdat = mtmp->data;
	struct edog *edog = EDOG(mtmp);
	struct obj *obj;
	struct trap *trap;
	xchar cnt, chcnt, nix, niy;
	schar dogroom, uroom;
	xchar gx, gy, gtyp, otyp;	/* current goal */
	coord poss[9];
	int info[9];
#define GDIST(x,y) ((x-gx)*(x-gx) + (y-gy)*(y-gy))
#define DDIST(x,y) ((x-omx)*(x-omx) + (y-omy)*(y-omy))

	if(moves <= edog->eattime) return(0);	/* dog is still eating */
	omx = mtmp->mx;
	omy = mtmp->my;
	whappr = (moves - EDOG(mtmp)->whistletime < 5);
	if(moves > edog->hungrytime + 500 && !mtmp->mconf){
		mtmp->mconf = 1;
		mtmp->mhpmax /= 3;
		if(mtmp->mhp > mtmp->mhpmax)
			mtmp->mhp = mtmp->mhpmax;
		if(cansee(omx,omy))
			pline("%s is confused from hunger.", Monnam(mtmp));
		else	pline("You feel worried about %s.", monnam(mtmp));
	} else
	if(moves > edog->hungrytime + 750 || mtmp->mhp < 1){
		if(cansee(omx,omy))
			pline("%s dies from hunger.", Monnam(mtmp));
		else
		pline("You have a sad feeling for a moment, then it passes.");
		mondied(mtmp);
		return(2);
	}
	dogroom = inroom(omx,omy);
	uroom = inroom(u.ux,u.uy);
	udist = dist(omx,omy);

	/* maybe we tamed him while being swallowed --jgm */
	if(!udist) return(0);

	/* if we are carrying sth then we drop it (perhaps near @) */
	/* Note: if apport == 1 then our behaviour is independent of udist */
	if(mtmp->minvent){
		if(!rn2(udist) || !rn2((int) edog->apport))
		if(rn2(10) < edog->apport){
			relobj(mtmp, (int) mtmp->minvis);
			if(edog->apport > 1) edog->apport--;
			edog->dropdist = udist;		/* hpscdi!jon */
			edog->droptime = moves;
		}
	} else {
		if ((obj = o_at(omx,omy)))
			if(!strchr("0_", obj->olet)){
				if((otyp = dogfood(obj)) <= CADAVER){
					nix = omx;
					niy = omy;
					goto eatobj;
				}
				if (obj->owt < 10*mtmp->data->mlevel)
					if (rn2(20) < edog->apport+3)
						if (rn2(udist) || !rn2((int) edog->apport)){
							freeobj(obj);
							unpobj(obj);
							/* if(levl[omx][omy].scrsym == obj->olet)
								newsym(omx,omy); */
							mpickobj(mtmp,obj);
						}
			}
	}

	/* first we look for food */
	gtyp = UNDEF;	/* no goal as yet */
	gx = gy = 0;
	for(obj = fobj; obj; obj = obj->nobj) {
		otyp = dogfood(obj);
		if(otyp > gtyp || otyp == UNDEF) continue;
		if(inroom(obj->ox,obj->oy) != dogroom) continue;
		if(otyp < MANFOOD &&
		 (dogroom >= 0 || DDIST(obj->ox,obj->oy) < 10)) {
			if(otyp < gtyp || (otyp == gtyp &&
				DDIST(obj->ox,obj->oy) < DDIST(gx,gy))){
				gx = obj->ox;
				gy = obj->oy;
				gtyp = otyp;
			}
		} else
		if(gtyp == UNDEF && dogroom >= 0 &&
		   uroom == dogroom &&
		   !mtmp->minvent && edog->apport > rn2(8)){
			gx = obj->ox;
			gy = obj->oy;
			gtyp = APPORT;
		}
	}
	if(gtyp == UNDEF ||
	  (gtyp != DOGFOOD && gtyp != APPORT && moves < edog->hungrytime)){
		if(dogroom < 0 || dogroom == uroom){
			gx = u.ux;
			gy = u.uy;
#ifndef QUEST
		} else {
			int tmp = rooms[(int)dogroom].fdoor;
			    cnt = rooms[(int)dogroom].doorct;

			gx = gy = FAR;	/* random, far away */
			while(cnt--){
			    if(dist(gx,gy) >
				dist(doors[tmp].x, doors[tmp].y)){
					gx = doors[tmp].x;
					gy = doors[tmp].y;
				}
				tmp++;
			}
			/* here gx == FAR e.g. when dog is in a vault */
			if(gx == FAR || (gx == omx && gy == omy)){
				gx = u.ux;
				gy = u.uy;
			}
#endif /* QUEST */
		}
		appr = (udist >= 9) ? 1 : (mtmp->mflee) ? -1 : 0;
		if(after && udist <= 4 && gx == u.ux && gy == u.uy)
			return(0);
		if(udist > 1){
			if (!IS_ROOM(levl[(int)u.ux][(int)u.uy].typ) || !rn2(4) ||
			   whappr ||
			   (mtmp->minvent && rn2((int) edog->apport)))
				appr = 1;
		}
		/* if you have dog food he'll follow you more closely */
		if (appr == 0) {
			obj = invent;
			while(obj){
				if(obj->otyp == TRIPE_RATION){
					appr = 1;
					break;
				}
				obj = obj->nobj;
			}
		}
	} else	appr = 1;	/* gtyp != UNDEF */
	if(mtmp->mconf) appr = 0;

	if(gx == u.ux && gy == u.uy && (dogroom != uroom || dogroom < 0)){
	coord *cp;
		cp = gettrack(omx,omy);
		if(cp){
			gx = cp->x;
			gy = cp->y;
		}
	}

	nix = omx;
	niy = omy;
	cnt = mfndpos(mtmp,poss,info,ALLOW_M | ALLOW_TRAPS);
	chcnt = 0;
	chi = -1;
	for(i=0; i<cnt; i++){
		nx = poss[i].x;
		ny = poss[i].y;
		if(info[i] & ALLOW_M){
			mtmp2 = m_at(nx,ny);
			if(mtmp2->data->mlevel >= mdat->mlevel+2 ||
			  mtmp2->data->mlet == 'c')
				continue;
			if(after) return(0); /* hit only once each move */

			if(hitmm(mtmp, mtmp2) == 1 && rn2(4) &&
			  mtmp2->mlstmv != moves &&
			  hitmm(mtmp2,mtmp) == 2) return(2);
			return(0);
		}

		/* dog avoids traps */
		/* but perhaps we have to pass a trap in order to follow @ */
		if((info[i] & ALLOW_TRAPS) && (trap = t_at(nx,ny))){
			if(!trap->tseen && rn2(40)) continue;
			if(rn2(10)) continue;
		}

		/* dog eschewes cursed objects */
		/* but likes dog food */
		obj = fobj;
		while(obj){
		    if(obj->ox != nx || obj->oy != ny)
			goto nextobj;
		    if(obj->cursed) goto nxti;
		    if(obj->olet == FOOD_SYM &&
			(otyp = dogfood(obj)) < MANFOOD &&
			(otyp < ACCFOOD || edog->hungrytime <= moves)){
			/* Note: our dog likes the food so much that he
			might eat it even when it conceals a cursed object */
			nix = nx;
			niy = ny;
			chi = i;
		     eatobj:
			edog->eattime =
			    moves + obj->quan * objects[obj->otyp].oc_delay;
			if(edog->hungrytime < moves)
			    edog->hungrytime = moves;
			edog->hungrytime +=
			    5*obj->quan * objects[obj->otyp].nutrition;
			mtmp->mconf = 0;
			if(cansee(nix,niy))
			    pline("%s ate %s.", Monnam(mtmp), doname(obj));
			/* perhaps this was a reward */
			if(otyp != CADAVER)
			edog->apport += 200/(edog->dropdist+moves-edog->droptime);
			delobj(obj);
			goto newdogpos;
		    }
		nextobj:
		    obj = obj->nobj;
		}

		for(j=0; j<MTSZ && j<cnt-1; j++)
			if(nx == mtmp->mtrack[j].x && ny == mtmp->mtrack[j].y)
				if(rn2(4*(cnt-j))) goto nxti;

/* Some stupid C compilers cannot compute the whole expression at once. */
		nearer = GDIST(nx,ny);
		nearer -= GDIST(nix,niy);
		nearer *= appr;
		if((nearer == 0 && !rn2(++chcnt)) || nearer<0 ||
			(nearer > 0 && !whappr &&
				((omx == nix && omy == niy && !rn2(3))
				|| !rn2(12))
			)){
			nix = nx;
			niy = ny;
			if(nearer < 0) chcnt = 0;
			chi = i;
		}
	nxti:	;
	}
newdogpos:
	if(nix != omx || niy != omy){
		if(info[chi] & ALLOW_U){
			(void) hitu(mtmp, d(mdat->damn, mdat->damd)+1);
			return(0);
		}
		mtmp->mx = nix;
		mtmp->my = niy;
		for(j=MTSZ-1; j>0; j--) mtmp->mtrack[j] = mtmp->mtrack[j-1];
		mtmp->mtrack[0].x = omx;
		mtmp->mtrack[0].y = omy;
	}
	if(mintrap(mtmp) == 2)	/* he died */
		return(2);
	pmon(mtmp);
	return(1);
}

/* return roomnumber or -1 */
int
inroom(xchar x, xchar y)
{
#ifndef QUEST
	struct mkroom *croom = &rooms[0];
	while(croom->hx >= 0){
		if(croom->hx >= x-1 && croom->lx <= x+1 &&
		   croom->hy >= y-1 && croom->ly <= y+1)
			return(croom - rooms);
		croom++;
	}
#endif /* QUEST */
	return(-1);	/* not in room or on door */
}

int
tamedog(struct monst *mtmp, struct obj *obj)
{
	struct monst *mtmp2;

	if(flags.moonphase == FULL_MOON && night() && rn2(6))
		return(0);

	/* If we cannot tame him, at least he's no longer afraid. */
	mtmp->mflee = 0;
	mtmp->mfleetim = 0;
	if(mtmp->mtame || mtmp->mfroz ||
#ifndef NOWORM
		mtmp->wormno ||
#endif /* NOWORM */
		mtmp->isshk || mtmp->isgd || strchr(" &@12", mtmp->data->mlet))
		return(0); /* no tame long worms? */
	if(obj) {
		if(dogfood(obj) >= MANFOOD) return(0);
		if(cansee(mtmp->mx,mtmp->my)){
			pline("%s devours the %s.", Monnam(mtmp),
				objects[obj->otyp].oc_name);
		}
		obfree(obj, (struct obj *) 0);
	}
	mtmp2 = newmonst(sizeof(struct edog) + mtmp->mnamelth);
	*mtmp2 = *mtmp;
	mtmp2->mxlth = sizeof(struct edog);
	if(mtmp->mnamelth)
		(void) strlcpy(NAME(mtmp2), NAME(mtmp), mtmp2->mnamelth);
	initedog(mtmp2);
	replmon(mtmp,mtmp2);
	return(1);
}
