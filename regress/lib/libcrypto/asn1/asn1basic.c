/* $OpenBSD: asn1basic.c,v 1.16 2024/02/04 13:07:02 tb Exp $ */
/*
 * Copyright (c) 2017, 2021 Joel Sing <jsing@openbsd.org>
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

#include <openssl/asn1.h>
#include <openssl/err.h>

#include <err.h>
#include <stdio.h>
#include <string.h>

#include "asn1_local.h"

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	fprintf(stderr, "\n");
}

static int
asn1_compare_bytes(const char *label, const unsigned char *d1, int len1,
    const unsigned char *d2, int len2)
{
	if (len1 != len2) {
		fprintf(stderr, "FAIL: %s - byte lengths differ "
		    "(%d != %d)\n", label, len1, len2);
		fprintf(stderr, "Got:\n");
		hexdump(d1, len1);
		fprintf(stderr, "Want:\n");
		hexdump(d2, len2);
		return 0;
	}
	if (memcmp(d1, d2, len1) != 0) {
		fprintf(stderr, "FAIL: %s - bytes differ\n", label);
		fprintf(stderr, "Got:\n");
		hexdump(d1, len1);
		fprintf(stderr, "Want:\n");
		hexdump(d2, len2);
		return 0;
	}
	return 1;
}

const uint8_t asn1_bit_string_primitive[] = {
	0x03, 0x07,
	0x04, 0x0a, 0x3b, 0x5f, 0x29, 0x1c, 0xd0,
};

static int
asn1_bit_string_test(void)
{
	uint8_t bs[] = {0x0a, 0x3b, 0x5f, 0x29, 0x1c, 0xd0};
	ASN1_BIT_STRING *abs;
	uint8_t *p = NULL, *pp;
	const uint8_t *q;
	int bit, i, len;
	int failed = 1;

	if ((abs = ASN1_BIT_STRING_new()) == NULL) {
		fprintf(stderr, "FAIL: ASN1_BIT_STRING_new() == NULL\n");
		goto failed;
	}
	if (!ASN1_BIT_STRING_set(abs, bs, sizeof(bs))) {
		fprintf(stderr, "FAIL: failed to set bit string\n");
		goto failed;
	}

	if ((len = i2d_ASN1_BIT_STRING(abs, NULL)) < 0) {
		fprintf(stderr, "FAIL: i2d_ASN1_BIT_STRING with NULL\n");
		goto failed;
	}
	if ((p = malloc(len)) == NULL)
		errx(1, "malloc");
	memset(p, 0xbd, len);
	pp = p;
	if ((i2d_ASN1_BIT_STRING(abs, &pp)) != len) {
		fprintf(stderr, "FAIL: i2d_ASN1_BIT_STRING\n");
		goto failed;
	}
	if (!asn1_compare_bytes("BIT_STRING", p, len, asn1_bit_string_primitive,
	    sizeof(asn1_bit_string_primitive)))
		goto failed;
	if (pp != p + len) {
		fprintf(stderr, "FAIL: i2d_ASN1_BIT_STRING pp = %p, want %p\n",
		    pp, p + len);
		goto failed;
	}

	/* Test primitive decoding. */
	q = p;
	if (d2i_ASN1_BIT_STRING(&abs, &q, len) == NULL) {
		fprintf(stderr, "FAIL: d2i_ASN1_BIT_STRING primitive\n");
		goto failed;
	}
	if (!asn1_compare_bytes("BIT_STRING primitive data", abs->data, abs->length,
	    bs, sizeof(bs)))
		goto failed;
	if (q != p + len) {
		fprintf(stderr, "FAIL: d2i_ASN1_BIT_STRING q = %p, want %p\n",
		    q, p + len);
		goto failed;
	}

	/* Test ASN1_BIT_STRING_get_bit(). */
	for (i = 0; i < ((int)sizeof(bs) * 8); i++) {
		bit = (bs[i / 8] >> (7 - i % 8)) & 1;

		if (ASN1_BIT_STRING_get_bit(abs, i) != bit) {
			fprintf(stderr, "FAIL: ASN1_BIT_STRING_get_bit(_, %d) "
			    "= %d, want %d\n", i,
			    ASN1_BIT_STRING_get_bit(abs, i), bit);
			goto failed;
		}
	}

	/* Test ASN1_BIT_STRING_set_bit(). */
	for (i = 0; i < ((int)sizeof(bs) * 8); i++) {
		if (!ASN1_BIT_STRING_set_bit(abs, i, 1)) {
			fprintf(stderr, "FAIL: ASN1_BIT_STRING_set_bit 1\n");
			goto failed;
		}
	}
	for (i = ((int)sizeof(bs) * 8) - 1; i >= 0; i--) {
		bit = (bs[i / 8] >> (7 - i % 8)) & 1;
		if (bit == 1)
			continue;
		if (!ASN1_BIT_STRING_set_bit(abs, i, 0)) {
			fprintf(stderr, "FAIL: ASN1_BIT_STRING_set_bit\n");
			goto failed;
		}
	}

	if ((i2d_ASN1_BIT_STRING(abs, NULL)) != len) {
		fprintf(stderr, "FAIL: i2d_ASN1_BIT_STRING\n");
		goto failed;
	}

	memset(p, 0xbd, len);
	pp = p;
	if ((i2d_ASN1_BIT_STRING(abs, &pp)) != len) {
		fprintf(stderr, "FAIL: i2d_ASN1_BIT_STRING\n");
		goto failed;
	}

	if (!asn1_compare_bytes("BIT_STRING set", p, len, asn1_bit_string_primitive,
	    sizeof(asn1_bit_string_primitive)))
		goto failed;

	failed = 0;

 failed:
	ASN1_BIT_STRING_free(abs);
	free(p);

	return failed;
}

const uint8_t asn1_boolean_false[] = {
	0x01, 0x01, 0x00,
};
const uint8_t asn1_boolean_true[] = {
	0x01, 0x01, 0x01,
};

static int
asn1_boolean_test(void)
{
	uint8_t *p = NULL, *pp;
	const uint8_t *q;
	int len;
	int failed = 1;

	if ((len = i2d_ASN1_BOOLEAN(0, NULL)) < 0) {
		fprintf(stderr, "FAIL: i2d_ASN1_BOOLEAN false with NULL\n");
		goto failed;
	}
	if ((p = malloc(len)) == NULL)
		errx(1, "calloc");
	memset(p, 0xbd, len);
	pp = p;
	if ((i2d_ASN1_BOOLEAN(0, &pp)) != len) {
		fprintf(stderr, "FAIL: i2d_ASN1_BOOLEAN false\n");
		goto failed;
	}
	if (pp != p + len) {
		fprintf(stderr, "FAIL: i2d_ASN1_BOOLEAN pp = %p, want %p\n",
		    pp, p + len);
		goto failed;
	}

	if (!asn1_compare_bytes("BOOLEAN false", p, len, asn1_boolean_false,
	    sizeof(asn1_boolean_false)))
		goto failed;

	q = p;
	if (d2i_ASN1_BOOLEAN(NULL, &q, len) != 0) {
		fprintf(stderr, "FAIL: BOOLEAN false did not decode to 0\n");
		goto failed;
	}
	if (q != p + len) {
		fprintf(stderr, "FAIL: d2i_ASN1_BOOLEAN q = %p, want %p\n",
		    q, p + len);
		goto failed;
	}

	free(p);
	p = NULL;

	if ((len = i2d_ASN1_BOOLEAN(1, NULL)) < 0) {
		fprintf(stderr, "FAIL: i2d_ASN1_BOOLEAN true with NULL\n");
		goto failed;
	}
	if ((p = calloc(1, len)) == NULL)
		errx(1, "calloc");
	pp = p;
	if ((i2d_ASN1_BOOLEAN(1, &pp)) != len) {
		fprintf(stderr, "FAIL: i2d_ASN1_BOOLEAN true\n");
		goto failed;
	}
	if (pp != p + len) {
		fprintf(stderr, "FAIL: i2d_ASN1_BOOLEAN pp = %p, want %p\n",
		    pp, p + len);
		goto failed;
	}

	if (!asn1_compare_bytes("BOOLEAN true", p, len, asn1_boolean_true,
	    sizeof(asn1_boolean_true)))
		goto failed;

	q = p;
	if (d2i_ASN1_BOOLEAN(NULL, &q, len) != 1) {
		fprintf(stderr, "FAIL: BOOLEAN true did not decode to 1\n");
		goto failed;
	}
	if (q != p + len) {
		fprintf(stderr, "FAIL: d2i_ASN1_BOOLEAN q = %p, want %p\n",
		    q, p + len);
		goto failed;
	}

	failed = 0;

 failed:
	free(p);

	return failed;
}

struct asn1_integer_test {
	long value;
	uint8_t content[64];
	size_t content_len;
	int content_neg;
	uint8_t der[64];
	size_t der_len;
	int want_error;
};

struct asn1_integer_test asn1_integer_tests[] = {
	{
		.value = 0,
		.content = {0x00},
		.content_len = 1,
		.der = {0x02, 0x01, 0x00},
		.der_len = 3,
	},
	{
		.value = 1,
		.content = {0x01},
		.content_len = 1,
		.der = {0x02, 0x01, 0x01},
		.der_len = 3,
	},
	{
		.value = -1,
		.content = {0x01},
		.content_len = 1,
		.content_neg = 1,
		.der = {0x02, 0x01, 0xff},
		.der_len = 3,
	},
	{
		.value = 127,
		.content = {0x7f},
		.content_len = 1,
		.der = {0x02, 0x01, 0x7f},
		.der_len = 3,
	},
	{
		.value = -127,
		.content = {0x7f},
		.content_len = 1,
		.content_neg = 1,
		.der = {0x02, 0x01, 0x81},
		.der_len = 3,
	},
	{
		.value = 128,
		.content = {0x80},
		.content_len = 1,
		.der = {0x02, 0x02, 0x00, 0x80},
		.der_len = 4,
	},
	{
		.value = -128,
		.content = {0x80},
		.content_len = 1,
		.content_neg = 1,
		.der = {0x02, 0x01, 0x80},
		.der_len = 3,
	},
	{
		/* 2^64 */
		.content = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		.content_len = 9,
		.der = {0x02, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		.der_len = 11,
	},
	{
		/* -2^64 */
		.content = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		.content_len = 9,
		.content_neg = 1,
		.der = {0x02, 0x09, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		.der_len = 11,
	},
	{
		/* Invalid length. */
		.der = {0x02, 0x00},
		.der_len = 2,
		.want_error = 1,
	},
	{
		/* Invalid padding. */
		.der = {0x02, 0x09, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		.der_len = 11,
		.want_error = 1,
	},
	{
		/* Invalid padding. */
		.der = {0x02, 0x09, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		.der_len = 11,
		.want_error = 1,
	},
	{
		/* Invalid encoding (constructed with definite length). */
		.der = {0x22, 0x03, 0x02, 0x01, 0x01},
		.der_len = 5,
		.want_error = 1,
	},
	{
		/* Invalid encoding (constructed with indefinite length). */
		.der = {0x22, 0x80, 0x02, 0x01, 0x01, 0x00, 0x00},
		.der_len = 7,
		.want_error = 1,
	},
};

#define N_ASN1_INTEGER_TESTS \
    (sizeof(asn1_integer_tests) / sizeof(*asn1_integer_tests))

static int
asn1_integer_set_test(struct asn1_integer_test *ait)
{
	ASN1_INTEGER *aint = NULL;
	uint8_t *p = NULL, *pp;
	int len;
	int failed = 1;

	if ((aint = ASN1_INTEGER_new()) == NULL) {
		fprintf(stderr, "FAIL: ASN1_INTEGER_new() == NULL\n");
		goto failed;
	}
	if (!ASN1_INTEGER_set(aint, ait->value)) {
		fprintf(stderr, "FAIL: ASN1_INTEGER_(%ld) failed\n",
		    ait->value);
		goto failed;
	}
	if (ait->value != 0 &&
	    !asn1_compare_bytes("INTEGER set", aint->data, aint->length,
	    ait->content, ait->content_len))
		goto failed;
	if (ait->content_neg && aint->type != V_ASN1_NEG_INTEGER) {
		fprintf(stderr, "FAIL: Not V_ASN1_NEG_INTEGER\n");
		goto failed;
	}
	if (ASN1_INTEGER_get(aint) != ait->value) {
		fprintf(stderr, "FAIL: ASN1_INTEGER_get() = %ld, want %ld\n",
		    ASN1_INTEGER_get(aint), ait->value);
		goto failed;
	}
	if ((len = i2d_ASN1_INTEGER(aint, NULL)) < 0) {
		fprintf(stderr, "FAIL: i2d_ASN1_INTEGER() failed\n");
		goto failed;
	}
	if ((p = malloc(len)) == NULL)
		errx(1, "malloc");
	memset(p, 0xbd, len);
	pp = p;
	if ((len = i2d_ASN1_INTEGER(aint, &pp)) < 0) {
		fprintf(stderr, "FAIL: i2d_ASN1_INTEGER() failed\n");
		goto failed;
	}
	if (!asn1_compare_bytes("INTEGER set", p, len, ait->der,
	    ait->der_len))
		goto failed;

	failed = 0;

 failed:
	ASN1_INTEGER_free(aint);
	free(p);

	return failed;
}

static int
asn1_integer_content_test(struct asn1_integer_test *ait)
{
	ASN1_INTEGER *aint = NULL;
	uint8_t *p = NULL, *pp;
	int len;
	int failed = 1;

	if ((aint = ASN1_INTEGER_new()) == NULL) {
		fprintf(stderr, "FAIL: ASN1_INTEGER_new() == NULL\n");
		goto failed;
	}
	if ((aint->data = malloc(ait->content_len)) == NULL)
		errx(1, "malloc");
	memcpy(aint->data, ait->content, ait->content_len);
	aint->length = ait->content_len;
	if (ait->content_neg)
		aint->type = V_ASN1_NEG_INTEGER;

	if ((len = i2d_ASN1_INTEGER(aint, NULL)) < 0) {
		fprintf(stderr, "FAIL: i2d_ASN1_INTEGER() failed\n");
		goto failed;
	}
	if ((p = malloc(len)) == NULL)
		errx(1, "malloc");
	memset(p, 0xbd, len);
	pp = p;
	if ((len = i2d_ASN1_INTEGER(aint, &pp)) < 0) {
		fprintf(stderr, "FAIL: i2d_ASN1_INTEGER() failed\n");
		goto failed;
	}
	if (!asn1_compare_bytes("INTEGER content", p, len, ait->der,
	    ait->der_len))
		goto failed;
	if (pp != p + len) {
		fprintf(stderr, "FAIL: i2d_ASN1_INTEGER pp = %p, want %p\n",
		    pp, p + len);
		goto failed;
	}

	failed = 0;

 failed:
	ASN1_INTEGER_free(aint);
	free(p);

	return failed;
}

static int
asn1_integer_decode_test(struct asn1_integer_test *ait)
{
	ASN1_INTEGER *aint = NULL;
	const uint8_t *q;
	int failed = 1;

	q = ait->der;
	if (d2i_ASN1_INTEGER(&aint, &q, ait->der_len) != NULL) {
		if (ait->want_error != 0) {
			fprintf(stderr, "FAIL: INTEGER decoded when it should "
			    "have failed\n");
			goto failed;
		}
		if (!asn1_compare_bytes("INTEGER content", aint->data,
		    aint->length, ait->content, ait->content_len))
			goto failed;
		if (q != ait->der + ait->der_len) {
			fprintf(stderr, "FAIL: d2i_ASN1_INTEGER q = %p, want %p\n",
			    q, ait->der + ait->der_len);
			goto failed;
		}
	} else if (ait->want_error == 0) {
		fprintf(stderr, "FAIL: INTEGER failed to decode\n");
		ERR_print_errors_fp(stderr);
		goto failed;
	}

	failed = 0;

 failed:
	ASN1_INTEGER_free(aint);

	return failed;
}

static int
asn1_integer_set_val_test(void)
{
	ASN1_INTEGER *aint = NULL;
	uint64_t uval;
	int64_t val;
	int failed = 1;

	if ((aint = ASN1_INTEGER_new()) == NULL) {
		fprintf(stderr, "FAIL: ASN1_INTEGER_new() == NULL\n");
		goto failed;
	}

	if (!ASN1_INTEGER_set_uint64(aint, 0)) {
		fprintf(stderr, "FAIL: ASN_INTEGER_set_uint64() failed with "
		    "0\n");
		goto failed;
	}
	if (!ASN1_INTEGER_get_uint64(&uval, aint)) {
		fprintf(stderr, "FAIL: ASN_INTEGER_get_uint64() failed with "
		    "0\n");
		goto failed;
	}
	if (uval != 0) {
		fprintf(stderr, "FAIL: uval != 0\n");
		goto failed;
	}

	if (!ASN1_INTEGER_set_uint64(aint, UINT64_MAX)) {
		fprintf(stderr, "FAIL: ASN_INTEGER_set_uint64() failed with "
		    "UINT64_MAX\n");
		goto failed;
	}
	if (!ASN1_INTEGER_get_uint64(&uval, aint)) {
		fprintf(stderr, "FAIL: ASN_INTEGER_get_uint64() failed with "
		    "UINT64_MAX\n");
		goto failed;
	}
	if (uval != UINT64_MAX) {
		fprintf(stderr, "FAIL: uval != UINT64_MAX\n");
		goto failed;
	}
	if (ASN1_INTEGER_get_int64(&val, aint)) {
		fprintf(stderr, "FAIL: ASN_INTEGER_get_int64() succeeded "
		    "with UINT64_MAX\n");
		goto failed;
	}

	if (!ASN1_INTEGER_set_int64(aint, INT64_MIN)) {
		fprintf(stderr, "FAIL: ASN_INTEGER_set_int64() failed with "
		    "INT64_MIN\n");
		goto failed;
	}
	if (!ASN1_INTEGER_get_int64(&val, aint)) {
		fprintf(stderr, "FAIL: ASN_INTEGER_get_int64() failed with "
		    "INT64_MIN\n");
		goto failed;
	}
	if (val != INT64_MIN) {
		fprintf(stderr, "FAIL: val != INT64_MIN\n");
		goto failed;
	}
	if (ASN1_INTEGER_get_uint64(&uval, aint)) {
		fprintf(stderr, "FAIL: ASN_INTEGER_get_uint64() succeeded "
		    "with INT64_MIN\n");
		goto failed;
	}

	if (!ASN1_INTEGER_set_int64(aint, INT64_MAX)) {
		fprintf(stderr, "FAIL: ASN_INTEGER_set_int64() failed with "
		    "INT64_MAX\n");
		goto failed;
	}
	if (!ASN1_INTEGER_get_int64(&val, aint)) {
		fprintf(stderr, "FAIL: ASN_INTEGER_get_int64() failed with "
		    "INT64_MAX\n");
		goto failed;
	}
	if (val != INT64_MAX) {
		fprintf(stderr, "FAIL: ASN_INTEGER_get_int64() failed with "
		    "INT64_MAX\n");
		goto failed;
	}
	if (!ASN1_INTEGER_get_uint64(&uval, aint)) {
		fprintf(stderr, "FAIL: ASN_INTEGER_get_uint64() failed with "
		    "INT64_MAX\n");
		goto failed;
	}
	if (uval != INT64_MAX) {
		fprintf(stderr, "FAIL: uval != INT64_MAX\n");
		goto failed;
	}

	failed = 0;

 failed:
	ASN1_INTEGER_free(aint);

	return failed;
}

static int
asn1_integer_cmp_test(void)
{
	ASN1_INTEGER *a = NULL, *b = NULL;
	int failed = 1;

	if ((a = ASN1_INTEGER_new()) == NULL)
		goto failed;
	if ((b = ASN1_INTEGER_new()) == NULL)
		goto failed;

	if (ASN1_INTEGER_cmp(a, b) != 0) {
		fprintf(stderr, "FAIL: INTEGER 0 == 0");
		goto failed;
	}

	if (!ASN1_INTEGER_set(b, 1)) {
		fprintf(stderr, "FAIL: failed to set INTEGER");
		goto failed;
	}
	if (ASN1_INTEGER_cmp(a, b) >= 0) {
		fprintf(stderr, "FAIL: INTEGER 0 < 1");
		goto failed;
	}
	if (ASN1_INTEGER_cmp(b, a) <= 0) {
		fprintf(stderr, "FAIL: INTEGER 1 > 0");
		goto failed;
	}

	if (!ASN1_INTEGER_set(b, -1)) {
		fprintf(stderr, "FAIL: failed to set INTEGER");
		goto failed;
	}
	if (ASN1_INTEGER_cmp(a, b) <= 0) {
		fprintf(stderr, "FAIL: INTEGER 0 > -1");
		goto failed;
	}
	if (ASN1_INTEGER_cmp(b, a) >= 0) {
		fprintf(stderr, "FAIL: INTEGER -1 < 0");
		goto failed;
	}

	if (!ASN1_INTEGER_set(a, 1)) {
		fprintf(stderr, "FAIL: failed to set INTEGER");
		goto failed;
	}
	if (ASN1_INTEGER_cmp(a, b) <= 0) {
		fprintf(stderr, "FAIL: INTEGER 1 > -1");
		goto failed;
	}
	if (ASN1_INTEGER_cmp(b, a) >= 0) {
		fprintf(stderr, "FAIL: INTEGER -1 < 1");
		goto failed;
	}

	if (!ASN1_INTEGER_set(b, 1)) {
		fprintf(stderr, "FAIL: failed to set INTEGER");
		goto failed;
	}
	if (ASN1_INTEGER_cmp(a, b) != 0) {
		fprintf(stderr, "FAIL: INTEGER 1 == 1");
		goto failed;
	}

	failed = 0;

 failed:
	ASN1_INTEGER_free(a);
	ASN1_INTEGER_free(b);

	return failed;
}

static int
asn1_integer_null_data_test(void)
{
	const uint8_t der[] = {0x02, 0x01, 0x00};
	ASN1_INTEGER *aint = NULL;
	uint8_t *p = NULL, *pp;
	int len;
	int failed = 1;

	if ((aint = ASN1_INTEGER_new()) == NULL) {
		fprintf(stderr, "FAIL: ASN1_INTEGER_new() == NULL\n");
		goto failed;
	}
	if ((len = i2d_ASN1_INTEGER(aint, NULL)) < 0) {
		fprintf(stderr, "FAIL: i2d_ASN1_INTEGER() failed\n");
		goto failed;
	}
	if ((p = calloc(1, len)) == NULL)
		errx(1, "calloc");
	pp = p;
	if ((len = i2d_ASN1_INTEGER(aint, &pp)) < 0) {
		fprintf(stderr, "FAIL: i2d_ASN1_INTEGER() failed\n");
		goto failed;
	}
	if (!asn1_compare_bytes("INTEGER NULL data", p, len, der, sizeof(der)))
		goto failed;

	failed = 0;

 failed:
	ASN1_INTEGER_free(aint);
	free(p);

	return failed;
}

static int
asn1_integer_test(void)
{
	struct asn1_integer_test *ait;
	int failed = 0;
	size_t i;

	for (i = 0; i < N_ASN1_INTEGER_TESTS; i++) {
		ait = &asn1_integer_tests[i];
		if (ait->content_len > 0 && ait->content_len <= 4)
			failed |= asn1_integer_set_test(ait);
		if (ait->content_len > 0)
			failed |= asn1_integer_content_test(ait);
		failed |= asn1_integer_decode_test(ait);
	}

	failed |= asn1_integer_cmp_test();
	failed |= asn1_integer_null_data_test();
	failed |= asn1_integer_set_val_test();

	return failed;
}

static const struct asn1_string_new_test {
	const char *name;
	ASN1_STRING *(*new)(void);
	void (*free)(ASN1_STRING *);
	int type;
	long flags;
} asn1_string_new_tests[] = {
	{
		.name = "ASN1_STRING",
		.new = ASN1_STRING_new,
		.free = ASN1_STRING_free,
		.type = V_ASN1_OCTET_STRING,
	},
	{
		.name = "ASN1_OCTET_STRING",
		.new = ASN1_OCTET_STRING_new,
		.free = ASN1_OCTET_STRING_free,
		.type = V_ASN1_OCTET_STRING,
	},
	{
		.name = "ASN1_BIT_STRING",
		.new = ASN1_BIT_STRING_new,
		.free = ASN1_BIT_STRING_free,
		.type = V_ASN1_BIT_STRING,
	},
	{
		.name = "ASN1_INTEGER",
		.new = ASN1_INTEGER_new,
		.free = ASN1_INTEGER_free,
		.type = V_ASN1_INTEGER,
	},
	{
		.name = "ASN1_ENUMERATED",
		.new = ASN1_ENUMERATED_new,
		.free = ASN1_ENUMERATED_free,
		.type = V_ASN1_ENUMERATED,
	},
	{
		.name = "ASN1_UTF8STRING",
		.new = ASN1_UTF8STRING_new,
		.free = ASN1_UTF8STRING_free,
		.type = V_ASN1_UTF8STRING,
	},
	{
		.name = "ASN1_IA5STRING",
		.new = ASN1_IA5STRING_new,
		.free = ASN1_IA5STRING_free,
		.type = V_ASN1_IA5STRING,
	},
	{
		.name = "ASN1_UNIVERSALSTRING",
		.new = ASN1_UNIVERSALSTRING_new,
		.free = ASN1_UNIVERSALSTRING_free,
		.type = V_ASN1_UNIVERSALSTRING,
	},
	{
		.name = "ASN1_BMPSTRING",
		.new = ASN1_BMPSTRING_new,
		.free = ASN1_BMPSTRING_free,
		.type = V_ASN1_BMPSTRING,
	},
	{
		.name = "ASN1_GENERALSTRING",
		.new = ASN1_GENERALSTRING_new,
		.free = ASN1_GENERALSTRING_free,
		.type = V_ASN1_GENERALSTRING,
	},
	{
		.name = "ASN1_T61STRING",
		.new = ASN1_T61STRING_new,
		.free = ASN1_T61STRING_free,
		.type = V_ASN1_T61STRING,
	},
	{
		.name = "ASN1_VISIBLESTRING",
		.new = ASN1_VISIBLESTRING_new,
		.free = ASN1_VISIBLESTRING_free,
		.type = V_ASN1_VISIBLESTRING,
	},
	{
		.name = "ASN1_PRINTABLESTRING",
		.new = ASN1_PRINTABLESTRING_new,
		.free = ASN1_PRINTABLESTRING_free,
		.type = V_ASN1_PRINTABLESTRING,
	},
	{
		.name = "ASN1_PRINTABLE",
		.new = ASN1_PRINTABLE_new,
		.free = ASN1_PRINTABLE_free,
		.type = V_ASN1_UNDEF,
		.flags = ASN1_STRING_FLAG_MSTRING,
	},
	{
		.name = "DIRECTORYSTRING",
		.new = DIRECTORYSTRING_new,
		.free = DIRECTORYSTRING_free,
		.type = V_ASN1_UNDEF,
		.flags = ASN1_STRING_FLAG_MSTRING,
	},
	{
		.name = "DISPLAYTEXT",
		.new = DISPLAYTEXT_new,
		.free = DISPLAYTEXT_free,
		.type = V_ASN1_UNDEF,
		.flags = ASN1_STRING_FLAG_MSTRING,
	},
	{
		.name = "ASN1_GENERALIZEDTIME",
		.new = ASN1_GENERALIZEDTIME_new,
		.free = ASN1_GENERALIZEDTIME_free,
		.type = V_ASN1_GENERALIZEDTIME,
	},
	{
		.name = "ASN1_UTCTIME",
		.new = ASN1_UTCTIME_new,
		.free = ASN1_UTCTIME_free,
		.type = V_ASN1_UTCTIME,
	},
	{
		.name = "ASN1_TIME",
		.new = ASN1_TIME_new,
		.free = ASN1_TIME_free,
		.type = V_ASN1_UNDEF,
		.flags = ASN1_STRING_FLAG_MSTRING,
	},
};

#define N_ASN1_STRING_NEW_TESTS \
    (sizeof(asn1_string_new_tests) / sizeof(asn1_string_new_tests[0]))

static int
asn1_string_new_test(void)
{
	size_t i;
	ASN1_STRING *astr = NULL;
	int failed = 1;

	for (i = 0; i < N_ASN1_STRING_NEW_TESTS; i++) {
		const struct asn1_string_new_test *asnt = &asn1_string_new_tests[i];

		if ((astr = asnt->new()) == NULL) {
			fprintf(stderr, "%s_new() failed\n", asnt->name);
			goto err;
		}
		if (ASN1_STRING_type(astr) != asnt->type) {
			fprintf(stderr, "%s type: want %d, got %d\n",
			    asnt->name, asnt->type, ASN1_STRING_type(astr));
			goto err;
		}
		if (ASN1_STRING_data(astr) != NULL) {
			fprintf(stderr, "%s data != NULL\n", asnt->name);
			goto err;
		}
		if (ASN1_STRING_get0_data(astr) != NULL) {
			fprintf(stderr, "%s data != NULL\n", asnt->name);
			goto err;
		}
		if (ASN1_STRING_length(astr) != 0) {
			fprintf(stderr, "%s length %d != 0\n", asnt->name,
			    ASN1_STRING_length(astr));
			goto err;
		}
		ASN1_STRING_length_set(astr, 20);
		if (ASN1_STRING_length(astr) != 20) {
			fprintf(stderr, "%s length %d != 20\n", asnt->name,
			    ASN1_STRING_length(astr));
			goto err;
		}
		astr->flags |= ASN1_STRING_FLAG_NDEF;
		if (astr->flags != (asnt->flags | ASN1_STRING_FLAG_NDEF)) {
			fprintf(stderr, "%s flags: %lx\n", asnt->name,
			    astr->flags);
			goto err;
		}
		/* ASN1_STRING_set0() clears ASN1_STRING_FLAG_NDEF. */
		ASN1_STRING_set0(astr, NULL, 0);
		if (astr->flags != asnt->flags) {
			fprintf(stderr, "%s flags: %lx != %lx\n", asnt->name,
			    astr->flags, asnt->flags);
			goto err;
		}
		asnt->free(astr);
		astr = NULL;

		if ((astr = ASN1_STRING_type_new(asnt->type)) == NULL) {
			fprintf(stderr, "ASN1_STRING_type_new(%s) failed\n",
			    asnt->name);
			goto err;
		}
		if (ASN1_STRING_type(astr) != asnt->type) {
			fprintf(stderr, "%s type: want %d, got %d\n",
			    asnt->name, asnt->type, ASN1_STRING_type(astr));
			goto err;
		}
		if (ASN1_STRING_data(astr) != NULL) {
			fprintf(stderr, "%s data != NULL\n", asnt->name);
			goto err;
		}
		/* ASN1_STRING_type_new() does not set flags. */
		if (astr->flags != 0) {
			fprintf(stderr, "%s flags %lx\n", asnt->name,
			    astr->flags);
			goto err;
		}
		asnt->free(astr);
		astr = NULL;

	}

	failed = 0;

 err:
	ASN1_STRING_free(astr);

	return failed;
}

static char *comparison_str = "mystring";

static int
asn1_string_cmp_test(void)
{
	ASN1_STRING *a = NULL, *b = NULL;
	int got, want;
	int failed = 1;

	if ((got = ASN1_STRING_cmp(NULL, NULL)) != -1) {
		fprintf(stderr, "ASN1_STRING_cmp(NULL, NULL): %d != -1\n", got);
		goto err;
	}

	if ((a = ASN1_STRING_new()) == NULL) {
		fprintf(stderr, "a = ASN1_STRING_new() failed\n");
		goto err;
	}
	if ((b = ASN1_STRING_type_new(V_ASN1_UTF8STRING)) == NULL) {
		fprintf(stderr, "b = ASN1_STRING_type_new() failed\n");
		goto err;
	}

	if ((got = ASN1_STRING_cmp(a, NULL)) != -1) {
		fprintf(stderr, "ASN1_STRING_cmp(a, NULL): %d != -1\n", got);
		goto err;
	}
	if ((got = ASN1_STRING_cmp(NULL, a)) != -1) {
		fprintf(stderr, "ASN1_STRING_cmp(NULL, a): %d != -1\n", got);
		goto err;
	}

	if (ASN1_STRING_cmp(a, b) >= 0) {
		fprintf(stderr, "V_ASN1_OCTET_STRING >= V_ASN1_UTF8STRING\n");
		goto err;
	}
	want = V_ASN1_UTF8STRING - V_ASN1_OCTET_STRING;
	if ((got = ASN1_STRING_cmp(b, a)) != want) {
		fprintf(stderr, "comparison of octet with utf8 string:"
		    "want %d, got %d\n", want, got);
		goto err;
	}

	ASN1_STRING_set0(a, comparison_str, strlen(comparison_str));
	ASN1_STRING_set0(b, comparison_str, strlen(comparison_str));

	/* Ensure any data set on a or b isn't freed/zeroed. */
	a->flags |= ASN1_STRING_FLAG_NDEF;
	b->flags |= ASN1_STRING_FLAG_NDEF;

	if ((got = ASN1_STRING_cmp(b, a)) != want) {
		fprintf(stderr, "comparison of octet with utf8 string:"
		    "want %d, got %d\n", want, got);
		goto err;
	}

	b->type = V_ASN1_OCTET_STRING;

	if ((got = ASN1_STRING_cmp(a, b)) != 0) {
		fprintf(stderr, "same string on both. want 0, got %d\n", got);
		goto err;
	}

	if (!ASN1_STRING_set(b, "myString", -1)) {
		fprintf(stderr, "ASN1_STRING_set(b) failed\n");
		goto err;
	}

	if ((got = ASN1_STRING_cmp(a, b)) <= 0) {
		fprintf(stderr, "capitalized letter compares larger: got %d\n",
		    got);
		goto err;
	}
	if ((got = ASN1_STRING_cmp(b, a)) >= 0) {
		fprintf(stderr, "capitalized letter is larger 2: %d\n", got);
		goto err;
	}

	ASN1_STRING_length_set(b, 2);

	want = strlen(comparison_str) - 2;

	if ((got = ASN1_STRING_cmp(a, b)) != want) {
		fprintf(stderr, "comparison of a with truncated b: "
		    "want %d, got %d\n", want, got);
		goto err;
	}

	want = -want;

	if ((got = ASN1_STRING_cmp(b, a)) != want) {
		fprintf(stderr, "comparison of truncated b with a: "
		    "want %d, got %d\n", want, got);
		goto err;
	}

	ASN1_STRING_length_set(a, 2);

	if ((got = ASN1_STRING_cmp(a, b)) != 0) {
		fprintf(stderr, "both truncated compared to %d\n", got);
		goto err;
	}

	ASN1_STRING_length_set(a, strlen(comparison_str));

	ASN1_STRING_set0(b, NULL, 0);

	want = strlen(comparison_str);
	if ((got = ASN1_STRING_cmp(a, b)) != want) {
		fprintf(stderr, "comparison of a with zeroed b: "
		    "want %d, got %d\n", want, got);
		goto err;
	}

	ASN1_STRING_set0(b, "", 0);
	b->flags |= ASN1_STRING_FLAG_NDEF;

	if ((got = ASN1_STRING_cmp(a, b)) != want) {
		fprintf(stderr, "comparison of a with zero-length b: "
		    "want %d, got %d\n", want, got);
		goto err;
	}

	ASN1_STRING_set0(a, NULL, 0);
	if ((got = ASN1_STRING_cmp(a, b)) != 0) {
		fprintf(stderr, "comparison of zeroed a with zero-length b: "
		    "want 0, got %d\n", got);
		goto err;
	}
	if ((got = ASN1_STRING_cmp(b, a)) != 0) {
		fprintf(stderr, "comparison of zero-length b with zeroed a: "
		    "want 0, got %d\n", got);
		goto err;
	}

	failed = 0;

 err:
	ASN1_STRING_free(a);
	ASN1_STRING_free(b);

	return failed;
}

static int
asn1_string_test(void)
{
	int failed = 0;

	failed |= asn1_string_new_test();
	failed |= asn1_string_cmp_test();

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= asn1_bit_string_test();
	failed |= asn1_boolean_test();
	failed |= asn1_integer_test();
	failed |= asn1_string_test();

	return (failed);
}
