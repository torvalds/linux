/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 Kai Wang
 * Copyright (c) 2007 Tim Kientzle
 * Copyright (c) 2007 Joseph Koshy
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

/*-
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Hugh Smith at The University of Guelph.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/queue.h>
#include <sys/types.h>
#include <archive.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "ar.h"

enum options
{
	OPTION_HELP
};

static struct option longopts[] =
{
	{"help", no_argument, NULL, OPTION_HELP},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};

static void	bsdar_usage(void);
static void	ranlib_usage(void);
static void	set_mode(struct bsdar *bsdar, char opt);
static void	only_mode(struct bsdar *bsdar, const char *opt,
		    const char *valid_modes);
static void	bsdar_version(void);
static void	ranlib_version(void);

int
main(int argc, char **argv)
{
	struct bsdar	*bsdar, bsdar_storage;
	char		*p;
	size_t		 len;
	int		 i, opt, Dflag, Uflag;

	bsdar = &bsdar_storage;
	memset(bsdar, 0, sizeof(*bsdar));
	Dflag = 0;
	Uflag = 0;

	if ((bsdar->progname = getprogname()) == NULL)
		bsdar->progname = "ar";

	/* Act like ranlib if our name ends in "ranlib"; this
	 * accommodates arm-freebsd7.1-ranlib, bsdranlib, etc. */
	len = strlen(bsdar->progname);
	if (len >= strlen("ranlib") &&
	    strcmp(bsdar->progname + len - strlen("ranlib"), "ranlib") == 0) {
		while ((opt = getopt_long(argc, argv, "tDUV", longopts,
		    NULL)) != -1) {
			switch(opt) {
			case 't':
				/* Ignored. */
				break;
			case 'D':
				Dflag = 1;
				Uflag = 0;
				break;
			case 'U':
				Uflag = 1;
				Dflag = 0;
				break;
			case 'V':
				ranlib_version();
				break;
			case OPTION_HELP:
				ranlib_usage();
			default:
				ranlib_usage();
			}
		}
		argv += optind;
		argc -= optind;

		if (*argv == NULL)
			ranlib_usage();

		/* Enable determinstic mode unless -U is set. */
		if (Uflag == 0)
			bsdar->options |= AR_D;
		bsdar->options |= AR_S;
		while ((bsdar->filename = *argv++) != NULL)
			ar_mode_s(bsdar);

		exit(EX_OK);
	} else {
		if (argc < 2)
			bsdar_usage();

		if (*argv[1] != '-') {
			len = strlen(argv[1]) + 2;
			if ((p = malloc(len)) == NULL)
				bsdar_errc(bsdar, EX_SOFTWARE, errno,
				    "malloc failed");
			*p = '-';
			(void)strlcpy(p + 1, argv[1], len - 1);
			argv[1] = p;
		}
	}

	while ((opt = getopt_long(argc, argv, "abCcdDfijlMmopqrSsTtUuVvxz",
	    longopts, NULL)) != -1) {
		switch(opt) {
		case 'a':
			bsdar->options |= AR_A;
			break;
		case 'b':
		case 'i':
			bsdar->options |= AR_B;
			break;
		case 'C':
			bsdar->options |= AR_CC;
			break;
		case 'c':
			bsdar->options |= AR_C;
			break;
		case 'd':
			set_mode(bsdar, opt);
			break;
		case 'D':
			Dflag = 1;
			Uflag = 0;
			break;
		case 'f':
		case 'T':
			bsdar->options |= AR_TR;
			break;
		case 'j':
			/* ignored */
			break;
		case 'l':
			/* ignored, for GNU ar comptibility */
			break;
		case 'M':
			set_mode(bsdar, opt);
			break;
		case 'm':
			set_mode(bsdar, opt);
			break;
		case 'o':
			bsdar->options |= AR_O;
			break;
		case 'p':
			set_mode(bsdar, opt);
			break;
		case 'q':
			set_mode(bsdar, opt);
			break;
		case 'r':
			set_mode(bsdar, opt);
			break;
		case 'S':
			bsdar->options |= AR_SS;
			break;
		case 's':
			bsdar->options |= AR_S;
			break;
		case 't':
			set_mode(bsdar, opt);
			break;
		case 'U':
			Uflag = 1;
			Dflag = 0;
			break;
		case 'u':
			bsdar->options |= AR_U;
			break;
		case 'V':
			bsdar_version();
			break;
		case 'v':
			bsdar->options |= AR_V;
			break;
		case 'x':
			set_mode(bsdar, opt);
			break;
		case 'z':
			/* ignored */
			break;
		case OPTION_HELP:
			bsdar_usage();
		default:
			bsdar_usage();
		}
	}

	argv += optind;
	argc -= optind;

	if (*argv == NULL && bsdar->mode != 'M')
		bsdar_usage();

	if (bsdar->options & AR_A && bsdar->options & AR_B)
		bsdar_errc(bsdar, EX_USAGE, 0,
		    "only one of -a and -[bi] options allowed");

	if (bsdar->options & AR_J && bsdar->options & AR_Z)
		bsdar_errc(bsdar, EX_USAGE, 0,
		    "only one of -j and -z options allowed");

	if (bsdar->options & AR_S && bsdar->options & AR_SS)
		bsdar_errc(bsdar, EX_USAGE, 0,
		    "only one of -s and -S options allowed");

	if (bsdar->options & (AR_A | AR_B)) {
		if (*argv == NULL)
			bsdar_errc(bsdar, EX_USAGE, 0,
			    "no position operand specified");
		if ((bsdar->posarg = basename(*argv)) == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno,
			    "basename failed");
		argc--;
		argv++;
	}

	/* Set determinstic mode for -D, and by default without -U. */
	if (Dflag || (Uflag == 0 && (bsdar->mode == 'q' || bsdar->mode == 'r' ||
	    (bsdar->mode == '\0' && bsdar->options & AR_S))))
		bsdar->options |= AR_D;

	if (bsdar->options & AR_A)
		only_mode(bsdar, "-a", "mqr");
	if (bsdar->options & AR_B)
		only_mode(bsdar, "-b", "mqr");
	if (bsdar->options & AR_C)
		only_mode(bsdar, "-c", "qr");
	if (bsdar->options & AR_CC)
		only_mode(bsdar, "-C", "x");
	if (Dflag)
		only_mode(bsdar, "-D", "qr");
	if (Uflag)
		only_mode(bsdar, "-U", "qr");
	if (bsdar->options & AR_O)
		only_mode(bsdar, "-o", "x");
	if (bsdar->options & AR_SS)
		only_mode(bsdar, "-S", "mqr");
	if (bsdar->options & AR_U)
		only_mode(bsdar, "-u", "qrx");

	if (bsdar->mode == 'M') {
		ar_mode_script(bsdar);
		exit(EX_OK);
	}

	if ((bsdar->filename = *argv) == NULL)
		bsdar_usage();

	bsdar->argc = --argc;
	bsdar->argv = ++argv;

	if ((!bsdar->mode || strchr("ptx", bsdar->mode)) &&
	    bsdar->options & AR_S) {
		ar_mode_s(bsdar);
		if (!bsdar->mode)
			exit(EX_OK);
	}

	switch(bsdar->mode) {
	case 'd':
		ar_mode_d(bsdar);
		break;
	case 'm':
		ar_mode_m(bsdar);
		break;
	case 'p':
		ar_mode_p(bsdar);
		break;
	case 'q':
		ar_mode_q(bsdar);
		break;
	case 'r':
		ar_mode_r(bsdar);
		break;
	case 't':
		ar_mode_t(bsdar);
		break;
	case 'x':
		ar_mode_x(bsdar);
		break;
	default:
		bsdar_usage();
		/* NOTREACHED */
	}

	for (i = 0; i < bsdar->argc; i++)
		if (bsdar->argv[i] != NULL)
			bsdar_warnc(bsdar, 0, "%s: not found in archive",
			    bsdar->argv[i]);

	exit(EX_OK);
}

static void
set_mode(struct bsdar *bsdar, char opt)
{

	if (bsdar->mode != '\0' && bsdar->mode != opt)
		bsdar_errc(bsdar, EX_USAGE, 0,
		    "Can't specify both -%c and -%c", opt, bsdar->mode);
	bsdar->mode = opt;
}

static void
only_mode(struct bsdar *bsdar, const char *opt, const char *valid_modes)
{

	if (strchr(valid_modes, bsdar->mode) == NULL)
		bsdar_errc(bsdar, EX_USAGE, 0,
		    "Option %s is not permitted in mode -%c", opt, bsdar->mode);
}

static void
bsdar_usage(void)
{

	(void)fprintf(stderr, "usage:  ar -d [-Tjsvz] archive file ...\n");
	(void)fprintf(stderr, "\tar -m [-Tjsvz] archive file ...\n");
	(void)fprintf(stderr, "\tar -m [-Tabijsvz] position archive file ...\n");
	(void)fprintf(stderr, "\tar -p [-Tv] archive [file ...]\n");
	(void)fprintf(stderr, "\tar -q [-TcDjsUvz] archive file ...\n");
	(void)fprintf(stderr, "\tar -r [-TcDjsUuvz] archive file ...\n");
	(void)fprintf(stderr, "\tar -r [-TabcDijsUuvz] position archive file ...\n");
	(void)fprintf(stderr, "\tar -s [-jz] archive\n");
	(void)fprintf(stderr, "\tar -t [-Tv] archive [file ...]\n");
	(void)fprintf(stderr, "\tar -x [-CTouv] archive [file ...]\n");
	(void)fprintf(stderr, "\tar -V\n");
	exit(EX_USAGE);
}

static void
ranlib_usage(void)
{

	(void)fprintf(stderr, "usage:	ranlib [-DtU] archive ...\n");
	(void)fprintf(stderr, "\tranlib -V\n");
	exit(EX_USAGE);
}

static void
bsdar_version(void)
{
	(void)printf("BSD ar %s - %s\n", BSDAR_VERSION, archive_version_string());
	exit(EX_OK);
}

static void
ranlib_version(void)
{
	(void)printf("ranlib %s - %s\n", BSDAR_VERSION, archive_version_string());
	exit(EX_OK);
}
