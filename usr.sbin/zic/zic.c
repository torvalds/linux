/*	$OpenBSD: zic.c,v 1.32 2025/06/23 13:53:11 millert Exp $	*/
/*
** This file is in the public domain, so clarified as of
** 2006-07-17 by Arthur David Olson.
*/

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

#include "tzfile.h"

#define TRUE	1
#define FALSE	0

#define TYPE_SIGNED(type) (((type) -1) < 0)

#define YEARSPERREPEAT	400	/* years before a Gregorian repeat */

#define GRANDPARENTED   "Local time zone must be set--see zic manual page"

#include <stdarg.h>

#define	ZIC_VERSION_PRE_2013 '2'
#define	ZIC_VERSION	'3'

typedef int_fast64_t	zic_t;
#define ZIC_MIN INT_FAST64_MIN
#define ZIC_MAX INT_FAST64_MAX
#define SCNdZIC SCNdFAST64

#ifndef ZIC_MAX_ABBR_LEN_WO_WARN
#define ZIC_MAX_ABBR_LEN_WO_WARN	6
#endif /* !defined ZIC_MAX_ABBR_LEN_WO_WARN */

#define MKDIR_UMASK (S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)

#define end(cp, n)	(memchr((cp), '\0', (n)))

struct rule {
	const char	*r_filename;
	int		r_linenum;
	const char	*r_name;

	zic_t		r_loyear;	/* for example, 1986 */
	zic_t		r_hiyear;	/* for example, 1986 */
	int		r_lowasnum;
	int		r_hiwasnum;

	int		r_month;	/* 0..11 */

	int		r_dycode;	/* see below */
	int		r_dayofmonth;
	int		r_wday;

	zic_t		r_tod;		/* time from midnight */
	int		r_todisstd;	/* above is standard time if TRUE */
					/* or wall clock time if FALSE */
	int		r_todisgmt;	/* above is GMT if TRUE */
					/* or local time if FALSE */
	zic_t		r_stdoff;	/* offset from standard time */
	const char	*r_abbrvar;	/* variable part of abbreviation */

	int		r_todo;		/* a rule to do (used in outzone) */
	zic_t		r_temp;		/* used in outzone */
};

/*
**	r_dycode		r_dayofmonth	r_wday
*/

#define DC_DOM		0	/* 1..31 */	/* unused */
#define DC_DOWGEQ	1	/* 1..31 */	/* 0..6 (Sun..Sat) */
#define DC_DOWLEQ	2	/* 1..31 */	/* 0..6 (Sun..Sat) */

struct zone {
	const char	*z_filename;
	int		z_linenum;

	const char	*z_name;
	zic_t		z_gmtoff;
	const char	*z_rule;
	const char	*z_format;
	char		z_format_specifier;

	zic_t		z_stdoff;

	struct rule	*z_rules;
	int		z_nrules;

	struct rule	z_untilrule;
	zic_t		z_untiltime;
};

static void	addtt(zic_t starttime, int type);
static int	addtype(zic_t gmtoff, const char *abbr, int isdst,
	    int ttisstd, int ttisgmt);
static void	leapadd(zic_t t, int positive, int rolling, int count);
static void	adjleap(void);
static void	associate(void);
static void	dolink(const char *fromfield, const char *tofield);
static char	**getfields(char *buf);
static zic_t	gethms(const char *string, const char *errstrng, int signable);
static void	infile(const char *filename);
static void	inleap(char **fields, int nfields);
static void	inlink(char **fields, int nfields);
static void	inrule(char **fields, int nfields);
static int	inzcont(char **fields, int nfields);
static int	inzone(char **fields, int nfields);
static int	inzsub(char **fields, int nfields, int iscont);
static int	itsdir(const char *name);
static int	mkdirs(char *filename);
static void	newabbr(const char *abbr);
static zic_t	oadd(zic_t t1, zic_t t2);
static void	outzone(const struct zone *zp, int ntzones);
static zic_t	rpytime(const struct rule *rp, zic_t wantedy);
static void	rulesub(struct rule *rp, const char *loyearp, const char *hiyearp,
	    const char *typep, const char *monthp,
	    const char *dayp, const char *timep);
static zic_t	tadd(zic_t t1, zic_t t2);

extern char	*__progname;

/* Bound on length of what %z can expand to.  */
enum { PERCENT_Z_LEN_BOUND = sizeof "+995959" - 1 };

static int		charcnt;
static int		errors;
static const char	*filename;
static int		leapcnt;
static int		leapseen;
static zic_t		leapminyear;
static zic_t		leapmaxyear;
static int		linenum;
static int		max_abbrvar_len = PERCENT_Z_LEN_BOUND;
static int		max_format_len;
static const zic_t	max_time = INT_FAST64_MAX;
static zic_t		max_year;
static const zic_t	min_time = INT_FAST64_MIN;
static zic_t		min_year;
static int		noise;
static const char	*rfilename;
static int		rlinenum;
static int		timecnt;
static int		typecnt;

/*
** Line codes.
*/

#define LC_RULE		0
#define LC_ZONE		1
#define LC_LINK		2
#define LC_LEAP		3

/*
** Which fields are which on a Zone line.
*/

#define ZF_NAME		1
#define ZF_GMTOFF	2
#define ZF_RULE		3
#define ZF_FORMAT	4
#define ZF_TILYEAR	5
#define ZF_TILMONTH	6
#define ZF_TILDAY	7
#define ZF_TILTIME	8
#define ZONE_MINFIELDS	5
#define ZONE_MAXFIELDS	9

/*
** Which fields are which on a Zone continuation line.
*/

#define ZFC_GMTOFF	0
#define ZFC_RULE	1
#define ZFC_FORMAT	2
#define ZFC_TILYEAR	3
#define ZFC_TILMONTH	4
#define ZFC_TILDAY	5
#define ZFC_TILTIME	6
#define ZONEC_MINFIELDS	3
#define ZONEC_MAXFIELDS	7

/*
** Which files are which on a Rule line.
*/

#define RF_NAME		1
#define RF_LOYEAR	2
#define RF_HIYEAR	3
#define RF_COMMAND	4
#define RF_MONTH	5
#define RF_DAY		6
#define RF_TOD		7
#define RF_STDOFF	8
#define RF_ABBRVAR	9
#define RULE_FIELDS	10

/*
** Which fields are which on a Link line.
*/

#define LF_FROM		1
#define LF_TO		2
#define LINK_FIELDS	3

/*
** Which fields are which on a Leap line.
*/

#define LP_YEAR		1
#define LP_MONTH	2
#define LP_DAY		3
#define LP_TIME		4
#define LP_CORR		5
#define LP_ROLL		6
#define LEAP_FIELDS	7

/*
** Year synonyms.
*/

#define YR_MINIMUM	0
#define YR_MAXIMUM	1
#define YR_ONLY		2

static struct rule	*rules;
static int		nrules;	/* number of rules */

static struct zone	*zones;
static int		nzones;	/* number of zones */

struct link {
	const char	*l_filename;
	int		l_linenum;
	const char	*l_from;
	const char	*l_to;
};

static struct link	*links;
static int		nlinks;

struct lookup {
	const char	*l_word;
	const int	l_value;
};

static struct lookup const	*byword(const char *string, const struct lookup *lp);

static struct lookup const	line_codes[] = {
	{ "Rule",	LC_RULE },
	{ "Zone",	LC_ZONE },
	{ "Link",	LC_LINK },
	{ "Leap",	LC_LEAP },
	{ NULL,		0}
};

static struct lookup const	mon_names[] = {
	{ "January",	TM_JANUARY },
	{ "February",	TM_FEBRUARY },
	{ "March",	TM_MARCH },
	{ "April",	TM_APRIL },
	{ "May",	TM_MAY },
	{ "June",	TM_JUNE },
	{ "July",	TM_JULY },
	{ "August",	TM_AUGUST },
	{ "September",	TM_SEPTEMBER },
	{ "October",	TM_OCTOBER },
	{ "November",	TM_NOVEMBER },
	{ "December",	TM_DECEMBER },
	{ NULL,		0 }
};

static struct lookup const	wday_names[] = {
	{ "Sunday",	TM_SUNDAY },
	{ "Monday",	TM_MONDAY },
	{ "Tuesday",	TM_TUESDAY },
	{ "Wednesday",	TM_WEDNESDAY },
	{ "Thursday",	TM_THURSDAY },
	{ "Friday",	TM_FRIDAY },
	{ "Saturday",	TM_SATURDAY },
	{ NULL,		0 }
};

static struct lookup const	lasts[] = {
	{ "last-Sunday",	TM_SUNDAY },
	{ "last-Monday",	TM_MONDAY },
	{ "last-Tuesday",	TM_TUESDAY },
	{ "last-Wednesday",	TM_WEDNESDAY },
	{ "last-Thursday",	TM_THURSDAY },
	{ "last-Friday",	TM_FRIDAY },
	{ "last-Saturday",	TM_SATURDAY },
	{ NULL,			0 }
};

static struct lookup const	begin_years[] = {
	{ "minimum",	YR_MINIMUM },
	{ "maximum",	YR_MAXIMUM },
	{ NULL,		0 }
};

static struct lookup const	end_years[] = {
	{ "minimum",	YR_MINIMUM },
	{ "maximum",	YR_MAXIMUM },
	{ "only",	YR_ONLY },
	{ NULL,		0 }
};

static struct lookup const	leap_types[] = {
	{ "Rolling",	TRUE },
	{ "Stationary",	FALSE },
	{ NULL,		0 }
};

static const int	len_months[2][MONSPERYEAR] = {
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

static const int	len_years[2] = {
	DAYSPERNYEAR, DAYSPERLYEAR
};

static struct attype {
	zic_t		at;
	unsigned char	type;
}			attypes[TZ_MAX_TIMES];

static zic_t		gmtoffs[TZ_MAX_TYPES];
static char		isdsts[TZ_MAX_TYPES];
static unsigned char	abbrinds[TZ_MAX_TYPES];
static char		ttisstds[TZ_MAX_TYPES];
static char		ttisgmts[TZ_MAX_TYPES];
static char		chars[TZ_MAX_CHARS];
static zic_t		trans[TZ_MAX_LEAPS];
static zic_t		corr[TZ_MAX_LEAPS];
static char		roll[TZ_MAX_LEAPS];

/*
** Memory allocation.
*/

static __pure void *
memcheck(void *ptr)
{
	if (ptr == NULL)
		err(1, "Memory exhausted");
	return ptr;
}

static char *
ecatalloc(char *start, const char *tail)
{
	size_t len;
	char *str;

	len = strlen(start) + strlen(tail) + 1;
	str = memcheck(realloc(start, len));
	strlcat(str, tail, len);
	return str;
}

#define emalloc(size)		memcheck(malloc(size))
#define ereallocarray(ptr, nmemb, size)		memcheck(reallocarray(ptr, nmemb, size))
#define erealloc(ptr, size)	memcheck(realloc((ptr), (size)))
#define ecpyalloc(ptr)		memcheck(strdup(ptr))

/*
** Error handling.
*/

static void
eats(const char *name, int num, const char *rname, int rnum)
{
	filename = name;
	linenum = num;
	rfilename = rname;
	rlinenum = rnum;
}

static void
eat(const char *name, int num)
{
	eats(name, num, NULL, -1);
}

static void __attribute__((__format__ (printf, 1, 0)))
verror(const char *const string, va_list args)
{
	/*
	** Match the format of "cc" to allow sh users to
	**	zic ... 2>&1 | error -t "*" -v
	** on BSD systems.
	*/
	fprintf(stderr, "\"%s\", line %d: ", filename, linenum);
	vfprintf(stderr, string, args);
	if (rfilename != NULL)
		fprintf(stderr, " (rule from \"%s\", line %d)",
			rfilename, rlinenum);
	fprintf(stderr, "\n");
	++errors;
}

static void __attribute__((__format__ (printf, 1, 2)))
error(const char *const string, ...)
{
	va_list args;
	va_start(args, string);
	verror(string, args);
	va_end(args);
}

static void __attribute__((__format__ (printf, 1, 2)))
warning(const char *const string, ...)
{
	va_list args;
	fprintf(stderr, "warning: ");
	va_start(args, string);
	verror(string, args);
	va_end(args);
	--errors;
}

static const char *
scheck(const char *string, const char *format)
{
	const char	*fp, *result;
	char		*fbuf, *tp, dummy;
	int		c;

	result = "";
	if (string == NULL || format == NULL)
		return result;
	fbuf = reallocarray(NULL, strlen(format) + 2, 2);
	if (fbuf == NULL)
		return result;
	fp = format;
	tp = fbuf;

	/*
	** Copy directives, suppressing each conversion that is not
	** already suppressed.  Scansets containing '%' are not
	** supported; e.g., the conversion specification "%[%]" is not
	** supported.  Also, multibyte characters containing a
	** non-leading '%' byte are not supported.
	*/
	while ((*tp++ = c = *fp++) != '\0') {
		if (c != '%')
			continue;
		if (isdigit((unsigned char)*fp)) {
			const char *f = fp;
			char *t = tp;
			do {
				*t++ = c = *f++;
			} while (isdigit((unsigned char)c));
			if (c == '$') {
				fp = f;
				tp = t;
			}
		}
		*tp++ = '*';
		if (*fp == '*')
			++fp;
		if ((*tp++ = *fp++) == '\0')
			break;
	}

	*(tp - 1) = '%';
	*tp++ = 'c';
	*tp = '\0';
	if (sscanf(string, fbuf, &dummy) != 1)
		result = format;
	free(fbuf);
	return result;
}

static __dead void
usage(void)
{
	fprintf(stderr,
	    "usage: %s [-v] [-d directory] [-L leapsecondfilename] [-l timezone]\n"
	    "\t\t[-p timezone] [filename ...]\n",
		__progname);
	exit(EXIT_FAILURE);
}

static const char	*psxrules;
static const char	*lcltime;
static const char	*directory;
static const char	*leapsec;

int
main(int argc, char **argv)
{
	int	i, j, c;

	if (pledge("stdio rpath wpath cpath proc exec", NULL) == -1)
		err(1, "pledge");

	umask(umask(S_IWGRP | S_IWOTH) | (S_IWGRP | S_IWOTH));
	while ((c = getopt(argc, argv, "d:l:p:L:vy:")) != -1)
		switch (c) {
			default:
				usage();
			case 'd':
				if (directory == NULL)
					directory = optarg;
				else
					errx(1, "More than one -d option specified");
				break;
			case 'l':
				if (lcltime == NULL)
					lcltime = optarg;
				else
					errx(1, "More than one -l option specified");
				break;
			case 'p':
				if (psxrules == NULL)
					psxrules = optarg;
				else
					errx(1, "More than one -p option specified");
				break;
			case 'y':
				warning("ignoring obsolescent option -y");
				break;
			case 'L':
				if (leapsec == NULL)
					leapsec = optarg;
				else
					errx(1, "More than one -L option specified");
				break;
			case 'v':
				noise = TRUE;
				break;
		}
	if (optind == argc - 1 && strcmp(argv[optind], "=") == 0)
		usage();	/* usage message by request */
	if (directory == NULL)
		directory = TZDIR;

	if (optind < argc && leapsec != NULL) {
		infile(leapsec);
		adjleap();
	}

	for (i = optind; i < argc; ++i)
		infile(argv[i]);
	if (errors)
		exit(EXIT_FAILURE);
	associate();
	for (i = 0; i < nzones; i = j) {
		/*
		** Find the next non-continuation zone entry.
		*/
		for (j = i + 1; j < nzones && zones[j].z_name == NULL; ++j)
			continue;
		outzone(&zones[i], j - i);
	}
	/*
	** Make links.
	*/
	for (i = 0; i < nlinks; ++i) {
		eat(links[i].l_filename, links[i].l_linenum);
		dolink(links[i].l_from, links[i].l_to);
		if (noise)
			for (j = 0; j < nlinks; ++j)
				if (strcmp(links[i].l_to,
					links[j].l_from) == 0)
						warning("link to link");
	}
	if (lcltime != NULL) {
		eat("command line", 1);
		dolink(lcltime, TZDEFAULT);
	}
	if (psxrules != NULL) {
		eat("command line", 1);
		dolink(psxrules, TZDEFRULES);
	}
	return (errors == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void
dolink(const char *fromfield, const char *tofield)
{
	char	*fromname, *toname;

	if (fromfield[0] == '/')
		fromname = ecpyalloc(fromfield);
	else {
		fromname = ecpyalloc(directory);
		fromname = ecatalloc(fromname, "/");
		fromname = ecatalloc(fromname, fromfield);
	}
	if (tofield[0] == '/')
		toname = ecpyalloc(tofield);
	else {
		toname = ecpyalloc(directory);
		toname = ecatalloc(toname, "/");
		toname = ecatalloc(toname, tofield);
	}
	/*
	** We get to be careful here since
	** there's a fair chance of root running us.
	*/
	if (!itsdir(toname))
		remove(toname);
	if (link(fromname, toname) != 0) {
		int	result;

		if (mkdirs(toname) != 0)
			exit(EXIT_FAILURE);

		result = link(fromname, toname);
		if (result != 0 && errno == EXDEV)
			result = symlink(fromname, toname);
		if (result != 0)
			err(1, "Can't link from %s to %s", fromname, toname);
	}
	free(fromname);
	free(toname);
}

static int
itsdir(const char *name)
{
	char	*myname;
	int	accres;

	myname = ecpyalloc(name);
	myname = ecatalloc(myname, "/.");
	accres = access(myname, F_OK);
	free(myname);
	return accres == 0;
}

/*
** Associate sets of rules with zones.
*/

/*
** Sort by rule name.
*/

static int
rcomp(const void *cp1, const void *cp2)
{
	return strcmp(((const struct rule *) cp1)->r_name,
		((const struct rule *) cp2)->r_name);
}

static void
associate(void)
{
	struct zone	*zp;
	struct rule	*rp;
	int		base, out, i, j;

	if (nrules != 0) {
		qsort(rules, nrules, sizeof *rules, rcomp);
		for (i = 0; i < nrules - 1; ++i) {
			if (strcmp(rules[i].r_name,
				rules[i + 1].r_name) != 0)
					continue;
			if (strcmp(rules[i].r_filename,
				rules[i + 1].r_filename) == 0)
					continue;
			eat(rules[i].r_filename, rules[i].r_linenum);
			warning("same rule name in multiple files");
			eat(rules[i + 1].r_filename, rules[i + 1].r_linenum);
			warning("same rule name in multiple files");
			for (j = i + 2; j < nrules; ++j) {
				if (strcmp(rules[i].r_name,
					rules[j].r_name) != 0)
						break;
				if (strcmp(rules[i].r_filename,
					rules[j].r_filename) == 0)
						continue;
				if (strcmp(rules[i + 1].r_filename,
					rules[j].r_filename) == 0)
						continue;
				break;
			}
			i = j - 1;
		}
	}
	for (i = 0; i < nzones; ++i) {
		zp = &zones[i];
		zp->z_rules = NULL;
		zp->z_nrules = 0;
	}
	for (base = 0; base < nrules; base = out) {
		rp = &rules[base];
		for (out = base + 1; out < nrules; ++out)
			if (strcmp(rp->r_name, rules[out].r_name) != 0)
				break;
		for (i = 0; i < nzones; ++i) {
			zp = &zones[i];
			if (strcmp(zp->z_rule, rp->r_name) != 0)
				continue;
			zp->z_rules = rp;
			zp->z_nrules = out - base;
		}
	}
	for (i = 0; i < nzones; ++i) {
		zp = &zones[i];
		if (zp->z_nrules == 0) {
			/*
			** Maybe we have a local standard time offset.
			*/
			eat(zp->z_filename, zp->z_linenum);
			zp->z_stdoff = gethms(zp->z_rule, "unruly zone",
				TRUE);
			/*
			** Note, though, that if there's no rule,
			** a '%s' in the format is a bad thing.
			*/
			if (zp->z_format_specifier == 's')
				error("%s", "%s in ruleless zone");
		}
	}
	if (errors)
		exit(EXIT_FAILURE);
}

static void
infile(const char *name)
{
	FILE			*fp;
	char			**fields, *cp;
	const struct lookup	*lp;
	int			nfields, wantcont, num;
	char			buf[BUFSIZ];

	if (strcmp(name, "-") == 0) {
		name = "standard input";
		fp = stdin;
	} else if ((fp = fopen(name, "r")) == NULL)
		err(1, "Can't open %s", name);
	wantcont = FALSE;
	for (num = 1; ; ++num) {
		eat(name, num);
		if (fgets(buf, sizeof buf, fp) != buf)
			break;
		cp = strchr(buf, '\n');
		if (cp == NULL) {
			error("line too long");
			exit(EXIT_FAILURE);
		}
		*cp = '\0';
		fields = getfields(buf);
		nfields = 0;
		while (fields[nfields] != NULL) {
			static char	nada;

			if (strcmp(fields[nfields], "-") == 0)
				fields[nfields] = &nada;
			++nfields;
		}
		if (nfields == 0) {
			/* nothing to do */
		} else if (wantcont) {
			wantcont = inzcont(fields, nfields);
		} else {
			lp = byword(fields[0], line_codes);
			if (lp == NULL)
				error("input line of unknown type");
			else switch (lp->l_value) {
				case LC_RULE:
					inrule(fields, nfields);
					wantcont = FALSE;
					break;
				case LC_ZONE:
					wantcont = inzone(fields, nfields);
					break;
				case LC_LINK:
					inlink(fields, nfields);
					wantcont = FALSE;
					break;
				case LC_LEAP:
					if (name != leapsec)
						fprintf(stderr,
						    "%s: Leap line in non leap seconds file %s\n",
							__progname, name);
						/* no exit? */
					else
						inleap(fields, nfields);
					wantcont = FALSE;
					break;
				default:	/* "cannot happen" */
					errx(1, "panic: Invalid l_value %d", lp->l_value);
			}
		}
		free(fields);
	}
	if (ferror(fp))
		errx(1, "Error reading %s", filename);
	if (fp != stdin && fclose(fp))
		err(1, "Error closing %s", filename);
	if (wantcont)
		error("expected continuation line not found");
}

/*
** Convert a string of one of the forms
**	h	-h	hh:mm	-hh:mm	hh:mm:ss	-hh:mm:ss
** into a number of seconds.
** A null string maps to zero.
** Call error with errstring and return zero on errors.
*/

static zic_t
gethms(const char *string, const char *errstring, int signable)
{
	zic_t	hh;
	int	mm, ss, sign;

	if (string == NULL || *string == '\0')
		return 0;
	if (!signable)
		sign = 1;
	else if (*string == '-') {
		sign = -1;
		++string;
	} else
		sign = 1;
	if (sscanf(string, scheck(string, "%"SCNdZIC), &hh) == 1)
		mm = ss = 0;
	else if (sscanf(string, scheck(string, "%"SCNdZIC":%d"), &hh, &mm) == 2)
		ss = 0;
	else if (sscanf(string, scheck(string, "%"SCNdZIC":%d:%d"),
	    &hh, &mm, &ss) != 3) {
		error("%s", errstring);
		return 0;
	}
	if (hh < 0 ||
	    mm < 0 || mm >= MINSPERHOUR ||
	    ss < 0 || ss > SECSPERMIN) {
		error("%s", errstring);
		return 0;
	}
	if (ZIC_MAX / SECSPERHOUR < hh) {
		error("time overflow");
		return 0;
	}
	return oadd(sign * hh * SECSPERHOUR, sign * (mm * SECSPERMIN + ss));
}

static void
inrule(char **fields, int nfields)
{
	static struct rule	r;

	if (nfields != RULE_FIELDS) {
		error("wrong number of fields on Rule line");
		return;
	}
	if (*fields[RF_NAME] == '\0') {
		error("nameless rule");
		return;
	}
	r.r_filename = filename;
	r.r_linenum = linenum;
	r.r_stdoff = gethms(fields[RF_STDOFF], "invalid saved time", TRUE);
	rulesub(&r, fields[RF_LOYEAR], fields[RF_HIYEAR], fields[RF_COMMAND],
		fields[RF_MONTH], fields[RF_DAY], fields[RF_TOD]);
	r.r_name = ecpyalloc(fields[RF_NAME]);
	r.r_abbrvar = ecpyalloc(fields[RF_ABBRVAR]);
	if (max_abbrvar_len < strlen(r.r_abbrvar))
		max_abbrvar_len = strlen(r.r_abbrvar);
	rules = ereallocarray(rules, nrules + 1, sizeof *rules);
	rules[nrules++] = r;
}

static int
inzone(char **fields, int nfields)
{
	int	i;

	if (nfields < ZONE_MINFIELDS || nfields > ZONE_MAXFIELDS) {
		error("wrong number of fields on Zone line");
		return FALSE;
	}
	if (strcmp(fields[ZF_NAME], TZDEFAULT) == 0 && lcltime != NULL) {
		error("\"Zone %s\" line and -l option are mutually exclusive",
		    TZDEFAULT);
		return FALSE;
	}
	if (strcmp(fields[ZF_NAME], TZDEFRULES) == 0 && psxrules != NULL) {
		error("\"Zone %s\" line and -p option are mutually exclusive",
		    TZDEFRULES);
		return FALSE;
	}
	for (i = 0; i < nzones; ++i)
		if (zones[i].z_name != NULL &&
		    strcmp(zones[i].z_name, fields[ZF_NAME]) == 0) {
			error("duplicate zone name %s (file \"%s\", line %d)",
			    fields[ZF_NAME],
			    zones[i].z_filename,
			    zones[i].z_linenum);
			return FALSE;
		}
	return inzsub(fields, nfields, FALSE);
}

static int
inzcont(char **fields, int nfields)
{
	if (nfields < ZONEC_MINFIELDS || nfields > ZONEC_MAXFIELDS) {
		error("wrong number of fields on Zone continuation line");
		return FALSE;
	}
	return inzsub(fields, nfields, TRUE);
}

static int
inzsub(char **fields, int nfields, int iscont)
{
	char		*cp;
	char		*cp1;
	static struct zone	z;
	int		i_gmtoff, i_rule, i_format;
	int		i_untilyear, i_untilmonth;
	int		i_untilday, i_untiltime;
	int		hasuntil;

	if (iscont) {
		i_gmtoff = ZFC_GMTOFF;
		i_rule = ZFC_RULE;
		i_format = ZFC_FORMAT;
		i_untilyear = ZFC_TILYEAR;
		i_untilmonth = ZFC_TILMONTH;
		i_untilday = ZFC_TILDAY;
		i_untiltime = ZFC_TILTIME;
		z.z_name = NULL;
	} else {
		i_gmtoff = ZF_GMTOFF;
		i_rule = ZF_RULE;
		i_format = ZF_FORMAT;
		i_untilyear = ZF_TILYEAR;
		i_untilmonth = ZF_TILMONTH;
		i_untilday = ZF_TILDAY;
		i_untiltime = ZF_TILTIME;
		z.z_name = ecpyalloc(fields[ZF_NAME]);
	}
	z.z_filename = filename;
	z.z_linenum = linenum;
	z.z_gmtoff = gethms(fields[i_gmtoff], "invalid UT offset", TRUE);
	if ((cp = strchr(fields[i_format], '%')) != 0) {
		if ((*++cp != 's' && *cp != 'z') || strchr(cp, '%')
		    || strchr(fields[i_format], '/')) {
			error("invalid abbreviation format");
			return FALSE;
		}
	}
	z.z_rule = ecpyalloc(fields[i_rule]);
	z.z_format = cp1 = ecpyalloc(fields[i_format]);
	z.z_format_specifier = cp ? *cp : '\0';
	if (z.z_format_specifier == 'z') {
		if (noise) {
			warning("format '%%z' not handled by pre-2015 versions "
			    "of zic");
		}
		cp1[cp - fields[i_format]] = 's';
	}
	if (max_format_len < strlen(z.z_format))
		max_format_len = strlen(z.z_format);
	hasuntil = nfields > i_untilyear;
	if (hasuntil) {
		z.z_untilrule.r_filename = filename;
		z.z_untilrule.r_linenum = linenum;
		rulesub(&z.z_untilrule,
			fields[i_untilyear],
			"only",
			"",
			(nfields > i_untilmonth) ?
			fields[i_untilmonth] : "Jan",
			(nfields > i_untilday) ? fields[i_untilday] : "1",
			(nfields > i_untiltime) ? fields[i_untiltime] : "0");
		z.z_untiltime = rpytime(&z.z_untilrule,
			z.z_untilrule.r_loyear);
		if (iscont && nzones > 0 &&
		    z.z_untiltime > min_time &&
		    z.z_untiltime < max_time &&
		    zones[nzones - 1].z_untiltime > min_time &&
		    zones[nzones - 1].z_untiltime < max_time &&
		    zones[nzones - 1].z_untiltime >= z.z_untiltime) {
			error("Zone continuation line end time is not after end time of previous line");
			return FALSE;
		}
	}
	zones = ereallocarray(zones, nzones + 1, sizeof *zones);
	zones[nzones++] = z;
	/*
	** If there was an UNTIL field on this line,
	** there's more information about the zone on the next line.
	*/
	return hasuntil;
}

static void
inleap(char **fields, int nfields)
{
	const char		*cp;
	const struct lookup	*lp;
	int			i, j;
	zic_t			year;
	int			month, day;
	zic_t			dayoff, tod;
	zic_t			t;

	if (nfields != LEAP_FIELDS) {
		error("wrong number of fields on Leap line");
		return;
	}
	dayoff = 0;
	cp = fields[LP_YEAR];
	if (sscanf(cp, scheck(cp, "%"SCNdZIC), &year) != 1) {
		/*
		** Leapin' Lizards!
		*/
		error("invalid leaping year");
		return;
	}
	if (!leapseen || leapmaxyear < year)
		leapmaxyear = year;
	if (!leapseen || leapminyear > year)
		leapminyear = year;
	leapseen = TRUE;
	j = EPOCH_YEAR;
	while (j != year) {
		if (year > j) {
			i = len_years[isleap(j)];
			++j;
		} else {
			--j;
			i = -len_years[isleap(j)];
		}
		dayoff = oadd(dayoff, i);
	}
	if ((lp = byword(fields[LP_MONTH], mon_names)) == NULL) {
		error("invalid month name");
		return;
	}
	month = lp->l_value;
	j = TM_JANUARY;
	while (j != month) {
		i = len_months[isleap(year)][j];
		dayoff = oadd(dayoff, i);
		++j;
	}
	cp = fields[LP_DAY];
	if (sscanf(cp, scheck(cp, "%d"), &day) != 1 ||
	    day <= 0 || day > len_months[isleap(year)][month]) {
		error("invalid day of month");
		return;
	}
	dayoff = oadd(dayoff, day - 1);
	if (dayoff < 0 && !TYPE_SIGNED(zic_t)) {
		error("time before zero");
		return;
	}
	if (dayoff < min_time / SECSPERDAY) {
		error("time too small");
		return;
	}
	if (dayoff > max_time / SECSPERDAY) {
		error("time too large");
		return;
	}
	t = (zic_t) dayoff * SECSPERDAY;
	tod = gethms(fields[LP_TIME], "invalid time of day", FALSE);
	cp = fields[LP_CORR];
	{
		int	positive;
		int		count;

		if (strcmp(cp, "") == 0) { /* infile() turns "-" into "" */
			positive = FALSE;
			count = 1;
		} else if (strcmp(cp, "--") == 0) {
			positive = FALSE;
			count = 2;
		} else if (strcmp(cp, "+") == 0) {
			positive = TRUE;
			count = 1;
		} else if (strcmp(cp, "++") == 0) {
			positive = TRUE;
			count = 2;
		} else {
			error("illegal CORRECTION field on Leap line");
			return;
		}
		if ((lp = byword(fields[LP_ROLL], leap_types)) == NULL) {
			error("illegal Rolling/Stationary field on Leap line");
			return;
		}
		leapadd(tadd(t, tod), positive, lp->l_value, count);
	}
}

static void
inlink(char **fields, int nfields)
{
	struct link	l;

	if (nfields != LINK_FIELDS) {
		error("wrong number of fields on Link line");
		return;
	}
	if (*fields[LF_FROM] == '\0') {
		error("blank FROM field on Link line");
		return;
	}
	if (*fields[LF_TO] == '\0') {
		error("blank TO field on Link line");
		return;
	}
	l.l_filename = filename;
	l.l_linenum = linenum;
	l.l_from = ecpyalloc(fields[LF_FROM]);
	l.l_to = ecpyalloc(fields[LF_TO]);
	links = ereallocarray(links, nlinks + 1, sizeof *links);
	links[nlinks++] = l;
}

static void
rulesub(struct rule * const rp, const char * const loyearp,
    const char * const hiyearp, const char * const typep,
    const char * const monthp, const char * const dayp,
    const char * const timep)
{
	const struct lookup	*lp;
	const char		*cp;
	char			*dp, *ep;

	if ((lp = byword(monthp, mon_names)) == NULL) {
		error("invalid month name");
		return;
	}
	rp->r_month = lp->l_value;
	rp->r_todisstd = FALSE;
	rp->r_todisgmt = FALSE;
	dp = ecpyalloc(timep);
	if (*dp != '\0') {
		ep = dp + strlen(dp) - 1;
		switch (tolower((unsigned char)*ep)) {
		case 's':	/* Standard */
			rp->r_todisstd = TRUE;
			rp->r_todisgmt = FALSE;
			*ep = '\0';
			break;
		case 'w':	/* Wall */
			rp->r_todisstd = FALSE;
			rp->r_todisgmt = FALSE;
			*ep = '\0';
			break;
		case 'g':	/* Greenwich */
		case 'u':	/* Universal */
		case 'z':	/* Zulu */
			rp->r_todisstd = TRUE;
			rp->r_todisgmt = TRUE;
			*ep = '\0';
			break;
		}
	}
	rp->r_tod = gethms(dp, "invalid time of day", FALSE);
	free(dp);
	/*
	** Year work.
	*/
	cp = loyearp;
	lp = byword(cp, begin_years);
	rp->r_lowasnum = lp == NULL;
	if (!rp->r_lowasnum) switch ((int) lp->l_value) {
		case YR_MINIMUM:
			rp->r_loyear = ZIC_MIN;
			break;
		case YR_MAXIMUM:
			rp->r_loyear = ZIC_MAX;
			break;
		default:	/* "cannot happen" */
			errx(1, "panic: Invalid l_value %d", lp->l_value);
	} else if (sscanf(cp, scheck(cp, "%"SCNdZIC), &rp->r_loyear) != 1) {
		error("invalid starting year");
		return;
	}
	cp = hiyearp;
	lp = byword(cp, end_years);
	rp->r_hiwasnum = lp == NULL;
	if (!rp->r_hiwasnum) switch ((int) lp->l_value) {
		case YR_MINIMUM:
			rp->r_hiyear = ZIC_MIN;
			break;
		case YR_MAXIMUM:
			rp->r_hiyear = ZIC_MAX;
			break;
		case YR_ONLY:
			rp->r_hiyear = rp->r_loyear;
			break;
		default:	/* "cannot happen" */
			errx(1, "panic: Invalid l_value %d", lp->l_value);
	} else if (sscanf(cp, scheck(cp, "%"SCNdZIC), &rp->r_hiyear) != 1) {
		error("invalid ending year");
		return;
	}
	if (rp->r_loyear > rp->r_hiyear) {
		error("starting year greater than ending year");
		return;
	}
	if (*typep != '\0') {
		if (rp->r_loyear == rp->r_hiyear) {
			error("typed single year");
			return;
		}
		warning("year type is obsolete; use \"-\" instead");
	}
	/*
	** Day work.
	** Accept things such as:
	**	1
	**	last-Sunday
	**	Sun<=20
	**	Sun>=7
	*/
	dp = ecpyalloc(dayp);
	if ((lp = byword(dp, lasts)) != NULL) {
		rp->r_dycode = DC_DOWLEQ;
		rp->r_wday = lp->l_value;
		rp->r_dayofmonth = len_months[1][rp->r_month];
	} else {
		if ((ep = strchr(dp, '<')) != 0)
			rp->r_dycode = DC_DOWLEQ;
		else if ((ep = strchr(dp, '>')) != 0)
			rp->r_dycode = DC_DOWGEQ;
		else {
			ep = dp;
			rp->r_dycode = DC_DOM;
		}
		if (rp->r_dycode != DC_DOM) {
			*ep++ = 0;
			if (*ep++ != '=') {
				error("invalid day of month");
				free(dp);
				return;
			}
			if ((lp = byword(dp, wday_names)) == NULL) {
				error("invalid weekday name");
				free(dp);
				return;
			}
			rp->r_wday = lp->l_value;
		}
		if (sscanf(ep, scheck(ep, "%d"), &rp->r_dayofmonth) != 1 ||
		    rp->r_dayofmonth <= 0 ||
		    (rp->r_dayofmonth > len_months[1][rp->r_month])) {
			error("invalid day of month");
			free(dp);
			return;
		}
	}
	free(dp);
}

static void
convert(int_fast32_t val, unsigned char *buf)
{
	int	i;
	int	shift;

	for (i = 0, shift = 24; i < 4; ++i, shift -= 8)
		buf[i] = val >> shift;
}

static void
convert64(zic_t val, unsigned char *buf)
{
	int	i;
	int	shift;

	for (i = 0, shift = 56; i < 8; ++i, shift -= 8)
		buf[i] = val >> shift;
}

static void
puttzcode(int_fast32_t val, FILE *fp)
{
	char	buf[4];

	convert(val, buf);
	fwrite(buf, sizeof buf, 1, fp);
}

static void
puttzcode64(zic_t val, FILE *fp)
{
	char	buf[8];

	convert64(val, buf);
	fwrite(buf, sizeof buf, 1, fp);
}

static int
atcomp(const void *avp, const void *bvp)
{
	const zic_t	a = ((const struct attype *) avp)->at;
	const zic_t	b = ((const struct attype *) bvp)->at;

	return (a < b) ? -1 : (a > b);
}

static int
is32(zic_t x)
{
	return INT32_MIN <= x && x <= INT32_MAX;
}

static void
writezone(const char *const name, const char *const string, char version)
{
	FILE			*fp;
	int			i, j;
	int			leapcnt32, leapi32;
	int			timecnt32, timei32;
	int			pass;
	static char		*fullname;
	static const struct tzhead	tzh0;
	static struct tzhead	tzh;
	zic_t			ats[TZ_MAX_TIMES];
	unsigned char		types[TZ_MAX_TIMES];
	size_t			len;

	/*
	** Sort.
	*/
	if (timecnt > 1)
		qsort(attypes, timecnt, sizeof *attypes, atcomp);
	/*
	** Optimize.
	*/
	{
		int	fromi;
		int	toi;

		toi = 0;
		fromi = 0;
		while (fromi < timecnt && attypes[fromi].at < min_time)
			++fromi;
		if (isdsts[0] == 0)
			while (fromi < timecnt && attypes[fromi].type == 0)
				++fromi;	/* handled by default rule */
		for ( ; fromi < timecnt; ++fromi) {
			if (toi != 0 && ((attypes[fromi].at +
			    gmtoffs[attypes[toi - 1].type]) <=
			    (attypes[toi - 1].at + gmtoffs[toi == 1 ? 0 :
			    attypes[toi - 2].type]))) {
				attypes[toi - 1].type = attypes[fromi].type;
				continue;
			}
			if (toi == 0 ||
			    attypes[toi - 1].type != attypes[fromi].type)
				attypes[toi++] = attypes[fromi];
		}
		timecnt = toi;
	}
	/*
	** Transfer.
	*/
	for (i = 0; i < timecnt; ++i) {
		ats[i] = attypes[i].at;
		types[i] = attypes[i].type;
	}
	/*
	** Correct for leap seconds.
	*/
	for (i = 0; i < timecnt; ++i) {
		j = leapcnt;
		while (--j >= 0)
			if (ats[i] > trans[j] - corr[j]) {
				ats[i] = tadd(ats[i], corr[j]);
				break;
			}
	}
	/*
	** Figure out 32-bit-limited starts and counts.
	*/
	timecnt32 = timecnt;
	timei32 = 0;
	leapcnt32 = leapcnt;
	leapi32 = 0;
	while (timecnt32 > 0 && !is32(ats[timecnt32 - 1]))
		--timecnt32;
	while (timecnt32 > 0 && !is32(ats[timei32])) {
		--timecnt32;
		++timei32;
	}
	while (leapcnt32 > 0 && !is32(trans[leapcnt32 - 1]))
		--leapcnt32;
	while (leapcnt32 > 0 && !is32(trans[leapi32])) {
		--leapcnt32;
		++leapi32;
	}
	len = strlen(directory) + 1 + strlen(name) + 1;
	fullname = erealloc(fullname, len);
	snprintf(fullname, len, "%s/%s", directory, name);
	/*
	** Remove old file, if any, to snap links.
	*/
	if (!itsdir(fullname) && remove(fullname) != 0 && errno != ENOENT)
		err(1, "Can't remove %s", fullname);
	if ((fp = fopen(fullname, "wb")) == NULL) {
		if (mkdirs(fullname) != 0)
			exit(EXIT_FAILURE);
		if ((fp = fopen(fullname, "wb")) == NULL)
			err(1, "Can't create %s", fullname);
	}
	for (pass = 1; pass <= 2; ++pass) {
		int	thistimei, thistimecnt;
		int	thisleapi, thisleapcnt;
		int	thistimelim, thisleaplim;
		int	writetype[TZ_MAX_TIMES];
		int	typemap[TZ_MAX_TYPES];
		int	thistypecnt;
		char	thischars[TZ_MAX_CHARS];
		char	thischarcnt;
		int	indmap[TZ_MAX_CHARS];

		if (pass == 1) {
			thistimei = timei32;
			thistimecnt = timecnt32;
			thisleapi = leapi32;
			thisleapcnt = leapcnt32;
		} else {
			thistimei = 0;
			thistimecnt = timecnt;
			thisleapi = 0;
			thisleapcnt = leapcnt;
		}
		thistimelim = thistimei + thistimecnt;
		thisleaplim = thisleapi + thisleapcnt;
		for (i = 0; i < typecnt; ++i)
			writetype[i] = thistimecnt == timecnt;
		if (thistimecnt == 0) {
			/*
			** No transition times fall in the current
			** (32- or 64-bit) window.
			*/
			if (typecnt != 0)
				writetype[typecnt - 1] = TRUE;
		} else {
			for (i = thistimei - 1; i < thistimelim; ++i)
				if (i >= 0)
					writetype[types[i]] = TRUE;
			/*
			** For America/Godthab and Antarctica/Palmer
			*/
			if (thistimei == 0)
				writetype[0] = TRUE;
		}
#ifndef LEAVE_SOME_PRE_2011_SYSTEMS_IN_THE_LURCH
		/*
		** For some pre-2011 systems: if the last-to-be-written
		** standard (or daylight) type has an offset different from the
		** most recently used offset,
		** append an (unused) copy of the most recently used type
		** (to help get global "altzone" and "timezone" variables
		** set correctly).
		*/
		{
			int	mrudst, mrustd, hidst, histd, type;

			hidst = histd = mrudst = mrustd = -1;
			for (i = thistimei; i < thistimelim; ++i)
				if (isdsts[types[i]])
					mrudst = types[i];
				else
					mrustd = types[i];
			for (i = 0; i < typecnt; ++i)
				if (writetype[i]) {
					if (isdsts[i])
						hidst = i;
					else
						histd = i;
				}
			if (hidst >= 0 && mrudst >= 0 && hidst != mrudst &&
			    gmtoffs[hidst] != gmtoffs[mrudst]) {
				isdsts[mrudst] = -1;
				type = addtype(gmtoffs[mrudst],
				    &chars[abbrinds[mrudst]],
				    TRUE, ttisstds[mrudst],
				    ttisgmts[mrudst]);
				isdsts[mrudst] = TRUE;
				writetype[type] = TRUE;
			}
			if (histd >= 0 && mrustd >= 0 && histd != mrustd &&
			    gmtoffs[histd] != gmtoffs[mrustd]) {
				isdsts[mrustd] = -1;
				type = addtype(gmtoffs[mrustd],
				    &chars[abbrinds[mrustd]],
				    FALSE, ttisstds[mrustd],
				    ttisgmts[mrustd]);
				isdsts[mrustd] = FALSE;
				writetype[type] = TRUE;
			}
		}
#endif /* !defined LEAVE_SOME_PRE_2011_SYSTEMS_IN_THE_LURCH */
		thistypecnt = 0;
		for (i = 0; i < typecnt; ++i)
			typemap[i] = writetype[i] ?  thistypecnt++ : -1;
		for (i = 0; i < sizeof indmap / sizeof indmap[0]; ++i)
			indmap[i] = -1;
		thischarcnt = 0;
		for (i = 0; i < typecnt; ++i) {
			char	*thisabbr;

			if (!writetype[i])
				continue;
			if (indmap[abbrinds[i]] >= 0)
				continue;
			thisabbr = &chars[abbrinds[i]];
			for (j = 0; j < thischarcnt; ++j)
				if (strcmp(&thischars[j], thisabbr) == 0)
					break;
			if (j == thischarcnt) {
				strlcpy(&thischars[(int) thischarcnt],
				    thisabbr, sizeof(thischars) - thischarcnt);
				thischarcnt += strlen(thisabbr) + 1;
			}
			indmap[abbrinds[i]] = j;
		}
#define DO(field)	fwrite(tzh.field, sizeof tzh.field, 1, fp)
		tzh = tzh0;
		strncpy(tzh.tzh_magic, TZ_MAGIC, sizeof tzh.tzh_magic);
		tzh.tzh_version[0] = version;
		convert(thistypecnt, tzh.tzh_ttisgmtcnt);
		convert(thistypecnt, tzh.tzh_ttisstdcnt);
		convert(thisleapcnt, tzh.tzh_leapcnt);
		convert(thistimecnt, tzh.tzh_timecnt);
		convert(thistypecnt, tzh.tzh_typecnt);
		convert(thischarcnt, tzh.tzh_charcnt);
		DO(tzh_magic);
		DO(tzh_version);
		DO(tzh_reserved);
		DO(tzh_ttisgmtcnt);
		DO(tzh_ttisstdcnt);
		DO(tzh_leapcnt);
		DO(tzh_timecnt);
		DO(tzh_typecnt);
		DO(tzh_charcnt);
#undef DO
		for (i = thistimei; i < thistimelim; ++i)
			if (pass == 1)
				puttzcode(ats[i], fp);
			else
				puttzcode64(ats[i], fp);
		for (i = thistimei; i < thistimelim; ++i) {
			unsigned char	uc;

			uc = typemap[types[i]];
			fwrite(&uc, sizeof uc, 1, fp);
		}
		for (i = 0; i < typecnt; ++i)
			if (writetype[i]) {
				puttzcode(gmtoffs[i], fp);
				putc(isdsts[i], fp);
				putc((unsigned char)indmap[abbrinds[i]], fp);
			}
		if (thischarcnt != 0)
			fwrite(thischars, sizeof thischars[0], thischarcnt, fp);
		for (i = thisleapi; i < thisleaplim; ++i) {
			zic_t	todo;

			if (roll[i]) {
				if (timecnt == 0 || trans[i] < ats[0]) {
					j = 0;
					while (isdsts[j])
						if (++j >= typecnt) {
							j = 0;
							break;
						}
				} else {
					j = 1;
					while (j < timecnt &&
					    trans[i] >= ats[j])
						++j;
					j = types[j - 1];
				}
				todo = tadd(trans[i], -gmtoffs[j]);
			} else
				todo = trans[i];
			if (pass == 1)
				puttzcode(todo, fp);
			else
				puttzcode64(todo, fp);
			puttzcode(corr[i], fp);
		}
		for (i = 0; i < typecnt; ++i)
			if (writetype[i])
				putc(ttisstds[i], fp);
		for (i = 0; i < typecnt; ++i)
			if (writetype[i])
				putc(ttisgmts[i], fp);
	}
	fprintf(fp, "\n%s\n", string);
	if (ferror(fp) || fclose(fp))
		errx(1, "Error writing %s", fullname);
}

static char const *
abbroffset(char *buf, zic_t offset)
{
	char sign = '+';
	int seconds, minutes;

	if (offset < 0) {
		offset = -offset;
		sign = '-';
	}

	seconds = offset % SECSPERMIN;
	offset /= SECSPERMIN;
	minutes = offset % MINSPERHOUR;
	offset /= MINSPERHOUR;
	if (100 <= offset) {
		error("%%z UTC offset magnitude exceeds 99:59:59");
		return "%z";
	} else {
		char *p = buf;
		*p++ = sign;
		*p++ = '0' + offset / 10;
		*p++ = '0' + offset % 10;
		if (minutes | seconds) {
			*p++ = '0' + minutes / 10;
			*p++ = '0' + minutes % 10;
			if (seconds) {
				*p++ = '0' + seconds / 10;
				*p++ = '0' + seconds % 10;
			}
		}
		*p = '\0';
		return buf;
	}
}

static void
doabbr(char *abbr, size_t size, struct zone const *zp, const char *letters,
    zic_t stdoff, int doquotes)
{
	char	*cp, *slashp;
	size_t	len;
	char const *format = zp->z_format;

	slashp = strchr(format, '/');
	if (slashp == NULL) {
		char letterbuf[PERCENT_Z_LEN_BOUND + 1];
		if (zp->z_format_specifier == 'z')
			letters = abbroffset(letterbuf, zp->z_gmtoff + stdoff);
		else if (letters == NULL)
			letters = "%s";
		snprintf(abbr, size, format, letters);
	} else if (stdoff != 0) {
		strlcpy(abbr, slashp + 1, size);
	} else {
		if (slashp - format + 1 < size)
			size = slashp - format + 1;
		strlcpy(abbr, format, size);
	}
	if (!doquotes)
		return;
	for (cp = abbr; *cp != '\0'; ++cp)
		if (strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZ", *cp) == NULL &&
			strchr("abcdefghijklmnopqrstuvwxyz", *cp) == NULL)
				break;
	len = strlen(abbr);
	if (len > 0 && *cp == '\0')
		return;
	abbr[len + 2] = '\0';
	abbr[len + 1] = '>';
	for ( ; len > 0; --len)
		abbr[len] = abbr[len - 1];
	abbr[0] = '<';
}

static void
updateminmax(zic_t x)
{
	if (min_year > x)
		min_year = x;
	if (max_year < x)
		max_year = x;
}

static int
stringoffset(char *result, size_t size, zic_t offset)
{
	int	hours, minutes, seconds;
	char	*ep;

	result[0] = '\0';
	if (offset < 0) {
		strlcpy(result, "-", size);
		offset = -offset;
	}
	seconds = offset % SECSPERMIN;
	offset /= SECSPERMIN;
	minutes = offset % MINSPERHOUR;
	offset /= MINSPERHOUR;
	hours = offset;
	if (hours >= HOURSPERDAY * DAYSPERWEEK) {
		result[0] = '\0';
		return -1;
	}
	ep = end(result, size);
	snprintf(ep, size - (ep - result), "%d", hours);
	if (minutes != 0 || seconds != 0) {
		ep = end(result, size);
		snprintf(ep, size - (ep - result), ":%02d", minutes);
		if (seconds != 0) {
			ep = end(result, size);
			snprintf(ep, size - (ep - result), ":%02d", seconds);
		}
	}
	return 0;
}

static int
stringrule(char *result, size_t size, const struct rule *rp, zic_t dstoff,
    zic_t gmtoff)
{
	zic_t	tod = rp->r_tod;
	int	compat = 0;
	char	*ep;

	ep = end(result, size);
	size -= ep - result;
	result = ep;
	if (rp->r_dycode == DC_DOM) {
		int	month, total;

		if (rp->r_dayofmonth == 29 && rp->r_month == TM_FEBRUARY)
			return -1;
		total = 0;
		for (month = 0; month < rp->r_month; ++month)
			total += len_months[0][month];
		/* Omit the "J" in Jan and Feb, as that's shorter.  */
		if (rp->r_month <= 1) {
			snprintf(result, size, "%d",
			    total + rp->r_dayofmonth - 1);
		} else {
			snprintf(result, size, "J%d",
			    total + rp->r_dayofmonth);
		}
	} else {
		int	week, wdayoff, wday = rp->r_wday;

		if (rp->r_dycode == DC_DOWGEQ) {
			wdayoff = (rp->r_dayofmonth - 1) % DAYSPERWEEK;
			if (wdayoff)
				compat = 2013;
			wday -= wdayoff;
			tod += wdayoff * SECSPERDAY;
			week = 1 + (rp->r_dayofmonth - 1) / DAYSPERWEEK;
		} else if (rp->r_dycode == DC_DOWLEQ) {
			if (rp->r_dayofmonth == len_months[1][rp->r_month])
				week = 5;
			else {
				wdayoff = rp->r_dayofmonth % DAYSPERWEEK;
				if (wdayoff)
					compat = 2013;
				wday -= wdayoff;
				tod += wdayoff * SECSPERDAY;
				week = rp->r_dayofmonth / DAYSPERWEEK;
			}
		} else
			return -1;	/* "cannot happen" */
		if (wday < 0)
			wday += DAYSPERWEEK;
		snprintf(result, size, "M%d.%d.%d",
			rp->r_month + 1, week, wday);
	}
	if (rp->r_todisgmt)
		tod += gmtoff;
	if (rp->r_todisstd && rp->r_stdoff == 0)
		tod += dstoff;
	if (tod != 2 * SECSPERMIN * MINSPERHOUR) {
		strlcat(result, "/", size);
		ep = end(result, size);
		if (stringoffset(ep, size - (ep - result), tod) != 0)
			return -1;
		if (tod < 0) {
			if (compat < 2013)
				compat = 2013;
		} else if (SECSPERDAY <= tod) {
			if (compat < 1994)
				compat = 1994;
		}
	}
	return compat;
}

static int
rule_cmp(struct rule const *a, struct rule const *b)
{
	if (!a)
		return -!!b;
	if (!b)
		return 1;
	if (a->r_hiyear != b->r_hiyear)
		return a->r_hiyear < b->r_hiyear ? -1 : 1;
	if (a->r_month - b->r_month != 0)
		return a->r_month - b->r_month;
	return a->r_dayofmonth - b->r_dayofmonth;
}

enum { YEAR_BY_YEAR_ZONE = 1 };

static int
stringzone(char *result, size_t size, const struct zone *zpfirst, int zonecount)
{
	const struct zone	*zp;
	struct rule		*rp, *stdrp, *dstrp;
	int			i, c, compat = 0;
	const char		*abbrvar;
	char			*ep;
	struct rule		stdr, dstr;

	result[0] = '\0';
	zp = zpfirst + zonecount - 1;
	stdrp = dstrp = NULL;
	for (i = 0; i < zp->z_nrules; ++i) {
		rp = &zp->z_rules[i];
		if (rp->r_hiwasnum || rp->r_hiyear != ZIC_MAX)
			continue;
		if (rp->r_stdoff == 0) {
			if (stdrp == NULL)
				stdrp = rp;
			else
				return -1;
		} else {
			if (dstrp == NULL)
				dstrp = rp;
			else
				return -1;
		}
	}
	if (stdrp == NULL && dstrp == NULL) {
		/*
		** There are no rules running through "max".
		** Find the latest std rule in stdabbrrp
		** and latest rule of any type in stdrp.
		*/
		struct rule *stdabbrrp = NULL;
		for (i = 0; i < zp->z_nrules; ++i) {
			rp = &zp->z_rules[i];
			if (rp->r_stdoff == 0 && rule_cmp(stdabbrrp, rp) < 0)
				stdabbrrp = rp;
			if (rule_cmp(stdrp, rp) < 0)
				stdrp = rp;
		}
		/*
		** Horrid special case: if year is 2037,
		** presume this is a zone handled on a year-by-year basis;
		** do not try to apply a rule to the zone.
		*/
		if (stdrp != NULL && stdrp->r_hiyear == 2037)
			return YEAR_BY_YEAR_ZONE;

		if (stdrp != NULL && stdrp->r_stdoff != 0) {
			/* Perpetual DST.  */
			dstr.r_month = TM_JANUARY;
			dstr.r_dycode = DC_DOM;
			dstr.r_dayofmonth = 1;
			dstr.r_tod = 0;
			dstr.r_todisstd = dstr.r_todisgmt = FALSE;
			dstr.r_stdoff = stdrp->r_stdoff;
			dstr.r_abbrvar = stdrp->r_abbrvar;
			stdr.r_month = TM_DECEMBER;
			stdr.r_dycode = DC_DOM;
			stdr.r_dayofmonth = 31;
			stdr.r_tod = SECSPERDAY + stdrp->r_stdoff;
			stdr.r_todisstd = stdr.r_todisgmt = FALSE;
			stdr.r_stdoff = 0;
			stdr.r_abbrvar
			  = (stdabbrrp ? stdabbrrp->r_abbrvar : "");
			dstrp = &dstr;
			stdrp = &stdr;
		}
	}
	if (stdrp == NULL && (zp->z_nrules != 0 || zp->z_stdoff != 0))
		return -1;
	abbrvar = (stdrp == NULL) ? "" : stdrp->r_abbrvar;
	doabbr(result, size, zp, abbrvar, 0, TRUE);
	ep = end(result, size);
	if (stringoffset(ep, size - (ep - result), -zp->z_gmtoff) != 0) {
		result[0] = '\0';
		return -1;
	}
	if (dstrp == NULL)
		return compat;
	ep = end(result, size);
	doabbr(ep, size - (ep - result), zp, dstrp->r_abbrvar, dstrp->r_stdoff,
	    TRUE);
	if (dstrp->r_stdoff != SECSPERMIN * MINSPERHOUR) {
		ep = end(result, size);
		if (stringoffset(ep, size - (ep - result),
			-(zp->z_gmtoff + dstrp->r_stdoff)) != 0) {
				result[0] = '\0';
				return -1;
		}
	}
	strlcat(result, ",", size);
	c = stringrule(result, size, dstrp, dstrp->r_stdoff, zp->z_gmtoff);
	if (c < 0) {
		result[0] = '\0';
		return -1;
	}
	if (compat < c)
		compat = c;
	strlcat(result, ",", size);
	c = stringrule(result, size, stdrp, dstrp->r_stdoff, zp->z_gmtoff);
	if (c < 0) {
		result[0] = '\0';
		return -1;
	}
	if (compat < c)
		compat = c;
	return compat;
}

static void
outzone(const struct zone *zpfirst, int zonecount)
{
	const struct zone	*zp;
	struct rule		*rp;
	int			i, j, usestart, useuntil, type;
	zic_t			starttime = 0, untiltime = 0;
	zic_t			gmtoff, stdoff, startoff, year;
	int			startttisstd = FALSE, startttisgmt = FALSE;
	char			*startbuf, *ab, *envvar;
	int			max_abbr_len, max_envvar_len;
	int			prodstic; /* all rules are min to max */
	int			compat, do_extend;
	char			version;

	max_abbr_len = 2 + max_format_len + max_abbrvar_len;
	max_envvar_len = 2 * max_abbr_len + 5 * 9;
	startbuf = emalloc(max_abbr_len + 1);
	ab = emalloc(max_abbr_len + 1);
	envvar = emalloc(max_envvar_len + 1);
	/*
	** Now. . .finally. . .generate some useful data!
	*/
	timecnt = 0;
	typecnt = 0;
	charcnt = 0;
	prodstic = zonecount == 1;
	/*
	** Thanks to Earl Chew
	** for noting the need to unconditionally initialize startttisstd.
	*/
	min_year = max_year = EPOCH_YEAR;
	if (leapseen) {
		updateminmax(leapminyear);
		updateminmax(leapmaxyear + (leapmaxyear < ZIC_MAX));
	}
	for (i = 0; i < zonecount; ++i) {
		zp = &zpfirst[i];
		if (i < zonecount - 1)
			updateminmax(zp->z_untilrule.r_loyear);
		for (j = 0; j < zp->z_nrules; ++j) {
			rp = &zp->z_rules[j];
			if (rp->r_lowasnum)
				updateminmax(rp->r_loyear);
			if (rp->r_hiwasnum)
				updateminmax(rp->r_hiyear);
			if (rp->r_lowasnum || rp->r_hiwasnum)
				prodstic = FALSE;
		}
	}
	/*
	** Generate lots of data if a rule can't cover all future times.
	*/
	compat = stringzone(envvar, max_envvar_len + 1, zpfirst, zonecount);
	version = compat < 2013 ? ZIC_VERSION_PRE_2013 : ZIC_VERSION;
	do_extend = compat < 0 || compat == YEAR_BY_YEAR_ZONE;
	if (noise) {
		if (!*envvar)
			warning("no POSIX environment variable for zone %s",
				zpfirst->z_name);
		else if (compat != 0 && compat != YEAR_BY_YEAR_ZONE) {
			/* Circa-COMPAT clients, and earlier clients, might
			   not work for this zone when given dates before
			   1970 or after 2038.  */
			warning("%s: pre-%d clients may mishandle"
				  " distant timestamps",
				zpfirst->z_name, compat);
		}
	}
	if (do_extend) {
		/*
		** Search through a couple of extra years past the obvious
		** 400, to avoid edge cases.  For example, suppose a non-POSIX
		** rule applies from 2012 onwards and has transitions in March
		** and September, plus some one-off transitions in November
		** 2013.  If zic looked only at the last 400 years, it would
		** set max_year=2413, with the intent that the 400 years 2014
		** through 2413 will be repeated.  The last transition listed
		** in the tzfile would be in 2413-09, less than 400 years
		** after the last one-off transition in 2013-11.  Two years
		** might be overkill, but with the kind of edge cases
		** available we're not sure that one year would suffice.
		*/
		enum { years_of_observations = YEARSPERREPEAT + 2 };

		if (min_year >= ZIC_MIN + years_of_observations)
			min_year -= years_of_observations;
		else
			min_year = ZIC_MIN;
		if (max_year <= ZIC_MAX - years_of_observations)
			max_year += years_of_observations;
		else
			max_year = ZIC_MAX;
		/*
		** Regardless of any of the above,
		** for a "proDSTic" zone which specifies that its rules
		** always have and always will be in effect,
		** we only need one cycle to define the zone.
		*/
		if (prodstic) {
			min_year = 1900;
			max_year = min_year + years_of_observations;
		}
	}
	/*
	** For the benefit of older systems,
	** generate data from 1900 through 2037.
	*/
	if (min_year > 1900)
		min_year = 1900;
	if (max_year < 2037)
		max_year = 2037;
	for (i = 0; i < zonecount; ++i) {
		/*
		** A guess that may well be corrected later.
		*/
		stdoff = 0;
		zp = &zpfirst[i];
		usestart = i > 0 && (zp - 1)->z_untiltime > min_time;
		useuntil = i < (zonecount - 1);
		if (useuntil && zp->z_untiltime <= min_time)
			continue;
		gmtoff = zp->z_gmtoff;
		eat(zp->z_filename, zp->z_linenum);
		*startbuf = '\0';
		startoff = zp->z_gmtoff;
		if (zp->z_nrules == 0) {
			stdoff = zp->z_stdoff;
			doabbr(startbuf, max_abbr_len + 1, zp, NULL,
			    stdoff, FALSE);
			type = addtype(oadd(zp->z_gmtoff, stdoff),
				startbuf, stdoff != 0, startttisstd,
				startttisgmt);
			if (usestart) {
				addtt(starttime, type);
				usestart = FALSE;
			} else if (stdoff != 0)
				addtt(min_time, type);
		} else for (year = min_year; year <= max_year; ++year) {
			if (useuntil && year > zp->z_untilrule.r_hiyear)
				break;
			/*
			** Mark which rules to do in the current year.
			** For those to do, calculate rpytime(rp, year);
			*/
			for (j = 0; j < zp->z_nrules; ++j) {
				rp = &zp->z_rules[j];
				eats(zp->z_filename, zp->z_linenum,
					rp->r_filename, rp->r_linenum);
				rp->r_todo = year >= rp->r_loyear &&
				    year <= rp->r_hiyear;
				if (rp->r_todo)
					rp->r_temp = rpytime(rp, year);
			}
			for ( ; ; ) {
				int	k;
				zic_t	jtime, ktime = 0;
				zic_t	offset;

				if (useuntil) {
					/*
					** Turn untiltime into UT
					** assuming the current gmtoff and
					** stdoff values.
					*/
					untiltime = zp->z_untiltime;
					if (!zp->z_untilrule.r_todisgmt)
						untiltime = tadd(untiltime,
							-gmtoff);
					if (!zp->z_untilrule.r_todisstd)
						untiltime = tadd(untiltime,
							-stdoff);
				}
				/*
				** Find the rule (of those to do, if any)
				** that takes effect earliest in the year.
				*/
				k = -1;
				for (j = 0; j < zp->z_nrules; ++j) {
					rp = &zp->z_rules[j];
					if (!rp->r_todo)
						continue;
					eats(zp->z_filename, zp->z_linenum,
						rp->r_filename, rp->r_linenum);
					offset = rp->r_todisgmt ? 0 : gmtoff;
					if (!rp->r_todisstd)
						offset = oadd(offset, stdoff);
					jtime = rp->r_temp;
					if (jtime == min_time ||
						jtime == max_time)
							continue;
					jtime = tadd(jtime, -offset);
					if (k < 0 || jtime < ktime) {
						k = j;
						ktime = jtime;
					}
				}
				if (k < 0)
					break;	/* go on to next year */
				rp = &zp->z_rules[k];
				rp->r_todo = FALSE;
				if (useuntil && ktime >= untiltime)
					break;
				stdoff = rp->r_stdoff;
				if (usestart && ktime == starttime)
					usestart = FALSE;
				if (usestart) {
					if (ktime < starttime) {
						startoff = oadd(zp->z_gmtoff,
						    stdoff);
						doabbr(startbuf,
						    max_abbr_len + 1,
						    zp,
						    rp->r_abbrvar,
						    rp->r_stdoff,
						    FALSE);
						continue;
					}
					if (*startbuf == '\0' &&
					    startoff == oadd(zp->z_gmtoff,
					    stdoff)) {
						doabbr(startbuf,
						    max_abbr_len + 1,
						    zp,
						    rp->r_abbrvar,
						    rp->r_stdoff,
						    FALSE);
					}
				}
				eats(zp->z_filename, zp->z_linenum,
				    rp->r_filename, rp->r_linenum);
				doabbr(ab, max_abbr_len + 1, zp,
				    rp->r_abbrvar, rp->r_stdoff, FALSE);
				offset = oadd(zp->z_gmtoff, rp->r_stdoff);
				type = addtype(offset, ab, rp->r_stdoff != 0,
				    rp->r_todisstd, rp->r_todisgmt);
				addtt(ktime, type);
			}
		}
		if (usestart) {
			if (*startbuf == '\0' &&
			    zp->z_format != NULL &&
			    strchr(zp->z_format, '%') == NULL &&
			    strchr(zp->z_format, '/') == NULL)
				strlcpy(startbuf, zp->z_format, max_abbr_len + 1);
			eat(zp->z_filename, zp->z_linenum);
			if (*startbuf == '\0')
				error("can't determine time zone abbreviation to use just after until time");
			else
				addtt(starttime,
				    addtype(startoff, startbuf,
				    startoff != zp->z_gmtoff,
				    startttisstd, startttisgmt));
		}
		/*
		** Now we may get to set starttime for the next zone line.
		*/
		if (useuntil) {
			startttisstd = zp->z_untilrule.r_todisstd;
			startttisgmt = zp->z_untilrule.r_todisgmt;
			starttime = zp->z_untiltime;
			if (!startttisstd)
				starttime = tadd(starttime, -stdoff);
			if (!startttisgmt)
				starttime = tadd(starttime, -gmtoff);
		}
	}
	if (do_extend) {
		/*
		** If we're extending the explicitly listed observations
		** for 400 years because we can't fill the POSIX-TZ field,
		** check whether we actually ended up explicitly listing
		** observations through that period.  If there aren't any
		** near the end of the 400-year period, add a redundant
		** one at the end of the final year, to make it clear
		** that we are claiming to have definite knowledge of
		** the lack of transitions up to that point.
		*/
		struct rule xr;
		struct attype *lastat;
		xr.r_month = TM_JANUARY;
		xr.r_dycode = DC_DOM;
		xr.r_dayofmonth = 1;
		xr.r_tod = 0;
		for (lastat = &attypes[0], i = 1; i < timecnt; i++)
			if (attypes[i].at > lastat->at)
				lastat = &attypes[i];
		if (lastat->at < rpytime(&xr, max_year - 1)) {
			/*
			** Create new type code for the redundant entry,
			** to prevent it being optimised away.
			*/
			if (typecnt >= TZ_MAX_TYPES) {
				error("too many local time types");
				exit(EXIT_FAILURE);
			}
			gmtoffs[typecnt] = gmtoffs[lastat->type];
			isdsts[typecnt] = isdsts[lastat->type];
			ttisstds[typecnt] = ttisstds[lastat->type];
			ttisgmts[typecnt] = ttisgmts[lastat->type];
			abbrinds[typecnt] = abbrinds[lastat->type];
			++typecnt;
			addtt(rpytime(&xr, max_year + 1), typecnt-1);
		}
	}
	writezone(zpfirst->z_name, envvar, version);
	free(startbuf);
	free(ab);
	free(envvar);
}

static void
addtt(const zic_t starttime, int type)
{
	size_t len;

	if (starttime <= min_time ||
	    (timecnt == 1 && attypes[0].at < min_time)) {
		gmtoffs[0] = gmtoffs[type];
		isdsts[0] = isdsts[type];
		ttisstds[0] = ttisstds[type];
		ttisgmts[0] = ttisgmts[type];
		if (abbrinds[type] != 0) {
			len = strlen(&chars[abbrinds[type]]) + 1;
			memmove(chars, &chars[abbrinds[type]], len);
		}
		abbrinds[0] = 0;
		charcnt = strlen(chars) + 1;
		typecnt = 1;
		timecnt = 0;
		type = 0;
	}
	if (timecnt >= TZ_MAX_TIMES) {
		error("too many transitions?!");
		exit(EXIT_FAILURE);
	}
	attypes[timecnt].at = starttime;
	attypes[timecnt].type = type;
	++timecnt;
}

static int
addtype(zic_t gmtoff, const char *abbr, int isdst, int ttisstd, int ttisgmt)
{
	int	i, j;

	if (isdst != TRUE && isdst != FALSE) {
		error("internal error - addtype called with bad isdst");
		exit(EXIT_FAILURE);
	}
	if (ttisstd != TRUE && ttisstd != FALSE) {
		error("internal error - addtype called with bad ttisstd");
		exit(EXIT_FAILURE);
	}
	if (ttisgmt != TRUE && ttisgmt != FALSE) {
		error("internal error - addtype called with bad ttisgmt");
		exit(EXIT_FAILURE);
	}
	/*
	** See if there's already an entry for this zone type.
	** If so, just return its index.
	*/
	for (i = 0; i < typecnt; ++i) {
		if (gmtoff == gmtoffs[i] && isdst == isdsts[i] &&
		    strcmp(abbr, &chars[abbrinds[i]]) == 0 &&
		    ttisstd == ttisstds[i] &&
		    ttisgmt == ttisgmts[i])
			return i;
	}
	/*
	** There isn't one; add a new one, unless there are already too
	** many.
	*/
	if (typecnt >= TZ_MAX_TYPES) {
		error("too many local time types");
		exit(EXIT_FAILURE);
	}
	if (! (-1L - 2147483647L <= gmtoff && gmtoff <= 2147483647L)) {
		error("UT offset out of range");
		exit(EXIT_FAILURE);
	}
	gmtoffs[i] = gmtoff;
	isdsts[i] = isdst;
	ttisstds[i] = ttisstd;
	ttisgmts[i] = ttisgmt;

	for (j = 0; j < charcnt; ++j)
		if (strcmp(&chars[j], abbr) == 0)
			break;
	if (j == charcnt)
		newabbr(abbr);
	abbrinds[i] = j;
	++typecnt;
	return i;
}

static void
leapadd(zic_t t, int positive, int rolling, int count)
{
	int	i, j;

	if (leapcnt + (positive ? count : 1) > TZ_MAX_LEAPS) {
		error("too many leap seconds");
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < leapcnt; ++i)
		if (t <= trans[i]) {
			if (t == trans[i]) {
				error("repeated leap second moment");
				exit(EXIT_FAILURE);
			}
			break;
		}
	do {
		for (j = leapcnt; j > i; --j) {
			trans[j] = trans[j - 1];
			corr[j] = corr[j - 1];
			roll[j] = roll[j - 1];
		}
		trans[i] = t;
		corr[i] = positive ? 1 : -count;
		roll[i] = rolling;
		++leapcnt;
	} while (positive && --count != 0);
}

static void
adjleap(void)
{
	int	i;
	zic_t	last = 0;

	/*
	** propagate leap seconds forward
	*/
	for (i = 0; i < leapcnt; ++i) {
		trans[i] = tadd(trans[i], last);
		last = corr[i] += last;
	}
}

/* this function is not strncasecmp */
static __pure int
itsabbr(const char *sabbr, const char *sword)
{
	const unsigned char *abbr = sabbr;
	const unsigned char *word = sword;

	if (tolower(*abbr) != tolower(*word))
		return FALSE;
	while (*++abbr != '\0')
		do {
			++word;
			if (*word == '\0')
				return FALSE;
		} while (tolower(*word) != tolower(*abbr));
	return TRUE;
}

static __pure const struct lookup *
byword(const char *word, const struct lookup *table)
{
	const struct lookup	*foundlp;
	const struct lookup	*lp;

	if (word == NULL || table == NULL)
		return NULL;
	/*
	** Look for exact match.
	*/
	for (lp = table; lp->l_word != NULL; ++lp)
		if (strcasecmp(word, lp->l_word) == 0)
			return lp;
	/*
	** Look for inexact match.
	*/
	foundlp = NULL;
	for (lp = table; lp->l_word != NULL; ++lp)
		if (itsabbr(word, lp->l_word)) {
			if (foundlp == NULL)
				foundlp = lp;
			else
				return NULL;	/* multiple inexact matches */
		}
	return foundlp;
}

static char **
getfields(char *cp)
{
	char		*dp;
	char		**array;
	int		nsubs;

	if (cp == NULL)
		return NULL;
	array = ereallocarray(NULL, strlen(cp) + 1, sizeof *array);
	nsubs = 0;
	for ( ; ; ) {
		while (isascii((unsigned char)*cp) &&
		    isspace((unsigned char)*cp))
			++cp;
		if (*cp == '\0' || *cp == '#')
			break;
		array[nsubs++] = dp = cp;
		do {
			if ((*dp = *cp++) != '"') {
				++dp;
			} else {
				while ((*dp = *cp++) != '"') {
					if (*dp != '\0')
						++dp;
					else {
						error("Odd number of quotation marks");
						exit(EXIT_FAILURE);
					}
				}
			}
		} while (*cp != '\0' && *cp != '#' &&
		    (!isascii((unsigned char)*cp) || !isspace((unsigned char)*cp)));
		if (isascii((unsigned char)*cp) && isspace((unsigned char)*cp))
			++cp;
		*dp = '\0';
	}
	array[nsubs] = NULL;
	return array;
}

static __pure zic_t
oadd(zic_t t1, zic_t t2)
{
	if (t1 < 0 ? t2 < ZIC_MIN - t1 : ZIC_MAX - t1 < t2) {
		error("time overflow");
		exit(EXIT_FAILURE);
	}
	return t1 + t2;
}

static __pure zic_t
tadd(zic_t t1, zic_t t2)
{
	if (t1 == max_time && t2 > 0)
		return max_time;
	if (t1 == min_time && t2 < 0)
		return min_time;
	if (t1 < 0 ? t2 < min_time - t1 : max_time - t1 < t2) {
		error("time overflow");
		exit(EXIT_FAILURE);
	}
	return t1 + t2;
}

/*
** Given a rule, and a year, compute the date - in seconds since January 1,
** 1970, 00:00 LOCAL time - in that year that the rule refers to.
*/
static zic_t
rpytime(const struct rule *rp, zic_t wantedy)
{
	int	m, i;
	zic_t	dayoff;			/* with a nod to Margaret O. */
	zic_t	t, y;

	if (wantedy == ZIC_MIN)
		return min_time;
	if (wantedy == ZIC_MAX)
		return max_time;
	dayoff = 0;
	m = TM_JANUARY;
	y = EPOCH_YEAR;
	while (wantedy != y) {
		if (wantedy > y) {
			i = len_years[isleap(y)];
			++y;
		} else {
			--y;
			i = -len_years[isleap(y)];
		}
		dayoff = oadd(dayoff, i);
	}
	while (m != rp->r_month) {
		i = len_months[isleap(y)][m];
		dayoff = oadd(dayoff, i);
		++m;
	}
	i = rp->r_dayofmonth;
	if (m == TM_FEBRUARY && i == 29 && !isleap(y)) {
		if (rp->r_dycode == DC_DOWLEQ)
			--i;
		else {
			error("use of 2/29 in non leap-year");
			exit(EXIT_FAILURE);
		}
	}
	--i;
	dayoff = oadd(dayoff, i);
	if (rp->r_dycode == DC_DOWGEQ || rp->r_dycode == DC_DOWLEQ) {
		zic_t	wday = EPOCH_WDAY;

		/*
		** Don't trust mod of negative numbers.
		*/
		if (dayoff >= 0)
			wday = (wday + dayoff) % DAYSPERWEEK;
		else {
			wday -= ((-dayoff) % DAYSPERWEEK);
			if (wday < 0)
				wday += DAYSPERWEEK;
		}
		while (wday != rp->r_wday)
			if (rp->r_dycode == DC_DOWGEQ) {
				dayoff = oadd(dayoff, 1);
				if (++wday >= DAYSPERWEEK)
					wday = 0;
				++i;
			} else {
				dayoff = oadd(dayoff, -1);
				if (--wday < 0)
					wday = DAYSPERWEEK - 1;
				--i;
			}
	}
	if (dayoff < min_time / SECSPERDAY)
		return min_time;
	if (dayoff > max_time / SECSPERDAY)
		return max_time;
	t = (zic_t) dayoff * SECSPERDAY;
	return tadd(t, rp->r_tod);
}

static void
newabbr(const char *string)
{
	int	i;

	if (strcmp(string, GRANDPARENTED) != 0) {
		const char *	cp;
		const char *	mp;

		cp = string;
		mp = NULL;
		while (isascii((unsigned char)*cp) &&
		    (isalnum((unsigned char)*cp) || *cp == '-' || *cp == '+'))
			++cp;
		if (noise && cp - string > 3)
			mp = "time zone abbreviation has more than 3 characters";
		if (cp - string > ZIC_MAX_ABBR_LEN_WO_WARN)
			mp = "time zone abbreviation has too many characters";
		if (*cp != '\0')
			mp = "time zone abbreviation differs from POSIX standard";
		if (mp != NULL)
			warning("%s (%s)", mp, string);
	}
	i = strlen(string) + 1;
	if (charcnt + i > TZ_MAX_CHARS) {
		error("too many, or too long, time zone abbreviations");
		exit(EXIT_FAILURE);
	}
	strlcpy(&chars[charcnt], string, sizeof(chars) - charcnt);
	charcnt += i;
}

static int
mkdirs(char *argname)
{
	char *	name;
	char *	cp;

	if (argname == NULL || *argname == '\0')
		return 0;
	cp = name = ecpyalloc(argname);
	while ((cp = strchr(cp + 1, '/')) != 0) {
		*cp = '\0';
		if (!itsdir(name)) {
			/*
			** It doesn't seem to exist, so we try to create it.
			** Creation may fail because of the directory being
			** created by some other multiprocessor, so we get
			** to do extra checking.
			*/
			if (mkdir(name, MKDIR_UMASK) != 0) {
				const char *e = strerror(errno);

				if (errno != EEXIST || !itsdir(name)) {
					fprintf(stderr,
					    "%s: Can't create directory %s: %s\n",
					    __progname, name, e);
					free(name);
					return -1;
				}
			}
		}
		*cp = '/';
	}
	free(name);
	return 0;
}
