/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1987, 1990, 1993, 1994
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
"@(#) Copyright (c) 1987, 1990, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#if 0
#ifndef lint
static char sccsid[] = "@(#)cmp.c	8.3 (Berkeley) 4/2/94";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>

#include <capsicum_helpers.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <nl_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

int	lflag, sflag, xflag, zflag;

static const struct option long_opts[] =
{
	{"verbose",	no_argument,		NULL, 'l'},
	{"silent",	no_argument,		NULL, 's'},
	{"quiet",	no_argument,		NULL, 's'},
	{NULL,		no_argument,		NULL, 0}
};

static void usage(void);

int
main(int argc, char *argv[])
{
	struct stat sb1, sb2;
	off_t skip1, skip2;
	int ch, fd1, fd2, oflag, special;
	const char *file1, *file2;

	oflag = O_RDONLY;
	while ((ch = getopt_long(argc, argv, "+hlsxz", long_opts, NULL)) != -1)
		switch (ch) {
		case 'h':		/* Don't follow symlinks */
			oflag |= O_NOFOLLOW;
			break;
		case 'l':		/* print all differences */
			lflag = 1;
			break;
		case 's':		/* silent run */
			sflag = 1;
			zflag = 1;
			break;
		case 'x':		/* hex output */
			lflag = 1;
			xflag = 1;
			break;
		case 'z':		/* compare size first */
			zflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	argv += optind;
	argc -= optind;

	if (lflag && sflag)
		errx(ERR_EXIT, "specifying -s with -l or -x is not permitted");

	if (argc < 2 || argc > 4)
		usage();

	/* Don't limit rights on stdin since it may be one of the inputs. */
	if (caph_limit_stream(STDOUT_FILENO, CAPH_WRITE | CAPH_IGNORE_EBADF))
		err(ERR_EXIT, "unable to limit rights on stdout");
	if (caph_limit_stream(STDERR_FILENO, CAPH_WRITE | CAPH_IGNORE_EBADF))
		err(ERR_EXIT, "unable to limit rights on stderr");

	/* Backward compatibility -- handle "-" meaning stdin. */
	special = 0;
	if (strcmp(file1 = argv[0], "-") == 0) {
		special = 1;
		fd1 = STDIN_FILENO;
		file1 = "stdin";
	} else if ((fd1 = open(file1, oflag, 0)) < 0 && errno != EMLINK) {
		if (!sflag)
			err(ERR_EXIT, "%s", file1);
		else
			exit(ERR_EXIT);
	}
	if (strcmp(file2 = argv[1], "-") == 0) {
		if (special)
			errx(ERR_EXIT,
				"standard input may only be specified once");
		special = 1;
		fd2 = STDIN_FILENO;
		file2 = "stdin";
	} else if ((fd2 = open(file2, oflag, 0)) < 0 && errno != EMLINK) {
		if (!sflag)
			err(ERR_EXIT, "%s", file2);
		else
			exit(ERR_EXIT);
	}

	skip1 = argc > 2 ? strtol(argv[2], NULL, 0) : 0;
	skip2 = argc == 4 ? strtol(argv[3], NULL, 0) : 0;

	if (fd1 == -1) {
		if (fd2 == -1) {
			c_link(file1, skip1, file2, skip2);
			exit(0);
		} else if (!sflag)
			errx(ERR_EXIT, "%s: Not a symbolic link", file2);
		else
			exit(ERR_EXIT);
	} else if (fd2 == -1) {
		if (!sflag)
			errx(ERR_EXIT, "%s: Not a symbolic link", file1);
		else
			exit(ERR_EXIT);
	}

	/* FD rights are limited in c_special() and c_regular(). */
	caph_cache_catpages();

	if (!special) {
		if (fstat(fd1, &sb1)) {
			if (!sflag)
				err(ERR_EXIT, "%s", file1);
			else
				exit(ERR_EXIT);
		}
		if (!S_ISREG(sb1.st_mode))
			special = 1;
		else {
			if (fstat(fd2, &sb2)) {
				if (!sflag)
					err(ERR_EXIT, "%s", file2);
				else
					exit(ERR_EXIT);
			}
			if (!S_ISREG(sb2.st_mode))
				special = 1;
		}
	}

	if (special)
		c_special(fd1, file1, skip1, fd2, file2, skip2);
	else {
		if (zflag && sb1.st_size != sb2.st_size) {
			if (!sflag)
				(void) printf("%s %s differ: size\n",
				    file1, file2);
			exit(DIFF_EXIT);
		}
		c_regular(fd1, file1, skip1, sb1.st_size,
		    fd2, file2, skip2, sb2.st_size);
	}
	exit(0);
}

static void
usage(void)
{

	(void)fprintf(stderr,
	    "usage: cmp [-l | -s | -x] [-hz] file1 file2 [skip1 [skip2]]\n");
	exit(ERR_EXIT);
}
