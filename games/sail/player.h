/*	$OpenBSD: player.h,v 1.8 2015/12/31 16:44:22 mestre Exp $	*/
/*	$NetBSD: player.h,v 1.4 1995/04/22 10:37:22 cgd Exp $	*/

/*
 * Copyright (c) 1983, 1993
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
 *	@(#)player.h	8.2 (Berkeley) 5/3/95
 */

#include <curses.h>

/* sizes and coordinates for the screen */

#define LINE_T		0
#define LINE_L		0
#define LINE_X		COLS
#define LINE_Y		1
#define LINE_B		(LINE_T+LINE_Y-1)
#define LINE_R		(LINE_L+LINE_X-1)

#define BOX_T		1
#define BOX_L		0
#define BOX_X		65
#define BOX_Y		16
#define BOX_B		(BOX_T+BOX_Y-1)
#define BOX_R		(BOX_L+BOX_X-1)

#define TURN_T		BOX_B
#define TURN_Y		1
#define TURN_L		((BOX_L+BOX_R-TURN_X)/2)
#define TURN_X		9
#define TURN_B		(TURN_T+TURN_Y+1)
#define TURN_R		(TURN_L+TURN_X+1)

#define STAT_T		0
#define STAT_B		BOX_B
#define STAT_L		(BOX_R+2)
#define STAT_X		14
#define STAT_Y		(STAT_B-STAT_T+1)
#define STAT_R		(STAT_L+STAT_X-1)
#define STAT_1		0
#define STAT_2		(STAT_1+4)
#define STAT_3		(STAT_2+7)

#define SCROLL_T	(BOX_B+1)
#define SCROLL_L	0
#define SCROLL_B	(LINES-1)
#define SCROLL_R	(COLS-1)
#define SCROLL_X	(SCROLL_R-SCROLL_L+1)
#define SCROLL_Y	(SCROLL_B-SCROLL_T+1)

#define VIEW_T		(BOX_T+1)
#define VIEW_L		(BOX_L+1)
#define VIEW_X		(BOX_X-5)
#define VIEW_Y		(BOX_Y-2)
#define VIEW_B		(VIEW_T+VIEW_Y-1)
#define VIEW_R		(VIEW_L+VIEW_X-1)

#define SLOT_T		VIEW_T
#define SLOT_L		(VIEW_R+1)
#define SLOT_X		3
#define SLOT_Y		VIEW_Y
#define SLOT_B		VIEW_B
#define SLOT_R		(SLOT_L+SLOT_X-1)

#ifdef SIGTSTP
#define SCREENTEST()	(initscr() != NULL && signal(SIGTSTP, SIG_DFL) != SIG_ERR && STAT_R < COLS && SCROLL_Y > 0)
#else
#define SCREENTEST()	(initscr() != NULL && STAT_R < COLS && SCROLL_Y > 0)
#endif

extern WINDOW *view_w;
extern WINDOW *slot_w;
extern WINDOW *scroll_w;
extern WINDOW *stat_w;
extern WINDOW *turn_w;

extern char done_curses;
extern char loaded, fired, changed, repaired;
extern char dont_adjust;
extern int viewrow, viewcol;
extern char movebuf[sizeof SHIP(0)->file->movebuf];
extern char version[];
extern int player;
extern struct ship *ms;		/* memorial structure, &cc->ship[player] */
extern struct File *mf;		/* ms->file */
extern struct shipspecs *mc;		/* ms->specs */

/* condition codes for leave() */
#define LEAVE_QUIT	0
#define LEAVE_CAPTURED	1
#define LEAVE_HURRICAN	2
#define LEAVE_DRIVER	3
#define LEAVE_FORK	4
#define LEAVE_SYNC	5
