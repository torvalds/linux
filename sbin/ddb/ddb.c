/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Robert N. M. Watson
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "ddb.h"

void ddb_readfile(char *file);
void ddb_main(int argc, char *argv[]);

void
usage(void)
{

	fprintf(stderr, "usage: ddb capture [-M core] [-N system] print\n");
	fprintf(stderr, "       ddb capture [-M core] [-N system] status\n");
	fprintf(stderr, "       ddb script scriptname\n");
	fprintf(stderr, "       ddb script scriptname=script\n");
	fprintf(stderr, "       ddb scripts\n");
	fprintf(stderr, "       ddb unscript scriptname\n");
	fprintf(stderr, "       ddb pathname\n");
	exit(EX_USAGE);
}

void
ddb_readfile(char *filename)
{
	char    buf[BUFSIZ];
	FILE*	f;

	if ((f = fopen(filename, "r")) == NULL)
		err(EX_UNAVAILABLE, "fopen: %s", filename);

#define WHITESP		" \t"
#define MAXARG	 	2
	while (fgets(buf, BUFSIZ, f)) {
		int argc = 0;
		char *argv[MAXARG];
		size_t spn;

		spn = strlen(buf);
		if (buf[spn-1] == '\n')
			buf[spn-1] = '\0';

		spn = strspn(buf, WHITESP);
		argv[0] = buf + spn;
		if (*argv[0] == '#' || *argv[0] == '\0')
			continue;
		argc++;

		spn = strcspn(argv[0], WHITESP);
		argv[1] = argv[0] + spn + strspn(argv[0] + spn, WHITESP);
		argv[0][spn] = '\0';
		if (*argv[1] != '\0')
			argc++;

#ifdef DEBUG
		{
			int i;
			printf("argc = %d\n", argc);
			for (i = 0; i < argc; i++) {
				printf("arg[%d] = %s\n", i, argv[i]);
			}
		}
#endif
		ddb_main(argc, argv);
	}
	fclose(f);
}

void
ddb_main(int argc, char *argv[])
{

	if (argc < 1)
		usage();

	if (strcmp(argv[0], "capture") == 0)
		ddb_capture(argc, argv);
	else if (strcmp(argv[0], "script") == 0)
		ddb_script(argc, argv);
	else if (strcmp(argv[0], "scripts") == 0)
		ddb_scripts(argc, argv);
	else if (strcmp(argv[0], "unscript") == 0)
		ddb_unscript(argc, argv);
	else
		usage();
}

int
main(int argc, char *argv[])
{

	/*
	 * If we've only got one argument and it's an absolute path to a file,
	 * interpret as a file to be read in.
	 */
	if (argc == 2 && argv[1][0] == '/' && access(argv[1], R_OK) == 0)
		ddb_readfile(argv[1]);
	else
		ddb_main(argc-1, argv+1);
	exit(EX_OK);
}
