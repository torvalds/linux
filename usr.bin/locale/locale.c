/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002, 2003 Alexey Zelkin <phantom@FreeBSD.org>
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

/*
 * XXX: implement missing era_* (LC_TIME) keywords (require libc &
 *	nl_langinfo(3) extensions)
 *
 * XXX: correctly handle reserved 'charmap' keyword and '-m' option (require
 *	localedef(1) implementation).  Currently it's handled via
 *	nl_langinfo(CODESET).
 */

#include <sys/param.h>
#include <sys/types.h>

#include <dirent.h>
#include <err.h>
#include <limits.h>
#include <locale.h>
#include <langinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stringlist.h>
#include <unistd.h>
#include "setlocale.h"

/* Local prototypes */
char	*format_grouping(const char *);
void	init_locales_list(void);
void	list_charmaps(void);
void	list_locales(void);
const char *lookup_localecat(int);
char	*kwval_lconv(int);
int	kwval_lookup(const char *, char **, int *, int *);
void	showdetails(const char *);
void	showkeywordslist(char *substring);
void	showlocale(void);
void	usage(void);

/* Global variables */
static StringList *locales = NULL;

static int	all_locales = 0;
static int	all_charmaps = 0;
static int	prt_categories = 0;
static int	prt_keywords = 0;

static const struct _lcinfo {
	const char	*name;
	int		id;
} lcinfo [] = {
	{ "LC_CTYPE",		LC_CTYPE },
	{ "LC_COLLATE",		LC_COLLATE },
	{ "LC_TIME",		LC_TIME },
	{ "LC_NUMERIC",		LC_NUMERIC },
	{ "LC_MONETARY",	LC_MONETARY },
	{ "LC_MESSAGES",	LC_MESSAGES }
};
#define	NLCINFO nitems(lcinfo)

/* ids for values not referenced by nl_langinfo() */
#define	KW_ZERO			10000
#define	KW_GROUPING		(KW_ZERO+1)
#define	KW_INT_CURR_SYMBOL	(KW_ZERO+2)
#define	KW_CURRENCY_SYMBOL	(KW_ZERO+3)
#define	KW_MON_DECIMAL_POINT	(KW_ZERO+4)
#define	KW_MON_THOUSANDS_SEP	(KW_ZERO+5)
#define	KW_MON_GROUPING		(KW_ZERO+6)
#define	KW_POSITIVE_SIGN	(KW_ZERO+7)
#define	KW_NEGATIVE_SIGN	(KW_ZERO+8)
#define	KW_INT_FRAC_DIGITS	(KW_ZERO+9)
#define	KW_FRAC_DIGITS		(KW_ZERO+10)
#define	KW_P_CS_PRECEDES	(KW_ZERO+11)
#define	KW_P_SEP_BY_SPACE	(KW_ZERO+12)
#define	KW_N_CS_PRECEDES	(KW_ZERO+13)
#define	KW_N_SEP_BY_SPACE	(KW_ZERO+14)
#define	KW_P_SIGN_POSN		(KW_ZERO+15)
#define	KW_N_SIGN_POSN		(KW_ZERO+16)
#define	KW_INT_P_CS_PRECEDES	(KW_ZERO+17)
#define	KW_INT_P_SEP_BY_SPACE	(KW_ZERO+18)
#define	KW_INT_N_CS_PRECEDES	(KW_ZERO+19)
#define	KW_INT_N_SEP_BY_SPACE	(KW_ZERO+20)
#define	KW_INT_P_SIGN_POSN	(KW_ZERO+21)
#define	KW_INT_N_SIGN_POSN	(KW_ZERO+22)

static const struct _kwinfo {
	const char	*name;
	int		isstr;		/* true - string, false - number */
	int		catid;		/* LC_* */
	int		value_ref;
	const char	*comment;
} kwinfo [] = {
	{ "charmap",		1, LC_CTYPE,	CODESET, "" },	/* hack */

	{ "decimal_point",	1, LC_NUMERIC,	RADIXCHAR, "" },
	{ "thousands_sep",	1, LC_NUMERIC,	THOUSEP, "" },
	{ "grouping",		1, LC_NUMERIC,	KW_GROUPING, "" },
	{ "radixchar",		1, LC_NUMERIC,	RADIXCHAR,
	  "Same as decimal_point (FreeBSD only)" },		/* compat */
	{ "thousep",		1, LC_NUMERIC,	THOUSEP,
	  "Same as thousands_sep (FreeBSD only)" },		/* compat */

	{ "int_curr_symbol",	1, LC_MONETARY,	KW_INT_CURR_SYMBOL, "" },
	{ "currency_symbol",	1, LC_MONETARY,	KW_CURRENCY_SYMBOL, "" },
	{ "mon_decimal_point",	1, LC_MONETARY,	KW_MON_DECIMAL_POINT, "" },
	{ "mon_thousands_sep",	1, LC_MONETARY,	KW_MON_THOUSANDS_SEP, "" },
	{ "mon_grouping",	1, LC_MONETARY,	KW_MON_GROUPING, "" },
	{ "positive_sign",	1, LC_MONETARY,	KW_POSITIVE_SIGN, "" },
	{ "negative_sign",	1, LC_MONETARY,	KW_NEGATIVE_SIGN, "" },

	{ "int_frac_digits",	0, LC_MONETARY,	KW_INT_FRAC_DIGITS, "" },
	{ "frac_digits",	0, LC_MONETARY,	KW_FRAC_DIGITS, "" },
	{ "p_cs_precedes",	0, LC_MONETARY,	KW_P_CS_PRECEDES, "" },
	{ "p_sep_by_space",	0, LC_MONETARY,	KW_P_SEP_BY_SPACE, "" },
	{ "n_cs_precedes",	0, LC_MONETARY,	KW_N_CS_PRECEDES, "" },
	{ "n_sep_by_space",	0, LC_MONETARY,	KW_N_SEP_BY_SPACE, "" },
	{ "p_sign_posn",	0, LC_MONETARY,	KW_P_SIGN_POSN, "" },
	{ "n_sign_posn",	0, LC_MONETARY,	KW_N_SIGN_POSN, "" },
	{ "int_p_cs_precedes",	0, LC_MONETARY,	KW_INT_P_CS_PRECEDES, "" },
	{ "int_p_sep_by_space",	0, LC_MONETARY,	KW_INT_P_SEP_BY_SPACE, "" },
	{ "int_n_cs_precedes",	0, LC_MONETARY,	KW_INT_N_CS_PRECEDES, "" },
	{ "int_n_sep_by_space",	0, LC_MONETARY,	KW_INT_N_SEP_BY_SPACE, "" },
	{ "int_p_sign_posn",	0, LC_MONETARY,	KW_INT_P_SIGN_POSN, "" },
	{ "int_n_sign_posn",	0, LC_MONETARY,	KW_INT_N_SIGN_POSN, "" },

	{ "d_t_fmt",		1, LC_TIME,	D_T_FMT, "" },
	{ "d_fmt",		1, LC_TIME,	D_FMT, "" },
	{ "t_fmt",		1, LC_TIME,	T_FMT, "" },
	{ "am_str",		1, LC_TIME,	AM_STR, "" },
	{ "pm_str",		1, LC_TIME,	PM_STR, "" },
	{ "t_fmt_ampm",		1, LC_TIME,	T_FMT_AMPM, "" },
	{ "day_1",		1, LC_TIME,	DAY_1, "" },
	{ "day_2",		1, LC_TIME,	DAY_2, "" },
	{ "day_3",		1, LC_TIME,	DAY_3, "" },
	{ "day_4",		1, LC_TIME,	DAY_4, "" },
	{ "day_5",		1, LC_TIME,	DAY_5, "" },
	{ "day_6",		1, LC_TIME,	DAY_6, "" },
	{ "day_7",		1, LC_TIME,	DAY_7, "" },
	{ "abday_1",		1, LC_TIME,	ABDAY_1, "" },
	{ "abday_2",		1, LC_TIME,	ABDAY_2, "" },
	{ "abday_3",		1, LC_TIME,	ABDAY_3, "" },
	{ "abday_4",		1, LC_TIME,	ABDAY_4, "" },
	{ "abday_5",		1, LC_TIME,	ABDAY_5, "" },
	{ "abday_6",		1, LC_TIME,	ABDAY_6, "" },
	{ "abday_7",		1, LC_TIME,	ABDAY_7, "" },
	{ "mon_1",		1, LC_TIME,	MON_1, "" },
	{ "mon_2",		1, LC_TIME,	MON_2, "" },
	{ "mon_3",		1, LC_TIME,	MON_3, "" },
	{ "mon_4",		1, LC_TIME,	MON_4, "" },
	{ "mon_5",		1, LC_TIME,	MON_5, "" },
	{ "mon_6",		1, LC_TIME,	MON_6, "" },
	{ "mon_7",		1, LC_TIME,	MON_7, "" },
	{ "mon_8",		1, LC_TIME,	MON_8, "" },
	{ "mon_9",		1, LC_TIME,	MON_9, "" },
	{ "mon_10",		1, LC_TIME,	MON_10, "" },
	{ "mon_11",		1, LC_TIME,	MON_11, "" },
	{ "mon_12",		1, LC_TIME,	MON_12, "" },
	{ "abmon_1",		1, LC_TIME,	ABMON_1, "" },
	{ "abmon_2",		1, LC_TIME,	ABMON_2, "" },
	{ "abmon_3",		1, LC_TIME,	ABMON_3, "" },
	{ "abmon_4",		1, LC_TIME,	ABMON_4, "" },
	{ "abmon_5",		1, LC_TIME,	ABMON_5, "" },
	{ "abmon_6",		1, LC_TIME,	ABMON_6, "" },
	{ "abmon_7",		1, LC_TIME,	ABMON_7, "" },
	{ "abmon_8",		1, LC_TIME,	ABMON_8, "" },
	{ "abmon_9",		1, LC_TIME,	ABMON_9, "" },
	{ "abmon_10",		1, LC_TIME,	ABMON_10, "" },
	{ "abmon_11",		1, LC_TIME,	ABMON_11, "" },
	{ "abmon_12",		1, LC_TIME,	ABMON_12, "" },
	{ "altmon_1",		1, LC_TIME,	ALTMON_1, "(FreeBSD only)" },
	{ "altmon_2",		1, LC_TIME,	ALTMON_2, "(FreeBSD only)" },
	{ "altmon_3",		1, LC_TIME,	ALTMON_3, "(FreeBSD only)" },
	{ "altmon_4",		1, LC_TIME,	ALTMON_4, "(FreeBSD only)" },
	{ "altmon_5",		1, LC_TIME,	ALTMON_5, "(FreeBSD only)" },
	{ "altmon_6",		1, LC_TIME,	ALTMON_6, "(FreeBSD only)" },
	{ "altmon_7",		1, LC_TIME,	ALTMON_7, "(FreeBSD only)" },
	{ "altmon_8",		1, LC_TIME,	ALTMON_8, "(FreeBSD only)" },
	{ "altmon_9",		1, LC_TIME,	ALTMON_9, "(FreeBSD only)" },
	{ "altmon_10",		1, LC_TIME,	ALTMON_10, "(FreeBSD only)" },
	{ "altmon_11",		1, LC_TIME,	ALTMON_11, "(FreeBSD only)" },
	{ "altmon_12",		1, LC_TIME,	ALTMON_12, "(FreeBSD only)" },
	{ "era",		1, LC_TIME,	ERA, "(unavailable)" },
	{ "era_d_fmt",		1, LC_TIME,	ERA_D_FMT, "(unavailable)" },
	{ "era_d_t_fmt",	1, LC_TIME,	ERA_D_T_FMT, "(unavailable)" },
	{ "era_t_fmt",		1, LC_TIME,	ERA_T_FMT, "(unavailable)" },
	{ "alt_digits",		1, LC_TIME,	ALT_DIGITS, "" },
	{ "d_md_order",		1, LC_TIME,	D_MD_ORDER,
	  "(FreeBSD only)"				},	/* local */

	{ "yesexpr",		1, LC_MESSAGES, YESEXPR, "" },
	{ "noexpr",		1, LC_MESSAGES, NOEXPR, "" },
	{ "yesstr",		1, LC_MESSAGES, YESSTR,
	  "(POSIX legacy)" },					/* compat */
	{ "nostr",		1, LC_MESSAGES, NOSTR,
	  "(POSIX legacy)" }					/* compat */

};
#define	NKWINFO (nitems(kwinfo))

static const char *boguslocales[] = { "UTF-8" };
#define	NBOGUS	(nitems(boguslocales))

int
main(int argc, char *argv[])
{
	int	ch;
	int	tmp;

	while ((ch = getopt(argc, argv, "ackms:")) != -1) {
		switch (ch) {
		case 'a':
			all_locales = 1;
			break;
		case 'c':
			prt_categories = 1;
			break;
		case 'k':
			prt_keywords = 1;
			break;
		case 'm':
			all_charmaps = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* validate arguments */
	if (all_locales && all_charmaps)
		usage();
	if ((all_locales || all_charmaps) && argc > 0)
		usage();
	if ((all_locales || all_charmaps) && (prt_categories || prt_keywords))
		usage();

	/* process '-a' */
	if (all_locales) {
		list_locales();
		exit(0);
	}

	/* process '-m' */
	if (all_charmaps) {
		list_charmaps();
		exit(0);
	}

	/* check for special case '-k list' */
	tmp = 0;
	if (prt_keywords && argc > 0)
		while (tmp < argc)
			if (strcasecmp(argv[tmp++], "list") == 0) {
				showkeywordslist(argv[tmp]);
				exit(0);
			}

	/* process '-c', '-k', or command line arguments. */
	if (prt_categories || prt_keywords || argc > 0) {
		if (prt_keywords || argc > 0)
			setlocale(LC_ALL, "");
		if (argc > 0) {
			while (argc > 0) {
				showdetails(*argv);
				argv++;
				argc--;
			}
		} else {
			uint i;
			for (i = 0; i < nitems(kwinfo); i++)
				showdetails(kwinfo[i].name);
		}
		exit(0);
	}

	/* no arguments, show current locale state */
	showlocale();

	return (0);
}

void
usage(void)
{
	printf("Usage: locale [ -a | -m ]\n"
	       "       locale -k list [prefix]\n"
	       "       locale [ -ck ] [keyword ...]\n");
	exit(1);
}

/*
 * Output information about all available locales
 *
 * XXX actually output of this function does not guarantee that locale
 *     is really available to application, since it can be broken or
 *     inconsistent thus setlocale() will fail.  Maybe add '-V' function to
 *     also validate these locales?
 */
void
list_locales(void)
{
	size_t i;

	init_locales_list();
	for (i = 0; i < locales->sl_cur; i++) {
		printf("%s\n", locales->sl_str[i]);
	}
}

/*
 * qsort() helper function
 */
static int
scmp(const void *s1, const void *s2)
{
	return strcmp(*(const char * const *)s1, *(const char * const *)s2);
}

/*
 * Output information about all available charmaps
 *
 * XXX this function is doing a task in hackish way, i.e. by scaning
 *     list of locales, spliting their codeset part and building list of
 *     them.
 */
void
list_charmaps(void)
{
	size_t i;
	char *s, *cs;
	StringList *charmaps;

	/* initialize StringList */
	charmaps = sl_init();
	if (charmaps == NULL)
		err(1, "could not allocate memory");

	/* fetch locales list */
	init_locales_list();

	/* split codesets and build their list */
	for (i = 0; i < locales->sl_cur; i++) {
		s = locales->sl_str[i];
		if ((cs = strchr(s, '.')) != NULL) {
			cs++;
			if (sl_find(charmaps, cs) == NULL)
				sl_add(charmaps, cs);
		}
	}

	/* add US-ASCII, if not yet added */
	if (sl_find(charmaps, "US-ASCII") == NULL)
		sl_add(charmaps, strdup("US-ASCII"));

	/* sort the list */
	qsort(charmaps->sl_str, charmaps->sl_cur, sizeof(char *), scmp);

	/* print results */
	for (i = 0; i < charmaps->sl_cur; i++) {
		printf("%s\n", charmaps->sl_str[i]);
	}
}

/*
 * Retrieve sorted list of system locales (or user locales, if PATH_LOCALE
 * environment variable is set)
 */
void
init_locales_list(void)
{
	DIR *dirp;
	struct dirent *dp;
	size_t i;
	int bogus;

	/* why call this function twice ? */
	if (locales != NULL)
		return;

	/* initialize StringList */
	locales = sl_init();
	if (locales == NULL)
		err(1, "could not allocate memory");

	/* get actual locales directory name */
	if (__detect_path_locale() != 0)
		err(1, "unable to find locales storage");

	/* open locales directory */
	dirp = opendir(_PathLocale);
	if (dirp == NULL)
		err(1, "could not open directory '%s'", _PathLocale);

	/* scan directory and store its contents except "." and ".." */
	while ((dp = readdir(dirp)) != NULL) {
		if (*(dp->d_name) == '.')
			continue;		/* exclude "." and ".." */
		for (bogus = i = 0; i < NBOGUS; i++)
			if (strncmp(dp->d_name, boguslocales[i],
			    strlen(boguslocales[i])) == 0)
				bogus = 1;
		if (!bogus)
			sl_add(locales, strdup(dp->d_name));
	}
	closedir(dirp);

	/* make sure that 'POSIX' and 'C' locales are present in the list.
	 * POSIX 1003.1-2001 requires presence of 'POSIX' name only here, but
	 * we also list 'C' for constistency
	 */
	if (sl_find(locales, "POSIX") == NULL)
		sl_add(locales, strdup("POSIX"));

	if (sl_find(locales, "C") == NULL)
		sl_add(locales, strdup("C"));

	/* make output nicer, sort the list */
	qsort(locales->sl_str, locales->sl_cur, sizeof(char *), scmp);
}

/*
 * Show current locale status, depending on environment variables
 */
void
showlocale(void)
{
	size_t	i;
	const char *lang, *vval, *eval;

	setlocale(LC_ALL, "");

	lang = getenv("LANG");
	if (lang == NULL) {
		lang = "";
	}
	printf("LANG=%s\n", lang);
	/* XXX: if LANG is null, then set it to "C" to get implied values? */

	for (i = 0; i < NLCINFO; i++) {
		vval = setlocale(lcinfo[i].id, NULL);
		eval = getenv(lcinfo[i].name);
		if (eval != NULL && !strcmp(eval, vval)
				&& strcmp(lang, vval)) {
			/*
			 * Appropriate environment variable set, its value
			 * is valid and not overridden by LC_ALL
			 *
			 * XXX: possible side effect: if both LANG and
			 * overridden environment variable are set into same
			 * value, then it'll be assumed as 'implied'
			 */
			printf("%s=%s\n", lcinfo[i].name, vval);
		} else {
			printf("%s=\"%s\"\n", lcinfo[i].name, vval);
		}
	}

	vval = getenv("LC_ALL");
	if (vval == NULL) {
		vval = "";
	}
	printf("LC_ALL=%s\n", vval);
}

char *
format_grouping(const char *binary)
{
	static char rval[64];
	const char *cp;
	size_t roff;
	int len;

	rval[0] = '\0';
	roff = 0;
	for (cp = binary; *cp != '\0'; ++cp) {
#if CHAR_MIN != 0
		if (*cp < 0)
			break;		/* garbage input */
#endif
		len = snprintf(&rval[roff], sizeof(rval) - roff, "%u;", *cp);
		if (len < 0 || (unsigned)len >= sizeof(rval) - roff)
			break;		/* insufficient space for output */
		roff += len;
		if (*cp == CHAR_MAX)
			break;		/* special termination */
	}

	/* Truncate at the last successfully snprintf()ed semicolon. */
	if (roff != 0)
		rval[roff - 1] = '\0';

	return (&rval[0]);
}

/*
 * keyword value lookup helper (via localeconv())
 */
char *
kwval_lconv(int id)
{
	struct lconv *lc;
	char *rval;

	rval = NULL;
	lc = localeconv();
	switch (id) {
		case KW_GROUPING:
			rval = format_grouping(lc->grouping);
			break;
		case KW_INT_CURR_SYMBOL:
			rval = lc->int_curr_symbol;
			break;
		case KW_CURRENCY_SYMBOL:
			rval = lc->currency_symbol;
			break;
		case KW_MON_DECIMAL_POINT:
			rval = lc->mon_decimal_point;
			break;
		case KW_MON_THOUSANDS_SEP:
			rval = lc->mon_thousands_sep;
			break;
		case KW_MON_GROUPING:
			rval = format_grouping(lc->mon_grouping);
			break;
		case KW_POSITIVE_SIGN:
			rval = lc->positive_sign;
			break;
		case KW_NEGATIVE_SIGN:
			rval = lc->negative_sign;
			break;
		case KW_INT_FRAC_DIGITS:
			rval = &(lc->int_frac_digits);
			break;
		case KW_FRAC_DIGITS:
			rval = &(lc->frac_digits);
			break;
		case KW_P_CS_PRECEDES:
			rval = &(lc->p_cs_precedes);
			break;
		case KW_P_SEP_BY_SPACE:
			rval = &(lc->p_sep_by_space);
			break;
		case KW_N_CS_PRECEDES:
			rval = &(lc->n_cs_precedes);
			break;
		case KW_N_SEP_BY_SPACE:
			rval = &(lc->n_sep_by_space);
			break;
		case KW_P_SIGN_POSN:
			rval = &(lc->p_sign_posn);
			break;
		case KW_N_SIGN_POSN:
			rval = &(lc->n_sign_posn);
			break;
		case KW_INT_P_CS_PRECEDES:
			rval = &(lc->int_p_cs_precedes);
			break;
		case KW_INT_P_SEP_BY_SPACE:
			rval = &(lc->int_p_sep_by_space);
			break;
		case KW_INT_N_CS_PRECEDES:
			rval = &(lc->int_n_cs_precedes);
			break;
		case KW_INT_N_SEP_BY_SPACE:
			rval = &(lc->int_n_sep_by_space);
			break;
		case KW_INT_P_SIGN_POSN:
			rval = &(lc->int_p_sign_posn);
			break;
		case KW_INT_N_SIGN_POSN:
			rval = &(lc->int_n_sign_posn);
			break;
		default:
			break;
	}
	return (rval);
}

/*
 * keyword value and properties lookup
 */
int
kwval_lookup(const char *kwname, char **kwval, int *cat, int *isstr)
{
	int	rval;
	size_t	i;

	rval = 0;
	for (i = 0; i < NKWINFO; i++) {
		if (strcasecmp(kwname, kwinfo[i].name) == 0) {
			rval = 1;
			*cat = kwinfo[i].catid;
			*isstr = kwinfo[i].isstr;
			if (kwinfo[i].value_ref < KW_ZERO) {
				*kwval = nl_langinfo(kwinfo[i].value_ref);
			} else {
				*kwval = kwval_lconv(kwinfo[i].value_ref);
			}
			break;
		}
	}

	return (rval);
}

/*
 * Show details about requested keyword according to '-k' and/or '-c'
 * command line options specified.
 */
void
showdetails(const char *kw)
{
	int	isstr, cat, tmpval;
	char	*kwval;

	if (kwval_lookup(kw, &kwval, &cat, &isstr) == 0) {
		/*
		 * invalid keyword specified.
		 * XXX: any actions?
		 */
		fprintf(stderr, "Unknown keyword: `%s'\n", kw);
		return;
	}

	if (prt_categories) {
		if (prt_keywords)
			printf("%-20s ", lookup_localecat(cat));
		else
			printf("%-20s\t%s\n", kw, lookup_localecat(cat));
	}

	if (prt_keywords) {
		if (isstr) {
			printf("%s=\"%s\"\n", kw, kwval);
		} else {
			tmpval = (char) *kwval;
			printf("%s=%d\n", kw, tmpval);
		}
	}

	if (!prt_categories && !prt_keywords) {
		if (isstr) {
			printf("%s\n", kwval);
		} else {
			tmpval = (char) *kwval;
			printf("%d\n", tmpval);
		}
	}
}

/*
 * Convert locale category id into string
 */
const char *
lookup_localecat(int cat)
{
	size_t	i;

	for (i = 0; i < NLCINFO; i++)
		if (lcinfo[i].id == cat) {
			return (lcinfo[i].name);
		}
	return ("UNKNOWN");
}

/*
 * Show list of keywords
 */
void
showkeywordslist(char *substring)
{
	size_t	i;

#define	FMT "%-20s %-12s %-7s %-20s\n"

	if (substring == NULL)
		printf("List of available keywords\n\n");
	else
		printf("List of available keywords starting with '%s'\n\n",
		    substring);
	printf(FMT, "Keyword", "Category", "Type", "Comment");
	printf("-------------------- ------------ ------- --------------------\n");
	for (i = 0; i < NKWINFO; i++) {
		if (substring != NULL) {
			if (strncmp(kwinfo[i].name, substring,
			    strlen(substring)) != 0)
				continue;
		}
		printf(FMT,
			kwinfo[i].name,
			lookup_localecat(kwinfo[i].catid),
			(kwinfo[i].isstr == 0) ? "number" : "string",
			kwinfo[i].comment);
	}
}
