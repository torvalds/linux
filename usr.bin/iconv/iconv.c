/* $FreeBSD$ */
/* $NetBSD: iconv.c,v 1.16 2009/02/20 15:28:21 yamt Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2003 Citrus Project,
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
#include <sys/capsicum.h>

#include <capsicum_helpers.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <iconv.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int		do_conv(FILE *, iconv_t, bool, bool);
static int		do_list(unsigned int, const char * const *, void *);
static void		usage(void) __dead2;

static const struct option long_options[] = {
	{"from-code",		required_argument,	NULL, 'f'},
	{"list",		no_argument,		NULL, 'l'},
	{"silent",		no_argument,		NULL, 's'},
        {"to-code",		required_argument,	NULL, 't'},
        {NULL,                  no_argument,            NULL, 0}
};

static void
usage(void)
{
	(void)fprintf(stderr,
	    "Usage:\t%1$s [-cs] -f <from_code> -t <to_code> [file ...]\n"
	    "\t%1$s -f <from_code> [-cs] [-t <to_code>] [file ...]\n"
	    "\t%1$s -t <to_code> [-cs] [-f <from_code>] [file ...]\n"
	    "\t%1$s -l\n", getprogname());
	exit(1);
}

#define INBUFSIZE 1024
#define OUTBUFSIZE (INBUFSIZE * 2)
static int
do_conv(FILE *fp, iconv_t cd, bool silent, bool hide_invalid)
{
	char inbuf[INBUFSIZE], outbuf[OUTBUFSIZE], *in, *out;
	unsigned long long invalids;
	size_t inbytes, outbytes, ret;

	int arg = (int)hide_invalid;
	if (iconvctl(cd, ICONV_SET_DISCARD_ILSEQ, (void *)&arg) == -1)
		err(EXIT_FAILURE, "iconvctl(DISCARD_ILSEQ, %d)", arg);

	invalids = 0;
	while ((inbytes = fread(inbuf, 1, INBUFSIZE, fp)) > 0) {
		in = inbuf;
		while (inbytes > 0) {
			size_t inval;

			out = outbuf;
			outbytes = OUTBUFSIZE;
			ret = __iconv(cd, &in, &inbytes, &out, &outbytes,
			    0, &inval);
			invalids += inval;
			if (outbytes < OUTBUFSIZE)
				(void)fwrite(outbuf, 1, OUTBUFSIZE - outbytes,
				    stdout);
			if (ret == (size_t)-1 && errno != E2BIG) {
				if (errno != EINVAL || in == inbuf)
					err(EXIT_FAILURE, "iconv()");

				/* incomplete input character */
				(void)memmove(inbuf, in, inbytes);
				ret = fread(inbuf + inbytes, 1,
				    INBUFSIZE - inbytes, fp);
				if (ret == 0) {
					fflush(stdout);
					if (feof(fp))
						errx(EXIT_FAILURE,
						    "unexpected end of file; "
						    "the last character is "
						    "incomplete.");
					else
						err(EXIT_FAILURE, "fread()");
				}
				in = inbuf;
				inbytes += ret;
			}
		}
	}
	/* reset the shift state of the output buffer */
	outbytes = OUTBUFSIZE;
	out = outbuf;
	ret = iconv(cd, NULL, NULL, &out, &outbytes);
	if (ret == (size_t)-1)
		err(EXIT_FAILURE, "iconv()");
	if (outbytes < OUTBUFSIZE)
		(void)fwrite(outbuf, 1, OUTBUFSIZE - outbytes, stdout);

	if (invalids > 0 && !silent)
		warnx("warning: invalid characters: %llu", invalids);

	return (invalids > 0);
}

static int
do_list(unsigned int n, const char * const *list, void *data __unused)
{
	unsigned int i;

	for(i = 0; i < n; i++) {
		printf("%s", list[i]);
		if (i < n - 1)
			printf(" ");
	}
	printf("\n");

	return (1);
}

int
main(int argc, char **argv)
{
	iconv_t cd;
	FILE *fp;
	const char *opt_f, *opt_t;
	int ch, i, res;
	bool opt_c = false, opt_s = false;

	opt_f = opt_t = "";

	setlocale(LC_ALL, "");
	setprogname(argv[0]);

	while ((ch = getopt_long(argc, argv, "csLlf:t:",
	    long_options, NULL)) != -1) {
		switch (ch) {
		case 'c':
			opt_c = true;
			break;
		case 's':
			opt_s = true;
			break;
		case 'l':
			/* list */
			if (opt_s || opt_c || strcmp(opt_f, "") != 0 ||
			    strcmp(opt_t, "") != 0) {
				warnx("-l is not allowed with other flags.");
				usage();
			}
			iconvlist(do_list, NULL);
			return (EXIT_SUCCESS);
		case 'f':
			/* from */
			if (optarg != NULL)
				opt_f = optarg;
			break;
		case 't':
			/* to */
			if (optarg != NULL)
				opt_t = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if ((strcmp(opt_f, "") == 0) && (strcmp(opt_t, "") == 0))
		usage();

	if (caph_limit_stdio() < 0)
		err(EXIT_FAILURE, "capsicum");

	/*
	 * Cache NLS data, for strerror, for err(3), before entering capability
	 * mode.
	 */
	caph_cache_catpages();

	/*
	 * Cache iconv conversion handle before entering sandbox.
	 */
	cd = iconv_open(opt_t, opt_f);
	if (cd == (iconv_t)-1)
		err(EXIT_FAILURE, "iconv_open(%s, %s)", opt_t, opt_f);

	if (argc == 0) {
		if (caph_enter() < 0)
			err(EXIT_FAILURE, "unable to enter capability mode");
		res = do_conv(stdin, cd, opt_s, opt_c);
	} else {
		res = 0;
		for (i = 0; i < argc; i++) {
			fp = (strcmp(argv[i], "-") != 0) ?
			    fopen(argv[i], "r") : stdin;
			if (fp == NULL)
				err(EXIT_FAILURE, "Cannot open `%s'",
				    argv[i]);
			/* Enter Capsicum sandbox for final input file. */
			if (i + 1 == argc && caph_enter() < 0)
				err(EXIT_FAILURE,
				    "unable to enter capability mode");
			res |= do_conv(fp, cd, opt_s, opt_c);
			(void)fclose(fp);

			/* Reset iconv descriptor state. */
			(void)iconv(cd, NULL, NULL, NULL, NULL);
		}
	}
	iconv_close(cd);
	return (res == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
