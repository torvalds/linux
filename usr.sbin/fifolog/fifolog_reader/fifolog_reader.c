/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2008 Poul-Henning Kamp
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

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <err.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>
#include <regex.h>

#include "libfifolog.h"

static time_t opt_B;
static time_t opt_E;
static const char *opt_T;
static const char *opt_o;
static const char *opt_R;
static regex_t R;

static FILE *fo;

static void
Render(void *priv __unused, time_t now, unsigned flag __unused, const unsigned char *p, unsigned l __unused)
{
	static struct tm utc;
	char buf[128];
	int i;

	if (now < opt_B || now > opt_E)
		return;

	if (opt_R != NULL && regexec(&R, (const char *)p, 0, NULL, 0))
		return;

	if (opt_T != NULL && *opt_T == '\0') {
		fprintf(fo, "%s\n", p);
	} else if (opt_T != NULL) {
		(void)gmtime_r(&now, &utc);
		i = strftime(buf, sizeof buf, opt_T, &utc);
		assert(i > 0);
		fprintf(fo, "%s %s\n", buf, p);
	} else {
		fprintf(fo, "%12ld %s\n", (long)now, p);
	}
}

/*--------------------------------------------------------------------*/

static void
Usage(void)
{
	fprintf(stderr,
		"Usage: fiforead [options] fifofile\n"
		"\t-b <start time integer>\n"
		"\t-B <start time>\n"
		"\t-e <end time integer>\n"
		"\t-E <end time>\n"
		"\t-o <output file>\n"
		"\t-R <regexp> # match regexp\n"
		"\t-t # format timestamps as %%Y%%m%%d%%H%%M%%S\n"
		"\t-T <timestamp format>\n"
	);
	exit (EX_USAGE);
}

int
main(int argc, char * const *argv)
{
	int ch, i;
	off_t o;
	struct fifolog_reader *fl;

	time(&opt_E);
	opt_o = "-";
	while ((ch = getopt(argc, argv, "b:B:e:E:o:R:tT:")) != -1) {
		switch (ch) {
		case 'b':
			opt_B = strtoul(optarg, NULL, 0);
			break;
		case 'B':
			opt_B = get_date(optarg);
			if (opt_B == -1)
				errx(1, "Didn't understand \"%s\"", optarg);
			break;
		case 'e':
			opt_E = strtoul(optarg, NULL, 0);
			break;
		case 'E':
			opt_E = get_date(optarg);
			if (opt_E == -1)
				errx(1, "Didn't understand \"%s\"", optarg);
			break;
		case 'o':
			opt_o = optarg;
			break;
		case 'R':
			opt_R = optarg;
			break;
		case 't':
			opt_T = "%Y%m%d%H%M%S";
			break;
		case 'T':
			opt_T = optarg;
			break;
		default:
			Usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (opt_R != NULL) {
		i = regcomp(&R, opt_R, REG_NOSUB);
		if (i != 0) {
			char buf[BUFSIZ];
			(void)regerror(i, &R, buf, sizeof buf);
			fprintf(stderr, "-R argument: %s\n", buf);
			exit (1);
		}
	}

	if (argv[0] == NULL)
		Usage();

	fprintf(stderr, "From\t%jd %s", (intmax_t)opt_B, ctime(&opt_B));
	fprintf(stderr, "To\t%jd %s", (intmax_t)opt_E, ctime(&opt_E));
	if (opt_B >= opt_E)
		errx(1, "Begin time not before End time");

	fl = fifolog_reader_open(argv[0]);

	if (!strcmp(opt_o, "-"))
		fo = stdout;
	else {
		fo = fopen(opt_o, "w");
		if (fo == NULL)
			err(1, "Cannot open: %s", argv[1]);
	}

	o = fifolog_reader_seek(fl, opt_B);
	fifolog_reader_process(fl, o, Render, NULL, opt_E);
	return (0);
}
