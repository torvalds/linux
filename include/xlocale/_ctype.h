/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by David Chisnall under sponsorship from
 * the FreeBSD Foundation.
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


#if	(defined(_XLOCALE_WCTYPES) && !defined(_XLOCALE_WCTYPE_H)) || \
	(!defined(_XLOCALE_WCTYPES) && !defined(_XLOCALE_CTYPE_H))

#ifdef _XLOCALE_WCTYPES
#define _XLOCALE_WCTYPE_H
#else
#define _XLOCALE_CTYPE_H
#endif

#ifndef _LOCALE_T_DEFINED
#define _LOCALE_T_DEFINED
typedef struct	_xlocale *locale_t;
#endif

#ifndef _XLOCALE_RUN_FUNCTIONS_DEFINED
#define _XLOCALE_RUN_FUNCTIONS_DEFINED 1
unsigned long	 ___runetype_l(__ct_rune_t, locale_t) __pure;
__ct_rune_t	 ___tolower_l(__ct_rune_t, locale_t) __pure;
__ct_rune_t	 ___toupper_l(__ct_rune_t, locale_t) __pure;
_RuneLocale	*__runes_for_locale(locale_t, int*);
#endif

#ifndef _XLOCALE_INLINE
#if defined(__GNUC__) && !defined(__GNUC_STDC_INLINE__)
/* GNU89 inline has nonstandard semantics. */
#define _XLOCALE_INLINE extern __inline
#else
/* Hack to work around people who define inline away */
#ifdef inline
#define _XLOCALE_INLINE static __inline
#else
/* Define with C++ / C99 compatible semantics */
#define _XLOCALE_INLINE inline
#endif
#endif
#endif /* _XLOCALE_INLINE */

#ifdef _XLOCALE_WCTYPES
_XLOCALE_INLINE int
__maskrune_l(__ct_rune_t __c, unsigned long __f, locale_t __loc);
_XLOCALE_INLINE int
__istype_l(__ct_rune_t __c, unsigned long __f, locale_t __loc);

_XLOCALE_INLINE int
__maskrune_l(__ct_rune_t __c, unsigned long __f, locale_t __loc)
{
	int __limit;
	_RuneLocale *runes = __runes_for_locale(__loc, &__limit);
	return ((__c < 0 || __c >= _CACHED_RUNES) ? ___runetype_l(__c, __loc) :
	        runes->__runetype[__c]) & __f;
}

_XLOCALE_INLINE int
__istype_l(__ct_rune_t __c, unsigned long __f, locale_t __loc)
{
	return (!!__maskrune_l(__c, __f, __loc));
}

#define XLOCALE_ISCTYPE(fname, cat) \
		_XLOCALE_INLINE int isw##fname##_l(int, locale_t);\
		_XLOCALE_INLINE int isw##fname##_l(int __c, locale_t __l)\
		{ return __istype_l(__c, cat, __l); }
#else
_XLOCALE_INLINE int
__sbmaskrune_l(__ct_rune_t __c, unsigned long __f, locale_t __loc);
_XLOCALE_INLINE int
__sbistype_l(__ct_rune_t __c, unsigned long __f, locale_t __loc);

_XLOCALE_INLINE int
__sbmaskrune_l(__ct_rune_t __c, unsigned long __f, locale_t __loc)
{
	int __limit;
	_RuneLocale *runes = __runes_for_locale(__loc, &__limit);
	return (__c < 0 || __c >= __limit) ? 0 :
	       runes->__runetype[__c] & __f;
}

_XLOCALE_INLINE int
__sbistype_l(__ct_rune_t __c, unsigned long __f, locale_t __loc)
{
	return (!!__sbmaskrune_l(__c, __f, __loc));
}

#define XLOCALE_ISCTYPE(__fname, __cat) \
		_XLOCALE_INLINE int is##__fname##_l(int, locale_t); \
		_XLOCALE_INLINE int is##__fname##_l(int __c, locale_t __l)\
		{ return __sbistype_l(__c, __cat, __l); }
#endif

XLOCALE_ISCTYPE(alnum, _CTYPE_A|_CTYPE_D|_CTYPE_N)
XLOCALE_ISCTYPE(alpha, _CTYPE_A)
XLOCALE_ISCTYPE(blank, _CTYPE_B)
XLOCALE_ISCTYPE(cntrl, _CTYPE_C)
XLOCALE_ISCTYPE(digit, _CTYPE_D)
XLOCALE_ISCTYPE(graph, _CTYPE_G)
XLOCALE_ISCTYPE(hexnumber, _CTYPE_X)
XLOCALE_ISCTYPE(ideogram, _CTYPE_I)
XLOCALE_ISCTYPE(lower, _CTYPE_L)
XLOCALE_ISCTYPE(number, _CTYPE_D|_CTYPE_N)
XLOCALE_ISCTYPE(phonogram, _CTYPE_Q)
XLOCALE_ISCTYPE(print, _CTYPE_R)
XLOCALE_ISCTYPE(punct, _CTYPE_P)
XLOCALE_ISCTYPE(rune, 0xFFFFFF00L)
XLOCALE_ISCTYPE(space, _CTYPE_S)
XLOCALE_ISCTYPE(special, _CTYPE_T)
XLOCALE_ISCTYPE(upper, _CTYPE_U)
XLOCALE_ISCTYPE(xdigit, _CTYPE_X)
#undef XLOCALE_ISCTYPE

#ifdef _XLOCALE_WCTYPES
_XLOCALE_INLINE int towlower_l(int, locale_t);
_XLOCALE_INLINE int __wcwidth_l(__ct_rune_t, locale_t);
_XLOCALE_INLINE int towupper_l(int, locale_t);

_XLOCALE_INLINE int towlower_l(int __c, locale_t __l)
{
	int __limit;
	_RuneLocale *__runes = __runes_for_locale(__l, &__limit);
	return (__c < 0 || __c >= _CACHED_RUNES) ? ___tolower_l(__c, __l) :
	       __runes->__maplower[__c];
}
_XLOCALE_INLINE int towupper_l(int __c, locale_t __l)
{
	int __limit;
	_RuneLocale *__runes = __runes_for_locale(__l, &__limit);
	return (__c < 0 || __c >= _CACHED_RUNES) ? ___toupper_l(__c, __l) :
	       __runes->__mapupper[__c];
}
_XLOCALE_INLINE int
__wcwidth_l(__ct_rune_t _c, locale_t __l)
{
	unsigned int _x;

	if (_c == 0)
		return (0);
	_x = (unsigned int)__maskrune_l(_c, _CTYPE_SWM|_CTYPE_R, __l);
	if ((_x & _CTYPE_SWM) != 0)
		return ((_x & _CTYPE_SWM) >> _CTYPE_SWS);
	return ((_x & _CTYPE_R) != 0 ? 1 : -1);
}
int iswctype_l(wint_t __wc, wctype_t __charclass, locale_t __l);
wctype_t wctype_l(const char *property, locale_t __l);
wint_t towctrans_l(wint_t __wc, wctrans_t desc, locale_t __l);
wint_t nextwctype_l(wint_t __wc, wctype_t wct, locale_t __l);
wctrans_t wctrans_l(const char *__charclass, locale_t __l);
#undef _XLOCALE_WCTYPES
#else
_XLOCALE_INLINE int digittoint_l(int, locale_t);
_XLOCALE_INLINE int tolower_l(int, locale_t);
_XLOCALE_INLINE int toupper_l(int, locale_t);

_XLOCALE_INLINE int digittoint_l(int __c, locale_t __l)
{ return __sbmaskrune_l((__c), 0xFF, __l); }

_XLOCALE_INLINE int tolower_l(int __c, locale_t __l)
{
	int __limit;
	_RuneLocale *__runes = __runes_for_locale(__l, &__limit);
	return (__c < 0 || __c >= __limit) ? __c :
	       __runes->__maplower[__c];
}
_XLOCALE_INLINE int toupper_l(int __c, locale_t __l)
{
	int __limit;
	_RuneLocale *__runes = __runes_for_locale(__l, &__limit);
	return (__c < 0 || __c >= __limit) ? __c :
	       __runes->__mapupper[__c];
}
#endif
#endif /* (defined(_XLOCALE_WCTYPES) && !defined(_XLOCALE_WCTYPE_H)) || \
	(!defined(_XLOCALE_WCTYPES) && !defined(_XLOCALE_CTYPE_H)) */
