/*	$OpenBSD: strcasecmp_l.c,v 1.1 2017/09/05 03:16:13 schwarze Exp $ */
/*
 * Written in 2017 by Ingo Schwarze <schwarze@openbsd.org>.
 * Released into the public domain.
 */

#include <string.h>

int
strcasecmp_l(const char *s1, const char *s2,
    locale_t locale __attribute__((__unused__)))
{
	return strcasecmp(s1, s2);
}

int
strncasecmp_l(const char *s1, const char *s2, size_t n,
    locale_t locale __attribute__((__unused__)))
{
	return strncasecmp(s1, s2, n);
}
