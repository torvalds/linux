/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993, 1994
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1991, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#if 0
#ifndef lint
static char sccsid[] = "@(#)basename.c	8.4 (Berkeley) 5/4/95";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <capsicum_helpers.h>
#include <err.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

void stripsuffix(char *, const char *, size_t);
void usage(void);

int
main(int argc, char **argv)
{
	char *p, *suffix;
	size_t suffixlen;
	int aflag, ch;

	setlocale(LC_ALL, "");

	if (caph_limit_stdio() < 0 || caph_enter() < 0)
		err(1, "capsicum");

	aflag = 0;
	suffix = NULL;
	suffixlen = 0;

	while ((ch = getopt(argc, argv, "as:")) != -1)
		switch(ch) {
		case 'a':
			aflag = 1;
			break;
		case 's':
			suffix = optarg;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	if (!*argv[0]) {
		printf("\n");
		exit(0);
	}
	if ((p = basename(argv[0])) == NULL)
		err(1, "%s", argv[0]);
	if ((suffix == NULL && !aflag) && argc == 2) {
		suffix = argv[1];
		argc--;
	}
	if (suffix != NULL)
		suffixlen = strlen(suffix);
	while (argc--) {
		if ((p = basename(*argv)) == NULL)
			err(1, "%s", argv[0]);
		stripsuffix(p, suffix, suffixlen);
		argv++;
		(void)printf("%s\n", p);
	}
	exit(0);
}

void
stripsuffix(char *p, const char *suffix, size_t suffixlen)
{
	char *q, *r;
	mbstate_t mbs;
	size_t n;

	if (suffixlen && (q = strchr(p, '\0') - suffixlen) > p &&
	    strcmp(suffix, q) == 0) {
		/* Ensure that the match occurred on a character boundary. */
		memset(&mbs, 0, sizeof(mbs));
		for (r = p; r < q; r += n) {
			n = mbrlen(r, MB_LEN_MAX, &mbs);
			if (n == (size_t)-1 || n == (size_t)-2) {
				memset(&mbs, 0, sizeof(mbs));
				n = 1;
			}
		}
		/* Chop off the suffix. */
		if (q == r)
			*q = '\0';
	}
}

void
usage(void)
{

	(void)fprintf(stderr,
"usage: basename string [suffix]\n"
"       basename [-a] [-s suffix] string [...]\n");
	exit(1);
}
