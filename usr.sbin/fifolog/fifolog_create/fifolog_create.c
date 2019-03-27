/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2008 Poul-Henning Kamp
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

#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <err.h>
#include <libutil.h>

#include "libfifolog.h"

#define DEF_RECSIZE	512
#define DEF_RECCNT	(24 * 60 * 60)

static void
usage(void)
{
	fprintf(stderr, "Usage: fifolog_create [-l record-size] "
	    "[-r record-count] [-s size] file\n");
	exit(EX_USAGE);
}

int
main(int argc, char * const *argv)
{
	int ch;
	int64_t size;
	int64_t recsize;
	int64_t reccnt;
	const char *s;

	recsize = 0;
	size = 0;
	reccnt = 0;
	while((ch = getopt(argc, argv, "l:r:s:")) != -1) {
		switch (ch) {
		case 'l':
			if (expand_number(optarg, &recsize))
				err(1, "Couldn't parse -l argument");
			break;
		case 'r':
			if (expand_number(optarg, &reccnt))
				err(1, "Couldn't parse -r argument");
			break;
		case 's':
			if (expand_number(optarg, &size))
				err(1, "Couldn't parse -s argument");
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();

	if (size != 0 && reccnt != 0 && recsize != 0) {		/* N N N */
		if (size !=  reccnt * recsize)
			errx(1, "Inconsistent -l, -r and -s values");
	} else if (size != 0 && reccnt != 0 && recsize == 0) {	/* N N Z */
		if (size % reccnt)
			errx(1,
			    "Inconsistent -r and -s values (gives remainder)");
		recsize = size / reccnt;
	} else if (size != 0 && reccnt == 0 && recsize != 0) {	/* N Z N */
		if (size % recsize)
		    errx(1, "-s arg not divisible by -l arg");
	} else if (size != 0 && reccnt == 0 && recsize == 0) {	/* N Z Z */
		recsize = DEF_RECSIZE;
		if (size % recsize)
		    errx(1, "-s arg not divisible by %jd", recsize);
	} else if (size == 0 && reccnt != 0 && recsize != 0) {	/* Z N N */
		size = reccnt * recsize;
	} else if (size == 0 && reccnt != 0 && recsize == 0) {	/* Z N Z */
		recsize = DEF_RECSIZE;
		size = reccnt * recsize;
	} else if (size == 0 && reccnt == 0 && recsize != 0) {	/* Z Z N */
		size = DEF_RECCNT * recsize;
	} else if (size == 0 && reccnt == 0 && recsize == 0) {	/* Z Z Z */
		recsize = DEF_RECSIZE;
		size = DEF_RECCNT * recsize;
	}

	s = fifolog_create(argv[0], size, recsize);
	if (s == NULL)
		return (0);
	err(1, "%s", s);
}
