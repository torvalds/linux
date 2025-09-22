/*	$OpenBSD: rmd_test.c,v 1.1 2022/09/02 15:45:52 tb Exp $ */
/*
 * Copyright (c) 2022 Joshua Sing <joshua@hypera.dev>
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

#include <openssl/evp.h>
#include <openssl/ripemd.h>

#include <stdint.h>
#include <string.h>

struct rmd_test {
	const uint8_t in[128];
	const size_t in_len;
	const uint8_t out[EVP_MAX_MD_SIZE];
};

static const struct rmd_test rmd_tests[] = {
	/*
	 * RIPEMD-160 - Test vectors from
	 * https://homes.esat.kuleuven.be/~bosselae/ripemd160.html
	 */
	{
		.in = "",
		.in_len = 0,
		.out = {
			0x9c, 0x11, 0x85, 0xa5, 0xc5, 0xe9, 0xfc, 0x54,
			0x61, 0x28, 0x08, 0x97, 0x7e, 0xe8, 0xf5, 0x48,
			0xb2, 0x25, 0x8d, 0x31,
		},
	},
	{
		.in = "a",
		.in_len = 1,
		.out = {
			0x0b, 0xdc, 0x9d, 0x2d, 0x25, 0x6b, 0x3e, 0xe9,
			0xda, 0xae, 0x34, 0x7b, 0xe6, 0xf4, 0xdc, 0x83,
			0x5a, 0x46, 0x7f, 0xfe,
		},
	},
	{
		.in = "abc",
		.in_len = 3,
		.out = {
			0x8e, 0xb2, 0x08, 0xf7, 0xe0, 0x5d, 0x98, 0x7a,
			0x9b, 0x04, 0x4a, 0x8e, 0x98, 0xc6, 0xb0, 0x87,
			0xf1, 0x5a, 0x0b, 0xfc,
		},
	},
	{
		.in = "message digest",
		.in_len = 14,
		.out = {
			0x5d, 0x06, 0x89, 0xef, 0x49, 0xd2, 0xfa, 0xe5,
			0x72, 0xb8, 0x81, 0xb1, 0x23, 0xa8, 0x5f, 0xfa,
			0x21, 0x59, 0x5f, 0x36,
		},
	},
	{
		.in = "abcdefghijklmnopqrstuvwxyz",
		.in_len = 26,
		.out = {
			0xf7, 0x1c, 0x27, 0x10, 0x9c, 0x69, 0x2c, 0x1b,
			0x56, 0xbb, 0xdc, 0xeb, 0x5b, 0x9d, 0x28, 0x65,
			0xb3, 0x70, 0x8d, 0xbc,
		},
	},
	{
		.in =
		    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		.in_len = 56,
		.out = {
			0x12, 0xa0, 0x53, 0x38, 0x4a, 0x9c, 0x0c, 0x88,
			0xe4, 0x05, 0xa0, 0x6c, 0x27, 0xdc, 0xf4, 0x9a,
			0xda, 0x62, 0xeb, 0x2b,
		},
	},
	{
		.in =
		    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuv"
		    "wxyz0123456789",
		.in_len = 62,
		.out = {
			0xb0, 0xe2, 0x0b, 0x6e, 0x31, 0x16, 0x64, 0x02,
			0x86, 0xed, 0x3a, 0x87, 0xa5, 0x71, 0x30, 0x79,
			0xb2, 0x1f, 0x51, 0x89,
		},
	},
	{
		.in =
		    "123456789012345678901234567890123456789012345678"
		    "90123456789012345678901234567890",
		.in_len = 80,
		.out = {
			0x9b, 0x75, 0x2e, 0x45, 0x57, 0x3d, 0x4b, 0x39,
			0xf4, 0xdb, 0xd3, 0x32, 0x3c, 0xab, 0x82, 0xbf,
			0x63, 0x32, 0x6b, 0xfb,
		},
	},
};

#define N_RMD_TESTS (sizeof(rmd_tests) / sizeof(rmd_tests[0]))

static int
rmd_test(void)
{
	const struct rmd_test *rt;
	EVP_MD_CTX *hash = NULL;
	uint8_t out[EVP_MAX_MD_SIZE];
	size_t in_len;
	size_t i;
	int failed = 1;

	if ((hash = EVP_MD_CTX_new()) == NULL) {
		fprintf(stderr, "FAIL: EVP_MD_CTX_new() failed\n");
		goto failed;
	}

	for (i = 0; i < N_RMD_TESTS; i++) {
		rt = &rmd_tests[i];

		/* Digest */
		memset(out, 0, sizeof(out));
		RIPEMD160(rt->in, rt->in_len, out);
		if (memcmp(rt->out, out, RIPEMD160_DIGEST_LENGTH) != 0) {
			fprintf(stderr, "FAIL: mismatch\n");
			goto failed;
		}

		/* EVP single-shot digest */
		memset(out, 0, sizeof(out));
		if (!EVP_Digest(rt->in, rt->in_len, out, NULL, EVP_ripemd160(), NULL)) {
			fprintf(stderr, "FAIL: EVP_Digest failed\n");
			goto failed;
		}

		if (memcmp(rt->out, out, RIPEMD160_DIGEST_LENGTH) != 0) {
			fprintf(stderr, "FAIL: EVP single-shot mismatch\n");
			goto failed;
		}

		/* EVP digest */
		memset(out, 0, sizeof(out));
		if (!EVP_DigestInit_ex(hash, EVP_ripemd160(), NULL)) {
			fprintf(stderr, "FAIL: EVP_DigestInit_ex failed\n");
			goto failed;
		}

		in_len = rt->in_len / 2;
		if (!EVP_DigestUpdate(hash, rt->in, in_len)) {
			fprintf(stderr,
			    "FAIL: EVP_DigestUpdate first half failed\n");
			goto failed;
		}

		if (!EVP_DigestUpdate(hash, rt->in + in_len,
		    rt->in_len - in_len)) {
			fprintf(stderr,
			    "FAIL: EVP_DigestUpdate second half failed\n");
			goto failed;
		}

		if (!EVP_DigestFinal_ex(hash, out, NULL)) {
			fprintf(stderr, "FAIL: EVP_DigestFinal_ex failed\n");
			goto failed;
		}

		if (memcmp(rt->out, out, RIPEMD160_DIGEST_LENGTH) != 0) {
			fprintf(stderr, "FAIL: EVP mismatch\n");
			goto failed;
		}
	}

	failed = 0;

 failed:
	EVP_MD_CTX_free(hash);
	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= rmd_test();

	return failed;
}
