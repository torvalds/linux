/*	$OpenBSD: hack.bones.c,v 1.12 2023/06/03 15:19:38 op Exp $	*/

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

#include <unistd.h>

#include "hack.h"

extern char plname[PL_NSIZ];
extern struct permonst pm_ghost;

char bones[] = "bones_xx";

/* save bones and possessions of a deceased adventurer */
void
savebones(void)
{
	int fd;
	struct obj *otmp;
	struct trap *ttmp;
	struct monst *mtmp;

	if(dlevel <= 0 || dlevel > MAXLEVEL) return;
	if(!rn2(1 + dlevel/2)) return;	/* not so many ghosts on low levels */
	bones[6] = '0' + (dlevel/10);
	bones[7] = '0' + (dlevel%10);
	if((fd = open(bones, O_RDONLY)) >= 0){
		(void) close(fd);
		return;
	}
	/* drop everything; the corpse's possessions are usually cursed */
	otmp = invent;
	while(otmp){
		otmp->ox = u.ux;
		otmp->oy = u.uy;
		otmp->age = 0;		/* very long ago */
		otmp->owornmask = 0;
		if(rn2(5)) otmp->cursed = 1;
		if(!otmp->nobj){
			otmp->nobj = fobj;
			fobj = invent;
			invent = 0;	/* superfluous */
			break;
		}
		otmp = otmp->nobj;
	}
	if(!(mtmp = makemon(PM_GHOST, u.ux, u.uy))) return;
	mtmp->mx = u.ux;
	mtmp->my = u.uy;
	mtmp->msleep = 1;
	(void) strlcpy((char *) mtmp->mextra, plname, mtmp->mxlth);
	mkgold(somegold() + d(dlevel,30), u.ux, u.uy);
	for(mtmp = fmon; mtmp; mtmp = mtmp->nmon){
		mtmp->m_id = 0;
		if(mtmp->mtame) {
			mtmp->mtame = 0;
			mtmp->mpeaceful = 0;
		}
		mtmp->mlstmv = 0;
		if(mtmp->mdispl) unpmon(mtmp);
	}
	for(ttmp = ftrap; ttmp; ttmp = ttmp->ntrap)
		ttmp->tseen = 0;
	for(otmp = fobj; otmp; otmp = otmp->nobj) {
		otmp->o_id = 0;
	     /* otmp->o_cnt_id = 0; - superfluous */
		otmp->onamelth = 0;
		otmp->known = 0;
		otmp->invlet = 0;
		if(otmp->olet == AMULET_SYM && !otmp->spe) {
			otmp->spe = -1;      /* no longer the actual amulet */
			otmp->cursed = 1;    /* flag as gotten from a ghost */
		}
	}
	if((fd = open(bones, O_CREAT | O_TRUNC | O_WRONLY, FMASK)) == -1) return;
	savelev(fd,dlevel);
	(void) close(fd);
}

int
getbones(void)
{
	int fd,x,y;

	if(rn2(3)) return(0);	/* only once in three times do we find bones */
	bones[6] = '0' + dlevel/10;
	bones[7] = '0' + dlevel%10;
	if((fd = open(bones, O_RDONLY)) == -1) return(0);
	getlev(fd, 0, dlevel);
	for(x = 0; x < COLNO; x++) for(y = 0; y < ROWNO; y++)
		levl[x][y].seen = levl[x][y].new = 0;
	(void) close(fd);
#ifdef WIZARD
	if(!wizard)	/* duvel!frans: don't remove bones while debugging */
#endif /* WiZARD */
	    if(unlink(bones) == -1){
		pline("Cannot unlink %s .", bones);
		return(0);
	}
	return(1);
}
