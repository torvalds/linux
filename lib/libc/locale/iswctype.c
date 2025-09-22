/*	$OpenBSD: iswctype.c,v 1.9 2024/02/04 12:46:01 jca Exp $ */
/*	$NetBSD: iswctype.c,v 1.15 2005/02/09 21:35:46 kleink Exp $	*/

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

#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include "rune.h"
#include "runetype.h"
#include "rune_local.h"
#include "_wctrans_local.h"

static inline _RuneType __runetype_w(wint_t);
static inline int __isctype_w(wint_t, _RuneType);
static inline wint_t __toupper_w(wint_t);
static inline wint_t __tolower_w(wint_t);

static inline _RuneType
__runetype_w(wint_t c)
{
	_RuneLocale *rl = _CurrentRuneLocale();

	return (_RUNE_ISCACHED(c) ?
		rl->rl_runetype[c] : ___runetype_mb(c, rl));
}

static inline int
__isctype_w(wint_t c, _RuneType f)
{
	return (!!(__runetype_w(c) & f));
}

static inline wint_t
__toupper_w(wint_t c)
{
	return (_towctrans(c, _wctrans_upper(_CurrentRuneLocale())));
}

static inline wint_t
__tolower_w(wint_t c)
{
	return (_towctrans(c, _wctrans_lower(_CurrentRuneLocale())));
}

int
iswalnum(wint_t c)
{
	return (__isctype_w((c), _RUNETYPE_A|_RUNETYPE_D));
}

int
iswalpha(wint_t c)
{
	return (__isctype_w((c), _RUNETYPE_A));
}

int
iswblank(wint_t c)
{
	return (__isctype_w((c), _RUNETYPE_B));
}

int
iswcntrl(wint_t c)
{
	return (__isctype_w((c), _RUNETYPE_C));
}

int
iswdigit(wint_t c)
{
	return (__isctype_w((c), _RUNETYPE_D));
}

int
iswgraph(wint_t c)
{
	return (__isctype_w((c), _RUNETYPE_G));
}

int
iswlower(wint_t c)
{
	return (__isctype_w((c), _RUNETYPE_L));
}

int
iswprint(wint_t c)
{
	return (__isctype_w((c), _RUNETYPE_R));
}

int
iswpunct(wint_t c)
{
	return (__isctype_w((c), _RUNETYPE_P));
}

int
iswspace(wint_t c)
{
	return (__isctype_w((c), _RUNETYPE_S));
}
DEF_STRONG(iswspace);

int
iswupper(wint_t c)
{
	return (__isctype_w((c), _RUNETYPE_U));
}
DEF_STRONG(iswupper);

int
iswxdigit(wint_t c)
{
	return (__isctype_w((c), _RUNETYPE_X));
}

wint_t
towupper(wint_t c)
{
	return (__toupper_w(c));
}

wint_t
towlower(wint_t c)
{
	return (__tolower_w(c));
}
DEF_STRONG(towlower);

int
wcwidth(wchar_t c)
{
	if (__isctype_w((c), _RUNETYPE_R))
		return (((unsigned)__runetype_w(c) & _RUNETYPE_SWM) >>
		    _RUNETYPE_SWS);
	return -1;
}
DEF_WEAK(wcwidth);

wctrans_t
wctrans(const char *charclass)
{
	int i;
	_RuneLocale *rl = _CurrentRuneLocale();

	if (rl->rl_wctrans[_WCTRANS_INDEX_LOWER].te_name==NULL)
		_wctrans_init(rl);

	for (i=0; i<_WCTRANS_NINDEXES; i++)
		if (!strcmp(rl->rl_wctrans[i].te_name, charclass))
			return ((wctrans_t)&rl->rl_wctrans[i]);

	return ((wctrans_t)NULL);
}

wint_t
towctrans(wint_t c, wctrans_t desc)
{
	if (desc==NULL) {
		errno = EINVAL;
		return (c);
	}
	return (_towctrans(c, (_WCTransEntry *)desc));
}
DEF_STRONG(towctrans);

int
iswctype(wint_t c, wctype_t charclass)
{

	/*
	 * SUSv3: If charclass is 0, iswctype() shall return 0.
	 */
	if (charclass == (wctype_t)0) {
		return 0;
	}

	return (__isctype_w(c, ((_WCTypeEntry *)charclass)->te_mask));
}
