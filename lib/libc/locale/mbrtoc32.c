/*	$OpenBSD: mbrtoc32.c,v 1.1 2023/08/20 15:02:51 schwarze Exp $ */
/*
 * Written by Ingo Schwarze <schwarze@openbsd.org>
 * and placed in the public domain on March 19, 2022.
 */

#include <uchar.h>
#include <wchar.h>

size_t
mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps)
{
	static mbstate_t mbs;

	if (ps == NULL)
		ps = &mbs;
	return mbrtowc(pc32, s, n, ps);
}
