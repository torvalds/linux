/*	$OpenBSD: misc.c,v 1.20 2017/09/25 17:10:55 krw Exp $	*/
/*	$NetBSD: misc.c,v 1.4 1995/03/07 21:26:23 cgd Exp $	*/

/*-
 * Copyright (c) 1991, 1993
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
 *	@(#)misc.c	8.1 (Berkeley) 6/6/93
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdarg.h>
#include "mtree.h"
#include "extern.h"

extern int lineno;

typedef struct _key {
	char *name;			/* key name */
	u_int val;			/* value */

#define	NEEDVALUE	0x01
	u_int flags;
} KEY;

/* NB: the following table must be sorted lexically. */
static KEY keylist[] = {
	{"cksum",	F_CKSUM,	NEEDVALUE},
	{"flags",	F_FLAGS,	NEEDVALUE},
	{"gid",		F_GID,		NEEDVALUE},
	{"gname",	F_GNAME,	NEEDVALUE},
	{"ignore",	F_IGN,		0},
	{"link",	F_SLINK,	NEEDVALUE},
	{"md5digest",	F_MD5,		NEEDVALUE},
	{"mode",	F_MODE,		NEEDVALUE},
	{"nlink",	F_NLINK,	NEEDVALUE},
	{"nochange",	F_NOCHANGE,	0},
	{"optional",	F_OPT,		0},
	{"rmd160digest",F_RMD160,	NEEDVALUE},
	{"sha1digest",	F_SHA1,		NEEDVALUE},
	{"sha256digest",F_SHA256,	NEEDVALUE},
	{"size",	F_SIZE,		NEEDVALUE},
	{"time",	F_TIME,		NEEDVALUE},
	{"type",	F_TYPE,		NEEDVALUE},
	{"uid",		F_UID,		NEEDVALUE},
	{"uname",	F_UNAME,	NEEDVALUE},
};

u_int
parsekey(char *name, int *needvaluep)
{
	KEY *k, tmp;
	int keycompare(const void *, const void *);

	tmp.name = name;
	k = (KEY *)bsearch(&tmp, keylist, sizeof(keylist) / sizeof(KEY),
	    sizeof(KEY), keycompare);
	if (k == NULL)
		error("unknown keyword %s", name);

	if (needvaluep)
		*needvaluep = k->flags & NEEDVALUE ? 1 : 0;
	return (k->val);
}

int
keycompare(const void *a, const void *b)
{
	return (strcmp(((KEY *)a)->name, ((KEY *)b)->name));
}

__dead void
error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)fflush(NULL);
	(void)fprintf(stderr, "\nmtree: ");
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void)fprintf(stderr, "\n");
	if (lineno)
		(void)fprintf(stderr,
		    "mtree: failed at line %d of the specification\n", lineno);
	exit(1);
	/* NOTREACHED */
}
