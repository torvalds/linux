/*	$OpenBSD: asn1oct.c,v 1.4 2023/05/13 07:17:32 tb Exp $ */

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

#include <assert.h>
#include <err.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/x509v3.h>

#define TESTBUFFER_SIZE		20

static const struct i2s_asn1_octet_string_test {
	const char *desc;
	const uint8_t buf[TESTBUFFER_SIZE];
	long len;
	const char *want;
} i2s_test[] = {
	{
		.desc = "Empty buffer gives empty string",
		.buf = { 0x00, },
		.len = 0,
		.want = "",
	},
	{
		.desc = "all hex digits",
		.buf = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, },
		.len = 8,
		.want = "01:23:45:67:89:AB:CD:EF",
	},
	{
		.desc = "all hex digits, scrambled",
		.buf = { 0x98, 0x24, 0xbf, 0x3a, 0xc7, 0xd6, 0x01, 0x5e, },
		.len = 8,
		.want = "98:24:BF:3A:C7:D6:01:5E",
	},
	{
		.desc = "Embedded 0 byte",
		.buf = { 0x7a, 0x00, 0xbb, },
		.len = 3,
		.want = "7A:00:BB",
	},
	{
		.desc = "All zeroes",
		.buf = { 0x00, 0x00, 0x00, 0x00, 0x00, },
		.len = 4,
		.want = "00:00:00:00",
	},
	{
		.desc = "All bits set",
		.buf = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, },
		.len = 8,
		.want = "FF:FF:FF:FF:FF:FF:FF:FF",
	},
	{
		.desc = "negative length",
		.buf = { 0x00, },
		.len = -1,
	},
};

#define N_I2S_TESTS (sizeof(i2s_test) / sizeof(i2s_test[0]))

static int
test_i2s_ASN1_OCTET_STRING(const struct i2s_asn1_octet_string_test *test)
{
	ASN1_OCTET_STRING *aos = NULL;
	int should_fail = test->want == NULL;
	char *got = NULL;
	int failed = 0;

	if ((aos = ASN1_OCTET_STRING_new()) == NULL)
		errx(1, "ASN1_OCTET_STRING_new");

	if (!ASN1_STRING_set(aos, (void *)test->buf, test->len))
		errx(1, "ASN1_STRING_set");

	if ((got = i2s_ASN1_OCTET_STRING(NULL, aos)) == NULL) {
		if (!should_fail)
			errx(1, "i2s_ASN1_OCTET_STRING");
	}

	if (!should_fail && strcmp(test->want, got) != 0) {
		fprintf(stderr, "%s: \"%s\" failed: want \"%s\", got \"%s\"\n",
		    __func__, test->desc, test->want, got);
		failed |= 1;
	}

	ASN1_OCTET_STRING_free(aos);
	free(got);

	return failed;
}

static int
test_new_ASN1_OCTET_STRING(void)
{
	ASN1_OCTET_STRING *aos = NULL;
	char *got;
	int failed = 0;

	if ((aos = ASN1_OCTET_STRING_new()) == NULL)
		errx(1, "%s: ASN1_OCTET_STRING_new", __func__);
	if ((got = i2s_ASN1_OCTET_STRING(NULL, aos)) == NULL)
		errx(1, "%s: i2s_ASN1_OCTET_STRING", __func__);

	if (strcmp("", got) != 0) {
		fprintf(stderr, "%s failed: want \"\", got \"%s\"\n",
		    __func__, got);
		failed |= 1;
	}

	ASN1_OCTET_STRING_free(aos);
	free(got);

	return failed;
}

static int
run_i2s_ASN1_OCTET_STRING_tests(void)
{
	size_t i;
	int failed = 0;

	failed |= test_new_ASN1_OCTET_STRING();

	for (i = 0; i < N_I2S_TESTS; i++)
		failed |= test_i2s_ASN1_OCTET_STRING(&i2s_test[i]);

	return failed;
}

static const struct s2i_asn1_octet_string_test {
	const char *desc;
	const char *in;
	const char *want;
} s2i_test[] = {
	/* Tests that should succeed. */
	{
		.desc = "empty string",
		.in = "",
		.want = "",
	},
	{
		.desc = "only colons",
		.in = ":::::::",
		.want = "",
	},
	{
		.desc = "a 0 octet",
		.in = "00",
		.want = "00",
	},
	{
		.desc = "a 0 octet with stupid colons",
		.in = ":::00:::::",
		.want = "00",
	},
	{
		.desc = "more stupid colons",
		.in = ":::C0fF::Ee:::::",
		.want = "C0:FF:EE",
	},
	{
		.desc = "all hex digits",
		.in = "0123456789abcdef",
		.want = "01:23:45:67:89:AB:CD:EF",
	},

	/* Tests that should fail. */
	{
		.desc = "colons between hex digits",
		.in = "A:F",
	},
	{
		.desc = "more colons between hex digits",
		.in = "5:7",
	},
	{
		.desc = "one hex digit",
		.in = "1",
	},
	{
		.desc = "three hex digits",
		.in = "bad",
	},
	{
		.desc = "three hex digits, colon after first digit",
		.in = "b:ad",
	},
	{
		.desc = "three hex digits, colon after second digit",
		.in = "ba:d",
	},
	{
		.desc = "non-hex digit",
		.in = "g00d",
	},
	{
		.desc = "non-hex digits",
		.in = "d0gged",
	},
	{
		.desc = "trailing non-hex digit",
		.in = "d00der",
	},
};

#define N_S2I_TESTS (sizeof(s2i_test) / sizeof(s2i_test[0]))

static int
test_s2i_ASN1_OCTET_STRING(const struct s2i_asn1_octet_string_test *test)
{
	ASN1_OCTET_STRING *aos = NULL;
	char *got = NULL;
	int should_fail = test->want == NULL;
	int failed = 0;

	if ((aos = s2i_ASN1_OCTET_STRING(NULL, NULL, test->in)) == NULL) {
		if (!should_fail)
			errx(1, "%s: s2i_ASN1_OCTET_STRING", test->desc);
		goto done;
	}

	if ((got = i2s_ASN1_OCTET_STRING(NULL, aos)) == NULL)
		errx(1, "%s: i2s_ASN1_OCTET_STRING", test->desc);

	assert(test->want != NULL);
	if (strcmp(test->want, got) != 0) {
		fprintf(stderr, "%s: \"%s\" failed: want \"%s\", got \"%s\"\n",
		    __func__, test->desc, test->want, got);
		failed |= 1;
	}

 done:
	ASN1_OCTET_STRING_free(aos);
	free(got);

	return failed;
}

static int
run_s2i_ASN1_OCTET_STRING_tests(void)
{
	size_t i;
	int failed = 0;

	failed |= test_new_ASN1_OCTET_STRING();

	for (i = 0; i < N_S2I_TESTS; i++)
		failed |= test_s2i_ASN1_OCTET_STRING(&s2i_test[i]);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= run_i2s_ASN1_OCTET_STRING_tests();
	failed |= run_s2i_ASN1_OCTET_STRING_tests();

	return failed;
}
