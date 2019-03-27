/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000, 2001 Alexey Zelkin <phantom@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

#include "ldpart.h"
#include "lmonetary.h"

extern const char * __fix_locale_grouping_str(const char *);

#define LCMONETARY_SIZE_FULL (sizeof(struct lc_monetary_T) / sizeof(char *))
#define LCMONETARY_SIZE_MIN \
		(offsetof(struct lc_monetary_T, int_p_cs_precedes) / \
		    sizeof(char *))

static char	empty[] = "";
static char	numempty[] = { CHAR_MAX, '\0'};

static const struct lc_monetary_T _C_monetary_locale = {
	empty,		/* int_curr_symbol */
	empty,		/* currency_symbol */
	empty,		/* mon_decimal_point */
	empty,		/* mon_thousands_sep */
	numempty,	/* mon_grouping */
	empty,		/* positive_sign */
	empty,		/* negative_sign */
	numempty,	/* int_frac_digits */
	numempty,	/* frac_digits */
	numempty,	/* p_cs_precedes */
	numempty,	/* p_sep_by_space */
	numempty,	/* n_cs_precedes */
	numempty,	/* n_sep_by_space */
	numempty,	/* p_sign_posn */
	numempty,	/* n_sign_posn */
	numempty,	/* int_p_cs_precedes */
	numempty,	/* int_n_cs_precedes */
	numempty,	/* int_p_sep_by_space */
	numempty,	/* int_n_sep_by_space */
	numempty,	/* int_p_sign_posn */
	numempty	/* int_n_sign_posn */
};

struct xlocale_monetary __xlocale_global_monetary;

static char
cnv(const char *str)
{
	int i = strtol(str, NULL, 10);

	if (i == -1)
		i = CHAR_MAX;
	return ((char)i);
}

static void
destruct_monetary(void *v)
{
	struct xlocale_monetary *l = v;
	if (l->buffer)
		free(l->buffer);
	free(l);
}

static int
monetary_load_locale_l(struct xlocale_monetary *loc, int *using_locale,
		int *changed, const char *name)
{
	int ret;
	struct lc_monetary_T *l = &loc->locale;

	ret = __part_load_locale(name, using_locale,
		&loc->buffer, "LC_MONETARY",
		LCMONETARY_SIZE_FULL, LCMONETARY_SIZE_MIN,
		(const char **)l);
	if (ret != _LDP_ERROR)
		*changed = 1;
	if (ret == _LDP_LOADED) {
		l->mon_grouping =
		     __fix_locale_grouping_str(l->mon_grouping);

#define M_ASSIGN_CHAR(NAME) (((char *)l->NAME)[0] = \
			     cnv(l->NAME))

		M_ASSIGN_CHAR(int_frac_digits);
		M_ASSIGN_CHAR(frac_digits);
		M_ASSIGN_CHAR(p_cs_precedes);
		M_ASSIGN_CHAR(p_sep_by_space);
		M_ASSIGN_CHAR(n_cs_precedes);
		M_ASSIGN_CHAR(n_sep_by_space);
		M_ASSIGN_CHAR(p_sign_posn);
		M_ASSIGN_CHAR(n_sign_posn);

		/*
		 * The six additional C99 international monetary formatting
		 * parameters default to the national parameters when
		 * reading FreeBSD LC_MONETARY data files.
		 */
#define	M_ASSIGN_ICHAR(NAME)						\
		do {							\
			if (l->int_##NAME == NULL)	\
				l->int_##NAME =		\
				    l->NAME;		\
			else						\
				M_ASSIGN_CHAR(int_##NAME);		\
		} while (0)

		M_ASSIGN_ICHAR(p_cs_precedes);
		M_ASSIGN_ICHAR(n_cs_precedes);
		M_ASSIGN_ICHAR(p_sep_by_space);
		M_ASSIGN_ICHAR(n_sep_by_space);
		M_ASSIGN_ICHAR(p_sign_posn);
		M_ASSIGN_ICHAR(n_sign_posn);
	}
	return (ret);
}
int
__monetary_load_locale(const char *name)
{
	return monetary_load_locale_l(&__xlocale_global_monetary,
			&__xlocale_global_locale.using_monetary_locale,
			&__xlocale_global_locale.monetary_locale_changed, name);
}
void* __monetary_load(const char *name, locale_t l)
{
	struct xlocale_monetary *new = calloc(sizeof(struct xlocale_monetary), 1);
	new->header.header.destructor = destruct_monetary;
	if (monetary_load_locale_l(new, &l->using_monetary_locale,
				&l->monetary_locale_changed, name) == _LDP_ERROR)
	{
		xlocale_release(new);
		return NULL;
	}
	return new;
}


struct lc_monetary_T *
__get_current_monetary_locale(locale_t loc)
{
	return (loc->using_monetary_locale
		? &((struct xlocale_monetary*)loc->components[XLC_MONETARY])->locale
		: (struct lc_monetary_T *)&_C_monetary_locale);
}

#ifdef LOCALE_DEBUG
void
monetdebug() {
printf(	"int_curr_symbol = %s\n"
	"currency_symbol = %s\n"
	"mon_decimal_point = %s\n"
	"mon_thousands_sep = %s\n"
	"mon_grouping = %s\n"
	"positive_sign = %s\n"
	"negative_sign = %s\n"
	"int_frac_digits = %d\n"
	"frac_digits = %d\n"
	"p_cs_precedes = %d\n"
	"p_sep_by_space = %d\n"
	"n_cs_precedes = %d\n"
	"n_sep_by_space = %d\n"
	"p_sign_posn = %d\n"
	"n_sign_posn = %d\n"
	"int_p_cs_precedes = %d\n"
	"int_p_sep_by_space = %d\n"
	"int_n_cs_precedes = %d\n"
	"int_n_sep_by_space = %d\n"
	"int_p_sign_posn = %d\n"
	"int_n_sign_posn = %d\n",
	_monetary_locale.int_curr_symbol,
	_monetary_locale.currency_symbol,
	_monetary_locale.mon_decimal_point,
	_monetary_locale.mon_thousands_sep,
	_monetary_locale.mon_grouping,
	_monetary_locale.positive_sign,
	_monetary_locale.negative_sign,
	_monetary_locale.int_frac_digits[0],
	_monetary_locale.frac_digits[0],
	_monetary_locale.p_cs_precedes[0],
	_monetary_locale.p_sep_by_space[0],
	_monetary_locale.n_cs_precedes[0],
	_monetary_locale.n_sep_by_space[0],
	_monetary_locale.p_sign_posn[0],
	_monetary_locale.n_sign_posn[0],
	_monetary_locale.int_p_cs_precedes[0],
	_monetary_locale.int_p_sep_by_space[0],
	_monetary_locale.int_n_cs_precedes[0],
	_monetary_locale.int_n_sep_by_space[0],
	_monetary_locale.int_p_sign_posn[0],
	_monetary_locale.int_n_sign_posn[0]
);
}
#endif /* LOCALE_DEBUG */
