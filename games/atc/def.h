/*	$OpenBSD: def.h,v 1.6 2016/12/01 20:08:59 fcambus Exp $	*/
/*	$NetBSD: def.h,v 1.3 1995/03/21 15:03:47 cgd Exp $	*/

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
 *	@(#)def.h	8.1 (Berkeley) 5/31/93
 */

/*
 * Copyright (c) 1987 by Ed James, UC Berkeley.  All rights reserved.
 *
 * Copy permission is hereby granted provided that this notice is
 * retained on all partial or complete copies.
 *
 * For more info on this and all of my stuff, mail edjames@berkeley.edu.
 */

#define AUTHOR_STR		"ATC - by Ed James"

#define LOWFUEL			15

#define REALLOC			10

#define SGN(x)			((x < 0) ? -1 : ((x > 0) ? 1 : 0))
#define ABS(x)			((x < 0) ? -(x) : (x))
#define DIR_FROM_DXDY(dx,dy)	((int) (atan2((double)(dy), (double)(dx)) \
				* MAXDIR / (2 * M_PI) + 2.5 + MAXDIR) % MAXDIR)

#define MAXDIR		8

#define D_LEFT		1
#define D_RIGHT		2
#define D_UP		3
#define D_DOWN		4

#define T_NODEST	0
#define T_BEACON	1
#define T_EXIT		2
#define T_AIRPORT	3

#define S_NONE		0
#define S_GONE		1
#define S_MARKED	2
#define S_UNMARKED	3
#define S_IGNORED	4

#define INPUT_LINES	3
#define PLANE_COLS	20


/*
 * NUM_SCORES - Number of scores that are kept track of.
 * Keep this greater than 0.
 * 4 lines are printed above the score, one below + your prompt, so
 * to prevent scrolling: 6 + NUM_SCORES <= 24 (lines on an average terminal).
 */
#define	NUM_SCORES 	18
