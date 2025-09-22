/* $OpenBSD: rsa_x931.c,v 1.13 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2005.
 */
/* ====================================================================
 * Copyright (c) 2005 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#include <stdio.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/objects.h>
#include <openssl/rsa.h>

#include "err_local.h"

int
RSA_padding_add_X931(unsigned char *to, int tlen, const unsigned char *from,
    int flen)
{
	int j;
	unsigned char *p;

	/*
	 * Absolute minimum amount of padding is 1 header nibble, 1 padding
	 * nibble and 2 trailer bytes: but 1 hash if is already in 'from'.
	 */
	j = tlen - flen - 2;

	if (j < 0) {
		RSAerror(RSA_R_DATA_TOO_LARGE_FOR_KEY_SIZE);
		return -1;
	}

	p = (unsigned char *)to;

	/* If no padding start and end nibbles are in one byte */
	if (j == 0)
		*p++ = 0x6A;
	else {
		*p++ = 0x6B;
		if (j > 1) {
			memset(p, 0xBB, j - 1);
			p += j - 1;
		}
		*p++ = 0xBA;
	}
	memcpy(p, from, flen);
	p += flen;
	*p = 0xCC;
	return 1;
}

int
RSA_padding_check_X931(unsigned char *to, int tlen, const unsigned char *from,
    int flen, int num)
{
	int i = 0, j;
	const unsigned char *p = from;

	if (num != flen || (*p != 0x6A && *p != 0x6B)) {
		RSAerror(RSA_R_INVALID_HEADER);
		return -1;
	}

	if (*p++ == 0x6B) {
		j = flen - 3;
		for (i = 0; i < j; i++) {
			unsigned char c = *p++;
			if (c == 0xBA)
				break;
			if (c != 0xBB) {
				RSAerror(RSA_R_INVALID_PADDING);
				return -1;
			}
		}

		if (i == 0) {
			RSAerror(RSA_R_INVALID_PADDING);
			return -1;
		}

		j -= i;
	} else
		j = flen - 2;

	if (j < 0 || p[j] != 0xCC) {
		RSAerror(RSA_R_INVALID_TRAILER);
		return -1;
	}

	memcpy(to, p, j);

	return j;
}

/* Translate between X931 hash ids and NIDs */

int
RSA_X931_hash_id(int nid)
{
	switch (nid) {
	case NID_sha1:
		return 0x33;
	case NID_sha256:
		return 0x34;
	case NID_sha384:
		return 0x36;
	case NID_sha512:
		return 0x35;
	}

	return -1;
}
