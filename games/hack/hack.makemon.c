/*	$OpenBSD: hack.makemon.c,v 1.9 2023/09/06 11:53:56 jsg Exp $	*/

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

extern char fut_geno[];
struct monst zeromonst;

/*
 * called with [x,y] = coordinates;
 *	[0,0] means anyplace
 *	[u.ux,u.uy] means: call mnexto (if !in_mklev)
 *
 *	In case we make an Orc or killer bee, we make an entire horde (swarm);
 *	note that in this case we return only one of them (the one at [x,y]).
 */
struct monst *
makemon(struct permonst *ptr, int x, int y)
{
	struct monst *mtmp;
	int tmp, ct;
	boolean anything = (!ptr);
	extern boolean in_mklev;

	if(x != 0 || y != 0) if(m_at(x,y)) return((struct monst *) 0);
	if(ptr){
		if(strchr(fut_geno, ptr->mlet)) return((struct monst *) 0);
	} else {
		ct = CMNUM - strlen(fut_geno);
		if(strchr(fut_geno, 'm')) ct++;  /* make only 1 minotaur */
		if(strchr(fut_geno, '@')) ct++;
		if(ct <= 0) return(0); 		  /* no more monsters! */
		tmp = rn2(ct*dlevel/24 + 7);
		if(tmp < dlevel - 4) tmp = rn2(ct*dlevel/24 + 12);
		if(tmp >= ct) tmp = rn1(ct - ct/2, ct/2);
		for(ct = 0; ct < CMNUM; ct++){
			ptr = &mons[ct];
			if(strchr(fut_geno, ptr->mlet))
				continue;
			if(!tmp--) goto gotmon;
		}
		panic("makemon?");
	}
gotmon:
	mtmp = newmonst(ptr->pxlth);
	*mtmp = zeromonst;	/* clear all entries in structure */
	for(ct = 0; ct < ptr->pxlth; ct++)
		((char *) &(mtmp->mextra[0]))[ct] = 0;
	mtmp->nmon = fmon;
	fmon = mtmp;
	mtmp->m_id = flags.ident++;
	mtmp->data = ptr;
	mtmp->mxlth = ptr->pxlth;
	if(ptr->mlet == 'D') mtmp->mhpmax = mtmp->mhp = 80;
	else if(!ptr->mlevel) mtmp->mhpmax = mtmp->mhp = rnd(4);
	else mtmp->mhpmax = mtmp->mhp = d(ptr->mlevel, 8);
	mtmp->mx = x;
	mtmp->my = y;
	mtmp->mcansee = 1;
	if(ptr->mlet == 'M'){
		mtmp->mimic = 1;
		mtmp->mappearance = ']';
	}
	if(!in_mklev) {
		if(x == u.ux && y == u.uy && ptr->mlet != ' ')
			mnexto(mtmp);
		if(x == 0 && y == 0)
			rloc(mtmp);
	}
	if(ptr->mlet == 's' || ptr->mlet == 'S') {
		mtmp->mhide = mtmp->mundetected = 1;
		if(in_mklev)
		if(mtmp->mx && mtmp->my)
			(void) mkobj_at(0, mtmp->mx, mtmp->my);
	}
	if(ptr->mlet == ':') {
		mtmp->cham = 1;
		(void) newcham(mtmp, &mons[dlevel+14+rn2(CMNUM-14-dlevel)]);
	}
	if(ptr->mlet == 'I' || ptr->mlet == ';')
		mtmp->minvis = 1;
	if(ptr->mlet == 'L' || ptr->mlet == 'N'
	    || (in_mklev && strchr("&w;", ptr->mlet) && rn2(5))
	) mtmp->msleep = 1;

#ifndef NOWORM
	if(ptr->mlet == 'w' && getwn(mtmp))
		initworm(mtmp);
#endif /* NOWORM */

	if(anything) if(ptr->mlet == 'O' || ptr->mlet == 'k') {
		coord mm;
		int cnt = rnd(10);
		mm.x = x;
		mm.y = y;
		while(cnt--) {
			mm = enexto(mm.x, mm.y);
			(void) makemon(ptr, mm.x, mm.y);
		}
	}

	return(mtmp);
}

coord
enexto(xchar xx, xchar yy)
{
	xchar x,y;
	coord foo[15], *tfoo;
	int range;

	tfoo = foo;
	range = 1;
	do {	/* full kludge action. */
		for(x = xx-range; x <= xx+range; x++)
			if(goodpos(x, yy-range)) {
				tfoo->x = x;
				tfoo++->y = yy-range;
				if(tfoo == &foo[15]) goto foofull;
			}
		for(x = xx-range; x <= xx+range; x++)
			if(goodpos(x,yy+range)) {
				tfoo->x = x;
				tfoo++->y = yy+range;
				if(tfoo == &foo[15]) goto foofull;
			}
		for(y = yy+1-range; y < yy+range; y++)
			if(goodpos(xx-range,y)) {
				tfoo->x = xx-range;
				tfoo++->y = y;
				if(tfoo == &foo[15]) goto foofull;
			}
		for(y = yy+1-range; y < yy+range; y++)
			if(goodpos(xx+range,y)) {
				tfoo->x = xx+range;
				tfoo++->y = y;
				if(tfoo == &foo[15]) goto foofull;
			}
		range++;
	} while(tfoo == foo);
foofull:
	return( foo[rn2(tfoo-foo)] );
}

/* used only in mnexto and rloc */
int
goodpos(int x, int y)
{
	return(
	! (x < 1 || x > COLNO-2 || y < 1 || y > ROWNO-2 ||
	   m_at(x,y) || !ACCESSIBLE(levl[x][y].typ)
	   || (x == u.ux && y == u.uy)
	   || sobj_at(ENORMOUS_ROCK, x, y)
	));
}

void
rloc(struct monst *mtmp)
{
	int tx,ty;
	char ch = mtmp->data->mlet;

#ifndef NOWORM
	if(ch == 'w' && mtmp->mx) return;	/* do not relocate worms */
#endif /* NOWORM */
	do {
		tx = rn1(COLNO-3,2);
		ty = rn2(ROWNO);
	} while(!goodpos(tx,ty));
	mtmp->mx = tx;
	mtmp->my = ty;
	if(u.ustuck == mtmp){
		if(u.uswallow) {
			u.ux = tx;
			u.uy = ty;
			docrt();
		} else	u.ustuck = 0;
	}
	pmon(mtmp);
}

struct monst *
mkmon_at(char let, int x, int y)
{
	int ct;
	struct permonst *ptr;

	for(ct = 0; ct < CMNUM; ct++) {
		ptr = &mons[ct];
		if(ptr->mlet == let)
			return(makemon(ptr,x,y));
	}
	return(NULL);
}
