/*-
 * Generic "support" routines to replace those obtained from libiberty for ld.
 *
 * I've collected these from random bits of (published) code I've written
 * over the years, not that they are a big deal.  peter@freebsd.org
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1996
 *	Peter Wemm.  All rights reserved.
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
 *-
 * $FreeBSD$
 */
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <err.h>

#include "support.h"

char *
concat(const char *s1, const char *s2, const char *s3)
{
	int len = 1;
	char *s;
	if (s1)
		len += strlen(s1);
	if (s2)
		len += strlen(s2);
	if (s3)
		len += strlen(s3);
	s = xmalloc(len);
	s[0] = '\0';
	if (s1)
		strcat(s, s1);
	if (s2)
		strcat(s, s2);
	if (s3)
		strcat(s, s3);
	return s;
}

void *
xmalloc(size_t n)
{
	char *p = malloc(n);

	if (p == NULL)
		errx(1, "Could not allocate memory");

	return p;
}

void *
xrealloc(void *p, size_t n)
{
	p = realloc(p, n);

	if (p == NULL)
		errx(1, "Could not allocate memory");

	return p;
}
