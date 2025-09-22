/*	$OpenBSD: s_llrint.c,v 1.7 2021/10/14 21:30:00 kettenis Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#define type		double
#define roundit		rint
#define dtype		long long
#define fn		llrint

#include "s_lrint.c"
