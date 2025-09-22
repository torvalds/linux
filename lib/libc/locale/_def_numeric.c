/*	$OpenBSD: _def_numeric.c,v 1.5 2016/05/23 00:05:15 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <locale.h>
#include "localedef.h"

const _NumericLocale _DefaultNumericLocale =
{
	".",
	"",
	""
};

const _NumericLocale *_CurrentNumericLocale = &_DefaultNumericLocale;
