/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1988, 1993
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

#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1988, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#ifndef lint
static const char sccsid[] = "@(#)what.c	8.1 (Berkeley) 6/6/93";
#endif

#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(void);
static bool search(bool, bool, FILE *);

int
main(int argc, char *argv[])
{
	const char *file;
	FILE *in;
	bool found, qflag, sflag;
	int c;

	qflag = sflag = false;

	while ((c = getopt(argc, argv, "qs")) != -1) {
		switch (c) {
		case 'q':
			qflag = true;
			break;
		case 's':
			sflag = true;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	found = false;

	if (argc == 0) {
		if (search(sflag, qflag, stdin))
			found = true;
	} else {
		while (argc--) {
			file = *argv++;
			in = fopen(file, "r");
			if (in == NULL) {
				if (!qflag)
					warn("%s", file);
				continue;
			}
			if (!qflag)
				printf("%s:\n", file);
			if (search(sflag, qflag, in))
				found = true;
			fclose(in);
		}
	}
	exit(found ? 0 : 1);
}

static void
usage(void)
{
	fprintf(stderr, "usage: what [-qs] [file ...]\n");
	exit(1);
}

bool
search(bool one, bool quiet, FILE *in)
{
	bool found;
	int c;

	found = false;

	while ((c = getc(in)) != EOF) {
loop:		if (c != '@')
			continue;
		if ((c = getc(in)) != '(')
			goto loop;
		if ((c = getc(in)) != '#')
			goto loop;
		if ((c = getc(in)) != ')')
			goto loop;
		if (!quiet)
			putchar('\t');
		while ((c = getc(in)) != EOF && c && c != '"' &&
		    c != '>' && c != '\\' && c != '\n')
			putchar(c);
		putchar('\n');
		found = true;
		if (one)
			break;
	}
	return (found);
}
