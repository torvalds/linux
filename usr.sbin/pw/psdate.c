/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1996
 *	David L. Nugent.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY DAVID L. NUGENT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL DAVID L. NUGENT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <ctype.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <xlocale.h>

#include "psdate.h"


int
numerics(char const * str)
{

	return (str[strspn(str, "0123456789x")] == '\0');
}

static int
aindex(char const * arr[], char const ** str, int len)
{
	int             l, i;
	char            mystr[32];

	mystr[len] = '\0';
	l = strlen(strncpy(mystr, *str, len));
	for (i = 0; i < l; i++)
		mystr[i] = (char) tolower((unsigned char)mystr[i]);
	for (i = 0; arr[i] && strcmp(mystr, arr[i]) != 0; i++);
	if (arr[i] == NULL)
		i = -1;
	else {			/* Skip past it */
		while (**str && isalpha((unsigned char)**str))
			++(*str);
		/* And any following whitespace */
		while (**str && (**str == ',' || isspace((unsigned char)**str)))
			++(*str);
	}			/* Return index */
	return i;
}

static int
weekday(char const ** str)
{
	static char const *days[] =
	{"sun", "mon", "tue", "wed", "thu", "fri", "sat", NULL};

	return aindex(days, str, 3);
}

static void
parse_datesub(char const * str, struct tm *t)
{
	struct tm	 tm;
	locale_t	 l;
	int		 i;
	char		*ret;
	const char	*valid_formats[] = {
		"%d-%b-%y",
		"%d-%b-%Y",
		"%d-%m-%y",
		"%d-%m-%Y",
		"%H:%M %d-%b-%y",
		"%H:%M %d-%b-%Y",
		"%H:%M %d-%m-%y",
		"%H:%M %d-%m-%Y",
		"%H:%M:%S %d-%b-%y",
		"%H:%M:%S %d-%b-%Y",
		"%H:%M:%S %d-%m-%y",
		"%H:%M:%S %d-%m-%Y",
		"%d-%b-%y %H:%M",
		"%d-%b-%Y %H:%M",
		"%d-%m-%y %H:%M",
		"%d-%m-%Y %H:%M",
		"%d-%b-%y %H:%M:%S",
		"%d-%b-%Y %H:%M:%S",
		"%d-%m-%y %H:%M:%S",
		"%d-%m-%Y %H:%M:%S",
		"%H:%M\t%d-%b-%y",
		"%H:%M\t%d-%b-%Y",
		"%H:%M\t%d-%m-%y",
		"%H:%M\t%d-%m-%Y",
		"%H:%M\t%S %d-%b-%y",
		"%H:%M\t%S %d-%b-%Y",
		"%H:%M\t%S %d-%m-%y",
		"%H:%M\t%S %d-%m-%Y",
		"%d-%b-%y\t%H:%M",
		"%d-%b-%Y\t%H:%M",
		"%d-%m-%y\t%H:%M",
		"%d-%m-%Y\t%H:%M",
		"%d-%b-%y\t%H:%M:%S",
		"%d-%b-%Y\t%H:%M:%S",
		"%d-%m-%y\t%H:%M:%S",
		"%d-%m-%Y\t%H:%M:%S",
		NULL,
	};

	l = newlocale(LC_ALL_MASK, "C", NULL);

	memset(&tm, 0, sizeof(tm));
	for (i=0; valid_formats[i] != NULL; i++) {
		ret = strptime_l(str, valid_formats[i], &tm, l);
		if (ret && *ret == '\0') {
			t->tm_mday = tm.tm_mday;
			t->tm_mon = tm.tm_mon;
			t->tm_year = tm.tm_year;
			t->tm_hour = tm.tm_hour;
			t->tm_min = tm.tm_min;
			t->tm_sec = tm.tm_sec;
			freelocale(l);
			return;
		}
	}

	freelocale(l);

	errx(EXIT_FAILURE, "Invalid date");
}


/*-
 * Parse time must be flexible, it handles the following formats:
 * nnnnnnnnnnn		UNIX timestamp (all numeric), 0 = now
 * 0xnnnnnnnn		UNIX timestamp in hexadecimal
 * 0nnnnnnnnn		UNIX timestamp in octal
 * 0			Given time
 * +nnnn[smhdwoy]	Given time + nnnn hours, mins, days, weeks, months or years
 * -nnnn[smhdwoy]	Given time - nnnn hours, mins, days, weeks, months or years
 * dd[ ./-]mmm[ ./-]yy	Date }
 * hh:mm:ss		Time } May be combined
 */

time_t
parse_date(time_t dt, char const * str)
{
	char           *p;
	int             i;
	long            val;
	struct tm      *T;

	if (dt == 0)
		dt = time(NULL);

	while (*str && isspace((unsigned char)*str))
		++str;

	if (numerics(str)) {
		dt = strtol(str, &p, 0);
	} else if (*str == '+' || *str == '-') {
		val = strtol(str, &p, 0);
		switch (*p) {
		case 'h':
		case 'H':	/* hours */
			dt += (val * 3600L);
			break;
		case '\0':
		case 'm':
		case 'M':	/* minutes */
			dt += (val * 60L);
			break;
		case 's':
		case 'S':	/* seconds */
			dt += val;
			break;
		case 'd':
		case 'D':	/* days */
			dt += (val * 86400L);
			break;
		case 'w':
		case 'W':	/* weeks */
			dt += (val * 604800L);
			break;
		case 'o':
		case 'O':	/* months */
			T = localtime(&dt);
			T->tm_mon += (int) val;
			i = T->tm_mday;
			goto fixday;
		case 'y':
		case 'Y':	/* years */
			T = localtime(&dt);
			T->tm_year += (int) val;
			i = T->tm_mday;
	fixday:
			dt = mktime(T);
			T = localtime(&dt);
			if (T->tm_mday != i) {
				T->tm_mday = 1;
				dt = mktime(T);
				dt -= (time_t) 86400L;
			}
		default:	/* unknown */
			break;	/* leave untouched */
		}
	} else {
		char           *q, tmp[64];

		/*
		 * Skip past any weekday prefix
		 */
		weekday(&str);
		strlcpy(tmp, str, sizeof(tmp));
		str = tmp;
		T = localtime(&dt);

		/*
		 * See if we can break off any timezone
		 */
		while ((q = strrchr(tmp, ' ')) != NULL) {
			if (strchr("(+-", q[1]) != NULL)
				*q = '\0';
			else {
				int             j = 1;

				while (q[j] && isupper((unsigned char)q[j]))
					++j;
				if (q[j] == '\0')
					*q = '\0';
				else
					break;
			}
		}

		parse_datesub(tmp, T);
		dt = mktime(T);
	}
	return dt;
}
