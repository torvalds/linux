/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2010 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 1995 Alex Tatmanjants <alex@elvisti.kiev.ua>
 *		at Electronni Visti IA, Kiev, Ukraine.
 *			All rights reserved.
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 * Portions of this software were developed by David Chisnall
 * under sponsorship from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <wchar.h>
#include "collate.h"


/*
 * In order to properly handle multibyte locales, its easiest to just
 * convert to wide characters and then use wcscoll.  However if an
 * error occurs, we gracefully fall back to simple strcmp.  Caller
 * should check errno.
 */
int
strcoll_l(const char *s, const char *s2, locale_t locale)
{
	int ret;
	wchar_t *t1 = NULL, *t2 = NULL;
	wchar_t *w1 = NULL, *w2 = NULL;
	const char *cs1, *cs2;
	mbstate_t mbs1;
	mbstate_t mbs2;
	size_t sz1, sz2;

	memset(&mbs1, 0, sizeof (mbstate_t));
	memset(&mbs2, 0, sizeof (mbstate_t));

	/*
	 * The mbsrtowcs_l function can set the src pointer to null upon
	 * failure, so it should act on a copy to avoid:
	 *   - sending null pointer to strcmp
	 *   - having strcoll/strcoll_l change *s or *s2 to null
	 */
	cs1 = s;
	cs2 = s2;

	FIX_LOCALE(locale);
	struct xlocale_collate *table =
		(struct xlocale_collate*)locale->components[XLC_COLLATE];

	if (table->__collate_load_error)
		goto error;

	sz1 = strlen(s) + 1;
	sz2 = strlen(s2) + 1;

	/*
	 * Simple assumption: conversion to wide format is strictly
	 * reducing, i.e. a single byte (or multibyte character)
	 * cannot result in multiple wide characters.
	 */
	if ((t1 = malloc(sz1 * sizeof (wchar_t))) == NULL)
		goto error;
	w1 = t1;
	if ((t2 = malloc(sz2 * sizeof (wchar_t))) == NULL)
		goto error;
	w2 = t2;

	if ((mbsrtowcs_l(w1, &cs1, sz1, &mbs1, locale)) == (size_t)-1)
		goto error;

	if ((mbsrtowcs_l(w2, &cs2, sz2, &mbs2, locale)) == (size_t)-1)
		goto error;

	ret = wcscoll_l(w1, w2, locale);
	free(t1);
	free(t2);

	return (ret);

error:
	free(t1);
	free(t2);
	return (strcmp(s, s2));
}

int
strcoll(const char *s, const char *s2)
{
	return strcoll_l(s, s2, __get_locale());
}

