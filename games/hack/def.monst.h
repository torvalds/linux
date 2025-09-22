/*	$OpenBSD: def.monst.h,v 1.5 2016/01/09 18:33:15 mestre Exp $*/
/*	$NetBSD: def.monst.h,v 1.3 1995/03/23 08:29:30 cgd Exp $*/

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

struct monst {
	struct monst *nmon;
	struct permonst *data;
	unsigned m_id;
	xchar mx,my;
	xchar mdx,mdy;		/* if mdispl then pos where last displayed */
#define	MTSZ	4
	coord mtrack[MTSZ];	/* monster track */
	schar mhp,mhpmax;
	char mappearance;	/* nonzero for undetected 'M's and for '1's */
	Bitfield(mimic,1);	/* undetected mimic */
	Bitfield(mdispl,1);	/* mdx,mdy valid */
	Bitfield(minvis,1);	/* invisible */
	Bitfield(cham,1);	/* shape-changer */
	Bitfield(mhide,1);	/* hides beneath objects */
	Bitfield(mundetected,1);	/* not seen in present hiding place */
	Bitfield(mspeed,2);
	Bitfield(msleep,1);
	Bitfield(mfroz,1);
	Bitfield(mconf,1);
	Bitfield(mflee,1);	/* fleeing */
	Bitfield(mfleetim,7);	/* timeout for mflee */
	Bitfield(mcan,1);	/* has been cancelled */
	Bitfield(mtame,1);		/* implies peaceful */
	Bitfield(mpeaceful,1);	/* does not attack unprovoked */
	Bitfield(isshk,1);	/* is shopkeeper */
	Bitfield(isgd,1);	/* is guard */
	Bitfield(mcansee,1);	/* cansee 1, temp.blinded 0, blind 0 */
	Bitfield(mblinded,7);	/* cansee 0, temp.blinded n, blind 0 */
	Bitfield(mtrapped,1);	/* trapped in a pit or bear trap */
	Bitfield(mnamelth,6);	/* length of name (following mxlth) */
#ifndef NOWORM
	Bitfield(wormno,5);	/* at most 31 worms on any level */
#endif /* NOWORM */
	unsigned mtrapseen;	/* bitmap of traps we've been trapped in */
	long mlstmv;	/* prevent two moves at once */
	struct obj *minvent;
	long mgold;
	unsigned mxlth;		/* length of following data */
	/* in order to prevent alignment problems mextra should
	   be (or follow) a long int */
	long mextra[1];		/* monster dependent info */
};

#define newmonst(xl)	(struct monst *) alloc((unsigned)(xl) + sizeof(struct monst))

extern struct monst *fmon;
extern struct monst *fallen_down;
struct monst *m_at(int, int);

/* these are in mspeed */
#define MSLOW 1 /* slow monster */
#define MFAST 2 /* speeded monster */

#define	NAME(mtmp)	(((char *) mtmp->mextra) + mtmp->mxlth)
#define	MREGEN		"TVi1"
#define	UNDEAD		"ZVW "
