/*	$OpenBSD: s_nan.c,v 1.14 2016/09/12 19:47:02 guenther Exp $	*/
/*-
 * Copyright (c) 2007 David Schultz
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/lib/msun/src/s_nan.c,v 1.2 2007/12/18 23:46:32 das Exp $
 */

#include <sys/types.h>
#include <ctype.h>
#include <endian.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <strings.h>

#include "math_private.h"

/*
 * OpenBSD's ctype doesn't have digittoint, so we define it here.
 */
static int
_digittoint(int c)
{
	if (!isxdigit(c))
		return 0;

	if (isdigit(c))
		return c - '0';
	else
		return isupper(c) ? c - 'A' + 10 : c - 'a' + 10;
}

/*
 * Scan a string of hexadecimal digits (the format nan(3) expects) and
 * make a bit array (using the local endianness). We stop when we
 * encounter an invalid character, NUL, etc.  If we overflow, we do
 * the same as gcc's __builtin_nan(), namely, discard the high order bits.
 *
 * The format this routine accepts needs to be compatible with what is used
 * in contrib/gdtoa/hexnan.c (for strtod/scanf) and what is used in
 * __builtin_nan(). In fact, we're only 100% compatible for strings we
 * consider valid, so we might be violating the C standard. But it's
 * impossible to use nan(3) portably anyway, so this seems good enough.
 */
void
_scan_nan(uint32_t *words, int num_words, const char *s)
{
	int si;		/* index into s */
	int bitpos;	/* index into words (in bits) */

	bzero(words, num_words * sizeof(uint32_t));

	/* Allow a leading '0x'. (It's expected, but redundant.) */
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;

	/* Scan forwards in the string, looking for the end of the sequence. */
	for (si = 0; isxdigit((unsigned char)s[si]); si++)
		;

	/* Scan backwards, filling in the bits in words[] as we go. */
#if BYTE_ORDER == LITTLE_ENDIAN
	for (bitpos = 0; bitpos < 32 * num_words; bitpos += 4) {
#else
	for (bitpos = 32 * num_words - 4; bitpos >= 0; bitpos -= 4) {
#endif
		if (--si < 0)
			break;
		words[bitpos / 32] |= _digittoint((unsigned char)s[si]) << (bitpos % 32);
	}
}

double
nan(const char *s)
{
	union {
		double d;
		uint32_t bits[2];
	} u;

	_scan_nan(u.bits, 2, s);
#if BYTE_ORDER == LITTLE_ENDIAN
	u.bits[1] |= 0x7ff80000;
#else
	u.bits[0] |= 0x7ff80000;
#endif
	return (u.d);
}
DEF_STD(nan);
LDBL_MAYBE_UNUSED_CLONE(nan);

float
nanf(const char *s)
{
	union {
		float f;
		uint32_t bits[1];
	} u;

	_scan_nan(u.bits, 1, s);
	u.bits[0] |= 0x7fc00000;
	return (u.f);
}
