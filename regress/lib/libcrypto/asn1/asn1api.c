/* $OpenBSD: asn1api.c,v 1.3 2022/07/09 14:47:42 tb Exp $ */
/*
 * Copyright (c) 2021 Joel Sing <jsing@openbsd.org>
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

const long asn1_tag2bits[] = {
	[0] = 0,
	[1] = 0,
	[2] = 0,
	[3] = B_ASN1_BIT_STRING,
	[4] = B_ASN1_OCTET_STRING,
	[5] = 0,
	[6] = 0,
	[7] = B_ASN1_UNKNOWN,
	[8] = B_ASN1_UNKNOWN,
	[9] = B_ASN1_UNKNOWN,
	[10] = B_ASN1_UNKNOWN,
	[11] = B_ASN1_UNKNOWN,
	[12] = B_ASN1_UTF8STRING,
	[13] = B_ASN1_UNKNOWN,
	[14] = B_ASN1_UNKNOWN,
	[15] = B_ASN1_UNKNOWN,
	[16] = B_ASN1_SEQUENCE,
	[17] = 0,
	[18] = B_ASN1_NUMERICSTRING,
	[19] = B_ASN1_PRINTABLESTRING,
	[20] = B_ASN1_T61STRING,
	[21] = B_ASN1_VIDEOTEXSTRING,
	[22] = B_ASN1_IA5STRING,
	[23] = B_ASN1_UTCTIME,
	[24] = B_ASN1_GENERALIZEDTIME,
	[25] = B_ASN1_GRAPHICSTRING,
	[26] = B_ASN1_ISO64STRING,
	[27] = B_ASN1_GENERALSTRING,
	[28] = B_ASN1_UNIVERSALSTRING,
	[29] = B_ASN1_UNKNOWN,
	[30] = B_ASN1_BMPSTRING,
};

static int
asn1_tag2bit(void)
{
	int failed = 1;
	long bit;
	int i;

	for (i = -3; i <= V_ASN1_NEG + 30; i++) {
		bit = ASN1_tag2bit(i);
		if (i >= 0 && i <= 30) {
			if (bit != asn1_tag2bits[i]) {
				fprintf(stderr, "FAIL: ASN1_tag2bit(%d) = 0x%lx,"
				    " want 0x%lx\n", i, bit, asn1_tag2bits[i]);
				goto failed;
			}
		} else {
			if (bit != 0) {
				fprintf(stderr, "FAIL: ASN1_tag2bit(%d) = 0x%lx,"
				    " want 0x0\n", i, bit);
				goto failed;
			}
		}
	}

	failed = 0;

 failed:
	return failed;
}

static int
asn1_tag2str(void)
{
	int failed = 1;
	const char *s;
	int i;

	for (i = -3; i <= V_ASN1_NEG + 30; i++) {
		if ((s = ASN1_tag2str(i)) == NULL) {
			fprintf(stderr, "FAIL: ASN1_tag2str(%d) returned "
			    "NULL\n", i);
			goto failed;
		}
		if ((i >= 0 && i <= 30) || i == V_ASN1_NEG_INTEGER ||
		    i == V_ASN1_NEG_ENUMERATED) {
			if (strcmp(s, "(unknown)") == 0) {
				fprintf(stderr, "FAIL: ASN1_tag2str(%d) = '%s',"
				    " want tag name\n", i, s);
				goto failed;
			}
		} else {
			if (strcmp(s, "(unknown)") != 0) {
				fprintf(stderr, "FAIL: ASN1_tag2str(%d) = '%s',"
				    " want '(unknown')\n", i, s);
				goto failed;
			}
		}
	}

	failed = 0;

 failed:
	return failed;
}

struct asn1_get_object_test {
	const uint8_t asn1[64];
	size_t asn1_len;
	size_t asn1_hdr_len;
	int want_ret;
	long want_length;
	int want_tag;
	int want_class;
	int want_error;
};

const struct asn1_get_object_test asn1_get_object_tests[] = {
	{
		/* Zero tag and zero length (EOC). */
		.asn1 = {0x00, 0x00},
		.asn1_len = 2,
		.asn1_hdr_len = 2,
		.want_ret = 0x00,
		.want_length = 0,
		.want_tag = 0,
		.want_class = 0,
	},
	{
		/* Boolean with short form length. */
		.asn1 = {0x01, 0x01},
		.asn1_len = 3,
		.asn1_hdr_len = 2,
		.want_ret = 0x00,
		.want_length = 1,
		.want_tag = 1,
		.want_class = 0,
	},
	{
		/* Long form tag. */
		.asn1 = {0x1f, 0x7f, 0x01},
		.asn1_len = 3 + 128,
		.asn1_hdr_len = 3,
		.want_ret = 0x00,
		.want_length = 1,
		.want_tag = 127,
		.want_class = 0,
	},
	{
		/* Long form tag with class application. */
		.asn1 = {0x5f, 0x7f, 0x01},
		.asn1_len = 3 + 128,
		.asn1_hdr_len = 3,
		.want_ret = 0x00,
		.want_length = 1,
		.want_tag = 127,
		.want_class = 1 << 6,
	},
	{
		/* Long form tag with class context-specific. */
		.asn1 = {0x9f, 0x7f, 0x01},
		.asn1_len = 3 + 128,
		.asn1_hdr_len = 3,
		.want_ret = 0x00,
		.want_length = 1,
		.want_tag = 127,
		.want_class = 2 << 6,
	},
	{
		/* Long form tag with class private. */
		.asn1 = {0xdf, 0x7f, 0x01},
		.asn1_len = 3 + 128,
		.asn1_hdr_len = 3,
		.want_ret = 0x00,
		.want_length = 1,
		.want_tag = 127,
		.want_class = 3 << 6,
	},
	{
		/* Long form tag (maximum). */
		.asn1 = {0x1f, 0x87, 0xff, 0xff, 0xff, 0x7f, 0x01},
		.asn1_len = 8,
		.asn1_hdr_len = 7,
		.want_ret = 0x00,
		.want_length = 1,
		.want_tag = 0x7fffffff,
		.want_class = 0,
	},
	{
		/* Long form tag (maximum + 1). */
		.asn1 = {0x1f, 0x88, 0x80, 0x80, 0x80, 0x00, 0x01},
		.asn1_len = 8,
		.asn1_hdr_len = 7,
		.want_ret = 0x80,
		.want_error = ASN1_R_HEADER_TOO_LONG,
	},
	{
		/* OctetString with long form length. */
		.asn1 = {0x04, 0x81, 0x80},
		.asn1_len = 3 + 128,
		.asn1_hdr_len = 3,
		.want_ret = 0x00,
		.want_length = 128,
		.want_tag = 4,
		.want_class = 0,
	},
	{
		/* OctetString with long form length. */
		.asn1 = {0x04, 0x84, 0x7f, 0xff, 0xff, 0xf9},
		.asn1_len = 0x7fffffff,
		.asn1_hdr_len = 6,
		.want_ret = 0x00,
		.want_length = 0x7ffffff9,
		.want_tag = 4,
		.want_class = 0,
	},
	{
		/* Long form tag and long form length. */
		.asn1 = {0x1f, 0x87, 0xff, 0xff, 0xff, 0x7f, 0x84, 0x7f, 0xff, 0xff, 0xf4},
		.asn1_len = 0x7fffffff,
		.asn1_hdr_len = 11,
		.want_ret = 0x00,
		.want_length = 0x7ffffff4,
		.want_tag = 0x7fffffff,
		.want_class = 0,
	},
	{
		/* Constructed OctetString with definite length. */
		.asn1 = {0x24, 0x03},
		.asn1_len = 5,
		.asn1_hdr_len = 2,
		.want_ret = 0x20,
		.want_length = 3,
		.want_tag = 4,
		.want_class = 0,
	},
	{
		/* Constructed OctetString with indefinite length. */
		.asn1 = {0x24, 0x80},
		.asn1_len = 5,
		.asn1_hdr_len = 2,
		.want_ret = 0x21,
		.want_length = 0,
		.want_tag = 4,
		.want_class = 0,
	},
	{
		/* Boolean with indefinite length (invalid). */
		.asn1 = {0x01, 0x80},
		.asn1_len = 3,
		.want_ret = 0x80,
		.want_error = ASN1_R_HEADER_TOO_LONG,
	},
	{
		/* OctetString with insufficient data (only tag). */
		.asn1 = {0x04, 0x04},
		.asn1_len = 1,
		.want_ret = 0x80,
		.want_error = ASN1_R_HEADER_TOO_LONG,
	},
	{
		/* OctetString with insufficient data (missing content). */
		.asn1 = {0x04, 0x04},
		.asn1_len = 2,
		.asn1_hdr_len = 2,
		.want_ret = 0x80,
		.want_length = 4,
		.want_tag = 4,
		.want_class = 0,
		.want_error = ASN1_R_TOO_LONG,
	},
	{
		/* OctetString with insufficient data (partial content). */
		.asn1 = {0x04, 0x04},
		.asn1_len = 5,
		.asn1_hdr_len = 2,
		.want_ret = 0x80,
		.want_length = 4,
		.want_tag = 4,
		.want_class = 0,
		.want_error = ASN1_R_TOO_LONG,
	},
	{
		/* Constructed OctetString with insufficient data (only tag/len). */
		.asn1 = {0x24, 0x04},
		.asn1_len = 2,
		.asn1_hdr_len = 2,
		.want_ret = 0xa0,
		.want_length = 4,
		.want_tag = 4,
		.want_class = 0,
		.want_error = ASN1_R_TOO_LONG,
	},
};

#define N_ASN1_GET_OBJECT_TESTS \
    (sizeof(asn1_get_object_tests) / sizeof(*asn1_get_object_tests))

static int
asn1_get_object(void)
{
	const struct asn1_get_object_test *agot;
	const uint8_t *p;
	int ret, tag, tag_class;
	long err, length;
	size_t i;
	int failed = 1;

	for (i = 0; i < N_ASN1_GET_OBJECT_TESTS; i++) {
		agot = &asn1_get_object_tests[i];

		ERR_clear_error();

		p = agot->asn1;
		ret = ASN1_get_object(&p, &length, &tag, &tag_class, agot->asn1_len);

		if (ret != agot->want_ret) {
			fprintf(stderr, "FAIL: %zu - got return value %x, want %x\n",
			    i, ret, agot->want_ret);
			goto failed;
		}
		if (ret & 0x80) {
			err = ERR_peek_error();
			if (ERR_GET_REASON(err) != agot->want_error) {
				fprintf(stderr, "FAIL: %zu - got error reason %d, "
				    "want %d\n", i, ERR_GET_REASON(err),
				    agot->want_error);
				goto failed;
			}
			if (ERR_GET_REASON(err) == ASN1_R_HEADER_TOO_LONG) {
				if (p != agot->asn1) {
					fprintf(stderr, "FAIL: %zu - got ber_in %p, "
					    "want %p\n", i, p, agot->asn1);
					goto failed;
				}
				continue;
			}
		}
		if (length != agot->want_length) {
			fprintf(stderr, "FAIL: %zu - got length %ld, want %ld\n",
			    i, length, agot->want_length);
			goto failed;
		}
		if (tag != agot->want_tag) {
			fprintf(stderr, "FAIL: %zu - got tag %d, want %d\n",
			    i, tag, agot->want_tag);
			goto failed;
		}
		if (tag_class != agot->want_class) {
			fprintf(stderr, "FAIL: %zu - got class %d, want %d\n",
			    i, tag_class, agot->want_class);
			goto failed;
		}
		if (p != agot->asn1 + agot->asn1_hdr_len) {
			fprintf(stderr, "FAIL: %zu - got ber_in %p, want %p\n",
			    i, p, agot->asn1 + agot->asn1_len);
			goto failed;
		}
	}

	failed = 0;

 failed:
	return failed;
}

static int
asn1_integer_get_null_test(void)
{
	int failed = 0;
	long ret;

	if ((ret = ASN1_INTEGER_get(NULL)) != 0) {
		fprintf(stderr, "FAIL: ASN1_INTEGER_get(NULL) %ld != 0\n", ret);
		failed |= 1;
	}

	if ((ret = ASN1_ENUMERATED_get(NULL)) != 0) {
		fprintf(stderr, "FAIL: ASN1_ENUMERATED_get(NULL) %ld != 0\n",
		    ret);
		failed |= 1;
	}

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= asn1_tag2bit();
	failed |= asn1_tag2str();
	failed |= asn1_get_object();
	failed |= asn1_integer_get_null_test();

	return (failed);
}
