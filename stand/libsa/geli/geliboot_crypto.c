/*-
 * Copyright (c) 2005-2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * Copyright (c) 2015 Allan Jude <allanjude@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "geliboot_internal.h"
#include "geliboot.h"

int
geliboot_crypt(u_int algo, int enc, u_char *data, size_t datasize,
    const u_char *key, size_t keysize, u_char *iv)
{
	keyInstance aeskey;
	cipherInstance cipher;
	struct aes_xts_ctx xtsctx, *ctxp;
	size_t xts_len;
	int err, blks, i;

	switch (algo) {
	case CRYPTO_AES_CBC:
		err = rijndael_makeKey(&aeskey, !enc, keysize, 
		    (const char *)key);
		if (err < 0) {
			printf("Failed to setup decryption keys: %d\n", err);
			return (err);
		}

		err = rijndael_cipherInit(&cipher, MODE_CBC, iv);
		if (err < 0) {
			printf("Failed to setup IV: %d\n", err);
			return (err);
		}

		if (enc == 0) {
			/* decrypt */
			blks = rijndael_blockDecrypt(&cipher, &aeskey, data, 
			    datasize * 8, data);
		} else {
			/* encrypt */
			blks = rijndael_blockEncrypt(&cipher, &aeskey, data, 
			    datasize * 8, data);
		}
		if (datasize != (blks / 8)) {
			printf("Failed to decrypt the entire input: "
			    "%u != %zu\n", blks, datasize);
			return (1);
		}
		break;
	case CRYPTO_AES_XTS:
		xts_len = keysize << 1;
		ctxp = &xtsctx;

		rijndael_set_key(&ctxp->key1, key, xts_len / 2);
		rijndael_set_key(&ctxp->key2, key + (xts_len / 16), xts_len / 2);

		enc_xform_aes_xts.reinit((caddr_t)ctxp, iv);

		switch (enc) {
		case 0: /* decrypt */
			for (i = 0; i < datasize; i += AES_XTS_BLOCKSIZE) {
				enc_xform_aes_xts.decrypt((caddr_t)ctxp, data + i);
			}
			break;
		case 1: /* encrypt */
			for (i = 0; i < datasize; i += AES_XTS_BLOCKSIZE) {
				enc_xform_aes_xts.encrypt((caddr_t)ctxp, data + i);
			}
			break;
		}
		break;
	default:
		printf("Unsupported crypto algorithm #%d\n", algo);
		return (1);
	}

	return (0);
}

static int
g_eli_crypto_cipher(u_int algo, int enc, u_char *data, size_t datasize,
    const u_char *key, size_t keysize)
{
	u_char iv[keysize];

	explicit_bzero(iv, sizeof(iv));
	return (geliboot_crypt(algo, enc, data, datasize, key, keysize, iv));
}

int
g_eli_crypto_encrypt(u_int algo, u_char *data, size_t datasize,
    const u_char *key, size_t keysize)
{

	/* We prefer AES-CBC for metadata protection. */
	if (algo == CRYPTO_AES_XTS)
		algo = CRYPTO_AES_CBC;

	return (g_eli_crypto_cipher(algo, 1, data, datasize, key, keysize));
}

int
g_eli_crypto_decrypt(u_int algo, u_char *data, size_t datasize,
    const u_char *key, size_t keysize)
{

	/* We prefer AES-CBC for metadata protection. */
	if (algo == CRYPTO_AES_XTS)
		algo = CRYPTO_AES_CBC;

	return (g_eli_crypto_cipher(algo, 0, data, datasize, key, keysize));
}
