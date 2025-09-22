/*	$OpenBSD: wcscasecmp.c,v 1.3 2015/09/12 16:23:14 guenther Exp $ */

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
wcscasecmp(const wchar_t *s1, const wchar_t *s2)
{
	wchar_t l1, l2;

	while ((l1 = towlower(*s1++)) == (l2 = towlower(*s2++))) {
		if (l1 == 0)
			return (0);
	}
	/* XXX assumes wchar_t = int */
	return ((rune_t)l1 - (rune_t)l2);
}
DEF_WEAK(wcscasecmp);

int
wcsncasecmp(const wchar_t *s1, const wchar_t *s2, size_t n)
{
	wchar_t l1, l2;

	if (n == 0)
		return (0);
	do {
		if (((l1 = towlower(*s1++))) != (l2 = towlower(*s2++))) {
			/* XXX assumes wchar_t = int */
			return ((rune_t)l1 - (rune_t)l2);
		}
		if (l1 == 0)
			break;
	} while (--n != 0);
	return (0);
}
DEF_WEAK(wcsncasecmp);
