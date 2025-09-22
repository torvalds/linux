/*	$OpenBSD: def.flag.h,v 1.4 2016/01/09 18:33:15 mestre Exp $*/
/*	$NetBSD: def.flag.h,v 1.3 1995/03/23 08:29:22 cgd Exp $*/

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

struct flag {
	unsigned ident;		/* social security number for each monster */
	unsigned debug:1;	/* in debugging mode */
#define	wizard	flags.debug
	unsigned toplin:2;	/* a top line (message) has been printed */
				/* 0: top line empty; 2: no --More-- reqd. */
	unsigned cbreak:1;	/* in cbreak mode, rogue format */
	unsigned standout:1;	/* use standout for --More-- */
	unsigned nonull:1;	/* avoid sending nulls to the terminal */
	unsigned time:1;	/* display elapsed 'time' */
	unsigned nonews:1;	/* suppress news printing */
	unsigned notombstone:1;
	unsigned end_top, end_around;	/* describe desired score list */
	unsigned end_own:1;		/* idem (list all own scores) */
	unsigned no_rest_on_space:1;	/* spaces are ignored */
	unsigned beginner:1;
	unsigned female:1;
	unsigned invlet_constant:1;	/* let objects keep their
					   inventory symbol */
	unsigned move:1;
	unsigned mv:1;
	unsigned run:3;		/* 0: h (etc), 1: H (etc), 2: fh (etc) */
				/* 3: FH, 4: ff+, 5: ff-, 6: FF+, 7: FF- */
	unsigned nopick:1;	/* do not pickup objects */
	unsigned echo:1;	/* 1 to echo characters */
	unsigned botl:1;	/* partially redo status line */
	unsigned botlx:1;	/* print an entirely new bottom line */
	unsigned nscrinh:1;	/* inhibit nscr() in pline(); */
	unsigned made_amulet:1;
	unsigned no_of_wizards:2;/* 0, 1 or 2 (wizard and his shadow) */
				/* reset from 2 to 1, but never to 0 */
	unsigned moonphase:3;
#define	NEW_MOON	0
#define	FULL_MOON	4

};

extern struct flag flags;
