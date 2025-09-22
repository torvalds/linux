/*      $OpenBSD: des3.c,v 1.10 2021/12/13 16:56:49 deraadt Exp $  */

/*
 * Copyright (c) 2002 Markus Friedl.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <openssl/des.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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


/* Simulate CBC mode */

static int
docrypt(const unsigned char *key, size_t klen, const unsigned char *iv0,
    const unsigned char *in, unsigned char *out, size_t len, int encrypt)
{
	u_int8_t block[8], iv[8], iv2[8], *ivp = iv, *nivp;
	u_int8_t ctx[384];
	int i, j, error = 0;

	memcpy(iv, iv0, 8);
	memset(ctx, 0, sizeof(ctx));
	error = des3_setkey(ctx, key, klen);
	if (error)
		return -1;
	for (i = 0; i < len / 8; i ++) {
		bcopy(in, block, 8);
		in += 8;
		if (encrypt) {
			for (j = 0; j < 8; j++)
				block[j] ^= ivp[j];
			des3_encrypt(ctx, block);
			memcpy(ivp, block, 8);
		} else {
			nivp = ivp == iv ? iv2 : iv;
			memcpy(nivp, block, 8);
			des3_decrypt(ctx, block);
			for (j = 0; j < 8; j++)
				block[j] ^= ivp[j];
			ivp = nivp;
		}
		bcopy(block, out, 8);
		out += 8;
	}
	return 0;
}

static int
match(unsigned char *a, unsigned char *b, size_t len)
{
	int i;

	if (memcmp(a, b, len) == 0)
		return (1);

	warnx("decrypt/plaintext mismatch");

	for (i = 0; i < len; i++)
		printf("%2.2x", a[i]);
	printf("\n");
	for (i = 0; i < len; i++)
		printf("%2.2x", b[i]);
	printf("\n");

	return (0);
}

#define SZ 16

int
main(int argc, char **argv)
{
	DES_key_schedule ks1, ks2, ks3;
	unsigned char iv0[8], iv[8], key[24] = "012345670123456701234567";
	unsigned char b1[SZ], b2[SZ];
	int i, fail = 0;
	u_int32_t rand = 0;

	/* setup data and iv */
	for (i = 0; i < sizeof(b1); i++ ) {
		if (i % 4 == 0)
                        rand = arc4random();
		b1[i] = rand;
		rand >>= 8;
	}
	for (i = 0; i < sizeof(iv0); i++ ) {
		if (i % 4 == 0)
                        rand = arc4random();
		iv0[i] = rand;
		rand >>= 8;
	}
	memset(b2, 0, sizeof(b2));

	/* keysetup for software */
        DES_set_key((void *) key, &ks1);
        DES_set_key((void *) (key+8), &ks2);
        DES_set_key((void *) (key+16), &ks3);

	/* encrypt with software, decrypt with /dev/crypto */
	memcpy(iv, iv0, sizeof(iv0));
        DES_ede3_cbc_encrypt((void *)b1, (void*)b2, sizeof(b1), &ks1, &ks2,
	    &ks3, (void*)iv, DES_ENCRYPT);
	memcpy(iv, iv0, sizeof(iv0));
	if (docrypt(key, sizeof(key), iv, b2, b2, sizeof(b1), 0) < 0) {
		warnx("decryption failed");
		fail++;
	}
	if (!match(b1, b2, sizeof(b1)))
		fail++;
	else
		printf("ok, decrypted\n");

	/* encrypt with kernel functions, decrypt with openssl */
	memset(b2, 0, sizeof(b2));
	memcpy(iv, iv0, sizeof(iv0));
	if (docrypt(key, sizeof(key), iv, b1, b2, sizeof(b1), 1) < 0) {
		warnx("encryption failed");
		fail++;
	}
	memcpy(iv, iv0, sizeof(iv0));
        DES_ede3_cbc_encrypt((void *)b2, (void*)b2, sizeof(b1), &ks1, &ks2,
	    &ks3, (void*)iv, DES_DECRYPT);
	if (!match(b1, b2, sizeof(b1)))
		fail++;
	else
		printf("ok, encrypted\n");

	exit((fail > 0) ? 1 : 0);
}
