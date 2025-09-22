/*	$OpenBSD: main.c,v 1.24 2018/09/24 21:26:02 deraadt Exp $	*/
/*	$NetBSD: main.c,v 1.8 1996/10/17 20:29:53 cgd Exp $	*/

/*
 * Copyright (C) 1995 Wolfgang Solfrank
 * Copyright (c) 1995 Martin Husemann
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <err.h>

#include "ext.h"

int alwaysno;		/* assume "no" for all questions */
int alwaysyes;		/* assume "yes" for all questions */
int preen;		/* set when preening */
int rdonly;		/* device is opened read only (supersedes above) */

static void usage(void);
int main(int, char **);

static void
usage(void)
{
	errexit("usage: fsck_msdos [-fnpy] filesystem\n");
}

int
main(int argc, char *argv[])
{
	int ch;

	checkroot();

	while ((ch = getopt(argc, argv, "pynf")) != -1) {
		switch (ch) {
		case 'f':
			/* Ignore for consistency with fsck_ffs */
			break;

		case 'n':
			alwaysno = 1;
			alwaysyes = preen = 0;
			break;

		case 'y':
			alwaysyes = 1;
			alwaysno = preen = 0;
			break;

		case 'p':
			preen = 1;
			alwaysyes = alwaysno = 0;
			break;

		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	setcdevname(*argv, NULL, preen);
	exit (checkfilesys(blockcheck(*argv)));
}

int
ask(int def, const char *fmt, ...)
{
	va_list ap;

	char prompt[256];
	int c;

	if (preen) {
		if (rdonly)
			def = 0;
		if (def)
			printf("FIXED\n");
		return (def);
	}

	va_start(ap, fmt);
	vsnprintf(prompt, sizeof(prompt), fmt, ap);
	va_end(ap);
	if (alwaysyes || rdonly) {
		printf("%s? %s\n", prompt, rdonly ? "no" : "yes");
		return (!rdonly);
	}
	do {
		printf("%s? [Fyn] ", prompt);
		fflush(stdout);
		c = getchar();
		if (c == 'F') {
			alwaysyes = 1;
			return (1);
		}
		while (c != '\n' && getchar() != '\n')
			if (feof(stdin))
				return (0);
	} while (c != 'y' && c != 'Y' && c != 'n' && c != 'N');
	return (c == 'y' || c == 'Y');
}
