/*	$OpenBSD: sm3test.c,v 1.3 2021/04/06 15:00:19 tb Exp $	*/
/*
 * Copyright (c) 2018 Ribose Inc
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include <openssl/evp.h>

#define SM3_TESTS 3

const char *sm3_input[SM3_TESTS] = {
	"",
	"abc",
	"abcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd",
};

const uint8_t sm3_expected[SM3_TESTS][32] = {
	{
		0x1a, 0xb2, 0x1d, 0x83, 0x55, 0xcf, 0xa1, 0x7f,
		0x8e, 0x61, 0x19, 0x48, 0x31, 0xe8, 0x1a, 0x8f,
		0x22, 0xbe, 0xc8, 0xc7, 0x28, 0xfe, 0xfb, 0x74,
		0x7e, 0xd0, 0x35, 0xeb, 0x50, 0x82, 0xaa, 0x2b,
	},
	{
		0x66, 0xc7, 0xf0, 0xf4, 0x62, 0xee, 0xed, 0xd9,
		0xd1, 0xf2, 0xd4, 0x6b, 0xdc, 0x10, 0xe4, 0xe2,
		0x41, 0x67, 0xc4, 0x87, 0x5c, 0xf2, 0xf7, 0xa2,
		0x29, 0x7d, 0xa0, 0x2b, 0x8f, 0x4b, 0xa8, 0xe0,
	},
	{
		0xde, 0xbe, 0x9f, 0xf9, 0x22, 0x75, 0xb8, 0xa1,
		0x38, 0x60, 0x48, 0x89, 0xc1, 0x8e, 0x5a, 0x4d,
		0x6f, 0xdb, 0x70, 0xe5, 0x38, 0x7e, 0x57, 0x65,
		0x29, 0x3d, 0xcb, 0xa3, 0x9c, 0x0c, 0x57, 0x32,
	},
};

/* Tweaked version of libssl/key_schedule/key_schedule.c. */
static void
hexdump(const uint8_t *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02x,%s", buf[i - 1], (i % 8) ? "" : "\n");

	if (i % 8 != 1)
		fprintf(stderr, "\n");
}

int
main(int argc, char *argv[])
{
	EVP_MD_CTX *ctx;
	uint8_t digest[32];
	int i;
	int numerrors = 0;

	if ((ctx = EVP_MD_CTX_new()) == NULL)
		err(1, NULL);

	for (i = 0; i < SM3_TESTS; i++) {
		if (!EVP_DigestInit(ctx, EVP_sm3()))
			errx(1, "EVP_DigestInit() failed");
		if (!EVP_DigestUpdate(ctx, sm3_input[i], strlen(sm3_input[i])))
			errx(1, "EVP_DigestInit() failed");
		if (!EVP_DigestFinal(ctx, digest, NULL))
			errx(1, "EVP_DigestFinal() failed");

		if (memcmp(digest, sm3_expected[i], sizeof(digest)) != 0) {
			fprintf(stderr, "TEST %d failed\n", i);
			fprintf(stderr, "Produced:\n");
			hexdump(digest, sizeof(digest));
			fprintf(stderr, "Expected:\n");
			hexdump(sm3_expected[i], sizeof(sm3_expected[i]));
			numerrors++;
		} else
			fprintf(stderr, "SM3 test %d ok\n", i);
	}

	EVP_MD_CTX_free(ctx);

	return (numerrors > 0) ? 1 : 0;
}
