/*
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Guy Harris at Network Appliance Corp.
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
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static const char sccsid[] = "@(#)random.c	8.5 (Berkeley) 4/5/94";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "randomize_fd.h"

static void usage(void);

int
main(int argc, char *argv[])
{
	double denom;
	int ch, fd, random_exit, randomize_lines, random_type, ret,
		selected, unique_output, unbuffer_output;
	char *ep;
	const char *filename;

	denom = 0;
	filename = "/dev/fd/0";
	random_type = RANDOM_TYPE_UNSET;
	random_exit = randomize_lines = unbuffer_output = 0;
	unique_output = 1;

	(void)setlocale(LC_CTYPE, "");

	while ((ch = getopt(argc, argv, "ef:hlruUw")) != -1)
		switch (ch) {
		case 'e':
			random_exit = 1;
			break;
		case 'f':
			randomize_lines = 1;
			if (strcmp(optarg, "-") != 0)
				filename = optarg;
			break;
		case 'l':
			randomize_lines = 1;
			random_type = RANDOM_TYPE_LINES;
			break;
		case 'r':
			unbuffer_output = 1;
			break;
		case 'u':
			randomize_lines = 1;
			unique_output = 1;
			break;
		case 'U':
			randomize_lines = 1;
			unique_output = 0;
			break;
		case 'w':
			randomize_lines = 1;
			random_type = RANDOM_TYPE_WORDS;
			break;
		default:
		case '?':
			usage();
			/* NOTREACHED */
		}

	argc -= optind;
	argv += optind;

	switch (argc) {
	case 0:
		denom = (randomize_lines ? 1 : 2);
		break;
	case 1:
		errno = 0;
		denom = strtod(*argv, &ep);
		if (errno == ERANGE)
			err(1, "%s", *argv);
		if (denom <= 0 || *ep != '\0')
			errx(1, "denominator is not valid.");
		if (random_exit && denom > 256)
			errx(1, "denominator must be <= 256 for random exit.");
		break;
	default:
		usage();
		/* NOTREACHED */
	}

	srandomdev();

	/*
	 * Act as a filter, randomly choosing lines of the standard input
	 * to write to the standard output.
	 */
	if (unbuffer_output)
		setbuf(stdout, NULL);

	/*
	 * Act as a filter, randomizing lines read in from a given file
	 * descriptor and write the output to standard output.
	 */
	if (randomize_lines) {
		if ((fd = open(filename, O_RDONLY, 0)) < 0)
			err(1, "%s", filename);
		ret = randomize_fd(fd, random_type, unique_output, denom);
		if (!random_exit)
			return(ret);
	}

	/* Compute a random exit status between 0 and denom - 1. */
	if (random_exit)
		return (int)(denom * random() / RANDOM_MAX_PLUS1);

	/*
	 * Select whether to print the first line.  (Prime the pump.)
	 * We find a random number between 0 and denom - 1 and, if it's
	 * 0 (which has a 1 / denom chance of being true), we select the
	 * line.
	 */
	selected = (int)(denom * random() / RANDOM_MAX_PLUS1) == 0;
	while ((ch = getchar()) != EOF) {
		if (selected)
			(void)putchar(ch);
		if (ch == '\n') {
			/* End of that line.  See if we got an error. */
			if (ferror(stdout))
				err(2, "stdout");

			/* Now see if the next line is to be printed. */
			selected = (int)(denom * random() /
				RANDOM_MAX_PLUS1) == 0;
		}
	}
	if (ferror(stdin))
		err(2, "stdin");
	exit (0);
}

static void
usage(void)
{

	fprintf(stderr, "usage: random [-elrUuw] [-f filename] [denominator]\n");
	exit(1);
}
