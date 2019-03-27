/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * This is the Posix.2 mandated C compiler.  Basically, a hook to the
 * cc(1) command.
 *
 * Copyright (c) 2001 by Jens Schweikhardt
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#define	CC "/usr/bin/cc"	/* The big kahuna doing the actual work. */
#define	N_ARGS_PREPENDED (sizeof(args_prepended) / sizeof(args_prepended[0]))

/*
 * We do not add -D_POSIX_SOURCE here because any POSIX source is supposed to
 * define it before inclusion of POSIX headers. This has the additional
 * benefit of making c89 -D_ANSI_SOURCE do the right thing (or any other
 * -D_FOO_SOURCE feature test macro we support.)
 */
static const char	*args_prepended[] = {
	"-std=iso9899:199409",
	"-pedantic"
};

static void	usage(void);

/*
 * Prepend the strings from args_prepended[] to the arg list; parse options,
 * accepting only the POSIX c89 mandated options. Then exec cc to do the
 * actual work.
 */
int
main(int argc, char **argv)
{
	int Argc, i;
	size_t j;
	union {
		const char **a;
		char * const *b;
	} Argv;

	Argc = 0;
	Argv.a = malloc((argc + 1 + N_ARGS_PREPENDED) * sizeof *Argv.a);
	if (Argv.a == NULL)
		err(1, "malloc");
	Argv.a[Argc++] = CC;
	for (j = 0; j < N_ARGS_PREPENDED; ++j)
		Argv.a[Argc++] = args_prepended[j];
	while ((i = getopt(argc, argv, "cD:EgI:l:L:o:OsU:")) != -1) {
		if (i == '?')
			usage();
		if (i == 'l') {
			if (argv[optind - 1][0] == '-') /* -llib */
				optind -= 1;
			else                            /* -l lib */
				optind -= 2;
			break; /* -llib or -l lib starts the operands. */
		}
	}
	if (argc == optind) {
		warnx("missing operand");
		usage();
	}

	/* Append argv[1..] at the end of Argv[].a. */
	for (i = 1; i <= argc; ++i)
		Argv.a[Argc++] = argv[i];
	(void)execv(CC, Argv.b);
	err(1, "execv(" CC ")");
}

static void
usage(void)
{
	fprintf(stderr,
"usage: c89 [-cEgOs] [-D name[=value]] ... [-I directory] ... [-L directory] ...\n"
"           [-o outfile] [-U name] ... operand ...\n"
"\n"
"       where operand is one or more of file.c, file.o, file.a\n"
"       or -llibrary\n");
	exit(1);
}
