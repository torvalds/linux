/*	$OpenBSD: extern.h,v 1.11 2015/12/31 16:50:29 mestre Exp $	*/
/*	$NetBSD: extern.h,v 1.4 1995/04/27 21:22:22 mycroft Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ed James.
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
 *	@(#)extern.h	8.1 (Berkeley) 5/31/93
 */

/*
 * Copyright (c) 1987 by Ed James, UC Berkeley.  All rights reserved.
 *
 * Copy permission is hereby granted provided that this notice is
 * retained on all partial or complete copies.
 *
 * For more info on this and all of my stuff, mail edjames@berkeley.edu.
 */

#include <curses.h>

#include "def.h"
#include "struct.h"

extern char		GAMES[];
extern const char	*file;

extern int		clck, safe_planes, seeded, test_mode, makenoise;

extern time_t		start_time;

extern FILE		*filein, *fileout;

extern C_SCREEN		screen, *sp;

extern LIST		air, ground;

extern struct termios	tty_start, tty_new;

extern DISPLACEMENT	displacement[MAXDIR];

int		addplane(void);
void		append(LIST *, PLANE *);
void		check_adir(int, int, int);
void		check_edge(int, int);
void		check_edir(int, int, int);
void		check_line(int, int, int, int);
void		check_linepoint(int, int);
void		check_point(int, int);
int		checkdefs(void);
int		compar(const void *, const void *);
void		delete(LIST *, PLANE *);
int		dir_deg(int);
int		dir_no(char);
void		done_screen(void);
void		draw_all(void);
void		draw_line(WINDOW *, int, int, int, int, const char *);
void		erase_all(void);
int		getAChar(void);
int		getcommand(void);
int		gettoken(void);
void		ioaddstr(int, const char *);
void		ioclrtobot(void);
void		ioclrtoeol(int);
void		ioerror(int, int, const char *);
void		iomove(int);
int		list_games(void);
int		log_score(int);
__dead void		log_score_quit(int);
__dead void		loser(const PLANE *, const char *);
char		name(const PLANE *);
int		next_plane(void);
void		noise(void);
int		number(char);
int		open_score_file(void);
void		planewin(void);
int		pop(void);
void		push(int, int);
void		quit(int);
int		read_file(const char *);
void		redraw(void);
void		rezero(void);
void		setseed(const char *);
void		setup_screen(const C_SCREEN *);
int		too_close(const PLANE *p1, const PLANE *p2, int);
void		update(int);
int		yyerror(const char *);
int		yylex(void);
int		yyparse(void);
const char      *Left(char);
const char	*Right(char);
const char	*airport(char);
const char	*beacon(char);
const char	*benum(char);
const char	*circle(char);
const char	*climb(char);
const char	*command(PLANE *);
const char	*default_game(void);
const char	*delayb(char);
const char	*descend(char);
const char	*ex_it(char);
PLANE		*findplane(int);
const char	*ignore(char);
const char	*left(char);
const char	*mark(char);
PLANE		*newplane(void);
const char	*okay_game(const char *);
const char	*rel_dir(char);
const char	*right(char);
const char	*setalt(char);
const char	*setplane(char);
const char	*setrelalt(char);
const char	*timestr(int);
const char	*to_dir(char);
const char	*turn(char);
const char	*unmark(char);
