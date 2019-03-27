/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
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
"@(#) Copyright (c) 1990, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)mkfifo.c	8.2 (Berkeley) 1/5/94";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	BASEMODE	S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | \
			S_IROTH | S_IWOTH

static void usage(void);

static int f_mode;

int
main(int argc, char *argv[])
{
	const char *modestr = NULL;
	const void *modep;
	mode_t fifomode;
	int ch, exitval;

	while ((ch = getopt(argc, argv, "m:")) != -1)
		switch(ch) {
		case 'm':
			f_mode = 1;
			modestr = optarg;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (argv[0] == NULL)
		usage();

	if (f_mode) {
		umask(0);
		errno = 0;
		if ((modep = setmode(modestr)) == NULL) {
			if (errno)
				err(1, "setmode");
			errx(1, "invalid file mode: %s", modestr);
		}
		fifomode = getmode(modep, BASEMODE);
	} else {
		fifomode = BASEMODE;
	}

	for (exitval = 0; *argv != NULL; ++argv)
		if (mkfifo(*argv, fifomode) < 0) {
			warn("%s", *argv);
			exitval = 1;
		}
	exit(exitval);
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: mkfifo [-m mode] fifo_name ...\n");
	exit(1);
}
