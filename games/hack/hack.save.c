/*	$OpenBSD: hack.save.c,v 1.14 2019/06/28 13:32:52 deraadt Exp $	*/

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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "hack.h"

extern char genocided[60];	/* defined in Decl.c */
extern char fut_geno[60];	/* idem */
extern char SAVEF[], nul[];
extern char pl_character[PL_CSIZ];

static int dosave0(int);

int
dosave(void)
{
	if(dosave0(0)) {
		settty("Be seeing you ...\n");
		exit(0);
	}
	return(0);
}

void
hackhangup(int notused)
{
	(void) dosave0(1);
	exit(1);
}

/* returns 1 if save successful */
static int
dosave0(int hu)
{
	int fd, ofd;
	int tmp;		/* not register ! */

	(void) signal(SIGHUP, SIG_IGN);
	(void) signal(SIGINT, SIG_IGN);
	if((fd = open(SAVEF, O_CREAT | O_TRUNC | O_WRONLY, FMASK)) == -1) {
		if(!hu) pline("Cannot open save file. (Continue or Quit)");
		(void) unlink(SAVEF);		/* ab@unido */
		return(0);
	}
	if(flags.moonphase == FULL_MOON)	/* ut-sally!fletcher */
		u.uluck--;			/* and unido!ab */
	savelev(fd,dlevel);
	saveobjchn(fd, invent);
	saveobjchn(fd, fcobj);
	savemonchn(fd, fallen_down);
	tmp = getuid();
	bwrite(fd, &tmp, sizeof tmp);
	bwrite(fd, &flags, sizeof(struct flag));
	bwrite(fd, &dlevel, sizeof dlevel);
	bwrite(fd, &maxdlevel, sizeof maxdlevel);
	bwrite(fd, &moves, sizeof moves);
	bwrite(fd, &u, sizeof(struct you));
	if(u.ustuck)
		bwrite(fd, &(u.ustuck->m_id), sizeof u.ustuck->m_id);
	bwrite(fd, pl_character, sizeof pl_character);
	bwrite(fd, genocided, sizeof genocided);
	bwrite(fd, fut_geno, sizeof fut_geno);
	savenames(fd);
	for(tmp = 1; tmp <= maxdlevel; tmp++) {
		extern int hackpid;
		extern boolean level_exists[];

		if(tmp == dlevel || !level_exists[tmp]) continue;
		glo(tmp);
		if((ofd = open(lock, O_RDONLY)) == -1) {
		    if(!hu) pline("Error while saving: cannot read %s.", lock);
		    (void) close(fd);
		    (void) unlink(SAVEF);
		    if(!hu) done("tricked");
		    return(0);
		}
		getlev(ofd, hackpid, tmp);
		(void) close(ofd);
		bwrite(fd, &tmp, sizeof tmp);		/* level number */
		savelev(fd,tmp);			/* actual level */
		(void) unlink(lock);
	}
	(void) close(fd);
	glo(dlevel);
	(void) unlink(lock);	/* get rid of current level --jgm */
	glo(0);
	(void) unlink(lock);
	return(1);
}

int
dorecover(int fd)
{
	int nfd;
	int tmp;		/* not a register ! */
	unsigned mid;		/* idem */
	struct obj *otmp;
	extern boolean restoring;

	restoring = TRUE;
	getlev(fd, 0, 0);
	invent = restobjchn(fd);
	for(otmp = invent; otmp; otmp = otmp->nobj)
		if(otmp->owornmask)
			setworn(otmp, otmp->owornmask);
	fcobj = restobjchn(fd);
	fallen_down = restmonchn(fd);
	mread(fd, (char *) &tmp, sizeof tmp);
	if(tmp != getuid()) {		/* strange ... */
		(void) close(fd);
		(void) unlink(SAVEF);
		puts("Saved game was not yours.");
		restoring = FALSE;
		return(0);
	}
	mread(fd, (char *) &flags, sizeof(struct flag));
	mread(fd, (char *) &dlevel, sizeof dlevel);
	mread(fd, (char *) &maxdlevel, sizeof maxdlevel);
	mread(fd, (char *) &moves, sizeof moves);
	mread(fd, (char *) &u, sizeof(struct you));
	if(u.ustuck)
		mread(fd, (char *) &mid, sizeof mid);
	mread(fd, (char *) pl_character, sizeof pl_character);
	mread(fd, (char *) genocided, sizeof genocided);
	mread(fd, (char *) fut_geno, sizeof fut_geno);
	restnames(fd);
	while(1) {
		if(read(fd, (char *) &tmp, sizeof tmp) != sizeof tmp)
			break;
		getlev(fd, 0, tmp);
		glo(tmp);
		if((nfd = open(lock, O_CREAT | O_TRUNC | O_WRONLY, FMASK)) == -1)
			panic("Cannot open temp file %s!\n", lock);
		savelev(nfd,tmp);
		(void) close(nfd);
	}
	(void) lseek(fd, (off_t)0, SEEK_SET);
	getlev(fd, 0, 0);
	(void) close(fd);
	(void) unlink(SAVEF);
	if(Punished) {
		for(otmp = fobj; otmp; otmp = otmp->nobj)
			if(otmp->olet == CHAIN_SYM) goto chainfnd;
		panic("Cannot find the iron chain?");
	chainfnd:
		uchain = otmp;
		if(!uball){
			for(otmp = fobj; otmp; otmp = otmp->nobj)
				if(otmp->olet == BALL_SYM && otmp->spe)
					goto ballfnd;
			panic("Cannot find the iron ball?");
		ballfnd:
			uball = otmp;
		}
	}
	if(u.ustuck) {
		struct monst *mtmp;

		for(mtmp = fmon; mtmp; mtmp = mtmp->nmon)
			if(mtmp->m_id == mid) goto monfnd;
		panic("Cannot find the monster ustuck.");
	monfnd:
		u.ustuck = mtmp;
	}
#ifndef QUEST
	setsee();  /* only to recompute seelx etc. - these weren't saved */
#endif /* QUEST */
	docrt();
	restoring = FALSE;
	return(1);
}

struct obj *
restobjchn(int fd)
{
	struct obj *otmp, *otmp2;
	struct obj *first = 0;
	int xl;
	while(1) {
		mread(fd, (char *) &xl, sizeof(xl));
		if(xl == -1) break;
		otmp = newobj(xl);
		if(!first) first = otmp;
		else otmp2->nobj = otmp;
		mread(fd, (char *) otmp, (unsigned) xl + sizeof(struct obj));
		if(!otmp->o_id) otmp->o_id = flags.ident++;
		otmp2 = otmp;
	}
	if(first && otmp2->nobj){
		impossible("Restobjchn: error reading objchn.");
		otmp2->nobj = 0;
	}
	return(first);
}

struct monst *
restmonchn(int fd)
{
	struct monst *mtmp, *mtmp2;
	struct monst *first = 0;
	int xl;

	struct permonst *monbegin;
	long differ;

	mread(fd, (char *)&monbegin, sizeof(monbegin));
	differ = (char *)(&mons[0]) - (char *)(monbegin);

	while(1) {
		mread(fd, (char *) &xl, sizeof(xl));
		if(xl == -1) break;
		mtmp = newmonst(xl);
		if(!first) first = mtmp;
		else mtmp2->nmon = mtmp;
		mread(fd, (char *) mtmp, (unsigned) xl + sizeof(struct monst));
		if(!mtmp->m_id)
			mtmp->m_id = flags.ident++;
		mtmp->data = (struct permonst *)
			((char *) mtmp->data + differ);
		if(mtmp->minvent)
			mtmp->minvent = restobjchn(fd);
		mtmp2 = mtmp;
	}
	if(first && mtmp2->nmon){
		impossible("Restmonchn: error reading monchn.");
		mtmp2->nmon = 0;
	}
	return(first);
}
