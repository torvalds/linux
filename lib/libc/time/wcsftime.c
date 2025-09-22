/*	$OpenBSD: wcsftime.c,v 1.7 2019/05/12 12:49:52 schwarze Exp $ */
/*
** Based on the UCB version with the ID appearing below.
** This is ANSIish only when "multibyte character == plain character".
**
** Copyright (c) 1989, 1993
**	The Regents of the University of California.  All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. Neither the name of the University nor the names of its contributors
**    may be used to endorse or promote products derived from this software
**    without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
*/

#include <fcntl.h>
#include <locale.h>
#include <wchar.h>

#include "private.h"
#include "tzfile.h"

struct lc_time_T {
	const wchar_t *	mon[MONSPERYEAR];
	const wchar_t *	month[MONSPERYEAR];
	const wchar_t *	wday[DAYSPERWEEK];
	const wchar_t *	weekday[DAYSPERWEEK];
	const wchar_t *	X_fmt;
	const wchar_t *	x_fmt;
	const wchar_t *	c_fmt;
	const wchar_t *	am;
	const wchar_t *	pm;
	const wchar_t *	date_fmt;
};

#define Locale	(&C_time_locale)

static const struct lc_time_T	C_time_locale = {
	{
		L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
		L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"
	}, {
		L"January", L"February", L"March", L"April", L"May", L"June",
		L"July", L"August", L"September", L"October", L"November", 
		L"December"
	}, {
		L"Sun", L"Mon", L"Tue", L"Wed",
		L"Thu", L"Fri", L"Sat"
	}, {
		L"Sunday", L"Monday", L"Tuesday", L"Wednesday",
		L"Thursday", L"Friday", L"Saturday"
	},

	/* X_fmt */
	L"%H:%M:%S",

	/*
	** x_fmt
	** C99 requires this format.
	** Using just numbers (as here) makes Quakers happier;
	** it's also compatible with SVR4.
	*/
	L"%m/%d/%y",

	/*
	** c_fmt
	** C99 requires this format.
	** Previously this code used "%D %X", but we now conform to C99.
	** Note that
	**	"%a %b %d %H:%M:%S %Y"
	** is used by Solaris 2.3.
	*/
	L"%a %b %e %T %Y",

	/* am */
	L"AM",

	/* pm */
	L"PM",

	/* date_fmt */
	L"%a %b %e %H:%M:%S %Z %Y"
};

#define UNKNOWN L"?"
static wchar_t *	_add(const wchar_t *, wchar_t *, const wchar_t *);
static wchar_t *	_sadd(const char *, wchar_t *, const wchar_t *);
static wchar_t *	_conv(int, const wchar_t *, wchar_t *, const wchar_t *);
static wchar_t *	_fmt(const wchar_t *, const struct tm *, wchar_t *, const wchar_t *,
			int *);
static wchar_t *	_yconv(int, int, int, int, wchar_t *, const wchar_t *);

extern char *	tzname[];

#define IN_NONE	0
#define IN_SOME	1
#define IN_THIS	2
#define IN_ALL	3

size_t
wcsftime(wchar_t *__restrict s, size_t maxsize, 
    const wchar_t *__restrict format, const struct tm *__restrict t)
{
	wchar_t *p;
	int	warn;

	tzset();
	warn = IN_NONE;
	p = _fmt(((format == NULL) ? L"%c" : format), t, s, s + maxsize, &warn);
	if (p == s + maxsize) {
		if (maxsize > 0)
			s[maxsize - 1] = '\0';
		return 0;
	}
	*p = L'\0';
	return p - s;
}

static wchar_t *
_fmt(const wchar_t *format, const struct tm *t, wchar_t *pt, 
    const wchar_t *ptlim, int *warnp)
{
	for ( ; *format; ++format) {
		if (*format != L'%') {
			if (pt == ptlim)
				break;
			*pt++ = *format;
			continue;
		}
label:
		switch (*++format) {
		case '\0':
			--format;
			break;
		case 'A':
			pt = _add((t->tm_wday < 0 ||
				t->tm_wday >= DAYSPERWEEK) ?
				UNKNOWN : Locale->weekday[t->tm_wday],
				pt, ptlim);
			continue;
		case 'a':
			pt = _add((t->tm_wday < 0 ||
				t->tm_wday >= DAYSPERWEEK) ?
				UNKNOWN : Locale->wday[t->tm_wday],
				pt, ptlim);
			continue;
		case 'B':
			pt = _add((t->tm_mon < 0 ||
				t->tm_mon >= MONSPERYEAR) ?
				UNKNOWN : Locale->month[t->tm_mon],
				pt, ptlim);
			continue;
		case 'b':
		case 'h':
			pt = _add((t->tm_mon < 0 ||
				t->tm_mon >= MONSPERYEAR) ?
				UNKNOWN : Locale->mon[t->tm_mon],
				pt, ptlim);
			continue;
		case 'C':
			/*
			** %C used to do a...
			**	_fmt("%a %b %e %X %Y", t);
			** ...whereas now POSIX 1003.2 calls for
			** something completely different.
			** (ado, 1993-05-24)
			*/
			pt = _yconv(t->tm_year, TM_YEAR_BASE, 1, 0,
				pt, ptlim);
			continue;
		case 'c':
			{
			int warn2 = IN_SOME;

			pt = _fmt(Locale->c_fmt, t, pt, ptlim, &warn2);
			if (warn2 == IN_ALL)
				warn2 = IN_THIS;
			if (warn2 > *warnp)
				*warnp = warn2;
			}
			continue;
		case 'D':
			pt = _fmt(L"%m/%d/%y", t, pt, ptlim, warnp);
			continue;
		case 'd':
			pt = _conv(t->tm_mday, L"%02d", pt, ptlim);
			continue;
		case 'E':
		case 'O':
			/*
			** C99 locale modifiers.
			** The sequences
			**	%Ec %EC %Ex %EX %Ey %EY
			**	%Od %oe %OH %OI %Om %OM
			**	%OS %Ou %OU %OV %Ow %OW %Oy
			** are supposed to provide alternate
			** representations.
			*/
			goto label;
		case 'e':
			pt = _conv(t->tm_mday, L"%2d", pt, ptlim);
			continue;
		case 'F':
			pt = _fmt(L"%Y-%m-%d", t, pt, ptlim, warnp);
			continue;
		case 'H':
			pt = _conv(t->tm_hour, L"%02d", pt, ptlim);
			continue;
		case 'I':
			pt = _conv((t->tm_hour % 12) ?
				(t->tm_hour % 12) : 12,
				L"%02d", pt, ptlim);
			continue;
		case 'j':
			pt = _conv(t->tm_yday + 1, L"%03d", pt, ptlim);
			continue;
		case 'k':
			/*
			** This used to be...
			**	_conv(t->tm_hour % 12 ?
			**		t->tm_hour % 12 : 12, 2, ' ');
			** ...and has been changed to the below to
			** match SunOS 4.1.1 and Arnold Robbins'
			** strftime version 3.0. That is, "%k" and
			** "%l" have been swapped.
			** (ado, 1993-05-24)
			*/
			pt = _conv(t->tm_hour, L"%2d", pt, ptlim);
			continue;
		case 'l':
			/*
			** This used to be...
			**	_conv(t->tm_hour, 2, ' ');
			** ...and has been changed to the below to
			** match SunOS 4.1.1 and Arnold Robbin's
			** strftime version 3.0. That is, "%k" and
			** "%l" have been swapped.
			** (ado, 1993-05-24)
			*/
			pt = _conv((t->tm_hour % 12) ?
				(t->tm_hour % 12) : 12,
				L"%2d", pt, ptlim);
			continue;
		case 'M':
			pt = _conv(t->tm_min, L"%02d", pt, ptlim);
			continue;
		case 'm':
			pt = _conv(t->tm_mon + 1, L"%02d", pt, ptlim);
			continue;
		case 'n':
			pt = _add(L"\n", pt, ptlim);
			continue;
		case 'p':
			pt = _add((t->tm_hour >= (HOURSPERDAY / 2)) ?
				Locale->pm :
				Locale->am,
				pt, ptlim);
			continue;
		case 'R':
			pt = _fmt(L"%H:%M", t, pt, ptlim, warnp);
			continue;
		case 'r':
			pt = _fmt(L"%I:%M:%S %p", t, pt, ptlim, warnp);
			continue;
		case 'S':
			pt = _conv(t->tm_sec, L"%02d", pt, ptlim);
			continue;
		case 's':
			{
				struct tm	tm;
				wchar_t		buf[INT_STRLEN_MAXIMUM(
							time_t) + 1];
				time_t		mkt;

				tm = *t;
				mkt = mktime(&tm);
				(void) swprintf(buf, 
				    sizeof buf/sizeof buf[0],
				    L"%ld", (long) mkt);
				pt = _add(buf, pt, ptlim);
			}
			continue;
		case 'T':
			pt = _fmt(L"%H:%M:%S", t, pt, ptlim, warnp);
			continue;
		case 't':
			pt = _add(L"\t", pt, ptlim);
			continue;
		case 'U':
			pt = _conv((t->tm_yday + DAYSPERWEEK -
				t->tm_wday) / DAYSPERWEEK,
				L"%02d", pt, ptlim);
			continue;
		case 'u':
			/*
			** From Arnold Robbins' strftime version 3.0:
			** "ISO 8601: Weekday as a decimal number
			** [1 (Monday) - 7]"
			** (ado, 1993-05-24)
			*/
			pt = _conv((t->tm_wday == 0) ?
				DAYSPERWEEK : t->tm_wday,
				L"%d", pt, ptlim);
			continue;
		case 'V':	/* ISO 8601 week number */
		case 'G':	/* ISO 8601 year (four digits) */
		case 'g':	/* ISO 8601 year (two digits) */
/*
** From Arnold Robbins' strftime version 3.0: "the week number of the
** year (the first Monday as the first day of week 1) as a decimal number
** (01-53)."
** (ado, 1993-05-24)
**
** From "http://www.ft.uni-erlangen.de/~mskuhn/iso-time.html" by Markus Kuhn:
** "Week 01 of a year is per definition the first week which has the
** Thursday in this year, which is equivalent to the week which contains
** the fourth day of January. In other words, the first week of a new year
** is the week which has the majority of its days in the new year. Week 01
** might also contain days from the previous year and the week before week
** 01 of a year is the last week (52 or 53) of the previous year even if
** it contains days from the new year. A week starts with Monday (day 1)
** and ends with Sunday (day 7). For example, the first week of the year
** 1997 lasts from 1996-12-30 to 1997-01-05..."
** (ado, 1996-01-02)
*/
			{
			int	year;
			int	base;
			int	yday;
			int	wday;
			int	w;

			year = t->tm_year;
			base = TM_YEAR_BASE;
			yday = t->tm_yday;
			wday = t->tm_wday;
			for ( ; ; ) {
				int	len;
				int	bot;
				int	top;

				len = isleap_sum(year, base) ?
					DAYSPERLYEAR :
					DAYSPERNYEAR;
				/*
				** What yday (-3 ... 3) does the ISO year 
				** begin on?
				*/
				bot = ((yday + 11 - wday) % DAYSPERWEEK) - 3;
				/*
				** What yday does the NEXT ISO year begin on?
				*/
				top = bot - (len % DAYSPERWEEK);
				if (top < -3)
					top += DAYSPERWEEK;
				top += len;
				if (yday >= top) {
					++base;
					w = 1;
					break;
				}
				if (yday >= bot) {
					w = 1 + ((yday - bot) / DAYSPERWEEK);
					break;
				}
				--base;
				yday += isleap_sum(year, base) ?
					DAYSPERLYEAR :
					DAYSPERNYEAR;
			}
			if ((w == 52 && t->tm_mon == TM_JANUARY) ||
				(w == 1 && t->tm_mon == TM_DECEMBER))
					w = 53;
			if (*format == 'V')
				pt = _conv(w, L"%02d", pt, ptlim);
			else if (*format == 'g') {
				*warnp = IN_ALL;
				pt = _yconv(year, base, 0, 1, pt, ptlim);
			} else	
				pt = _yconv(year, base, 1, 1, pt, ptlim);
			}
			continue;
		case 'v':
			/*
			** From Arnold Robbins' strftime version 3.0:
			** "date as dd-bbb-YYYY"
			** (ado, 1993-05-24)
			*/
			pt = _fmt(L"%e-%b-%Y", t, pt, ptlim, warnp);
			continue;
		case 'W':
			pt = _conv((t->tm_yday + DAYSPERWEEK -
				(t->tm_wday ?
				(t->tm_wday - 1) :
				(DAYSPERWEEK - 1))) / DAYSPERWEEK,
				L"%02d", pt, ptlim);
			continue;
		case 'w':
			pt = _conv(t->tm_wday, L"%d", pt, ptlim);
			continue;
		case 'X':
			pt = _fmt(Locale->X_fmt, t, pt, ptlim, warnp);
			continue;
		case 'x':
			{
			int	warn2 = IN_SOME;

			pt = _fmt(Locale->x_fmt, t, pt, ptlim, &warn2);
			if (warn2 == IN_ALL)
				warn2 = IN_THIS;
			if (warn2 > *warnp)
				*warnp = warn2;
			}
			continue;
		case 'y':
			*warnp = IN_ALL;
			pt = _yconv(t->tm_year, TM_YEAR_BASE, 0, 1, pt, ptlim);
			continue;
		case 'Y':
			pt = _yconv(t->tm_year, TM_YEAR_BASE, 1, 1, pt, ptlim);
			continue;
		case 'Z':
			if (t->tm_zone != NULL)
				pt = _sadd(t->tm_zone, pt, ptlim);
			else
				if (t->tm_isdst >= 0)
					pt = _sadd(tzname[t->tm_isdst != 0], 
					    pt, ptlim);
			/*
			** C99 says that %Z must be replaced by the
			** empty string if the time zone is not
			** determinable.
			*/
			continue;
		case 'z':
			{
			int		diff;
			wchar_t const *	sign;

			if (t->tm_isdst < 0)
				continue;
			diff = t->tm_gmtoff;
			if (diff < 0) {
				sign = L"-";
				diff = -diff;
			} else	
				sign = L"+";
			pt = _add(sign, pt, ptlim);
			diff /= SECSPERMIN;
			diff = (diff / MINSPERHOUR) * 100 +
				(diff % MINSPERHOUR);
			pt = _conv(diff, L"%04d", pt, ptlim);
			}
			continue;
		case '+':
			pt = _fmt(Locale->date_fmt, t, pt, ptlim, warnp);
			continue;
		case '%':
		/*
		** X311J/88-090 (4.12.3.5): if conversion wchar_t is
		** undefined, behavior is undefined. Print out the
		** character itself as printf(3) also does.
		*/
		default:
			if (pt != ptlim)
				*pt++ = *format;
			break;
		}
	}
	return pt;
}

static wchar_t *
_conv(int n, const wchar_t *format, wchar_t *pt, const wchar_t *ptlim)
{
	wchar_t	buf[INT_STRLEN_MAXIMUM(int) + 1];

	(void) swprintf(buf, sizeof buf/sizeof buf[0], format, n);
	return _add(buf, pt, ptlim);
}

static wchar_t *
_add(const wchar_t *str, wchar_t *pt, const wchar_t *ptlim)
{
	while (pt < ptlim && (*pt = *str++) != L'\0')
		++pt;
	return pt;
}

static wchar_t *
_sadd(const char *str, wchar_t *pt, const wchar_t *ptlim)
{
	while (pt < ptlim && (*pt = btowc(*str++)) != L'\0')
		++pt;
	return pt;
}
/*
** POSIX and the C Standard are unclear or inconsistent about
** what %C and %y do if the year is negative or exceeds 9999.
** Use the convention that %C concatenated with %y yields the
** same output as %Y, and that %Y contains at least 4 bytes,
** with more only if necessary.
*/

static wchar_t *
_yconv(int a, int b, int convert_top, int convert_yy, wchar_t *pt, 
    const wchar_t *ptlim)
{
	int	lead;
	int	trail;

#define DIVISOR	100
	trail = a % DIVISOR + b % DIVISOR;
	lead = a / DIVISOR + b / DIVISOR + trail / DIVISOR;
	trail %= DIVISOR;
	if (trail < 0 && lead > 0) {
		trail += DIVISOR;
		--lead;
	} else if (lead < 0 && trail > 0) {
		trail -= DIVISOR;
		++lead;
	}
	if (convert_top) {
		if (lead == 0 && trail < 0)
			pt = _add(L"-0", pt, ptlim);
		else	pt = _conv(lead, L"%02d", pt, ptlim);
	}
	if (convert_yy)
		pt = _conv(((trail < 0) ? -trail : trail), L"%02d", pt, ptlim);
	return pt;
}

