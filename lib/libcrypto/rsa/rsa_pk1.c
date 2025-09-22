/* $OpenBSD: rsa_pk1.c,v 1.18 2025/05/10 05:54:38 tb Exp $ */
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
#include <stdlib.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/rsa.h>

#include "bytestring.h"
#include "err_local.h"

int
RSA_padding_add_PKCS1_type_1(unsigned char *to, int tlen,
    const unsigned char *from, int flen)
{
	CBB cbb;
	int i;
	int ret = 0;

	/*
	 * Pad data block with PKCS1 type 1 padding - RFC 2313, section 8.1.
	 */

	memset(&cbb, 0, sizeof(cbb));

	if (flen < 0 || tlen < 0)
		goto err;

	if (flen > tlen - RSA_PKCS1_PADDING_SIZE) {
		RSAerror(RSA_R_DATA_TOO_LARGE_FOR_KEY_SIZE);
		goto err;
	}

	if (!CBB_init_fixed(&cbb, to, tlen))
		goto err;

	/*
	 * Add leading NUL, block type (0x01), padding bytes (0xff) and
	 * trailing NUL.
	 */
	if (!CBB_add_u8(&cbb, 0))
		goto err;
	if (!CBB_add_u8(&cbb, 1))
		goto err;
	for (i = 0; i < tlen - 3 - flen; i++) {
		if (!CBB_add_u8(&cbb, 0xff))
			goto err;
	}
	if (!CBB_add_u8(&cbb, 0))
		goto err;

	/* Now add the actual data. */
	if (!CBB_add_bytes(&cbb, from, flen))
		goto err;

	if (!CBB_finish(&cbb, NULL, NULL))
		goto err;

	ret = 1;

 err:
	CBB_cleanup(&cbb);

	return ret;
}
LCRYPTO_ALIAS(RSA_padding_add_PKCS1_type_1);

int
RSA_padding_check_PKCS1_type_1(unsigned char *to, int tlen,
    const unsigned char *from, int flen, int num)
{
	int i, j;
	const unsigned char *p;

	p = from;
	if (num != flen + 1 || *(p++) != 01) {
		RSAerror(RSA_R_BLOCK_TYPE_IS_NOT_01);
		return -1;
	}

	/* scan over padding data */
	j = flen - 1; /* one for type. */
	for (i = 0; i < j; i++) {
		if (*p != 0xff) {
			/* should decrypt to 0xff */
			if (*p == 0) {
				p++;
				break;
			} else {
				RSAerror(RSA_R_BAD_FIXED_HEADER_DECRYPT);
				return -1;
			}
		}
		p++;
	}

	if (i == j) {
		RSAerror(RSA_R_NULL_BEFORE_BLOCK_MISSING);
		return -1;
	}

	if (i < 8) {
		RSAerror(RSA_R_BAD_PAD_BYTE_COUNT);
		return -1;
	}
	i++; /* Skip over the '\0' */
	j -= i;
	if (j > tlen) {
		RSAerror(RSA_R_DATA_TOO_LARGE);
		return -1;
	}
	memcpy(to, p, j);

	return j;
}
LCRYPTO_ALIAS(RSA_padding_check_PKCS1_type_1);

int
RSA_padding_add_PKCS1_type_2(unsigned char *to, int tlen,
    const unsigned char *from, int flen)
{
	uint8_t padding[256];
	uint8_t pad;
	CBB cbb;
	CBS cbs;
	int i;
	int ret = 0;

	/*
	 * Pad data block with PKCS1 type 2 padding - RFC 2313, section 8.1.
	 */

	memset(&cbb, 0, sizeof(cbb));
	CBS_init(&cbs, NULL, 0);

	if (flen < 0 || tlen < 0)
		goto err;

	if (flen > tlen - RSA_PKCS1_PADDING_SIZE) {
		RSAerror(RSA_R_DATA_TOO_LARGE_FOR_KEY_SIZE);
		goto err;
	}

	if (!CBB_init_fixed(&cbb, to, tlen))
		goto err;

	/*
	 * Add leading NUL, block type (0x02), padding bytes (random non-zero
	 * bytes) and trailing NUL.
	 */
	if (!CBB_add_u8(&cbb, 0))
		goto err;
	if (!CBB_add_u8(&cbb, 2))
		goto err;
	for (i = 0; i < tlen - 3 - flen; i++) {
		do {
			if (CBS_len(&cbs) == 0) {
				arc4random_buf(padding, sizeof(padding));
				CBS_init(&cbs, padding, sizeof(padding));
			}
			if (!CBS_get_u8(&cbs, &pad))
				goto err;
		} while (pad == 0);

		if (!CBB_add_u8(&cbb, pad))
			goto err;
	}
	if (!CBB_add_u8(&cbb, 0))
		goto err;

	/* Now add the actual data. */
	if (!CBB_add_bytes(&cbb, from, flen))
		goto err;

	if (!CBB_finish(&cbb, NULL, NULL))
		goto err;

	ret = 1;

 err:
	CBB_cleanup(&cbb);
	explicit_bzero(padding, sizeof(padding));

	return ret;
}
LCRYPTO_ALIAS(RSA_padding_add_PKCS1_type_2);

int
RSA_padding_check_PKCS1_type_2(unsigned char *to, int tlen,
    const unsigned char *from, int flen, int num)
{
	int i, j;
	const unsigned char *p;

	p = from;
	if (num != flen + 1 || *(p++) != 02) {
		RSAerror(RSA_R_BLOCK_TYPE_IS_NOT_02);
		return -1;
	}

	/* scan over padding data */
	j = flen - 1; /* one for type. */
	for (i = 0; i < j; i++)
		if (*(p++) == 0)
			break;

	if (i == j) {
		RSAerror(RSA_R_NULL_BEFORE_BLOCK_MISSING);
		return -1;
	}

	if (i < 8) {
		RSAerror(RSA_R_BAD_PAD_BYTE_COUNT);
		return -1;
	}
	i++; /* Skip over the '\0' */
	j -= i;
	if (j > tlen) {
		RSAerror(RSA_R_DATA_TOO_LARGE);
		return -1;
	}
	memcpy(to, p, j);

	return j;
}
LCRYPTO_ALIAS(RSA_padding_check_PKCS1_type_2);
