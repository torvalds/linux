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
 * SHA512-based Unix crypt implementation. Released into the Public Domain by
 * Ulrich Drepper <drepper@redhat.com>. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/param.h>

#include <errno.h>
#include <limits.h>
#include <sha512.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypt.h"

/* Define our magic string to mark salt for SHA512 "encryption" replacement. */
static const char sha512_salt_prefix[] = "$6$";

/* Prefix for optional rounds specification. */
static const char sha512_rounds_prefix[] = "rounds=";

/* Maximum salt string length. */
#define SALT_LEN_MAX 16
/* Default number of rounds if not explicitly specified. */
#define ROUNDS_DEFAULT 5000
/* Minimum number of rounds. */
#define ROUNDS_MIN 1000
/* Maximum number of rounds. */
#define ROUNDS_MAX 999999999

int
crypt_sha512(const char *key, const char *salt, char *buffer)
{
	u_long srounds;
	uint8_t alt_result[64], temp_result[64];
	SHA512_CTX ctx, alt_ctx;
	size_t salt_len, key_len, cnt, rounds;
	char *cp, *p_bytes, *s_bytes, *endp;
	const char *num;
	bool rounds_custom;

	/* Default number of rounds. */
	rounds = ROUNDS_DEFAULT;
	rounds_custom = false;

	/* Find beginning of salt string. The prefix should normally always
	 * be present. Just in case it is not. */
	if (strncmp(sha512_salt_prefix, salt, sizeof(sha512_salt_prefix) - 1) == 0)
		/* Skip salt prefix. */
		salt += sizeof(sha512_salt_prefix) - 1;

	if (strncmp(salt, sha512_rounds_prefix, sizeof(sha512_rounds_prefix) - 1)
	    == 0) {
		num = salt + sizeof(sha512_rounds_prefix) - 1;
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
	SHA512_Init(&ctx);

	/* Add the key string. */
	SHA512_Update(&ctx, key, key_len);

	/* The last part is the salt string. This must be at most 8
	 * characters and it ends at the first `$' character (for
	 * compatibility with existing implementations). */
	SHA512_Update(&ctx, salt, salt_len);

	/* Compute alternate SHA512 sum with input KEY, SALT, and KEY. The
	 * final result will be added to the first context. */
	SHA512_Init(&alt_ctx);

	/* Add key. */
	SHA512_Update(&alt_ctx, key, key_len);

	/* Add salt. */
	SHA512_Update(&alt_ctx, salt, salt_len);

	/* Add key again. */
	SHA512_Update(&alt_ctx, key, key_len);

	/* Now get result of this (64 bytes) and add it to the other context. */
	SHA512_Final(alt_result, &alt_ctx);

	/* Add for any character in the key one byte of the alternate sum. */
	for (cnt = key_len; cnt > 64; cnt -= 64)
		SHA512_Update(&ctx, alt_result, 64);
	SHA512_Update(&ctx, alt_result, cnt);

	/* Take the binary representation of the length of the key and for
	 * every 1 add the alternate sum, for every 0 the key. */
	for (cnt = key_len; cnt > 0; cnt >>= 1)
		if ((cnt & 1) != 0)
			SHA512_Update(&ctx, alt_result, 64);
		else
			SHA512_Update(&ctx, key, key_len);

	/* Create intermediate result. */
	SHA512_Final(alt_result, &ctx);

	/* Start computation of P byte sequence. */
	SHA512_Init(&alt_ctx);

	/* For every character in the password add the entire password. */
	for (cnt = 0; cnt < key_len; ++cnt)
		SHA512_Update(&alt_ctx, key, key_len);

	/* Finish the digest. */
	SHA512_Final(temp_result, &alt_ctx);

	/* Create byte sequence P. */
	cp = p_bytes = alloca(key_len);
	for (cnt = key_len; cnt >= 64; cnt -= 64) {
		memcpy(cp, temp_result, 64);
		cp += 64;
	}
	memcpy(cp, temp_result, cnt);

	/* Start computation of S byte sequence. */
	SHA512_Init(&alt_ctx);

	/* For every character in the password add the entire password. */
	for (cnt = 0; cnt < 16 + alt_result[0]; ++cnt)
		SHA512_Update(&alt_ctx, salt, salt_len);

	/* Finish the digest. */
	SHA512_Final(temp_result, &alt_ctx);

	/* Create byte sequence S. */
	cp = s_bytes = alloca(salt_len);
	for (cnt = salt_len; cnt >= 64; cnt -= 64) {
		memcpy(cp, temp_result, 64);
		cp += 64;
	}
	memcpy(cp, temp_result, cnt);

	/* Repeatedly run the collected hash value through SHA512 to burn CPU
	 * cycles. */
	for (cnt = 0; cnt < rounds; ++cnt) {
		/* New context. */
		SHA512_Init(&ctx);

		/* Add key or last result. */
		if ((cnt & 1) != 0)
			SHA512_Update(&ctx, p_bytes, key_len);
		else
			SHA512_Update(&ctx, alt_result, 64);

		/* Add salt for numbers not divisible by 3. */
		if (cnt % 3 != 0)
			SHA512_Update(&ctx, s_bytes, salt_len);

		/* Add key for numbers not divisible by 7. */
		if (cnt % 7 != 0)
			SHA512_Update(&ctx, p_bytes, key_len);

		/* Add key or last result. */
		if ((cnt & 1) != 0)
			SHA512_Update(&ctx, alt_result, 64);
		else
			SHA512_Update(&ctx, p_bytes, key_len);

		/* Create intermediate result. */
		SHA512_Final(alt_result, &ctx);
	}

	/* Now we can construct the result string. It consists of three
	 * parts. */
	cp = stpcpy(buffer, sha512_salt_prefix);

	if (rounds_custom)
		cp += sprintf(cp, "%s%zu$", sha512_rounds_prefix, rounds);

	cp = stpncpy(cp, salt, salt_len);

	*cp++ = '$';

	b64_from_24bit(alt_result[0], alt_result[21], alt_result[42], 4, &cp);
	b64_from_24bit(alt_result[22], alt_result[43], alt_result[1], 4, &cp);
	b64_from_24bit(alt_result[44], alt_result[2], alt_result[23], 4, &cp);
	b64_from_24bit(alt_result[3], alt_result[24], alt_result[45], 4, &cp);
	b64_from_24bit(alt_result[25], alt_result[46], alt_result[4], 4, &cp);
	b64_from_24bit(alt_result[47], alt_result[5], alt_result[26], 4, &cp);
	b64_from_24bit(alt_result[6], alt_result[27], alt_result[48], 4, &cp);
	b64_from_24bit(alt_result[28], alt_result[49], alt_result[7], 4, &cp);
	b64_from_24bit(alt_result[50], alt_result[8], alt_result[29], 4, &cp);
	b64_from_24bit(alt_result[9], alt_result[30], alt_result[51], 4, &cp);
	b64_from_24bit(alt_result[31], alt_result[52], alt_result[10], 4, &cp);
	b64_from_24bit(alt_result[53], alt_result[11], alt_result[32], 4, &cp);
	b64_from_24bit(alt_result[12], alt_result[33], alt_result[54], 4, &cp);
	b64_from_24bit(alt_result[34], alt_result[55], alt_result[13], 4, &cp);
	b64_from_24bit(alt_result[56], alt_result[14], alt_result[35], 4, &cp);
	b64_from_24bit(alt_result[15], alt_result[36], alt_result[57], 4, &cp);
	b64_from_24bit(alt_result[37], alt_result[58], alt_result[16], 4, &cp);
	b64_from_24bit(alt_result[59], alt_result[17], alt_result[38], 4, &cp);
	b64_from_24bit(alt_result[18], alt_result[39], alt_result[60], 4, &cp);
	b64_from_24bit(alt_result[40], alt_result[61], alt_result[19], 4, &cp);
	b64_from_24bit(alt_result[62], alt_result[20], alt_result[41], 4, &cp);
	b64_from_24bit(0, 0, alt_result[63], 2, &cp);

	*cp = '\0';	/* Terminate the string. */

	/* Clear the buffer for the intermediate result so that people
	 * attaching to processes or reading core dumps cannot get any
	 * information. We do it in this way to clear correct_words[] inside
	 * the SHA512 implementation as well. */
	SHA512_Init(&ctx);
	SHA512_Final(alt_result, &ctx);
	memset(temp_result, '\0', sizeof(temp_result));
	memset(p_bytes, '\0', key_len);
	memset(s_bytes, '\0', salt_len);

	return (0);
}

#ifdef TEST

static const struct {
	const char *input;
	const char result[64];
} tests[] =
{
	/* Test vectors from FIPS 180-2: appendix C.1. */
	{
		"abc",
		"\xdd\xaf\x35\xa1\x93\x61\x7a\xba\xcc\x41\x73\x49\xae\x20\x41\x31"
		"\x12\xe6\xfa\x4e\x89\xa9\x7e\xa2\x0a\x9e\xee\xe6\x4b\x55\xd3\x9a"
		"\x21\x92\x99\x2a\x27\x4f\xc1\xa8\x36\xba\x3c\x23\xa3\xfe\xeb\xbd"
		"\x45\x4d\x44\x23\x64\x3c\xe8\x0e\x2a\x9a\xc9\x4f\xa5\x4c\xa4\x9f"
	},
	/* Test vectors from FIPS 180-2: appendix C.2. */
	{
		"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
		"hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
		"\x8e\x95\x9b\x75\xda\xe3\x13\xda\x8c\xf4\xf7\x28\x14\xfc\x14\x3f"
		"\x8f\x77\x79\xc6\xeb\x9f\x7f\xa1\x72\x99\xae\xad\xb6\x88\x90\x18"
		"\x50\x1d\x28\x9e\x49\x00\xf7\xe4\x33\x1b\x99\xde\xc4\xb5\x43\x3a"
		"\xc7\xd3\x29\xee\xb6\xdd\x26\x54\x5e\x96\xe5\x5b\x87\x4b\xe9\x09"
	},
	/* Test vectors from the NESSIE project. */
	{
		"",
		"\xcf\x83\xe1\x35\x7e\xef\xb8\xbd\xf1\x54\x28\x50\xd6\x6d\x80\x07"
		"\xd6\x20\xe4\x05\x0b\x57\x15\xdc\x83\xf4\xa9\x21\xd3\x6c\xe9\xce"
		"\x47\xd0\xd1\x3c\x5d\x85\xf2\xb0\xff\x83\x18\xd2\x87\x7e\xec\x2f"
		"\x63\xb9\x31\xbd\x47\x41\x7a\x81\xa5\x38\x32\x7a\xf9\x27\xda\x3e"
	},
	{
		"a",
		"\x1f\x40\xfc\x92\xda\x24\x16\x94\x75\x09\x79\xee\x6c\xf5\x82\xf2"
		"\xd5\xd7\xd2\x8e\x18\x33\x5d\xe0\x5a\xbc\x54\xd0\x56\x0e\x0f\x53"
		"\x02\x86\x0c\x65\x2b\xf0\x8d\x56\x02\x52\xaa\x5e\x74\x21\x05\x46"
		"\xf3\x69\xfb\xbb\xce\x8c\x12\xcf\xc7\x95\x7b\x26\x52\xfe\x9a\x75"
	},
	{
		"message digest",
		"\x10\x7d\xbf\x38\x9d\x9e\x9f\x71\xa3\xa9\x5f\x6c\x05\x5b\x92\x51"
		"\xbc\x52\x68\xc2\xbe\x16\xd6\xc1\x34\x92\xea\x45\xb0\x19\x9f\x33"
		"\x09\xe1\x64\x55\xab\x1e\x96\x11\x8e\x8a\x90\x5d\x55\x97\xb7\x20"
		"\x38\xdd\xb3\x72\xa8\x98\x26\x04\x6d\xe6\x66\x87\xbb\x42\x0e\x7c"
	},
	{
		"abcdefghijklmnopqrstuvwxyz",
		"\x4d\xbf\xf8\x6c\xc2\xca\x1b\xae\x1e\x16\x46\x8a\x05\xcb\x98\x81"
		"\xc9\x7f\x17\x53\xbc\xe3\x61\x90\x34\x89\x8f\xaa\x1a\xab\xe4\x29"
		"\x95\x5a\x1b\xf8\xec\x48\x3d\x74\x21\xfe\x3c\x16\x46\x61\x3a\x59"
		"\xed\x54\x41\xfb\x0f\x32\x13\x89\xf7\x7f\x48\xa8\x79\xc7\xb1\xf1"
	},
	{
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		"\x20\x4a\x8f\xc6\xdd\xa8\x2f\x0a\x0c\xed\x7b\xeb\x8e\x08\xa4\x16"
		"\x57\xc1\x6e\xf4\x68\xb2\x28\xa8\x27\x9b\xe3\x31\xa7\x03\xc3\x35"
		"\x96\xfd\x15\xc1\x3b\x1b\x07\xf9\xaa\x1d\x3b\xea\x57\x78\x9c\xa0"
		"\x31\xad\x85\xc7\xa7\x1d\xd7\x03\x54\xec\x63\x12\x38\xca\x34\x45"
	},
	{
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
		"\x1e\x07\xbe\x23\xc2\x6a\x86\xea\x37\xea\x81\x0c\x8e\xc7\x80\x93"
		"\x52\x51\x5a\x97\x0e\x92\x53\xc2\x6f\x53\x6c\xfc\x7a\x99\x96\xc4"
		"\x5c\x83\x70\x58\x3e\x0a\x78\xfa\x4a\x90\x04\x1d\x71\xa4\xce\xab"
		"\x74\x23\xf1\x9c\x71\xb9\xd5\xa3\xe0\x12\x49\xf0\xbe\xbd\x58\x94"
	},
	{
		"123456789012345678901234567890123456789012345678901234567890"
		"12345678901234567890",
		"\x72\xec\x1e\xf1\x12\x4a\x45\xb0\x47\xe8\xb7\xc7\x5a\x93\x21\x95"
		"\x13\x5b\xb6\x1d\xe2\x4e\xc0\xd1\x91\x40\x42\x24\x6e\x0a\xec\x3a"
		"\x23\x54\xe0\x93\xd7\x6f\x30\x48\xb4\x56\x76\x43\x46\x90\x0c\xb1"
		"\x30\xd2\xa4\xfd\x5d\xd1\x6a\xbb\x5e\x30\xbc\xb8\x50\xde\xe8\x43"
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
		"$6$saltstring", "Hello world!",
		"$6$saltstring$svn8UoSVapNtMuq1ukKS4tPQd8iKwSMHWjl/O817G3uBnIFNjnQJu"
		"esI68u4OTLiBFdcbYEdFCoEOfaS35inz1"
	},
	{
		"$6$rounds=10000$saltstringsaltstring", "Hello world!",
		"$6$rounds=10000$saltstringsaltst$OW1/O6BYHV6BcXZu8QVeXbDWra3Oeqh0sb"
		"HbbMCVNSnCM/UrjmM0Dp8vOuZeHBy/YTBmSK6H9qs/y3RnOaw5v."
	},
	{
		"$6$rounds=5000$toolongsaltstring", "This is just a test",
		"$6$rounds=5000$toolongsaltstrin$lQ8jolhgVRVhY4b5pZKaysCLi0QBxGoNeKQ"
		"zQ3glMhwllF7oGDZxUhx1yxdYcz/e1JSbq3y6JMxxl8audkUEm0"
	},
	{
		"$6$rounds=1400$anotherlongsaltstring",
		"a very much longer text to encrypt.  This one even stretches over more"
		"than one line.",
		"$6$rounds=1400$anotherlongsalts$POfYwTEok97VWcjxIiSOjiykti.o/pQs.wP"
		"vMxQ6Fm7I6IoYN3CmLs66x9t0oSwbtEW7o7UmJEiDwGqd8p4ur1"
	},
	{
		"$6$rounds=77777$short",
		"we have a short salt string but not a short password",
		"$6$rounds=77777$short$WuQyW2YR.hBNpjjRhpYD/ifIw05xdfeEyQoMxIXbkvr0g"
		"ge1a1x3yRULJ5CCaUeOxFmtlcGZelFl5CxtgfiAc0"
	},
	{
		"$6$rounds=123456$asaltof16chars..", "a short string",
		"$6$rounds=123456$asaltof16chars..$BtCwjqMJGx5hrJhZywWvt0RLE8uZ4oPwc"
		"elCjmw2kSYu.Ec6ycULevoBK25fs2xXgMNrCzIMVcgEJAstJeonj1"
	},
	{
		"$6$rounds=10$roundstoolow", "the minimum number is still observed",
		"$6$rounds=1000$roundstoolow$kUMsbe306n21p9R.FRkW3IGn.S9NPN0x50YhH1x"
		"hLsPuWGsUSklZt58jaTfF4ZEQpyUNGc0dqbpBYYBaHHrsX."
	},
};

#define ntests2 (sizeof (tests2) / sizeof (tests2[0]))

int
main(void)
{
	SHA512_CTX ctx;
	uint8_t sum[64];
	int result = 0;
	int i, cnt;

	for (cnt = 0; cnt < (int)ntests; ++cnt) {
		SHA512_Init(&ctx);
		SHA512_Update(&ctx, tests[cnt].input, strlen(tests[cnt].input));
		SHA512_Final(sum, &ctx);
		if (memcmp(tests[cnt].result, sum, 64) != 0) {
			printf("test %d run %d failed\n", cnt, 1);
			result = 1;
		}

		SHA512_Init(&ctx);
		for (i = 0; tests[cnt].input[i] != '\0'; ++i)
			SHA512_Update(&ctx, &tests[cnt].input[i], 1);
		SHA512_Final(sum, &ctx);
		if (memcmp(tests[cnt].result, sum, 64) != 0) {
			printf("test %d run %d failed\n", cnt, 2);
			result = 1;
		}
	}

	/* Test vector from FIPS 180-2: appendix C.3. */
	char buf[1000];

	memset(buf, 'a', sizeof(buf));
	SHA512_Init(&ctx);
	for (i = 0; i < 1000; ++i)
		SHA512_Update(&ctx, buf, sizeof(buf));
	SHA512_Final(sum, &ctx);
	static const char expected[64] =
	"\xe7\x18\x48\x3d\x0c\xe7\x69\x64\x4e\x2e\x42\xc7\xbc\x15\xb4\x63"
	"\x8e\x1f\x98\xb1\x3b\x20\x44\x28\x56\x32\xa8\x03\xaf\xa9\x73\xeb"
	"\xde\x0f\xf2\x44\x87\x7e\xa6\x0a\x4c\xb0\x43\x2c\xe5\x77\xc3\x1b"
	"\xeb\x00\x9c\x5c\x2c\x49\xaa\x2e\x4e\xad\xb2\x17\xad\x8c\xc0\x9b";

	if (memcmp(expected, sum, 64) != 0) {
		printf("test %d failed\n", cnt);
		result = 1;
	}

	for (cnt = 0; cnt < ntests2; ++cnt) {
		char *cp = crypt_sha512(tests2[cnt].input, tests2[cnt].salt);

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
