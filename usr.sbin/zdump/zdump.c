/*	$OpenBSD: zdump.c,v 1.19 2025/06/23 13:53:11 millert Exp $ */
/*
** This file is in the public domain, so clarified as of
** 2009-05-17 by Arthur David Olson.
*/

/*
** This code has been made independent of the rest of the time
** conversion package to increase confidence in the verification it provides.
** You can use this code to help in verifying other implementations.
*/

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define ZDUMP_LO_YEAR	(-500)
#define ZDUMP_HI_YEAR	2500

#define MAX_STRING_LENGTH	1024

#define TRUE		1
#define FALSE		0

#define SECSPERMIN	60
#define MINSPERHOUR	60
#define SECSPERHOUR	(SECSPERMIN * MINSPERHOUR)
#define HOURSPERDAY	24
#define EPOCH_YEAR	1970
#define TM_YEAR_BASE	1900
#define DAYSPERNYEAR	365

#define SECSPERDAY	(SECSPERHOUR * HOURSPERDAY)
#define SECSPERNYEAR	(SECSPERDAY * DAYSPERNYEAR)
#define SECSPERLYEAR	(SECSPERNYEAR + SECSPERDAY)
#define SECSPER400YEARS	(SECSPERNYEAR * (intmax_t) (300 + 3)	\
			 + SECSPERLYEAR * (intmax_t) (100 - 3))

/*
** True if SECSPER400YEARS is known to be representable as an
** intmax_t.  It's OK that SECSPER400YEARS_FITS can in theory be false
** even if SECSPER400YEARS is representable, because when that happens
** the code merely runs a bit more slowly, and this slowness doesn't
** occur on any practical platform.
*/
enum { SECSPER400YEARS_FITS = SECSPERLYEAR <= INTMAX_MAX / 400 };

#define isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))

#ifndef isleap_sum
/*
** See tzfile.h for details on isleap_sum.
*/
#define isleap_sum(a, b)	isleap((a) % 400 + (b) % 400)
#endif /* !defined isleap_sum */

extern char	**environ;
extern char	*tzname[2];
extern char 	*__progname;

static const time_t	absolute_min_time = LLONG_MIN;
static const time_t	absolute_max_time = LLONG_MAX;
static size_t		longest;
static int		warned;

static char 		*abbr(struct tm *tmp);
static void		abbrok(const char *abbrp, const char *zone);
static __pure intmax_t	delta(struct tm *newp, struct tm *oldp);
static void		dumptime(const struct tm *tmp);
static time_t		hunt(char *name, time_t lot, time_t hit);
static void		show(char *zone, time_t t, int v);
static __pure time_t	yeartot(intmax_t y);
static __dead void	usage(void);

static void
abbrok(const char * const abbrp, const char * const zone)
{
	const char 	*cp;
	char 		*wp;

	if (warned)
		return;
	cp = abbrp;
	wp = NULL;
	while (isascii((unsigned char)*cp) &&
	    (isalnum((unsigned char)*cp) || *cp == '-' || *cp == '+'))
		++cp;
	if (cp - abbrp < 3)
		wp = "has fewer than 3 characters";
	else if (cp - abbrp > 6)
		wp = "has more than 6 characters";
	else if (*cp)
		wp = "has characters other than ASCII alphanumerics, '-' or '+'";
	else
		return;
	fflush(stdout);
	fprintf(stderr, "%s: warning: zone \"%s\" abbreviation \"%s\" %s\n",
		__progname, zone, abbrp, wp);
	warned = TRUE;
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-Vv] [-c [loyear,]hiyear] "
	    "[-t [lotime,]hitime] zonename ...\n", __progname);
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	int		ch, i, Vflag = 0, vflag = 0;
	char		*cutarg = NULL;
	char		*cuttimes = NULL;
	time_t		cutlotime = absolute_min_time;
	time_t		cuthitime = absolute_max_time;
	time_t		now, t, newt;
	struct tm	tm, newtm, *tmp, *newtmp;
	char		**fakeenv;

	if (pledge("stdio rpath", NULL) == -1) {
		perror("pledge");
		exit(1);
	}

	while ((ch = getopt(argc, argv, "c:t:Vv")) != -1) {
		switch (ch) {
		case 'c':
			cutarg = optarg;
			break;
		case 't':
			cuttimes = optarg;
			break;
		case 'V':
			Vflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 1 && strcmp(argv[0], "=") == 0)
		usage();

	if (vflag || Vflag) {
		char dummy;
		intmax_t cutloyear = ZDUMP_LO_YEAR;
		intmax_t cuthiyear = ZDUMP_HI_YEAR;
		intmax_t lo, hi;

		if (cutarg != NULL) {
			if (sscanf(cutarg, "%"SCNdMAX"%c", &hi, &dummy) == 1) {
				cuthiyear = hi;
			} else if (sscanf(cutarg, "%"SCNdMAX",%"SCNdMAX"%c",
			    &lo, &hi, &dummy) == 2) {
				cutloyear = lo;
				cuthiyear = hi;
			} else {
				fprintf(stderr, "%s: wild -c argument %s\n",
				    __progname, cutarg);
				exit(EXIT_FAILURE);
			}
		}
		if (cutarg != NULL || cuttimes == NULL) {
			cutlotime = yeartot(cutloyear);
			cuthitime = yeartot(cuthiyear);
		}
		if (cuttimes != NULL) {
			if (sscanf(cuttimes, "%"SCNdMAX"%c", &hi, &dummy) == 1) {
				cutlotime = yeartot(cutloyear);
				if (hi < cuthitime) {
					if (hi < absolute_min_time)
						hi = absolute_min_time;
					cuthitime = hi;
				}
			} else if (sscanf(cuttimes, "%"SCNdMAX",%"SCNdMAX"%c",
					  &lo, &hi, &dummy) == 2) {
				if (cutlotime < lo) {
					if (absolute_max_time < lo)
						lo = absolute_max_time;
					cutlotime = lo;
				}
				if (hi < cuthitime) {
					if (hi < absolute_min_time)
						hi = absolute_min_time;
					cuthitime = hi;
				}
			} else {
				(void) fprintf(stderr,
					"%s: wild -t argument %s\n",
					__progname, cuttimes);
				exit(EXIT_FAILURE);
			}
		}
	}
	time(&now);
	longest = 0;
	for (i = 0; i < argc; ++i)
		if (strlen(argv[i]) > longest)
			longest = strlen(argv[i]);

	{
		int	from, to;

		for (i = 0; environ[i] != NULL; ++i)
			continue;
		fakeenv = reallocarray(NULL, i + 2, sizeof *fakeenv);
		if (fakeenv == NULL ||
		    (fakeenv[0] = malloc(longest + 4)) == NULL) {
			perror(__progname);
			exit(EXIT_FAILURE);
		}
		to = 0;
		strlcpy(fakeenv[to++], "TZ=", longest + 4);
		for (from = 0; environ[from] != NULL; ++from)
			if (strncmp(environ[from], "TZ=", 3) != 0)
				fakeenv[to++] = environ[from];
		fakeenv[to] = NULL;
		environ = fakeenv;
	}
	for (i = 0; i < argc; ++i) {
		char	buf[MAX_STRING_LENGTH];

		strlcpy(&fakeenv[0][3], argv[i], longest + 1);
		if (!(vflag | Vflag)) {
			show(argv[i], now, FALSE);
			continue;
		}
		warned = FALSE;
		t = absolute_min_time;
		if (!Vflag) {
			show(argv[i], t, TRUE);
			t += SECSPERDAY;
			show(argv[i], t, TRUE);
		}
		if (t < cutlotime)
			t = cutlotime;
		tmp = localtime(&t);
		if (tmp != NULL) {
			tm = *tmp;
			strlcpy(buf, abbr(&tm), sizeof buf);
		}
		for ( ; ; ) {
			newt = (t < absolute_max_time - SECSPERDAY / 2
				? t + SECSPERDAY / 2
				: absolute_max_time);
			if (cuthitime <= newt)
				break;
			newtmp = localtime(&newt);
			if (newtmp != NULL)
				newtm = *newtmp;
			if ((tmp == NULL || newtmp == NULL) ? (tmp != newtmp) :
			    (delta(&newtm, &tm) != (newt - t) ||
			    newtm.tm_isdst != tm.tm_isdst ||
			    strcmp(abbr(&newtm), buf) != 0)) {
				newt = hunt(argv[i], t, newt);
				newtmp = localtime(&newt);
				if (newtmp != NULL) {
					newtm = *newtmp;
					strlcpy(buf, abbr(&newtm), sizeof buf);
				}
			}
			t = newt;
			tm = newtm;
			tmp = newtmp;
		}
		if (!Vflag) {
			t = absolute_max_time;
			t -= SECSPERDAY;
			show(argv[i], t, TRUE);
			t += SECSPERDAY;
			show(argv[i], t, TRUE);
		}
	}
	if (fflush(stdout) || ferror(stdout)) {
		fprintf(stderr, "%s: ", __progname);
		perror("Error writing to standard output");
		exit(EXIT_FAILURE);
	}
	return 0;
}

static time_t
yeartot(const intmax_t y)
{
	intmax_t	seconds, years, myy = EPOCH_YEAR;
	time_t		t = 0;

	while (myy < y) {
		if (SECSPER400YEARS_FITS && 400 <= y - myy) {
			intmax_t diff400 = (y - myy) / 400;
			if (INTMAX_MAX / SECSPER400YEARS < diff400)
				return absolute_max_time;
			seconds = diff400 * SECSPER400YEARS;
			years = diff400 * 400;
                } else {
			seconds = isleap(myy) ? SECSPERLYEAR : SECSPERNYEAR;
			years = 1;
		}
		myy += years;
		if (t > absolute_max_time - seconds)
			return absolute_max_time;
		t += seconds;
	}
	while (y < myy) {
		if (SECSPER400YEARS_FITS && y + 400 <= myy && myy < 0) {
			intmax_t diff400 = (myy - y) / 400;
			if (INTMAX_MAX / SECSPER400YEARS < diff400)
				return absolute_min_time;
			seconds = diff400 * SECSPER400YEARS;
			years = diff400 * 400;
		} else {
			seconds = isleap(myy - 1) ? SECSPERLYEAR : SECSPERNYEAR;
			years = 1;
		}
		myy -= years;
		if (t < absolute_min_time + seconds)
			return absolute_min_time;
		t -= seconds;
	}
	return t;
}

static time_t
hunt(char *name, time_t lot, time_t hit)
{
	time_t			t;
	struct tm		lotm, *lotmp;
	struct tm		tm, *tmp;
	char			loab[MAX_STRING_LENGTH];

	lotmp = localtime(&lot);
	if (lotmp != NULL) {
		lotm = *lotmp;
		strlcpy(loab, abbr(&lotm), sizeof loab);
	}
	for ( ; ; ) {
		time_t diff = hit - lot;
		if (diff < 2)
			break;
		t = lot;
		t += diff / 2;
		if (t <= lot)
			++t;
		else if (t >= hit)
			--t;
		tmp = localtime(&t);
		if (tmp != NULL)
			tm = *tmp;
		if ((lotmp == NULL || tmp == NULL) ? (lotmp == tmp) :
		    (delta(&tm, &lotm) == (t - lot) &&
		    tm.tm_isdst == lotm.tm_isdst &&
		    strcmp(abbr(&tm), loab) == 0)) {
			lot = t;
			lotm = tm;
			lotmp = tmp;
		} else
			hit = t;
	}
	show(name, lot, TRUE);
	show(name, hit, TRUE);
	return hit;
}

/*
** Thanks to Paul Eggert for logic used in delta.
*/

static intmax_t
delta(struct tm *newp, struct tm *oldp)
{
	intmax_t	result;
	int		tmy;

	if (newp->tm_year < oldp->tm_year)
		return -delta(oldp, newp);
	result = 0;
	for (tmy = oldp->tm_year; tmy < newp->tm_year; ++tmy)
		result += DAYSPERNYEAR + isleap_sum(tmy, TM_YEAR_BASE);
	result += newp->tm_yday - oldp->tm_yday;
	result *= HOURSPERDAY;
	result += newp->tm_hour - oldp->tm_hour;
	result *= MINSPERHOUR;
	result += newp->tm_min - oldp->tm_min;
	result *= SECSPERMIN;
	result += newp->tm_sec - oldp->tm_sec;
	return result;
}

static void
show(char *zone, time_t t, int v)
{
	struct tm 	*tmp;

	printf("%-*s  ", (int) longest, zone);
	if (v) {
		tmp = gmtime(&t);
		if (tmp == NULL) {
			printf("%lld", t);
		} else {
			dumptime(tmp);
			printf(" UT");
		}
		printf(" = ");
	}
	tmp = localtime(&t);
	dumptime(tmp);
	if (tmp != NULL) {
		if (*abbr(tmp) != '\0')
			printf(" %s", abbr(tmp));
		if (v) {
			printf(" isdst=%d", tmp->tm_isdst);
#ifdef TM_GMTOFF
			printf(" gmtoff=%ld", tmp->TM_GMTOFF);
#endif /* defined TM_GMTOFF */
		}
	}
	printf("\n");
	if (tmp != NULL && *abbr(tmp) != '\0')
		abbrok(abbr(tmp), zone);
}

static char *
abbr(struct tm *tmp)
{
	char 		*result;
	static char	nada;

	if (tmp->tm_isdst != 0 && tmp->tm_isdst != 1)
		return &nada;
	result = tzname[tmp->tm_isdst];
	return (result == NULL) ? &nada : result;
}

static void
dumptime(const struct tm *timeptr)
{
	static const char wday_name[][3] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static const char mon_name[][3] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	const char	*wn, *mn;
	int		lead, trail;

	if (timeptr == NULL) {
		printf("NULL");
		return;
	}
	/*
	** The packaged versions of localtime and gmtime never put out-of-range
	** values in tm_wday or tm_mon, but since this code might be compiled
	** with other (perhaps experimental) versions, paranoia is in order.
	*/
	if (timeptr->tm_wday < 0 || timeptr->tm_wday >=
	    (int) (sizeof wday_name / sizeof wday_name[0]))
		wn = "???";
	else
		wn = wday_name[timeptr->tm_wday];
	if (timeptr->tm_mon < 0 || timeptr->tm_mon >=
	    (int) (sizeof mon_name / sizeof mon_name[0]))
		mn = "???";
	else
		mn = mon_name[timeptr->tm_mon];
	printf("%.3s %.3s%3d %.2d:%.2d:%.2d ",
	    wn, mn,
	    timeptr->tm_mday, timeptr->tm_hour,
	    timeptr->tm_min, timeptr->tm_sec);
#define DIVISOR	10
	trail = timeptr->tm_year % DIVISOR + TM_YEAR_BASE % DIVISOR;
	lead = timeptr->tm_year / DIVISOR + TM_YEAR_BASE / DIVISOR +
		trail / DIVISOR;
	trail %= DIVISOR;
	if (trail < 0 && lead > 0) {
		trail += DIVISOR;
		--lead;
	} else if (lead < 0 && trail > 0) {
		trail -= DIVISOR;
		++lead;
	}
	if (lead == 0)
		printf("%d", trail);
	else
		printf("%d%d", lead, ((trail < 0) ? -trail : trail));
}
