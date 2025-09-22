/*	$OpenBSD: hack.mklev.c,v 1.9 2021/01/26 20:42:49 millert Exp $	*/

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

#define somex() ((random()%(croom->hx-croom->lx+1))+croom->lx)
#define somey() ((random()%(croom->hy-croom->ly+1))+croom->ly)

#define	XLIM	4	/* define minimum required space around a room */
#define	YLIM	3
boolean secret;		/* TRUE while making a vault: increase [XY]LIM */
int smeq[MAXNROFROOMS+1];
int doorindex;
struct rm zerorm;
schar nxcor;
boolean goldseen;
int nroom;

/* Definitions used by makerooms() and addrs() */
#define	MAXRS	50	/* max lth of temp rectangle table - arbitrary */
struct rectangle {
	xchar rlx,rly,rhx,rhy;
} rs[MAXRS+1];
int rscnt,rsmax;	/* 0..rscnt-1: currently under consideration */
			/* rscnt..rsmax: discarded */

static void addrs(int, int, int, int);
static void addrsx(int, int, int, int, boolean);
int comp(const void *, const void *);
static coord finddpos(int, int, int, int);
static int  okdoor(int, int);
static void dodoor(int, int, struct mkroom *);
static void dosdoor(int, int, struct mkroom *, int);
static int  maker(schar, schar, schar, schar);
static void makecorridors(void);
static void join(int, int);
static void make_niches(void);
static void makevtele(void);
static void makeniche(boolean);

void
makelevel(void)
{
	struct mkroom *croom, *troom;
	unsigned tryct;
	int x,y;

	nroom = 0;
	doorindex = 0;
	rooms[0].hx = -1;	/* in case we are in a maze */

	for(x=0; x<COLNO; x++) for(y=0; y<ROWNO; y++)
		levl[x][y] = zerorm;

	oinit();	/* assign level dependent obj probabilities */

	if(dlevel >= rn1(3, 26)) {	/* there might be several mazes */
		makemaz();
		return;
	}

	/* construct the rooms */
	nroom = 0;
	secret = FALSE;
	(void) makerooms();

	/* construct stairs (up and down in different rooms if possible) */
	croom = &rooms[rn2(nroom)];
	xdnstair = somex();
	ydnstair = somey();
	levl[(int)xdnstair][(int)ydnstair].scrsym ='>';
	levl[(int)xdnstair][(int)ydnstair].typ = STAIRS;
	if(nroom > 1) {
		troom = croom;
		croom = &rooms[rn2(nroom-1)];
		if(croom >= troom) croom++;
	}
	xupstair = somex();	/* %% < and > might be in the same place */
	yupstair = somey();
	levl[(int)xupstair][(int)yupstair].scrsym ='<';
	levl[(int)xupstair][(int)yupstair].typ = STAIRS;

	/* for each room: put things inside */
	for(croom = rooms; croom->hx > 0; croom++) {

		/* put a sleeping monster inside */
		/* Note: monster may be on the stairs. This cannot be
		   avoided: maybe the player fell through a trapdoor
		   while a monster was on the stairs. Conclusion:
		   we have to check for monsters on the stairs anyway. */
		if(!rn2(3)) (void)
			makemon((struct permonst *) 0, somex(), somey());

		/* put traps and mimics inside */
		goldseen = FALSE;
		while(!rn2(8-(dlevel/6))) mktrap(0,0,croom);
		if(!goldseen && !rn2(3)) mkgold(0L,somex(),somey());
		if(!rn2(3)) {
			(void) mkobj_at(0, somex(), somey());
			tryct = 0;
			while(!rn2(5)) {
				if(++tryct > 100){
					printf("tryct overflow4\n");
					break;
				}
				(void) mkobj_at(0, somex(), somey());
			}
		}
	}

	qsort((char *) rooms, nroom, sizeof(struct mkroom), comp);
	makecorridors();
	make_niches();

	/* make a secret treasure vault, not connected to the rest */
	if(nroom <= (2*MAXNROFROOMS/3)) if(rn2(3)) {
		troom = &rooms[nroom];
		secret = TRUE;
		if(makerooms()) {
			troom->rtype = VAULT;		/* treasure vault */
			for(x = troom->lx; x <= troom->hx; x++)
			for(y = troom->ly; y <= troom->hy; y++)
				mkgold((long)(rnd(dlevel*100) + 50), x, y);
			if(!rn2(3))
				makevtele();
		}
	}

#ifndef QUEST
#ifdef WIZARD
	if(wizard && getenv("SHOPTYPE")) mkshop(); else
#endif /* WIZARD */
 	if(dlevel > 1 && dlevel < 20 && rn2(dlevel) < 3) mkshop();
	else
	if(dlevel > 6 && !rn2(7)) mkzoo(ZOO);
	else
	if(dlevel > 9 && !rn2(5)) mkzoo(BEEHIVE);
	else
	if(dlevel > 11 && !rn2(6)) mkzoo(MORGUE);
	else
	if(dlevel > 18 && !rn2(6)) mkswamp();
#endif /* QUEST */
}

int
makerooms(void)
{
	struct rectangle *rsp;
	int lx, ly, hx, hy, lowx, lowy, hix, hiy, dx, dy;
	int tryct = 0, xlim, ylim;

	/* init */
	xlim = XLIM + secret;
	ylim = YLIM + secret;
	if(nroom == 0) {
		rsp = rs;
		rsp->rlx = rsp->rly = 0;
		rsp->rhx = COLNO-1;
		rsp->rhy = ROWNO-1;
		rsmax = 1;
	}
	rscnt = rsmax;

	/* make rooms until satisfied */
	while(rscnt > 0 && nroom < MAXNROFROOMS-1) {
		if(!secret && nroom > (MAXNROFROOMS/3) &&
		   !rn2((MAXNROFROOMS-nroom)*(MAXNROFROOMS-nroom)))
			return(0);

		/* pick a rectangle */
		rsp = &rs[rn2(rscnt)];
		hx = rsp->rhx;
		hy = rsp->rhy;
		lx = rsp->rlx;
		ly = rsp->rly;

		/* find size of room */
		if(secret)
			dx = dy = 1;
		else {
			dx = 2 + rn2((hx-lx-8 > 20) ? 12 : 8);
			dy = 2 + rn2(4);
			if(dx*dy > 50)
				dy = 50/dx;
		}

		/* look whether our room will fit */
		if(hx-lx < dx + dx/2 + 2*xlim || hy-ly < dy + dy/3 + 2*ylim) {
					/* no, too small */
					/* maybe we throw this area out */
			if(secret || !rn2(MAXNROFROOMS+1-nroom-tryct)) {
				rscnt--;
				rs[rsmax] = *rsp;
				*rsp = rs[rscnt];
				rs[rscnt] = rs[rsmax];
				tryct = 0;
			} else
				tryct++;
			continue;
		}

		lowx = lx + xlim + rn2(hx - lx - dx - 2*xlim + 1);
		lowy = ly + ylim + rn2(hy - ly - dy - 2*ylim + 1);
		hix = lowx + dx;
		hiy = lowy + dy;

		if(maker(lowx, dx, lowy, dy)) {
			if(secret)
				return(1);
			addrs(lowx-1, lowy-1, hix+1, hiy+1);
			tryct = 0;
		} else
			if(tryct++ > 100)
				break;
	}
	return(0);	/* failed to make vault - very strange */
}

static void
addrs(int lowx, int lowy, int hix, int hiy)
{
	struct rectangle *rsp;
	int lx,ly,hx,hy,xlim,ylim;
	boolean discarded;

	xlim = XLIM + secret;
	ylim = YLIM + secret;

	/* walk down since rscnt and rsmax change */
	for(rsp = &rs[rsmax-1]; rsp >= rs; rsp--) {
		
		if((lx = rsp->rlx) > hix || (ly = rsp->rly) > hiy ||
		   (hx = rsp->rhx) < lowx || (hy = rsp->rhy) < lowy)
			continue;
		if((discarded = (rsp >= &rs[rscnt]))) {
			*rsp = rs[--rsmax];
		} else {
			rsmax--;
			rscnt--;
			*rsp = rs[rscnt];
			if(rscnt != rsmax)
				rs[rscnt] = rs[rsmax];
		}
		if(lowy - ly > 2*ylim + 4)
			addrsx(lx,ly,hx,lowy-2,discarded);
		if(lowx - lx > 2*xlim + 4)
			addrsx(lx,ly,lowx-2,hy,discarded);
		if(hy - hiy > 2*ylim + 4)
			addrsx(lx,hiy+2,hx,hy,discarded);
		if(hx - hix > 2*xlim + 4)
			addrsx(hix+2,ly,hx,hy,discarded);
	}
}

static void
addrsx(int lx, int ly, int hx, int hy, boolean discarded)
/* boolean discarded;		 piece of a discarded area */
{
	struct rectangle *rsp;

	/* check inclusions */
	for(rsp = rs; rsp < &rs[rsmax]; rsp++) {
		if(lx >= rsp->rlx && hx <= rsp->rhx &&
		   ly >= rsp->rly && hy <= rsp->rhy)
			return;
	}

	/* make a new entry */
	if(rsmax >= MAXRS) {
#ifdef WIZARD
		if(wizard) pline("MAXRS may be too small.");
#endif /* WIZARD */
		return;
	}
	rsmax++;
	if(!discarded) {
		*rsp = rs[rscnt];
		rsp = &rs[rscnt];
		rscnt++;
	}
	rsp->rlx = lx;
	rsp->rly = ly;
	rsp->rhx = hx;
	rsp->rhy = hy;
}

int
comp(const void *x, const void *y)
{
	if(((struct mkroom *)x)->lx < ((struct mkroom *)y)->lx)
		return(-1);
	return(((struct mkroom *)x)->lx > ((struct mkroom *)y)->lx);
}

static coord
finddpos(int xl, int yl, int xh, int yh)
{
	coord ff;
	int x,y;

	x = (xl == xh) ? xl : (xl + rn2(xh-xl+1));
	y = (yl == yh) ? yl : (yl + rn2(yh-yl+1));
	if(okdoor(x, y))
		goto gotit;

	for(x = xl; x <= xh; x++) for(y = yl; y <= yh; y++)
		if(okdoor(x, y))
			goto gotit;

	for(x = xl; x <= xh; x++) for(y = yl; y <= yh; y++)
		if(levl[x][y].typ == DOOR || levl[x][y].typ == SDOOR)
			goto gotit;
	/* cannot find something reasonable -- strange */
	x = xl;
	y = yh;
gotit:
	ff.x = x;
	ff.y = y;
	return(ff);
}

/* see whether it is allowable to create a door at [x,y] */
static int
okdoor(int x, int y)
{
	if(levl[x-1][y].typ == DOOR || levl[x+1][y].typ == DOOR ||
	   levl[x][y+1].typ == DOOR || levl[x][y-1].typ == DOOR ||
	   levl[x-1][y].typ == SDOOR || levl[x+1][y].typ == SDOOR ||
	   levl[x][y-1].typ == SDOOR || levl[x][y+1].typ == SDOOR ||
	   (levl[x][y].typ != HWALL && levl[x][y].typ != VWALL) ||
	   doorindex >= DOORMAX)
		return(0);
	return(1);
}

static void
dodoor(int x, int y, struct mkroom *aroom)
{
	if(doorindex >= DOORMAX) {
		impossible("DOORMAX exceeded?");
		return;
	}
	if(!okdoor(x,y) && nxcor)
		return;
	dosdoor(x,y,aroom,rn2(8) ? DOOR : SDOOR);
}

static void
dosdoor(int x, int y, struct mkroom *aroom, int type)
{
	struct mkroom *broom;
	int tmp;

	if(!IS_WALL(levl[x][y].typ))	/* avoid SDOORs with '+' as scrsym */
		type = DOOR;
	levl[x][y].typ = type;
	if(type == DOOR)
		levl[x][y].scrsym = '+';
	aroom->doorct++;
	broom = aroom+1;
	if(broom->hx < 0) tmp = doorindex; else
	for(tmp = doorindex; tmp > broom->fdoor; tmp--)
		doors[tmp] = doors[tmp-1];
	doorindex++;
	doors[tmp].x = x;
	doors[tmp].y = y;
	for( ; broom->hx >= 0; broom++) broom->fdoor++;
}

/* Only called from makerooms() */
static int
maker(schar lowx, schar ddx, schar lowy, schar ddy)
{
	struct mkroom *croom;
	int x, y, hix = lowx+ddx, hiy = lowy+ddy;
	int xlim = XLIM + secret, ylim = YLIM + secret;

	if(nroom >= MAXNROFROOMS) return(0);
	if(lowx < XLIM) lowx = XLIM;
	if(lowy < YLIM) lowy = YLIM;
	if(hix > COLNO-XLIM-1) hix = COLNO-XLIM-1;
	if(hiy > ROWNO-YLIM-1) hiy = ROWNO-YLIM-1;
chk:
	if(hix <= lowx || hiy <= lowy) return(0);

	/* check area around room (and make room smaller if necessary) */
	for(x = lowx - xlim; x <= hix + xlim; x++) {
		for(y = lowy - ylim; y <= hiy + ylim; y++) {
			if(levl[x][y].typ) {
#ifdef WIZARD
			    if(wizard && !secret)
				pline("Strange area [%d,%d] in maker().",x,y);
#endif /* WIZARD */
				if(!rn2(3)) return(0);
				if(x < lowx)
					lowx = x+xlim+1;
				else
					hix = x-xlim-1;
				if(y < lowy)
					lowy = y+ylim+1;
				else
					hiy = y-ylim-1;
				goto chk;
			}
		}
	}

	croom = &rooms[nroom];

	/* on low levels the room is lit (usually) */
	/* secret vaults are always lit */
	if((rnd(dlevel) < 10 && rn2(77)) || (ddx == 1 && ddy == 1)) {
		for(x = lowx-1; x <= hix+1; x++)
			for(y = lowy-1; y <= hiy+1; y++)
				levl[x][y].lit = 1;
		croom->rlit = 1;
	} else
		croom->rlit = 0;
	croom->lx = lowx;
	croom->hx = hix;
	croom->ly = lowy;
	croom->hy = hiy;
	croom->rtype = croom->doorct = croom->fdoor = 0;

	for(x = lowx-1; x <= hix+1; x++)
	    for(y = lowy-1; y <= hiy+1; y += (hiy-lowy+2)) {
		levl[x][y].scrsym = '-';
		levl[x][y].typ = HWALL;
	}
	for(x = lowx-1; x <= hix+1; x += (hix-lowx+2))
	    for(y = lowy; y <= hiy; y++) {
		levl[x][y].scrsym = '|';
		levl[x][y].typ = VWALL;
	}
	for(x = lowx; x <= hix; x++)
	    for(y = lowy; y <= hiy; y++) {
		levl[x][y].scrsym = '.';
		levl[x][y].typ = ROOM;
	}

	smeq[nroom] = nroom;
	croom++;
	croom->hx = -1;
	nroom++;
	return(1);
}

static void
makecorridors(void)
{
	int a,b;

	nxcor = 0;
	for(a = 0; a < nroom-1; a++)
		join(a, a+1);
	for(a = 0; a < nroom-2; a++)
	    if(smeq[a] != smeq[a+2])
		join(a, a+2);
	for(a = 0; a < nroom; a++)
	    for(b = 0; b < nroom; b++)
		if(smeq[a] != smeq[b])
		    join(a, b);
	if(nroom > 2)
	    for(nxcor = rn2(nroom) + 4; nxcor; nxcor--) {
		a = rn2(nroom);
		b = rn2(nroom-2);
		if(b >= a) b += 2;
		join(a, b);
	    }
}

static void
join(int a, int b)
{
	coord cc,tt;
	int tx, ty, xx, yy;
	struct rm *crm;
	struct mkroom *croom, *troom;
	int dx, dy, dix, diy, cct;

	croom = &rooms[a];
	troom = &rooms[b];

	/* find positions cc and tt for doors in croom and troom
	   and direction for a corridor between them */

	if(troom->hx < 0 || croom->hx < 0 || doorindex >= DOORMAX) return;
	if(troom->lx > croom->hx) {
		dx = 1;
		dy = 0;
		xx = croom->hx+1;
		tx = troom->lx-1;
		cc = finddpos(xx,croom->ly,xx,croom->hy);
		tt = finddpos(tx,troom->ly,tx,troom->hy);
	} else if(troom->hy < croom->ly) {
		dy = -1;
		dx = 0;
		yy = croom->ly-1;
		cc = finddpos(croom->lx,yy,croom->hx,yy);
		ty = troom->hy+1;
		tt = finddpos(troom->lx,ty,troom->hx,ty);
	} else if(troom->hx < croom->lx) {
		dx = -1;
		dy = 0;
		xx = croom->lx-1;
		tx = troom->hx+1;
		cc = finddpos(xx,croom->ly,xx,croom->hy);
		tt = finddpos(tx,troom->ly,tx,troom->hy);
	} else {
		dy = 1;
		dx = 0;
		yy = croom->hy+1;
		ty = troom->ly-1;
		cc = finddpos(croom->lx,yy,croom->hx,yy);
		tt = finddpos(troom->lx,ty,troom->hx,ty);
	}
	xx = cc.x;
	yy = cc.y;
	tx = tt.x - dx;
	ty = tt.y - dy;
	if(nxcor && levl[xx+dx][yy+dy].typ)
		return;
	dodoor(xx,yy,croom);

	cct = 0;
	while(xx != tx || yy != ty) {
	    xx += dx;
	    yy += dy;

	    /* loop: dig corridor at [xx,yy] and find new [xx,yy] */
	    if(cct++ > 500 || (nxcor && !rn2(35)))
		return;

	    if(xx == COLNO-1 || xx == 0 || yy == 0 || yy == ROWNO-1)
		return;		/* impossible */

	    crm = &levl[xx][yy];
	    if(!(crm->typ)) {
		if(rn2(100)) {
			crm->typ = CORR;
			crm->scrsym = CORR_SYM;
			if(nxcor && !rn2(50))
				(void) mkobj_at(ROCK_SYM, xx, yy);
		} else {
			crm->typ = SCORR;
			crm->scrsym = ' ';
		}
	    } else
	    if(crm->typ != CORR && crm->typ != SCORR) {
		/* strange ... */
		return;
	    }

	    /* find next corridor position */
	    dix = abs(xx-tx);
	    diy = abs(yy-ty);

	    /* do we have to change direction ? */
	    if(dy && dix > diy) {
		int ddx = (xx > tx) ? -1 : 1;

		crm = &levl[xx+ddx][yy];
		if(!crm->typ || crm->typ == CORR || crm->typ == SCORR) {
		    dx = ddx;
		    dy = 0;
		    continue;
		}
	    } else if(dx && diy > dix) {
		int ddy = (yy > ty) ? -1 : 1;

		crm = &levl[xx][yy+ddy];
		if(!crm->typ || crm->typ == CORR || crm->typ == SCORR) {
		    dy = ddy;
		    dx = 0;
		    continue;
		}
	    }

	    /* continue straight on? */
	    crm = &levl[xx+dx][yy+dy];
	    if(!crm->typ || crm->typ == CORR || crm->typ == SCORR)
		continue;

	    /* no, what must we do now?? */
	    if(dx) {
		dx = 0;
		dy = (ty < yy) ? -1 : 1;
		crm = &levl[xx+dx][yy+dy];
		if(!crm->typ || crm->typ == CORR || crm->typ == SCORR)
		    continue;
		dy = -dy;
		continue;
	    } else {
		dy = 0;
		dx = (tx < xx) ? -1 : 1;
		crm = &levl[xx+dx][yy+dy];
		if(!crm->typ || crm->typ == CORR || crm->typ == SCORR)
		    continue;
		dx = -dx;
		continue;
	    }
	}

	/* we succeeded in digging the corridor */
	dodoor(tt.x, tt.y, troom);

	if(smeq[a] < smeq[b])
		smeq[b] = smeq[a];
	else
		smeq[a] = smeq[b];
}

static void
make_niches(void)
{
	int ct = rnd(nroom/2 + 1);
	while(ct--) makeniche(FALSE);
}

static void
makevtele(void)
{
	makeniche(TRUE);
}

static void
makeniche(boolean with_trap)
{
	struct mkroom *aroom;
	struct rm *rm;
	int vct = 8;
	coord dd;
	int dy,xx,yy;
	struct trap *ttmp;

	if(doorindex < DOORMAX)
	  while(vct--) {
	    aroom = &rooms[rn2(nroom-1)];
	    if(aroom->rtype != 0) continue;	/* not an ordinary room */
	    if(aroom->doorct == 1 && rn2(5)) continue;
	    if(rn2(2)) {
		dy = 1;
		dd = finddpos(aroom->lx,aroom->hy+1,aroom->hx,aroom->hy+1);
	    } else {
		dy = -1;
		dd = finddpos(aroom->lx,aroom->ly-1,aroom->hx,aroom->ly-1);
	    }
	    xx = dd.x;
	    yy = dd.y;
	    if((rm = &levl[xx][yy+dy])->typ) continue;
	    if(with_trap || !rn2(4)) {
		rm->typ = SCORR;
		rm->scrsym = ' ';
		if(with_trap) {
		    ttmp = maketrap(xx, yy+dy, TELEP_TRAP);
		    ttmp->once = 1;
		    make_engr_at(xx, yy-dy, "ad ae?ar um");
		}
		dosdoor(xx, yy, aroom, SDOOR);
	    } else {
		rm->typ = CORR;
		rm->scrsym = CORR_SYM;
		if(rn2(7))
		    dosdoor(xx, yy, aroom, rn2(5) ? SDOOR : DOOR);
		else {
		    mksobj_at(SCR_TELEPORTATION, xx, yy+dy);
		    if(!rn2(3)) (void) mkobj_at(0, xx, yy+dy);
		}
	    }
	    return;
	}
}

/* make a trap somewhere (in croom if mazeflag = 0) */
void
mktrap(int num, int mazeflag, struct mkroom *croom)
{
	struct trap *ttmp;
	int kind,nopierc,nomimic,fakedoor,fakegold,tryct = 0;
	xchar mx,my;
	extern char fut_geno[];

	if(!num || num >= TRAPNUM) {
		nopierc = (dlevel < 4) ? 1 : 0;
		nomimic = (dlevel < 9 || goldseen ) ? 1 : 0;
		if(strchr(fut_geno, 'M')) nomimic = 1;
		kind = rn2(TRAPNUM - nopierc - nomimic);
		/* note: PIERC = 7, MIMIC = 8, TRAPNUM = 9 */
	} else kind = num;

	if(kind == MIMIC) {
		struct monst *mtmp;

		fakedoor = (!rn2(3) && !mazeflag);
		fakegold = (!fakedoor && !rn2(2));
		if(fakegold) goldseen = TRUE;
		do {
			if(++tryct > 200) return;
			if(fakedoor) {
				/* note: fakedoor maybe on actual door */
				if(rn2(2)){
					if(rn2(2))
						mx = croom->hx+1;
					else mx = croom->lx-1;
					my = somey();
				} else {
					if(rn2(2))
						my = croom->hy+1;
					else my = croom->ly-1;
					mx = somex();
				}
			} else if(mazeflag) {
				coord mm;
				mx = mm.x;
				my = mm.y;
			} else {
				mx = somex();
				my = somey();
			}
		} while(m_at(mx,my) || levl[(int)mx][(int)my].typ == STAIRS);
		if ((mtmp = makemon(PM_MIMIC,mx,my))) {
		    mtmp->mimic = 1;
		    mtmp->mappearance =
			fakegold ? '$' : fakedoor ? '+' :
			(mazeflag && rn2(2)) ? AMULET_SYM :
			"=/)%?![<>" [ rn2(9) ];
		}
		return;
	}

	do {
		if(++tryct > 200)
			return;
		if(mazeflag){
			extern coord mazexy();
			coord mm;
			mm = mazexy();
			mx = mm.x;
			my = mm.y;
		} else {
			mx = somex();
			my = somey();
		}
	} while(t_at(mx, my) || levl[(int)mx][(int)my].typ == STAIRS);
	ttmp = maketrap(mx, my, kind);
	if(mazeflag && !rn2(10) && ttmp->ttyp < PIERC)
		ttmp->tseen = 1;
}
