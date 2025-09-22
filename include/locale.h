/*	$OpenBSD: locale.h,v 1.11 2017/09/05 03:16:13 schwarze Exp $	*/
/*	$NetBSD: locale.h,v 1.6 1994/10/26 00:56:02 cgd Exp $	*/

/*
 * Copyright (c) 1991 The Regents of the University of California.
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
 *
 *	@(#)locale.h	5.2 (Berkeley) 2/24/91
 */

#ifndef _LOCALE_H_
#define _LOCALE_H_

#include <sys/_null.h>

struct lconv {
	char	*decimal_point;
	char	*thousands_sep;
	char	*grouping;
	char	*int_curr_symbol;
	char	*currency_symbol;
	char	*mon_decimal_point;
	char	*mon_thousands_sep;
	char	*mon_grouping;
	char	*positive_sign;
	char	*negative_sign;
	char	int_frac_digits;
	char	frac_digits;
	char	p_cs_precedes;
	char	p_sep_by_space;
	char	n_cs_precedes;
	char	n_sep_by_space;
	char	p_sign_posn;
	char	n_sign_posn;
	char	int_p_cs_precedes;
	char	int_p_sep_by_space;
	char	int_n_cs_precedes;
	char	int_n_sep_by_space;
	char	int_p_sign_posn;
	char	int_n_sign_posn;
};

#define	LC_ALL		0
#define	LC_COLLATE	1
#define	LC_CTYPE	2
#define	LC_MONETARY	3
#define	LC_NUMERIC	4
#define	LC_TIME		5
#define LC_MESSAGES	6

#define	_LC_LAST	7		/* marks end */

#include <sys/cdefs.h>

#if __POSIX_VISIBLE >= 200809

#ifndef	_LOCALE_T_DEFINED_
#define	_LOCALE_T_DEFINED_
typedef void	*locale_t;
#endif

#define	LC_COLLATE_MASK		(1 << LC_COLLATE)
#define	LC_CTYPE_MASK		(1 << LC_CTYPE)
#define	LC_MONETARY_MASK	(1 << LC_MONETARY)
#define	LC_NUMERIC_MASK		(1 << LC_NUMERIC)
#define	LC_TIME_MASK		(1 << LC_TIME)
#define	LC_MESSAGES_MASK	(1 << LC_MESSAGES)

#define	LC_ALL_MASK		((1 << _LC_LAST) - 2)

#define	LC_GLOBAL_LOCALE	((locale_t)-1)

#endif /* __POSIX_VISIBLE >= 200809 */


__BEGIN_DECLS
struct lconv	*localeconv(void);
char		*setlocale(int, const char *);

#if __POSIX_VISIBLE >= 200809
locale_t	 duplocale(locale_t);
void		 freelocale(locale_t);
locale_t	 newlocale(int, const char *, locale_t);
locale_t	 uselocale(locale_t);
#endif
__END_DECLS

#endif /* _LOCALE_H_ */
