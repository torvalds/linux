/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Tim J. Robbins.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _WORDEXP_H_
#define	_WORDEXP_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#if __XSI_VISIBLE && !defined(_SIZE_T_DECLARED)
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

typedef struct {
	__size_t	we_wordc;	/* count of words matched */
	char		**we_wordv;	/* pointer to list of words */
	__size_t	we_offs;	/* slots to reserve in we_wordv */
	char		*we_strings;	/* storage for wordv strings */
	__size_t	we_nbytes;	/* size of we_strings */
} wordexp_t;

/*
 * Flags for wordexp().
 */
#define	WRDE_APPEND	0x1		/* append to previously generated */
#define	WRDE_DOOFFS	0x2		/* we_offs member is valid */
#define	WRDE_NOCMD	0x4		/* disallow command substitution */
#define	WRDE_REUSE	0x8		/* reuse wordexp_t */
#define	WRDE_SHOWERR	0x10		/* don't redirect stderr to /dev/null */
#define	WRDE_UNDEF	0x20		/* disallow undefined shell vars */

/*
 * Return values from wordexp().
 */
#define	WRDE_BADCHAR	1		/* unquoted special character */
#define	WRDE_BADVAL	2		/* undefined variable */
#define	WRDE_CMDSUB	3		/* command substitution not allowed */
#define	WRDE_NOSPACE	4		/* no memory for result */
#if __XSI_VISIBLE
#define	WRDE_NOSYS	5		/* obsolete, reserved */
#endif
#define	WRDE_SYNTAX	6		/* shell syntax error */

__BEGIN_DECLS
int	wordexp(const char * __restrict, wordexp_t * __restrict, int);
void	wordfree(wordexp_t *);
__END_DECLS

#endif /* !_WORDEXP_H_ */
