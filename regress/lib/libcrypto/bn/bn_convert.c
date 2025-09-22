/*	$OpenBSD: bn_convert.c,v 1.9 2024/11/05 18:20:08 tb Exp $ */
/*
 * Copyright (c) 2023 Joel Sing <jsing@openbsd.org>
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

#include <openssl/bn.h>

/*
 * Additional test coverage is needed for:
 *
 * - BN_print()/BN_print_fp()
 *
 * - Invalid inputs to {asc,dec,hex,mpi}2bn
 * - Zero padded inputs
 */

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	fprintf(stderr, "\n");
}

static int
check_bin_output(size_t test_no, const char *label, const uint8_t *bin,
    size_t bin_len, const BIGNUM *bn)
{
	uint8_t *out = NULL;
	int out_len;
	int ret;
	int failed = 1;

	out_len = BN_num_bytes(bn);
	if (out_len != (int)bin_len) {
		fprintf(stderr, "FAIL: Test %zu %s - BN_num_bytes() = %d, "
		    "want %zu\n", test_no, label, out_len, bin_len);
		goto failure;
	}
	if (out_len > 0 && (out = malloc(out_len)) == NULL)
		err(1, "malloc");
	if ((ret = BN_bn2bin(bn, out)) != out_len) {
		fprintf(stderr, "FAIL: Test %zu %s - BN_bn2bin() returned %d, "
		    "want %d\n", test_no, label, ret, out_len);
		goto failure;
	}
	if (memcmp(out, bin, bin_len) != 0) {
		fprintf(stderr, "FAIL: Test %zu %s - output from "
		    "BN_bn2bin() differs\n", test_no, label);
		fprintf(stderr, "Got:\n");
		hexdump(out, out_len);
		fprintf(stderr, "Want:\n");
		hexdump(bin, bin_len);
		goto failure;
	}

	failed = 0;

 failure:
	free(out);

	return failed;
}

struct bn_asc2bn_test {
	const char *in;
	const uint8_t bin[64];
	size_t bin_len;
	int neg;
	int want_error;
};

static const struct bn_asc2bn_test bn_asc2bn_tests[] = {
	{
		.in = "",
		.want_error = 1,
	},
	{
		.in = "-",
		.want_error = 1,
	},
	{
		.in = "0",
		.bin = { 0x00, },
		.bin_len = 0,
		.neg = 0,
	},
	{
		.in = "0x0",
		.bin = { 0x00, },
		.bin_len = 0,
		.neg = 0,
	},
	{
		.in = "-0",
		.bin = { 0x00, },
		.bin_len = 0,
		.neg = 0,
	},
	{
		.in = "-0x0",
		.bin = { 0x00, },
		.bin_len = 0,
		.neg = 0,
	},
	{
		.in = "123456789",
		.bin = { 0x07, 0x5b, 0xcd, 0x15, },
		.bin_len = 4,
		.neg = 0,
	},
	{
		.in = "0123456789",
		.bin = { 0x07, 0x5b, 0xcd, 0x15, },
		.bin_len = 4,
		.neg = 0,
	},
	{
		.in = "-123456789",
		.bin = { 0x07, 0x5b, 0xcd, 0x15, },
		.bin_len = 4,
		.neg = 1,
	},
	{
		.in = "0X123456789",
		.bin = { 0x01, 0x23, 0x45, 0x67, 0x89, },
		.bin_len = 5,
		.neg = 0,
	},
	{
		.in = "0x123456789",
		.bin = { 0x01, 0x23, 0x45, 0x67, 0x89, },
		.bin_len = 5,
		.neg = 0,
	},
	{
		.in = "-0x123456789",
		.bin = { 0x01, 0x23, 0x45, 0x67, 0x89, },
		.bin_len = 5,
		.neg = 1,
	},
	{
		.in = "abcdef123456789",
		.want_error = 1,
	},
	{
		.in = "0x000123456789abCdEf",
		.bin = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef },
		.bin_len = 8,
		.neg = 0,
	},
};

#define N_BN_ASC2BN_TESTS \
    (sizeof(bn_asc2bn_tests) / sizeof(*bn_asc2bn_tests))

static int
test_bn_asc2bn(void)
{
	const struct bn_asc2bn_test *bat;
	BIGNUM *bn = NULL;
	size_t i;
	int failed = 1;

	for (i = 0; i < N_BN_ASC2BN_TESTS; i++) {
		bat = &bn_asc2bn_tests[i];

		BN_free(bn);
		bn = NULL;

		if (!BN_asc2bn(&bn, bat->in)) {
			if (bat->want_error)
				continue;
			fprintf(stderr, "FAIL: Test %zu - BN_asc2bn() failed\n", i);
			goto failure;
		}
		if (bat->want_error) {
			fprintf(stderr, "FAIL: Test %zu - BN_asc2bn() succeeded "
			    "when it should have failed\n", i);
			goto failure;
		}

		if (check_bin_output(i, "BN_asc2bn()", bat->bin, bat->bin_len,
		    bn) != 0)
			goto failure;

		if (BN_is_negative(bn) != bat->neg) {
			fprintf(stderr, "FAIL: Test %zu - BN_asc2bn() resulted "
			    "in negative %d, want %d", i, BN_is_negative(bn),
			    bat->neg);
			goto failure;
		}
	}

	/*
	 * While it makes little sense to call BN_asc2bn() with a NULL bn,
	 * check for consistent behavior.
	 */
	if (!BN_asc2bn(NULL, "1") || !BN_asc2bn(NULL, "-1") ||
	    !BN_asc2bn(NULL, "0x1") || !BN_asc2bn(NULL, "-0x1")) {
		fprintf(stderr, "FAIL: BN_asc2bn() with NULL BIGNUM failed\n");
		goto failure;
	}

	failed = 0;

 failure:
	BN_free(bn);

	return failed;
}

struct bn_convert_test {
	const uint8_t bin[64];
	size_t bin_len;
	int neg;
	const char *dec;
	const char *hex;
	const uint8_t mpi[64];
	int mpi_len;
};

static const struct bn_convert_test bn_convert_tests[] = {
	{
		.bin = { 0x0, },
		.bin_len = 0,
		.neg = 0,
		.dec = "0",
		.hex = "0",
		.mpi = { 0x00, 0x00, 0x00, 0x00, },
		.mpi_len = 4,
	},
	{
		.bin = { 0x1, },
		.bin_len = 1,
		.neg = 0,
		.dec = "1",
		.hex = "01",
		.mpi = { 0x00, 0x00, 0x00, 0x01, 0x01, },
		.mpi_len = 5,
	},
	{
		.bin = { 0x1, },
		.bin_len = 1,
		.neg = 1,
		.dec = "-1",
		.hex = "-01",
		.mpi = { 0x00, 0x00, 0x00, 0x01, 0x81, },
		.mpi_len = 5,
	},
	{
		.bin = { 0x7f, 0xff, 0xff, },
		.bin_len = 3,
		.neg = 0,
		.dec = "8388607",
		.hex = "7FFFFF",
		.mpi = { 0x00, 0x00, 0x00, 0x03, 0x7f, 0xff, 0xff, },
		.mpi_len = 7,
	},
	{
		.bin = { 0x7f, 0xff, 0xff, },
		.bin_len = 3,
		.neg = 1,
		.dec = "-8388607",
		.hex = "-7FFFFF",
		.mpi = { 0x00, 0x00, 0x00, 0x03, 0xff, 0xff, 0xff, },
		.mpi_len = 7,
	},
	{
		.bin = { 0x01, 0x02, 0x03, 0x04, },
		.bin_len = 4,
		.neg = 0,
		.dec = "16909060",
		.hex = "01020304",
		.mpi = { 0x00, 0x00, 0x00, 0x04, 0x01, 0x02, 0x03, 0x04, },
		.mpi_len = 8,
	},
	{
		.bin = { 0x04, 0x03, 0x02, 0x01, },
		.bin_len = 4,
		.neg = 0,
		.dec = "67305985",
		.hex = "04030201",
		.mpi = { 0x00, 0x00, 0x00, 0x04, 0x04, 0x03, 0x02, 0x01, },
		.mpi_len = 8,
	},
	{
		.bin = { 0xff, 0xff, 0xff, 0xff, },
		.bin_len = 4,
		.neg = 0,
		.dec = "4294967295",
		.hex = "FFFFFFFF",
		.mpi = {
			0x00, 0x00, 0x00, 0x05, 0x00, 0xff, 0xff, 0xff,
			0xff,
		 },
		.mpi_len = 9,
	},
	{
		.bin = { 0xff, 0xff, 0xff, 0xff, },
		.bin_len = 4,
		.neg = 1,
		.dec = "-4294967295",
		.hex = "-FFFFFFFF",
		.mpi = {
			0x00, 0x00, 0x00, 0x05, 0x80, 0xff, 0xff, 0xff,
			0xff,
		 },
		.mpi_len = 9,
	},
	{
		.bin = { 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, },
		.bin_len = 8,
		.neg = 0,
		.dec = "18446744069414584320",
		.hex = "FFFFFFFF00000000",
		.mpi = {
			0x00, 0x00, 0x00, 0x09, 0x00, 0xff, 0xff, 0xff,
			0xff, 0x00, 0x00, 0x00, 0x00,
		 },
		.mpi_len = 13,
	},
	{
		.bin = { 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, },
		.bin_len = 8,
		.neg = 1,
		.dec = "-18446744069414584320",
		.hex = "-FFFFFFFF00000000",
		.mpi = {
			0x00, 0x00, 0x00, 0x09, 0x80, 0xff, 0xff, 0xff,
			0xff, 0x00, 0x00, 0x00, 0x00,
		 },
		.mpi_len = 13,
	},
	{
		.bin = { 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, },
		.bin_len = 8,
		.neg = 0,
		.dec = "9223794255762391041",
		.hex = "8001800180018001",
		.mpi = {
			0x00, 0x00, 0x00, 0x09, 0x00, 0x80, 0x01, 0x80,
			0x01, 0x80, 0x01, 0x80, 0x01,
		 },
		.mpi_len = 13,
	},
	{
		.bin = { 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, },
		.bin_len = 8,
		.neg = 1,
		.dec = "-9223794255762391041",
		.hex = "-8001800180018001",
		.mpi = {
			0x00, 0x00, 0x00, 0x09, 0x80, 0x80, 0x01, 0x80,
			0x01, 0x80, 0x01, 0x80, 0x01,
		 },
		.mpi_len = 13,
	},
	{
		.bin = {
			0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80,
			0x01,
		},
		.bin_len = 9,
		.neg = 0,
		.dec = "27670538329471942657",
		.hex = "018001800180018001",
		.mpi = {
			0x00, 0x00, 0x00, 0x09, 0x01, 0x80, 0x01, 0x80,
			0x01, 0x80, 0x01, 0x80, 0x01,
		 },
		.mpi_len = 13,
	},
	{
		.bin = {
			0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80,
			0x01,
		},
		.bin_len = 9,
		.neg = 1,
		.dec = "-27670538329471942657",
		.hex = "-018001800180018001",
		.mpi = {
			0x00, 0x00, 0x00, 0x09, 0x81, 0x80, 0x01, 0x80,
			0x01, 0x80, 0x01, 0x80, 0x01,
		 },
		.mpi_len = 13,
	},
	{
		.bin = {
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
		},
		.bin_len = 32,
		.neg = 0,
		.dec = "57895161181645529494837117048595051142566530671229791132691030063130991362047",
		.hex = "7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF",
		.mpi = {
			0x00, 0x00, 0x00, 0x20, 0x7f, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff,
		 },
		.mpi_len = 36,
	},
	{
		.bin = {
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
		},
		.bin_len = 32,
		.neg = 1,
		.dec = "-57895161181645529494837117048595051142566530671229791132691030063130991362047",
		.hex = "-7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF7FFF",
		.mpi = {
			0x00, 0x00, 0x00, 0x20, 0xff, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff,
			0x7f, 0xff, 0x7f, 0xff,
		 },
		.mpi_len = 36,
	},
};

#define N_BN_CONVERT_TESTS \
    (sizeof(bn_convert_tests) / sizeof(*bn_convert_tests))

static int
test_bn_convert(void)
{
	const struct bn_convert_test *bct;
	uint8_t *mpi_out = NULL;
	char *out_str = NULL;
	uint8_t lebin[64];
	BIGNUM *bn = NULL;
	int mpi_len;
	size_t i, j;
	int failed = 1;

	for (i = 0; i < N_BN_CONVERT_TESTS; i++) {
		bct = &bn_convert_tests[i];

		BN_free(bn);
		if ((bn = BN_bin2bn(bct->bin, bct->bin_len, NULL)) == NULL) {
			fprintf(stderr, "FAIL: BN_bin2bn() failed\n");
			goto failure;
		}
		BN_set_negative(bn, bct->neg);

		if (check_bin_output(i, "BN_bin2bn()", bct->bin, bct->bin_len,
		    bn) != 0)
			goto failure;

		for (j = 0; j < bct->bin_len; j++)
			lebin[j] = bct->bin[bct->bin_len - j - 1];

		BN_free(bn);
		if ((bn = BN_lebin2bn(lebin, bct->bin_len, NULL)) == NULL) {
			fprintf(stderr, "FAIL: BN_lebin2bn() failed\n");
			goto failure;
		}
		BN_set_negative(bn, bct->neg);

		if (check_bin_output(i, "BN_lebin2bn()", bct->bin, bct->bin_len,
		    bn) != 0)
			goto failure;

		free(out_str);
		if ((out_str = BN_bn2dec(bn)) == NULL) {
			fprintf(stderr, "FAIL: BN_bn2dec() failed\n");
			goto failure;
		}
		if (strcmp(out_str, bct->dec) != 0) {
			fprintf(stderr, "FAIL: Test %zu - BN_bn2dec() returned "
			    "'%s', want '%s'", i, out_str, bct->dec);
			goto failure;
		}

		free(out_str);
		if ((out_str = BN_bn2hex(bn)) == NULL) {
			fprintf(stderr, "FAIL: BN_bn2hex() failed\n");
			goto failure;
		}
		if (strcmp(out_str, bct->hex) != 0) {
			fprintf(stderr, "FAIL: Test %zu - BN_bn2hex() returned "
			    "'%s', want '%s'", i, out_str, bct->hex);
			goto failure;
		}

		free(mpi_out);
		mpi_out = NULL;

		if ((mpi_len = BN_bn2mpi(bn, NULL)) != bct->mpi_len) {
			fprintf(stderr, "FAIL: Test %zu - BN_bn2mpi() returned "
			    "%d, want %d", i, mpi_len, bct->mpi_len);
			goto failure;
		}
		if ((mpi_out = calloc(1, bct->mpi_len)) == NULL)
			goto failure;
		if ((mpi_len = BN_bn2mpi(bn, mpi_out)) != bct->mpi_len) {
			fprintf(stderr, "FAIL: Test %zu - BN_bn2mpi() returned "
			    "%d, want %d", i, mpi_len, bct->mpi_len);
			goto failure;
		}
		if (memcmp(mpi_out, bct->mpi, bct->mpi_len) != 0) {
			fprintf(stderr, "FAIL: Test %zu - BN_bn2mpi() "
			    "generated:\n", i);
			hexdump(mpi_out, bct->mpi_len);
			fprintf(stderr, "Want:\n");
			hexdump(bct->mpi, bct->mpi_len);
			goto failure;
		}

		if (BN_dec2bn(&bn, bct->dec) != (int)strlen(bct->dec)) {
			fprintf(stderr, "FAIL: BN_dec2bn() failed\n");
			goto failure;
		}
		if (BN_is_negative(bn) != bct->neg) {
			fprintf(stderr, "FAIL: Test %zu - BN_dec2bn() resulted "
			    "in negative %d, want %d", i, BN_is_negative(bn),
			    bct->neg);
			goto failure;
		}
		if (check_bin_output(i, "BN_dec2bn()", bct->bin, bct->bin_len,
		    bn) != 0)
			goto failure;

		if (BN_hex2bn(&bn, bct->hex) != (int)strlen(bct->hex)) {
			fprintf(stderr, "FAIL: BN_hex2bn() failed\n");
			goto failure;
		}
		if (BN_is_negative(bn) != bct->neg) {
			fprintf(stderr, "FAIL: Test %zu - BN_hex2bn() resulted "
			    "in negative %d, want %d", i, BN_is_negative(bn),
			    bct->neg);
			goto failure;
		}
		if (check_bin_output(i, "BN_hex2bn()", bct->bin, bct->bin_len,
		    bn) != 0)
			goto failure;

		if (BN_mpi2bn(bct->mpi, bct->mpi_len, bn) == NULL) {
			fprintf(stderr, "FAIL: BN_mpi2bn() failed\n");
			goto failure;
		}
		if (BN_is_negative(bn) != bct->neg) {
			fprintf(stderr, "FAIL: Test %zu - BN_mpi2bn() resulted "
			    "in negative %d, want %d", i, BN_is_negative(bn),
			    bct->neg);
			goto failure;
		}
		if (check_bin_output(i, "BN_mpi2bn()", bct->bin, bct->bin_len,
		    bn) != 0)
			goto failure;
	}

	failed = 0;

 failure:
	free(mpi_out);
	free(out_str);
	BN_free(bn);

	return failed;
}

static int
test_bn_dec2bn(void)
{
	BIGNUM *bn = NULL;
	BN_ULONG w;
	int ret;
	int failed = 1;

	/* An empty string fails to parse, as does NULL. */
	if (BN_dec2bn(&bn, "") != 0) {
		fprintf(stderr, "FAIL: BN_dec2bn(_, \"\") succeeded\n");
		goto failure;
	}
	if (bn != NULL) {
		fprintf(stderr, "FAIL: BN_dec2bn(_, \"\") succeeded\n");
		goto failure;
	}
	if (BN_dec2bn(&bn, NULL) != 0) {
		fprintf(stderr, "FAIL: BN_dec2bn(_, NULL) succeeded\n");
		goto failure;
	}
	if (bn != NULL) {
		fprintf(stderr, "FAIL: BN_dec2bn(_, NULL) succeeded\n");
		goto failure;
	}

	/* A minus sign parses as 0. */
	if (BN_dec2bn(&bn, "-") != 1) {
		fprintf(stderr, "FAIL: BN_dec2bn(_, \"-\") failed\n");
		goto failure;
	}
	if (bn == NULL) {
		fprintf(stderr, "FAIL: BN_dec2bn(_, \"-\") failed\n");
		goto failure;
	}
	if (!BN_is_zero(bn)) {
		fprintf(stderr, "FAIL: BN_dec2bn(_, \"-\") is non-zero\n");
		goto failure;
	}
	if (BN_is_negative(bn)) {
		fprintf(stderr, "FAIL: BN_dec2bn(_, \"-\") resulted in "
		    "negative zero\n");
		goto failure;
	}

	/* Ensure that -0 results in 0. */
	if (BN_dec2bn(&bn, "-0") != 2) {
		fprintf(stderr, "FAIL: BN_dec2bn(_, \"-0\") failed\n");
		goto failure;
	}
	if (!BN_is_zero(bn)) {
		fprintf(stderr, "FAIL: BN_dec2bn(_, \"-0\") is non-zero\n");
		goto failure;
	}
	if (BN_is_negative(bn)) {
		fprintf(stderr, "FAIL: BN_dec2bn(_, \"-0\") resulted in "
		    "negative zero\n");
		goto failure;
	}

	/* BN_dec2bn() is the new atoi()... */
	if ((ret = BN_dec2bn(&bn, "0123456789abcdef")) != 10) {
		fprintf(stderr, "FAIL: BN_dec2bn() returned %d, want 10\n", ret);
		goto failure;
	}
	if ((w = BN_get_word(bn)) != 0x75bcd15) {
		fprintf(stderr, "FAIL: BN_dec2bn() resulted in %llx, want %llx\n",
		    (unsigned long long)w, 0x75bcd15ULL);
		goto failure;
	}

	/* And we can call BN_dec2bn() without actually converting to a BIGNUM. */
	if ((ret = BN_dec2bn(NULL, "0123456789abcdef")) != 10) {
		fprintf(stderr, "FAIL: BN_dec2bn() returned %d, want 10\n", ret);
		goto failure;
	}

	failed = 0;

 failure:
	BN_free(bn);

	return failed;
}

static int
test_bn_hex2bn(void)
{
	BIGNUM *bn = NULL;
	BN_ULONG w;
	int ret;
	int failed = 1;

	/* An empty string fails to parse, as does NULL. */
	if (BN_hex2bn(&bn, "") != 0) {
		fprintf(stderr, "FAIL: BN_hex2bn(_, \"\") succeeded\n");
		goto failure;
	}
	if (bn != NULL) {
		fprintf(stderr, "FAIL: BN_hex2bn(_, \"\") succeeded\n");
		goto failure;
	}
	if (BN_hex2bn(&bn, NULL) != 0) {
		fprintf(stderr, "FAIL: BN_hex2bn(_, NULL) succeeded\n");
		goto failure;
	}
	if (bn != NULL) {
		fprintf(stderr, "FAIL: BN_hex2bn(_, NULL) succeeded\n");
		goto failure;
	}

	/* A minus sign parses as 0. */
	if (BN_hex2bn(&bn, "-") != 1) {
		fprintf(stderr, "FAIL: BN_hex2bn(_, \"-\") failed\n");
		goto failure;
	}
	if (bn == NULL) {
		fprintf(stderr, "FAIL: BN_hex2bn(_, \"-\") failed\n");
		goto failure;
	}
	if (!BN_is_zero(bn)) {
		fprintf(stderr, "FAIL: BN_hex2bn(_, \"-\") returned non-zero\n");
		goto failure;
	}
	if (BN_is_negative(bn)) {
		fprintf(stderr, "FAIL: BN_hex2bn(_, \"-\") returned negative zero\n");
		goto failure;
	}

	/* Ensure that -0 results in 0. */
	if (BN_hex2bn(&bn, "-0") != 2) {
		fprintf(stderr, "FAIL: BN_hex2bn(_, \"-0\") failed\n");
		goto failure;
	}
	if (!BN_is_zero(bn)) {
		fprintf(stderr, "FAIL: BN_hex2bn(_, \"-0\") is non-zero\n");
		goto failure;
	}
	if (BN_is_negative(bn)) {
		fprintf(stderr, "FAIL: BN_hex2bn(_, \"-0\") resulted in "
		    "negative zero\n");
		goto failure;
	}

	/* BN_hex2bn() is the new atoi()... */
	if ((ret = BN_hex2bn(&bn, "9abcdefz")) != 7) {
		fprintf(stderr, "FAIL: BN_hex2bn() returned %d, want 7\n", ret);
		goto failure;
	}
	if ((w = BN_get_word(bn)) != 0x9abcdef) {
		fprintf(stderr, "FAIL: BN_hex2bn() resulted in %llx, want %llx\n",
		    (unsigned long long)w, 0x9abcdefULL);
		goto failure;
	}

	/* A 0x prefix fails to parse without BN_asc2bn() (instead we get 0!). */
	if (BN_hex2bn(&bn, "0x1") != 1) {
		fprintf(stderr, "FAIL: BN_hex2bn() parsed a 0x prefix\n");
		goto failure;
	}

	/* And we can call BN_hex2bn() without actually converting to a BIGNUM. */
	if ((ret = BN_hex2bn(NULL, "9abcdefz")) != 7) {
		fprintf(stderr, "FAIL: BN_hex2bn() returned %d, want 7\n", ret);
		goto failure;
	}

	failed = 0;

 failure:
	BN_free(bn);

	return failed;
}

static int
test_bn_binpad(void)
{
	const struct bn_convert_test *bct;
	BIGNUM *bn = NULL;
	uint8_t lebin[64];
	uint8_t buf[128];
	size_t i, j;
	int ret;
	int failed = 1;

	for (i = 0; i < N_BN_CONVERT_TESTS; i++) {
		bct = &bn_convert_tests[i];

		BN_free(bn);
		if ((bn = BN_bin2bn(bct->bin, bct->bin_len, NULL)) == NULL) {
			fprintf(stderr, "FAIL: BN_bin2bn() failed\n");
			goto failure;
		}
		BN_set_negative(bn, bct->neg);

		for (j = 0; j < bct->bin_len; j++)
			lebin[j] = bct->bin[bct->bin_len - j - 1];

		if ((ret = BN_bn2binpad(bn, buf, bct->bin_len)) < 0) {
			fprintf(stderr, "FAIL: BN_bn2binpad() failed\n");
			goto failure;
		}
		if ((size_t)ret != bct->bin_len) {
			fprintf(stderr, "FAIL: BN_bn2binpad() = %d, want %zu\n",
			    ret, bct->bin_len);
			goto failure;
		}
		if (memcmp(buf, bct->bin, bct->bin_len) != 0) {
			fprintf(stderr, "FAIL: Test %zu - output from "
			    "BN_bn2binpad() differs\n", i);
			fprintf(stderr, "Got:\n");
			hexdump(buf, bct->bin_len);
			fprintf(stderr, "Want:\n");
			hexdump(bct->bin, bct->bin_len);
			goto failure;
		}
		if (bct->bin_len > 0) {
			if ((ret = BN_bn2binpad(bn, buf, bct->bin_len - 1)) != -1) {
				fprintf(stderr, "FAIL: BN_bn2binpad() succeeded "
				    "with truncation\n");
				goto failure;
			}
		}
		if ((ret = BN_bn2binpad(bn, buf, 128)) < 0) {
			fprintf(stderr, "FAIL: BN_bn2binpad() failed\n");
			goto failure;
		}
		if (ret != 128) {
			fprintf(stderr, "FAIL: BN_bn2binpad() = %d, want 128\n",
			    ret);
			goto failure;
		}
		if (memcmp(&buf[128 - bct->bin_len], bct->bin, bct->bin_len) != 0) {
			fprintf(stderr, "FAIL: Test %zu - output from "
			    "BN_bn2binpad() differs\n", i);
			fprintf(stderr, "Got:\n");
			hexdump(&buf[128 - bct->bin_len], bct->bin_len);
			fprintf(stderr, "Want:\n");
			hexdump(bct->bin, bct->bin_len);
			goto failure;
		}
		for (j = 0; j < 128 - bct->bin_len; j++) {
			if (buf[j] != 0) {
				fprintf(stderr, "FAIL: BN_bn2binpad() is not "
				    "zero padded\n");
				goto failure;
			}
		}

		if ((ret = BN_bn2lebinpad(bn, buf, bct->bin_len)) < 0) {
			fprintf(stderr, "FAIL: BN_bn2lebinpad() failed\n");
			goto failure;
		}
		if ((size_t)ret != bct->bin_len) {
			fprintf(stderr, "FAIL: BN_bn2lebinpad() = %d, want %zu\n",
			    ret, bct->bin_len);
			goto failure;
		}
		if (memcmp(buf, lebin, bct->bin_len) != 0) {
			fprintf(stderr, "FAIL: Test %zu - output from "
			    "BN_bn2lebinpad() differs\n", i);
			fprintf(stderr, "Got:\n");
			hexdump(buf, bct->bin_len);
			fprintf(stderr, "Want:\n");
			hexdump(lebin, bct->bin_len);
			goto failure;
		}
		if (bct->bin_len > 0) {
			if ((ret = BN_bn2lebinpad(bn, buf, bct->bin_len - 1)) != -1) {
				fprintf(stderr, "FAIL: BN_bn2lebinpad() succeeded "
				    "with truncation\n");
				goto failure;
			}
		}
		if ((ret = BN_bn2lebinpad(bn, buf, 128)) < 0) {
			fprintf(stderr, "FAIL: BN_bn2lebinpad() failed\n");
			goto failure;
		}
		if (ret != 128) {
			fprintf(stderr, "FAIL: BN_bn2lebinpad() = %d, want 128\n",
			    ret);
			goto failure;
		}
		if (memcmp(buf, lebin, bct->bin_len) != 0) {
			fprintf(stderr, "FAIL: Test %zu - output from "
			    "BN_bn2lebinpad() differs\n", i);
			fprintf(stderr, "Got:\n");
			hexdump(buf, bct->bin_len);
			fprintf(stderr, "Want:\n");
			hexdump(lebin, bct->bin_len);
			goto failure;
		}
		for (j = bct->bin_len; j < 128; j++) {
			if (buf[j] != 0) {
				fprintf(stderr, "FAIL: BN_bn2lebinpad() is not "
				    "zero padded\n");
				goto failure;
			}
		}
	}

	failed = 0;

 failure:
	BN_free(bn);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= test_bn_asc2bn();
	failed |= test_bn_convert();
	failed |= test_bn_dec2bn();
	failed |= test_bn_hex2bn();
	failed |= test_bn_binpad();

	return failed;
}
