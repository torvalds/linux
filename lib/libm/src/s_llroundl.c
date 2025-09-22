/*	$OpenBSD: s_llroundl.c,v 1.1 2011/07/06 00:02:42 martynas Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#define	type		long double
#define	roundit		roundl
#define	dtype		long long
#define	DTYPE_MIN	LLONG_MIN
#define	DTYPE_MAX	LLONG_MAX
#define	fn		llroundl

#include "s_lroundl.c"
