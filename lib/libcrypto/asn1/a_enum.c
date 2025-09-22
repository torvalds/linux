/* $OpenBSD: a_enum.c,v 1.31 2025/05/10 05:54:38 tb Exp $ */
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
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/bn.h>
#include <openssl/buffer.h>

#include "asn1_local.h"
#include "bytestring.h"
#include "err_local.h"

/*
 * Code for ENUMERATED type: identical to INTEGER apart from a different tag.
 * for comments on encoding see a_int.c
 */

const ASN1_ITEM ASN1_ENUMERATED_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_ENUMERATED,
	.sname = "ASN1_ENUMERATED",
};
LCRYPTO_ALIAS(ASN1_ENUMERATED_it);

ASN1_ENUMERATED *
ASN1_ENUMERATED_new(void)
{
	return (ASN1_ENUMERATED *)ASN1_item_new(&ASN1_ENUMERATED_it);
}
LCRYPTO_ALIAS(ASN1_ENUMERATED_new);

static void
asn1_aenum_clear(ASN1_ENUMERATED *aenum)
{
	freezero(aenum->data, aenum->length);

	memset(aenum, 0, sizeof(*aenum));

	aenum->type = V_ASN1_ENUMERATED;
}

void
ASN1_ENUMERATED_free(ASN1_ENUMERATED *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_ENUMERATED_it);
}
LCRYPTO_ALIAS(ASN1_ENUMERATED_free);

int
ASN1_ENUMERATED_get_int64(int64_t *out_val, const ASN1_ENUMERATED *aenum)
{
	CBS cbs;

	*out_val = 0;

	if (aenum == NULL || aenum->length < 0)
		return 0;

	if (aenum->type != V_ASN1_ENUMERATED &&
	    aenum->type != V_ASN1_NEG_ENUMERATED) {
		ASN1error(ASN1_R_WRONG_INTEGER_TYPE);
		return 0;
	}

	CBS_init(&cbs, aenum->data, aenum->length);

	return asn1_aint_get_int64(&cbs, (aenum->type == V_ASN1_NEG_ENUMERATED),
	    out_val);
}
LCRYPTO_ALIAS(ASN1_ENUMERATED_get_int64);

int
ASN1_ENUMERATED_set_int64(ASN1_ENUMERATED *aenum, int64_t val)
{
	uint64_t uval;

	asn1_aenum_clear(aenum);

	uval = (uint64_t)val;

	if (val < 0) {
		aenum->type = V_ASN1_NEG_ENUMERATED;
		uval = -uval;
	}

	return asn1_aint_set_uint64(uval, &aenum->data, &aenum->length);
}
LCRYPTO_ALIAS(ASN1_ENUMERATED_set_int64);

long
ASN1_ENUMERATED_get(const ASN1_ENUMERATED *aenum)
{
	int64_t val;

	if (aenum == NULL)
		return 0;
	if (!ASN1_ENUMERATED_get_int64(&val, aenum))
		return -1;
	if (val < LONG_MIN || val > LONG_MAX) {
		/* hmm... a bit ugly, return all ones */
		return -1;
	}

	return (long)val;
}
LCRYPTO_ALIAS(ASN1_ENUMERATED_get);

int
ASN1_ENUMERATED_set(ASN1_ENUMERATED *aenum, long val)
{
	return ASN1_ENUMERATED_set_int64(aenum, val);
}
LCRYPTO_ALIAS(ASN1_ENUMERATED_set);

ASN1_ENUMERATED *
BN_to_ASN1_ENUMERATED(const BIGNUM *bn, ASN1_ENUMERATED *ai)
{
	ASN1_ENUMERATED *ret;
	int len, j;

	if (ai == NULL)
		ret = ASN1_ENUMERATED_new();
	else
		ret = ai;
	if (ret == NULL) {
		ASN1error(ERR_R_NESTED_ASN1_ERROR);
		goto err;
	}
	if (BN_is_negative(bn))
		ret->type = V_ASN1_NEG_ENUMERATED;
	else
		ret->type = V_ASN1_ENUMERATED;
	j = BN_num_bits(bn);
	len = ((j == 0) ? 0 : ((j / 8) + 1));
	if (ret->length < len + 4) {
		unsigned char *new_data = realloc(ret->data, len + 4);
		if (!new_data) {
			ASN1error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		ret->data = new_data;
	}
	ret->length = BN_bn2bin(bn, ret->data);

	/* Correct zero case */
	if (!ret->length) {
		ret->data[0] = 0;
		ret->length = 1;
	}
	return (ret);

 err:
	if (ret != ai)
		ASN1_ENUMERATED_free(ret);
	return (NULL);
}
LCRYPTO_ALIAS(BN_to_ASN1_ENUMERATED);

BIGNUM *
ASN1_ENUMERATED_to_BN(const ASN1_ENUMERATED *ai, BIGNUM *bn)
{
	BIGNUM *ret;

	if ((ret = BN_bin2bn(ai->data, ai->length, bn)) == NULL)
		ASN1error(ASN1_R_BN_LIB);
	else if (ai->type == V_ASN1_NEG_ENUMERATED)
		BN_set_negative(ret, 1);
	return (ret);
}
LCRYPTO_ALIAS(ASN1_ENUMERATED_to_BN);

/* Based on a_int.c: equivalent ENUMERATED functions */

int
i2a_ASN1_ENUMERATED(BIO *bp, const ASN1_ENUMERATED *a)
{
	int i, n = 0;
	static const char h[] = "0123456789ABCDEF";
	char buf[2];

	if (a == NULL)
		return (0);

	if (a->length == 0) {
		if (BIO_write(bp, "00", 2) != 2)
			goto err;
		n = 2;
	} else {
		for (i = 0; i < a->length; i++) {
			if ((i != 0) && (i % 35 == 0)) {
				if (BIO_write(bp, "\\\n", 2) != 2)
					goto err;
				n += 2;
			}
			buf[0] = h[((unsigned char)a->data[i] >> 4) & 0x0f];
			buf[1] = h[((unsigned char)a->data[i]) & 0x0f];
			if (BIO_write(bp, buf, 2) != 2)
				goto err;
			n += 2;
		}
	}
	return (n);

 err:
	return (-1);
}
LCRYPTO_ALIAS(i2a_ASN1_ENUMERATED);

int
a2i_ASN1_ENUMERATED(BIO *bp, ASN1_ENUMERATED *bs, char *buf, int size)
{
	int ret = 0;
	int i, j,k, m,n, again, bufsize;
	unsigned char *s = NULL, *sp;
	unsigned char *bufp;
	int first = 1;
	size_t num = 0, slen = 0;

	bs->type = V_ASN1_ENUMERATED;

	bufsize = BIO_gets(bp, buf, size);
	for (;;) {
		if (bufsize < 1)
			goto err_sl;
		i = bufsize;
		if (buf[i-1] == '\n')
			buf[--i] = '\0';
		if (i == 0)
			goto err_sl;
		if (buf[i-1] == '\r')
			buf[--i] = '\0';
		if (i == 0)
			goto err_sl;
		if (buf[i - 1] == '\\') {
			i--;
			again = 1;
		} else
			again = 0;
		buf[i] = '\0';
		if (i < 2)
			goto err_sl;

		bufp = (unsigned char *)buf;
		if (first) {
			first = 0;
			if ((bufp[0] == '0') && (buf[1] == '0')) {
				bufp += 2;
				i -= 2;
			}
		}
		k = 0;
		if (i % 2 != 0) {
			ASN1error(ASN1_R_ODD_NUMBER_OF_CHARS);
			goto err;
		}
		i /= 2;
		if (num + i > slen) {
			sp = realloc(s, num + i);
			if (sp == NULL) {
				ASN1error(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			s = sp;
			slen = num + i;
		}
		for (j = 0; j < i; j++, k += 2) {
			for (n = 0; n < 2; n++) {
				m = bufp[k + n];
				if ((m >= '0') && (m <= '9'))
					m -= '0';
				else if ((m >= 'a') && (m <= 'f'))
					m = m - 'a' + 10;
				else if ((m >= 'A') && (m <= 'F'))
					m = m - 'A' + 10;
				else {
					ASN1error(ASN1_R_NON_HEX_CHARACTERS);
					goto err;
				}
				s[num + j] <<= 4;
				s[num + j] |= m;
			}
		}
		num += i;
		if (again)
			bufsize = BIO_gets(bp, buf, size);
		else
			break;
	}
	bs->length = num;
	bs->data = s;
	return (1);

 err_sl:
	ASN1error(ASN1_R_SHORT_LINE);
 err:
	free(s);
	return (ret);
}
LCRYPTO_ALIAS(a2i_ASN1_ENUMERATED);

int
c2i_ASN1_ENUMERATED_cbs(ASN1_ENUMERATED **out_aenum, CBS *cbs)
{
	ASN1_ENUMERATED *aenum = NULL;

	if (out_aenum == NULL)
		return 0;

	if (*out_aenum != NULL) {
		ASN1_INTEGER_free(*out_aenum);
		*out_aenum = NULL;
	}

	if (!c2i_ASN1_INTEGER_cbs((ASN1_INTEGER **)&aenum, cbs))
		return 0;

	aenum->type = V_ASN1_ENUMERATED | (aenum->type & V_ASN1_NEG);
	*out_aenum = aenum;

	return 1;
}

int
i2d_ASN1_ENUMERATED(ASN1_ENUMERATED *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_ENUMERATED_it);
}
LCRYPTO_ALIAS(i2d_ASN1_ENUMERATED);

ASN1_ENUMERATED *
d2i_ASN1_ENUMERATED(ASN1_ENUMERATED **a, const unsigned char **in, long len)
{
	return (ASN1_ENUMERATED *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_ENUMERATED_it);
}
LCRYPTO_ALIAS(d2i_ASN1_ENUMERATED);
