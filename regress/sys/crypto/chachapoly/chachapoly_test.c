/*      $OpenBSD: chachapoly_test.c,v 1.3 2021/12/14 06:27:48 deraadt Exp $  */

/*
 * Copyright (c) 2010,2015 Mike Belopuhov <mikeb@openbsd.org>
 * Copyright (c) 2005 Markus Friedl <markus@openbsd.org>
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

#include <sys/types.h>
#include <crypto/chachapoly.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MINIMUM(a, b) (((a) < (b)) ? (a) : (b))

int debug = 0;

enum { TST_KEY, TST_IV, TST_AAD, TST_PLAIN, TST_CIPHER, TST_TAG, TST_NUM };

struct {
	char	*data[TST_NUM];
} tests[] = {
	/* Chacha20, counter=1 test vectors */

	/* Test vector from RFC7539 2.4.2 */
	{
		/* key + salt */
		"00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f "
		"10 11 12 13 14 15 16 17 18 19 1a 1b 1c 1d 1e 1f "
		"00 00 00 00",
		/* iv */
		"00 00 00 4a 00 00 00 00",
		/* aad */
		NULL,
		/* plaintext */
		"4c 61 64 69 65 73 20 61 6e 64 20 47 65 6e 74 6c "
		"65 6d 65 6e 20 6f 66 20 74 68 65 20 63 6c 61 73 "
		"73 20 6f 66 20 27 39 39 3a 20 49 66 20 49 20 63 "
		"6f 75 6c 64 20 6f 66 66 65 72 20 79 6f 75 20 6f "
		"6e 6c 79 20 6f 6e 65 20 74 69 70 20 66 6f 72 20 "
		"74 68 65 20 66 75 74 75 72 65 2c 20 73 75 6e 73 "
		"63 72 65 65 6e 20 77 6f 75 6c 64 20 62 65 20 69 "
		"74 2e",
		/* ciphertext */
		"6e 2e 35 9a 25 68 f9 80 41 ba 07 28 dd 0d 69 81 "
		"e9 7e 7a ec 1d 43 60 c2 0a 27 af cc fd 9f ae 0b "
		"f9 1b 65 c5 52 47 33 ab 8f 59 3d ab cd 62 b3 57 "
		"16 39 d6 24 e6 51 52 ab 8f 53 0c 35 9f 08 61 d8 "
		"07 ca 0d bf 50 0d 6a 61 56 a3 8e 08 8a 22 b6 5e "
		"52 bc 51 4d 16 cc f8 06 81 8c e9 1a b7 79 37 36 "
		"5a f9 0b bf 74 a3 5b e6 b4 0b 8e ed f2 78 5e 42 "
		"87 4d",
		/* tag */
		NULL
	},

	/* Test vector#2 from RFC7539 A.2 */
	{
		/* key + salt */
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 "
		"00 00 00 00",
		/* iv */
		"00 00 00 00 00 00 00 02",
		/* aad */
		NULL,
		/* plaintext */
		"41 6e 79 20 73 75 62 6d 69 73 73 69 6f 6e 20 74 "
		"6f 20 74 68 65 20 49 45 54 46 20 69 6e 74 65 6e "
		"64 65 64 20 62 79 20 74 68 65 20 43 6f 6e 74 72 "
		"69 62 75 74 6f 72 20 66 6f 72 20 70 75 62 6c 69 "
		"63 61 74 69 6f 6e 20 61 73 20 61 6c 6c 20 6f 72 "
		"20 70 61 72 74 20 6f 66 20 61 6e 20 49 45 54 46 "
		"20 49 6e 74 65 72 6e 65 74 2d 44 72 61 66 74 20 "
		"6f 72 20 52 46 43 20 61 6e 64 20 61 6e 79 20 73 "
		"74 61 74 65 6d 65 6e 74 20 6d 61 64 65 20 77 69 "
		"74 68 69 6e 20 74 68 65 20 63 6f 6e 74 65 78 74 "
		"20 6f 66 20 61 6e 20 49 45 54 46 20 61 63 74 69 "
		"76 69 74 79 20 69 73 20 63 6f 6e 73 69 64 65 72 "
		"65 64 20 61 6e 20 22 49 45 54 46 20 43 6f 6e 74 "
		"72 69 62 75 74 69 6f 6e 22 2e 20 53 75 63 68 20 "
		"73 74 61 74 65 6d 65 6e 74 73 20 69 6e 63 6c 75 "
		"64 65 20 6f 72 61 6c 20 73 74 61 74 65 6d 65 6e "
		"74 73 20 69 6e 20 49 45 54 46 20 73 65 73 73 69 "
		"6f 6e 73 2c 20 61 73 20 77 65 6c 6c 20 61 73 20 "
		"77 72 69 74 74 65 6e 20 61 6e 64 20 65 6c 65 63 "
		"74 72 6f 6e 69 63 20 63 6f 6d 6d 75 6e 69 63 61 "
		"74 69 6f 6e 73 20 6d 61 64 65 20 61 74 20 61 6e "
		"79 20 74 69 6d 65 20 6f 72 20 70 6c 61 63 65 2c "
		"20 77 68 69 63 68 20 61 72 65 20 61 64 64 72 65 "
		"73 73 65 64 20 74 6f",
		/* ciphertext */
		"a3 fb f0 7d f3 fa 2f de 4f 37 6c a2 3e 82 73 70 "
		"41 60 5d 9f 4f 4f 57 bd 8c ff 2c 1d 4b 79 55 ec "
		"2a 97 94 8b d3 72 29 15 c8 f3 d3 37 f7 d3 70 05 "
		"0e 9e 96 d6 47 b7 c3 9f 56 e0 31 ca 5e b6 25 0d "
		"40 42 e0 27 85 ec ec fa 4b 4b b5 e8 ea d0 44 0e "
		"20 b6 e8 db 09 d8 81 a7 c6 13 2f 42 0e 52 79 50 "
		"42 bd fa 77 73 d8 a9 05 14 47 b3 29 1c e1 41 1c "
		"68 04 65 55 2a a6 c4 05 b7 76 4d 5e 87 be a8 5a "
		"d0 0f 84 49 ed 8f 72 d0 d6 62 ab 05 26 91 ca 66 "
		"42 4b c8 6d 2d f8 0e a4 1f 43 ab f9 37 d3 25 9d "
		"c4 b2 d0 df b4 8a 6c 91 39 dd d7 f7 69 66 e9 28 "
		"e6 35 55 3b a7 6c 5c 87 9d 7b 35 d4 9e b2 e6 2b "
		"08 71 cd ac 63 89 39 e2 5e 8a 1e 0e f9 d5 28 0f "
		"a8 ca 32 8b 35 1c 3c 76 59 89 cb cf 3d aa 8b 6c "
		"cc 3a af 9f 39 79 c9 2b 37 20 fc 88 dc 95 ed 84 "
		"a1 be 05 9c 64 99 b9 fd a2 36 e7 e8 18 b0 4b 0b "
		"c3 9c 1e 87 6b 19 3b fe 55 69 75 3f 88 12 8c c0 "
		"8a aa 9b 63 d1 a1 6f 80 ef 25 54 d7 18 9c 41 1f "
		"58 69 ca 52 c5 b8 3f a3 6f f2 16 b9 c1 d3 00 62 "
		"be bc fd 2d c5 bc e0 91 19 34 fd a7 9a 86 f6 e6 "
		"98 ce d7 59 c3 ff 9b 64 77 33 8f 3d a4 f9 cd 85 "
		"14 ea 99 82 cc af b3 41 b2 38 4d d9 02 f3 d1 ab "
		"7a c6 1d d2 9c 6f 21 ba 5b 86 2f 37 30 e3 7c fd "
		"c4 fd 80 6c 22 f2 21",
		/* tag */
		NULL
	},

	/* Poly1305 test vectors */

	/* Test vector from RFC7539 2.8.2 */
	{
		/* key + salt */
		"80 81 82 83 84 85 86 87 88 89 8a 8b 8c 8d 8e 8f "
		"90 91 92 93 94 95 96 97 98 99 9a 9b 9c 9d 9e 9f "
		"07 00 00 00",
		/* iv */
		"40 41 42 43 44 45 46 47",
		/* aad */
		"50 51 52 53 c0 c1 c2 c3 c4 c5 c6 c7",
		/* plaintext */
		"4c 61 64 69 65 73 20 61 6e 64 20 47 65 6e 74 6c "
		"65 6d 65 6e 20 6f 66 20 74 68 65 20 63 6c 61 73 "
		"73 20 6f 66 20 27 39 39 3a 20 49 66 20 49 20 63 "
		"6f 75 6c 64 20 6f 66 66 65 72 20 79 6f 75 20 6f "
		"6e 6c 79 20 6f 6e 65 20 74 69 70 20 66 6f 72 20 "
		"74 68 65 20 66 75 74 75 72 65 2c 20 73 75 6e 73 "
		"63 72 65 65 6e 20 77 6f 75 6c 64 20 62 65 20 69 "
		"74 2e",
		/* ciphertext */
		"d3 1a 8d 34 64 8e 60 db 7b 86 af bc 53 ef 7e c2 "
		"a4 ad ed 51 29 6e 08 fe a9 e2 b5 a7 36 ee 62 d6 "
		"3d be a4 5e 8c a9 67 12 82 fa fb 69 da 92 72 8b "
		"1a 71 de 0a 9e 06 0b 29 05 d6 a5 b6 7e cd 3b 36 "
		"92 dd bd 7f 2d 77 8b 8c 98 03 ae e3 28 09 1b 58 "
		"fa b3 24 e4 fa d6 75 94 55 85 80 8b 48 31 d7 bc "
		"3f f4 de f0 8e 4b 7a 9d e5 76 d2 65 86 ce c6 4b "
		"61 16",
		/* tag */
		"1a e1 0b 59 4f 09 e2 6a 7e 90 2e cb d0 60 06 91"
	},

	/* Test vector from RFC7539 Appendix A.5 */
	{
		/* key + salt */
		"1c 92 40 a5 eb 55 d3 8a f3 33 88 86 04 f6 b5 f0 "
		"47 39 17 c1 40 2b 80 09 9d ca 5c bc 20 70 75 c0 "
		"00 00 00 00",
		/* iv */
		"01 02 03 04 05 06 07 08",
		/* aad */
		"f3 33 88 86 00 00 00 00 00 00 4e 91",
		/* plaintext */
		"49 6e 74 65 72 6e 65 74 2d 44 72 61 66 74 73 20 "
		"61 72 65 20 64 72 61 66 74 20 64 6f 63 75 6d 65 "
		"6e 74 73 20 76 61 6c 69 64 20 66 6f 72 20 61 20 "
		"6d 61 78 69 6d 75 6d 20 6f 66 20 73 69 78 20 6d "
		"6f 6e 74 68 73 20 61 6e 64 20 6d 61 79 20 62 65 "
		"20 75 70 64 61 74 65 64 2c 20 72 65 70 6c 61 63 "
		"65 64 2c 20 6f 72 20 6f 62 73 6f 6c 65 74 65 64 "
		"20 62 79 20 6f 74 68 65 72 20 64 6f 63 75 6d 65 "
		"6e 74 73 20 61 74 20 61 6e 79 20 74 69 6d 65 2e "
		"20 49 74 20 69 73 20 69 6e 61 70 70 72 6f 70 72 "
		"69 61 74 65 20 74 6f 20 75 73 65 20 49 6e 74 65 "
		"72 6e 65 74 2d 44 72 61 66 74 73 20 61 73 20 72 "
		"65 66 65 72 65 6e 63 65 20 6d 61 74 65 72 69 61 "
		"6c 20 6f 72 20 74 6f 20 63 69 74 65 20 74 68 65 "
		"6d 20 6f 74 68 65 72 20 74 68 61 6e 20 61 73 20 "
		"2f e2 80 9c 77 6f 72 6b 20 69 6e 20 70 72 6f 67 "
		"72 65 73 73 2e 2f e2 80 9d",
		/* ciphertext */
		"64 a0 86 15 75 86 1a f4 60 f0 62 c7 9b e6 43 bd "
		"5e 80 5c fd 34 5c f3 89 f1 08 67 0a c7 6c 8c b2 "
		"4c 6c fc 18 75 5d 43 ee a0 9e e9 4e 38 2d 26 b0 "
		"bd b7 b7 3c 32 1b 01 00 d4 f0 3b 7f 35 58 94 cf "
		"33 2f 83 0e 71 0b 97 ce 98 c8 a8 4a bd 0b 94 81 "
		"14 ad 17 6e 00 8d 33 bd 60 f9 82 b1 ff 37 c8 55 "
		"97 97 a0 6e f4 f0 ef 61 c1 86 32 4e 2b 35 06 38 "
		"36 06 90 7b 6a 7c 02 b0 f9 f6 15 7b 53 c8 67 e4 "
		"b9 16 6c 76 7b 80 4d 46 a5 9b 52 16 cd e7 a4 e9 "
		"90 40 c5 a4 04 33 22 5e e2 82 a1 b0 a0 6c 52 3e "
		"af 45 34 d7 f8 3f a1 15 5b 00 47 71 8c bc 54 6a "
		"0d 07 2b 04 b3 56 4e ea 1b 42 22 73 f5 48 27 1a "
		"0b b2 31 60 53 fa 76 99 19 55 eb d6 31 59 43 4e "
		"ce bb 4e 46 6d ae 5a 10 73 a6 72 76 27 09 7a 10 "
		"49 e6 17 d9 1d 36 10 94 fa 68 f0 ff 77 98 71 30 "
		"30 5b ea ba 2e da 04 df 99 7b 71 4d 6c 6f 2c 29 "
		"a6 ad 5c b4 02 2b 02 70 9b",
		/* tag */
		"ee ad 9d 67 89 0c bb 22 39 23 36 fe a1 85 1f 38"
	},

	/* Test vector from RFC7634 Appendix A */
	{
		/* key + salt */
		"80 81 82 83 84 85 86 87 88 89 8a 8b 8c 8d 8e 8f "
		"90 91 92 93 94 95 96 97 98 99 9a 9b 9c 9d 9e 9f "
		"a0 a1 a2 a3",
		/* iv */
		"10 11 12 13 14 15 16 17",
		/* aad */
		"01 02 03 04 00 00 00 05",
		/* plaintext */
		"45 00 00 54 a6 f2 00 00 40 01 e7 78 c6 33 64 05 "
		"c0 00 02 05 08 00 5b 7a 3a 08 00 00 55 3b ec 10 "
		"00 07 36 27 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 "
		"14 15 16 17 18 19 1a 1b 1c 1d 1e 1f 20 21 22 23 "
		"24 25 26 27 28 29 2a 2b 2c 2d 2e 2f 30 31 32 33 "
		"34 35 36 37 01 02 02 04",
		/* ciphertext */
		"24 03 94 28 b9 7f 41 7e 3c 13 75 3a 4f 05 08 7b "
		"67 c3 52 e6 a7 fa b1 b9 82 d4 66 ef 40 7a e5 c6 "
		"14 ee 80 99 d5 28 44 eb 61 aa 95 df ab 4c 02 f7 "
		"2a a7 1e 7c 4c 4f 64 c9 be fe 2f ac c6 38 e8 f3 "
		"cb ec 16 3f ac 46 9b 50 27 73 f6 fb 94 e6 64 da "
		"91 65 b8 28 29 f6 41 e0",
		/* tag */
		"76 aa a8 26 6b 7f b0 f7 b1 1b 36 99 07 e1 ad 43"
	},

	/* Test vector from RFC7634 Appendix B */
	{
		/* key + salt */
		"80 81 82 83 84 85 86 87 88 89 8a 8b 8c 8d 8e 8f "
		"90 91 92 93 94 95 96 97 98 99 9a 9b 9c 9d 9e 9f "
		"a0 a1 a2 a3",
		/* iv */
		"10 11 12 13 14 15 16 17",
		/* aad */
		"c0 c1 c2 c3 c4 c5 c6 c7 d0 d1 d2 d3 d4 d5 d6 d7 "
		"2e 20 25 00 00 00 00 09 00 00 00 45 29 00 00 29",
		/* plaintext */
		"00 00 00 0c 00 00 40 01 00 00 00 0a 00",
		/* ciphertext */
		"61 03 94 70 1f 8d 01 7f 7c 12 92 48 89",
		/* tag */
		"6b 71 bf e2 52 36 ef d7 cd c6 70 66 90 63 15 b2"
	},

	/* Chacha20 test cases from the libcrypto aeadtests.txt */
	{
		/* key + salt */
		"42 90 bc b1 54 17 35 31 f3 14 af 57 f3 be 3b 50 "
		"06 da 37 1e ce 27 2a fa 1b 5d bd d1 10 0a 10 07 "
		"00 00 00 00",
		/* iv */
		"cd 7c f6 7b e3 9c 79 4a",
		/* aad */
		NULL,
		/* plaintext */
		"86 d0 99 74 84 0b de d2 a5 ca",
		/* ciphertext */
		"e3 e4 46 f7 ed e9 a1 9b 62 a4",
		/* tag */
		NULL
	}
};

static void
dochacha(unsigned char *key, size_t klen, unsigned char *iv,
    size_t ivlen, unsigned char *plain, size_t plen)
{
	struct chacha20_ctx ctx;
	uint8_t blk[CHACHA20_BLOCK_LEN];
	int i;

	memset(&ctx, 0, sizeof ctx);

	if (chacha20_setkey(&ctx, key, CHACHA20_KEYSIZE + CHACHA20_SALT))
		errx(1, "chacha20_setkey");
	chacha20_reinit((caddr_t)&ctx, iv);

	while (plen >= CHACHA20_BLOCK_LEN) {
		chacha20_crypt((caddr_t)&ctx, plain);
		plain += CHACHA20_BLOCK_LEN;
		plen -= CHACHA20_BLOCK_LEN;
	}
	if (plen > 0) {
		for (i = 0; i < plen; i++)
			blk[i] = plain[i];
		for (; i < CHACHA20_BLOCK_LEN; i++)
			blk[i] = 0;
		chacha20_crypt((caddr_t)&ctx, blk);
		memcpy(plain, blk, plen);
	}
}

static void
dopoly(const unsigned char *key, size_t klen,
    const unsigned char *iv, size_t ivlen,
    const unsigned char *aad, size_t aadlen,
    const unsigned char *in, unsigned char *out, size_t len)
{
	CHACHA20_POLY1305_CTX ctx;
	uint8_t blk[CHACHA20_BLOCK_LEN];
	uint32_t *p;
	int i;

	Chacha20_Poly1305_Init(&ctx);

	Chacha20_Poly1305_Setkey(&ctx, key, klen);

	Chacha20_Poly1305_Reinit(&ctx, iv, ivlen);

	for (i = 0; i < aadlen; i += POLY1305_BLOCK_LEN) {
		memset(blk, 0, POLY1305_BLOCK_LEN);
		memcpy(blk, aad + i, MINIMUM(aadlen - i, POLY1305_BLOCK_LEN));
		Chacha20_Poly1305_Update(&ctx, blk, POLY1305_BLOCK_LEN);
	}

	for (i = 0; i < len; i += CHACHA20_BLOCK_LEN) {
		int dlen = MINIMUM(len - i, CHACHA20_BLOCK_LEN);
		Chacha20_Poly1305_Update(&ctx, in + i, dlen);
	}

	bzero(blk, sizeof blk);
	p = (uint32_t *)blk;
	*p = htole32(aadlen);
	p = (uint32_t *)blk + 2;
	*p = htole32(len);
	Chacha20_Poly1305_Update(&ctx, blk, POLY1305_BLOCK_LEN);

	Chacha20_Poly1305_Final(out, &ctx);
}

static int
match(unsigned char *a, unsigned char *b, size_t len)
{
	int i;

	if (memcmp(a, b, len) == 0)
		return (1);

	warnx("mismatch");

	for (i = 0; i < len; i++)
		printf("%2.2x", a[i]);
	printf("\n");
	for (i = 0; i < len; i++)
		printf("%2.2x", b[i]);
	printf("\n");

	return (0);
}

static int
run(int num)
{
	int i, fail = 1, len, j, length[TST_NUM];
	u_long val;
	char *ep, *from;
	u_char *p, *data[TST_NUM], tag[POLY1305_TAGLEN];
	u_char *ciphertext;

	for (i = 0; i < TST_NUM; i++)
		data[i] = NULL;
	for (i = 0; i < TST_NUM; i++) {
		from = tests[num].data[i];
		if (debug)
			printf("%s\n", from);
		if (!from) {
			length[i] = 0;
			data[i] = NULL;
			continue;
		}
		len = strlen(from);
		if ((p = malloc(len)) == NULL) {
			warn("malloc");
			goto done;
		}
		errno = 0;
		for (j = 0; j < len; j++) {
			val = strtoul(&from[j*3], &ep, 16);
			p[j] = (u_char)val;
			if (*ep == '\0' || errno)
				break;
		}
		length[i] = j+1;
		data[i] = p;
	}

	if (length[TST_PLAIN] != 0) {
		dochacha(data[TST_KEY], length[TST_KEY], data[TST_IV],
		    length[TST_IV], data[TST_PLAIN], length[TST_PLAIN]);
		fail = !match(data[TST_CIPHER], data[TST_PLAIN],
		    length[TST_PLAIN]);
		printf("%s test vector %d (Chacha20)\n",
		    fail ? "FAILED" : "OK", num);
	}
	if (length[TST_AAD] != 0) {
		dopoly(data[TST_KEY], length[TST_KEY], data[TST_IV],
		    length[TST_IV], data[TST_AAD], length[TST_AAD],
		    data[TST_CIPHER], tag, length[TST_CIPHER]);
		fail = !match(data[TST_TAG], tag, POLY1305_TAGLEN);
		printf("%s test vector %d (Poly1305)\n",
		    fail ? "FAILED" : "OK", num);
	}

 done:
	for (i = 0; i < TST_NUM; i++)
		free(data[i]);
	return (fail);
}

int
main(void)
{
	int i, fail = 0;

	for (i = 0; i < (sizeof(tests) / sizeof(tests[0])); i++)
		fail += run(i);

	return (fail > 0 ? 1 : 0);
}
