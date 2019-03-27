/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993
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
"@(#) Copyright (c) 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#ifndef lint
static const char sccsid[] = "@(#)touch.c	8.1 (Berkeley) 6/6/93";
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void	stime_arg1(const char *, struct timespec *);
static void	stime_arg2(const char *, int, struct timespec *);
static void	stime_darg(const char *, struct timespec *);
static void	stime_file(const char *, struct timespec *);
static int	timeoffset(const char *);
static void	usage(const char *);

int
main(int argc, char *argv[])
{
	struct stat sb;
	struct timespec ts[2];
	int atflag;
	int Aflag, aflag, cflag, mflag, ch, fd, len, rval, timeset;
	char *p;
	char *myname;

	myname = basename(argv[0]);
	Aflag = aflag = cflag = mflag = timeset = 0;
	atflag = 0;
	ts[0].tv_sec = ts[1].tv_sec = 0;
	ts[0].tv_nsec = ts[1].tv_nsec = UTIME_NOW;

	while ((ch = getopt(argc, argv, "A:acd:fhmr:t:")) != -1)
		switch(ch) {
		case 'A':
			Aflag = timeoffset(optarg);
			break;
		case 'a':
			aflag = 1;
			break;
		case 'c':
			cflag = 1;
			break;
		case 'd':
			timeset = 1;
			stime_darg(optarg, ts);
			break;
		case 'f':
			/* No-op for compatibility. */
			break;
		case 'h':
			cflag = 1;
			atflag = AT_SYMLINK_NOFOLLOW;
			break;
		case 'm':
			mflag = 1;
			break;
		case 'r':
			timeset = 1;
			stime_file(optarg, ts);
			break;
		case 't':
			timeset = 1;
			stime_arg1(optarg, ts);
			break;
		default:
			usage(myname);
		}
	argc -= optind;
	argv += optind;

	if (aflag == 0 && mflag == 0)
		aflag = mflag = 1;

	if (timeset) {
		if (Aflag) {
			/*
			 * We're setting the time to an offset from a specified
			 * time.  God knows why, but it means that we can set
			 * that time once and for all here.
			 */
			if (aflag)
				ts[0].tv_sec += Aflag;
			if (mflag)
				ts[1].tv_sec += Aflag;
			Aflag = 0;		/* done our job */
		}
	} else {
		/*
		 * If no -r or -t flag, at least two operands, the first of
		 * which is an 8 or 10 digit number, use the obsolete time
		 * specification, otherwise use the current time.
		 */
		if (argc > 1) {
			strtol(argv[0], &p, 10);
			len = p - argv[0];
			if (*p == '\0' && (len == 8 || len == 10)) {
				timeset = 1;
				stime_arg2(*argv++, len == 10, ts);
			}
		}
		/* Both times default to the same. */
		ts[1] = ts[0];
	}

	if (!aflag)
		ts[0].tv_nsec = UTIME_OMIT;
	if (!mflag)
		ts[1].tv_nsec = UTIME_OMIT;

	if (*argv == NULL)
		usage(myname);

	if (Aflag)
		cflag = 1;

	for (rval = 0; *argv; ++argv) {
		/* See if the file exists. */
		if (fstatat(AT_FDCWD, *argv, &sb, atflag) != 0) {
			if (errno != ENOENT) {
				rval = 1;
				warn("%s", *argv);
				continue;
			}
			if (!cflag) {
				/* Create the file. */
				fd = open(*argv,
				    O_WRONLY | O_CREAT, DEFFILEMODE);
				if (fd == -1 || fstat(fd, &sb) || close(fd)) {
					rval = 1;
					warn("%s", *argv);
					continue;
				}

				/* If using the current time, we're done. */
				if (!timeset)
					continue;
			} else
				continue;
		}

		/*
		 * We're adjusting the times based on the file times, not a
		 * specified time (that gets handled above).
		 */
		if (Aflag) {
			if (aflag) {
				ts[0] = sb.st_atim;
				ts[0].tv_sec += Aflag;
			}
			if (mflag) {
				ts[1] = sb.st_mtim;
				ts[1].tv_sec += Aflag;
			}
		}

		if (!utimensat(AT_FDCWD, *argv, ts, atflag))
			continue;

		rval = 1;
		warn("%s", *argv);
	}
	exit(rval);
}

#define	ATOI2(ar)	((ar)[0] - '0') * 10 + ((ar)[1] - '0'); (ar) += 2;

static void
stime_arg1(const char *arg, struct timespec *tvp)
{
	time_t now;
	struct tm *t;
	int yearset;
	char *p;

	now = time(NULL);
	if ((t = localtime(&now)) == NULL)
		err(1, "localtime");
					/* [[CC]YY]MMDDhhmm[.SS] */
	if ((p = strchr(arg, '.')) == NULL)
		t->tm_sec = 0;		/* Seconds defaults to 0. */
	else {
		if (strlen(p + 1) != 2)
			goto terr;
		*p++ = '\0';
		t->tm_sec = ATOI2(p);
	}

	yearset = 0;
	switch(strlen(arg)) {
	case 12:			/* CCYYMMDDhhmm */
		t->tm_year = ATOI2(arg);
		t->tm_year *= 100;
		yearset = 1;
		/* FALLTHROUGH */
	case 10:			/* YYMMDDhhmm */
		if (yearset) {
			yearset = ATOI2(arg);
			t->tm_year += yearset;
		} else {
			yearset = ATOI2(arg);
			if (yearset < 69)
				t->tm_year = yearset + 2000;
			else
				t->tm_year = yearset + 1900;
		}
		t->tm_year -= 1900;	/* Convert to UNIX time. */
		/* FALLTHROUGH */
	case 8:				/* MMDDhhmm */
		t->tm_mon = ATOI2(arg);
		--t->tm_mon;		/* Convert from 01-12 to 00-11 */
		t->tm_mday = ATOI2(arg);
		t->tm_hour = ATOI2(arg);
		t->tm_min = ATOI2(arg);
		break;
	default:
		goto terr;
	}

	t->tm_isdst = -1;		/* Figure out DST. */
	tvp[0].tv_sec = tvp[1].tv_sec = mktime(t);
	if (tvp[0].tv_sec == -1)
		goto terr;

	tvp[0].tv_nsec = tvp[1].tv_nsec = 0;
	return;

terr:
	errx(1, "out of range or illegal time specification: [[CC]YY]MMDDhhmm[.SS]");
}

static void
stime_arg2(const char *arg, int year, struct timespec *tvp)
{
	time_t now;
	struct tm *t;

	now = time(NULL);
	if ((t = localtime(&now)) == NULL)
		err(1, "localtime");

	t->tm_mon = ATOI2(arg);		/* MMDDhhmm[yy] */
	--t->tm_mon;			/* Convert from 01-12 to 00-11 */
	t->tm_mday = ATOI2(arg);
	t->tm_hour = ATOI2(arg);
	t->tm_min = ATOI2(arg);
	if (year) {
		t->tm_year = ATOI2(arg);
		if (t->tm_year < 39)	/* support 2000-2038 not 1902-1969 */
			t->tm_year += 100;
	}

	t->tm_isdst = -1;		/* Figure out DST. */
	tvp[0].tv_sec = tvp[1].tv_sec = mktime(t);
	if (tvp[0].tv_sec == -1)
		errx(1,
	"out of range or illegal time specification: MMDDhhmm[yy]");

	tvp[0].tv_nsec = tvp[1].tv_nsec = 0;
}

static void
stime_darg(const char *arg, struct timespec *tvp)
{
	struct tm t = { .tm_sec = 0 };
	const char *fmt, *colon;
	char *p;
	int val, isutc = 0;

	tvp[0].tv_nsec = 0;
	t.tm_isdst = -1;
	colon = strchr(arg, ':');
	if (colon == NULL || strchr(colon + 1, ':') == NULL)
		goto bad;
	fmt = strchr(arg, 'T') != NULL ? "%Y-%m-%dT%H:%M:%S" :
	    "%Y-%m-%d %H:%M:%S";
	p = strptime(arg, fmt, &t);
	if (p == NULL)
		goto bad;
	/* POSIX: must have at least one digit after dot */
	if ((*p == '.' || *p == ',') && isdigit((unsigned char)p[1])) {
		p++;
		val = 100000000;
		while (isdigit((unsigned char)*p)) {
			tvp[0].tv_nsec += val * (*p - '0');
			p++;
			val /= 10;
		}
	}
	if (*p == 'Z') {
		isutc = 1;
		p++;
	}
	if (*p != '\0')
		goto bad;

	tvp[0].tv_sec = isutc ? timegm(&t) : mktime(&t);

	tvp[1] = tvp[0];
	return;

bad:
	errx(1, "out of range or illegal time specification: YYYY-MM-DDThh:mm:SS[.frac][tz]");
}

/* Calculate a time offset in seconds, given an arg of the format [-]HHMMSS. */
int
timeoffset(const char *arg)
{
	int offset;
	int isneg;

	offset = 0;
	isneg = *arg == '-';
	if (isneg)
		arg++;
	switch (strlen(arg)) {
	default:				/* invalid */
		errx(1, "Invalid offset spec, must be [-][[HH]MM]SS");

	case 6:					/* HHMMSS */
		offset = ATOI2(arg);
		/* FALLTHROUGH */
	case 4:					/* MMSS */
		offset = offset * 60 + ATOI2(arg);
		/* FALLTHROUGH */
	case 2:					/* SS */
		offset = offset * 60 + ATOI2(arg);
	}
	if (isneg)
		return (-offset);
	else
		return (offset);
}

static void
stime_file(const char *fname, struct timespec *tsp)
{
	struct stat sb;

	if (stat(fname, &sb))
		err(1, "%s", fname);
	tsp[0] = sb.st_atim;
	tsp[1] = sb.st_mtim;
}

static void
usage(const char *myname)
{
	fprintf(stderr, "usage: %s [-A [-][[hh]mm]SS] [-achm] [-r file] "
		"[-t [[CC]YY]MMDDhhmm[.SS]]\n"
		"       [-d YYYY-MM-DDThh:mm:SS[.frac][tz]] "
		"file ...\n", myname);
	exit(1);
}
