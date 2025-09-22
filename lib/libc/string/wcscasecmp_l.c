/*	$OpenBSD: wcscasecmp_l.c,v 1.1 2017/09/05 03:16:14 schwarze Exp $ */

/*
 * Copyright (c) 2011 Marc Espie
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
 * THIS SOFTWARE IS PROVIDED BY THE OPENBSD PROJECT AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OPENBSD
 * PROJECT OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <wchar.h>
#include <wctype.h>
#include "locale/runetype.h"

int
wcscasecmp_l(const wchar_t *s1, const wchar_t *s2, locale_t locale)
{
	wchar_t l1, l2;

	while ((l1 = towlower_l(*s1++, locale)) ==
	    (l2 = towlower_l(*s2++, locale))) {
		if (l1 == 0)
			return (0);
	}
	/* XXX assumes wchar_t = int */
	return ((rune_t)l1 - (rune_t)l2);
}

int
wcsncasecmp_l(const wchar_t *s1, const wchar_t *s2, size_t n, locale_t locale)
{
	wchar_t l1, l2;

	if (n == 0)
		return (0);
	do {
		if (((l1 = towlower_l(*s1++, locale))) !=
		    (l2 = towlower_l(*s2++, locale))) {
			/* XXX assumes wchar_t = int */
			return ((rune_t)l1 - (rune_t)l2);
		}
		if (l1 == 0)
			break;
	} while (--n != 0);
	return (0);
}
