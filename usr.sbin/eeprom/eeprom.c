/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 *
 *	from: NetBSD: main.c,v 1.15 2001/02/19 23:22:42 cgd Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "ofw_options.h"

static int	action(char *);
static void	dump_config(void);
static void	usage(void);

static void
usage(void)
{

	fprintf(stderr,
	    "usage: eeprom -a\n"
	    "       eeprom [-] name[=value] ...\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	int do_stdin, opt;
	int aflag, rv;
	char *cp;
	char line[BUFSIZ];

	aflag = do_stdin = 0;
	rv = EX_OK;
	while ((opt = getopt(argc, argv, "-a")) != -1) {
		switch (opt) {
		case '-':
			if (aflag)
				usage();
			do_stdin = 1;
			break;
		case 'a':
			if (do_stdin)
				usage();
			aflag = 1;
			break;
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (aflag) {
		if (argc != 0)
			usage();
		dump_config();
	} else {
		if (do_stdin) {
			while (fgets(line, BUFSIZ, stdin) != NULL &&
			    rv == EX_OK) {
				if (line[0] == '\n')
					continue;
				if ((cp = strrchr(line, '\n')) != NULL)
					*cp = '\0';
				rv = action(line);
			}
			if (ferror(stdin))
				err(EX_NOINPUT, "stdin");
		} else {
			if (argc == 0)
				usage();
			while (argc && rv == EX_OK) {
				rv = action(*argv);
				++argv;
				--argc;
			}
		}
	}
	return (rv);
}

static int
action(char *line)
{
	int rv;
	char *keyword, *arg;

	keyword = strdup(line);
	if (keyword == NULL)
		err(EX_OSERR, "malloc() failed");
	if ((arg = strrchr(keyword, '=')) != NULL)
		*arg++ = '\0';
	switch (rv = ofwo_action(keyword, arg)) {
		case EX_UNAVAILABLE:
			warnx("nothing available for '%s'.", keyword);
			break;
		case EX_DATAERR:
			warnx("invalid value '%s' for '%s'.", arg, keyword);
			break;
	}
	free(keyword);
	return(rv);
}

static void
dump_config(void)
{

	ofwo_dump();
}
