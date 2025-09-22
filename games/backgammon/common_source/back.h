/*	$OpenBSD: back.h,v 1.15 2015/12/26 00:26:39 mestre Exp $	*/

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
 *
 *	@(#)back.h	8.1 (Berkeley) 5/31/93
 */

#include <sys/types.h>
#include <sys/uio.h>

#include <curses.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>
#include <unistd.h>

#define rnum(r)	arc4random_uniform(r)
#define D0	dice[0]
#define D1	dice[1]
#define swap	{D0 ^= D1; D1 ^= D0; D0 ^= D1; d0 = 1-d0;}
 
#define CIN_SIZE 40

#ifdef DEBUG
extern FILE	*ftrace;
#endif

/*
 *
 * Some numerical conventions:
 *
 *	Arrays have white's value in [0], red in [1].
 *	Numeric values which are one color or the other use
 *	-1 for white, 1 for red.
 *	Hence, white will be negative values, red positive one.
 *	This makes a lot of sense since white is going in decending
 *	order around the board, and red is ascending.
 *
 */

extern	const char	EXEC[];		/* object for main program */
extern	const char	TEACH[];	/* object for tutorial program */

extern	int	pnum;		/* color of player:
					-1 = white
					 1 = red
					 0 = both
					 2 = not yet init'ed */
extern	int	aflag;		/* flag to ask for rules or instructions */
extern	int	cflag;		/* case conversion flag */
extern	int	hflag;		/* flag for cleaning screen */
extern	int	mflag;		/* backgammon flag */
extern	int	raflag;		/* 'roll again' flag for recovered game */
extern	int	rflag;		/* recovered game flag */
extern	int	dflag;		/* disable doubling flag */
extern	int	rfl;		/* saved value of rflag */
extern	int	iroll;		/* special flag for inputting rolls */
extern	int	board[26];	/* board:  negative values are white,
				   positive are red */
extern	int	dice[2];	/* value of dice */
extern	int	mvlim;		/* 'move limit':  max. number of moves */
extern	int	mvl;		/* working copy of mvlim */
extern	int	p[5];		/* starting position of moves */
extern	int	g[5];		/* ending position of moves (goals) */
extern	int	h[4];		/* flag for each move if a man was hit */
extern	int	cturn;		/* whose turn it currently is:
					-1 = white
					 1 = red
					 0 = just quit
					-2 = white just lost
					 2 = red just lost */
extern	int	d0;		/* flag if dice have been reversed from
				   original position */
extern	int	table[6][6];	/* odds table for possible rolls */
extern	int	rscore;		/* red's score */
extern	int	wscore;		/* white's score */
extern	int	gvalue;		/* value of game (64 max.) */
extern	int	dlast;		/* who doubled last (0 = neither) */
extern	int	bar;		/* position of bar for current player */
extern	int	home;		/* position of home for current player */
extern	int	off[2];		/* number of men off board */
extern	int	*offptr;	/* pointer to off for current player */
extern	int	*offopp;	/* pointer to off for opponent */
extern	int	in[2];		/* number of men in inner table */
extern	int	*inptr;		/* pointer to in for current player */
extern	int	*inopp;		/* pointer to in for opponent */

extern	int	ncin;		/* number of characters in cin */
extern	char	cin[CIN_SIZE];	/* input line of current move
				   (used for reconstructing input after
				   a backspace) */

extern	const char	*const color[];	 /* colors as strings */
extern	const char	*const *colorptr;	/* color of current player */
extern	const char	*const *Colorptr;	/* color of current player,
						 * capitalized */
extern	int	colen;		/* length of color of current player */

extern	int	begscr;		/* 'beginning' of screen
				   (not including board) */

int	addbuf(int);
void	backone(int);
void	bsect(int, int, int, int);
int	canhit(int, int);
int	checkd(int);
int	checkmove(int);
int	count(void);
int	dotable(char, int);
void	errexit(const char *);
void	fboard(void);
void	fixcol(int, int, int, int, int);
void	fixpos(int, int, int, int, int);
void	getarg(int, char **);
void	getmove(void);
__dead void	getout(int);	/* function to exit backgammon cleanly */
void	gwrite(void);
void	init(void);
void	initcurses(void);
int	last(void);
int	makmove(int);
int	movallow(void);
void	movback(int);
void	moveplayers(void);
void	moverr(int);
int	movokay(int);
void	nexturn(void);
void	norec(const char *);
void	odds(int, int, int);
void	proll(void);
int	quit(void);
int	readc(void);
void	recover(const char *);
void	roll(void);
int	rsetbrd(void);
void	save(int);
int	text(const char *const *);
void	wrboard(void);
void	wrhit(int);
void	wrscore(void);
int	yorn(char);
