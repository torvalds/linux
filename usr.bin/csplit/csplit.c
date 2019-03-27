/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Tim J. Robbins.
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

/*
 * csplit -- split files based on context
 *
 * This utility splits its input into numbered output files by line number
 * or by a regular expression. Regular expression matches have an optional
 * offset with them, allowing the split to occur a specified number of
 * lines before or after the match.
 *
 * To handle negative offsets, we stop reading when the match occurs and
 * store the offset that the file should have been split at, then use
 * this output file as input until all the "overflowed" lines have been read.
 * The file is then closed and truncated to the correct length.
 *
 * We assume that the output files can be seeked upon (ie. they cannot be
 * symlinks to named pipes or character devices), but make no such
 * assumption about the input.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void	 cleanup(void);
static void	 do_lineno(const char *);
static void	 do_rexp(const char *);
static char	*get_line(void);
static void	 handlesig(int);
static FILE	*newfile(void);
static void	 toomuch(FILE *, long);
static void	 usage(void);

/*
 * Command line options
 */
static const char *prefix;	/* File name prefix */
static long	 sufflen;	/* Number of decimal digits for suffix */
static int	 sflag;		/* Suppress output of file names */
static int	 kflag;		/* Keep output if error occurs */

/*
 * Other miscellaneous globals (XXX too many)
 */
static long	 lineno;	/* Current line number in input file */
static long	 reps;		/* Number of repetitions for this pattern */
static long	 nfiles;	/* Number of files output so far */
static long	 maxfiles;	/* Maximum number of files we can create */
static char	 currfile[PATH_MAX]; /* Current output file */
static const char *infn;	/* Name of the input file */
static FILE	*infile;	/* Input file handle */
static FILE	*overfile;	/* Overflow file for toomuch() */
static off_t	 truncofs;	/* Offset this file should be truncated at */
static int	 doclean;	/* Should cleanup() remove output? */

int
main(int argc, char *argv[])
{
	struct sigaction sa;
	long i;
	int ch;
	const char *expr;
	char *ep, *p;
	FILE *ofp;

	setlocale(LC_ALL, "");

	kflag = sflag = 0;
	prefix = "xx";
	sufflen = 2;
	while ((ch = getopt(argc, argv, "ksf:n:")) > 0) {
		switch (ch) {
		case 'f':
			prefix = optarg;
			break;
		case 'k':
			kflag = 1;
			break;
		case 'n':
			errno = 0;
			sufflen = strtol(optarg, &ep, 10);
			if (sufflen <= 0 || *ep != '\0' || errno != 0)
				errx(1, "%s: bad suffix length", optarg);
			break;
		case 's':
			sflag = 1;
			break;
		default:
			usage();
			/*NOTREACHED*/
		}
	}

	if (sufflen + strlen(prefix) >= PATH_MAX)
		errx(1, "name too long");

	argc -= optind;
	argv += optind;

	if ((infn = *argv++) == NULL)
		usage();
	if (strcmp(infn, "-") == 0) {
		infile = stdin;
		infn = "stdin";
	} else if ((infile = fopen(infn, "r")) == NULL)
		err(1, "%s", infn);

	if (!kflag) {
		doclean = 1;
		atexit(cleanup);
		sa.sa_flags = 0;
		sa.sa_handler = handlesig;
		sigemptyset(&sa.sa_mask);
		sigaddset(&sa.sa_mask, SIGHUP);
		sigaddset(&sa.sa_mask, SIGINT);
		sigaddset(&sa.sa_mask, SIGTERM);
		sigaction(SIGHUP, &sa, NULL);
		sigaction(SIGINT, &sa, NULL);
		sigaction(SIGTERM, &sa, NULL);
	}

	lineno = 0;
	nfiles = 0;
	truncofs = 0;
	overfile = NULL;

	/* Ensure 10^sufflen < LONG_MAX. */
	for (maxfiles = 1, i = 0; i < sufflen; i++) {
		if (maxfiles > LONG_MAX / 10)
			errx(1, "%ld: suffix too long (limit %ld)",
			    sufflen, i);
		maxfiles *= 10;
	}

	/* Create files based on supplied patterns. */
	while (nfiles < maxfiles - 1 && (expr = *argv++) != NULL) {
		/* Look ahead & see if this pattern has any repetitions. */
		if (*argv != NULL && **argv == '{') {
			errno = 0;
			reps = strtol(*argv + 1, &ep, 10);
			if (reps < 0 || *ep != '}' || errno != 0)
				errx(1, "%s: bad repetition count", *argv + 1);
			argv++;
		} else
			reps = 0;

		if (*expr == '/' || *expr == '%') {
			do
				do_rexp(expr);
			while (reps-- != 0 && nfiles < maxfiles - 1);
		} else if (isdigit((unsigned char)*expr))
			do_lineno(expr);
		else
			errx(1, "%s: unrecognised pattern", expr);
	}

	/* Copy the rest into a new file. */
	if (!feof(infile)) {
		ofp = newfile();
		while ((p = get_line()) != NULL && fputs(p, ofp) != EOF)
			;
		if (!sflag)
			printf("%jd\n", (intmax_t)ftello(ofp));
		if (fclose(ofp) != 0)
			err(1, "%s", currfile);
	}

	toomuch(NULL, 0);
	doclean = 0;

	return (0);
}

static void
usage(void)
{

	fprintf(stderr,
"usage: csplit [-ks] [-f prefix] [-n number] file args ...\n");
	exit(1);
}

static void
handlesig(int sig __unused)
{
	const char msg[] = "csplit: caught signal, cleaning up\n";

	write(STDERR_FILENO, msg, sizeof(msg) - 1);
	cleanup();
	_exit(2);
}

/* Create a new output file. */
static FILE *
newfile(void)
{
	FILE *fp;

	if ((size_t)snprintf(currfile, sizeof(currfile), "%s%0*ld", prefix,
	    (int)sufflen, nfiles) >= sizeof(currfile))
		errc(1, ENAMETOOLONG, NULL);
	if ((fp = fopen(currfile, "w+")) == NULL)
		err(1, "%s", currfile);
	nfiles++;

	return (fp);
}

/* Remove partial output, called before exiting. */
static void
cleanup(void)
{
	char fnbuf[PATH_MAX];
	long i;

	if (!doclean)
		return;

	/*
	 * NOTE: One cannot portably assume to be able to call snprintf()
	 * from inside a signal handler. It does, however, appear to be safe
	 * to do on FreeBSD. The solution to this problem is worse than the
	 * problem itself.
	 */

	for (i = 0; i < nfiles; i++) {
		snprintf(fnbuf, sizeof(fnbuf), "%s%0*ld", prefix,
		    (int)sufflen, i);
		unlink(fnbuf);
	}
}

/* Read a line from the input into a static buffer. */
static char *
get_line(void)
{
	static char lbuf[LINE_MAX];
	FILE *src;

	src = overfile != NULL ? overfile : infile;

again: if (fgets(lbuf, sizeof(lbuf), src) == NULL) {
		if (src == overfile) {
			src = infile;
			goto again;
		}
		return (NULL);
	}
	if (ferror(src))
		err(1, "%s", infn);
	lineno++;

	return (lbuf);
}

/* Conceptually rewind the input (as obtained by get_line()) back `n' lines. */
static void
toomuch(FILE *ofp, long n)
{
	char buf[BUFSIZ];
	size_t i, nread;

	if (overfile != NULL) {
		/*
		 * Truncate the previous file we overflowed into back to
		 * the correct length, close it.
		 */
		if (fflush(overfile) != 0)
			err(1, "overflow");
		if (ftruncate(fileno(overfile), truncofs) != 0)
			err(1, "overflow");
		if (fclose(overfile) != 0)
			err(1, "overflow");
		overfile = NULL;
	}

	if (n == 0)
		/* Just tidying up */
		return;

	lineno -= n;

	/*
	 * Wind the overflow file backwards to `n' lines before the
	 * current one.
	 */
	do {
		if (ftello(ofp) < (off_t)sizeof(buf))
			rewind(ofp);
		else
			fseeko(ofp, -(off_t)sizeof(buf), SEEK_CUR);
		if (ferror(ofp))
			errx(1, "%s: can't seek", currfile);
		if ((nread = fread(buf, 1, sizeof(buf), ofp)) == 0)
			errx(1, "can't read overflowed output");
		if (fseeko(ofp, -(off_t)nread, SEEK_CUR) != 0)
			err(1, "%s", currfile);
		for (i = 1; i <= nread; i++)
			if (buf[nread - i] == '\n' && n-- == 0)
				break;
		if (ftello(ofp) == 0)
			break;
	} while (n > 0);
	if (fseeko(ofp, nread - i + 1, SEEK_CUR) != 0)
		err(1, "%s", currfile);

	/*
	 * get_line() will read from here. Next call will truncate to
	 * truncofs in this file.
	 */
	overfile = ofp;
	truncofs = ftello(overfile);
}

/* Handle splits for /regexp/ and %regexp% patterns. */
static void
do_rexp(const char *expr)
{
	regex_t cre;
	intmax_t nwritten;
	long ofs;
	int first;
	char *ecopy, *ep, *p, *pofs, *re;
	FILE *ofp;

	if ((ecopy = strdup(expr)) == NULL)
		err(1, "strdup");

	re = ecopy + 1;
	if ((pofs = strrchr(ecopy, *expr)) == NULL || pofs[-1] == '\\')
		errx(1, "%s: missing trailing %c", expr, *expr);
	*pofs++ = '\0';

	if (*pofs != '\0') {
		errno = 0;
		ofs = strtol(pofs, &ep, 10);
		if (*ep != '\0' || errno != 0)
			errx(1, "%s: bad offset", pofs);
	} else
		ofs = 0;

	if (regcomp(&cre, re, REG_BASIC|REG_NOSUB) != 0)
		errx(1, "%s: bad regular expression", re);

	if (*expr == '/')
		/* /regexp/: Save results to a file. */
		ofp = newfile();
	else {
		/* %regexp%: Make a temporary file for overflow. */
		if ((ofp = tmpfile()) == NULL)
			err(1, "tmpfile");
	}

	/* Read and output lines until we get a match. */
	first = 1;
	while ((p = get_line()) != NULL) {
		if (fputs(p, ofp) == EOF)
			break;
		if (!first && regexec(&cre, p, 0, NULL, 0) == 0)
			break;
		first = 0;
	}

	if (p == NULL) {
		toomuch(NULL, 0);
		errx(1, "%s: no match", re);
	}

	if (ofs <= 0) {
		/*
		 * Negative (or zero) offset: throw back any lines we should
		 * not have read yet.
		  */
		if (p != NULL) {
			toomuch(ofp, -ofs + 1);
			nwritten = (intmax_t)truncofs;
		} else
			nwritten = (intmax_t)ftello(ofp);
	} else {
		/*
		 * Positive offset: copy the requested number of lines
		 * after the match.
		 */
		while (--ofs > 0 && (p = get_line()) != NULL)
			fputs(p, ofp);
		toomuch(NULL, 0);
		nwritten = (intmax_t)ftello(ofp);
		if (fclose(ofp) != 0)
			err(1, "%s", currfile);
	}

	if (!sflag && *expr == '/')
		printf("%jd\n", nwritten);

	regfree(&cre);
	free(ecopy);
}

/* Handle splits based on line number. */
static void
do_lineno(const char *expr)
{
	long lastline, tgtline;
	char *ep, *p;
	FILE *ofp;

	errno = 0;
	tgtline = strtol(expr, &ep, 10);
	if (tgtline <= 0 || errno != 0 || *ep != '\0')
		errx(1, "%s: bad line number", expr);
	lastline = tgtline;
	if (lastline <= lineno)
		errx(1, "%s: can't go backwards", expr);

	while (nfiles < maxfiles - 1) {
		ofp = newfile();
		while (lineno + 1 != lastline) {
			if ((p = get_line()) == NULL)
				errx(1, "%ld: out of range", lastline);
			if (fputs(p, ofp) == EOF)
				break;
		}
		if (!sflag)
			printf("%jd\n", (intmax_t)ftello(ofp));
		if (fclose(ofp) != 0)
			err(1, "%s", currfile);
		if (reps-- == 0)
			break;
		lastline += tgtline;
	} 
}
