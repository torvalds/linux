/*-
 * Copyright 2010 Nexenta Systems, Inc.  All rights reserved.
 * Copyright 2015 John Marino <draco@marino.st>
 *
 * This source code is derived from the illumos localedef command, and
 * provided under BSD-style license terms by Nexenta Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * LC_TIME database generation routines for localedef.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include "localedef.h"
#include "parser.h"
#include "timelocal.h"

struct lc_time_T tm;

void
init_time(void)
{
	(void) memset(&tm, 0, sizeof (tm));
}

void
add_time_str(wchar_t *wcs)
{
	char	*str;

	if ((str = to_mb_string(wcs)) == NULL) {
		INTERR;
		return;
	}
	free(wcs);

	switch (last_kw) {
	case T_D_T_FMT:
		tm.c_fmt = str;
		break;
	case T_D_FMT:
		tm.x_fmt = str;
		break;
	case T_T_FMT:
		tm.X_fmt = str;
		break;
	case T_T_FMT_AMPM:
		tm.ampm_fmt = str;
		break;
	case T_DATE_FMT:
		/*
		 * This one is a Solaris extension, Too bad date just
		 * doesn't use %c, which would be simpler.
		 */
		tm.date_fmt = str;
		break;
	case T_ERA_D_FMT:
	case T_ERA_T_FMT:
	case T_ERA_D_T_FMT:
		/* Silently ignore it. */
		free(str);
		break;
	default:
		free(str);
		INTERR;
		break;
	}
}

static void
add_list(const char *ptr[], char *str, int limit)
{
	int	i;
	for (i = 0; i < limit; i++) {
		if (ptr[i] == NULL) {
			ptr[i] = str;
			return;
		}
	}
	fprintf(stderr,"too many list elements");
}

void
add_time_list(wchar_t *wcs)
{
	char *str;

	if ((str = to_mb_string(wcs)) == NULL) {
		INTERR;
		return;
	}
	free(wcs);

	switch (last_kw) {
	case T_ABMON:
		add_list(tm.mon, str, 12);
		break;
	case T_MON:
		add_list(tm.month, str, 12);
		break;
	case T_ABDAY:
		add_list(tm.wday, str, 7);
		break;
	case T_DAY:
		add_list(tm.weekday, str, 7);
		break;
	case T_AM_PM:
		if (tm.am == NULL) {
			tm.am = str;
		} else if (tm.pm == NULL) {
			tm.pm = str;
		} else {
			fprintf(stderr,"too many list elements");
			free(str);
		}
		break;
	case T_ALT_DIGITS:
	case T_ERA:
		free(str);
		break;
	default:
		free(str);
		INTERR;
		break;
	}
}

void
check_time_list(void)
{
	switch (last_kw) {
	case T_ABMON:
		if (tm.mon[11] != NULL)
			return;
		break;
	case T_MON:
		if (tm.month[11] != NULL)
			return;
		break;
	case T_ABDAY:
		if (tm.wday[6] != NULL)
			return;
		break;
	case T_DAY:
		if (tm.weekday[6] != NULL)
			return;
		break;
	case T_AM_PM:
		if (tm.pm != NULL)
			return;
		break;
	case T_ERA:
	case T_ALT_DIGITS:
		return;
	default:
		fprintf(stderr,"unknown list");
		break;
	}

	fprintf(stderr,"too few items in list (%d)", last_kw);
}

void
reset_time_list(void)
{
	int i;
	switch (last_kw) {
	case T_ABMON:
		for (i = 0; i < 12; i++) {
			free((char *)tm.mon[i]);
			tm.mon[i] = NULL;
		}
		break;
	case T_MON:
		for (i = 0; i < 12; i++) {
			free((char *)tm.month[i]);
			tm.month[i] = NULL;
		}
		break;
	case T_ABDAY:
		for (i = 0; i < 7; i++) {
			free((char *)tm.wday[i]);
			tm.wday[i] = NULL;
		}
		break;
	case T_DAY:
		for (i = 0; i < 7; i++) {
			free((char *)tm.weekday[i]);
			tm.weekday[i] = NULL;
		}
		break;
	case T_AM_PM:
		free((char *)tm.am);
		tm.am = NULL;
		free((char *)tm.pm);
		tm.pm = NULL;
		break;
	}
}

void
dump_time(void)
{
	FILE *f;
	int i;

	if ((f = open_category()) == NULL) {
		return;
	}

	for (i = 0; i < 12; i++) {
		if (putl_category(tm.mon[i], f) == EOF) {
			return;
		}
	}
	for (i = 0; i < 12; i++) {
		if (putl_category(tm.month[i], f) == EOF) {
			return;
		}
	}
	for (i = 0; i < 7; i++) {
		if (putl_category(tm.wday[i], f) == EOF) {
			return;
		}
	}
	for (i = 0; i < 7; i++) {
		if (putl_category(tm.weekday[i], f) == EOF) {
			return;
		}
	}

	/*
	 * NOTE: If date_fmt is not specified, then we'll default to
	 * using the %c for date.  This is reasonable for most
	 * locales, although for reasons that I don't understand
	 * Solaris historically has had a separate format for date.
	 */
	if ((putl_category(tm.X_fmt, f) == EOF) ||
	    (putl_category(tm.x_fmt, f) == EOF) ||
	    (putl_category(tm.c_fmt, f) == EOF) ||
	    (putl_category(tm.am, f) == EOF) ||
	    (putl_category(tm.pm, f) == EOF) ||
	    (putl_category(tm.date_fmt ? tm.date_fmt : tm.c_fmt, f) == EOF) ||
	    (putl_category(tm.ampm_fmt, f) == EOF)) {
		return;
	}
	close_category(f);
}
