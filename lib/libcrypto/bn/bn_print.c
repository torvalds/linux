/*	$OpenBSD: bn_print.c,v 1.47 2024/03/02 09:18:28 tb Exp $ */

/*
 * Copyright (c) 2023 Theo Buehler <tb@openbsd.org>
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

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <openssl/bio.h>
#include <openssl/bn.h>

#include "bio_local.h"
#include "bn_local.h"
#include "bytestring.h"

static int
bn_print_zero(BIO *bio, const BIGNUM *bn)
{
	if (!BN_is_zero(bn))
		return 0;
	if (BIO_printf(bio, " 0\n") <= 0)
		return 0;
	return 1;
}

static int
bn_print_word(BIO *bio, const BIGNUM *bn)
{
	unsigned long long word;
	const char *neg = "";

	if (BN_is_zero(bn) || BN_num_bytes(bn) > BN_BYTES)
		return 0;

	if (BN_is_negative(bn))
		neg = "-";

	word = BN_get_word(bn);
	if (BIO_printf(bio, " %s%llu (%s0x%llx)\n", neg, word, neg, word) <= 0)
		return 0;

	return 1;
}

static int
bn_print_bignum(BIO *bio, const BIGNUM *bn, int indent)
{
	CBS cbs;
	char *hex = NULL;
	size_t hex_len = 0;
	size_t octets = 0;
	uint8_t hi, lo;
	const char *sep = ":";
	int ret = 0;

	if (BN_num_bytes(bn) <= BN_BYTES)
		goto err;

	/* Secondary indent is 4 spaces, capped at 128. */
	if (indent > 124)
		indent = 124;
	indent += 4;
	if (indent < 0)
		indent = 0;

	if (!bn_bn2hex_nosign(bn, &hex, &hex_len))
		goto err;

	CBS_init(&cbs, hex, hex_len);

	if (BN_is_negative(bn)) {
		if (BIO_printf(bio, " (Negative)") <= 0)
			goto err;
	}

	while (CBS_len(&cbs) > 0) {
		if (!CBS_get_u8(&cbs, &hi))
			goto err;
		if (!CBS_get_u8(&cbs, &lo))
			goto err;
		if (octets++ % 15 == 0) {
			if (BIO_printf(bio, "\n%*s", indent, "") <= 0)
				goto err;
		}
		/* First nibble has the high bit set. Insert leading 0 octet. */
		if (octets == 1 && hi >= '8') {
			if (BIO_printf(bio, "00:") <= 0)
				goto err;
			octets++;
		}
		if (CBS_len(&cbs) == 0)
			sep = "";
		if (BIO_printf(bio, "%c%c%s", tolower(hi), tolower(lo), sep) <= 0)
			goto err;
	}

	if (BIO_printf(bio, "\n") <= 0)
		goto err;

	ret = 1;

 err:
	freezero(hex, hex_len);

	return ret;
}

int
bn_printf(BIO *bio, const BIGNUM *bn, int indent, const char *fmt, ...)
{
	va_list ap;
	int rv;

	if (bn == NULL)
		return 1;

	if (!BIO_indent(bio, indent, 128))
		return 0;

	va_start(ap, fmt);
	rv = BIO_vprintf(bio, fmt, ap);
	va_end(ap);
	if (rv < 0)
		return 0;

	if (BN_is_zero(bn))
		return bn_print_zero(bio, bn);

	if (BN_num_bytes(bn) <= BN_BYTES)
		return bn_print_word(bio, bn);

	return bn_print_bignum(bio, bn, indent);
}

int
BN_print(BIO *bio, const BIGNUM *bn)
{
	char *hex = NULL;
	size_t hex_len = 0;
	int ret = 0;

	if (!bn_bn2hex_nibbles(bn, &hex, &hex_len))
		goto err;
	if (BIO_printf(bio, "%s", hex) <= 0)
		goto err;

	ret = 1;

 err:
	freezero(hex, hex_len);

	return ret;
}
LCRYPTO_ALIAS(BN_print);

int
BN_print_fp(FILE *fp, const BIGNUM *bn)
{
	char *hex = NULL;
	size_t hex_len = 0;
	int ret = 0;

	if (!bn_bn2hex_nibbles(bn, &hex, &hex_len))
		goto err;
	if (fprintf(fp, "%s", hex) < 0)
		goto err;

	ret = 1;

 err:
	freezero(hex, hex_len);

	return ret;
}
LCRYPTO_ALIAS(BN_print_fp);
