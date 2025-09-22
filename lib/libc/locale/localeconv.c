/*	$OpenBSD: localeconv.c,v 1.9 2022/12/27 17:10:06 jmc Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <locale.h>
#include "localedef.h"

/*
 * The localeconv() function constructs a struct lconv from the current
 * monetary and numeric locales.
 *
 * Because localeconv() may be called many times (especially by library
 * routines like printf() & strtod()), the appropriate members of the
 * lconv structure are computed only when the monetary or numeric
 * locale has been changed.
 */
static int __mlocale_changed = 1;
static int __nlocale_changed = 1;

/*
 * Return the current locale conversion.
 */
struct lconv *
localeconv(void)
{
    static struct lconv ret;

    if (__mlocale_changed) {
	/* LC_MONETARY */
	ret.int_curr_symbol	= _CurrentMonetaryLocale->int_curr_symbol;
	ret.currency_symbol	= _CurrentMonetaryLocale->currency_symbol;
	ret.mon_decimal_point	= _CurrentMonetaryLocale->mon_decimal_point;
	ret.mon_thousands_sep	= _CurrentMonetaryLocale->mon_thousands_sep;
	ret.mon_grouping	= _CurrentMonetaryLocale->mon_grouping;
	ret.positive_sign	= _CurrentMonetaryLocale->positive_sign;
	ret.negative_sign	= _CurrentMonetaryLocale->negative_sign;
	ret.int_frac_digits	= _CurrentMonetaryLocale->int_frac_digits;
	ret.frac_digits		= _CurrentMonetaryLocale->frac_digits;
	ret.p_cs_precedes	= _CurrentMonetaryLocale->p_cs_precedes;
	ret.p_sep_by_space	= _CurrentMonetaryLocale->p_sep_by_space;
	ret.n_cs_precedes	= _CurrentMonetaryLocale->n_cs_precedes;
	ret.n_sep_by_space	= _CurrentMonetaryLocale->n_sep_by_space;
	ret.p_sign_posn		= _CurrentMonetaryLocale->p_sign_posn;
	ret.n_sign_posn		= _CurrentMonetaryLocale->n_sign_posn;
	ret.int_p_cs_precedes	= _CurrentMonetaryLocale->int_p_cs_precedes;
	ret.int_p_sep_by_space	= _CurrentMonetaryLocale->int_p_sep_by_space;
	ret.int_n_cs_precedes	= _CurrentMonetaryLocale->int_n_cs_precedes;
	ret.int_n_sep_by_space	= _CurrentMonetaryLocale->int_n_sep_by_space;
	ret.int_p_sign_posn	= _CurrentMonetaryLocale->int_p_sign_posn;
	ret.int_n_sign_posn	= _CurrentMonetaryLocale->int_n_sign_posn;
	__mlocale_changed = 0;
    }

    if (__nlocale_changed) {
	/* LC_NUMERIC */
	ret.decimal_point	= (char *) _CurrentNumericLocale->decimal_point;
	ret.thousands_sep	= (char *) _CurrentNumericLocale->thousands_sep;
	ret.grouping		= (char *) _CurrentNumericLocale->grouping;
	__nlocale_changed = 0;
    }

    return (&ret);
}
DEF_STRONG(localeconv);
