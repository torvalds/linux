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
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)yacc.c	8.3 (Berkeley) 4/2/94";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ctype.h>
#include <limits.h>
#include <stdio.h>

#include "ctags.h"

/*
 * y_entries:
 *	find the yacc tags and put them in.
 */
void
y_entries(void)
{
	int	c;
	char	*sp;
	bool	in_rule;
	char	tok[MAXTOKEN];

	in_rule = NO;

	while (GETC(!=, EOF))
		switch (c) {
		case '\n':
			SETLINE;
			/* FALLTHROUGH */
		case ' ':
		case '\f':
		case '\r':
		case '\t':
			break;
		case '{':
			if (skip_key('}'))
				in_rule = NO;
			break;
		case '\'':
		case '"':
			if (skip_key(c))
				in_rule = NO;
			break;
		case '%':
			if (GETC(==, '%'))
				return;
			(void)ungetc(c, inf);
			break;
		case '/':
			if (GETC(==, '*') || c == '/')
				skip_comment(c);
			else
				(void)ungetc(c, inf);
			break;
		case '|':
		case ';':
			in_rule = NO;
			break;
		default:
			if (in_rule || (!isalpha(c) && c != '.' && c != '_'))
				break;
			sp = tok;
			*sp++ = c;
			while (GETC(!=, EOF) && (intoken(c) || c == '.'))
				*sp++ = c;
			*sp = EOS;
			get_line();		/* may change before ':' */
			while (iswhite(c)) {
				if (c == '\n')
					SETLINE;
				if (GETC(==, EOF))
					return;
			}
			if (c == ':') {
				pfnote(tok, lineno);
				in_rule = YES;
			}
			else
				(void)ungetc(c, inf);
		}
}

/*
 * toss_yysec --
 *	throw away lines up to the next "\n%%\n"
 */
void
toss_yysec(void)
{
	int	c;			/* read character */
	int	state;

	/*
	 * state == 0 : waiting
	 * state == 1 : received a newline
	 * state == 2 : received first %
	 * state == 3 : received second %
	 */
	lineftell = ftell(inf);
	for (state = 0; GETC(!=, EOF);)
		switch (c) {
		case '\n':
			++lineno;
			lineftell = ftell(inf);
			if (state == 3)		/* done! */
				return;
			state = 1;		/* start over */
			break;
		case '%':
			if (state)		/* if 1 or 2 */
				++state;	/* goto 3 */
			break;
		default:
			state = 0;		/* reset */
			break;
		}
}
