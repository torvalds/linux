/*	$OpenBSD: main.c,v 1.26 2022/11/09 07:20:12 miod Exp $	*/
/*	$NetBSD: main.c,v 1.3 1996/05/16 16:00:55 thorpej Exp $	*/

/*-
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
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#include <machine/openpromio.h>

#include "defs.h"

static	void action(char *);
static	void dump_prom(void);
static	void usage(void);

char	*path_openprom = "/dev/openprom";
int	eval = 0;
int	print_tree = 0;
int	verbose = 0;

extern	char *__progname;

int
main(int argc, char *argv[])
{
	int ch, do_stdin = 0;
	char *cp, line[BUFSIZE];
	char *optstring = "f:pv-";

	while ((ch = getopt(argc, argv, optstring)) != -1)
		switch (ch) {
		case '-':
			do_stdin = 1;
			break;
		case 'f':
			path_openprom = optarg;
			break;
		case 'p':
			print_tree = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (print_tree) {
		op_tree();
		exit(0);
	}

	if (do_stdin) {
		while (fgets(line, BUFSIZE, stdin) != NULL) {
			if (line[0] == '\n')
				continue;
			if ((cp = strrchr(line, '\n')) != NULL)
				*cp = '\0';
			action(line);
		}
		if (ferror(stdin))
			err(++eval, "stdin");
	} else {
		if (argc == 0) {
			dump_prom();
			exit(eval);
		}

		while (argc) {
			action(*argv);
			++argv;
			--argc;
		}
	}

	exit(eval);
}

/*
 * Separate the keyword from the argument (if any), find the keyword in
 * the table, and call the corresponding handler function.
 */
static void
action(char *line)
{
	char *keyword, *arg, *cp;

	keyword = strdup(line);
	if (!keyword)
		errx(1, "out of memory");
	if ((arg = strrchr(keyword, '=')) != NULL)
		*arg++ = '\0';

	/*
	 * The whole point of the Openprom is that one
	 * isn't required to know the keywords.  With this
	 * in mind, we just dump the whole thing off to
	 * the generic op_handler.
	 */
	if ((cp = op_handler(keyword, arg)) != NULL)
		warnx("%s", cp);
}

/*
 * Dump the contents of the prom corresponding to all known keywords.
 */
static void
dump_prom(void)
{
	/*
	 * We have a special dump routine for this.
	 */
	op_dump();
}

static void
usage(void)
{

	fprintf(stderr,
	    "usage: %s [-pv] [-f device] [field[=value] ...]\n",
	    __progname);
	exit(1);
}
