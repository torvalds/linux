/*      $OpenBSD: aesctr.c,v 1.4 2021/12/13 16:56:49 deraadt Exp $  */

/*
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
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

int debug = 0;

enum { TST_KEY, TST_IV, TST_PLAIN, TST_CIPHER, TST_NUM };

/* Test vectors from RFC 3686 */
struct {
	char *data[TST_NUM];
} tests[] = {
	/* 128 bit key */
	{
		"AE 68 52 F8 12 10 67 CC 4B F7 A5 76 55 77 F3 9E "
		"00 00 00 30",
		"00 00 00 00 00 00 00 00",
		"53 69 6E 67 6C 65 20 62 6C 6F 63 6B 20 6D 73 67",
		"E4 09 5D 4F B7 A7 B3 79 2D 61 75 A3 26 13 11 B8"
	},
	{
		"7E 24 06 78 17 FA E0 D7 43 D6 CE 1F 32 53 91 63 "
		"00 6C B6 DB",
		"C0 54 3B 59 DA 48 D9 0B",
		"00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
		"10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F",
		"51 04 A1 06 16 8A 72 D9 79 0D 41 EE 8E DA D3 88 "
		"EB 2E 1E FC 46 DA 57 C8 FC E6 30 DF 91 41 BE 28"
	},
	{
		"76 91 BE 03 5E 50 20 A8 AC 6E 61 85 29 F9 A0 DC "
		"00 E0 01 7B",
		"27 77 7F 3F 4A 17 86 F0",
		"00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
		"10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F"
		/*"20 21 22 23"*/,
		"C1 CF 48 A8 9F 2F FD D9 CF 46 52 E9 EF DB 72 D7 "
		"45 40 A4 2B DE 6D 78 36 D5 9A 5C EA AE F3 10 53"
                /*"25 B2 07 2F"*/
	},
	/* 192 bit key */
	{
		"16 AF 5B 14 5F C9 F5 79 C1 75 F9 3E 3B FB 0E ED "
		"86 3D 06 CC FD B7 85 15 "
		"00 00 00 48",
		"36 73 3C 14 7D 6D 93 CB",
		"53 69 6E 67 6C 65 20 62 6C 6F 63 6B 20 6D 73 67",
		"4B 55 38 4F E2 59 C9 C8 4E 79 35 A0 03 CB E9 28",
	},
	{
		"7C 5C B2 40 1B 3D C3 3C 19 E7 34 08 19 E0 F6 9C "
		"67 8C 3D B8 E6 F6 A9 1A "
		"00 96 B0 3B",
		"02 0C 6E AD C2 CB 50 0D",
		"00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
		"10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F",
		"45 32 43 FC 60 9B 23 32 7E DF AA FA 71 31 CD 9F "
		"84 90 70 1C 5A D4 A7 9C FC 1F E0 FF 42 F4 FB 00",
	},
	{
		"02 BF 39 1E E8 EC B1 59 B9 59 61 7B 09 65 27 9B "
		"F5 9B 60 A7 86 D3 E0 FE "
		"00 07 BD FD",
		"5C BD 60 27 8D CC 09 12",
		"00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
		"10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F"
		/*"20 21 22 23"*/,
		"96 89 3F C5 5E 5C 72 2F 54 0B 7D D1 DD F7 E7 58 "
		"D2 88 BC 95 C6 91 65 88 45 36 C8 11 66 2F 21 88"
		/*"AB EE 09 35"*/,
	},
	/* 256 bit key */
	{
		"77 6B EF F2 85 1D B0 6F 4C 8A 05 42 C8 69 6F 6C "
		"6A 81 AF 1E EC 96 B4 D3 7F C1 D6 89 E6 C1 C1 04 "
		"00 00 00 60",
		"DB 56 72 C9 7A A8 F0 B2",
		"53 69 6E 67 6C 65 20 62 6C 6F 63 6B 20 6D 73 67",
		"14 5A D0 1D BF 82 4E C7 56 08 63 DC 71 E3 E0 C0"
	},
	{
		"F6 D6 6D 6B D5 2D 59 BB 07 96 36 58 79 EF F8 86 "
		"C6 6D D5 1A 5B 6A 99 74 4B 50 59 0C 87 A2 38 84 "
		"00 FA AC 24",
		"C1 58 5E F1 5A 43 D8 75",
		"00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
		"10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F",
		"F0 5E 23 1B 38 94 61 2C 49 EE 00 0B 80 4E B2 A9 "
		"B8 30 6B 50 8F 83 9D 6A 55 30 83 1D 93 44 AF 1C",
	},
	{
		"FF 7A 61 7C E6 91 48 E4 F1 72 6E 2F 43 58 1D E2 "
		"AA 62 D9 F8 05 53 2E DF F1 EE D6 87 FB 54 15 3D "
		"00 1C C5 B7",
		"51 A5 1D 70 A1 C1 11 48",
		"00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
		"10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F"
		/*"20 21 22 23"*/,
		"EB 6C 52 82 1D 0B BB F7 CE 75 94 46 2A CA 4F AA "
		"B4 07 DF 86 65 69 FD 07 F4 8C C0 B5 83 D6 07 1F"
		/*"1E C0 E6 B8"*/,
	},
};

/* Stubs */

u_int32_t deflate_global(u_int8_t *, u_int32_t, int, u_int8_t **);

u_int32_t
deflate_global(u_int8_t *data, u_int32_t size, int comp, u_int8_t **out)
{
	return 0;
}

void	explicit_bzero(void *, size_t);

void
explicit_bzero(void *b, size_t len)
{
	bzero(b, len);
}

/* Definitions from /sys/crypto/xform.c */

#define AESCTR_NONCESIZE	4
#define AESCTR_IVSIZE		8
#define AESCTR_BLOCKSIZE	16

struct aes_ctr_ctx {
	AES_CTX		ac_key;
	u_int8_t	ac_block[AESCTR_BLOCKSIZE];
};

int  aes_ctr_setkey(void *, u_int8_t *, int);
void aes_ctr_encrypt(caddr_t, u_int8_t *);
void aes_ctr_decrypt(caddr_t, u_int8_t *);
void aes_ctr_reinit(caddr_t, u_int8_t *);

static int
docrypt(const unsigned char *key, size_t klen, const unsigned char *iv,
    const unsigned char *in, unsigned char *out, size_t len, int encrypt)
{
	u_int8_t block[AESCTR_BLOCKSIZE];
	struct aes_ctr_ctx ctx;
	int error = 0;
	size_t i;

	error = aes_ctr_setkey(&ctx, (u_int8_t *)key, klen);
	if (error)
		return -1;
	aes_ctr_reinit((caddr_t)&ctx, (u_int8_t *)iv);
	for (i = 0; i < len / AESCTR_BLOCKSIZE; i++) {
		bcopy(in, block, AESCTR_BLOCKSIZE);
		in += AESCTR_BLOCKSIZE;
		aes_ctr_crypt(&ctx, block);
		bcopy(block, out, AESCTR_BLOCKSIZE);
		out += AESCTR_BLOCKSIZE;
	}
	return 0;
}

static int
match(unsigned char *a, unsigned char *b, size_t len)
{
	int i;

	if (memcmp(a, b, len) == 0)
		return (1);

	warnx("ciphertext mismatch");

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
	u_char *p, *data[TST_NUM];

	for (i = 0; i < TST_NUM; i++)
		data[i] = NULL;
	for (i = 0; i < TST_NUM; i++) {
		from = tests[num].data[i];
		if (debug)
			printf("%s\n", from);
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
	len = length[TST_PLAIN];
	if ((p = malloc(len)) == 0) {
		warn("malloc");
		return (1);
	}
	if (docrypt(data[TST_KEY], length[TST_KEY],
	    data[TST_IV], data[TST_PLAIN], p,
	    length[TST_PLAIN], 0) < 0) {
		warnx("encryption failed");
		goto done;
	}
	fail = !match(data[TST_CIPHER], p, len);
	printf("%s test vector %d\n", fail ? "FAILED" : "OK", num);
done:
	for (i = 0; i < TST_NUM; i++)
		free(data[i]);
	return (fail);
}

int
main(int argc, char **argv)
{
	int fail = 0, i;

	for (i = 0; i < (sizeof(tests) / sizeof(tests[0])); i++)
		fail += run(i);
	exit((fail > 0) ? 1 : 0);
}
