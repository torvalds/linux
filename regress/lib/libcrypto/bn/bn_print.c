/*	$OpenBSD: bn_print.c,v 1.5 2023/07/27 06:41:39 tb Exp $ */

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

#include <err.h>
#include <stdio.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>

#include "bn_local.h"

#define BATIHDIDIDI "mana mana"
#define BUF_MEM_LEN 1024

static const char *pk = "040d305e1b159d03d0a17935b73a3c927aca151ccd62f39c"
			"265c073de554faa3d6cc12eaf4145fe88e19ab2f2e48e6ac"
			"184378acd037c3bdb2cd2ce647e21ae663b83d2e2f78c44f"
			"dbf40fa4684c55726b951d4e18429578cc373c91e29b652b"
			"29";

const struct print_test {
	const char *desc;
	const char *want;
} bn_print_tests[] = {
	{
		.desc = "zero",
		.want = "    mana mana 0\n",
	},
	{
		.desc = "minus one",
		.want = "    mana mana 1 (0x1)\n",
	},
	{
		.desc = "minus one",
		.want = "    mana mana -1 (-0x1)\n",
	},
#ifdef _LP64
	{
		.desc = "largest word",
		.want = "    mana mana 18446744073709551615 "
			"(0xffffffffffffffff)\n",
	},
	{
		.desc = "smallest word",
		.want = "    mana mana -18446744073709551615 "
			"(-0xffffffffffffffff)\n",
	},
	{
		.desc = "largest negative non-word",
		.want = "    mana mana (Negative)\n"
			"        01:00:00:00:00:00:00:00:00\n",
	},
	{
		.desc = "smallest positive non-word",
		.want = "    mana mana\n"
			"        01:00:00:00:00:00:00:00:00\n",
	},
#else
	{
		.desc = "largest word",
		.want = "    mana mana 4294967295 (0xffffffff)\n",
	},
	{
		.desc = "smallest word",
		.want = "    mana mana -4294967295 (-0xffffffff)\n",
	},
	{
		.desc = "largest negative non-word",
		.want = "    mana mana (Negative)\n"
			"        01:00:00:00:00\n",
	},
	{
		.desc = "smallest positive non-word",
		.want = "    mana mana\n"
			"        01:00:00:00:00\n",
	},
#endif
	{
		.desc = "some pubkey",
		.want = "    mana mana\n"
			"        04:0d:30:5e:1b:15:9d:03:d0:a1:79:35:b7:3a:3c:\n"
			"        92:7a:ca:15:1c:cd:62:f3:9c:26:5c:07:3d:e5:54:\n"
			"        fa:a3:d6:cc:12:ea:f4:14:5f:e8:8e:19:ab:2f:2e:\n"
			"        48:e6:ac:18:43:78:ac:d0:37:c3:bd:b2:cd:2c:e6:\n"
			"        47:e2:1a:e6:63:b8:3d:2e:2f:78:c4:4f:db:f4:0f:\n"
			"        a4:68:4c:55:72:6b:95:1d:4e:18:42:95:78:cc:37:\n"
			"        3c:91:e2:9b:65:2b:29\n",
	},
	{
		.desc = "negated pubkey",
		.want = "    mana mana (Negative)\n"
			"        04:0d:30:5e:1b:15:9d:03:d0:a1:79:35:b7:3a:3c:\n"
			"        92:7a:ca:15:1c:cd:62:f3:9c:26:5c:07:3d:e5:54:\n"
			"        fa:a3:d6:cc:12:ea:f4:14:5f:e8:8e:19:ab:2f:2e:\n"
			"        48:e6:ac:18:43:78:ac:d0:37:c3:bd:b2:cd:2c:e6:\n"
			"        47:e2:1a:e6:63:b8:3d:2e:2f:78:c4:4f:db:f4:0f:\n"
			"        a4:68:4c:55:72:6b:95:1d:4e:18:42:95:78:cc:37:\n"
			"        3c:91:e2:9b:65:2b:29\n",
	},
	{
		.desc = "shifted negated pubkey",
		.want = "    mana mana (Negative)\n"
			"        04:0d:30:5e:1b:15:9d:03:d0:a1:79:35:b7:3a:3c:\n"
			"        92:7a:ca:15:1c:cd:62:f3:9c:26:5c:07:3d:e5:54:\n"
			"        fa:a3:d6:cc:12:ea:f4:14:5f:e8:8e:19:ab:2f:2e:\n"
			"        48:e6:ac:18:43:78:ac:d0:37:c3:bd:b2:cd:2c:e6:\n"
			"        47:e2:1a:e6:63:b8:3d:2e:2f:78:c4:4f:db:f4:0f:\n"
			"        a4:68:4c:55:72:6b:95:1d:4e:18:42:95:78:cc:37\n",
	},
	{
		.desc = "shifted pubkey",
		.want = "    mana mana\n"
			"        04:0d:30:5e:1b:15:9d:03:d0:a1:79:35:b7:3a:3c:\n"
			"        92:7a:ca:15:1c:cd:62:f3:9c:26:5c:07:3d:e5:54:\n"
			"        fa:a3:d6:cc:12:ea:f4:14:5f:e8:8e:19:ab:2f:2e:\n"
			"        48:e6:ac:18:43:78:ac:d0:37:c3:bd:b2:cd:2c:e6:\n"
			"        47:e2:1a:e6:63:b8:3d:2e:2f:78:c4:4f:db:f4:0f:\n"
			"        a4:68:4c:55:72:6b:95:1d:4e:18:42:95:78:cc:37\n",
	},
	{
		.desc = "high bit of first nibble is set",
		.want = "    mana mana\n"
			"        00:80:00:00:00:00:00:00:00:00\n",
	},
	{
		/* XXX - this is incorrect and should be fixed. */
		.desc = "high bit of first nibble is set for negative number",
		.want = "    mana mana (Negative)\n"
			"        00:80:00:00:00:00:00:00:00:00\n",
	},
};

#define N_TESTCASES	(sizeof(bn_print_tests) / sizeof(bn_print_tests[0]))

static int
bn_print_testcase(const BIGNUM *bn, const struct print_test *test)
{
	BIO *bio;
	char *got;
	size_t want_len;
	long got_len;
	int failed = 1;

	if ((bio = BIO_new(BIO_s_mem())) == NULL)
		errx(1, "BIO_new");

	if (!bn_printf(bio, bn, 4, "%s", BATIHDIDIDI))
		errx(1, "bn_printf");

	if ((got_len = BIO_get_mem_data(bio, &got)) < 0)
		errx(1, "BIO_get_mem_data");

	if ((want_len = strlen(test->want)) != (size_t)got_len) {
		fprintf(stderr, "%s: want: %zu, got %ld\n",
		    test->desc, want_len, got_len);
		goto err;
	}

	if (strncmp(got, test->want, want_len) != 0) {
		fprintf(stderr, "%s: strings differ\n", test->desc);
		fprintf(stderr, "want: \"%s\"\ngot : \"%*s\"\n",
		    test->want, (int)got_len, got);
		goto err;
	}

	failed = 0;
 err:
	BIO_free(bio);

	return failed;
}

int
main(void)
{
	const struct print_test *test;
	size_t testcase = 0;
	BIGNUM *bn;
	int failed = 0;

	/* zero */
	if ((bn = BN_new()) == NULL)
		errx(1, "BN_new");
	if (testcase >= N_TESTCASES)
		errx(1, "Too many tests");
	test = &bn_print_tests[testcase++];
	failed |= bn_print_testcase(bn, test);

	/* one */
	if (!BN_set_word(bn, 1))
		errx(1, "BIO_set_word");
	if (testcase >= N_TESTCASES)
		errx(1, "Too many tests");
	test = &bn_print_tests[testcase++];
	failed |= bn_print_testcase(bn, test);

	/* minus one */
	BN_set_negative(bn, 1);
	if (testcase >= N_TESTCASES)
		errx(1, "Too many tests");
	test = &bn_print_tests[testcase++];
	failed |= bn_print_testcase(bn, test);

	/* largest word */
	if (!BN_set_word(bn, ~0))
		errx(1, "BN_set_word");
	if (testcase >= N_TESTCASES)
		errx(1, "Too many tests");
	test = &bn_print_tests[testcase++];
	failed |= bn_print_testcase(bn, test);

	/* smallest word */
	BN_set_negative(bn, 1);
	if (testcase >= N_TESTCASES)
		errx(1, "Too many tests");
	test = &bn_print_tests[testcase++];
	failed |= bn_print_testcase(bn, test);

	/* largest negative non-word */
	if (!BN_sub_word(bn, 1))
		errx(1, "ASN1_bn_print");
	if (testcase >= N_TESTCASES)
		errx(1, "Too many tests");
	test = &bn_print_tests[testcase++];
	failed |= bn_print_testcase(bn, test);

	/* smallest positive non-word */
	BN_set_negative(bn, 0);
	if (testcase >= N_TESTCASES)
		errx(1, "Too many tests");
	test = &bn_print_tests[testcase++];
	failed |= bn_print_testcase(bn, test);

	/* some pubkey */
	if (BN_hex2bn(&bn, pk) == 0)
		errx(1, "BN_hex2bn");
	if (testcase >= N_TESTCASES)
		errx(1, "Too many tests");
	test = &bn_print_tests[testcase++];
	failed |= bn_print_testcase(bn, test);

	/* negated pubkey */
	BN_set_negative(bn, 1);
	if (testcase >= N_TESTCASES)
		errx(1, "Too many tests");
	test = &bn_print_tests[testcase++];
	failed |= bn_print_testcase(bn, test);

	/* shifted negated pubkey */
	if (!BN_rshift(bn, bn, 7 * 8))
		errx(1, "BN_rshift");
	if (testcase >= N_TESTCASES)
		errx(1, "Too many tests");
	test = &bn_print_tests[testcase++];
	failed |= bn_print_testcase(bn, test);

	/* shifted pubkey */
	BN_set_negative(bn, 0);
	if (testcase >= N_TESTCASES)
		errx(1, "Too many tests");
	test = &bn_print_tests[testcase++];
	failed |= bn_print_testcase(bn, test);

	/* high bit of first nibble is set. */
	BN_zero(bn);
	if (!BN_set_bit(bn, 71))
		errx(1, "BN_set_bit");
	if (testcase >= N_TESTCASES)
		errx(1, "Too many tests");
	test = &bn_print_tests[testcase++];
	failed |= bn_print_testcase(bn, test);

	/* high bit of first nibble is set for negative number. */
	BN_set_negative(bn, 1);
	if (testcase >= N_TESTCASES)
		errx(1, "Too many tests");
	test = &bn_print_tests[testcase++];
	failed |= bn_print_testcase(bn, test);

	if (testcase != N_TESTCASES) {
		warnx("Not all tests run");
		failed |= 1;
	}

	BN_free(bn);

	return failed;
}
