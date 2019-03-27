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
 * LC_NUMERIC database generation routines for localedef.
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
#include "lnumeric.h"

static struct lc_numeric_T numeric;

void
init_numeric(void)
{
	(void) memset(&numeric, 0, sizeof (numeric));
}

void
add_numeric_str(wchar_t *wcs)
{
	char *str;

	if ((str = to_mb_string(wcs)) == NULL) {
		INTERR;
		return;
	}
	free(wcs);

	switch (last_kw) {
	case T_DECIMAL_POINT:
		numeric.decimal_point = str;
		break;
	case T_THOUSANDS_SEP:
		numeric.thousands_sep = str;
		break;
	default:
		free(str);
		INTERR;
		break;
	}
}

void
reset_numeric_group(void)
{
	free((char *)numeric.grouping);
	numeric.grouping = NULL;
}

void
add_numeric_group(int n)
{
	char *s;

	if (numeric.grouping == NULL) {
		(void) asprintf(&s, "%d", n);
	} else {
		(void) asprintf(&s, "%s;%d", numeric.grouping, n);
	}
	if (s == NULL)
		fprintf(stderr, "out of memory");

	free((char *)numeric.grouping);
	numeric.grouping = s;
}

void
dump_numeric(void)
{
	FILE *f;

	if ((f = open_category()) == NULL) {
		return;
	}

	if ((putl_category(numeric.decimal_point, f) == EOF) ||
	    (putl_category(numeric.thousands_sep, f) == EOF) ||
	    (putl_category(numeric.grouping, f) == EOF)) {
		return;
	}
	close_category(f);
}
