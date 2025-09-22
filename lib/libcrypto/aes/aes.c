/* $OpenBSD: aes.c,v 1.17 2025/09/15 07:36:12 tb Exp $ */
/* ====================================================================
 * Copyright (c) 2002-2006 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 */

#include <string.h>

#include <openssl/aes.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/modes.h>

#include "crypto_arch.h"
#include "crypto_internal.h"
#include "modes_local.h"

static const unsigned char aes_wrap_default_iv[] = {
	0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6,
};

int aes_set_encrypt_key_internal(const unsigned char *userKey, const int bits,
    AES_KEY *key);
int aes_set_decrypt_key_internal(const unsigned char *userKey, const int bits,
    AES_KEY *key);
void aes_encrypt_internal(const unsigned char *in, unsigned char *out,
    const AES_KEY *key);
void aes_decrypt_internal(const unsigned char *in, unsigned char *out,
    const AES_KEY *key);

static int
aes_rounds_for_key_length(int bits)
{
	if (bits == 128)
		return 10;
	if (bits == 192)
		return 12;
	if (bits == 256)
		return 14;

	return 0;
}

int
AES_set_encrypt_key(const unsigned char *userKey, const int bits, AES_KEY *key)
{
	if (userKey == NULL || key == NULL)
		return -1;

	explicit_bzero(key->rd_key, sizeof(key->rd_key));

	if ((key->rounds = aes_rounds_for_key_length(bits)) <= 0)
		return -2;

	return aes_set_encrypt_key_internal(userKey, bits, key);
}
LCRYPTO_ALIAS(AES_set_encrypt_key);

int
AES_set_decrypt_key(const unsigned char *userKey, const int bits, AES_KEY *key)
{
	if (userKey == NULL || key == NULL)
		return -1;

	explicit_bzero(key->rd_key, sizeof(key->rd_key));

	if ((key->rounds = aes_rounds_for_key_length(bits)) <= 0)
		return -2;

	return aes_set_decrypt_key_internal(userKey, bits, key);
}
LCRYPTO_ALIAS(AES_set_decrypt_key);

void
AES_encrypt(const unsigned char *in, unsigned char *out, const AES_KEY *key)
{
	aes_encrypt_internal(in, out, key);
}
LCRYPTO_ALIAS(AES_encrypt);

void
AES_decrypt(const unsigned char *in, unsigned char *out, const AES_KEY *key)
{
	aes_decrypt_internal(in, out, key);
}
LCRYPTO_ALIAS(AES_decrypt);

void
aes_encrypt_block128(const unsigned char *in, unsigned char *out, const void *key)
{
	aes_encrypt_internal(in, out, key);
}

void
aes_decrypt_block128(const unsigned char *in, unsigned char *out, const void *key)
{
	aes_decrypt_internal(in, out, key);
}

#ifdef HAVE_AES_CBC_ENCRYPT_INTERNAL
void aes_cbc_encrypt_internal(const unsigned char *in, unsigned char *out,
    size_t len, const AES_KEY *key, unsigned char *ivec, const int enc);

#else
static inline void
aes_cbc_encrypt_internal(const unsigned char *in, unsigned char *out,
    size_t len, const AES_KEY *key, unsigned char *ivec, const int enc)
{
	if (enc)
		CRYPTO_cbc128_encrypt(in, out, len, key, ivec,
		    aes_encrypt_block128);
	else
		CRYPTO_cbc128_decrypt(in, out, len, key, ivec,
		    aes_decrypt_block128);
}
#endif

void
AES_cbc_encrypt(const unsigned char *in, unsigned char *out,
    size_t len, const AES_KEY *key, unsigned char *ivec, const int enc)
{
	aes_cbc_encrypt_internal(in, out, len, key, ivec, enc);
}
LCRYPTO_ALIAS(AES_cbc_encrypt);

/*
 * The input and output encrypted as though 128bit cfb mode is being
 * used.  The extra state information to record how much of the
 * 128bit block we have used is contained in *num;
 */

void
AES_cfb128_encrypt(const unsigned char *in, unsigned char *out, size_t length,
    const AES_KEY *key, unsigned char *ivec, int *num, const int enc)
{
	CRYPTO_cfb128_encrypt(in, out, length, key, ivec, num, enc,
	    aes_encrypt_block128);
}
LCRYPTO_ALIAS(AES_cfb128_encrypt);

/* N.B. This expects the input to be packed, MS bit first */
void
AES_cfb1_encrypt(const unsigned char *in, unsigned char *out, size_t length,
    const AES_KEY *key, unsigned char *ivec, int *num, const int enc)
{
	CRYPTO_cfb128_1_encrypt(in, out, length, key, ivec, num, enc,
	    aes_encrypt_block128);
}
LCRYPTO_ALIAS(AES_cfb1_encrypt);

void
AES_cfb8_encrypt(const unsigned char *in, unsigned char *out, size_t length,
    const AES_KEY *key, unsigned char *ivec, int *num, const int enc)
{
	CRYPTO_cfb128_8_encrypt(in, out, length, key, ivec, num, enc,
	    aes_encrypt_block128);
}
LCRYPTO_ALIAS(AES_cfb8_encrypt);

void
aes_ccm64_encrypt_generic(const unsigned char *in, unsigned char *out,
    size_t blocks, const void *key, const unsigned char ivec[16],
    unsigned char cmac[16], int encrypt)
{
	uint8_t iv[AES_BLOCK_SIZE], buf[AES_BLOCK_SIZE];
	uint8_t in_mask;
	uint64_t ctr;
	int i;

	in_mask = 0 - (encrypt != 0);

	memcpy(iv, ivec, sizeof(iv));

	ctr = crypto_load_be64toh(&iv[8]);

	while (blocks > 0) {
		crypto_store_htobe64(&iv[8], ctr);
		aes_encrypt_internal(iv, buf, key);
		ctr++;

		for (i = 0; i < 16; i++) {
			out[i] = in[i] ^ buf[i];
			cmac[i] ^= (in[i] & in_mask) | (out[i] & ~in_mask);
		}

		aes_encrypt_internal(cmac, cmac, key);

		in += 16;
		out += 16;
		blocks--;
	}

	explicit_bzero(buf, sizeof(buf));
	explicit_bzero(iv, sizeof(iv));
}

#ifdef HAVE_AES_CCM64_ENCRYPT_INTERNAL
void aes_ccm64_encrypt_internal(const unsigned char *in, unsigned char *out,
    size_t blocks, const void *key, const unsigned char ivec[16],
    unsigned char cmac[16], int encrypt);

#else
static inline void
aes_ccm64_encrypt_internal(const unsigned char *in, unsigned char *out,
    size_t blocks, const void *key, const unsigned char ivec[16],
    unsigned char cmac[16], int encrypt)
{
	aes_ccm64_encrypt_generic(in, out, blocks, key, ivec, cmac, encrypt);
}
#endif

void
aes_ccm64_encrypt_ccm128f(const unsigned char *in, unsigned char *out,
    size_t blocks, const void *key, const unsigned char ivec[16],
    unsigned char cmac[16])
{
	aes_ccm64_encrypt_internal(in, out, blocks, key, ivec, cmac, 1);
}

void
aes_ccm64_decrypt_ccm128f(const unsigned char *in, unsigned char *out,
    size_t blocks, const void *key, const unsigned char ivec[16],
    unsigned char cmac[16])
{
	aes_ccm64_encrypt_internal(in, out, blocks, key, ivec, cmac, 0);
}

void
aes_ctr32_encrypt_generic(const unsigned char *in, unsigned char *out,
    size_t blocks, const AES_KEY *key, const unsigned char ivec[AES_BLOCK_SIZE])
{
	uint8_t iv[AES_BLOCK_SIZE], buf[AES_BLOCK_SIZE];
	uint32_t ctr;
	int i;

	memcpy(iv, ivec, sizeof(iv));

	ctr = crypto_load_be32toh(&iv[12]);

	while (blocks > 0) {
		crypto_store_htobe32(&iv[12], ctr);
		aes_encrypt_internal(iv, buf, key);
		ctr++;

		for (i = 0; i < AES_BLOCK_SIZE; i++)
			out[i] = in[i] ^ buf[i];

		in += 16;
		out += 16;
		blocks--;
	}

	explicit_bzero(buf, sizeof(buf));
	explicit_bzero(iv, sizeof(iv));
}

#ifdef HAVE_AES_CTR32_ENCRYPT_INTERNAL
void aes_ctr32_encrypt_internal(const unsigned char *in, unsigned char *out,
    size_t blocks, const AES_KEY *key, const unsigned char ivec[AES_BLOCK_SIZE]);

#else
static inline void
aes_ctr32_encrypt_internal(const unsigned char *in, unsigned char *out,
    size_t blocks, const AES_KEY *key, const unsigned char ivec[AES_BLOCK_SIZE])
{
	aes_ctr32_encrypt_generic(in, out, blocks, key, ivec);
}
#endif

void
aes_ctr32_encrypt_ctr128f(const unsigned char *in, unsigned char *out, size_t blocks,
    const void *key, const unsigned char ivec[AES_BLOCK_SIZE])
{
	aes_ctr32_encrypt_internal(in, out, blocks, key, ivec);
}

void
AES_ctr128_encrypt(const unsigned char *in, unsigned char *out,
    size_t length, const AES_KEY *key, unsigned char ivec[AES_BLOCK_SIZE],
    unsigned char ecount_buf[AES_BLOCK_SIZE], unsigned int *num)
{
	CRYPTO_ctr128_encrypt_ctr32(in, out, length, key, ivec, ecount_buf,
	    num, aes_ctr32_encrypt_ctr128f);
}
LCRYPTO_ALIAS(AES_ctr128_encrypt);

void
AES_ecb_encrypt(const unsigned char *in, unsigned char *out,
    const AES_KEY *key, const int enc)
{
	if (AES_ENCRYPT == enc)
		AES_encrypt(in, out, key);
	else
		AES_decrypt(in, out, key);
}
LCRYPTO_ALIAS(AES_ecb_encrypt);

#ifndef HAVE_AES_ECB_ENCRYPT_INTERNAL
void
aes_ecb_encrypt_internal(const unsigned char *in, unsigned char *out,
    size_t len, const AES_KEY *key, int encrypt)
{
	while (len >= AES_BLOCK_SIZE) {
		AES_ecb_encrypt(in, out, key, encrypt);
		in += AES_BLOCK_SIZE;
		out += AES_BLOCK_SIZE;
		len -= AES_BLOCK_SIZE;
	}
}
#endif

#define N_WORDS (AES_BLOCK_SIZE / sizeof(unsigned long))
typedef struct {
	unsigned long data[N_WORDS];
} aes_block_t;

void
AES_ige_encrypt(const unsigned char *in, unsigned char *out, size_t length,
    const AES_KEY *key, unsigned char *ivec, const int enc)
{
	aes_block_t tmp, tmp2;
	aes_block_t iv;
	aes_block_t iv2;
	size_t n;
	size_t len;

	/* N.B. The IV for this mode is _twice_ the block size */

	OPENSSL_assert((length % AES_BLOCK_SIZE) == 0);

	len = length / AES_BLOCK_SIZE;

	memcpy(iv.data, ivec, AES_BLOCK_SIZE);
	memcpy(iv2.data, ivec + AES_BLOCK_SIZE, AES_BLOCK_SIZE);

	if (AES_ENCRYPT == enc) {
		while (len) {
			memcpy(tmp.data, in, AES_BLOCK_SIZE);
			for (n = 0; n < N_WORDS; ++n)
				tmp2.data[n] = tmp.data[n] ^ iv.data[n];
			AES_encrypt((unsigned char *)tmp2.data,
			    (unsigned char *)tmp2.data, key);
			for (n = 0; n < N_WORDS; ++n)
				tmp2.data[n] ^= iv2.data[n];
			memcpy(out, tmp2.data, AES_BLOCK_SIZE);
			iv = tmp2;
			iv2 = tmp;
			--len;
			in += AES_BLOCK_SIZE;
			out += AES_BLOCK_SIZE;
		}
	} else {
		while (len) {
			memcpy(tmp.data, in, AES_BLOCK_SIZE);
			tmp2 = tmp;
			for (n = 0; n < N_WORDS; ++n)
				tmp.data[n] ^= iv2.data[n];
			AES_decrypt((unsigned char *)tmp.data,
			    (unsigned char *)tmp.data, key);
			for (n = 0; n < N_WORDS; ++n)
				tmp.data[n] ^= iv.data[n];
			memcpy(out, tmp.data, AES_BLOCK_SIZE);
			iv = tmp2;
			iv2 = tmp;
			--len;
			in += AES_BLOCK_SIZE;
			out += AES_BLOCK_SIZE;
		}
	}
	memcpy(ivec, iv.data, AES_BLOCK_SIZE);
	memcpy(ivec + AES_BLOCK_SIZE, iv2.data, AES_BLOCK_SIZE);
}
LCRYPTO_ALIAS(AES_ige_encrypt);

void
AES_ofb128_encrypt(const unsigned char *in, unsigned char *out, size_t length,
    const AES_KEY *key, unsigned char *ivec, int *num)
{
	CRYPTO_ofb128_encrypt(in, out, length, key, ivec, num,
	    aes_encrypt_block128);
}
LCRYPTO_ALIAS(AES_ofb128_encrypt);

void
aes_xts_encrypt_generic(const unsigned char *in, unsigned char *out, size_t len,
    const AES_KEY *key1, const AES_KEY *key2, const unsigned char iv[16],
    int encrypt)
{
	XTS128_CONTEXT xctx;

	if (encrypt)
		xctx.block1 = aes_encrypt_block128;
	else 
		xctx.block1 = aes_decrypt_block128;

	xctx.block2 = aes_encrypt_block128;
	xctx.key1 = key1;
	xctx.key2 = key2;

	CRYPTO_xts128_encrypt(&xctx, iv, in, out, len, encrypt);
}

#ifndef HAVE_AES_XTS_ENCRYPT_INTERNAL
void
aes_xts_encrypt_internal(const unsigned char *in, unsigned char *out, size_t len,
    const AES_KEY *key1, const AES_KEY *key2, const unsigned char iv[16],
    int encrypt)
{
	aes_xts_encrypt_generic(in, out, len, key1, key2, iv, encrypt);
}
#endif

int
AES_wrap_key(AES_KEY *key, const unsigned char *iv, unsigned char *out,
    const unsigned char *in, unsigned int inlen)
{
	unsigned char *A, B[16], *R;
	unsigned int i, j, t;

	if ((inlen & 0x7) || (inlen < 16))
		return -1;
	A = B;
	t = 1;
	memmove(out + 8, in, inlen);
	if (!iv)
		iv = aes_wrap_default_iv;

	memcpy(A, iv, 8);

	for (j = 0; j < 6; j++) {
		R = out + 8;
		for (i = 0; i < inlen; i += 8, t++, R += 8) {
			memcpy(B + 8, R, 8);
			AES_encrypt(B, B, key);
			A[7] ^= (unsigned char)(t & 0xff);
			if (t > 0xff) {
				A[6] ^= (unsigned char)((t >> 8) & 0xff);
				A[5] ^= (unsigned char)((t >> 16) & 0xff);
				A[4] ^= (unsigned char)((t >> 24) & 0xff);
			}
			memcpy(R, B + 8, 8);
		}
	}
	memcpy(out, A, 8);
	return inlen + 8;
}
LCRYPTO_ALIAS(AES_wrap_key);

int
AES_unwrap_key(AES_KEY *key, const unsigned char *iv, unsigned char *out,
    const unsigned char *in, unsigned int inlen)
{
	unsigned char *A, B[16], *R;
	unsigned int i, j, t;

	if ((inlen & 0x7) || (inlen < 24))
		return -1;
	inlen -= 8;
	A = B;
	t = 6 * (inlen >> 3);
	memcpy(A, in, 8);
	memmove(out, in + 8, inlen);
	for (j = 0; j < 6; j++) {
		R = out + inlen - 8;
		for (i = 0; i < inlen; i += 8, t--, R -= 8) {
			A[7] ^= (unsigned char)(t & 0xff);
			if (t > 0xff) {
				A[6] ^= (unsigned char)((t >> 8) & 0xff);
				A[5] ^= (unsigned char)((t >> 16) & 0xff);
				A[4] ^= (unsigned char)((t >> 24) & 0xff);
			}
			memcpy(B + 8, R, 8);
			AES_decrypt(B, B, key);
			memcpy(R, B + 8, 8);
		}
	}
	if (!iv)
		iv = aes_wrap_default_iv;
	if (timingsafe_memcmp(A, iv, 8) != 0) {
		explicit_bzero(out, inlen);
		return 0;
	}
	return inlen;
}
LCRYPTO_ALIAS(AES_unwrap_key);
