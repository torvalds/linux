/*	$OpenBSD: asn1_string_to_utf8.c,v 1.2 2022/11/23 08:51:05 tb Exp $ */
/*
 * Copyright (c) 2022 Theo Buehler <tb@openbsd.org>
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

#include <err.h>
#include <string.h>

#include <openssl/asn1.h>

struct asn1_string_to_utf8_test_case {
	const char *description;
	const ASN1_ITEM *item;
	const uint8_t der[32];
	size_t der_len;
	const uint8_t want[32];
	int want_len;
};

static const struct asn1_string_to_utf8_test_case tests[] = {
	{
		.description = "hello",
		.item = &ASN1_PRINTABLESTRING_it,
		.der = {
			0x13, 0x05, 0x68, 0x65, 0x6c, 0x6c, 0x6f,
		},
		.der_len = 7,
		.want = {
			0x68, 0x65, 0x6c, 0x6c, 0x6f,
		},
		.want_len = 5,
	},
	{
		.description = "face with tears of joy",
		.item = &ASN1_UTF8STRING_it,
		.der = {
			0x0c, 0x04, 0xF0, 0x9F, 0x98, 0x82,
		},
		.der_len = 6,
		.want = {
			0xF0, 0x9F, 0x98, 0x82,
		},
		.want_len = 4,
	},
	{
		.description = "hi",
		.item = &ASN1_IA5STRING_it,
		.der = {
			0x16, 0x02, 0x68, 0x69,
		},
		.der_len = 4,
		.want = {
			0x68, 0x69,
		},
		.want_len = 2,
	},
};

const size_t N_TESTS = sizeof(tests) / sizeof(tests[0]);

static int
asn1_string_to_utf8_test(const struct asn1_string_to_utf8_test_case *test)
{
	ASN1_STRING *str = NULL;
	const unsigned char *der;
	unsigned char *out = NULL;
	int ret;
	int failed = 1;

	der = test->der;
	if ((str = (ASN1_STRING *)ASN1_item_d2i(NULL, &der, test->der_len,
	    test->item)) == NULL) {
		warnx("ASN1_item_d2i failed");
		goto err;
	}

	if ((ret = ASN1_STRING_to_UTF8(&out, str)) < 0) {
		warnx("ASN1_STRING_to_UTF8 failed: got %d, want %d", ret,
		    test->want_len);
		goto err;
	}

	if (ret != test->want_len) {
		warnx("ASN1_STRING_to_UTF8: got %d, want %d", ret,
		    test->want_len);
		goto err;
	}

	if (memcmp(out, test->want, test->want_len) != 0) {
		warnx("memcmp failed");
		goto err;
	}

	failed = 0;
 err:
	ASN1_STRING_free(str);
	free(out);

	return failed;
}

static int
asn1_string_to_utf8_tests(void)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_TESTS; i++)
		failed |= asn1_string_to_utf8_test(&tests[i]);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= asn1_string_to_utf8_tests();

	return failed;
}
