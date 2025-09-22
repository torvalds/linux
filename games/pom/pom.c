/*	$OpenBSD: pom.c,v 1.28 2017/12/24 22:12:49 cheloha Exp $	*/
/*    $NetBSD: pom.c,v 1.6 1996/02/06 22:47:29 jtc Exp $      */

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

/*
 * Phase of the Moon.  Calculates the current phase of the moon.
 * Based on routines from `Practical Astronomy with Your Calculator',
 * by Duffett-Smith.  Comments give the section from the book that
 * particular piece of code was adapted from.
 *
 * -- Keith E. Brandt  VIII 1984
 *
 * Updated to the Third Edition of Duffett-Smith's book, IX 1998
 *
 */

#include <ctype.h>
#include <err.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	EPOCH	  90
#define	EPSILONg  279.403303	/* solar ecliptic long at EPOCH */
#define	RHOg	  282.768422	/* solar ecliptic long of perigee at EPOCH */
#define	ECCEN	  0.016713	/* solar orbit eccentricity */
#define	lzero	  318.351648	/* lunar mean long at EPOCH */
#define	Pzero	  36.340410	/* lunar mean long of perigee at EPOCH */
#define	Nzero	  318.510107	/* lunar mean long of node at EPOCH */

#define	isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))

void	adj360(double *);
double	dtor(double);
double	potm(double);
time_t	parsetime(char *);
__dead void	badformat(void);

int
main(int argc, char *argv[])
{
	struct tm *GMT;
	time_t tmpt;
	double days, today, tomorrow;
	int cnt, principal, usertime;
	char buf[1024];
	char *descriptor, *name;

	principal = 1;
	usertime = 0;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (argc > 1) {
		usertime = 1;
		tmpt = parsetime(argv[1]);
		strftime(buf, sizeof(buf), "%a %Y %b %e %H:%M:%S (%Z)",
			localtime(&tmpt));
	} else
		tmpt = time(NULL);
	GMT = gmtime(&tmpt);
	days = (GMT->tm_yday + 1) + ((GMT->tm_hour +
	    (GMT->tm_min / 60.0) + (GMT->tm_sec / 3600.0)) / 24.0);
	for (cnt = EPOCH; cnt < GMT->tm_year; ++cnt)
		days += isleap(cnt + 1900) ? 366 : 365;
	/* Selected time could be before EPOCH */
	for (cnt = GMT->tm_year; cnt < EPOCH; ++cnt)
		days -= isleap(cnt + 1900) ? 366 : 365;
	today = potm(days);
	if (lround(today) == 100)
		name = "Full";
	else if (lround(today) == 0)
		name = "New";
	else {
		tomorrow = potm(days + 1);
		if (lround(today) == 50) {
			if (tomorrow > today)
				name = "at the First Quarter";
			else
				name = "at the Last Quarter";
		} else {
			principal = 0;
			if (tomorrow > today)
				descriptor = "Waxing";
			else
				descriptor = "Waning";
			if (today > 50.0)
				name = "Gibbous";
			else /* (today < 50.0) */
				name = "Crescent";
		}
	}
	if (usertime)
		printf("%s:  ", buf);
	printf("The Moon is ");
	if (principal)
		printf("%s\n", name);
	else
		printf("%s %s (%1.0f%% of Full)\n", descriptor, name, today);
	return 0;
}

/*
 * potm --
 *	return phase of the moon
 */
double
potm(double days)
{
	double N, Msol, Ec, LambdaSol, l, Mm, Ev, Ac, A3, Mmprime;
	double A4, lprime, V, ldprime, D, Nm;

	N = 360.0 * days / 365.242191;				/* sec 46 #3 */
	adj360(&N);
	Msol = N + EPSILONg - RHOg;				/* sec 46 #4 */
	adj360(&Msol);
	Ec = 360 / M_PI * ECCEN * sin(dtor(Msol));		/* sec 46 #5 */
	LambdaSol = N + Ec + EPSILONg;				/* sec 46 #6 */
	adj360(&LambdaSol);
	l = 13.1763966 * days + lzero;				/* sec 65 #4 */
	adj360(&l);
	Mm = l - (0.1114041 * days) - Pzero;			/* sec 65 #5 */
	adj360(&Mm);
	Nm = Nzero - (0.0529539 * days);			/* sec 65 #6 */
	adj360(&Nm);
	Ev = 1.2739 * sin(dtor(2*(l - LambdaSol) - Mm));	/* sec 65 #7 */
	Ac = 0.1858 * sin(dtor(Msol));				/* sec 65 #8 */
	A3 = 0.37 * sin(dtor(Msol));
	Mmprime = Mm + Ev - Ac - A3;				/* sec 65 #9 */
	Ec = 6.2886 * sin(dtor(Mmprime));			/* sec 65 #10 */
	A4 = 0.214 * sin(dtor(2 * Mmprime));			/* sec 65 #11 */
	lprime = l + Ev + Ec - Ac + A4;				/* sec 65 #12 */
	V = 0.6583 * sin(dtor(2 * (lprime - LambdaSol)));	/* sec 65 #13 */
	ldprime = lprime + V;					/* sec 65 #14 */
	D = ldprime - LambdaSol;				/* sec 67 #2 */
	return(50.0 * (1 - cos(dtor(D))));			/* sec 67 #3 */
}

/*
 * dtor --
 *	convert degrees to radians
 */
double
dtor(double deg)
{
	return(deg * M_PI / 180);
}

/*
 * adj360 --
 *	adjust value so 0 <= deg <= 360
 */
void
adj360(double *deg)
{
	*deg = fmod(*deg, 360.0);
	if (*deg < 0.0)
		*deg += 360.0;
}

#define	ATOI2(ar)	((ar)[0] - '0') * 10 + ((ar)[1] - '0'); (ar) += 2;
time_t
parsetime(char *p)
{
	struct tm *lt;
	int bigyear;
	int yearset = 0;
	time_t tval;
	char *t;
	
	for (t = p; *t; ++t) {
		if (isdigit((unsigned char)*t))
			continue;
		badformat();
	}

	tval = time(NULL);
	lt = localtime(&tval);
	lt->tm_sec = 0;
	lt->tm_min = 0;

	switch (strlen(p)) {
	case 10:				/* yyyy */
		bigyear = ATOI2(p);
		lt->tm_year = (bigyear * 100) - 1900;
		yearset = 1;
		/* FALLTHROUGH */
	case 8:					/* yy */
		if (yearset) {
			lt->tm_year += ATOI2(p);
		} else {
			lt->tm_year = ATOI2(p);
			if (lt->tm_year < 69)		/* hack for 2000 */
				lt->tm_year += 100;
		}
		/* FALLTHROUGH */
	case 6:					/* mm */
		lt->tm_mon = ATOI2(p);
		if ((lt->tm_mon > 12) || !lt->tm_mon)
			badformat();
		--lt->tm_mon;			/* time struct is 0 - 11 */
		/* FALLTHROUGH */
	case 4:					/* dd */
		lt->tm_mday = ATOI2(p);
		if ((lt->tm_mday > 31) || !lt->tm_mday)
			badformat();
		/* FALLTHROUGH */
	case 2:					/* HH */
		lt->tm_hour = ATOI2(p);
		if (lt->tm_hour > 23)
			badformat();
		break;
	default:
		badformat();
	}
	/* The calling code needs a valid tm_ydays and this is the easiest
	 * way to get one */
	if ((tval = mktime(lt)) == -1)
		errx(1, "specified date is outside allowed range");
	return (tval);
}

void
badformat(void)
{
	warnx("illegal time format");
	(void)fprintf(stderr, "usage: %s [[[[[cc]yy]mm]dd]HH]\n",
	    getprogname());
	exit(1);
}
