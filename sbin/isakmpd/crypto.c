/* $OpenBSD: crypto.c,v 1.35 2018/01/15 09:54:48 mpi Exp $	 */
/* $EOM: crypto.c,v 1.32 2000/03/07 20:08:51 niklas Exp $	 */

/*
 * Copyright (c) 1998 Niels Provos.  All rights reserved.
 * Copyright (c) 1999, 2000 Niklas Hallqvist.  All rights reserved.
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

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "log.h"

enum cryptoerr  des3_init(struct keystate *, u_int8_t *, u_int16_t);
enum cryptoerr  blf_init(struct keystate *, u_int8_t *, u_int16_t);
enum cryptoerr  cast_init(struct keystate *, u_int8_t *, u_int16_t);
enum cryptoerr  aes_init(struct keystate *, u_int8_t *, u_int16_t);
void            des3_encrypt(struct keystate *, u_int8_t *, u_int16_t);
void            des3_decrypt(struct keystate *, u_int8_t *, u_int16_t);
void            blf_encrypt(struct keystate *, u_int8_t *, u_int16_t);
void            blf_decrypt(struct keystate *, u_int8_t *, u_int16_t);
void            cast1_encrypt(struct keystate *, u_int8_t *, u_int16_t);
void            cast1_decrypt(struct keystate *, u_int8_t *, u_int16_t);
void            aes_encrypt(struct keystate *, u_int8_t *, u_int16_t);
void            aes_decrypt(struct keystate *, u_int8_t *, u_int16_t);

struct crypto_xf transforms[] = {
	{
		TRIPLEDES_CBC, "Triple-DES (CBC-Mode)", 24, 24,
		BLOCKSIZE, 0,
		des3_init,
		des3_encrypt, des3_decrypt
	},
	{
		BLOWFISH_CBC, "Blowfish (CBC-Mode)", 12, 56,
		BLOCKSIZE, 0,
		blf_init,
		blf_encrypt, blf_decrypt
	},
	{
		CAST_CBC, "CAST (CBC-Mode)", 12, 16,
		BLOCKSIZE, 0,
		cast_init,
		cast1_encrypt, cast1_decrypt
	},
	{
		AES_CBC, "AES (CBC-Mode)", 16, 32,
		AES_BLOCK_SIZE, 0,
		aes_init,
		aes_encrypt, aes_decrypt
	},
};

enum cryptoerr
des3_init(struct keystate *ks, u_int8_t *key, u_int16_t len)
{
	DES_set_odd_parity((void *)key);
	DES_set_odd_parity((void *)(key + 8));
	DES_set_odd_parity((void *)(key + 16));

	/* As of the draft Tripe-DES does not check for weak keys */
	DES_set_key((void *)key, &ks->ks_des[0]);
	DES_set_key((void *)(key + 8), &ks->ks_des[1]);
	DES_set_key((void *)(key + 16), &ks->ks_des[2]);

	return EOKAY;
}

void
des3_encrypt(struct keystate *ks, u_int8_t *data, u_int16_t len)
{
	u_int8_t        iv[MAXBLK];

	memcpy(iv, ks->riv, ks->xf->blocksize);
	DES_ede3_cbc_encrypt((void *)data, (void *)data, len, &ks->ks_des[0],
	    &ks->ks_des[1], &ks->ks_des[2], (void *)iv, DES_ENCRYPT);
}

void
des3_decrypt(struct keystate *ks, u_int8_t *data, u_int16_t len)
{
	u_int8_t        iv[MAXBLK];

	memcpy(iv, ks->riv, ks->xf->blocksize);
	DES_ede3_cbc_encrypt((void *)data, (void *)data, len, &ks->ks_des[0],
	    &ks->ks_des[1], &ks->ks_des[2], (void *)iv, DES_DECRYPT);
}

enum cryptoerr
blf_init(struct keystate *ks, u_int8_t *key, u_int16_t len)
{
	blf_key(&ks->ks_blf, key, len);

	return EOKAY;
}

void
blf_encrypt(struct keystate *ks, u_int8_t *data, u_int16_t len)
{
	u_int16_t       i, blocksize = ks->xf->blocksize;
	u_int8_t       *iv = ks->liv;
	u_int32_t       xl, xr;

	memcpy(iv, ks->riv, blocksize);

	for (i = 0; i < len; data += blocksize, i += blocksize) {
		XOR64(data, iv);
		xl = GET_32BIT_BIG(data);
		xr = GET_32BIT_BIG(data + 4);
		Blowfish_encipher(&ks->ks_blf, &xl, &xr);
		SET_32BIT_BIG(data, xl);
		SET_32BIT_BIG(data + 4, xr);
		SET64(iv, data);
	}
}

void
blf_decrypt(struct keystate *ks, u_int8_t *data, u_int16_t len)
{
	u_int16_t       i, blocksize = ks->xf->blocksize;
	u_int32_t       xl, xr;

	data += len - blocksize;
	for (i = len - blocksize; i >= blocksize; data -= blocksize,
	    i -= blocksize) {
		xl = GET_32BIT_BIG(data);
		xr = GET_32BIT_BIG(data + 4);
		Blowfish_decipher(&ks->ks_blf, &xl, &xr);
		SET_32BIT_BIG(data, xl);
		SET_32BIT_BIG(data + 4, xr);
		XOR64(data, data - blocksize);

	}
	xl = GET_32BIT_BIG(data);
	xr = GET_32BIT_BIG(data + 4);
	Blowfish_decipher(&ks->ks_blf, &xl, &xr);
	SET_32BIT_BIG(data, xl);
	SET_32BIT_BIG(data + 4, xr);
	XOR64(data, ks->riv);
}

enum cryptoerr
cast_init(struct keystate *ks, u_int8_t *key, u_int16_t len)
{
	CAST_set_key(&ks->ks_cast, len, key);
	return EOKAY;
}

void
cast1_encrypt(struct keystate *ks, u_int8_t *data, u_int16_t len)
{
	memcpy(ks->liv, ks->riv, ks->xf->blocksize);
	CAST_cbc_encrypt(data, data, len, &ks->ks_cast, ks->liv, 1);
}

void
cast1_decrypt(struct keystate *ks, u_int8_t *data, u_int16_t len)
{
	CAST_cbc_encrypt(data, data, len, &ks->ks_cast, ks->riv, 0);
}

enum cryptoerr
aes_init(struct keystate *ks, u_int8_t *key, u_int16_t len)
{
	AES_set_encrypt_key(key, len << 3, &ks->ks_aes[0]);
	AES_set_decrypt_key(key, len << 3, &ks->ks_aes[1]);
	return EOKAY;
}

void
aes_encrypt(struct keystate *ks, u_int8_t *data, u_int16_t len)
{
	u_int8_t        iv[MAXBLK];

	memcpy(iv, ks->riv, ks->xf->blocksize);
	AES_cbc_encrypt(data, data, len, &ks->ks_aes[0], iv, AES_ENCRYPT);
}

void
aes_decrypt(struct keystate *ks, u_int8_t *data, u_int16_t len)
{
	u_int8_t        iv[MAXBLK];

	memcpy(iv, ks->riv, ks->xf->blocksize);
	AES_cbc_encrypt(data, data, len, &ks->ks_aes[1], iv, AES_DECRYPT);
}

struct crypto_xf *
crypto_get(enum transform id)
{
	size_t          i;

	for (i = 0; i < sizeof transforms / sizeof transforms[0]; i++)
		if (id == transforms[i].id)
			return &transforms[i];

	return 0;
}

struct keystate *
crypto_init(struct crypto_xf *xf, u_int8_t *key, u_int16_t len,
    enum cryptoerr *err)
{
	struct keystate *ks;

	if (len < xf->keymin || len > xf->keymax) {
		LOG_DBG((LOG_CRYPTO, 10, "crypto_init: invalid key length %d",
		    len));
		*err = EKEYLEN;
		return 0;
	}
	ks = calloc(1, sizeof *ks);
	if (!ks) {
		log_error("crypto_init: calloc (1, %lu) failed",
		    (unsigned long)sizeof *ks);
		*err = ENOCRYPTO;
		return 0;
	}
	ks->xf = xf;

	/* Setup the IV.  */
	ks->riv = ks->iv;
	ks->liv = ks->iv2;

	LOG_DBG_BUF((LOG_CRYPTO, 40, "crypto_init: key", key, len));

	*err = xf->init(ks, key, len);
	if (*err != EOKAY) {
		LOG_DBG((LOG_CRYPTO, 30, "crypto_init: weak key found for %s",
		    xf->name));
		free(ks);
		return 0;
	}
	return ks;
}

void
crypto_update_iv(struct keystate *ks)
{
	u_int8_t       *tmp;

	tmp = ks->riv;
	ks->riv = ks->liv;
	ks->liv = tmp;

	LOG_DBG_BUF((LOG_CRYPTO, 50, "crypto_update_iv: updated IV", ks->riv,
	    ks->xf->blocksize));
}

void
crypto_init_iv(struct keystate *ks, u_int8_t *buf, size_t len)
{
	memcpy(ks->riv, buf, len);

	LOG_DBG_BUF((LOG_CRYPTO, 50, "crypto_init_iv: initialized IV", ks->riv,
	    len));
}

void
crypto_encrypt(struct keystate *ks, u_int8_t *buf, u_int16_t len)
{
	LOG_DBG_BUF((LOG_CRYPTO, 70, "crypto_encrypt: before encryption", buf,
	    len));
	ks->xf->encrypt(ks, buf, len);
	memcpy(ks->liv, buf + len - ks->xf->blocksize, ks->xf->blocksize);
	LOG_DBG_BUF((LOG_CRYPTO, 70, "crypto_encrypt: after encryption", buf,
	    len));
}

void
crypto_decrypt(struct keystate *ks, u_int8_t *buf, u_int16_t len)
{
	LOG_DBG_BUF((LOG_CRYPTO, 70, "crypto_decrypt: before decryption", buf,
	    len));
	/*
	 * XXX There is controversy about the correctness of updating the IV
	 * like this.
	 */
	memcpy(ks->liv, buf + len - ks->xf->blocksize, ks->xf->blocksize);
	ks->xf->decrypt(ks, buf, len);
	LOG_DBG_BUF((LOG_CRYPTO, 70, "crypto_decrypt: after decryption", buf,
	    len));
}

/* Make a copy of the keystate pointed to by OKS.  */
struct keystate *
crypto_clone_keystate(struct keystate *oks)
{
	struct keystate *ks;

	ks = malloc(sizeof *ks);
	if (!ks) {
		log_error("crypto_clone_keystate: malloc (%lu) failed",
		    (unsigned long)sizeof *ks);
		return 0;
	}
	memcpy(ks, oks, sizeof *ks);
	if (oks->riv == oks->iv) {
		ks->riv = ks->iv;
		ks->liv = ks->iv2;
	} else {
		ks->riv = ks->iv2;
		ks->liv = ks->iv;
	}
	return ks;
}
