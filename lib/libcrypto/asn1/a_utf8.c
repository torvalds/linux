/* $OpenBSD: a_utf8.c,v 1.9 2022/11/26 16:08:50 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stdio.h>

#include <openssl/asn1.h>

#include "asn1_local.h"

/* UTF8 utilities */

/*
 * This parses a UTF8 string one character at a time. It is passed a pointer
 * to the string and the length of the string. It sets 'value' to the value of
 * the current character. It returns the number of characters read or a
 * negative error code:
 * -1 = string too short
 * -2 = illegal character
 * -3 = subsequent characters not of the form 10xxxxxx
 * -4 = character encoded incorrectly (not minimal length).
 */

int
UTF8_getc(const unsigned char *str, int len, unsigned long *val)
{
	const unsigned char *p;
	unsigned long value;
	int ret;
	if (len <= 0)
		return 0;
	p = str;

	/* Check syntax and work out the encoded value (if correct) */
	if ((*p & 0x80) == 0) {
		value = *p++ & 0x7f;
		ret = 1;
	} else if ((*p & 0xe0) == 0xc0) {
		if (*p < 0xc2)
			return -2;
		if (len < 2)
			return -1;
		if ((p[1] & 0xc0) != 0x80)
			return -3;
		value = (*p++ & 0x1f) << 6;
		value |= *p++ & 0x3f;
		if (value < 0x80)
			return -4;
		ret = 2;
	} else if ((*p & 0xf0) == 0xe0) {
		if (len < 3)
			return -1;
		if (((p[1] & 0xc0) != 0x80) ||
		    ((p[2] & 0xc0) != 0x80))
			return -3;
		value = (*p++ & 0xf) << 12;
		value |= (*p++ & 0x3f) << 6;
		value |= *p++ & 0x3f;
		if (value < 0x800)
			return -4;
		/* surrogate pair code points are not valid */
		if (value >= 0xd800 && value < 0xe000)
			return -2;
		ret = 3;
	} else if ((*p & 0xf8) == 0xf0 && (*p < 0xf5)) {
		if (len < 4)
			return -1;
		if (((p[1] & 0xc0) != 0x80) ||
		    ((p[2] & 0xc0) != 0x80) ||
		    ((p[3] & 0xc0) != 0x80))
			return -3;
		value = ((unsigned long)(*p++ & 0x7)) << 18;
		value |= (*p++ & 0x3f) << 12;
		value |= (*p++ & 0x3f) << 6;
		value |= *p++ & 0x3f;
		if (value < 0x10000)
			return -4;
		if (value > UNICODE_MAX)
			return -2;
		ret = 4;
	} else
		return -2;
	*val = value;
	return ret;
}

/* This takes a Unicode code point 'value' and writes its UTF-8 encoded form
 * in 'str' where 'str' is a buffer of at least length 'len'.  If 'str'
 * is NULL, then nothing is written and just the return code is determined.

 * Returns less than zero on error:
 *  -1 if 'str' is not NULL and 'len' is too small
 *  -2 if 'value' is an invalid character (surrogate or out-of-range)
 *
 * Otherwise, returns the number of bytes in 'value's encoded form
 * (i.e., the number of bytes written to 'str' when it's not NULL).
 *
 * It will need at most 4 characters.
 */

int
UTF8_putc(unsigned char *str, int len, unsigned long value)
{
	if (value < 0x80) {
		if (str != NULL) {
			if (len < 1)
				return -1;
			str[0] = (unsigned char)value;
		}
		return 1;
	}
	if (value < 0x800) {
		if (str != NULL) {
			if (len < 2)
				return -1;
			str[0] = (unsigned char)(((value >> 6) & 0x1f) | 0xc0);
			str[1] = (unsigned char)((value & 0x3f) | 0x80);
		}
		return 2;
	}
	if (value < 0x10000) {
		if (UNICODE_IS_SURROGATE(value))
			return -2;
		if (str != NULL) {
			if (len < 3)
				return -1;
			str[0] = (unsigned char)(((value >> 12) & 0xf) | 0xe0);
			str[1] = (unsigned char)(((value >> 6) & 0x3f) | 0x80);
			str[2] = (unsigned char)((value & 0x3f) | 0x80);
		}
		return 3;
	}
	if (value <= UNICODE_MAX) {
		if (str != NULL) {
			if (len < 4)
				return -1;
			str[0] = (unsigned char)(((value >> 18) & 0x7) | 0xf0);
			str[1] = (unsigned char)(((value >> 12) & 0x3f) | 0x80);
			str[2] = (unsigned char)(((value >> 6) & 0x3f) | 0x80);
			str[3] = (unsigned char)((value & 0x3f) | 0x80);
		}
		return 4;
	}
	return -2;
}
