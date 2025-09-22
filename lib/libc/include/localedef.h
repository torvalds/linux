/*	$OpenBSD: localedef.h,v 1.1 2016/05/23 00:05:15 guenther Exp $	*/
/*	$NetBSD: localedef.h,v 1.4 1996/04/09 20:55:31 cgd Exp $	*/

/*
 * Copyright (c) 1994 Winning Strategies, Inc.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Winning Strategies, Inc.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LOCALEDEF_H_
#define _LOCALEDEF_H_

#include <sys/types.h>

typedef struct
{
	char *yesexpr;
	char *noexpr;
	char *yesstr;
	char *nostr;
} _MessagesLocale;


typedef struct
{
	char *int_curr_symbol;
	char *currency_symbol;
	char *mon_decimal_point;
	char *mon_thousands_sep;
	char *mon_grouping;
	char *positive_sign;
	char *negative_sign;
	char int_frac_digits;
	char frac_digits;
	char p_cs_precedes;
	char p_sep_by_space;
	char n_cs_precedes;
	char n_sep_by_space;
	char p_sign_posn;
	char n_sign_posn;
	char int_p_cs_precedes;
	char int_p_sep_by_space;
	char int_n_cs_precedes;
	char int_n_sep_by_space;
	char int_p_sign_posn;
	char int_n_sign_posn;
} _MonetaryLocale;


typedef struct
{
	const char *decimal_point;
	const char *thousands_sep;
	const char *grouping;
} _NumericLocale;


typedef struct {
	const char *abday[7];
	const char *day[7];
	const char *abmon[12];
	const char *mon[12];
	const char *am_pm[2];
	const char *d_t_fmt;
	const char *d_fmt;
	const char *t_fmt;
	const char *t_fmt_ampm;
} _TimeLocale;


__BEGIN_HIDDEN_DECLS
extern const _MessagesLocale *_CurrentMessagesLocale;
extern const _MessagesLocale  _DefaultMessagesLocale;
extern const _MonetaryLocale *_CurrentMonetaryLocale;
extern const _MonetaryLocale  _DefaultMonetaryLocale;
extern const _NumericLocale *_CurrentNumericLocale;
extern const _NumericLocale  _DefaultNumericLocale;
extern const _TimeLocale *_CurrentTimeLocale;
extern const _TimeLocale  _DefaultTimeLocale;
__END_HIDDEN_DECLS

#endif /* !_LOCALEDEF_H_ */
