/*
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995 Wolfram Schneider <wosch@FreeBSD.org>. Berlin.
 * Copyright (c) 1989, 1993
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)locate.h	8.1 (Berkeley) 6/6/93
 * $FreeBSD$
 */

/* Symbolic constants shared by locate.c and code.c */

#define	NBG		128		/* number of bigrams considered */
#define	OFFSET		14		/* abs value of max likely diff */
#define	PARITY		0200		/* parity bit */
#define	SWITCH		30		/* switch code */
#define UMLAUT          31              /* an 8 bit char followed */

/* 	0-28	likeliest differential counts + offset to make nonnegative */
#define LDC_MIN         0
#define LDC_MAX        28

/*	128-255 bigram codes (128 most common, as determined by 'updatedb') */
#define BIGRAM_MIN    (UCHAR_MAX - SCHAR_MAX) 
#define BIGRAM_MAX    UCHAR_MAX

/*	32-127  single character (printable) ascii residue (ie, literal) */
#define ASCII_MIN      32
#define ASCII_MAX     SCHAR_MAX

/* #define TO7BIT(x)     (x = ( ((u_char)x) & SCHAR_MAX )) */
#define TO7BIT(x)     (x = x & SCHAR_MAX )


#if UCHAR_MAX >= 4096
   define TOLOWER(ch)	  tolower(ch)
#else

u_char myctype[UCHAR_MAX + 1];
#define TOLOWER(ch)	(myctype[ch])
#endif

#define INTSIZE (sizeof(int))

#define LOCATE_REG "*?[]\\"  /* fnmatch(3) meta characters */
