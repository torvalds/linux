/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1987, 1993, 1994
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1987, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#ifndef lint
static const char sccsid[] = "@(#)split.c	8.2 (Berkeley) 4/16/94";
#endif

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libutil.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <sysexits.h>

#define DEFLINE	1000			/* Default num lines per file. */

static off_t	 bytecnt;		/* Byte count to split on. */
static off_t	 chunks = 0;		/* Chunks count to split into. */
static long	 numlines;		/* Line count to split on. */
static int	 file_open;		/* If a file open. */
static int	 ifd = -1, ofd = -1;	/* Input/output file descriptors. */
static char	 bfr[MAXBSIZE];		/* I/O buffer. */
static char	 fname[MAXPATHLEN];	/* File name prefix. */
static regex_t	 rgx;
static int	 pflag;
static bool	 dflag;
static long	 sufflen = 2;		/* File name suffix length. */

static void newfile(void);
static void split1(void);
static void split2(void);
static void split3(void);
static void usage(void);

int
main(int argc, char **argv)
{
	int ch;
	int error;
	char *ep, *p;

	setlocale(LC_ALL, "");

	dflag = false;
	while ((ch = getopt(argc, argv, "0123456789a:b:dl:n:p:")) != -1)
		switch (ch) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			/*
			 * Undocumented kludge: split was originally designed
			 * to take a number after a dash.
			 */
			if (numlines == 0) {
				p = argv[optind - 1];
				if (p[0] == '-' && p[1] == ch && !p[2])
					numlines = strtol(++p, &ep, 10);
				else
					numlines =
					    strtol(argv[optind] + 1, &ep, 10);
				if (numlines <= 0 || *ep)
					errx(EX_USAGE,
					    "%s: illegal line count", optarg);
			}
			break;
		case 'a':		/* Suffix length */
			if ((sufflen = strtol(optarg, &ep, 10)) <= 0 || *ep)
				errx(EX_USAGE,
				    "%s: illegal suffix length", optarg);
			break;
		case 'b':		/* Byte count. */
			errno = 0;
			error = expand_number(optarg, &bytecnt);
			if (error == -1)
				errx(EX_USAGE, "%s: offset too large", optarg);
			break;
		case 'd':		/* Decimal suffix */
			dflag = true;
			break;
		case 'l':		/* Line count. */
			if (numlines != 0)
				usage();
			if ((numlines = strtol(optarg, &ep, 10)) <= 0 || *ep)
				errx(EX_USAGE,
				    "%s: illegal line count", optarg);
			break;
		case 'n':		/* Chunks. */
			if (!isdigit((unsigned char)optarg[0]) ||
			    (chunks = (size_t)strtoul(optarg, &ep, 10)) == 0 ||
			    *ep != '\0') {
				errx(EX_USAGE, "%s: illegal number of chunks",
				     optarg);
			}
			break;

		case 'p':		/* pattern matching. */
			if (regcomp(&rgx, optarg, REG_EXTENDED|REG_NOSUB) != 0)
				errx(EX_USAGE, "%s: illegal regexp", optarg);
			pflag = 1;
			break;
		default:
			usage();
		}
	argv += optind;
	argc -= optind;

	if (*argv != NULL) {			/* Input file. */
		if (strcmp(*argv, "-") == 0)
			ifd = STDIN_FILENO;
		else if ((ifd = open(*argv, O_RDONLY, 0)) < 0)
			err(EX_NOINPUT, "%s", *argv);
		++argv;
	}
	if (*argv != NULL)			/* File name prefix. */
		if (strlcpy(fname, *argv++, sizeof(fname)) >= sizeof(fname))
			errx(EX_USAGE, "file name prefix is too long");
	if (*argv != NULL)
		usage();

	if (strlen(fname) + (unsigned long)sufflen >= sizeof(fname))
		errx(EX_USAGE, "suffix is too long");
	if (pflag && (numlines != 0 || bytecnt != 0 || chunks != 0))
		usage();

	if (numlines == 0)
		numlines = DEFLINE;
	else if (bytecnt != 0 || chunks != 0)
		usage();

	if (bytecnt && chunks)
		usage();

	if (ifd == -1)				/* Stdin by default. */
		ifd = 0;

	if (bytecnt) {
		split1();
		exit (0);
	} else if (chunks) {
		split3();
		exit (0);
	}
	split2();
	if (pflag)
		regfree(&rgx);
	exit(0);
}

/*
 * split1 --
 *	Split the input by bytes.
 */
static void
split1(void)
{
	off_t bcnt;
	char *C;
	ssize_t dist, len;
	int nfiles;

	nfiles = 0;

	for (bcnt = 0;;)
		switch ((len = read(ifd, bfr, MAXBSIZE))) {
		case 0:
			exit(0);
		case -1:
			err(EX_IOERR, "read");
			/* NOTREACHED */
		default:
			if (!file_open) {
				if (!chunks || (nfiles < chunks)) {
					newfile();
					nfiles++;
				}
			}
			if (bcnt + len >= bytecnt) {
				dist = bytecnt - bcnt;
				if (write(ofd, bfr, dist) != dist)
					err(EX_IOERR, "write");
				len -= dist;
				for (C = bfr + dist; len >= bytecnt;
				    len -= bytecnt, C += bytecnt) {
					if (!chunks || (nfiles < chunks)) {
					newfile();
						nfiles++;
					}
					if (write(ofd,
					    C, bytecnt) != bytecnt)
						err(EX_IOERR, "write");
				}
				if (len != 0) {
					if (!chunks || (nfiles < chunks)) {
					newfile();
						nfiles++;
					}
					if (write(ofd, C, len) != len)
						err(EX_IOERR, "write");
				} else
					file_open = 0;
				bcnt = len;
			} else {
				bcnt += len;
				if (write(ofd, bfr, len) != len)
					err(EX_IOERR, "write");
			}
		}
}

/*
 * split2 --
 *	Split the input by lines.
 */
static void
split2(void)
{
	long lcnt = 0;
	FILE *infp;

	/* Stick a stream on top of input file descriptor */
	if ((infp = fdopen(ifd, "r")) == NULL)
		err(EX_NOINPUT, "fdopen");

	/* Process input one line at a time */
	while (fgets(bfr, sizeof(bfr), infp) != NULL) {
		const int len = strlen(bfr);

		/* If line is too long to deal with, just write it out */
		if (bfr[len - 1] != '\n')
			goto writeit;

		/* Check if we need to start a new file */
		if (pflag) {
			regmatch_t pmatch;

			pmatch.rm_so = 0;
			pmatch.rm_eo = len - 1;
			if (regexec(&rgx, bfr, 0, &pmatch, REG_STARTEND) == 0)
				newfile();
		} else if (lcnt++ == numlines) {
			newfile();
			lcnt = 1;
		}

writeit:
		/* Open output file if needed */
		if (!file_open)
			newfile();

		/* Write out line */
		if (write(ofd, bfr, len) != len)
			err(EX_IOERR, "write");
	}

	/* EOF or error? */
	if (ferror(infp))
		err(EX_IOERR, "read");
	else
		exit(0);
}

/*
 * split3 --
 *	Split the input into specified number of chunks
 */
static void
split3(void)
{
	struct stat sb;

	if (fstat(ifd, &sb) == -1) {
		err(1, "stat");
		/* NOTREACHED */
	}

	if (chunks > sb.st_size) {
		errx(1, "can't split into more than %d files",
		    (int)sb.st_size);
		/* NOTREACHED */
	}

	bytecnt = sb.st_size / chunks;
	split1();
}


/*
 * newfile --
 *	Open a new output file.
 */
static void
newfile(void)
{
	long i, maxfiles, tfnum;
	static long fnum;
	static char *fpnt;
	char beg, end;
	int pattlen;

	if (ofd == -1) {
		if (fname[0] == '\0') {
			fname[0] = 'x';
			fpnt = fname + 1;
		} else {
			fpnt = fname + strlen(fname);
		}
		ofd = fileno(stdout);
	}

	if (dflag) {
		beg = '0';
		end = '9';
	}
	else {
		beg = 'a';
		end = 'z';
	}
	pattlen = end - beg + 1;

	/* maxfiles = pattlen^sufflen, but don't use libm. */
	for (maxfiles = 1, i = 0; i < sufflen; i++)
		if (LONG_MAX / pattlen < maxfiles)
			errx(EX_USAGE, "suffix is too long (max %ld)", i);
		else
			maxfiles *= pattlen;

	if (fnum == maxfiles)
		errx(EX_DATAERR, "too many files");

	/* Generate suffix of sufflen letters */
	tfnum = fnum;
	i = sufflen - 1;
	do {
		fpnt[i] = tfnum % pattlen + beg;
		tfnum /= pattlen;
	} while (i-- > 0);
	fpnt[sufflen] = '\0';

	++fnum;
	if (!freopen(fname, "w", stdout))
		err(EX_IOERR, "%s", fname);
	file_open = 1;
}

static void
usage(void)
{
	(void)fprintf(stderr,
"usage: split [-l line_count] [-a suffix_length] [file [prefix]]\n"
"       split -b byte_count[K|k|M|m|G|g] [-a suffix_length] [file [prefix]]\n"
"       split -n chunk_count [-a suffix_length] [file [prefix]]\n"
"       split -p pattern [-a suffix_length] [file [prefix]]\n");
	exit(EX_USAGE);
}
