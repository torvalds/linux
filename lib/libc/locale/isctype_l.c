/*	$OpenBSD: isctype_l.c,v 1.1 2017/09/05 03:16:13 schwarze Exp $ */
/*
 * Copyright (c) 2017 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Only ASCII and UTF-8 are supported, and all single-byte UTF-8
 * characters agree with ASCII, so always use the ASCII tables.
 */

#define	_ANSI_LIBRARY
#include <ctype.h>

#undef isalnum_l
int
isalnum_l(int c, locale_t locale __attribute__((__unused__)))
{
	return isalnum(c);
}

#undef isalpha_l
int
isalpha_l(int c, locale_t locale __attribute__((__unused__)))
{
	return isalpha(c);
}

#undef isblank_l
int
isblank_l(int c, locale_t locale __attribute__((__unused__)))
{
	return isblank(c);
}

#undef iscntrl_l
int
iscntrl_l(int c, locale_t locale __attribute__((__unused__)))
{
	return iscntrl(c);
}

#undef isdigit_l
int
isdigit_l(int c, locale_t locale __attribute__((__unused__)))
{
	return isdigit(c);
}

#undef isgraph_l
int
isgraph_l(int c, locale_t locale __attribute__((__unused__)))
{
	return isgraph(c);
}

#undef islower_l
int
islower_l(int c, locale_t locale __attribute__((__unused__)))
{
	return islower(c);
}

#undef isprint_l
int
isprint_l(int c, locale_t locale __attribute__((__unused__)))
{
	return isprint(c);
}

#undef ispunct_l
int
ispunct_l(int c, locale_t locale __attribute__((__unused__)))
{
	return ispunct(c);
}

#undef isspace_l
int
isspace_l(int c, locale_t locale __attribute__((__unused__)))
{
	return isspace(c);
}

#undef isupper_l
int
isupper_l(int c, locale_t locale __attribute__((__unused__)))
{
	return isupper(c);
}

#undef isxdigit_l
int
isxdigit_l(int c, locale_t locale __attribute__((__unused__)))
{
	return isxdigit(c);
}

#undef tolower_l
int
tolower_l(int c, locale_t locale __attribute__((__unused__)))
{
	return tolower(c);
}

#undef toupper_l
int
toupper_l(int c, locale_t locale __attribute__((__unused__)))
{
	return toupper(c);
}
