/*	$OpenBSD: init.c,v 1.11 2015/12/02 20:05:01 tb Exp $	*/

/*
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
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

/*
 * variable initialization.
 */

#ifdef DEBUG
#include <stdio.h>
FILE	*ftrace;
#endif
#include <back.h>

/* name of executable object programs */
const char    EXEC[] = "/usr/games/backgammon";
const char    TEACH[] = "/usr/games/teachgammon";

int     pnum = 2;		/* color of player:
						-1 = white
						 1 = red
						 0 = both
						 2 = not yet init'ed */
int     aflag = 1;		/* flag to ask for rules or instructions */
int     cflag = 0;		/* case conversion flag */
int     hflag = 1;		/* flag for cleaning screen */
int     mflag = 0;		/* backgammon flag */
int     raflag = 0;		/* 'roll again' flag for recovered game */
int     rflag = 0;		/* recovered game flag */
int     iroll = 0;		/* special flag for inputting rolls */
int	dflag = 1;		/* doubling enabled */
int     rfl = 0;

const char   *const color[] = {"White", "Red", "white", "red"};


const char	*const *Colorptr;
const char	*const *colorptr;
int	*inopp;
int	*inptr;
int	*offopp;
int	*offptr;
int	bar;
int	begscr;
int	board[26];
char	cin[CIN_SIZE];
int	colen;
int	cturn;
int	d0;
int	dice[2];
int	dlast;
int	g[5];
int	gvalue;
int	h[4];
int	home;
int	in[2];
int	mvl;
int	mvlim;
int	ncin;
int	off[2];
int	p[5];
int	rscore;
int	table[6][6];
int	wscore;
