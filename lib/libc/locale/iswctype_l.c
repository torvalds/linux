/*	$OpenBSD: iswctype_l.c,v 1.3 2024/02/04 12:46:01 jca Exp $ */
/*	$NetBSD: iswctype.c,v 1.15 2005/02/09 21:35:46 kleink Exp $ */

/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "rune.h"
#include "runetype.h"
#include "rune_local.h"
#include "_wctrans_local.h"

static _RuneLocale	*__runelocale(locale_t);
static int		 __isctype_wl(wint_t, _RuneType, locale_t);

/*
 * For all these functions, POSIX says that behaviour is undefined
 * for LC_GLOBAL_LOCALE and for invalid locale arguments.
 * The choice made here is to use the C locale in that case.
 */
static _RuneLocale *
__runelocale(locale_t locale)
{
	_RuneLocale	*rl;

	rl = NULL;
	if (locale == _LOCALE_UTF8)
		rl = _Utf8RuneLocale;
	if (rl == NULL)
		rl = &_DefaultRuneLocale;
	return rl;
}

static int
__isctype_wl(wint_t c, _RuneType f, locale_t locale)
{
	_RuneLocale	*rl;
	_RuneType	 rt;

	rl = __runelocale(locale);
	rt = _RUNE_ISCACHED(c) ? rl->rl_runetype[c] : ___runetype_mb(c, rl);
	return (rt & f) != 0;
}

int
iswalnum_l(wint_t c, locale_t locale)
{
	return __isctype_wl(c, _RUNETYPE_A|_RUNETYPE_D, locale);
}

int
iswalpha_l(wint_t c, locale_t locale)
{
	return __isctype_wl(c, _RUNETYPE_A, locale);
}

int
iswblank_l(wint_t c, locale_t locale)
{
	return __isctype_wl(c, _RUNETYPE_B, locale);
}

int
iswcntrl_l(wint_t c, locale_t locale)
{
	return __isctype_wl(c, _RUNETYPE_C, locale);
}

int
iswdigit_l(wint_t c, locale_t locale)
{
	return __isctype_wl(c, _RUNETYPE_D, locale);
}

int
iswgraph_l(wint_t c, locale_t locale)
{
	return __isctype_wl(c, _RUNETYPE_G, locale);
}

int
iswlower_l(wint_t c, locale_t locale)
{
	return __isctype_wl(c, _RUNETYPE_L, locale);
}

int
iswprint_l(wint_t c, locale_t locale)
{
	return __isctype_wl(c, _RUNETYPE_R, locale);
}

int
iswpunct_l(wint_t c, locale_t locale)
{
	return __isctype_wl(c, _RUNETYPE_P, locale);
}

int
iswspace_l(wint_t c, locale_t locale)
{
	return __isctype_wl(c, _RUNETYPE_S, locale);
}

int
iswupper_l(wint_t c, locale_t locale)
{
	return __isctype_wl(c, _RUNETYPE_U, locale);
}

int
iswxdigit_l(wint_t c, locale_t locale)
{
	return __isctype_wl(c, _RUNETYPE_X, locale);
}

wint_t
towupper_l(wint_t c, locale_t locale)
{
	return _towctrans(c, _wctrans_upper(__runelocale(locale)));
}

wint_t
towlower_l(wint_t c, locale_t locale)
{
	return _towctrans(c, _wctrans_lower(__runelocale(locale)));
}
DEF_WEAK(towlower_l);

wctrans_t
wctrans_l(const char *charclass, locale_t locale)
{
	_RuneLocale	*rl;
	int		 i;

	rl = __runelocale(locale);
	if (rl->rl_wctrans[_WCTRANS_INDEX_LOWER].te_name == NULL)
		_wctrans_init(rl);

	for (i = 0; i < _WCTRANS_NINDEXES; i++)
		if (strcmp(rl->rl_wctrans[i].te_name, charclass) == 0)
			return &rl->rl_wctrans[i];

	return NULL;
}

/*
 * POSIX says that the behaviour is unspecified if the LC_CTYPE in
 * the locale argument does not match what was used to get desc.
 * The choice made here is to simply ignore the locale argument
 * and rely on the desc argument only.
 */
wint_t
towctrans_l(wint_t c, wctrans_t desc,
    locale_t locale __attribute__((__unused__)))
{
	return towctrans(c, desc);
}

int
iswctype_l(wint_t c, wctype_t charclass, locale_t locale)
{
	if (charclass == (wctype_t)0)
		return 0;  /* Required by SUSv3. */

	return __isctype_wl(c, ((_WCTypeEntry *)charclass)->te_mask, locale);
}
