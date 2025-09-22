/*	$OpenBSD: s_llrintf.c,v 1.3 2021/10/14 21:30:00 kettenis Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#define type		float
#define roundit		rintf
#define dtype		long long
#define fn		llrintf

#include "s_lrintf.c"
