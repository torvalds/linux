/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software posted to USENET.
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

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static const char sccsid[] = "@(#)pom.c       8.1 (Berkeley) 5/31/93";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Phase of the Moon.  Calculates the current phase of the moon.
 * Based on routines from `Practical Astronomy with Your Calculator',
 * by Duffett-Smith.  Comments give the section from the book that
 * particular piece of code was adapted from.
 *
 * -- Keith E. Brandt  VIII 1984
 *
 */

#include <sys/capsicum.h>
#include <capsicum_helpers.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h> 

#ifndef	PI
#define	PI	  3.14159265358979323846
#endif
#define	EPOCH	  85
#define	EPSILONg  279.611371	/* solar ecliptic long at EPOCH */
#define	RHOg	  282.680403	/* solar ecliptic long of perigee at EPOCH */
#define	ECCEN	  0.01671542	/* solar orbit eccentricity */
#define	lzero	  18.251907	/* lunar mean long at EPOCH */
#define	Pzero	  192.917585	/* lunar mean long of perigee at EPOCH */
#define	Nzero	  55.204723	/* lunar mean long of node at EPOCH */
#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

static void	adj360(double *);
static double	dtor(double);
static double	potm(double);
static void	usage(char *progname);

int
main(int argc, char **argv)
{
	time_t tt;
	struct tm GMT, tmd;
	double days, today, tomorrow;
	int ch, cnt, pflag = 0;
	char *odate = NULL, *otime = NULL;
	char *progname = argv[0];

	if (caph_limit_stdio() < 0)
		err(1, "unable to limit capabitilities for stdio");

	caph_cache_catpages();
	if (caph_enter() < 0)
		err(1, "unable to enter capability mode");

	while ((ch = getopt(argc, argv, "d:pt:")) != -1)
		switch (ch) {
		case 'd':
			odate = optarg;
			break;
		case 'p':
			pflag = 1;
			break;
		case 't':
			otime = optarg;
			break;
		default:
			usage(progname);
		}

        argc -= optind;
	argv += optind;

	if (argc)
		usage(progname);

	/* Adjust based on users preferences */
	time(&tt);
	if (otime != NULL || odate != NULL) {
		/* Save today in case -d isn't specified */
		localtime_r(&tt, &tmd);

		if (odate != NULL) {
			tmd.tm_year = strtol(odate, NULL, 10) - 1900;
			tmd.tm_mon = strtol(odate + 5, NULL, 10) - 1;
			tmd.tm_mday = strtol(odate + 8, NULL, 10);
			/* Use midnight as the middle of the night */
			tmd.tm_hour = 0;
			tmd.tm_min = 0;
			tmd.tm_sec = 0;
			tmd.tm_isdst = -1;
		}
		if (otime != NULL) {
			tmd.tm_hour = strtol(otime, NULL, 10);
			tmd.tm_min = strtol(otime + 3, NULL, 10);
			tmd.tm_sec = strtol(otime + 6, NULL, 10);
			tmd.tm_isdst = -1;
		}
		tt = mktime(&tmd);
	}

	gmtime_r(&tt, &GMT);
	days = (GMT.tm_yday + 1) + ((GMT.tm_hour +
	    (GMT.tm_min / 60.0) + (GMT.tm_sec / 3600.0)) / 24.0);
	for (cnt = EPOCH; cnt < GMT.tm_year; ++cnt)
		days += isleap(1900 + cnt) ? 366 : 365;
	today = potm(days);
	if (pflag) {
		(void)printf("%1.0f\n", today);
		return (0);
	}
	(void)printf("The Moon is ");
	if (today >= 99.5)
		(void)printf("Full\n");
	else if (today < 0.5)
		(void)printf("New\n");
	else {
		tomorrow = potm(days + 1);
		if (today >= 49.5 && today < 50.5)
			(void)printf("%s\n", tomorrow > today ?
			    "at the First Quarter" : "at the Last Quarter");
		else {
			(void)printf("%s ", tomorrow > today ?
			    "Waxing" : "Waning");
			if (today > 50)
				(void)printf("Gibbous (%1.0f%% of Full)\n",
				    today);
			else if (today < 50)
				(void)printf("Crescent (%1.0f%% of Full)\n",
				    today);
		}
	}

	return 0;
}

/*
 * potm --
 *	return phase of the moon
 */
static double
potm(double days)
{
	double N, Msol, Ec, LambdaSol, l, Mm, Ev, Ac, A3, Mmprime;
	double A4, lprime, V, ldprime, D, Nm;

	N = 360 * days / 365.2422;				/* sec 42 #3 */
	adj360(&N);
	Msol = N + EPSILONg - RHOg;				/* sec 42 #4 */
	adj360(&Msol);
	Ec = 360 / PI * ECCEN * sin(dtor(Msol));		/* sec 42 #5 */
	LambdaSol = N + Ec + EPSILONg;				/* sec 42 #6 */
	adj360(&LambdaSol);
	l = 13.1763966 * days + lzero;				/* sec 61 #4 */
	adj360(&l);
	Mm = l - (0.1114041 * days) - Pzero;			/* sec 61 #5 */
	adj360(&Mm);
	Nm = Nzero - (0.0529539 * days);			/* sec 61 #6 */
	adj360(&Nm);
	Ev = 1.2739 * sin(dtor(2*(l - LambdaSol) - Mm));	/* sec 61 #7 */
	Ac = 0.1858 * sin(dtor(Msol));				/* sec 61 #8 */
	A3 = 0.37 * sin(dtor(Msol));
	Mmprime = Mm + Ev - Ac - A3;				/* sec 61 #9 */
	Ec = 6.2886 * sin(dtor(Mmprime));			/* sec 61 #10 */
	A4 = 0.214 * sin(dtor(2 * Mmprime));			/* sec 61 #11 */
	lprime = l + Ev + Ec - Ac + A4;				/* sec 61 #12 */
	V = 0.6583 * sin(dtor(2 * (lprime - LambdaSol)));	/* sec 61 #13 */
	ldprime = lprime + V;					/* sec 61 #14 */
	D = ldprime - LambdaSol;				/* sec 63 #2 */
	return(50 * (1 - cos(dtor(D))));			/* sec 63 #3 */
}

/*
 * dtor --
 *	convert degrees to radians
 */
static double
dtor(double deg)
{

	return(deg * PI / 180);
}

/*
 * adj360 --
 *	adjust value so 0 <= deg <= 360
 */
static void
adj360(double *deg)
{

	for (;;)
		if (*deg < 0)
			*deg += 360;
		else if (*deg > 360)
			*deg -= 360;
		else
			break;
}

static void
usage(char *progname)
{

	fprintf(stderr, "Usage: %s [-p] [-d yyyy.mm.dd] [-t hh:mm:ss]\n",
	    progname);
	exit(EX_USAGE);
}
