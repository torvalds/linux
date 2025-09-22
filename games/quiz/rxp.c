/*	$OpenBSD: rxp.c,v 1.8 2009/10/27 23:59:26 deraadt Exp $	*/
/*	$NetBSD: rxp.c,v 1.5 1995/04/22 10:17:00 cgd Exp $	*/

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
 */

/*
 * regular expression parser
 *
 * external functions and return values are:
 * rxp_compile(s)
 *	TRUE	success
 *	FALSE	parse failure; error message will be in char rxperr[]
 * metas are:
 *	{...}	optional pattern, equialent to [...|]
 *	|	alternate pattern
 *	[...]	pattern delimiters
 *
 * rxp_match(s)
 *	TRUE	string s matches compiled pattern
 *	FALSE	match failure or regexp error
 *
 * rxp_expand()
 *	char *	reverse-engineered regular expression string
 *	NULL	regexp error
 */

#include <stdio.h>
#include <ctype.h>
#include "quiz.h"
					/* regexp tokens,	arg */
#define	LIT	(-1)			/* literal character,	char */
#define	SOT	(-2)			/* start text anchor,	- */
#define	EOT	(-3)			/* end text anchor,	- */
#define	GRP_S	(-4)			/* start alternate grp,	ptr_to_end */
#define	GRP_E	(-5)			/* end group,		- */
#define	ALT_S	(-6)			/* alternate starts,	ptr_to_next */
#define	ALT_E	(-7)			/* alternate ends,	- */
#define	END	(-8)			/* end of regexp,	- */

typedef short Rxp_t;			/* type for regexp tokens */

static Rxp_t rxpbuf[RXP_LINE_SZ];	/* compiled regular expression buffer */
char rxperr[128];			/* parser error message */

static int	 rxp__compile(const char *, int);
static char	*rxp__expand(int);
static int	 rxp__match(const char *, int, Rxp_t *, Rxp_t *, const char *);

int
rxp_compile(const char *s)
{
	return (rxp__compile(s, TRUE));
}

static int
rxp__compile(const char *s, int first)
{
	static Rxp_t *rp;
	static const char *sp;
	Rxp_t *grp_ptr;
	Rxp_t *alt_ptr;
	int esc, err;

	if (s == NULL) {
		(void)snprintf(rxperr, sizeof(rxperr),
		    "null string sent to rxp_compile");
		return(FALSE);
	}
	esc = 0;
	if (first) {
		rp = rxpbuf;
		sp = s;
		*rp++ = SOT;	/* auto-anchor: pat is really ^pat$ */
		*rp++ = GRP_S;	/* auto-group: ^pat$ is really ^[pat]$ */
		*rp++ = 0;
	}
	*rp++ = ALT_S;
	alt_ptr = rp;
	*rp++ = 0;
	for (; *sp; ++sp) {
		if (rp - rxpbuf >= RXP_LINE_SZ - 4) {
			(void)snprintf(rxperr, sizeof(rxperr),
			    "regular expression too long %s", s);
			return (FALSE);
		}
		if (*sp == ':' && !esc)
			break;
		if (esc) {
			*rp++ = LIT;
			*rp++ = *sp;
			esc = 0;
		}
		else switch (*sp) {
		case '\\':
			esc = 1;
			break;
		case '{':
		case '[':
			*rp++ = GRP_S;
			grp_ptr = rp;
			*rp++ = 0;
			sp++;
			if ((err = rxp__compile(s, FALSE)) != TRUE)
				return (err);
			*rp++ = GRP_E;
			*grp_ptr = rp - rxpbuf;
			break;
		case '}':
		case ']':
		case '|':
			*rp++ = ALT_E;
			*alt_ptr = rp - rxpbuf;
			if (*sp != ']') {
				*rp++ = ALT_S;
				alt_ptr = rp;
				*rp++ = 0;
			}
			if (*sp != '|') {
				if (*sp != ']') {
					*rp++ = ALT_E;
					*alt_ptr = rp - rxpbuf;
				}
				if (first) {
					(void)snprintf(rxperr, sizeof(rxperr),
					    "unmatched alternator in regexp %s",
					     s);
					return (FALSE);
				}
				return (TRUE);
			}
			break;
		default:
			*rp++ = LIT;
			*rp++ = *sp;
			esc = 0;
			break;
		}
	}
	if (!first) {
		(void)snprintf(rxperr, sizeof(rxperr),
		    "unmatched alternator in regexp %s", s);
		return (FALSE);
	}
	*rp++ = ALT_E;
	*alt_ptr = rp - rxpbuf;
	*rp++ = GRP_E;
	*(rxpbuf + 2) = rp - rxpbuf;
	*rp++ = EOT;
	*rp = END;
	return (TRUE);
}

/*
 * match string against compiled regular expression
 */
int
rxp_match(const char *s)
{
	return (rxp__match(s, TRUE, NULL, NULL, NULL));
}

/*
 * j_succ : jump here on successful alt match
 * j_fail : jump here on failed match
 * sp_fail: reset sp to here on failed match
 */
static int
rxp__match(const char *s, int first, Rxp_t *j_succ, Rxp_t *j_fail,
           const char *sp_fail)
{
	static Rxp_t *rp;
	static const char *sp;
	int ch;
	Rxp_t *grp_end = NULL;
	int err;

	if (first) {
		rp = rxpbuf;
		sp = s;
	}
	while (rp < rxpbuf + RXP_LINE_SZ && *rp != END)
		switch(*rp) {
		case LIT:
			rp++;
			ch = isascii(*rp) && isupper(*rp) ? tolower(*rp) : *rp;
			if (ch != *sp++) {
				rp = j_fail;
				sp = sp_fail;
				return (TRUE);
			}
			rp++;
			break;
		case SOT:
			if (sp != s)
				return (FALSE);
			rp++;
			break;
		case EOT:
			if (*sp != 0)
				return (FALSE);
			rp++;
			break;
		case GRP_S:
			rp++;
			grp_end = rxpbuf + *rp++;
			break;
		case ALT_S:
			rp++;
			if ((err = rxp__match(sp,
			    FALSE, grp_end, rxpbuf + *rp++, sp)) != TRUE)
				return (err);
			break;
		case ALT_E:
			rp = j_succ;
			return (TRUE);
		case GRP_E:
		default:
			return (FALSE);
		}
	return (*rp != END ? FALSE : TRUE);
}

/*
 * Reverse engineer the regular expression, by picking first of all alternates.
 */
char *
rxp_expand(void)
{
	return (rxp__expand(TRUE));
}

static char *
rxp__expand(int first)
{
	static char buf[RXP_LINE_SZ/2];
	static Rxp_t *rp;
	static char *bp;
	Rxp_t *grp_ptr;
	char *err;

	if (first) {
		rp = rxpbuf;
		bp = buf;
	}
	while (rp < rxpbuf + RXP_LINE_SZ && *rp != END)
		switch(*rp) {
		case LIT:
			rp++;
			*bp++ = *rp++;
			break;
		case GRP_S:
			rp++;
			grp_ptr = rxpbuf + *rp;
			rp++;
			if ((err = rxp__expand(FALSE)) == NULL)
				return (err);
			rp = grp_ptr;
			break;
		case ALT_E:
			return (buf);
		case ALT_S:
			rp++;
			/* FALLTHROUGH */
		case SOT:
		case EOT:
		case GRP_E:
			rp++;
			break;
		default:
			return (NULL);
		}
	if (first) {
		if (*rp != END)
			return (NULL);
		*bp = '\0';
	}
	return (buf);
}
