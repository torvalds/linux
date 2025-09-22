/*	$OpenBSD: quiz.h,v 1.5 2003/06/03 03:01:40 millert Exp $	*/
/*	$NetBSD: quiz.h,v 1.4 1995/04/22 10:16:59 cgd Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jim R. Oldroyd at The Instruction Set and Keith Gabryelski at
 * Commodore Business Machines.
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
 *	@(#)quiz.h	8.1 (Berkeley) 5/31/93
 */

#define	TRUE		1
#define	FALSE		0

/* Length of compiled regexp machine; increase if not big enough. */
#define	RXP_LINE_SZ	8192

/* Maximum line length for data files. */
#define	LINE_SZ		1024

/* Linked list for holding index and data file information. */
typedef struct qentry {
	struct qentry *q_next;		/* next one */
	char	*q_text;		/* category text string from file */
	int	 q_asked;		/* TRUE if question's been asked */
	int	 q_answered;		/* TRUE if question's been answered */
} QE;

extern char rxperr[];

int	 rxp_compile(const char *);
char	*rxp_expand(void);
int	 rxp_match(const char *);
