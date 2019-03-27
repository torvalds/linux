/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Cimarron D. Taylor of the University of California, Berkeley.
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
static char sccsid[] = "@(#)misc.c	8.2 (Berkeley) 4/1/94";
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "find.h"

/*
 * brace_subst --
 *	Replace occurrences of {} in s1 with s2 and return the result string.
 */
void
brace_subst(char *orig, char **store, char *path, size_t len)
{
	const char *pastorigend, *p, *q;
	char *dst;
	size_t newlen, plen;

	plen = strlen(path);
	newlen = strlen(orig) + 1;
	pastorigend = orig + newlen;
	for (p = orig; (q = strstr(p, "{}")) != NULL; p = q + 2) {
		if (plen > 2 && newlen + plen - 2 < newlen)
			errx(2, "brace_subst overflow");
		newlen += plen - 2;
	}
	if (newlen > len) {
		*store = reallocf(*store, newlen);
		if (*store == NULL)
			err(2, NULL);
	}
	dst = *store;
	for (p = orig; (q = strstr(p, "{}")) != NULL; p = q + 2) {
		memcpy(dst, p, q - p);
		dst += q - p;
		memcpy(dst, path, plen);
		dst += plen;
	}
	memcpy(dst, p, pastorigend - p);
}

/*
 * queryuser --
 *	print a message to standard error and then read input from standard
 *	input. If the input is an affirmative response (according to the
 *	current locale) then 1 is returned.
 */
int
queryuser(char *argv[])
{
	char *p, resp[256];

	(void)fprintf(stderr, "\"%s", *argv);
	while (*++argv)
		(void)fprintf(stderr, " %s", *argv);
	(void)fprintf(stderr, "\"? ");
	(void)fflush(stderr);

	if (fgets(resp, sizeof(resp), stdin) == NULL)
		*resp = '\0';
	if ((p = strchr(resp, '\n')) != NULL)
		*p = '\0';
	else {
		(void)fprintf(stderr, "\n");
		(void)fflush(stderr);
	}
        return (rpmatch(resp) == 1);
}
