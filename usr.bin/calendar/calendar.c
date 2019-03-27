/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1994
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#if 0
#ifndef lint
static char sccsid[] = "@(#)calendar.c	8.3 (Berkeley) 3/25/94";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <errno.h>
#include <locale.h>
#include <langinfo.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "calendar.h"

#define	UTCOFFSET_NOTSET	100	/* Expected between -24 and +24 */
#define	LONGITUDE_NOTSET	1000	/* Expected between -360 and +360 */

struct passwd	*pw;
int		doall = 0;
int		debug = 0;
static char	*DEBUG = NULL;
static time_t	f_time = 0;
double		UTCOffset = UTCOFFSET_NOTSET;
int		EastLongitude = LONGITUDE_NOTSET;
#ifdef WITH_ICONV
const char	*outputEncoding;
#endif

static void	usage(void) __dead2;

int
main(int argc, char *argv[])
{
	int	f_dayAfter = 0;		/* days after current date */
	int	f_dayBefore = 0;	/* days before current date */
	int	Friday = 5;		/* day before weekend */

	int ch;
	struct tm tp1, tp2;

	(void)setlocale(LC_ALL, "");
#ifdef WITH_ICONV
	/* save the information about the encoding used in the terminal */
	outputEncoding = strdup(nl_langinfo(CODESET));
	if (outputEncoding == NULL)
		errx(1, "cannot allocate memory");
#endif

	while ((ch = getopt(argc, argv, "-A:aB:D:dF:f:l:t:U:W:?")) != -1)
		switch (ch) {
		case '-':		/* backward contemptible */
		case 'a':
			if (getuid()) {
				errno = EPERM;
				err(1, NULL);
			}
			doall = 1;
			break;

		case 'W': /* we don't need no steenking Fridays */
			Friday = -1;
			/* FALLTHROUGH */

		case 'A': /* days after current date */
			f_dayAfter = atoi(optarg);
			if (f_dayAfter < 0)
				errx(1, "number of days must be positive");
			break;

		case 'B': /* days before current date */
			f_dayBefore = atoi(optarg);
			if (f_dayBefore < 0)
				errx(1, "number of days must be positive");
			break;

		case 'D': /* debug output of sun and moon info */
			DEBUG = optarg;
			break;

		case 'd': /* debug output of current date */
			debug = 1;
			break;

		case 'F': /* Change the time: When does weekend start? */
			Friday = atoi(optarg);
			break;

		case 'f': /* other calendar file */
			calendarFile = optarg;
			break;

		case 'l': /* Change longitudal position */
			EastLongitude = strtol(optarg, NULL, 10);
			break;

		case 't': /* other date, for tests */
			f_time = Mktime(optarg);
			break;

		case 'U': /* Change UTC offset */
			UTCOffset = strtod(optarg, NULL);
			break;

		case '?':
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc)
		usage();

	/* use current time */
	if (f_time <= 0)
		(void)time(&f_time);

	/* if not set, determine where I could be */
	{
		if (UTCOffset == UTCOFFSET_NOTSET &&
		    EastLongitude == LONGITUDE_NOTSET) {
			/* Calculate on difference between here and UTC */
			time_t t;
			struct tm tm;
			long utcoffset, hh, mm, ss;
			double uo;

			time(&t);
			localtime_r(&t, &tm);
			utcoffset = tm.tm_gmtoff;
			/* seconds -> hh:mm:ss */
			hh = utcoffset / SECSPERHOUR;
			utcoffset %= SECSPERHOUR;
			mm = utcoffset / SECSPERMINUTE;
			utcoffset %= SECSPERMINUTE;
			ss = utcoffset;

			/* hh:mm:ss -> hh.mmss */
			uo = mm + (100.0 * (ss / 60.0));
			uo /=  60.0 / 100.0;
			uo = hh + uo / 100;

			UTCOffset = uo;
			EastLongitude = UTCOffset * 15;
		} else if (UTCOffset == UTCOFFSET_NOTSET) {
			/* Base on information given */
			UTCOffset = EastLongitude / 15;
		} else if (EastLongitude == LONGITUDE_NOTSET) {
			/* Base on information given */
			EastLongitude = UTCOffset * 15;
		}
	}

	settimes(f_time, f_dayBefore, f_dayAfter, Friday, &tp1, &tp2);
	generatedates(&tp1, &tp2);

	/*
	 * FROM now on, we are working in UTC.
	 * This will only affect moon and sun related events anyway.
	 */
	if (setenv("TZ", "UTC", 1) != 0)
		errx(1, "setenv: %s", strerror(errno));
	tzset();

	if (debug)
		dumpdates();

	if (DEBUG != NULL) {
		dodebug(DEBUG);
		exit(0);
	}

	if (doall)
		while ((pw = getpwent()) != NULL) {
			(void)setegid(pw->pw_gid);
			(void)initgroups(pw->pw_name, pw->pw_gid);
			(void)seteuid(pw->pw_uid);
			if (!chdir(pw->pw_dir))
				cal();
			(void)seteuid(0);
		}
	else
		cal();
	exit(0);
}


static void __dead2
usage(void)
{

	fprintf(stderr, "%s\n%s\n%s\n",
	    "usage: calendar [-A days] [-a] [-B days] [-D sun|moon] [-d]",
	    "		     [-F friday] [-f calendarfile] [-l longitude]",
	    "		     [-t dd[.mm[.year]]] [-U utcoffset] [-W days]"
	    );
	exit(1);
}
