/*      $OpenBSD: gmac_test.c,v 1.7 2021/12/14 06:27:48 deraadt Exp $  */

/*
 * Copyright (c) 2010 Mike Belopuhov <mikeb@openbsd.org>
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
#include <crypto/aes.h>
#include <crypto/gmac.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MINIMUM(a, b)       (((a) < (b)) ? (a) : (b))

int debug = 0;

enum { TST_KEY, TST_IV, TST_AAD, TST_CIPHER, TST_TAG, TST_NUM };

struct {
	char	*data[TST_NUM];
} tests[] = {
	/* Test vectors from gcm-spec.pdf (initial proposal to NIST) */

	/* 128 bit key */

	/* Test Case 1 */
	{
		/* key + salt */
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00",
		/* iv */
		"00 00 00 00 00 00 00 00",
		/* aad */
		NULL,
		/* ciphertext */
		NULL,
		/* tag */
		"58 e2 fc ce fa 7e 30 61 36 7f 1d 57 a4 e7 45 5a"
	},
	/* Test Case 2 */
	{
		/* key + salt */
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00",
		/* iv */
		"00 00 00 00 00 00 00 00",
		/* aad */
		NULL,
		/* ciphertext */
		"03 88 da ce 60 b6 a3 92 f3 28 c2 b9 71 b2 fe 78",
		/* tag */
		"ab 6e 47 d4 2c ec 13 bd f5 3a 67 b2 12 57 bd df"
	},
	/* Test Case 3 */
	{
		/* key + salt */
		"fe ff e9 92 86 65 73 1c 6d 6a 8f 94 67 30 83 08 "
		"ca fe ba be",
		/* iv */
		"fa ce db ad de ca f8 88",
		/* aad */
		NULL,
		/* ciphertext */
		"42 83 1e c2 21 77 74 24 4b 72 21 b7 84 d0 d4 9c "
		"e3 aa 21 2f 2c 02 a4 e0 35 c1 7e 23 29 ac a1 2e "
		"21 d5 14 b2 54 66 93 1c 7d 8f 6a 5a ac 84 aa 05 "
		"1b a3 0b 39 6a 0a ac 97 3d 58 e0 91 47 3f 59 85",
		/* tag */
		"4d 5c 2a f3 27 cd 64 a6 2c f3 5a bd 2b a6 fa b4"
	},
	/* Test Case 4 */
	{
		/* key + salt */
		"fe ff e9 92 86 65 73 1c 6d 6a 8f 94 67 30 83 08 "
		"ca fe ba be",
		/* iv */
		"fa ce db ad de ca f8 88",
		/* aad */
		"fe ed fa ce de ad be ef fe ed fa ce de ad be ef "
		"ab ad da d2",
		/* ciphertext */
		"42 83 1e c2 21 77 74 24 4b 72 21 b7 84 d0 d4 9c "
		"e3 aa 21 2f 2c 02 a4 e0 35 c1 7e 23 29 ac a1 2e "
		"21 d5 14 b2 54 66 93 1c 7d 8f 6a 5a ac 84 aa 05 "
		"1b a3 0b 39 6a 0a ac 97 3d 58 e0 91",
		/* tag */
		"5b c9 4f bc 32 21 a5 db 94 fa e9 5a e7 12 1a 47"
	},

	/* 192 bit key */

	/* Test Case 7 */
	{
		/* key + salt */
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00 00 00 00 00 "
		"00 00 00 00",
		/* iv */
		"00 00 00 00 00 00 00 00",
		/* aad */
		NULL,
		/* ciphertext */
		NULL,
		/* tag */
		"cd 33 b2 8a c7 73 f7 4b a0 0e d1 f3 12 57 24 35"
	},
	/* Test Case 8 */
	{
		/* key + salt */
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00 00 00 00 00 "
		"00 00 00 00",
		/* iv */
		"00 00 00 00 00 00 00 00",
		/* aad */
		NULL,
		/* ciphertext */
		"98 e7 24 7c 07 f0 fe 41 1c 26 7e 43 84 b0 f6 00",
		/* tag */
		"2f f5 8d 80 03 39 27 ab 8e f4 d4 58 75 14 f0 fb"
	},
	/* Test Case 9 */
	{
		/* key + salt */
		"fe ff e9 92 86 65 73 1c 6d 6a 8f 94 67 30 83 08 "
		"fe ff e9 92 86 65 73 1c "
		"ca fe ba be",
		/* iv */
		"fa ce db ad de ca f8 88",
		/* aad */
		NULL,
		/* ciphertext */
		"39 80 ca 0b 3c 00 e8 41 eb 06 fa c4 87 2a 27 57 "
		"85 9e 1c ea a6 ef d9 84 62 85 93 b4 0c a1 e1 9c "
		"7d 77 3d 00 c1 44 c5 25 ac 61 9d 18 c8 4a 3f 47 "
		"18 e2 44 8b 2f e3 24 d9 cc da 27 10 ac ad e2 56",
		/* tag */
		"99 24 a7 c8 58 73 36 bf b1 18 02 4d b8 67 4a 14"
	},
	/* Test Case 10 */
	{
		/* key + salt */
		"fe ff e9 92 86 65 73 1c 6d 6a 8f 94 67 30 83 08 "
		"fe ff e9 92 86 65 73 1c "
		"ca fe ba be",
		/* iv */
		"fa ce db ad de ca f8 88",
		/* aad */
		"fe ed fa ce de ad be ef fe ed fa ce de ad be ef "
		"ab ad da d2",
		/* ciphertext */
		"39 80 ca 0b 3c 00 e8 41 eb 06 fa c4 87 2a 27 57 "
		"85 9e 1c ea a6 ef d9 84 62 85 93 b4 0c a1 e1 9c "
		"7d 77 3d 00 c1 44 c5 25 ac 61 9d 18 c8 4a 3f 47 "
		"18 e2 44 8b 2f e3 24 d9 cc da 27 10",
		/* tag */
		"25 19 49 8e 80 f1 47 8f 37 ba 55 bd 6d 27 61 8c"
	},

	/* 256 bit key */

	/* Test Case 13 */
	{
		/* key + salt */
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00",
		/* iv */
		"00 00 00 00 00 00 00 00",
		/* aad */
		NULL,
		/* ciphertext */
		NULL,
		/* tag */
		"53 0f 8a fb c7 45 36 b9 a9 63 b4 f1 c4 cb 73 8b"
	},
	/* Test Case 14 */
	{
		/* key + salt */
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00",
		/* iv */
		"00 00 00 00 00 00 00 00",
		/* aad */
		NULL,
		/* ciphertext */
		"ce a7 40 3d 4d 60 6b 6e 07 4e c5 d3 ba f3 9d 18",
		/* tag */
		"d0 d1 c8 a7 99 99 6b f0 26 5b 98 b5 d4 8a b9 19"
	},
	/* Test Case 15 */
	{
		/* key + salt */
		"fe ff e9 92 86 65 73 1c 6d 6a 8f 94 67 30 83 08 "
		"fe ff e9 92 86 65 73 1c 6d 6a 8f 94 67 30 83 08 "
		"ca fe ba be",
		/* iv */
		"fa ce db ad de ca f8 88",
		/* aad */
		NULL,
		/* ciphertext */
		"52 2d c1 f0 99 56 7d 07 f4 7f 37 a3 2a 84 42 7d "
		"64 3a 8c dc bf e5 c0 c9 75 98 a2 bd 25 55 d1 aa "
		"8c b0 8e 48 59 0d bb 3d a7 b0 8b 10 56 82 88 38 "
		"c5 f6 1e 63 93 ba 7a 0a bc c9 f6 62 89 80 15 ad",
		/* tag */
		"b0 94 da c5 d9 34 71 bd ec 1a 50 22 70 e3 cc 6c"
	},
	/* Test Case 16 */
	{
		/* key + salt */
		"fe ff e9 92 86 65 73 1c 6d 6a 8f 94 67 30 83 08 "
		"fe ff e9 92 86 65 73 1c 6d 6a 8f 94 67 30 83 08 "
		"ca fe ba be",
		/* iv */
		"fa ce db ad de ca f8 88",
		/* aad */
		"fe ed fa ce de ad be ef fe ed fa ce de ad be ef "
		"ab ad da d2",
		/* ciphertext */
		"52 2d c1 f0 99 56 7d 07 f4 7f 37 a3 2a 84 42 7d "
		"64 3a 8c dc bf e5 c0 c9 75 98 a2 bd 25 55 d1 aa "
		"8c b0 8e 48 59 0d bb 3d a7 b0 8b 10 56 82 88 38 "
		"c5 f6 1e 63 93 ba 7a 0a bc c9 f6 62",
		/* tag */
		"76 fc 6e ce 0f 4e 17 68 cd df 88 53 bb 2d 55 1b"
	},

	/* Test vectors from draft-mcgrew-gcm-test-01.txt */

	/* Page 6 */
	{
		/* key + salt */
		"4c 80 cd ef bb 5d 10 da 90 6a c7 3c 36 13 a6 34 "
		"2e 44 3b 68",
		/* iv */
		"49 56 ed 7e 3b 24 4c fe",
		/* aad */
		"00 00 43 21 87 65 43 21 00 00 00 00",
		/* ciphertext */
		"fe cf 53 7e 72 9d 5b 07 dc 30 df 52 8d d2 2b 76 "
		"8d 1b 98 73 66 96 a6 fd 34 85 09 fa 13 ce ac 34 "
		"cf a2 43 6f 14 a3 f3 cf 65 92 5b f1 f4 a1 3c 5d "
		"15 b2 1e 18 84 f5 ff 62 47 ae ab b7 86 b9 3b ce "
		"61 bc 17 d7 68 fd 97 32",
		/* tag */
		"45 90 18 14 8f 6c be 72 2f d0 47 96 56 2d fd b4"
	},
	/* Page 7 */
	{
		/* key + salt */
		"fe ff e9 92 86 65 73 1c 6d 6a 8f 94 67 30 83 08 "
		"ca fe ba be",
		/* iv */
		"fa ce db ad de ca f8 88",
		/* aad */
		"00 00 a5 f8 00 00 00 0a",
		/* ciphertext */
		"de b2 2c d9 b0 7c 72 c1 6e 3a 65 be eb 8d f3 04 "
		"a5 a5 89 7d 33 ae 53 0f 1b a7 6d 5d 11 4d 2a 5c "
		"3d e8 18 27 c1 0e 9a 4f 51 33 0d 0e ec 41 66 42 "
		"cf bb 85 a5 b4 7e 48 a4 ec 3b 9b a9 5d 91 8b d1",
		/* tag */
		"83 b7 0d 3a a8 bc 6e e4 c3 09 e9 d8 5a 41 ad 4a"
	},
	/* Page 8 */
	{
		/* key + salt */
		"ab bc cd de f0 01 12 23 34 45 56 67 78 89 9a ab "
		"ab bc cd de f0 01 12 23 34 45 56 67 78 89 9a ab "
		"11 22 33 44",
		/* iv */
		"01 02 03 04 05 06 07 08",
		/* aad */
		"4a 2c bf e3 00 00 00 02",
		/* ciphertext */
		"ff 42 5c 9b 72 45 99 df 7a 3b cd 51 01 94 e0 0d "
		"6a 78 10 7f 1b 0b 1c bf 06 ef ae 9d 65 a5 d7 63 "
		"74 8a 63 79 85 77 1d 34 7f 05 45 65 9f 14 e9 9d "
		"ef 84 2d 8e",
		/* tag */
		"b3 35 f4 ee cf db f8 31 82 4b 4c 49 15 95 6c 96"
	},
	/* Page 9 */
	{
		/* key + salt */
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00",
		/* iv */
		"00 00 00 00 00 00 00 00",
		/* aad */
		"00 00 00 00 00 00 00 01",
		/* ciphertext */
		"46 88 da f2 f9 73 a3 92 73 29 09 c3 31 d5 6d 60 "
		"f6 94 ab aa 41 4b 5e 7f f5 fd cd ff f5 e9 a2 84 "
		"45 64 76 49 27 19 ff b6 4d e7 d9 dc a1 e1 d8 94 "
		"bc 3b d5 78 73 ed 4d 18 1d 19 d4 d5 c8 c1 8a f3",
		/* tag */
		"f8 21 d4 96 ee b0 96 e9 8a d2 b6 9e 47 99 c7 1d"
	},
	/* Page 10 */
	{
		/* key + salt */
		"3d e0 98 74 b3 88 e6 49 19 88 d0 c3 60 7e ae 1f "
		"57 69 0e 43",
		/* iv */
		"4e 28 00 00 a2 fc a1 a3",
		/* aad */
		"42 f6 7e 3f 10 10 10 10 10 10 10 10",
		/* ciphertext */
		"fb a2 ca a4 85 3c f9 f0 f2 2c b1 0d 86 dd 83 b0 "
		"fe c7 56 91 cf 1a 04 b0 0d 11 38 ec 9c 35 79 17 "
		"65 ac bd 87 01 ad 79 84 5b f9 fe 3f ba 48 7b c9 "
		"17 55 e6 66 2b 4c 8d 0d 1f 5e 22 73 95 30 32 0a",
		/* tag */
		"e0 d7 31 cc 97 8e ca fa ea e8 8f 00 e8 0d 6e 48"
	},
	/* Page 11 */
	{
		/* key + salt */
		"3d e0 98 74 b3 88 e6 49 19 88 d0 c3 60 7e ae 1f "
		"57 69 0e 43",
		/* iv */
		"4e 28 00 00 a2 fc a1 a3",
		/* aad */
		"42 f6 7e 3f 10 10 10 10 10 10 10 10",
		/* ciphertext */
		"fb a2 ca 84 5e 5d f9 f0 f2 2c 3e 6e 86 dd 83 1e "
		"1f c6 57 92 cd 1a f9 13 0e 13 79 ed",
		/* tag */
		"36 9f 07 1f 35 e0 34 be 95 f1 12 e4 e7 d0 5d 35"
	},
	/* Page 11 */
	{
		/* key + salt */
		"fe ff e9 92 86 65 73 1c 6d 6a 8f 94 67 30 83 08 "
		"fe ff e9 92 86 65 73 1c "
		"ca fe ba be",
		/* iv */
		"fa ce db ad de ca f8 88",
		/* aad */
		"00 00 a5 f8 00 00 00 0a",
		/* ciphertext */
		"a5 b1 f8 06 60 29 ae a4 0e 59 8b 81 22 de 02 42 "
		"09 38 b3 ab 33 f8 28 e6 87 b8 85 8b 5b fb db d0 "
		"31 5b 27 45 21 44 cc 77",
		/* tag */
		"95 45 7b 96 52 03 7f 53 18 02 7b 5b 4c d7 a6 36"
	},
	/* Page 12 */
	{
		/* key + salt */
		"ab bc cd de f0 01 12 23 34 45 56 67 78 89 9a ab "
		"de ca f8 88",
		/* iv */
		"ca fe de ba ce fa ce 74",
		/* aad */
		"00 00 01 00 00 00 00 00 00 00 00 01",
		/* ciphertext */
		"18 a6 fd 42 f7 2c bf 4a b2 a2 ea 90 1f 73 d8 14 "
		"e3 e7 f2 43 d9 54 12 e1 c3 49 c1 d2 fb ec 16 8f "
		"91 90 fe eb af 2c b0 19 84 e6 58 63 96 5d 74 72 "
		"b7 9d a3 45 e0 e7 80 19 1f 0d 2f 0e 0f 49 6c 22 "
		"6f 21 27 b2 7d b3 57 24 e7 84 5d 68",
		/* tag */
		"65 1f 57 e6 5f 35 4f 75 ff 17 01 57 69 62 34 36"
	},
	/* Page 13 */
	{
		/* key + salt */
		"ab bc cd de f0 01 12 23 34 45 56 67 78 89 9a ab "
		"ab bc cd de f0 01 12 23 34 45 56 67 78 89 9a ab "
		"73 61 6c 74",
		/* iv */
		"61 6e 64 01 69 76 65 63",
		/* aad */
		"17 40 5e 67 15 6f 31 26 dd 0d b9 9b",
		/* ciphertext */
		"f2 d6 9e cd bd 5a 0d 5b 8d 5e f3 8b ad 4d a5 8d "
		"1f 27 8f de 98 ef 67 54 9d 52 4a 30 18 d9 a5 7f "
		"f4 d3 a3 1c e6 73 11 9e",
		/* tag */
		"45 16 26 c2 41 57 71 e3 b7 ee bc a6 14 c8 9b 35"
	},
	/* Page 14 */
	{
		/* key + salt */
		"3d e0 98 74 b3 88 e6 49 19 88 d0 c3 60 7e ae 1f "
		"57 69 0e 43",
		/* iv */
		"4e 28 00 00 a2 fc a1 a3",
		/* aad */
		"42 f6 7e 3f 10 10 10 10 10 10 10 10",
		/* ciphertext */
		"fb a2 ca d1 2f c1 f9 f0 0d 3c eb f3 05 41 0d b8 "
		"3d 77 84 b6 07 32 3d 22 0f 24 b0 a9 7d 54 18 28 "
		"00 ca db 0f 68 d9 9e f0 e0 c0 c8 9a e9 be a8 88 "
		"4e 52 d6 5b c1 af d0 74 0f 74 24 44 74 7b 5b 39 "
		"ab 53 31 63 aa d4 55 0e e5 16 09 75",
		/* tag */
		"cd b6 08 c5 76 91 89 60 97 63 b8 e1 8c aa 81 e2"
	},
	/* Page 15 */
	{
		/* key + salt */
		"ab bc cd de f0 01 12 23 34 45 56 67 78 89 9a ab "
		"ab bc cd de f0 01 12 23 34 45 56 67 78 89 9a ab "
		"73 61 6c 74",
		/* iv */
		"61 6e 64 01 69 76 65 63",
		/* aad */
		"17 40 5e 67 15 6f 31 26 dd 0d b9 9b",
		/* ciphertext */
		"d4 b7 ed 86 a1 77 7f 2e a1 3d 69 73 d3 24 c6 9e "
		"7b 43 f8 26 fb 56 83 12 26 50 8b eb d2 dc eb 18 "
		"d0 a6 df 10 e5 48 7d f0 74 11 3e 14 c6 41 02 4e "
		"3e 67 73 d9 1a 62 ee 42 9b 04 3a 10 e3 ef e6 b0 "
		"12 a4 93 63 41 23 64 f8",
		/* tag */
		"c0 ca c5 87 f2 49 e5 6b 11 e2 4f 30 e4 4c cc 76"
	},
	/* Page 16 */
	{
		/* key + salt */
		"7d 77 3d 00 c1 44 c5 25 ac 61 9d 18 c8 4a 3f 47 "
		"d9 66 42 67",
		/* iv */
		"43 45 7e 91 82 44 3b c6",
		/* aad */
		"33 54 67 ae ff ff ff ff",
		/* ciphertext */
		"43 7f 86 6b",
		/* tag */
		"cb 3f 69 9f e9 b0 82 2b ac 96 1c 45 04 be f2 70"
	},
	/* Page 16 */
	{
		/* key + salt */
		"ab bc cd de f0 01 12 23 34 45 56 67 78 89 9a ab "
		"de ca f8 88",
		/* iv */
		"ca fe de ba ce fa ce 74",
		/* aad */
		"00 00 01 00 00 00 00 00 00 00 00 01",
		/* ciphertext */
		"29 c9 fc 69 a1 97 d0 38 cc dd 14 e2 dd fc aa 05 "
		"43 33 21 64",
		/* tag */
		"41 25 03 52 43 03 ed 3c 6c 5f 28 38 43 af 8c 3e"
	},
	/* Page 17 */
	{
		/* key + salt */
		"6c 65 67 61 6c 69 7a 65 6d 61 72 69 6a 75 61 6e "
		"61 61 6e 64 64 6f 69 74 62 65 66 6f 72 65 69 61 "
		"74 75 72 6e",
		/* iv */
		"33 30 21 69 67 65 74 6d",
		/* aad */
		"79 6b 69 63 ff ff ff ff ff ff ff ff",
		/* ciphertext */
		"f9 7a b2 aa 35 6d 8e dc e1 76 44 ac 8c 78 e2 5d "
		"d2 4d ed bb 29 eb f1 b6 4a 27 4b 39 b4 9c 3a 86 "
		"4c d3 d7 8c a4 ae 68 a3 2b 42 45 8f b5 7d be 82 "
		"1d cc 63 b9",
		/* tag */
		"d0 93 7b a2 94 5f 66 93 68 66 1a 32 9f b4 c0 53"
	},
	/* Page 18 */
	{
		/* key + salt */
		"4c 80 cd ef bb 5d 10 da 90 6a c7 3c 36 13 a6 34 "
		"22 43 3c 64",
		/* iv */
		"00 00 00 00 00 00 00 00",
		/* aad */
		"00 00 43 21 00 00 00 07 00 00 00 00 00 00 00 00 "
		"45 00 00 30 da 3a 00 00 80 01 df 3b c0 a8 00 05 "
		"c0 a8 00 01 08 00 c6 cd 02 00 07 00 61 62 63 64 "
		"65 66 67 68 69 6a 6b 6c 6d 6e 6f 70 71 72 73 74 "
		"01 02 02 01",
		/* ciphertext */
		NULL,
		/* tag */
		"f2 a9 a8 36 e1 55 10 6a a8 dc d6 18 e4 09 9a aa"
	},
	/* Page 19 */
	{
		/* key + salt */
		"3d e0 98 74 b3 88 e6 49 19 88 d0 c3 60 7e ae 1f "
		"57 69 0e 43",
		/* iv */
		"4e 28 00 00 a2 fc a1 a3",
		/* aad */
		"3f 7e f6 42 10 10 10 10 10 10 10 10",
		/* ciphertext */
		"fb a2 ca a8 c6 c5 f9 f0 f2 2c a5 4a 06 12 10 ad "
		"3f 6e 57 91 cf 1a ca 21 0d 11 7c ec 9c 35 79 17 "
		"65 ac bd 87 01 ad 79 84 5b f9 fe 3f ba 48 7b c9 "
		"63 21 93 06",
		/* tag */
		"84 ee ca db 56 91 25 46 e7 a9 5c 97 40 d7 cb 05"
	},
	/* Page 20 */
	{
		/* key + salt */
		"4c 80 cd ef bb 5d 10 da 90 6a c7 3c 36 13 a6 34 "
		"22 43 3c 64",
		/* iv */
		"48 55 ec 7d 3a 23 4b fd",
		/* aad */
		"00 00 43 21 87 65 43 21 00 00 00 07",
		/* ciphertext */
		"74 75 2e 8a eb 5d 87 3c d7 c0 f4 ac c3 6c 4b ff "
		"84 b7 d7 b9 8f 0c a8 b6 ac da 68 94 bc 61 90 69",
		/* tag */
		"ef 9c bc 28 fe 1b 56 a7 c4 e0 d5 8c 86 cd 2b c0"
	},

	/* local add-ons, primarily streaming ghash tests */

	/* 128 bytes aad */
	{
		/* key + salt */
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00",
		/* iv */
		"00 00 00 00 00 00 00 00",
		/* aad */
		"d9 31 32 25 f8 84 06 e5 a5 59 09 c5 af f5 26 9a "
		"86 a7 a9 53 15 34 f7 da 2e 4c 30 3d 8a 31 8a 72 "
		"1c 3c 0c 95 95 68 09 53 2f cf 0e 24 49 a6 b5 25 "
		"b1 6a ed f5 aa 0d e6 57 ba 63 7b 39 1a af d2 55 "
		"52 2d c1 f0 99 56 7d 07 f4 7f 37 a3 2a 84 42 7d "
		"64 3a 8c dc bf e5 c0 c9 75 98 a2 bd 25 55 d1 aa "
		"8c b0 8e 48 59 0d bb 3d a7 b0 8b 10 56 82 88 38 "
		"c5 f6 1e 63 93 ba 7a 0a bc c9 f6 62 89 80 15 ad",
		/* ciphertext */
		NULL,
		/* tag */
		"5f ea 79 3a 2d 6f 97 4d 37 e6 8e 0c b8 ff 94 92"
	},
	/* 48 bytes plaintext */
	{
		/* key + salt */
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00",
		/* iv */
		"00 00 00 00 00 00 00 00",
		/* aad */
		NULL,
		/* ciphertext */
		"03 88 da ce 60 b6 a3 92 f3 28 c2 b9 71 b2 fe 78 "
		"f7 95 aa ab 49 4b 59 23 f7 fd 89 ff 94 8b c1 e0 "
		"20 02 11 21 4e 73 94 da 20 89 b6 ac d0 93 ab e0",
		/* tag */
		"9d d0 a3 76 b0 8e 40 eb 00 c3 5f 29 f9 ea 61 a4"
	},
	/* 80 bytes plaintext */
	{
		/* key + salt */
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00",
		/* iv */
		"00 00 00 00 00 00 00 00",
		/* aad */
		NULL,
		/* ciphertext */
		"03 88 da ce 60 b6 a3 92 f3 28 c2 b9 71 b2 fe 78 "
		"f7 95 aa ab 49 4b 59 23 f7 fd 89 ff 94 8b c1 e0 "
		"20 02 11 21 4e 73 94 da 20 89 b6 ac d0 93 ab e0 "
		"c9 4d a2 19 11 8e 29 7d 7b 7e bc bc c9 c3 88 f2 "
		"8a de 7d 85 a8 ee 35 61 6f 71 24 a9 d5 27 02 91",
		/* tag */
		"98 88 5a 3a 22 bd 47 42 fe 7b 72 17 21 93 b1 63"
	},
	/* 128 bytes plaintext */
	{
		/* key + salt */
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"00 00 00 00",
		/* iv */
		"00 00 00 00 00 00 00 00",
		/* aad */
		NULL,
		/* ciphertext */
		"03 88 da ce 60 b6 a3 92 f3 28 c2 b9 71 b2 fe 78 "
		"f7 95 aa ab 49 4b 59 23 f7 fd 89 ff 94 8b c1 e0 "
		"20 02 11 21 4e 73 94 da 20 89 b6 ac d0 93 ab e0 "
		"c9 4d a2 19 11 8e 29 7d 7b 7e bc bc c9 c3 88 f2 "
		"8a de 7d 85 a8 ee 35 61 6f 71 24 a9 d5 27 02 91 "
		"95 b8 4d 1b 96 c6 90 ff 2f 2d e3 0b f2 ec 89 e0 "
		"02 53 78 6e 12 65 04 f0 da b9 0c 48 a3 03 21 de "
		"33 45 e6 b0 46 1e 7c 9e 6c 6b 7a fe dd e8 3f 40",
		/* tag */
		"ca c4 5f 60 e3 1e fd 3b 5a 43 b9 8a 22 ce 1a a1"
	},
	/* 80 bytes plaintext, submitted by Intel */
	{
		/* key + salt */
		"84 3f fc f5 d2 b7 26 94 d1 9e d0 1d 01 24 94 12 "
		"db cc a3 2e",
		/* iv */
		"bf 9b 80 46 17 c3 aa 9e",
		/* aad */
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"10 11 12 13 14 15 16 17 18 19 1a 1b 1c 1d 1e 1f",
		/* ciphertext */
		"62 68 c6 fa 2a 80 b2 d1 37 46 7f 09 2f 65 7a c0 "
		"4d 89 be 2b ea a6 23 d6 1b 5a 86 8c 8f 03 ff 95 "
		"d3 dc ee 23 ad 2f 1a b3 a6 c8 0e af 4b 14 0e b0 "
		"5d e3 45 7f 0f bc 11 1a 6b 43 d0 76 3a a4 22 a3 "
		"01 3c f1 dc 37 fe 41 7d 1f bf c4 49 b7 5d 4c c5",
		/* tag */
		"3b 62 9c cf bc 11 19 b7 31 9e 1d ce 2c d6 fd 6d"
	}
};

static void
dogmac(const unsigned char *key, size_t klen,
    const unsigned char *iv, size_t ivlen,
    const unsigned char *aad, size_t aadlen,
    const unsigned char *in, unsigned char *out, size_t len)
{
	AES_GMAC_CTX ctx;
	uint8_t blk[GMAC_BLOCK_LEN];
	uint32_t *p;
	int i;

	AES_GMAC_Init(&ctx);

	AES_GMAC_Setkey(&ctx, key, klen);

	AES_GMAC_Reinit(&ctx, iv, ivlen);

	for (i = 0; i < aadlen; i += GMAC_BLOCK_LEN) {
		memset(blk, 0, GMAC_BLOCK_LEN);
		memcpy(blk, aad + i, MINIMUM(aadlen - i, GMAC_BLOCK_LEN));
		AES_GMAC_Update(&ctx, blk, GMAC_BLOCK_LEN);
	}

	for (i = 0; i < len; i += GMAC_BLOCK_LEN) {
		int dlen = MINIMUM(len - i, GMAC_BLOCK_LEN);
		AES_GMAC_Update(&ctx, in + i, dlen);
	}

	bzero(blk, sizeof blk);
	p = (uint32_t *)blk + 1;
	*p = htobe32(aadlen * 8);
	p = (uint32_t *)blk + 3;
	*p = htobe32(len * 8);
	AES_GMAC_Update(&ctx, blk, 16);

	AES_GMAC_Final(out, &ctx);
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
	u_char *p, *data[TST_NUM], tag[GMAC_DIGEST_LEN];

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
		if ((p = malloc(len)) == 0) {
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

	dogmac(data[TST_KEY], length[TST_KEY], data[TST_IV], length[TST_IV],
	    data[TST_AAD], length[TST_AAD], data[TST_CIPHER], tag,
	    length[TST_CIPHER]);

	fail = !match(data[TST_TAG], tag, GMAC_DIGEST_LEN);
	printf("%s test vector %d\n", fail ? "FAILED" : "OK", num);

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

void
explicit_bzero(void *b, size_t len)
{
	bzero(b, len);
}
