/*	$OpenBSD: hack.search.c,v 1.6 2016/01/09 18:33:15 mestre Exp $	*/

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

/* returns number of things found */
int
findit(void)
{
	int num;
	xchar zx,zy;
	struct trap *ttmp;
	struct monst *mtmp;
	xchar lx,hx,ly,hy;

	if(u.uswallow) return(0);
	for(lx = u.ux; (num = levl[lx-1][(int)u.uy].typ) && num != CORR; lx--) ;
	for(hx = u.ux; (num = levl[hx+1][(int)u.uy].typ) && num != CORR; hx++) ;
	for(ly = u.uy; (num = levl[(int)u.ux][ly-1].typ) && num != CORR; ly--) ;
	for(hy = u.uy; (num = levl[(int)u.ux][hy+1].typ) && num != CORR; hy++) ;
	num = 0;
	for(zy = ly; zy <= hy; zy++)
		for(zx = lx; zx <= hx; zx++) {
			if (levl[(int)zx][(int)zy].typ == SDOOR) {
				levl[(int)zx][(int)zy].typ = DOOR;
				atl(zx, zy, '+');
				num++;
			} else if (levl[(int)zx][(int)zy].typ == SCORR) {
				levl[(int)zx][(int)zy].typ = CORR;
				atl(zx, zy, CORR_SYM);
				num++;
			} else if ((ttmp = t_at(zx, zy))) {
				if(ttmp->ttyp == PIERC){
					(void) makemon(PM_PIERCER, zx, zy);
					num++;
					deltrap(ttmp);
				} else if (!ttmp->tseen) {
					ttmp->tseen = 1;
					if(!vism_at(zx, zy))
						atl(zx,zy,'^');
					num++;
				}
			} else if ((mtmp = m_at(zx,zy)) && (mtmp->mimic)) {
				seemimic(mtmp);
				num++;
			}
		}
	return(num);
}

int
dosearch(void)
{
	xchar x,y;
	struct trap *trap;
	struct monst *mtmp;

	if(u.uswallow)
		pline("What are you looking for? The exit?");
	else
	for(x = u.ux-1; x < u.ux+2; x++)
	for(y = u.uy-1; y < u.uy+2; y++) if(x != u.ux || y != u.uy) {
		if(levl[(int)x][(int)y].typ == SDOOR) {
			if(rn2(7)) continue;
			levl[(int)x][(int)y].typ = DOOR;
			levl[(int)x][(int)y].seen = 0;	/* force prl */
			prl(x,y);
			nomul(0);
		} else if(levl[(int)x][(int)y].typ == SCORR) {
			if(rn2(7)) continue;
			levl[(int)x][(int)y].typ = CORR;
			levl[(int)x][(int)y].seen = 0;	/* force prl */
			prl(x,y);
			nomul(0);
		} else {
		/* Be careful not to find anything in an SCORR or SDOOR */
			if ((mtmp = m_at(x,y)))
				if(mtmp->mimic){
					seemimic(mtmp);
					pline("You find a mimic.");
					return(1);
				}
			for(trap = ftrap; trap; trap = trap->ntrap)
			if(trap->tx == x && trap->ty == y &&
			   !trap->tseen && !rn2(8)) {
				nomul(0);
				pline("You find a%s.", traps[trap->ttyp]);
				if(trap->ttyp == PIERC) {
					deltrap(trap);
					(void) makemon(PM_PIERCER,x,y);
					return(1);
				}
				trap->tseen = 1;
				if(!vism_at(x,y)) atl(x,y,'^');
			}
		}
	}
	return(1);
}

int
doidtrap(void)
{
	struct trap *trap;
	int x,y;

	if(!getdir(1)) return(0);
	x = u.ux + u.dx;
	y = u.uy + u.dy;
	for(trap = ftrap; trap; trap = trap->ntrap)
		if(trap->tx == x && trap->ty == y && trap->tseen) {
		    if(u.dz)
			if((u.dz < 0) != (!xdnstair && trap->ttyp == TRAPDOOR))
			    continue;
		    pline("That is a%s.", traps[trap->ttyp]);
		    return(0);
		}
	pline("I can't see a trap there.");
	return(0);
}

void
wakeup(struct monst *mtmp)
{
	mtmp->msleep = 0;
	setmangry(mtmp);
	if(mtmp->mimic) seemimic(mtmp);
}

/* NOTE: we must check if(mtmp->mimic) before calling this routine */
void
seemimic(struct monst *mtmp)
{
		mtmp->mimic = 0;
		mtmp->mappearance = 0;
		unpmon(mtmp);
		pmon(mtmp);
}
