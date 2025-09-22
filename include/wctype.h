/*	$OpenBSD: wctype.h,v 1.6 2017/09/05 03:16:13 schwarze Exp $	*/
/*	$NetBSD: wctype.h,v 1.5 2003/03/02 22:18:11 tshiozak Exp $	*/

/*-
 * Copyright (c)1999 Citrus Project,
 * All rights reserved.
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
 *	citrus Id: wctype.h,v 1.4 2000/12/21 01:50:21 itojun Exp
 */

#ifndef _WCTYPE_H_
#define	_WCTYPE_H_

#include <sys/cdefs.h>
#include <machine/_types.h>

#ifndef	_WINT_T_DEFINED_
#define	_WINT_T_DEFINED_
typedef	__wint_t    wint_t;
#endif

#ifndef	_WCTRANS_T_DEFINED_
#define	_WCTRANS_T_DEFINED_
typedef	__wctrans_t	wctrans_t;
#endif

#ifndef	_WCTYPE_T_DEFINED_
#define	_WCTYPE_T_DEFINED_
typedef	__wctype_t	wctype_t;
#endif

#ifndef WEOF
#define	WEOF	((wint_t)-1)
#endif

#if __POSIX_VISIBLE >= 200809
#ifndef	_LOCALE_T_DEFINED_
#define	_LOCALE_T_DEFINED_
typedef void	*locale_t;
#endif
#endif

__BEGIN_DECLS
int	iswalnum(wint_t);
int	iswalpha(wint_t);
int	iswblank(wint_t);
int	iswcntrl(wint_t);
int	iswdigit(wint_t);
int	iswgraph(wint_t);
int	iswlower(wint_t);
int	iswprint(wint_t);
int	iswpunct(wint_t);
int	iswspace(wint_t);
int	iswupper(wint_t);
int	iswxdigit(wint_t);
int	iswctype(wint_t, wctype_t);
wint_t	towctrans(wint_t, wctrans_t);
wint_t	towlower(wint_t);
wint_t	towupper(wint_t);
wctrans_t wctrans(const char *);
wctype_t wctype(const char *);

#if __POSIX_VISIBLE >= 200809
int	iswalnum_l(wint_t, locale_t);
int	iswalpha_l(wint_t, locale_t);
int	iswblank_l(wint_t, locale_t);
int	iswcntrl_l(wint_t, locale_t);
int	iswdigit_l(wint_t, locale_t);
int	iswgraph_l(wint_t, locale_t);
int	iswlower_l(wint_t, locale_t);
int	iswprint_l(wint_t, locale_t);
int	iswpunct_l(wint_t, locale_t);
int	iswspace_l(wint_t, locale_t);
int	iswupper_l(wint_t, locale_t);
int	iswxdigit_l(wint_t, locale_t);
int	iswctype_l(wint_t, wctype_t, locale_t);
wint_t	towctrans_l(wint_t, wctrans_t, locale_t);
wint_t	towlower_l(wint_t, locale_t);
wint_t	towupper_l(wint_t, locale_t);
wctrans_t wctrans_l(const char *, locale_t);
wctype_t wctype_l(const char *, locale_t);
#endif

__END_DECLS

#endif		/* _WCTYPE_H_ */
