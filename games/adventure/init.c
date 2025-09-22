/*	$OpenBSD: init.c,v 1.16 2017/05/19 08:22:53 tb Exp $	*/
/*	$NetBSD: init.c,v 1.4 1996/05/21 21:53:05 mrg Exp $	*/

/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * The game adventure was originally written in Fortran by Will Crowther
 * and Don Woods.  It was later translated to C and enhanced by Jim
 * Gillogly.  This code is derived from software contributed to Berkeley
 * by Jim Gillogly at The Rand Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*	Re-coding of advent in C: data initialization	*/

#include <stdlib.h>

#include "extern.h"
#include "hdr.h"

int     blklin = TRUE;

int     setbit[16] = {1, 2, 4, 010, 020, 040, 0100, 0200, 0400, 01000, 02000,
				04000, 010000, 020000, 040000, 0100000};


void
init(void)			/* everything for 1st time run */
{
	/*
	 * We need deterministic randomness for the obfuscation schemes
	 * in io.c and setup.c.
	 */
	srandom_deterministic(1);
	rdata();		/* read data from orig. file */
	linkdata();
	poof();
}

char *
decr(char a, char b, char c, char d, char e)
{
	static char buf[6];

	buf[0] = a + '+';
	buf[1] = b + '-';
	buf[2] = c + '#';
	buf[3] = d + '&';
	buf[4] = e + '%';
	buf[5] = 0;
	return buf;
}

void
linkdata(void)			/* secondary data manipulation */
{
	int     i, j;

	/* array linkages */
	for (i = 1; i < LOCSIZ; i++)
		if (ltext[i].seekadr != 0 && travel[i] != 0)
			if ((travel[i]->tverb) == 1)
				cond[i] = 2;
	for (j = 100; j > 0; j--)
		if (fixd[j] > 0) {
			drop(j + 100, fixd[j]);
			drop(j, plac[j]);
		}
	for (j = 100; j > 0; j--) {
		fixed[j] = fixd[j];
		if (plac[j] != 0 && fixd[j] <= 0)
			drop(j, plac[j]);
	}

	maxtrs = 79;
	tally = 0;
	tally2 = 0;

	for (i = 50; i <= maxtrs; i++) {
		if (ptext[i].seekadr != 0)
			prop[i] = -1;
		tally -= prop[i];
	}

	/* define mnemonics */
	keys  = vocab(DECR(k,e,y,s,\0), 1, 0);
	lamp  = vocab(DECR(l,a,m,p,\0), 1, 0);
	grate = vocab(DECR(g,r,a,t,e), 1, 0);
	cage  = vocab(DECR(c,a,g,e,\0),1, 0);
	rod   = vocab(DECR(r,o,d,\0,\0),1, 0);
	rod2  = rod + 1;
	steps = vocab(DECR(s,t,e,p,s),1, 0);
	bird  = vocab(DECR(b,i,r,d,\0),1, 0);
	door  = vocab(DECR(d,o,o,r,\0),1, 0);
	pillow= vocab(DECR(p,i,l,l,o), 1, 0);
	snake = vocab(DECR(s,n,a,k,e), 1, 0);
	fissur= vocab(DECR(f,i,s,s,u), 1, 0);
	tablet= vocab(DECR(t,a,b,l,e), 1, 0);
	clam  = vocab(DECR(c,l,a,m,\0),1, 0);
	oyster= vocab(DECR(o,y,s,t,e), 1, 0);
	magzin= vocab(DECR(m,a,g,a,z), 1, 0);
	dwarf = vocab(DECR(d,w,a,r,f), 1, 0);
	knife = vocab(DECR(k,n,i,f,e), 1, 0);
	food  = vocab(DECR(f,o,o,d,\0),1, 0);
	bottle= vocab(DECR(b,o,t,t,l), 1, 0);
	water = vocab(DECR(w,a,t,e,r), 1, 0);
	oil   = vocab(DECR(o,i,l,\0,\0),1, 0);
	plant = vocab(DECR(p,l,a,n,t), 1, 0);
	plant2= plant + 1;
	axe   = vocab(DECR(a,x,e,\0,\0),1, 0);
	mirror= vocab(DECR(m,i,r,r,o), 1, 0);
	dragon= vocab(DECR(d,r,a,g,o), 1, 0);
	chasm = vocab(DECR(c,h,a,s,m), 1, 0);
	troll = vocab(DECR(t,r,o,l,l), 1, 0);
	troll2= troll + 1;
	bear  = vocab(DECR(b,e,a,r,\0),1, 0);
	messag= vocab(DECR(m,e,s,s,a), 1, 0);
	vend  = vocab(DECR(v,e,n,d,i), 1, 0);
	batter= vocab(DECR(b,a,t,t,e), 1, 0);

	nugget= vocab(DECR(g,o,l,d,\0),1, 0);
	coins = vocab(DECR(c,o,i,n,s), 1, 0);
	chest = vocab(DECR(c,h,e,s,t), 1, 0);
	eggs  = vocab(DECR(e,g,g,s,\0),1, 0);
	tridnt= vocab(DECR(t,r,i,d,e), 1, 0);
	vase  = vocab(DECR(v,a,s,e,\0),1, 0);
	emrald= vocab(DECR(e,m,e,r,a), 1, 0);
	pyram = vocab(DECR(p,y,r,a,m), 1, 0);
	pearl = vocab(DECR(p,e,a,r,l), 1, 0);
	rug   = vocab(DECR(r,u,g,\0,\0),1, 0);
	chain = vocab(DECR(c,h,a,i,n), 1, 0);

	back  = vocab(DECR(b,a,c,k,\0),0, 0);
	look  = vocab(DECR(l,o,o,k,\0),0, 0);
	cave  = vocab(DECR(c,a,v,e,\0),0, 0);
	null  = vocab(DECR(n,u,l,l,\0),0, 0);
	entrnc= vocab(DECR(e,n,t,r,a), 0, 0);
	dprssn= vocab(DECR(d,e,p,r,e), 0, 0);
	enter = vocab(DECR(e,n,t,e,r), 0, 0);

	pour  = vocab(DECR(p,o,u,r,\0), 2, 0);
	say   = vocab(DECR(s,a,y,\0,\0),2, 0);
	lock  = vocab(DECR(l,o,c,k,\0),2, 0);
	throw = vocab(DECR(t,h,r,o,w), 2, 0);
	find  = vocab(DECR(f,i,n,d,\0),2, 0);
	invent= vocab(DECR(i,n,v,e,n), 2, 0);

	/* initialize dwarves */
	chloc = 114;
	chloc2 = 140;
	for (i = 1; i <= 6; i++)
		dseen[i] = FALSE;
	dflag = 0;
	dloc[1] = 19;
	dloc[2] = 27;
	dloc[3] = 33;
	dloc[4] = 44;
	dloc[5] = 64;
	dloc[6] = chloc;
	daltlc = 18;

	/* random flags & ctrs */
	turns = 0;
	lmwarn = FALSE;
	iwest = 0;
	knfloc = 0;
	detail = 0;
	abbnum = 5;
	for (i = 0; i <= 4; i++)
		if (rtext[2 * i + 81].seekadr != 0)
			maxdie = i + 1;
	numdie = holdng = dkill = foobar = bonus = 0;
	clock1 = 30;
	clock2 = 50;
	saved = 0;
	closng = panic = closed = scorng = FALSE;
}


void
trapdel(int n)			/* come here if he hits a del	*/
{
	delhit = 1;		/* main checks, treats as QUIT	*/
	signal(SIGINT, trapdel);/* catch subsequent DELs	*/
}


void
startup(void)
{
	demo = Start();
	hinted[3] = yes(65, 1, 0);
	newloc = 1;
	delhit = 0;
	limit = 330;
	if (hinted[3])
		limit = 1000;	/* better batteries if instrucs */
}
