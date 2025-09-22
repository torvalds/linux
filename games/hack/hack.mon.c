/*	$OpenBSD: hack.mon.c,v 1.11 2016/01/09 18:33:15 mestre Exp $	*/

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
#include "hack.mfndpos.h"

int warnlevel;		/* used by movemon and dochugw */
long lastwarntime;
int lastwarnlev;
char *warnings[] = {
	"white", "pink", "red", "ruby", "purple", "black"
};

static int  dochugw(struct monst *);
static void mpickgold(struct monst *);
static void mpickgems(struct monst *);
static void dmonsfree(void);
static int  ishuman(struct monst *);

void
movemon(void)
{
	struct monst *mtmp;
	int fr;

	warnlevel = 0;

	while(1) {
		/* find a monster that we haven't treated yet */
		/* note that mtmp or mtmp->nmon might get killed
		   while mtmp moves, so we cannot just walk down the
		   chain (even new monsters might get created!) */
		for(mtmp = fmon; mtmp; mtmp = mtmp->nmon)
			if(mtmp->mlstmv < moves) goto next_mon;
		/* treated all monsters */
		break;

	next_mon:
		mtmp->mlstmv = moves;

		/* most monsters drown in pools */
		{ boolean inpool, iseel;

		  inpool = (levl[(int)mtmp->mx][(int)mtmp->my].typ == POOL);
		  iseel = (mtmp->data->mlet == ';');
		  if(inpool && !iseel) {
			if(cansee(mtmp->mx,mtmp->my))
			    pline("%s drowns.", Monnam(mtmp));
			mondead(mtmp);
			continue;
		  }
		/* but eels have a difficult time outside */
		  if(iseel && !inpool) {
			if(mtmp->mhp > 1) mtmp->mhp--;
			mtmp->mflee = 1;
			mtmp->mfleetim += 2;
		  }
		}
		if(mtmp->mblinded && !--mtmp->mblinded)
			mtmp->mcansee = 1;
		if(mtmp->mfleetim && !--mtmp->mfleetim)
			mtmp->mflee = 0;
		if(mtmp->mimic) continue;
		if(mtmp->mspeed != MSLOW || !(moves%2)){
			/* continue if the monster died fighting */
			fr = -1;
			if(Conflict && cansee(mtmp->mx,mtmp->my)
				&& (fr = fightm(mtmp)) == 2)
				continue;
			if(fr<0 && dochugw(mtmp))
				continue;
		}
		if(mtmp->mspeed == MFAST && dochugw(mtmp))
			continue;
	}

	warnlevel -= u.ulevel;
	if(warnlevel >= SIZE(warnings))
		warnlevel = SIZE(warnings)-1;
	if(warnlevel >= 0)
	if(warnlevel > lastwarnlev || moves > lastwarntime + 5){
	    char *rr;
	    switch(Warning & (LEFT_RING | RIGHT_RING)){
	    case LEFT_RING:
		rr = "Your left ring glows";
		break;
	    case RIGHT_RING:
		rr = "Your right ring glows";
		break;
	    case LEFT_RING | RIGHT_RING:
		rr = "Both your rings glow";
		break;
	    default:
		rr = "Your fingertips glow";
		break;
	    }
	    pline("%s %s!", rr, warnings[warnlevel]);
	    lastwarntime = moves;
	    lastwarnlev = warnlevel;
	}

	dmonsfree();	/* remove all dead monsters */
}

void
justswld(struct monst *mtmp, char *name)
{
	mtmp->mx = u.ux;
	mtmp->my = u.uy;
	u.ustuck = mtmp;
	pmon(mtmp);
	kludge("%s swallows you!",name);
	more();
	seeoff(1);
	u.uswallow = 1;
	u.uswldtim = 0;
	swallowed();
}

void
youswld(struct monst *mtmp, int dam, int die, char *name)
{
	if(mtmp != u.ustuck) return;
	kludge("%s digests you!",name);
	u.uhp -= dam;
	if(u.uswldtim++ >= die){	/* a3 */
		pline("It totally digests you!");
		u.uhp = -1;
	}
	if(u.uhp < 1) done_in_by(mtmp);
	/* flags.botlx = 1; */		/* should we show status line ? */
}

static int
dochugw(struct monst *mtmp)
{
	int x = mtmp->mx;
	int y = mtmp->my;
	int d = dochug(mtmp);
	int dd;

	if(!d)		/* monster still alive */
	if(Warning)
	if(!mtmp->mpeaceful)
	if(mtmp->data->mlevel > warnlevel)
	if((dd = dist(mtmp->mx,mtmp->my)) < dist(x,y))
	if(dd < 100)
	if(!canseemon(mtmp))
		warnlevel = mtmp->data->mlevel;
	return(d);
}

/* returns 1 if monster died moving, 0 otherwise */
int
dochug(struct monst *mtmp)
{
	struct permonst *mdat;
	int tmp, nearby, scared;

	if(mtmp->cham && !rn2(6))
		(void) newcham(mtmp, &mons[dlevel+14+rn2(CMNUM-14-dlevel)]);
	mdat = mtmp->data;
	if(mdat->mlevel < 0)
		panic("bad monster %c (%d)",mdat->mlet,mdat->mlevel);

	/* regenerate monsters */
	if((!(moves%20) || strchr(MREGEN, mdat->mlet)) &&
	    mtmp->mhp < mtmp->mhpmax)
		mtmp->mhp++;

	if(mtmp->mfroz) return(0); /* frozen monsters don't do anything */

	if(mtmp->msleep) {
		/* wake up, or get out of here. */
		/* ettins are hard to surprise */
		/* Nymphs and Leprechauns do not easily wake up */
		if(cansee(mtmp->mx,mtmp->my) &&
			(!Stealth || (mdat->mlet == 'e' && rn2(10))) &&
			(!strchr("NL",mdat->mlet) || !rn2(50)) &&
			(Aggravate_monster || strchr("d1", mdat->mlet)
				|| (!rn2(7) && !mtmp->mimic)))
			mtmp->msleep = 0;
		else return(0);
	}

	/* not frozen or sleeping: wipe out texts written in the dust */
	wipe_engr_at(mtmp->mx, mtmp->my, 1);

	/* confused monsters get unconfused with small probability */
	if(mtmp->mconf && !rn2(50)) mtmp->mconf = 0;

	/* some monsters teleport */
	if(mtmp->mflee && strchr("tNL", mdat->mlet) && !rn2(40)){
		rloc(mtmp);
		return(0);
	}
	if(mdat->mmove < rnd(6)) return(0);

	/* fleeing monsters might regain courage */
	if(mtmp->mflee && !mtmp->mfleetim
	    && mtmp->mhp == mtmp->mhpmax && !rn2(25))
		mtmp->mflee = 0;

	nearby = (dist(mtmp->mx, mtmp->my) < 3);
	scared = (nearby && (sengr_at("Elbereth", u.ux, u.uy) ||
			sobj_at(SCR_SCARE_MONSTER, u.ux, u.uy)));
	if(scared && !mtmp->mflee) {
		mtmp->mflee = 1;
		mtmp->mfleetim = (rn2(7) ? rnd(10) : rnd(100));
	}

	if(!nearby ||
		mtmp->mflee ||
		mtmp->mconf ||
		(mtmp->minvis && !rn2(3)) ||
		(strchr("BIuy", mdat->mlet) && !rn2(4)) ||
		(mdat->mlet == 'L' && !u.ugold && (mtmp->mgold || rn2(2))) ||
		(!mtmp->mcansee && !rn2(4)) ||
		mtmp->mpeaceful
	   ) {
		tmp = m_move(mtmp,0);	/* 2: monster died moving */
		if(tmp == 2 || (tmp && mdat->mmove <= 12))
			return(tmp == 2);
	}

	if(!strchr("Ea", mdat->mlet) && nearby &&
	 !mtmp->mpeaceful && u.uhp > 0 && !scared) {
		if(mhitu(mtmp))
			return(1);	/* monster died (e.g. 'y' or 'F') */
	}
	/* extra movement for fast monsters */
	if(mdat->mmove-12 > rnd(12)) tmp = m_move(mtmp,1);
	return(tmp == 2);
}

int
m_move(struct monst *mtmp, int after)
{
	struct monst *mtmp2;
	int nx,ny,omx,omy,appr,nearer,cnt,i,j;
	xchar gx,gy,nix,niy,chcnt;
	int chi;
	boolean likegold, likegems, likeobjs;
	char msym = mtmp->data->mlet;
	schar mmoved = 0;	/* not strictly nec.: chi >= 0 will do */
	coord poss[9];
	int info[9];

	if(mtmp->mfroz || mtmp->msleep)
		return(0);
	if(mtmp->mtrapped) {
		i = mintrap(mtmp);
		if(i == 2) return(2);	/* he died */
		if(i == 1) return(0);	/* still in trap, so didnt move */
	}
	if(mtmp->mhide && o_at(mtmp->mx,mtmp->my) && rn2(10))
		return(0);		/* do not leave hiding place */

#ifndef NOWORM
	if(mtmp->wormno)
		goto not_special;
#endif /* NOWORM */

	/* my dog gets a special treatment */
	if(mtmp->mtame) {
		return( dog_move(mtmp, after) );
	}

	/* likewise for shopkeeper */
	if(mtmp->isshk) {
		mmoved = shk_move(mtmp);
		if(mmoved >= 0)
			goto postmov;
		mmoved = 0;		/* follow player outside shop */
	}

	/* and for the guard */
	if(mtmp->isgd) {
		mmoved = gd_move();
		goto postmov;
	}

/* teleport if that lies in our nature ('t') or when badly wounded ('1') */
	if((msym == 't' && !rn2(5))
	|| (msym == '1' && (mtmp->mhp < 7 || (!xdnstair && !rn2(5))
		|| levl[(int)u.ux][(int)u.uy].typ == STAIRS))) {
		if(mtmp->mhp < 7 || (msym == 't' && rn2(2)))
			rloc(mtmp);
		else
			mnexto(mtmp);
		mmoved = 1;
		goto postmov;
	}

	/* spit fire ('D') or use a wand ('1') when appropriate */
	if(strchr("D1", msym))
		inrange(mtmp);

	if(msym == 'U' && !mtmp->mcan && canseemon(mtmp) &&
	    mtmp->mcansee && rn2(5)) {
		if(!Confusion)
			pline("%s's gaze has confused you!", Monnam(mtmp));
		else
			pline("You are getting more and more confused.");
		if(rn2(3)) mtmp->mcan = 1;
		Confusion += d(3,4);		/* timeout */
	}
not_special:
	if(!mtmp->mflee && u.uswallow && u.ustuck != mtmp) return(1);
	appr = 1;
	if(mtmp->mflee) appr = -1;
	if(mtmp->mconf || Invis ||  !mtmp->mcansee ||
		(strchr("BIy", msym) && !rn2(3)))
		appr = 0;
	omx = mtmp->mx;
	omy = mtmp->my;
	gx = u.ux;
	gy = u.uy;
	if(msym == 'L' && appr == 1 && mtmp->mgold > u.ugold)
		appr = -1;

	/* random criterion for 'smell' or track finding ability
	   should use mtmp->msmell or sth
	 */
	if(msym == '@' ||
	  ('a' <= msym && msym <= 'z')) {
	coord *cp;
	schar mroom;
		mroom = inroom(omx,omy);
		if(mroom < 0 || mroom != inroom(u.ux,u.uy)){
		    cp = gettrack(omx,omy);
		    if(cp){
			gx = cp->x;
			gy = cp->y;
		    }
		}
	}

	/* look for gold or jewels nearby */
	likegold = (strchr("LOD", msym) != NULL);
	likegems = (strchr("ODu", msym) != NULL);
	likeobjs = mtmp->mhide;
#define	SRCHRADIUS	25
	{ xchar mind = SRCHRADIUS;		/* not too far away */
	  int dd;
	  if(likegold){
		struct gold *gold;
		for(gold = fgold; gold; gold = gold->ngold)
		  if((dd = DIST(omx,omy,gold->gx,gold->gy)) < mind){
		    mind = dd;
		    gx = gold->gx;
		    gy = gold->gy;
		}
	  }
	  if(likegems || likeobjs){
		struct obj *otmp;
		for(otmp = fobj; otmp; otmp = otmp->nobj)
		if(likeobjs || otmp->olet == GEM_SYM)
		if(msym != 'u' ||
			objects[otmp->otyp].g_val != 0)
		if((dd = DIST(omx,omy,otmp->ox,otmp->oy)) < mind){
		    mind = dd;
		    gx = otmp->ox;
		    gy = otmp->oy;
		}
	  }
	  if(mind < SRCHRADIUS && appr == -1) {
		if(dist(omx,omy) < 10) {
		    gx = u.ux;
		    gy = u.uy;
		} else
		    appr = 1;
	  }
	}
	nix = omx;
	niy = omy;
	cnt = mfndpos(mtmp,poss,info,
		msym == 'u' ? NOTONL :
		(msym == '@' || msym == '1') ? (ALLOW_SSM | ALLOW_TRAPS) :
		strchr(UNDEAD, msym) ? NOGARLIC : ALLOW_TRAPS);
		/* ALLOW_ROCK for some monsters ? */
	chcnt = 0;
	chi = -1;
	for(i=0; i<cnt; i++) {
		nx = poss[i].x;
		ny = poss[i].y;
		for(j=0; j<MTSZ && j<cnt-1; j++)
			if(nx == mtmp->mtrack[j].x && ny == mtmp->mtrack[j].y)
				if(rn2(4*(cnt-j))) goto nxti;
#ifdef STUPID
		/* some stupid compilers think that this is too complicated */
		{ int d1 = DIST(nx,ny,gx,gy);
		  int d2 = DIST(nix,niy,gx,gy);
		  nearer = (d1 < d2);
		}
#else
		nearer = (DIST(nx,ny,gx,gy) < DIST(nix,niy,gx,gy));
#endif /* STUPID */
		if((appr == 1 && nearer) || (appr == -1 && !nearer) ||
			!mmoved ||
			(!appr && !rn2(++chcnt))){
			nix = nx;
			niy = ny;
			chi = i;
			mmoved = 1;
		}
	nxti:	;
	}
	if(mmoved){
		if(info[chi] & ALLOW_M){
			mtmp2 = m_at(nix,niy);
			if(hitmm(mtmp,mtmp2) == 1 && rn2(4) &&
			  hitmm(mtmp2,mtmp) == 2) return(2);
			return(0);
		}
		if(info[chi] & ALLOW_U){
		  (void) hitu(mtmp, d(mtmp->data->damn, mtmp->data->damd)+1);
		  return(0);
		}
		mtmp->mx = nix;
		mtmp->my = niy;
		for(j=MTSZ-1; j>0; j--) mtmp->mtrack[j] = mtmp->mtrack[j-1];
		mtmp->mtrack[0].x = omx;
		mtmp->mtrack[0].y = omy;
#ifndef NOWORM
		if(mtmp->wormno) worm_move(mtmp);
#endif /* NOWORM */
	} else {
		if(msym == 'u' && rn2(2)){
			rloc(mtmp);
			return(0);
		}
#ifndef NOWORM
		if(mtmp->wormno) worm_nomove(mtmp);
#endif /* NOWORM */
	}
postmov:
	if(mmoved == 1) {
		if(mintrap(mtmp) == 2)	/* he died */
			return(2);
		if(likegold) mpickgold(mtmp);
		if(likegems) mpickgems(mtmp);
		if(mtmp->mhide) mtmp->mundetected = 1;
	}
	pmon(mtmp);
	return(mmoved);
}

static void
mpickgold(struct monst *mtmp)
{
	struct gold *gold;

	while ((gold = g_at(mtmp->mx, mtmp->my))) {
		mtmp->mgold += gold->amount;
		freegold(gold);
		if(levl[(int)mtmp->mx][(int)mtmp->my].scrsym == '$')
			newsym(mtmp->mx, mtmp->my);
	}
}

static void
mpickgems(struct monst *mtmp)
{
	struct obj *otmp;

	for (otmp = fobj; otmp; otmp = otmp->nobj)
	if (otmp->olet == GEM_SYM)
	if (otmp->ox == mtmp->mx && otmp->oy == mtmp->my)
	if (mtmp->data->mlet != 'u' || objects[otmp->otyp].g_val != 0){
		freeobj(otmp);
		mpickobj(mtmp, otmp);
		if(levl[(int)mtmp->mx][(int)mtmp->my].scrsym == GEM_SYM)
			newsym(mtmp->mx, mtmp->my);	/* %% */
		return;	/* pick only one object */
	}
}

/* return number of acceptable neighbour positions */
int
mfndpos(struct monst *mon, coord poss[9],int info[9], int flag)
{
	int x,y,nx,ny,cnt = 0,ntyp;
	struct monst *mtmp;
	int nowtyp;
	boolean pool;

	x = mon->mx;
	y = mon->my;
	nowtyp = levl[x][y].typ;

	pool = (mon->data->mlet == ';');
nexttry:	/* eels prefer the water, but if there is no water nearby,
		   they will crawl over land */
	if(mon->mconf) {
		flag |= ALLOW_ALL;
		flag &= ~NOTONL;
	}
	for(nx = x-1; nx <= x+1; nx++) for(ny = y-1; ny <= y+1; ny++)
	if(nx != x || ny != y) if(isok(nx,ny))
	if(!IS_ROCK(ntyp = levl[nx][ny].typ))
	if(!(nx != x && ny != y && (nowtyp == DOOR || ntyp == DOOR)))
	if((ntyp == POOL) == pool) {
		info[cnt] = 0;
		if (nx == u.ux && ny == u.uy) {
			if(!(flag & ALLOW_U)) continue;
			info[cnt] = ALLOW_U;
		} else if ((mtmp = m_at(nx,ny))) {
			if (!(flag & ALLOW_M))
				continue;
			info[cnt] = ALLOW_M;
			if(mtmp->mtame){
				if(!(flag & ALLOW_TM)) continue;
				info[cnt] |= ALLOW_TM;
			}
		}
		if(sobj_at(CLOVE_OF_GARLIC, nx, ny)) {
			if(flag & NOGARLIC) continue;
			info[cnt] |= NOGARLIC;
		}
		if(sobj_at(SCR_SCARE_MONSTER, nx, ny) ||
		   (!mon->mpeaceful && sengr_at("Elbereth", nx, ny))) {
			if(!(flag & ALLOW_SSM)) continue;
			info[cnt] |= ALLOW_SSM;
		}
		if(sobj_at(ENORMOUS_ROCK, nx, ny)) {
			if(!(flag & ALLOW_ROCK)) continue;
			info[cnt] |= ALLOW_ROCK;
		}
		if(!Invis && online(nx,ny)){
			if(flag & NOTONL) continue;
			info[cnt] |= NOTONL;
		}
		/* we cannot avoid traps of an unknown kind */
		{ struct trap *ttmp = t_at(nx, ny);
		  int tt;
			if(ttmp) {
				tt = 1 << ttmp->ttyp;
				if(mon->mtrapseen & tt){
					if(!(flag & tt)) continue;
					info[cnt] |= tt;
				}
			}
		}
		poss[cnt].x = nx;
		poss[cnt].y = ny;
		cnt++;
	}
	if(!cnt && pool && nowtyp != POOL) {
		pool = FALSE;
		goto nexttry;
	}
	return(cnt);
}

int
dist(int x, int y)
{
	return((x-u.ux)*(x-u.ux) + (y-u.uy)*(y-u.uy));
}

void
poisoned(char *string, char *pname)
{
	int i;

	if(Blind) pline("It was poisoned.");
	else pline("The %s was poisoned!",string);
	if(Poison_resistance) {
		pline("The poison doesn't seem to affect you.");
		return;
	}
	i = rn2(10);
	if(i == 0) {
		u.uhp = -1;
		pline("I am afraid the poison was deadly ...");
	} else if(i <= 5) {
		losestr(rn1(3,3));
	} else {
		losehp(rn1(10,6), pname);
	}
	if(u.uhp < 1) {
		killer = pname;
		done("died");
	}
}

void
mondead(struct monst *mtmp)
{
	relobj(mtmp,1);
	unpmon(mtmp);
	relmon(mtmp);
	unstuck(mtmp);
	if(mtmp->isshk) shkdead(mtmp);
	if(mtmp->isgd) gddead();
#ifndef NOWORM
	if(mtmp->wormno) wormdead(mtmp);
#endif /* NOWORM */
	monfree(mtmp);
}

/* called when monster is moved to larger structure */
void
replmon(struct monst *mtmp, struct monst *mtmp2)
{
	relmon(mtmp);
	monfree(mtmp);
	mtmp2->nmon = fmon;
	fmon = mtmp2;
	if(u.ustuck == mtmp) u.ustuck = mtmp2;
	if(mtmp2->isshk) replshk(mtmp,mtmp2);
	if(mtmp2->isgd) replgd(mtmp,mtmp2);
}

void
relmon(struct monst *mon)
{
	struct monst *mtmp;

	if(mon == fmon) fmon = fmon->nmon;
	else {
		for(mtmp = fmon; mtmp->nmon != mon; mtmp = mtmp->nmon) ;
		mtmp->nmon = mon->nmon;
	}
}

/* we do not free monsters immediately, in order to have their name
   available shortly after their demise */
struct monst *fdmon;	/* chain of dead monsters, need not to be saved */

void
monfree(struct monst *mtmp)
{
	mtmp->nmon = fdmon;
	fdmon = mtmp;
}

static void
dmonsfree(void)
{
	struct monst *mtmp;

	while ((mtmp = fdmon)) {
		fdmon = mtmp->nmon;
		free(mtmp);
	}
}

void
unstuck(struct monst *mtmp)
{
	if(u.ustuck == mtmp) {
		if(u.uswallow){
			u.ux = mtmp->mx;
			u.uy = mtmp->my;
			u.uswallow = 0;
			setsee();
			docrt();
		}
		u.ustuck = 0;
	}
}

void
killed(struct monst *mtmp)
{
	int tmp, nk, x, y;
	struct permonst *mdat;

	if(mtmp->cham) mtmp->data = PM_CHAMELEON;
	mdat = mtmp->data;
	if(Blind) pline("You destroy it!");
	else {
		pline("You destroy %s!",
			mtmp->mtame ? amonnam(mtmp, "poor") : monnam(mtmp));
	}
	if(u.umconf) {
		if(!Blind) pline("Your hands stop glowing blue.");
		u.umconf = 0;
	}

	/* count killed monsters */
#define	MAXMONNO	100
	nk = 1;		      /* in case we cannot find it in mons */
	tmp = mdat - mons;    /* index in mons array (if not 'd', '@', ...) */
	if(tmp >= 0 && tmp < CMNUM+2) {
	    extern char fut_geno[];
	    u.nr_killed[tmp]++;
	    if((nk = u.nr_killed[tmp]) > MAXMONNO &&
		!strchr(fut_geno, mdat->mlet))
		    charcat(fut_geno,  mdat->mlet);
	}

	/* punish bad behaviour */
	if(mdat->mlet == '@') Telepat = 0, u.uluck -= 2;
	if(mtmp->mpeaceful || mtmp->mtame) u.uluck--;
	if(mdat->mlet == 'u') u.uluck -= 5;
	if((int)u.uluck < LUCKMIN) u.uluck = LUCKMIN;

	/* give experience points */
	tmp = 1 + mdat->mlevel * mdat->mlevel;
	if(mdat->ac < 3) tmp += 2*(7 - mdat->ac);
	if(strchr("AcsSDXaeRTVWU&In:P", mdat->mlet))
		tmp += 2*mdat->mlevel;
	if(strchr("DeV&P",mdat->mlet)) tmp += (7*mdat->mlevel);
	if(mdat->mlevel > 6) tmp += 50;
	if(mdat->mlet == ';') tmp += 1000;

#ifdef NEW_SCORING
	/* ------- recent addition: make nr of points decrease
		   when this is not the first of this kind */
	{ int ul = u.ulevel;
	  int ml = mdat->mlevel;
	  int tmp2;

	if(ul < 14)    /* points are given based on present and future level */
	    for(tmp2 = 0; !tmp2 || ul + tmp2 <= ml; tmp2++)
		if(u.uexp + 1 + (tmp + ((tmp2 <= 0) ? 0 : 4<<(tmp2-1)))/nk
		    >= 10*pow((unsigned)(ul-1)))
			if(++ul == 14) break;

	tmp2 = ml - ul -1;
	tmp = (tmp + ((tmp2 < 0) ? 0 : 4<<tmp2))/nk;
	if(!tmp) tmp = 1;
	}
	/* note: ul is not necessarily the future value of u.ulevel */
	/* ------- end of recent valuation change ------- */
#endif /* NEW_SCORING */

	more_experienced(tmp,0);
	flags.botl = 1;
	while(u.ulevel < 14 && u.uexp >= newuexp()){
		pline("Welcome to experience level %u.", ++u.ulevel);
		tmp = rnd(10);
		if(tmp < 3) tmp = rnd(10);
		u.uhpmax += tmp;
		u.uhp += tmp;
		flags.botl = 1;
	}

	/* dispose of monster and make cadaver */
	x = mtmp->mx;	y = mtmp->my;
	mondead(mtmp);
	tmp = mdat->mlet;
	if(tmp == 'm') { /* he killed a minotaur, give him a wand of digging */
			/* note: the dead minotaur will be on top of it! */
		mksobj_at(WAN_DIGGING, x, y);
		/* if(cansee(x,y)) atl(x,y,fobj->olet); */
		stackobj(fobj);
	} else
#ifndef NOWORM
	if(tmp == 'w') {
		mksobj_at(WORM_TOOTH, x, y);
		stackobj(fobj);
	} else
#endif /* NOWORM */
	if(!letter(tmp) || (!strchr("mw", tmp) && !rn2(3))) tmp = 0;

	if(ACCESSIBLE(levl[x][y].typ))	/* might be mimic in wall or dead eel*/
	    if(x != u.ux || y != u.uy)	/* might be here after swallowed */
		if(strchr("NTVm&",mdat->mlet) || rn2(5)) {
		struct obj *obj2 = mkobj_at(tmp,x,y);
		if(cansee(x,y))
			atl(x,y,obj2->olet);
		stackobj(obj2);
	}
}

void
kludge(char *str, char *arg)
{
	if(Blind) {
		if(*str == '%') pline(str,"It");
		else pline(str,"it");
	} else pline(str,arg);
}

void
rescham(void)	/* force all chameleons to become normal */
{
	struct monst *mtmp;

	for(mtmp = fmon; mtmp; mtmp = mtmp->nmon)
		if(mtmp->cham) {
			mtmp->cham = 0;
			(void) newcham(mtmp, PM_CHAMELEON);
		}
}

/* make a chameleon look like a new monster */
/* returns 1 if the monster actually changed */
int
newcham(struct monst *mtmp, struct permonst *mdat)
{
	int mhp, hpn, hpd;

	if(mdat == mtmp->data) return(0);	/* still the same monster */
#ifndef NOWORM
	if(mtmp->wormno) wormdead(mtmp);	/* throw tail away */
#endif /* NOWORM */
	if (u.ustuck == mtmp) {
		if (u.uswallow) {
			u.uswallow = 0;
			u.uswldtim = 0;
			mnexto (mtmp);
			docrt();
			prme();
		}
		u.ustuck = 0;
	}
	hpn = mtmp->mhp;
	hpd = (mtmp->data->mlevel)*8;
	if(!hpd) hpd = 4;
	mtmp->data = mdat;
	mhp = (mdat->mlevel)*8;
	/* new hp: same fraction of max as before */
	mtmp->mhp = 2 + (hpn*mhp)/hpd;
	hpn = mtmp->mhpmax;
	mtmp->mhpmax = 2 + (hpn*mhp)/hpd;
	mtmp->minvis = (mdat->mlet == 'I') ? 1 : 0;
#ifndef NOWORM
	if(mdat->mlet == 'w' && getwn(mtmp)) initworm(mtmp);
			/* perhaps we should clear mtmp->mtame here? */
#endif /* NOWORM */
	unpmon(mtmp);	/* necessary for 'I' and to force pmon */
	pmon(mtmp);
	return(1);
}

/* Make monster mtmp next to you (if possible) */
void
mnexto(struct monst *mtmp)
{
	coord mm;
	mm = enexto(u.ux, u.uy);
	mtmp->mx = mm.x;
	mtmp->my = mm.y;
	pmon(mtmp);
}

static int
ishuman(struct monst *mtmp)
{
	return(mtmp->data->mlet == '@');
}

void
setmangry(struct monst *mtmp)
{
	if(!mtmp->mpeaceful) return;
	if(mtmp->mtame) return;
	mtmp->mpeaceful = 0;
	if(ishuman(mtmp)) pline("%s gets angry!", Monnam(mtmp));
}

/* not one hundred percent correct: now a snake may hide under an
   invisible object */
int
canseemon(struct monst *mtmp)
{
	return((!mtmp->minvis || See_invisible)
		&& (!mtmp->mhide || !o_at(mtmp->mx,mtmp->my))
		&& cansee(mtmp->mx, mtmp->my));
}
