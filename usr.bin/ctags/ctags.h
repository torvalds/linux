/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1987, 1993, 1994
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
 *	@(#)ctags.h	8.3 (Berkeley) 4/2/94
 *
 * $FreeBSD$
 *
 */

#define	bool	char

#define	YES		1
#define	NO		0
#define	EOS		'\0'

#define	ENDLINE		50		/* max length of pattern */
#define	MAXTOKEN	250		/* max size of single token */

#define	SETLINE		{++lineno;lineftell = ftell(inf);}
#define	GETC(op,exp)	((c = getc(inf)) op (int)exp)

/*
 * These character classification macros assume that the (EOF & 0xff) element
 * of the arrays is always 'NO', as the EOF return from getc() gets masked
 * to that value.  Masking with 0xff has no effect for normal characters
 * returned by getc() provided chars have 8 bits.
 */

#define	iswhite(arg)	_wht[arg & 0xff]	/* T if char is white */
#define	begtoken(arg)	_btk[arg & 0xff]	/* T if char can start token */
#define	intoken(arg)	_itk[arg & 0xff]	/* T if char can be in token */
#define	endtoken(arg)	_etk[arg & 0xff]	/* T if char ends tokens */
#define	isgood(arg)	_gd[arg & 0xff]	/* T if char can be after ')' */

typedef struct nd_st {			/* sorting structure */
	struct nd_st	*left,
			*right;		/* left and right sons */
	char	*entry,			/* function or type name */
		*file,			/* file name */
		*pat;			/* search pattern */
	int	lno;			/* for -x option */
	bool	been_warned;		/* set if noticed dup */
} NODE;

extern char	*curfile;		/* current input file name */
extern NODE	*head;			/* head of the sorted binary tree */
extern FILE    *inf;			/* ioptr for current input file */
extern FILE    *outf;			/* ioptr for current output file */
extern long	lineftell;		/* ftell after getc( inf ) == '\n' */
extern int	lineno;			/* line number of current line */
extern int	dflag;			/* -d: non-macro defines */
extern int	tflag;			/* -t: create tags for typedefs */
extern int	vflag;			/* -v: vgrind style index output */
extern int	wflag;			/* -w: suppress warnings */
extern int	xflag;			/* -x: cxref style output */
extern bool	_wht[], _etk[], _itk[], _btk[], _gd[];
extern char	lbuf[LINE_MAX];
extern char    *lbp;
extern char	searchar;		/* ex search character */

extern int	cicmp(const char *);
extern void	get_line(void);
extern void	pfnote(const char *, int);
extern int	skip_key(int);
extern void	put_entries(NODE *);
extern void	toss_yysec(void);
extern void	l_entries(void);
extern void	y_entries(void);
extern int	PF_funcs(void);
extern void	c_entries(void);
extern void	skip_comment(int);
