/*	$NetBSD: fmtcheck.c,v 1.8 2008/04/28 20:22:59 martin Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code was contributed to The NetBSD Foundation by Allen Briggs.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <string.h>
#include <ctype.h>

__weak_reference(__fmtcheck, fmtcheck);
const char * __fmtcheck(const char *, const char *);

enum __e_fmtcheck_types {
	FMTCHECK_START,
	FMTCHECK_SHORT,
	FMTCHECK_INT,
	FMTCHECK_WINTT,
	FMTCHECK_LONG,
	FMTCHECK_QUAD,
	FMTCHECK_INTMAXT,
	FMTCHECK_PTRDIFFT,
	FMTCHECK_SIZET,
	FMTCHECK_CHARPOINTER,
	FMTCHECK_SHORTPOINTER,
	FMTCHECK_INTPOINTER,
	FMTCHECK_LONGPOINTER,
	FMTCHECK_QUADPOINTER,
	FMTCHECK_INTMAXTPOINTER,
	FMTCHECK_PTRDIFFTPOINTER,
	FMTCHECK_SIZETPOINTER,
#ifndef NO_FLOATING_POINT
	FMTCHECK_DOUBLE,
	FMTCHECK_LONGDOUBLE,
#endif
	FMTCHECK_STRING,
	FMTCHECK_WSTRING,
	FMTCHECK_WIDTH,
	FMTCHECK_PRECISION,
	FMTCHECK_DONE,
	FMTCHECK_UNKNOWN
};
typedef enum __e_fmtcheck_types EFT;

enum e_modifier {
	MOD_NONE,
	MOD_CHAR,
	MOD_SHORT,
	MOD_LONG,
	MOD_QUAD,
	MOD_INTMAXT,
	MOD_LONGDOUBLE,
	MOD_PTRDIFFT,
	MOD_SIZET,
};

#define RETURN(pf,f,r) do { \
			*(pf) = (f); \
			return r; \
		       } /*NOTREACHED*/ /*CONSTCOND*/ while (0)

static EFT
get_next_format_from_precision(const char **pf)
{
	enum e_modifier	modifier;
	const char	*f;

	f = *pf;
	switch (*f) {
	case 'h':
		f++;
		if (!*f) RETURN(pf,f,FMTCHECK_UNKNOWN);
		if (*f == 'h') {
			f++;
			modifier = MOD_CHAR;
		} else {
			modifier = MOD_SHORT;
		}
		break;
	case 'j':
		f++;
		modifier = MOD_INTMAXT;
		break;
	case 'l':
		f++;
		if (!*f) RETURN(pf,f,FMTCHECK_UNKNOWN);
		if (*f == 'l') {
			f++;
			modifier = MOD_QUAD;
		} else {
			modifier = MOD_LONG;
		}
		break;
	case 'q':
		f++;
		modifier = MOD_QUAD;
		break;
	case 't':
		f++;
		modifier = MOD_PTRDIFFT;
		break;
	case 'z':
		f++;
		modifier = MOD_SIZET;
		break;
	case 'L':
		f++;
		modifier = MOD_LONGDOUBLE;
		break;
	default:
		modifier = MOD_NONE;
		break;
	}
	if (!*f) RETURN(pf,f,FMTCHECK_UNKNOWN);
	if (strchr("diouxX", *f)) {
		switch (modifier) {
		case MOD_LONG:
			RETURN(pf,f,FMTCHECK_LONG);
		case MOD_QUAD:
			RETURN(pf,f,FMTCHECK_QUAD);
		case MOD_INTMAXT:
			RETURN(pf,f,FMTCHECK_INTMAXT);
		case MOD_PTRDIFFT:
			RETURN(pf,f,FMTCHECK_PTRDIFFT);
		case MOD_SIZET:
			RETURN(pf,f,FMTCHECK_SIZET);
		case MOD_CHAR:
		case MOD_SHORT:
		case MOD_NONE:
			RETURN(pf,f,FMTCHECK_INT);
		default:
			RETURN(pf,f,FMTCHECK_UNKNOWN);
		}
	}
	if (*f == 'n') {
		switch (modifier) {
		case MOD_CHAR:
			RETURN(pf,f,FMTCHECK_CHARPOINTER);
		case MOD_SHORT:
			RETURN(pf,f,FMTCHECK_SHORTPOINTER);
		case MOD_LONG:
			RETURN(pf,f,FMTCHECK_LONGPOINTER);
		case MOD_QUAD:
			RETURN(pf,f,FMTCHECK_QUADPOINTER);
		case MOD_INTMAXT:
			RETURN(pf,f,FMTCHECK_INTMAXTPOINTER);
		case MOD_PTRDIFFT:
			RETURN(pf,f,FMTCHECK_PTRDIFFTPOINTER);
		case MOD_SIZET:
			RETURN(pf,f,FMTCHECK_SIZETPOINTER);
		case MOD_NONE:
			RETURN(pf,f,FMTCHECK_INTPOINTER);
		default:
			RETURN(pf,f,FMTCHECK_UNKNOWN);
		}
	}
	if (strchr("DOU", *f)) {
		if (modifier != MOD_NONE)
			RETURN(pf,f,FMTCHECK_UNKNOWN);
		RETURN(pf,f,FMTCHECK_LONG);
	}
#ifndef NO_FLOATING_POINT
	if (strchr("aAeEfFgG", *f)) {
		switch (modifier) {
		case MOD_LONGDOUBLE:
			RETURN(pf,f,FMTCHECK_LONGDOUBLE);
		case MOD_LONG:
		case MOD_NONE:
			RETURN(pf,f,FMTCHECK_DOUBLE);
		default:
			RETURN(pf,f,FMTCHECK_UNKNOWN);
		}
	}
#endif
	if (*f == 'c') {
		switch (modifier) {
		case MOD_LONG:
			RETURN(pf,f,FMTCHECK_WINTT);
		case MOD_NONE:
			RETURN(pf,f,FMTCHECK_INT);
		default:
			RETURN(pf,f,FMTCHECK_UNKNOWN);
		}
	}
	if (*f == 'C') {
		if (modifier != MOD_NONE)
			RETURN(pf,f,FMTCHECK_UNKNOWN);
		RETURN(pf,f,FMTCHECK_WINTT);
	}
	if (*f == 's') {
		switch (modifier) {
		case MOD_LONG:
			RETURN(pf,f,FMTCHECK_WSTRING);
		case MOD_NONE:
			RETURN(pf,f,FMTCHECK_STRING);
		default:
			RETURN(pf,f,FMTCHECK_UNKNOWN);
		}
	}
	if (*f == 'S') {
		if (modifier != MOD_NONE)
			RETURN(pf,f,FMTCHECK_UNKNOWN);
		RETURN(pf,f,FMTCHECK_WSTRING);
	}
	if (*f == 'p') {
		if (modifier != MOD_NONE)
			RETURN(pf,f,FMTCHECK_UNKNOWN);
		RETURN(pf,f,FMTCHECK_LONG);
	}
	RETURN(pf,f,FMTCHECK_UNKNOWN);
	/*NOTREACHED*/
}

static EFT
get_next_format_from_width(const char **pf)
{
	const char	*f;

	f = *pf;
	if (*f == '.') {
		f++;
		if (*f == '*') {
			RETURN(pf,f,FMTCHECK_PRECISION);
		}
		/* eat any precision (empty is allowed) */
		while (isdigit(*f)) f++;
		if (!*f) RETURN(pf,f,FMTCHECK_UNKNOWN);
	}
	RETURN(pf,f,get_next_format_from_precision(pf));
	/*NOTREACHED*/
}

static EFT
get_next_format(const char **pf, EFT eft)
{
	int		infmt;
	const char	*f;

	if (eft == FMTCHECK_WIDTH) {
		(*pf)++;
		return get_next_format_from_width(pf);
	} else if (eft == FMTCHECK_PRECISION) {
		(*pf)++;
		return get_next_format_from_precision(pf);
	}

	f = *pf;
	infmt = 0;
	while (!infmt) {
		f = strchr(f, '%');
		if (f == NULL)
			RETURN(pf,f,FMTCHECK_DONE);
		f++;
		if (!*f)
			RETURN(pf,f,FMTCHECK_UNKNOWN);
		if (*f != '%')
			infmt = 1;
		else
			f++;
	}

	/* Eat any of the flags */
	while (*f && (strchr("#'0- +", *f)))
		f++;

	if (*f == '*') {
		RETURN(pf,f,FMTCHECK_WIDTH);
	}
	/* eat any width */
	while (isdigit(*f)) f++;
	if (!*f) {
		RETURN(pf,f,FMTCHECK_UNKNOWN);
	}

	RETURN(pf,f,get_next_format_from_width(pf));
	/*NOTREACHED*/
}

const char *
__fmtcheck(const char *f1, const char *f2)
{
	const char	*f1p, *f2p;
	EFT		f1t, f2t;

	if (!f1) return f2;
	
	f1p = f1;
	f1t = FMTCHECK_START;
	f2p = f2;
	f2t = FMTCHECK_START;
	while ((f1t = get_next_format(&f1p, f1t)) != FMTCHECK_DONE) {
		if (f1t == FMTCHECK_UNKNOWN)
			return f2;
		f2t = get_next_format(&f2p, f2t);
		if (f1t != f2t)
			return f2;
	}
	return f1;
}
