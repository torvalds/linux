/*	$OpenBSD: s_llrintl.c,v 1.1 2011/07/06 00:02:42 martynas Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#define	type		long double
#define	roundit		rintl
#define	dtype		long long
#define	fn		llrintl

#include "s_lrintl.c"
