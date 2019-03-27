/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Jeremie Le Hen <jlh@FreeBSD.org>
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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define	LIBSTDBUF	"/usr/lib/libstdbuf.so"
#define	LIBSTDBUF32	"/usr/lib32/libstdbuf.so"

extern char *__progname;

static void
usage(int s)
{

	fprintf(stderr, "Usage: %s [-e 0|L|B|<sz>] [-i 0|L|B|<sz>] [-o 0|L|B|<sz>] "
	    "<cmd> [args ...]\n", __progname);
	exit(s);
}

int
main(int argc, char *argv[])
{
	char *ibuf, *obuf, *ebuf;
	char *preload0, *preload1;
	int i;

	ibuf = obuf = ebuf = NULL;
	while ((i = getopt(argc, argv, "e:i:o:")) != -1) {
		switch (i) {
		case 'e':
			ebuf = optarg;
			break;
		case 'i':
			ibuf = optarg;
			break;
		case 'o':
			obuf = optarg;
			break;
		case '?':
		default:
			usage(1);
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc == 0)
		exit(0);

	if (ibuf != NULL && setenv("_STDBUF_I", ibuf, 1) == -1)
		warn("Failed to set environment variable: %s=%s",
		    "_STDBUF_I", ibuf);
	if (obuf != NULL && setenv("_STDBUF_O", obuf, 1) == -1)
		warn("Failed to set environment variable: %s=%s",
		    "_STDBUF_O", obuf);
	if (ebuf != NULL && setenv("_STDBUF_E", ebuf, 1) == -1)
		warn("Failed to set environment variable: %s=%s",
		    "_STDBUF_E", ebuf);

	preload0 = getenv("LD_PRELOAD");
	if (preload0 == NULL)
		i = asprintf(&preload1, "LD_PRELOAD=" LIBSTDBUF);
	else
		i = asprintf(&preload1, "LD_PRELOAD=%s:%s", preload0,
		    LIBSTDBUF);

	if (i < 0 || putenv(preload1) == -1)
		warn("Failed to set environment variable: LD_PRELOAD");

	preload0 = getenv("LD_32_PRELOAD");
	if (preload0 == NULL)
		i = asprintf(&preload1, "LD_32_PRELOAD=" LIBSTDBUF32);
	else
		i = asprintf(&preload1, "LD_32_PRELOAD=%s:%s", preload0,
		    LIBSTDBUF32);

	if (i < 0 || putenv(preload1) == -1)
		warn("Failed to set environment variable: LD_32_PRELOAD");

	execvp(argv[0], argv);
	err(2, "%s", argv[0]);
}
