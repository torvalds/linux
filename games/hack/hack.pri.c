/*	$OpenBSD: hack.pri.c,v 1.13 2016/01/09 18:33:15 mestre Exp $	*/

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

#include <curses.h>
#include <stdio.h>
#include <stdlib.h>

#include "hack.h"

xchar scrlx, scrhx, scrly, scrhy;	/* corners of new area on screen */

extern char *hu_stat[];	/* in eat.c */
extern char *CD;

static void cornbot(int);

void
swallowed(void)
{
	char ulook[] = "|@|";
	ulook[1] = u.usym;

	cls();
	curs(u.ux-1, u.uy+1);
	fputs("/-\\", stdout);
	curx = u.ux+2;
	curs(u.ux-1, u.uy+2);
	fputs(ulook, stdout);
	curx = u.ux+2;
	curs(u.ux-1, u.uy+3);
	fputs("\\-/", stdout);
	curx = u.ux+2;
	u.udispl = 1;
	u.udisx = u.ux;
	u.udisy = u.uy;
}


boolean panicking;

void
panic(const char *str, ...)
{
	va_list ap;

	if(panicking++) exit(1);    /* avoid loops - this should never happen*/
	home();
	puts(" Suddenly, the dungeon collapses.");
	fputs(" ERROR:  ", stdout);
	va_start(ap, str);
	vprintf(str, ap);
	va_end(ap);
#ifdef DEBUG
#ifdef UNIX
	if(!fork())
		abort();	/* generate core dump */
#endif /* UNIX */
#endif /* DEBUG */
	more();			/* contains a fflush() */
	done("panicked");
}

void
atl(int x, int y, int ch)
{
	struct rm *crm = &levl[x][y];

	if(x<0 || x>COLNO-1 || y<0 || y>ROWNO-1){
		impossible("atl(%d,%d,%c)",x,y,ch);
		return;
	}
	if(crm->seen && crm->scrsym == ch) return;
	crm->scrsym = ch;
	crm->new = 1;
	on_scr(x,y);
}

void
on_scr(int x, int y)
{
	if(x < scrlx) scrlx = x;
	if(x > scrhx) scrhx = x;
	if(y < scrly) scrly = y;
	if(y > scrhy) scrhy = y;
}

/* call: (x,y) - display
 *	(-1,0) - close (leave last symbol)
 *	(-1,-1)- close (undo last symbol)
 *	(-1,let)-open: initialize symbol
 *	(-2,let)-change let
 */
void
tmp_at(schar x, schar y)
{
	static schar prevx, prevy;
	static char let;

	if((int)x == -2){	/* change let call */
		let = y;
		return;
	}
	if((int)x == -1 && (int)y >= 0){	/* open or close call */
		let = y;
		prevx = -1;
		return;
	}
	if(prevx >= 0 && cansee(prevx,prevy)) {
		delay_output(50);
		prl(prevx, prevy);	/* in case there was a monster */
		at(prevx, prevy, levl[(int)prevx][(int)prevy].scrsym);
	}
	if(x >= 0){	/* normal call */
		if(cansee(x,y)) at(x,y,let);
		prevx = x;
		prevy = y;
	} else {	/* close call */
		let = 0;
		prevx = -1;
	}
}

/* like the previous, but the symbols are first erased on completion */
void
Tmp_at(schar x, schar y)
{
	static char let;
	static xchar cnt;
	static coord tc[COLNO];		/* but watch reflecting beams! */
	int xx,yy;

	if((int)x == -1) {
		if(y > 0) {	/* open call */
			let = y;
			cnt = 0;
			return;
		}
		/* close call (do not distinguish y==0 and y==-1) */
		while(cnt--) {
			xx = tc[(int)cnt].x;
			yy = tc[(int)cnt].y;
			prl(xx, yy);
			at(xx, yy, levl[xx][yy].scrsym);
		}
		cnt = let = 0;	/* superfluous */
		return;
	}
	if((int)x == -2) {	/* change let call */
		let = y;
		return;
	}
	/* normal call */
	if(cansee(x,y)) {
		if(cnt) delay_output(50);
		at(x,y,let);
		tc[(int)cnt].x = x;
		tc[(int)cnt].y = y;
		if(++cnt >= COLNO) panic("Tmp_at overflow?");
		levl[(int)x][(int)y].new = 0;	/* prevent pline-nscr erasing --- */
	}
}

void
setclipped(void)
{
	error("Hack needs a screen of size at least %d by %d.\n",
		ROWNO+2, COLNO);
}

void
at(xchar x, xchar y, char ch)
{
	if(x < 0 || x > COLNO-1 || y < 0 || y > ROWNO-1) {
		impossible("At gets 0%o at %d %d.", ch, x, y);
		return;
	}
	if(!ch) {
		impossible("At gets null at %d %d.", x, y);
		return;
	}
	y += 2;
	curs(x,y);
	(void) putchar(ch);
	curx++;
}

void
prme(void)
{
	if(!Invisible) at(u.ux,u.uy,u.usym);
}

int
doredraw(void)
{
	docrt();
	return(0);
}

void
docrt(void)
{
	int x,y;
	struct rm *room;
	struct monst *mtmp;

	if(u.uswallow) {
		swallowed();
		return;
	}
	cls();

/* Some ridiculous code to get display of @ and monsters (almost) right */
	if(!Invisible) {
		levl[(int)(u.udisx = u.ux)][(int)(u.udisy = u.uy)].scrsym = u.usym;
		levl[(int)u.udisx][(int)u.udisy].seen = 1;
		u.udispl = 1;
	} else	u.udispl = 0;

	seemons();	/* reset old positions */
	for(mtmp = fmon; mtmp; mtmp = mtmp->nmon)
		mtmp->mdispl = 0;
	seemons();	/* force new positions to be shown */
/* This nonsense should disappear soon --------------------------------- */

	for(y = 0; y < ROWNO; y++)
		for(x = 0; x < COLNO; x++)
			if((room = &levl[x][y])->new) {
				room->new = 0;
				at(x,y,room->scrsym);
			} else if(room->seen)
				at(x,y,room->scrsym);
	scrlx = COLNO;
	scrly = ROWNO;
	scrhx = scrhy = 0;
	flags.botlx = 1;
	bot();
}

void
docorner(int xmin, int ymax)
{
	int x,y;
	struct rm *room;
	struct monst *mtmp;

	if(u.uswallow) {	/* Can be done more efficiently */
		swallowed();
		return;
	}

	seemons();	/* reset old positions */
	for(mtmp = fmon; mtmp; mtmp = mtmp->nmon)
	    if(mtmp->mx >= xmin && mtmp->my < ymax)
		mtmp->mdispl = 0;
	seemons();	/* force new positions to be shown */

	for(y = 0; y < ymax; y++) {
		if(y > ROWNO && CD) break;
		curs(xmin,y+2);
		cl_end();
		if(y < ROWNO) {
		    for(x = xmin; x < COLNO; x++) {
			if((room = &levl[x][y])->new) {
				room->new = 0;
				at(x,y,room->scrsym);
			} else
				if(room->seen)
					at(x,y,room->scrsym);
		    }
		}
	}
	if(ymax > ROWNO) {
		cornbot(xmin-1);
		if(ymax > ROWNO+1 && CD) {
			curs(1,ROWNO+3);
			cl_eos();
		}
	}
}

void
curs_on_u(void)
{
	curs(u.ux, u.uy+2);
}

void
pru(void)
{
	if(u.udispl && (Invisible || u.udisx != u.ux || u.udisy != u.uy))
		/* if(! levl[u.udisx][u.udisy].new) */
			if(!vism_at(u.udisx, u.udisy))
				newsym(u.udisx, u.udisy);
	if(Invisible) {
		u.udispl = 0;
		prl(u.ux,u.uy);
	} else
	if(!u.udispl || u.udisx != u.ux || u.udisy != u.uy) {
		atl(u.ux, u.uy, u.usym);
		u.udispl = 1;
		u.udisx = u.ux;
		u.udisy = u.uy;
	}
	levl[(int)u.ux][(int)u.uy].seen = 1;
}

#ifndef NOWORM
extern struct wseg *m_atseg;
#endif /* NOWORM */

/* print a position that is visible for @ */
void
prl(int x, int y)
{
	struct rm *room;
	struct monst *mtmp;
	struct obj *otmp;

	if(x == u.ux && y == u.uy && (!Invisible)) {
		pru();
		return;
	}
	if(!isok(x,y)) return;
	room = &levl[x][y];
	if((!room->typ) ||
	   (IS_ROCK(room->typ) && levl[(int)u.ux][(int)u.uy].typ == CORR))
		return;
	if((mtmp = m_at(x,y)) && !mtmp->mhide &&
		(!mtmp->minvis || See_invisible)) {
#ifndef NOWORM
		if(m_atseg)
			pwseg(m_atseg);
		else
#endif /* NOWORM */
		pmon(mtmp);
	}
	else if((otmp = o_at(x,y)) && room->typ != POOL)
		atl(x,y,otmp->olet);
	else if(mtmp && (!mtmp->minvis || See_invisible)) {
		/* must be a hiding monster, but not hiding right now */
		/* assume for the moment that long worms do not hide */
		pmon(mtmp);
	}
	else if(g_at(x,y) && room->typ != POOL)
		atl(x,y,'$');
	else if(!room->seen || room->scrsym == ' ') {
		room->new = room->seen = 1;
		newsym(x,y);
		on_scr(x,y);
	}
	room->seen = 1;
}

char
news0(xchar x, xchar y)
{
	struct obj *otmp;
	struct trap *ttmp;
	struct rm *room;
	char tmp;

	room = &levl[(int)x][(int)y];
	if(!room->seen) tmp = ' ';
	else if(room->typ == POOL) tmp = POOL_SYM;
	else if(!Blind && (otmp = o_at(x,y))) tmp = otmp->olet;
	else if(!Blind && g_at(x,y)) tmp = '$';
	else if(x == xupstair && y == yupstair) tmp = '<';
	else if(x == xdnstair && y == ydnstair) tmp = '>';
	else if((ttmp = t_at(x,y)) && ttmp->tseen) tmp = '^';
	else switch(room->typ) {
	case SCORR:
	case SDOOR:
		tmp = room->scrsym;	/* %% wrong after killing mimic ! */
		break;
	case HWALL:
		tmp = '-';
		break;
	case VWALL:
		tmp = '|';
		break;
	case LDOOR:
	case DOOR:
		tmp = '+';
		break;
	case CORR:
		tmp = CORR_SYM;
		break;
	case ROOM:
		if(room->lit || cansee(x,y) || Blind) tmp = '.';
		else tmp = ' ';
		break;
/*
	case POOL:
		tmp = POOL_SYM;
		break;
*/
	default:
		tmp = ERRCHAR;
	}
	return(tmp);
}

void
newsym(int x, int y)
{
	atl(x,y,news0(x,y));
}

/* used with wand of digging (or pick-axe): fill scrsym and force display */
/* also when a POOL evaporates */
void
mnewsym(int x, int y)
{
	struct rm *room;
	char newscrsym;

	if(!vism_at(x,y)) {
		room = &levl[x][y];
		newscrsym = news0(x,y);
		if(room->scrsym != newscrsym) {
			room->scrsym = newscrsym;
			room->seen = 0;
		}
	}
}

void
nosee(int x, int y)
{
	struct rm *room;

	if(!isok(x,y)) return;
	room = &levl[x][y];
	if(room->scrsym == '.' && !room->lit && !Blind) {
		room->scrsym = ' ';
		room->new = 1;
		on_scr(x,y);
	}
}

#ifndef QUEST
void
prl1(int x, int y)
{
	if(u.dx) {
		if(u.dy) {
			prl(x-(2*u.dx),y);
			prl(x-u.dx,y);
			prl(x,y);
			prl(x,y-u.dy);
			prl(x,y-(2*u.dy));
		} else {
			prl(x,y-1);
			prl(x,y);
			prl(x,y+1);
		}
	} else {
		prl(x-1,y);
		prl(x,y);
		prl(x+1,y);
	}
}

void
nose1(int x, int y)
{
	if(u.dx) {
		if(u.dy) {
			nosee(x,u.uy);
			nosee(x,u.uy-u.dy);
			nosee(x,y);
			nosee(u.ux-u.dx,y);
			nosee(u.ux,y);
		} else {
			nosee(x,y-1);
			nosee(x,y);
			nosee(x,y+1);
		}
	} else {
		nosee(x-1,y);
		nosee(x,y);
		nosee(x+1,y);
	}
}
#endif /* QUEST */

int
vism_at(int x, int y)
{
	struct monst *mtmp;

	return((x == u.ux && y == u.uy && !Invisible)
			? 1 :
	       (mtmp = m_at(x,y))
			? ((Blind && Telepat) || canseemon(mtmp)) :
		0);
}

void
unpobj(struct obj *obj)
{
/* 	if(obj->odispl){
		if(!vism_at(obj->odx, obj->ody))
			newsym(obj->odx, obj->ody);
		obj->odispl = 0;
	}
*/
	if(!vism_at(obj->ox,obj->oy))
		newsym(obj->ox,obj->oy);
}

void
seeobjs(void)
{
	struct obj *obj, *obj2;

	for(obj = fobj; obj; obj = obj2) {
		obj2 = obj->nobj;
		if(obj->olet == FOOD_SYM && obj->otyp >= CORPSE
			&& obj->age + 250 < moves)
				delobj(obj);
	}
	for(obj = invent; obj; obj = obj2) {
		obj2 = obj->nobj;
		if(obj->olet == FOOD_SYM && obj->otyp >= CORPSE
			&& obj->age + 250 < moves)
				useup(obj);
	}
}

void
seemons(void)
{
	struct monst *mtmp;

	for(mtmp = fmon; mtmp; mtmp = mtmp->nmon){
		if(mtmp->data->mlet == ';')
			mtmp->minvis = (u.ustuck != mtmp &&
					levl[(int)mtmp->mx][(int)mtmp->my].typ == POOL);
		pmon(mtmp);
#ifndef NOWORM
		if(mtmp->wormno) wormsee(mtmp->wormno);
#endif /* NOWORM */
	}
}

void
pmon(struct monst *mon)
{
	int show = (Blind && Telepat) || canseemon(mon);

	if(mon->mdispl){
		if(mon->mdx != mon->mx || mon->mdy != mon->my || !show)
			unpmon(mon);
	}
	if(show && !mon->mdispl){
		atl(mon->mx,mon->my,
		 (!mon->mappearance
		  || u.uprops[PROP(RIN_PROTECTION_FROM_SHAPE_CHANGERS)].p_flgs
		 ) ? mon->data->mlet : mon->mappearance);
		mon->mdispl = 1;
		mon->mdx = mon->mx;
		mon->mdy = mon->my;
	}
}

void
unpmon(struct monst *mon)
{
	if(mon->mdispl){
		newsym(mon->mdx, mon->mdy);
		mon->mdispl = 0;
	}
}

void
nscr(void)
{
	int x,y;
	struct rm *room;

	if(u.uswallow || u.ux == FAR || flags.nscrinh) return;
	pru();
	for(y = scrly; y <= scrhy; y++)
		for(x = scrlx; x <= scrhx; x++)
			if((room = &levl[x][y])->new) {
				room->new = 0;
				at(x,y,room->scrsym);
			}
	scrhx = scrhy = 0;
	scrlx = COLNO;
	scrly = ROWNO;
}

/* 100 suffices for bot(); no relation with COLNO */
char oldbot[100], newbot[100];

static void
cornbot(int lth)
{
	if(lth < sizeof(oldbot)) {
		oldbot[lth] = 0;
		flags.botl = 1;
	}
}

void
bot(void)
{
	char *ob = oldbot, *nb = newbot, *bp;
	int i;

	if(flags.botlx) *ob = 0;
	flags.botl = flags.botlx = 0;
#ifdef GOLD_ON_BOTL
	(void) snprintf(newbot, sizeof newbot,
		"Level %-2d  Gold %-5lu  Hp %3d(%d)  Ac %-2d  Str ",
		dlevel, u.ugold, u.uhp, u.uhpmax, u.uac);
#else
	(void) snprintf(newbot, sizeof newbot,
		"Level %-2d   Hp %3d(%d)   Ac %-2d   Str ",
		dlevel,  u.uhp, u.uhpmax, u.uac);
#endif /* GOLD_ON_BOTL */
	if(u.ustr>18) {
	    if(u.ustr>117)
		(void) strlcat(newbot,"18/**",sizeof newbot);
	    else {
		bp = eos(newbot);
		(void) snprintf(bp, newbot + sizeof newbot - bp,
		  "18/%02d",u.ustr-18);
	    }
	} else {
	    bp = eos(newbot);
	    (void) snprintf(bp, newbot + sizeof newbot - bp, "%-2d   ",u.ustr);
	}
	bp = eos(newbot);
#ifdef EXP_ON_BOTL
	(void) snprintf(bp, newbot + sizeof newbot - bp,
	  "  Exp %2d/%-5lu ", u.ulevel,u.uexp);
#else
	(void) snprintf(bp, newbot + sizeof newbot - bp,
	  "   Exp %2u  ", u.ulevel);
#endif /* EXP_ON_BOTL */
	(void) strlcat(newbot, hu_stat[u.uhs], sizeof newbot);
	if(flags.time) {
	    bp = eos(newbot);
	    (void) snprintf(bp, newbot + sizeof newbot - bp, "  %ld", moves);
	}
	if(strlen(newbot) >= COLNO) {
		char *bp0, *bp1;
		bp0 = bp1 = newbot;
		do {
			if(*bp0 != ' ' || bp0[1] != ' ' || bp0[2] != ' ')
				*bp1++ = *bp0;
		} while(*bp0++);
	}
	for(i = 1; i<COLNO; i++) {
		if(*ob != *nb){
			curs(i,ROWNO+2);
			(void) putchar(*nb ? *nb : ' ');
			curx++;
		}
		if(*ob) ob++;
		if(*nb) nb++;
	}
	(void) strlcpy(oldbot, newbot, sizeof oldbot);
}

#ifdef WAN_PROBING
mstatusline(mtmp) struct monst *mtmp; {
	pline("Status of %s: ", monnam(mtmp));
	pline("Level %-2d  Gold %-5lu  Hp %3d(%d)  Ac %-2d  Dam %d",
	    mtmp->data->mlevel, mtmp->mgold, mtmp->mhp, mtmp->mhpmax,
	    mtmp->data->ac, (mtmp->data->damn + 1) * (mtmp->data->damd + 1));
}
#endif /* WAN_PROBING */

void
cls(void)
{
	if(flags.toplin == 1)
		more();
	flags.toplin = 0;

	clr_screen();

	flags.botlx = 1;
}
