/*	$OpenBSD: s_scalbln.c,v 1.1 2009/07/25 11:38:10 martynas Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#include <limits.h>
#include <math.h>

double
scalbln(double x, long n)
{
	if (n < INT_MIN)
		return scalbn(x, INT_MIN);
	else if (n > INT_MAX)
		return scalbn(x, INT_MAX);
	else
		return scalbn(x, (int)n);
}

float
scalblnf(float x, long n)
{
	if (n < INT_MIN)
		return scalbnf(x, INT_MIN);
	else if (n > INT_MAX)
		return scalbnf(x, INT_MAX);
	else
		return scalbnf(x, (int)n);
}

long double
scalblnl(long double x, long n)
{
	if (n < INT_MIN)
		return scalbnl(x, INT_MIN);
	else if (n > INT_MAX)
		return scalbnl(x, INT_MAX);
	else
		return scalbnl(x, (int)n);
}

