/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2013 Garrett D'Amore <garrett@damore.org>
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2004 Tim J. Robbins.
 * All rights reserved.
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

#ifndef _MBLOCAL_H_
#define	_MBLOCAL_H_

#include <runetype.h>
#include "xlocale_private.h"

#define	SS2	0x008e
#define SS3	0x008f

/*
 * Conversion function pointers for current encoding.
 */
struct xlocale_ctype {
	struct xlocale_component header;
	_RuneLocale *runes;
	size_t (*__mbrtowc)(wchar_t * __restrict, const char * __restrict,
		size_t, mbstate_t * __restrict);
	int (*__mbsinit)(const mbstate_t *);
	size_t (*__mbsnrtowcs)(wchar_t * __restrict, const char ** __restrict,
		size_t, size_t, mbstate_t * __restrict);
	size_t (*__wcrtomb)(char * __restrict, wchar_t, mbstate_t * __restrict);
	size_t (*__wcsnrtombs)(char * __restrict, const wchar_t ** __restrict,
		size_t, size_t, mbstate_t * __restrict);
	int __mb_cur_max;
	int __mb_sb_limit;
	/** Persistent state used by mblen() calls. */
	__mbstate_t mblen;
	/** Persistent state used by mbrlen() calls. */
	__mbstate_t mbrlen;
	/** Persistent state used by mbrtoc16() calls. */
	__mbstate_t mbrtoc16;
	/** Persistent state used by mbrtoc32() calls. */
	__mbstate_t mbrtoc32;
	/** Persistent state used by mbrtowc() calls. */
	__mbstate_t mbrtowc;
	/** Persistent state used by mbsnrtowcs() calls. */
	__mbstate_t mbsnrtowcs;
	/** Persistent state used by mbsrtowcs() calls. */
	__mbstate_t mbsrtowcs;
	/** Persistent state used by mbtowc() calls. */
	__mbstate_t mbtowc;
	/** Persistent state used by c16rtomb() calls. */
	__mbstate_t c16rtomb;
	/** Persistent state used by c32rtomb() calls. */
	__mbstate_t c32rtomb;
	/** Persistent state used by wcrtomb() calls. */
	__mbstate_t wcrtomb;
	/** Persistent state used by wcsnrtombs() calls. */
	__mbstate_t wcsnrtombs;
	/** Persistent state used by wcsrtombs() calls. */
	__mbstate_t wcsrtombs;
	/** Persistent state used by wctomb() calls. */
	__mbstate_t wctomb;
};
#define XLOCALE_CTYPE(x) ((struct xlocale_ctype*)(x)->components[XLC_CTYPE])
extern struct xlocale_ctype __xlocale_global_ctype;

/*
 * Rune initialization function prototypes.
 */
int	_none_init(struct xlocale_ctype *, _RuneLocale *) __hidden;
int	_UTF8_init(struct xlocale_ctype *, _RuneLocale *) __hidden;
int	_EUC_CN_init(struct xlocale_ctype *, _RuneLocale *) __hidden;
int	_EUC_JP_init(struct xlocale_ctype *, _RuneLocale *) __hidden;
int	_EUC_KR_init(struct xlocale_ctype *, _RuneLocale *) __hidden;
int	_EUC_TW_init(struct xlocale_ctype *, _RuneLocale *) __hidden;
int	_GB18030_init(struct xlocale_ctype *, _RuneLocale *) __hidden;
int	_GB2312_init(struct xlocale_ctype *, _RuneLocale *) __hidden;
int	_GBK_init(struct xlocale_ctype *, _RuneLocale *) __hidden;
int	_BIG5_init(struct xlocale_ctype *, _RuneLocale *) __hidden;
int	_MSKanji_init(struct xlocale_ctype *, _RuneLocale *) __hidden;
int	_ascii_init(struct xlocale_ctype *, _RuneLocale *) __hidden;

typedef size_t (*mbrtowc_pfn_t)(wchar_t * __restrict,
    const char * __restrict, size_t, mbstate_t * __restrict);
typedef size_t (*wcrtomb_pfn_t)(char * __restrict, wchar_t,
    mbstate_t * __restrict);
size_t __mbsnrtowcs_std(wchar_t * __restrict, const char ** __restrict,
    size_t, size_t, mbstate_t * __restrict, mbrtowc_pfn_t);
size_t __wcsnrtombs_std(char * __restrict, const wchar_t ** __restrict,
    size_t, size_t, mbstate_t * __restrict, wcrtomb_pfn_t);

#endif	/* _MBLOCAL_H_ */
