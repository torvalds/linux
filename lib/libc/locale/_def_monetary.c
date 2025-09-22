/*	$OpenBSD: _def_monetary.c,v 1.6 2016/05/23 00:05:15 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <limits.h>
#include <locale.h>
#include "localedef.h"

const _MonetaryLocale _DefaultMonetaryLocale =
{
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	CHAR_MAX,
	CHAR_MAX,

	CHAR_MAX,	/* local p_cs_precedes */
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,

	CHAR_MAX,	/* intl p_cs_precedes */
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
	CHAR_MAX,
};

const _MonetaryLocale *_CurrentMonetaryLocale = &_DefaultMonetaryLocale;
