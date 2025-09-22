/* $OpenBSD: asn1complex.c,v 1.4 2022/09/05 21:06:31 tb Exp $ */
/*
 * Copyright (c) 2017, 2021 Joel Sing <jsing@openbsd.org>
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
#include <openssl/asn1t.h>
#include <openssl/err.h>

#include <err.h>
#include <stdio.h>
#include <string.h>

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

/* Constructed octet string with length 12. */
const uint8_t asn1_constructed_basic_ber[] = {
	0x24, 0x0c,
	0x04, 0x01, 0x01,
	0x04, 0x02, 0x01, 0x02,
	0x04, 0x03, 0x01, 0x02, 0x03
};
const uint8_t asn1_constructed_basic_content[] = {
	0x01, 0x01, 0x02, 0x01, 0x02, 0x03,
};

/* Nested constructed octet string. */
const uint8_t asn1_constructed_nested_ber[] = {
	0x24, 0x1a,
	0x04, 0x01, 0x01,
	0x24, 0x15,
	0x04, 0x02, 0x02, 0x03,
	0x24, 0x0f,
	0x24, 0x0d,
	0x04, 0x03, 0x04, 0x05, 0x06,
	0x24, 0x06,
	0x24, 0x04,
	0x04, 0x02, 0x07, 0x08,
};
const uint8_t asn1_constructed_nested_content[] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
};

/* Deeply nested constructed octet string. */
const uint8_t asn1_constructed_deep_nested_ber[] = {
	0x24, 0x1b,
	0x04, 0x01, 0x01,
	0x24, 0x16,
	0x04, 0x02, 0x02, 0x03,
	0x24, 0x10,
	0x24, 0x0e,
	0x04, 0x03, 0x04, 0x05, 0x06,
	0x24, 0x07,
	0x24, 0x05,
	0x24, 0x03,
	0x04, 0x01, 0x07,
};

/* Constructed octet string with indefinite length. */
const uint8_t asn1_constructed_indefinite_ber[] = {
	0x24, 0x80,
	0x04, 0x01, 0x01,
	0x04, 0x02, 0x01, 0x02,
	0x04, 0x03, 0x01, 0x02, 0x03,
	0x00, 0x00,
};
const uint8_t asn1_constructed_indefinite_content[] = {
	0x01, 0x01, 0x02, 0x01, 0x02, 0x03,
};

struct asn1_constructed_test {
	const char *name;
	const uint8_t *asn1;
	size_t asn1_len;
	const uint8_t *want;
	size_t want_len;
	int want_error;
	int valid;
};

const struct asn1_constructed_test asn1_constructed_tests[] = {
	{
		.name = "basic constructed",
		.asn1 = asn1_constructed_basic_ber,
		.asn1_len = sizeof(asn1_constructed_basic_ber),
		.want = asn1_constructed_basic_content,
		.want_len = sizeof(asn1_constructed_basic_content),
		.valid = 1,
	},
	{
		.name = "nested constructed",
		.asn1 = asn1_constructed_nested_ber,
		.asn1_len = sizeof(asn1_constructed_nested_ber),
		.want = asn1_constructed_nested_content,
		.want_len = sizeof(asn1_constructed_nested_content),
		.valid = 1,
	},
	{
		.name = "deep nested constructed",
		.asn1 = asn1_constructed_deep_nested_ber,
		.asn1_len = sizeof(asn1_constructed_deep_nested_ber),
		.want_error = ASN1_R_NESTED_ASN1_STRING,
		.valid = 0,
	},
	{
		.name = "indefinite length constructed",
		.asn1 = asn1_constructed_indefinite_ber,
		.asn1_len = sizeof(asn1_constructed_indefinite_ber),
		.want = asn1_constructed_indefinite_content,
		.want_len = sizeof(asn1_constructed_indefinite_content),
		.valid = 1,
	},
};

#define N_CONSTRUCTED_TESTS \
    (sizeof(asn1_constructed_tests) / sizeof(*asn1_constructed_tests))

static int
do_asn1_constructed_test(const struct asn1_constructed_test *act)
{
	ASN1_OCTET_STRING *aos = NULL;
	const uint8_t *p;
	long err;
	int failed = 1;

	ERR_clear_error();

	p = act->asn1;
	aos = d2i_ASN1_OCTET_STRING(NULL, &p, act->asn1_len);
	if (!act->valid) {
		if (aos != NULL) {
			fprintf(stderr, "FAIL: invalid ASN.1 decoded\n");
			goto failed;
		}
		if (act->want_error != 0) {
			err = ERR_peek_error();
			if (ERR_GET_REASON(err) != act->want_error) {
				fprintf(stderr, "FAIL: got error reason %d,"
				    "want %d", ERR_GET_REASON(err),
				    act->want_error);
				goto failed;
			}
		}
		goto done;
	}
	if (aos == NULL) {
		fprintf(stderr, "FAIL: failed to decode ASN.1 constructed "
		    "octet string\n");
		ERR_print_errors_fp(stderr);
		goto failed;
	}
	if (!asn1_compare_bytes(act->name, ASN1_STRING_data(aos),
	    ASN1_STRING_length(aos), act->want, act->want_len))
		goto failed;

 done:
	failed = 0;

 failed:
	ASN1_OCTET_STRING_free(aos);

	return failed;
}

static int
do_asn1_constructed_tests(void)
{
	const struct asn1_constructed_test *act;
	int failed = 0;
	size_t i;

	for (i = 0; i < N_CONSTRUCTED_TESTS; i++) {
		act = &asn1_constructed_tests[i];
		failed |= do_asn1_constructed_test(act);
	}

	return failed;
}

/* Sequence with length. */
const uint8_t asn1_sequence_ber[] = {
	0x30, 0x16,
	0x04, 0x01, 0x01,
	0x04, 0x02, 0x01, 0x02,
	0x04, 0x03, 0x01, 0x02, 0x03,
	0x30, 0x80, 0x04, 0x01, 0x01, 0x00, 0x00,
	0x04, 0x01, 0x01,

	0x04, 0x01, 0x01, /* Trailing data. */
};

const uint8_t asn1_sequence_content[] = {
	0x30, 0x16, 0x04, 0x01, 0x01, 0x04, 0x02, 0x01,
	0x02, 0x04, 0x03, 0x01, 0x02, 0x03, 0x30, 0x80,
	0x04, 0x01, 0x01, 0x00, 0x00, 0x04, 0x01, 0x01,
};

/* Sequence with indefinite length. */
const uint8_t asn1_sequence_indefinite_ber[] = {
	0x30, 0x80,
	0x04, 0x01, 0x01,
	0x04, 0x02, 0x01, 0x02,
	0x04, 0x03, 0x01, 0x02, 0x03,
	0x30, 0x80, 0x04, 0x01, 0x01, 0x00, 0x00,
	0x04, 0x01, 0x01,
	0x00, 0x00,

	0x04, 0x01, 0x01, /* Trailing data. */
};

const uint8_t asn1_sequence_indefinite_content[] = {
	0x30, 0x80, 0x04, 0x01, 0x01, 0x04, 0x02, 0x01,
	0x02, 0x04, 0x03, 0x01, 0x02, 0x03, 0x30, 0x80,
	0x04, 0x01, 0x01, 0x00, 0x00, 0x04, 0x01, 0x01,
	0x00, 0x00,
};

static int
do_asn1_sequence_string_tests(void)
{
	ASN1_STRING *astr = NULL;
	const uint8_t *p;
	long len;
	int failed = 1;

	ERR_clear_error();

	/*
	 * Test decoding of sequence with length and indefinite length into
	 * a string - in this case the ASN.1 is not decoded and is stored
	 * directly as the content for the string.
	 */
	if ((astr = ASN1_STRING_new()) == NULL) {
		fprintf(stderr, "FAIL: ASN1_STRING_new() returned NULL\n");
		goto failed;
	}

	p = asn1_sequence_ber;
	len = sizeof(asn1_sequence_ber);
	if (ASN1_item_d2i((ASN1_VALUE **)&astr, &p, len,
	    &ASN1_SEQUENCE_it) == NULL) {
		fprintf(stderr, "FAIL: failed to decode ASN1_SEQUENCE\n");
		ERR_print_errors_fp(stderr);
		goto failed;
	}

	if (!asn1_compare_bytes("sequence", ASN1_STRING_data(astr),
	    ASN1_STRING_length(astr), asn1_sequence_content,
	    sizeof(asn1_sequence_content)))
		goto failed;

	p = asn1_sequence_indefinite_ber;
	len = sizeof(asn1_sequence_indefinite_ber);
	if (ASN1_item_d2i((ASN1_VALUE **)&astr, &p, len,
	    &ASN1_SEQUENCE_it) == NULL) {
		fprintf(stderr, "FAIL: failed to decode ASN1_SEQUENCE\n");
		ERR_print_errors_fp(stderr);
		goto failed;
	}

	if (!asn1_compare_bytes("sequence indefinite", ASN1_STRING_data(astr),
	    ASN1_STRING_length(astr), asn1_sequence_indefinite_content,
	    sizeof(asn1_sequence_indefinite_content)))
		goto failed;

	failed = 0;

 failed:
	ASN1_STRING_free(astr);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= do_asn1_constructed_tests();
	failed |= do_asn1_sequence_string_tests();

	return (failed);
}
