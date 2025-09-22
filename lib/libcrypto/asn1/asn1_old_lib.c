/* $OpenBSD: asn1_old_lib.c,v 1.7 2025/05/10 05:54:38 tb Exp $ */
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

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <openssl/asn1.h>

#include "asn1_local.h"
#include "err_local.h"

static void asn1_put_length(unsigned char **pp, int length);

int
ASN1_get_object(const unsigned char **pp, long *plength, int *ptag,
    int *pclass, long omax)
{
	int constructed, indefinite;
	uint32_t tag_number;
	uint8_t tag_class;
	size_t length;
	CBS cbs;
	int ret = 0;

	*pclass = 0;
	*ptag = 0;
	*plength = 0;

	CBS_init(&cbs, *pp, omax);

	if (!asn1_get_object_cbs(&cbs, 0, &tag_class, &constructed, &tag_number,
	    &indefinite, &length)) {
		ASN1error(ASN1_R_HEADER_TOO_LONG);
		return 0x80;
	}

	if (tag_number > INT_MAX) {
		ASN1error(ASN1_R_HEADER_TOO_LONG);
		return 0x80;
	}

	/*
	 * API insanity ahead... in this case we add an error to the stack and
	 * signal an error by setting the 8th bit in the return value... but we
	 * still provide all of the decoded data.
	 */
	if (length > CBS_len(&cbs) || length > LONG_MAX) {
		ASN1error(ASN1_R_TOO_LONG);
		ret = 0x80;
	}

	*pclass = tag_class << 6;
	*ptag = tag_number;
	*plength = length;

	*pp = CBS_data(&cbs);

	if (constructed)
		ret |= 1 << 5;
	if (indefinite)
		ret |= 1;

	return ret;
}
LCRYPTO_ALIAS(ASN1_get_object);

/* class 0 is constructed
 * constructed == 2 for indefinite length constructed */
void
ASN1_put_object(unsigned char **pp, int constructed, int length, int tag,
    int xclass)
{
	unsigned char *p = *pp;
	int i, ttag;

	i = (constructed) ? V_ASN1_CONSTRUCTED : 0;
	i |= (xclass & V_ASN1_PRIVATE);
	if (tag < 31)
		*(p++) = i | (tag & V_ASN1_PRIMITIVE_TAG);
	else {
		*(p++) = i | V_ASN1_PRIMITIVE_TAG;
		for(i = 0, ttag = tag; ttag > 0; i++)
			ttag >>= 7;
		ttag = i;
		while (i-- > 0) {
			p[i] = tag & 0x7f;
			if (i != (ttag - 1))
				p[i] |= 0x80;
			tag >>= 7;
		}
		p += ttag;
	}
	if (constructed == 2)
		*(p++) = 0x80;
	else
		asn1_put_length(&p, length);
	*pp = p;
}
LCRYPTO_ALIAS(ASN1_put_object);

int
ASN1_put_eoc(unsigned char **pp)
{
	unsigned char *p = *pp;

	*p++ = 0;
	*p++ = 0;
	*pp = p;
	return 2;
}
LCRYPTO_ALIAS(ASN1_put_eoc);

static void
asn1_put_length(unsigned char **pp, int length)
{
	unsigned char *p = *pp;

	int i, l;
	if (length <= 127)
		*(p++) = (unsigned char)length;
	else {
		l = length;
		for (i = 0; l > 0; i++)
			l >>= 8;
		*(p++) = i | 0x80;
		l = i;
		while (i-- > 0) {
			p[i] = length & 0xff;
			length >>= 8;
		}
		p += l;
	}
	*pp = p;
}

int
ASN1_object_size(int constructed, int length, int tag)
{
	int ret;

	ret = length;
	ret++;
	if (tag >= 31) {
		while (tag > 0) {
			tag >>= 7;
			ret++;
		}
	}
	if (constructed == 2)
		return ret + 3;
	ret++;
	if (length > 127) {
		while (length > 0) {
			length >>= 8;
			ret++;
		}
	}
	return (ret);
}
LCRYPTO_ALIAS(ASN1_object_size);
