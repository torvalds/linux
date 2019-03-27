/*-
 * Copyright (c) 2003 Poul-Henning Kamp
 * All rights reserved.
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
 *
 * $FreeBSD$
 *
 * This test checks for inplace decryption working.  This is the case
 * where the same buffer is passed as input and output to
 * rijndael_blockDecrypt().
 */

#include <stdio.h>
#include <sys/param.h>
#include <sys/types.h>

#include <crypto/rijndael/rijndael-api-fst.h>

#define LL 32 
int
main(int argc, char **argv)
{
	keyInstance ki;
	cipherInstance ci;
	uint8_t key[16];
	uint8_t in[LL];
	uint8_t out[LL];
	int i, j;

	rijndael_cipherInit(&ci, MODE_CBC, NULL);
	for (i = 0; i < 16; i++)
		key[i] = i;
	rijndael_makeKey(&ki, DIR_DECRYPT, 128, key);
	for (i = 0; i < LL; i++)
		in[i] = i;
	rijndael_blockDecrypt(&ci, &ki, in, LL * 8, out);
	for (i = 0; i < LL; i++)
		printf("%02x", out[i]);
	putchar('\n');
	rijndael_blockDecrypt(&ci, &ki, in, LL * 8, in);
	j = 0;
	for (i = 0; i < LL; i++) {
		printf("%02x", in[i]);
		if (in[i] != out[i])
			j++;
	}
	putchar('\n');
	if (j != 0) {
		fprintf(stderr,
		    "Error: inplace decryption fails in %d places\n", j);
		return (1);
	} else {
		return (0);
	}
}
