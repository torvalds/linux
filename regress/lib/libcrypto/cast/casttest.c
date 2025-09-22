/*	$OpenBSD: casttest.c,v 1.7 2023/08/20 22:35:52 tb Exp $	*/
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
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
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <openssl/cast.h>

static const unsigned char k[16] = {
	0x01, 0x23, 0x45, 0x67, 0x12, 0x34, 0x56, 0x78,
	0x23, 0x45, 0x67, 0x89, 0x34, 0x56, 0x78, 0x9A,
};

static const unsigned char in[8] = {
	0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
};

static int k_len[3] = {16, 10, 5};
static const unsigned char c[3][8] = {
	{0x23, 0x8B, 0x4F, 0xE5, 0x84, 0x7E, 0x44, 0xB2},
	{0xEB, 0x6A, 0x71, 0x1A, 0x2C, 0x02, 0x27, 0x1B},
	{0x7A, 0xC8, 0x16, 0xD1, 0x6E, 0x9B, 0x30, 0x2E},
};
static unsigned char out[80];

static const unsigned char in_a[16] = {
	0x01, 0x23, 0x45, 0x67, 0x12, 0x34, 0x56, 0x78,
	0x23, 0x45, 0x67, 0x89, 0x34, 0x56, 0x78, 0x9A,
};
static const unsigned char in_b[16] = {
	0x01, 0x23, 0x45, 0x67, 0x12, 0x34, 0x56, 0x78,
	0x23, 0x45, 0x67, 0x89, 0x34, 0x56, 0x78, 0x9A,
};

static const unsigned char c_a[16] = {
	0xEE, 0xA9, 0xD0, 0xA2, 0x49, 0xFD, 0x3B, 0xA6,
	0xB3, 0x43, 0x6F, 0xB8, 0x9D, 0x6D, 0xCA, 0x92,
};
static const unsigned char c_b[16] = {
	0xB2, 0xC9, 0x5E, 0xB0, 0x0C, 0x31, 0xAD, 0x71,
	0x80, 0xAC, 0x05, 0xB8, 0xE8, 0x3D, 0x69, 0x6E,
};

int
main(int argc, char *argv[])
{
	long l;
	CAST_KEY key_b;
	int i, z, err = 0;
	CAST_KEY key;

	for (z = 0; z < 3; z++) {
		CAST_set_key(&key, k_len[z], k);

		CAST_ecb_encrypt(in, out, &key, CAST_ENCRYPT);
		if (memcmp(out, &(c[z][0]), 8) != 0) {
			printf("ecb cast error encrypting for keysize %d\n",
			    k_len[z]*8);
			printf("got     :");
			for (i = 0; i < 8; i++)
				printf("%02X ", out[i]);
			printf("\n");
			printf("expected:");
			for (i = 0; i < 8; i++)
				printf("%02X ", c[z][i]);
			err = 20;
			printf("\n");
		}

		CAST_ecb_encrypt(out, out, &key, CAST_DECRYPT);
		if (memcmp(out, in, 8) != 0) {
			printf("ecb cast error decrypting for keysize %d\n",
			    k_len[z]*8);
			printf("got     :");
			for (i = 0; i < 8; i++)
				printf("%02X ", out[i]);
			printf("\n");
			printf("expected:");
			for (i = 0; i < 8; i++)
				printf("%02X ", in[i]);
			printf("\n");
			err = 3;
		}
	}
	if (err == 0)
		printf("ecb cast5 ok\n");

	{
		unsigned char out_a[16], out_b[16];
		static char *hex = "0123456789ABCDEF";

		printf("This test will take some time....");
		fflush(stdout);
		memcpy(out_a, in_a, sizeof(in_a));
		memcpy(out_b, in_b, sizeof(in_b));
		i = 1;

		for (l = 0; l < 1000000L; l++) {
			CAST_set_key(&key_b, 16, out_b);
			CAST_ecb_encrypt(&(out_a[0]), &(out_a[0]), &key_b,
			    CAST_ENCRYPT);
			CAST_ecb_encrypt(&(out_a[8]), &(out_a[8]), &key_b,
			    CAST_ENCRYPT);
			CAST_set_key(&key, 16, out_a);
			CAST_ecb_encrypt(&(out_b[0]), &(out_b[0]), &key,
			    CAST_ENCRYPT);
			CAST_ecb_encrypt(&(out_b[8]), &(out_b[8]), &key,
			    CAST_ENCRYPT);
			if ((l & 0xffff) == 0xffff) {
				printf("%c", hex[i & 0x0f]);
				fflush(stdout);
				i++;
			}
		}

		if ((memcmp(out_a, c_a, sizeof(c_a)) != 0) ||
		    (memcmp(out_b, c_b, sizeof(c_b)) != 0)) {
			printf("\n");
			printf("Error\n");

			printf("A out =");
			for (i = 0; i < 16; i++)
				printf("%02X ", out_a[i]);
			printf("\nactual=");
			for (i = 0; i < 16; i++)
				printf("%02X ", c_a[i]);
			printf("\n");

			printf("B out =");
			for (i = 0; i < 16; i++)
				printf("%02X ", out_b[i]);
			printf("\nactual=");
			for (i = 0; i < 16; i++)
				printf("%02X ", c_b[i]);
			printf("\n");
		} else
			printf(" ok\n");
	}

	exit(err);
}
