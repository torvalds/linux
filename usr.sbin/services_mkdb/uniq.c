/*	$NetBSD: uniq.c,v 1.4 2008/04/28 20:24:17 martin Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2007 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
#include <stdlib.h>
#include <db.h>
#include <err.h>
#include <libutil.h>
#include <ctype.h>
#include <fcntl.h>

#include "extern.h"

static int comp(const char *, char **, size_t *);

/*
 * Preserve only unique content lines in a file. Input lines that have
 * content [alphanumeric characters before a comment] are white-space
 * normalized and have their comments removed. Then they are placed
 * in a hash table, and only the first instance of them is printed.
 * Comment lines without any alphanumeric content are always printed
 * since they are there to make the file "pretty". Comment lines with
 * alphanumeric content are also placed into the hash table and only
 * printed once.
 */
void
uniq(const char *fname)
{
	DB *db;
	DBT key;
	static const DBT data = { NULL, 0 };
	FILE *fp;
	char *line;
	size_t len;

	if ((db = dbopen(NULL, O_RDWR, 0, DB_HASH, &hinfo)) == NULL)
		err(1, "Cannot create in memory database");

	if ((fp = fopen(fname, "r")) == NULL)
		err(1, "Cannot open `%s'", fname);
	while ((line = fgetln(fp, &len)) != NULL) {
		size_t complen = len;
		char *compline;
		if (!comp(line, &compline, &complen)) {
			(void)fprintf(stdout, "%*.*s", (int)len, (int)len,
			    line);
			continue;
		}
		key.data = compline;
		key.size = complen;
		switch ((db->put)(db, &key, &data, R_NOOVERWRITE)) {
		case 0:
			(void)fprintf(stdout, "%*.*s", (int)len, (int)len,
			    line);
			break;
		case 1:
			break;
		case -1:
			err(1, "put");
		default:
			abort();
			break;
		}
	}
	(void)fflush(stdout);
	exit(0);
}

/*
 * normalize whitespace in the original line and place a new string
 * with whitespace converted to a single space in compline. If the line
 * contains just comments, we preserve them. If it contains data and
 * comments, we kill the comments. Return 1 if the line had actual
 * contents, or 0 if it was just a comment without alphanumeric characters.
 */
static int
comp(const char *origline, char **compline, size_t *len)
{
	const unsigned char *p;
	unsigned char *q;
	char *cline;
	size_t l = *len, complen;
	int hasalnum, iscomment;

	/* Eat leading space */
	for (p = (const unsigned char *)origline; l && *p && isspace(*p);
	    p++, l--)
		continue;
	if ((cline = malloc(l + 1)) == NULL)
		err(1, "Cannot allocate %zu bytes", l + 1);
	(void)memcpy(cline, p, l);
	cline[l] = '\0';
	if (*cline == '\0')
		return 0;

	complen = 0;
	hasalnum = 0;
	iscomment = 0;

	for (q = (unsigned char *)cline; l && *p; p++, l--) {
		if (isspace(*p)) {
			if (complen && isspace(q[-1]))
				continue;
			*q++ = ' ';
			complen++;
		} else {
			if (!iscomment && *p == '#') {
				if (hasalnum)
					break;
				iscomment = 1;
			} else
				hasalnum |= isalnum(*p);
			*q++ = *p;
			complen++;
		}
	}

	/* Eat trailing space */
	while (complen && isspace(q[-1])) {
		--q;
		--complen;
	}
	*q = '\0';
	*compline = cline;
	*len = complen;
	return hasalnum;
}
