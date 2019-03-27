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
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/uio.h>

#define	SECSPERDAY	(24 * 60 * 60)
#define	SECSPERHOUR	(60 * 60)
#define	SECSPERMINUTE	(60)
#define	MINSPERHOUR	(60)
#define	HOURSPERDAY	(24)
#define	FSECSPERDAY	(24.0 * 60.0 * 60.0)
#define	FSECSPERHOUR	(60.0 * 60.0)
#define	FSECSPERMINUTE	(60.0)
#define	FMINSPERHOUR	(60.0)
#define	FHOURSPERDAY	(24.0)

#define	DAYSPERYEAR	365
#define	DAYSPERLEAPYEAR	366

/* Not yet categorized */

extern struct passwd *pw;
extern int doall;
extern time_t t1, t2;
extern const char *calendarFile;
extern int yrdays;
extern struct fixs neaster, npaskha, ncny, nfullmoon, nnewmoon;
extern struct fixs nmarequinox, nsepequinox, njunsolstice, ndecsolstice;
extern double UTCOffset;
extern int EastLongitude;
#ifdef WITH_ICONV
extern const char *outputEncoding;
#endif

#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

/* Flags to determine the returned values by determinestyle() in parsedata.c */
#define	F_NONE			0x00000
#define	F_MONTH			0x00001
#define	F_DAYOFWEEK		0x00002
#define	F_DAYOFMONTH		0x00004
#define	F_MODIFIERINDEX		0x00008
#define	F_MODIFIEROFFSET	0x00010
#define	F_SPECIALDAY		0x00020
#define	F_ALLMONTH		0x00040
#define	F_ALLDAY		0x00080
#define	F_VARIABLE		0x00100
#define	F_EASTER		0x00200
#define	F_CNY			0x00400
#define	F_PASKHA		0x00800
#define	F_NEWMOON		0x01000
#define	F_FULLMOON		0x02000
#define	F_MAREQUINOX		0x04000
#define	F_SEPEQUINOX		0x08000
#define	F_JUNSOLSTICE		0x10000
#define	F_DECSOLSTICE		0x20000
#define	F_YEAR			0x40000

#define	STRING_EASTER		"Easter"
#define	STRING_PASKHA		"Paskha"
#define	STRING_CNY		"ChineseNewYear"
#define STRING_NEWMOON		"NewMoon"
#define STRING_FULLMOON		"FullMoon"
#define STRING_MAREQUINOX	"MarEquinox"
#define STRING_SEPEQUINOX	"SepEquinox"
#define STRING_JUNSOLSTICE	"JunSolstice"
#define STRING_DECSOLSTICE	"DecSolstice"

#define	MAXCOUNT		125	/* Random number of maximum number of
					 * repeats of an event. Should be 52
					 * (number of weeks per year), if you
					 * want to show two years then it
					 * should be 104. If you are seeing
					 * more than this you are using this
					 * program wrong.
					 */

/*
 * All the astronomical calculations are carried out for the meridian 120
 * degrees east of Greenwich.
 */
#define UTCOFFSET_CNY		8.0

extern int	debug;		/* show parsing of the input */
extern int	year1, year2;

/* events.c */
/*
 * Event sorting related functions:
 * - Use event_add() to create a new event
 * - Use event_continue() to add more text to the last added event
 * - Use event_print_all() to display them in time chronological order
 */
struct event *event_add(int, int, int, char *, int, char *, char *);
void	event_continue(struct event *events, char *txt);
void	event_print_all(FILE *fp);
struct event {
	int	year;
	int	month;
	int	day;
	int	var;
	char	*date;
	char	*text;
	char	*extra;
	struct event *next;
};

/* locale.c */

struct fixs {
	char	*name;
	size_t	len;
};

extern const char *days[];
extern const char *fdays[];
extern const char *fmonths[];
extern const char *months[];
extern const char *sequences[];
extern struct fixs fndays[8];		/* full national days names */
extern struct fixs fnmonths[13];	/* full national months names */
extern struct fixs ndays[8];		/* short national days names */
extern struct fixs nmonths[13];		/* short national month names */
extern struct fixs nsequences[10];

void	setnnames(void);
void	setnsequences(char *);

/* day.c */
extern const struct tm tm0;
extern char dayname[];
void	settimes(time_t,int before, int after, int friday, struct tm *tp1, struct tm *tp2);
time_t	Mktime(char *);

/* parsedata.c */
int	parsedaymonth(char *, int *, int *, int *, int *, char **);
void	dodebug(char *type);

/* io.c */
void	cal(void);
void	closecal(FILE *);
FILE	*opencalin(void);
FILE	*opencalout(void);

/* ostern.c / paskha.c */
int	paskha(int);
int	easter(int);
int	j2g(int);

/* dates.c */
extern int cumdaytab[][14];
extern int monthdaytab[][14];
extern int debug_remember;
void	generatedates(struct tm *tp1, struct tm *tp2);
void	dumpdates(void);
int	remember_ymd(int y, int m, int d);
int	remember_yd(int y, int d, int *rm, int *rd);
int	first_dayofweek_of_year(int y);
int	first_dayofweek_of_month(int y, int m);
int	walkthrough_dates(struct event **e);
void	addtodate(struct event *e, int year, int month, int day);

/* pom.c */
#define	MAXMOONS	18
void	pom(int year, double UTCoffset, int *fms, int *nms);
void	fpom(int year, double utcoffset, double *ffms, double *fnms);

/* sunpos.c */
void	equinoxsolstice(int year, double UTCoffset, int *equinoxdays, int *solsticedays);
void	fequinoxsolstice(int year, double UTCoffset, double *equinoxdays, double *solsticedays);
int	calculatesunlongitude30(int year, int degreeGMToffset, int *ichinesemonths);

#ifdef WITH_ICONV
void	set_new_encoding(void);
#endif
