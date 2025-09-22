/* $OpenBSD: bn_convert.c,v 1.24 2025/05/10 05:54:38 tb Exp $ */
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

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/bio.h>
#include <openssl/buffer.h>

#include "bn_local.h"
#include "bytestring.h"
#include "crypto_internal.h"
#include "err_local.h"

static int bn_dec2bn_cbs(BIGNUM **bnp, CBS *cbs);
static int bn_hex2bn_cbs(BIGNUM **bnp, CBS *cbs);

static const char hex_digits[] = "0123456789ABCDEF";

static int
bn_bn2binpad_internal(const BIGNUM *bn, uint8_t *out, int out_len,
    int little_endian)
{
	uint8_t mask, v;
	BN_ULONG w;
	int i, j;
	int b, n;

	n = BN_num_bytes(bn);

	if (out_len == -1)
		out_len = n;
	if (out_len < n)
		return -1;

	if (bn->dmax == 0) {
		explicit_bzero(out, out_len);
		return out_len;
	}

	mask = 0;
	b = BN_BITS2;
	j = 0;

	for (i = out_len - 1; i >= 0; i--) {
		if (b == BN_BITS2) {
			mask = crypto_ct_lt_mask(j, bn->top);
			w = bn->d[j++ % bn->dmax];
			b = 0;
		}
		out[i] = (w >> b) & mask;
		b += 8;
	}

	if (little_endian) {
		for (i = 0, j = out_len - 1; i < out_len / 2; i++, j--) {
			v = out[i];
			out[i] = out[j];
			out[j] = v;
		}
	}

	return out_len;
}

int
BN_bn2bin(const BIGNUM *bn, unsigned char *to)
{
	return bn_bn2binpad_internal(bn, to, -1, 0);
}
LCRYPTO_ALIAS(BN_bn2bin);

int
BN_bn2binpad(const BIGNUM *bn, unsigned char *to, int to_len)
{
	if (to_len < 0)
		return -1;

	return bn_bn2binpad_internal(bn, to, to_len, 0);
}
LCRYPTO_ALIAS(BN_bn2binpad);

static int
bn_bin2bn_cbs(BIGNUM **bnp, CBS *cbs, int lebin)
{
	BIGNUM *bn = NULL;
	BN_ULONG w;
	uint8_t v;
	int b, i;

	if ((bn = *bnp) == NULL)
		bn = BN_new();
	if (bn == NULL)
		goto err;
	if (!bn_expand_bytes(bn, CBS_len(cbs)))
		goto err;

	b = 0;
	i = 0;
	w = 0;

	while (CBS_len(cbs) > 0) {
		if (lebin) {
			if (!CBS_get_u8(cbs, &v))
				goto err;
		} else {
			if (!CBS_get_last_u8(cbs, &v))
				goto err;
		}

		w |= (BN_ULONG)v << b;
		b += 8;

		if (b == BN_BITS2 || CBS_len(cbs) == 0) {
			b = 0;
			bn->d[i++] = w;
			w = 0;
		}
	}

	bn->neg = 0;
	bn->top = i;

	bn_correct_top(bn);

	*bnp = bn;

	return 1;

 err:
	if (*bnp == NULL)
		BN_free(bn);

	return 0;
}

BIGNUM *
BN_bin2bn(const unsigned char *d, int len, BIGNUM *bn)
{
	CBS cbs;

	if (len < 0)
		return NULL;

	CBS_init(&cbs, d, len);

	if (!bn_bin2bn_cbs(&bn, &cbs, 0))
		return NULL;

	return bn;
}
LCRYPTO_ALIAS(BN_bin2bn);

int
BN_bn2lebinpad(const BIGNUM *bn, unsigned char *to, int to_len)
{
	if (to_len < 0)
		return -1;

	return bn_bn2binpad_internal(bn, to, to_len, 1);
}
LCRYPTO_ALIAS(BN_bn2lebinpad);

BIGNUM *
BN_lebin2bn(const unsigned char *d, int len, BIGNUM *bn)
{
	CBS cbs;

	if (len < 0)
		return NULL;

	CBS_init(&cbs, d, len);

	if (!bn_bin2bn_cbs(&bn, &cbs, 1))
		return NULL;

	return bn;
}
LCRYPTO_ALIAS(BN_lebin2bn);

int
BN_asc2bn(BIGNUM **bnp, const char *s)
{
	CBS cbs, cbs_hex;
	size_t s_len;
	uint8_t v;
	int neg;

	if (bnp != NULL && *bnp != NULL)
		BN_zero(*bnp);

	if (s == NULL)
		return 0;
	if ((s_len = strlen(s)) == 0)
		return 0;

	CBS_init(&cbs, s, s_len);

	/* Handle negative sign. */
	if (!CBS_peek_u8(&cbs, &v))
		return 0;
	if ((neg = (v == '-'))) {
		if (!CBS_skip(&cbs, 1))
			return 0;
	}

	/* Try parsing as hexadecimal with a 0x prefix. */
	CBS_dup(&cbs, &cbs_hex);
	if (!CBS_get_u8(&cbs_hex, &v))
		goto decimal;
	if (v != '0')
		goto decimal;
	if (!CBS_get_u8(&cbs_hex, &v))
		goto decimal;
	if (v != 'X' && v != 'x')
		goto decimal;
	if (bn_hex2bn_cbs(bnp, &cbs_hex) == 0)
		return 0;

	goto done;

 decimal:
	if (bn_dec2bn_cbs(bnp, &cbs) == 0)
		return 0;

 done:
	if (bnp != NULL && *bnp != NULL)
		BN_set_negative(*bnp, neg);

	return 1;
}
LCRYPTO_ALIAS(BN_asc2bn);

char *
BN_bn2dec(const BIGNUM *bn)
{
	int started = 0;
	BIGNUM *tmp = NULL;
	uint8_t *data = NULL;
	size_t data_len = 0;
	uint8_t *s = NULL;
	size_t s_len;
	BN_ULONG v, w;
	uint8_t c;
	CBB cbb;
	CBS cbs;
	int i;

	if (!CBB_init(&cbb, 0))
		goto err;

	if ((tmp = BN_dup(bn)) == NULL)
		goto err;

	/*
	 * Divide the BIGNUM by a large multiple of 10, then break the remainder
	 * into decimal digits. This produces a reversed string of digits,
	 * potentially with leading zeroes.
	 */
	while (!BN_is_zero(tmp)) {
		if ((w = BN_div_word(tmp, BN_DEC_CONV)) == -1)
			goto err;
		for (i = 0; i < BN_DEC_NUM; i++) {
			v = w % 10;
			if (!CBB_add_u8(&cbb, '0' + v))
				goto err;
			w /= 10;
		}
	}
	if (!CBB_finish(&cbb, &data, &data_len))
		goto err;

	if (data_len > SIZE_MAX - 3)
		goto err;
	if (!CBB_init(&cbb, data_len + 3))
		goto err;

	if (BN_is_negative(bn)) {
		if (!CBB_add_u8(&cbb, '-'))
			goto err;
	}

	/* Reverse digits and trim leading zeroes. */
	CBS_init(&cbs, data, data_len);
	while (CBS_len(&cbs) > 0) {
		if (!CBS_get_last_u8(&cbs, &c))
			goto err;
		if (!started && c == '0')
			continue;
		if (!CBB_add_u8(&cbb, c))
			goto err;
		started = 1;
	}

	if (!started) {
		if (!CBB_add_u8(&cbb, '0'))
			goto err;
	}
	if (!CBB_add_u8(&cbb, '\0'))
		goto err;
	if (!CBB_finish(&cbb, &s, &s_len))
		goto err;

 err:
	BN_free(tmp);
	CBB_cleanup(&cbb);
	freezero(data, data_len);

	return s;
}
LCRYPTO_ALIAS(BN_bn2dec);

static int
bn_dec2bn_cbs(BIGNUM **bnp, CBS *cbs)
{
	CBS cbs_digits;
	BIGNUM *bn = NULL;
	int d, neg, num;
	size_t digits = 0;
	BN_ULONG w;
	uint8_t v;

	/* Handle negative sign. */
	if (!CBS_peek_u8(cbs, &v))
		goto err;
	if ((neg = (v == '-'))) {
		if (!CBS_skip(cbs, 1))
			goto err;
	}

	/* Scan to find last decimal digit. */
	CBS_dup(cbs, &cbs_digits);
	while (CBS_len(&cbs_digits) > 0) {
		if (!CBS_get_u8(&cbs_digits, &v))
			goto err;
		if (!isdigit(v))
			break;
		digits++;
	}
	if (digits > INT_MAX / 4)
		goto err;

	num = digits + neg;

	if (bnp == NULL)
		return num;

	if ((bn = *bnp) == NULL)
		bn = BN_new();
	if (bn == NULL)
		goto err;
	if (!bn_expand_bits(bn, digits * 4))
		goto err;

	if ((d = digits % BN_DEC_NUM) == 0)
		d = BN_DEC_NUM;

	w = 0;

	/* Work forwards from most significant digit. */
	while (digits-- > 0) {
		if (!CBS_get_u8(cbs, &v))
			goto err;

		if (v < '0' || v > '9')
			goto err;

		v -= '0';
		w = w * 10 + v;
		d--;

		if (d == 0) {
			if (!BN_mul_word(bn, BN_DEC_CONV))
				goto err;
			if (!BN_add_word(bn, w))
				goto err;

			d = BN_DEC_NUM;
			w = 0;
		}
	}

	bn_correct_top(bn);

	BN_set_negative(bn, neg);

	*bnp = bn;

	return num;

 err:
	if (bnp != NULL && *bnp == NULL)
		BN_free(bn);

	return 0;
}

int
BN_dec2bn(BIGNUM **bnp, const char *s)
{
	size_t s_len;
	CBS cbs;

	if (bnp != NULL && *bnp != NULL)
		BN_zero(*bnp);

	if (s == NULL)
		return 0;
	if ((s_len = strlen(s)) == 0)
		return 0;

	CBS_init(&cbs, s, s_len);

	return bn_dec2bn_cbs(bnp, &cbs);
}
LCRYPTO_ALIAS(BN_dec2bn);

static int
bn_bn2hex_internal(const BIGNUM *bn, int include_sign, int nibbles_only,
    char **out, size_t *out_len)
{
	int started = 0;
	uint8_t *s = NULL;
	size_t s_len = 0;
	BN_ULONG v, w;
	int i, j;
	CBB cbb;
	CBS cbs;
	uint8_t nul;
	int ret = 0;

	*out = NULL;
	*out_len = 0;

	if (!CBB_init(&cbb, 0))
		goto err;

	if (BN_is_negative(bn) && include_sign) {
		if (!CBB_add_u8(&cbb, '-'))
			goto err;
	}
	if (BN_is_zero(bn)) {
		if (!CBB_add_u8(&cbb, '0'))
			goto err;
	}
	for (i = bn->top - 1; i >= 0; i--) {
		w = bn->d[i];
		for (j = BN_BITS2 - 8; j >= 0; j -= 8) {
			v = (w >> j) & 0xff;
			if (!started && v == 0)
				continue;
			if (started || !nibbles_only || (v >> 4) != 0) {
				if (!CBB_add_u8(&cbb, hex_digits[v >> 4]))
					goto err;
			}
			if (!CBB_add_u8(&cbb, hex_digits[v & 0xf]))
				goto err;
			started = 1;
		}
	}
	if (!CBB_add_u8(&cbb, '\0'))
		goto err;
	if (!CBB_finish(&cbb, &s, &s_len))
		goto err;

	/* The length of a C string does not include the terminating NUL. */
	CBS_init(&cbs, s, s_len);
	if (!CBS_get_last_u8(&cbs, &nul))
		goto err;

	*out = (char *)CBS_data(&cbs);
	*out_len = CBS_len(&cbs);
	s = NULL;
	s_len = 0;

	ret = 1;

 err:
	CBB_cleanup(&cbb);
	freezero(s, s_len);

	return ret;
}

int
bn_bn2hex_nosign(const BIGNUM *bn, char **out, size_t *out_len)
{
	return bn_bn2hex_internal(bn, 0, 0, out, out_len);
}

int
bn_bn2hex_nibbles(const BIGNUM *bn, char **out, size_t *out_len)
{
	return bn_bn2hex_internal(bn, 1, 1, out, out_len);
}

char *
BN_bn2hex(const BIGNUM *bn)
{
	char *s;
	size_t s_len;

	if (!bn_bn2hex_internal(bn, 1, 0, &s, &s_len))
		return NULL;

	return s;
}
LCRYPTO_ALIAS(BN_bn2hex);

static int
bn_hex2bn_cbs(BIGNUM **bnp, CBS *cbs)
{
	CBS cbs_digits;
	BIGNUM *bn = NULL;
	int b, i, neg, num;
	size_t digits = 0;
	BN_ULONG w;
	uint8_t v;

	/* Handle negative sign. */
	if (!CBS_peek_u8(cbs, &v))
		goto err;
	if ((neg = (v == '-'))) {
		if (!CBS_skip(cbs, 1))
			goto err;
	}

	/* Scan to find last hexadecimal digit. */
	CBS_dup(cbs, &cbs_digits);
	while (CBS_len(&cbs_digits) > 0) {
		if (!CBS_get_u8(&cbs_digits, &v))
			goto err;
		if (!isxdigit(v))
			break;
		digits++;
	}
	if (digits > INT_MAX / 4)
		goto err;

	num = digits + neg;

	if (bnp == NULL)
		return num;

	if ((bn = *bnp) == NULL)
		bn = BN_new();
	if (bn == NULL)
		goto err;
	if (!bn_expand_bits(bn, digits * 4))
		goto err;

	if (!CBS_get_bytes(cbs, cbs, digits))
		goto err;

	b = 0;
	i = 0;
	w = 0;

	/* Work backwards from least significant digit. */
	while (digits-- > 0) {
		if (!CBS_get_last_u8(cbs, &v))
			goto err;

		if (v >= '0' && v <= '9')
			v -= '0';
		else if (v >= 'a' && v <= 'f')
			v -= 'a' - 10;
		else if (v >= 'A' && v <= 'F')
			v -= 'A' - 10;
		else
			goto err;

		w |= (BN_ULONG)v << b;
		b += 4;

		if (b == BN_BITS2 || digits == 0) {
			b = 0;
			bn->d[i++] = w;
			w = 0;
		}
	}

	bn->top = i;
	bn_correct_top(bn);

	BN_set_negative(bn, neg);

	*bnp = bn;

	return num;

 err:
	if (bnp != NULL && *bnp == NULL)
		BN_free(bn);

	return 0;
}

int
BN_hex2bn(BIGNUM **bnp, const char *s)
{
	size_t s_len;
	CBS cbs;

	if (bnp != NULL && *bnp != NULL)
		BN_zero(*bnp);

	if (s == NULL)
		return 0;
	if ((s_len = strlen(s)) == 0)
		return 0;

	CBS_init(&cbs, s, s_len);

	return bn_hex2bn_cbs(bnp, &cbs);
}
LCRYPTO_ALIAS(BN_hex2bn);

int
BN_bn2mpi(const BIGNUM *bn, unsigned char *d)
{
	uint8_t *out_bin;
	size_t out_len, out_bin_len;
	int bits, bytes;
	int extend;
	CBB cbb, cbb_bin;

	bits = BN_num_bits(bn);
	bytes = (bits + 7) / 8;
	extend = (bits != 0) && (bits % 8 == 0);
	out_bin_len = extend + bytes;
	out_len = 4 + out_bin_len;

	if (d == NULL)
		return out_len;

	if (!CBB_init_fixed(&cbb, d, out_len))
		goto err;
	if (!CBB_add_u32_length_prefixed(&cbb, &cbb_bin))
		goto err;
	if (!CBB_add_space(&cbb_bin, &out_bin, out_bin_len))
		goto err;
	if (BN_bn2binpad(bn, out_bin, out_bin_len) != out_bin_len)
		goto err;
	if (!CBB_finish(&cbb, NULL, NULL))
		goto err;

	if (bn->neg)
		d[4] |= 0x80;

	return out_len;

 err:
	CBB_cleanup(&cbb);

	return -1;
}
LCRYPTO_ALIAS(BN_bn2mpi);

BIGNUM *
BN_mpi2bn(const unsigned char *d, int n, BIGNUM *bn_in)
{
	BIGNUM *bn = bn_in;
	uint32_t mpi_len;
	uint8_t v;
	int neg = 0;
	CBS cbs;

	if (n < 0)
		return NULL;

	CBS_init(&cbs, d, n);

	if (!CBS_get_u32(&cbs, &mpi_len)) {
		BNerror(BN_R_INVALID_LENGTH);
		return NULL;
	}
	if (CBS_len(&cbs) != mpi_len) {
		BNerror(BN_R_ENCODING_ERROR);
		return NULL;
	}
	if (CBS_len(&cbs) > 0) {
		if (!CBS_peek_u8(&cbs, &v))
			return NULL;
		neg = (v >> 7) & 1;
	}

	if (!bn_bin2bn_cbs(&bn, &cbs, 0))
		return NULL;

	if (neg)
		BN_clear_bit(bn, BN_num_bits(bn) - 1);

	BN_set_negative(bn, neg);

	return bn;
}
LCRYPTO_ALIAS(BN_mpi2bn);
