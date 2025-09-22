/*	$OpenBSD: tokenizer.c,v 1.21 2016/04/11 21:17:29 schwarze Exp $	*/
/*	$NetBSD: tokenizer.c,v 1.28 2016/04/11 18:56:31 christos Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christos Zoulas of Cornell University.
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

#include "config.h"

/* We build this file twice, once as NARROW, once as WIDE. */
/*
 * tokenize.c: Bourne shell like tokenizer
 */
#include <stdlib.h>
#include <string.h>

#include "histedit.h"

typedef enum {
	Q_none, Q_single, Q_double, Q_one, Q_doubleone
} quote_t;

#define	TOK_KEEP	1
#define	TOK_EAT		2

#define	WINCR		20
#define	AINCR		10

#define	IFS		STR("\t \n")

#ifdef NARROWCHAR
#define	Char			char
#define	FUN(prefix, rest)	prefix ## _ ## rest
#define	TYPE(type)		type
#define	STR(x)			x
#define	Strchr(s, c)		strchr(s, c)
#define	tok_strdup(s)		strdup(s)
#else
#define	Char			wchar_t
#define	FUN(prefix, rest)	prefix ## _w ## rest
#define	TYPE(type)		type ## W
#define	STR(x)			L ## x
#define	Strchr(s, c)		wcschr(s, c)
#define	tok_strdup(s)		wcsdup(s)
#endif

struct TYPE(tokenizer) {
	Char	*ifs;		/* In field separator			 */
	int	 argc, amax;	/* Current and maximum number of args	 */
	Char   **argv;		/* Argument list			 */
	Char	*wptr, *wmax;	/* Space and limit on the word buffer	 */
	Char	*wstart;	/* Beginning of next word		 */
	Char	*wspace;	/* Space of word buffer			 */
	quote_t	 quote;		/* Quoting state			 */
	int	 flags;		/* flags;				 */
};


static void FUN(tok,finish)(TYPE(Tokenizer) *);


/* FUN(tok,finish)():
 *	Finish a word in the tokenizer.
 */
static void
FUN(tok,finish)(TYPE(Tokenizer) *tok)
{

	*tok->wptr = '\0';
	if ((tok->flags & TOK_KEEP) || tok->wptr != tok->wstart) {
		tok->argv[tok->argc++] = tok->wstart;
		tok->argv[tok->argc] = NULL;
		tok->wstart = ++tok->wptr;
	}
	tok->flags &= ~TOK_KEEP;
}


/* FUN(tok,init)():
 *	Initialize the tokenizer
 */
TYPE(Tokenizer) *
FUN(tok,init)(const Char *ifs)
{
	TYPE(Tokenizer) *tok = malloc(sizeof(TYPE(Tokenizer)));

	if (tok == NULL)
		return NULL;
	tok->ifs = tok_strdup(ifs ? ifs : IFS);
	if (tok->ifs == NULL) {
		free(tok);
		return NULL;
	}
	tok->argc = 0;
	tok->amax = AINCR;
	tok->argv = reallocarray(NULL, tok->amax, sizeof(*tok->argv));
	if (tok->argv == NULL) {
		free(tok->ifs);
		free(tok);
		return NULL;
	}
	tok->argv[0] = NULL;
	tok->wspace = reallocarray(NULL, WINCR, sizeof(*tok->wspace));
	if (tok->wspace == NULL) {
		free(tok->argv);
		free(tok->ifs);
		free(tok);
		return NULL;
	}
	tok->wmax = tok->wspace + WINCR;
	tok->wstart = tok->wspace;
	tok->wptr = tok->wspace;
	tok->flags = 0;
	tok->quote = Q_none;

	return tok;
}


/* FUN(tok,reset)():
 *	Reset the tokenizer
 */
void
FUN(tok,reset)(TYPE(Tokenizer) *tok)
{

	tok->argc = 0;
	tok->wstart = tok->wspace;
	tok->wptr = tok->wspace;
	tok->flags = 0;
	tok->quote = Q_none;
}


/* FUN(tok,end)():
 *	Clean up
 */
void
FUN(tok,end)(TYPE(Tokenizer) *tok)
{

	free(tok->ifs);
	free(tok->wspace);
	free(tok->argv);
	free(tok);
}



/* FUN(tok,line)():
 *	Bourne shell (sh(1)) like tokenizing
 *	Arguments:
 *		tok	current tokenizer state (setup with FUN(tok,init)())
 *		line	line to parse
 *	Returns:
 *		-1	Internal error
 *		 3	Quoted return
 *		 2	Unmatched double quote
 *		 1	Unmatched single quote
 *		 0	Ok
 *	Modifies (if return value is 0):
 *		argc	number of arguments
 *		argv	argument array
 *		cursorc	if !NULL, argv element containing cursor
 *		cursorv	if !NULL, offset in argv[cursorc] of cursor
 */
int
FUN(tok,line)(TYPE(Tokenizer) *tok, const TYPE(LineInfo) *line,
    int *argc, const Char ***argv, int *cursorc, int *cursoro)
{
	const Char *ptr;
	int cc, co;

	cc = co = -1;
	ptr = line->buffer;
	for (ptr = line->buffer; ;ptr++) {
		if (ptr >= line->lastchar)
			ptr = STR("");
		if (ptr == line->cursor) {
			cc = tok->argc;
			co = (int)(tok->wptr - tok->wstart);
		}
		switch (*ptr) {
		case '\'':
			tok->flags |= TOK_KEEP;
			tok->flags &= ~TOK_EAT;
			switch (tok->quote) {
			case Q_none:
				tok->quote = Q_single;	/* Enter single quote
							 * mode */
				break;

			case Q_single:	/* Exit single quote mode */
				tok->quote = Q_none;
				break;

			case Q_one:	/* Quote this ' */
				tok->quote = Q_none;
				*tok->wptr++ = *ptr;
				break;

			case Q_double:	/* Stay in double quote mode */
				*tok->wptr++ = *ptr;
				break;

			case Q_doubleone:	/* Quote this ' */
				tok->quote = Q_double;
				*tok->wptr++ = *ptr;
				break;

			default:
				return -1;
			}
			break;

		case '"':
			tok->flags &= ~TOK_EAT;
			tok->flags |= TOK_KEEP;
			switch (tok->quote) {
			case Q_none:	/* Enter double quote mode */
				tok->quote = Q_double;
				break;

			case Q_double:	/* Exit double quote mode */
				tok->quote = Q_none;
				break;

			case Q_one:	/* Quote this " */
				tok->quote = Q_none;
				*tok->wptr++ = *ptr;
				break;

			case Q_single:	/* Stay in single quote mode */
				*tok->wptr++ = *ptr;
				break;

			case Q_doubleone:	/* Quote this " */
				tok->quote = Q_double;
				*tok->wptr++ = *ptr;
				break;

			default:
				return -1;
			}
			break;

		case '\\':
			tok->flags |= TOK_KEEP;
			tok->flags &= ~TOK_EAT;
			switch (tok->quote) {
			case Q_none:	/* Quote next character */
				tok->quote = Q_one;
				break;

			case Q_double:	/* Quote next character */
				tok->quote = Q_doubleone;
				break;

			case Q_one:	/* Quote this, restore state */
				*tok->wptr++ = *ptr;
				tok->quote = Q_none;
				break;

			case Q_single:	/* Stay in single quote mode */
				*tok->wptr++ = *ptr;
				break;

			case Q_doubleone:	/* Quote this \ */
				tok->quote = Q_double;
				*tok->wptr++ = *ptr;
				break;

			default:
				return -1;
			}
			break;

		case '\n':
			tok->flags &= ~TOK_EAT;
			switch (tok->quote) {
			case Q_none:
				goto tok_line_outok;

			case Q_single:
			case Q_double:
				*tok->wptr++ = *ptr;	/* Add the return */
				break;

			case Q_doubleone:   /* Back to double, eat the '\n' */
				tok->flags |= TOK_EAT;
				tok->quote = Q_double;
				break;

			case Q_one:	/* No quote, more eat the '\n' */
				tok->flags |= TOK_EAT;
				tok->quote = Q_none;
				break;

			default:
				return 0;
			}
			break;

		case '\0':
			switch (tok->quote) {
			case Q_none:
				/* Finish word and return */
				if (tok->flags & TOK_EAT) {
					tok->flags &= ~TOK_EAT;
					return 3;
				}
				goto tok_line_outok;

			case Q_single:
				return 1;

			case Q_double:
				return 2;

			case Q_doubleone:
				tok->quote = Q_double;
				*tok->wptr++ = *ptr;
				break;

			case Q_one:
				tok->quote = Q_none;
				*tok->wptr++ = *ptr;
				break;

			default:
				return -1;
			}
			break;

		default:
			tok->flags &= ~TOK_EAT;
			switch (tok->quote) {
			case Q_none:
				if (Strchr(tok->ifs, *ptr) != NULL)
					FUN(tok,finish)(tok);
				else
					*tok->wptr++ = *ptr;
				break;

			case Q_single:
			case Q_double:
				*tok->wptr++ = *ptr;
				break;


			case Q_doubleone:
				*tok->wptr++ = '\\';
				tok->quote = Q_double;
				*tok->wptr++ = *ptr;
				break;

			case Q_one:
				tok->quote = Q_none;
				*tok->wptr++ = *ptr;
				break;

			default:
				return -1;

			}
			break;
		}

		if (tok->wptr >= tok->wmax - 4) {
			size_t size = tok->wmax - tok->wspace + WINCR;
			Char *s = reallocarray(tok->wspace, size, sizeof(*s));
			if (s == NULL)
				return -1;

			if (s != tok->wspace) {
				int i;
				for (i = 0; i < tok->argc; i++) {
				    tok->argv[i] =
					(tok->argv[i] - tok->wspace) + s;
				}
				tok->wptr = (tok->wptr - tok->wspace) + s;
				tok->wstart = (tok->wstart - tok->wspace) + s;
				tok->wspace = s;
			}
			tok->wmax = s + size;
		}
		if (tok->argc >= tok->amax - 4) {
			Char **p;
			tok->amax += AINCR;
			p = reallocarray(tok->argv, tok->amax, sizeof(*p));
			if (p == NULL) {
				tok->amax -= AINCR;
				return -1;
			}
			tok->argv = p;
		}
	}
 tok_line_outok:
	if (cc == -1 && co == -1) {
		cc = tok->argc;
		co = (int)(tok->wptr - tok->wstart);
	}
	if (cursorc != NULL)
		*cursorc = cc;
	if (cursoro != NULL)
		*cursoro = co;
	FUN(tok,finish)(tok);
	*argv = (const Char **)tok->argv;
	*argc = tok->argc;
	return 0;
}

/* FUN(tok,str)():
 *	Simpler version of tok_line, taking a NUL terminated line
 *	and splitting into words, ignoring cursor state.
 */
int
FUN(tok,str)(TYPE(Tokenizer) *tok, const Char *line, int *argc,
    const Char ***argv)
{
	TYPE(LineInfo) li;

	memset(&li, 0, sizeof(li));
	li.buffer = line;
	li.cursor = li.lastchar = Strchr(line, '\0');
	return FUN(tok,line)(tok, &li, argc, argv, NULL, NULL);
}
