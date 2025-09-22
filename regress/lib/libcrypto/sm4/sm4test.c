/*	$OpenBSD: sm4test.c,v 1.1 2019/03/17 17:48:31 tb Exp $	*/
/*
 * Copyright (c) 2017, 2019 Ribose Inc
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

#include <openssl/sm4.h>

static void
hexdump(FILE *fp, const char *title, const uint8_t *buf, size_t len)
{
	size_t i;

	fprintf(fp, "%s:\n", title);
	for (i = 1; i <= len; i++)
		fprintf(fp, " 0x%02x,%s", buf[i - 1], (i % 8) ? "" : "\n");

	if (i % 8 != 1)
		fprintf(fp, "\n");
}

int
main(int argc, char *argv[])
{
	int i;
	SM4_KEY key;
	uint8_t block[SM4_BLOCK_SIZE];

	static const uint8_t k[SM4_BLOCK_SIZE] = {
		0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
		0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
	};

	static const uint8_t input[SM4_BLOCK_SIZE] = {
		0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
		0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
	};

	/*
	 * This test vector comes from Example 1 of GB/T 32907-2016,
	 * and described in Internet Draft draft-ribose-cfrg-sm4-02.
	 */
	static const uint8_t expected[SM4_BLOCK_SIZE] = {
		0x68, 0x1e, 0xdf, 0x34, 0xd2, 0x06, 0x96, 0x5e,
		0x86, 0xb3, 0xe9, 0x4f, 0x53, 0x6e, 0x42, 0x46
	};

	/*
	 * This test vector comes from Example 2 from GB/T 32907-2016,
	 * and described in Internet Draft draft-ribose-cfrg-sm4-02.
	 * After 1,000,000 iterations.
	 */
	static const uint8_t expected_iter[SM4_BLOCK_SIZE] = {
		0x59, 0x52, 0x98, 0xc7, 0xc6, 0xfd, 0x27, 0x1f,
		0x04, 0x02, 0xf8, 0x04, 0xc3, 0x3d, 0x3f, 0x66
	};

	if (!SM4_set_key(k, &key))
		errx(1, "SM4_set_key() failed");

	memcpy(block, input, SM4_BLOCK_SIZE);

	SM4_encrypt(block, block, &key);

	if (memcmp(block, expected, SM4_BLOCK_SIZE) != 0) {
		fprintf(stderr, "FAIL: Encryption failed\n");
		hexdump(stderr, "Got", block, SM4_BLOCK_SIZE);
		hexdump(stderr, "Expected", expected, SM4_BLOCK_SIZE);
		return 1;
	}

	for (i = 0; i < 999999; i++)
		SM4_encrypt(block, block, &key);

	if (memcmp(block, expected_iter, SM4_BLOCK_SIZE) != 0) {
		fprintf(stderr, "FAIL: Multi-iteration encryption failed\n");
		hexdump(stderr, "Got", block, SM4_BLOCK_SIZE);
		hexdump(stderr, "Expected", expected_iter, SM4_BLOCK_SIZE);
		return 1;
	}

	for (i = 0; i < 1000000; i++)
		SM4_decrypt(block, block, &key);

	if (memcmp(block, input, SM4_BLOCK_SIZE) != 0) {
		fprintf(stderr, "FAIL: Decrypted data does not match input\n");
		hexdump(stderr, "Got", block, SM4_BLOCK_SIZE);
		hexdump(stderr, "Expected", input, SM4_BLOCK_SIZE);
		return 1;
	}

	return 0;
}
