/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 The FreeBSD Project. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Based on:
 * SHA256-based Unix crypt implementation. Released into the Public Domain by
 * Ulrich Drepper <drepper@redhat.com>. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/param.h>

#include <errno.h>
#include <limits.h>
#include <sha256.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypt.h"

/* Define our magic string to mark salt for SHA256 "encryption" replacement. */
static const char sha256_salt_prefix[] = "$5$";

/* Prefix for optional rounds specification. */
static const char sha256_rounds_prefix[] = "rounds=";

/* Maximum salt string length. */
#define SALT_LEN_MAX 16
/* Default number of rounds if not explicitly specified. */
#define ROUNDS_DEFAULT 5000
/* Minimum number of rounds. */
#define ROUNDS_MIN 1000
/* Maximum number of rounds. */
#define ROUNDS_MAX 999999999

int
crypt_sha256(const char *key, const char *salt, char *buffer)
{
	u_long srounds;
	uint8_t alt_result[32], temp_result[32];
	SHA256_CTX ctx, alt_ctx;
	size_t salt_len, key_len, cnt, rounds;
	char *cp, *p_bytes, *s_bytes, *endp;
	const char *num;
	bool rounds_custom;

	/* Default number of rounds. */
	rounds = ROUNDS_DEFAULT;
	rounds_custom = false;

	/* Find beginning of salt string. The prefix should normally always
	 * be present. Just in case it is not. */
	if (strncmp(sha256_salt_prefix, salt, sizeof(sha256_salt_prefix) - 1) == 0)
		/* Skip salt prefix. */
		salt += sizeof(sha256_salt_prefix) - 1;

	if (strncmp(salt, sha256_rounds_prefix, sizeof(sha256_rounds_prefix) - 1)
	    == 0) {
		num = salt + sizeof(sha256_rounds_prefix) - 1;
		srounds = strtoul(num, &endp, 10);

		if (*endp == '$') {
			salt = endp + 1;
			rounds = MAX(ROUNDS_MIN, MIN(srounds, ROUNDS_MAX));
			rounds_custom = true;
		}
	}

	salt_len = MIN(strcspn(salt, "$"), SALT_LEN_MAX);
	key_len = strlen(key);

	/* Prepare for the real work. */
	SHA256_Init(&ctx);

	/* Add the key string. */
	SHA256_Update(&ctx, key, key_len);

	/* The last part is the salt string. This must be at most 8
	 * characters and it ends at the first `$' character (for
	 * compatibility with existing implementations). */
	SHA256_Update(&ctx, salt, salt_len);

	/* Compute alternate SHA256 sum with input KEY, SALT, and KEY. The
	 * final result will be added to the first context. */
	SHA256_Init(&alt_ctx);

	/* Add key. */
	SHA256_Update(&alt_ctx, key, key_len);

	/* Add salt. */
	SHA256_Update(&alt_ctx, salt, salt_len);

	/* Add key again. */
	SHA256_Update(&alt_ctx, key, key_len);

	/* Now get result of this (32 bytes) and add it to the other context. */
	SHA256_Final(alt_result, &alt_ctx);

	/* Add for any character in the key one byte of the alternate sum. */
	for (cnt = key_len; cnt > 32; cnt -= 32)
		SHA256_Update(&ctx, alt_result, 32);
	SHA256_Update(&ctx, alt_result, cnt);

	/* Take the binary representation of the length of the key and for
	 * every 1 add the alternate sum, for every 0 the key. */
	for (cnt = key_len; cnt > 0; cnt >>= 1)
		if ((cnt & 1) != 0)
			SHA256_Update(&ctx, alt_result, 32);
		else
			SHA256_Update(&ctx, key, key_len);

	/* Create intermediate result. */
	SHA256_Final(alt_result, &ctx);

	/* Start computation of P byte sequence. */
	SHA256_Init(&alt_ctx);

	/* For every character in the password add the entire password. */
	for (cnt = 0; cnt < key_len; ++cnt)
		SHA256_Update(&alt_ctx, key, key_len);

	/* Finish the digest. */
	SHA256_Final(temp_result, &alt_ctx);

	/* Create byte sequence P. */
	cp = p_bytes = alloca(key_len);
	for (cnt = key_len; cnt >= 32; cnt -= 32) {
		memcpy(cp, temp_result, 32);
		cp += 32;
	}
	memcpy(cp, temp_result, cnt);

	/* Start computation of S byte sequence. */
	SHA256_Init(&alt_ctx);

	/* For every character in the password add the entire password. */
	for (cnt = 0; cnt < 16 + alt_result[0]; ++cnt)
		SHA256_Update(&alt_ctx, salt, salt_len);

	/* Finish the digest. */
	SHA256_Final(temp_result, &alt_ctx);

	/* Create byte sequence S. */
	cp = s_bytes = alloca(salt_len);
	for (cnt = salt_len; cnt >= 32; cnt -= 32) {
		memcpy(cp, temp_result, 32);
		cp += 32;
	}
	memcpy(cp, temp_result, cnt);

	/* Repeatedly run the collected hash value through SHA256 to burn CPU
	 * cycles. */
	for (cnt = 0; cnt < rounds; ++cnt) {
		/* New context. */
		SHA256_Init(&ctx);

		/* Add key or last result. */
		if ((cnt & 1) != 0)
			SHA256_Update(&ctx, p_bytes, key_len);
		else
			SHA256_Update(&ctx, alt_result, 32);

		/* Add salt for numbers not divisible by 3. */
		if (cnt % 3 != 0)
			SHA256_Update(&ctx, s_bytes, salt_len);

		/* Add key for numbers not divisible by 7. */
		if (cnt % 7 != 0)
			SHA256_Update(&ctx, p_bytes, key_len);

		/* Add key or last result. */
		if ((cnt & 1) != 0)
			SHA256_Update(&ctx, alt_result, 32);
		else
			SHA256_Update(&ctx, p_bytes, key_len);

		/* Create intermediate result. */
		SHA256_Final(alt_result, &ctx);
	}

	/* Now we can construct the result string. It consists of three
	 * parts. */
	cp = stpcpy(buffer, sha256_salt_prefix);

	if (rounds_custom)
		cp += sprintf(cp, "%s%zu$", sha256_rounds_prefix, rounds);

	cp = stpncpy(cp, salt, salt_len);

	*cp++ = '$';

	b64_from_24bit(alt_result[0], alt_result[10], alt_result[20], 4, &cp);
	b64_from_24bit(alt_result[21], alt_result[1], alt_result[11], 4, &cp);
	b64_from_24bit(alt_result[12], alt_result[22], alt_result[2], 4, &cp);
	b64_from_24bit(alt_result[3], alt_result[13], alt_result[23], 4, &cp);
	b64_from_24bit(alt_result[24], alt_result[4], alt_result[14], 4, &cp);
	b64_from_24bit(alt_result[15], alt_result[25], alt_result[5], 4, &cp);
	b64_from_24bit(alt_result[6], alt_result[16], alt_result[26], 4, &cp);
	b64_from_24bit(alt_result[27], alt_result[7], alt_result[17], 4, &cp);
	b64_from_24bit(alt_result[18], alt_result[28], alt_result[8], 4, &cp);
	b64_from_24bit(alt_result[9], alt_result[19], alt_result[29], 4, &cp);
	b64_from_24bit(0, alt_result[31], alt_result[30], 3, &cp);
	*cp = '\0';	/* Terminate the string. */

	/* Clear the buffer for the intermediate result so that people
	 * attaching to processes or reading core dumps cannot get any
	 * information. We do it in this way to clear correct_words[] inside
	 * the SHA256 implementation as well. */
	SHA256_Init(&ctx);
	SHA256_Final(alt_result, &ctx);
	memset(temp_result, '\0', sizeof(temp_result));
	memset(p_bytes, '\0', key_len);
	memset(s_bytes, '\0', salt_len);

	return (0);
}

#ifdef TEST

static const struct {
	const char *input;
	const char result[32];
} tests[] =
{
	/* Test vectors from FIPS 180-2: appendix B.1. */
	{
		"abc",
		"\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23"
		"\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad"
	},
	/* Test vectors from FIPS 180-2: appendix B.2. */
	{
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		"\x24\x8d\x6a\x61\xd2\x06\x38\xb8\xe5\xc0\x26\x93\x0c\x3e\x60\x39"
		"\xa3\x3c\xe4\x59\x64\xff\x21\x67\xf6\xec\xed\xd4\x19\xdb\x06\xc1"
	},
	/* Test vectors from the NESSIE project. */
	{
		"",
		"\xe3\xb0\xc4\x42\x98\xfc\x1c\x14\x9a\xfb\xf4\xc8\x99\x6f\xb9\x24"
		"\x27\xae\x41\xe4\x64\x9b\x93\x4c\xa4\x95\x99\x1b\x78\x52\xb8\x55"
	},
	{
		"a",
		"\xca\x97\x81\x12\xca\x1b\xbd\xca\xfa\xc2\x31\xb3\x9a\x23\xdc\x4d"
		"\xa7\x86\xef\xf8\x14\x7c\x4e\x72\xb9\x80\x77\x85\xaf\xee\x48\xbb"
	},
	{
		"message digest",
		"\xf7\x84\x6f\x55\xcf\x23\xe1\x4e\xeb\xea\xb5\xb4\xe1\x55\x0c\xad"
		"\x5b\x50\x9e\x33\x48\xfb\xc4\xef\xa3\xa1\x41\x3d\x39\x3c\xb6\x50"
	},
	{
		"abcdefghijklmnopqrstuvwxyz",
		"\x71\xc4\x80\xdf\x93\xd6\xae\x2f\x1e\xfa\xd1\x44\x7c\x66\xc9\x52"
		"\x5e\x31\x62\x18\xcf\x51\xfc\x8d\x9e\xd8\x32\xf2\xda\xf1\x8b\x73"
	},
	{
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		"\x24\x8d\x6a\x61\xd2\x06\x38\xb8\xe5\xc0\x26\x93\x0c\x3e\x60\x39"
		"\xa3\x3c\xe4\x59\x64\xff\x21\x67\xf6\xec\xed\xd4\x19\xdb\x06\xc1"
	},
	{
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
		"\xdb\x4b\xfc\xbd\x4d\xa0\xcd\x85\xa6\x0c\x3c\x37\xd3\xfb\xd8\x80"
		"\x5c\x77\xf1\x5f\xc6\xb1\xfd\xfe\x61\x4e\xe0\xa7\xc8\xfd\xb4\xc0"
	},
	{
		"123456789012345678901234567890123456789012345678901234567890"
		"12345678901234567890",
		"\xf3\x71\xbc\x4a\x31\x1f\x2b\x00\x9e\xef\x95\x2d\xd8\x3c\xa8\x0e"
		"\x2b\x60\x02\x6c\x8e\x93\x55\x92\xd0\xf9\xc3\x08\x45\x3c\x81\x3e"
	}
};

#define ntests (sizeof (tests) / sizeof (tests[0]))

static const struct {
	const char *salt;
	const char *input;
	const char *expected;
} tests2[] =
{
	{
		"$5$saltstring", "Hello world!",
		"$5$saltstring$5B8vYYiY.CVt1RlTTf8KbXBH3hsxY/GNooZaBBGWEc5"
	},
	{
		"$5$rounds=10000$saltstringsaltstring", "Hello world!",
		"$5$rounds=10000$saltstringsaltst$3xv.VbSHBb41AL9AvLeujZkZRBAwqFMz2."
		"opqey6IcA"
	},
	{
		"$5$rounds=5000$toolongsaltstring", "This is just a test",
		"$5$rounds=5000$toolongsaltstrin$Un/5jzAHMgOGZ5.mWJpuVolil07guHPvOW8"
		"mGRcvxa5"
	},
	{
		"$5$rounds=1400$anotherlongsaltstring",
		"a very much longer text to encrypt.  This one even stretches over more"
		"than one line.",
		"$5$rounds=1400$anotherlongsalts$Rx.j8H.h8HjEDGomFU8bDkXm3XIUnzyxf12"
		"oP84Bnq1"
	},
	{
		"$5$rounds=77777$short",
		"we have a short salt string but not a short password",
		"$5$rounds=77777$short$JiO1O3ZpDAxGJeaDIuqCoEFysAe1mZNJRs3pw0KQRd/"
	},
	{
		"$5$rounds=123456$asaltof16chars..", "a short string",
		"$5$rounds=123456$asaltof16chars..$gP3VQ/6X7UUEW3HkBn2w1/Ptq2jxPyzV/"
		"cZKmF/wJvD"
	},
	{
		"$5$rounds=10$roundstoolow", "the minimum number is still observed",
		"$5$rounds=1000$roundstoolow$yfvwcWrQ8l/K0DAWyuPMDNHpIVlTQebY9l/gL97"
		"2bIC"
	},
};

#define ntests2 (sizeof (tests2) / sizeof (tests2[0]))

int
main(void)
{
	SHA256_CTX ctx;
	uint8_t sum[32];
	int result = 0;
	int i, cnt;

	for (cnt = 0; cnt < (int)ntests; ++cnt) {
		SHA256_Init(&ctx);
		SHA256_Update(&ctx, tests[cnt].input, strlen(tests[cnt].input));
		SHA256_Final(sum, &ctx);
		if (memcmp(tests[cnt].result, sum, 32) != 0) {
			for (i = 0; i < 32; i++)
				printf("%02X", tests[cnt].result[i]);
			printf("\n");
			for (i = 0; i < 32; i++)
				printf("%02X", sum[i]);
			printf("\n");
			printf("test %d run %d failed\n", cnt, 1);
			result = 1;
		}

		SHA256_Init(&ctx);
		for (i = 0; tests[cnt].input[i] != '\0'; ++i)
			SHA256_Update(&ctx, &tests[cnt].input[i], 1);
		SHA256_Final(sum, &ctx);
		if (memcmp(tests[cnt].result, sum, 32) != 0) {
			for (i = 0; i < 32; i++)
				printf("%02X", tests[cnt].result[i]);
			printf("\n");
			for (i = 0; i < 32; i++)
				printf("%02X", sum[i]);
			printf("\n");
			printf("test %d run %d failed\n", cnt, 2);
			result = 1;
		}
	}

	/* Test vector from FIPS 180-2: appendix B.3. */
	char buf[1000];

	memset(buf, 'a', sizeof(buf));
	SHA256_Init(&ctx);
	for (i = 0; i < 1000; ++i)
		SHA256_Update(&ctx, buf, sizeof(buf));
	SHA256_Final(sum, &ctx);
	static const char expected[32] =
	"\xcd\xc7\x6e\x5c\x99\x14\xfb\x92\x81\xa1\xc7\xe2\x84\xd7\x3e\x67"
	"\xf1\x80\x9a\x48\xa4\x97\x20\x0e\x04\x6d\x39\xcc\xc7\x11\x2c\xd0";

	if (memcmp(expected, sum, 32) != 0) {
		printf("test %d failed\n", cnt);
		result = 1;
	}

	for (cnt = 0; cnt < ntests2; ++cnt) {
		char *cp = crypt_sha256(tests2[cnt].input, tests2[cnt].salt);

		if (strcmp(cp, tests2[cnt].expected) != 0) {
			printf("test %d: expected \"%s\", got \"%s\"\n",
			       cnt, tests2[cnt].expected, cp);
			result = 1;
		}
	}

	if (result == 0)
		puts("all tests OK");

	return result;
}

#endif /* TEST */
