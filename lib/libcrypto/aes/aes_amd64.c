/* $OpenBSD: aes_amd64.c,v 1.5 2025/07/22 09:13:49 jsing Exp $ */
/*
 * Copyright (c) 2025 Joel Sing <jsing@openbsd.org>
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

#include <openssl/aes.h>

#include "crypto_arch.h"
#include "modes_local.h"

int aes_set_encrypt_key_generic(const unsigned char *userKey, const int bits,
    AES_KEY *key);
int aes_set_decrypt_key_generic(const unsigned char *userKey, const int bits,
    AES_KEY *key);

void aes_encrypt_generic(const unsigned char *in, unsigned char *out,
    const AES_KEY *key);
void aes_decrypt_generic(const unsigned char *in, unsigned char *out,
    const AES_KEY *key);

void aes_cbc_encrypt_generic(const unsigned char *in, unsigned char *out,
    size_t len, const AES_KEY *key, unsigned char *ivec, const int enc);

void aes_ccm64_encrypt_generic(const unsigned char *in, unsigned char *out,
    size_t blocks, const void *key, const unsigned char ivec[16],
    unsigned char cmac[16], int encrypt);

void aes_ctr32_encrypt_generic(const unsigned char *in, unsigned char *out,
    size_t blocks, const AES_KEY *key, const unsigned char ivec[AES_BLOCK_SIZE]);

void aes_xts_encrypt_generic(const unsigned char *in, unsigned char *out,
    size_t len, const AES_KEY *key1, const AES_KEY *key2,
    const unsigned char iv[16], int encrypt);

int aesni_set_encrypt_key(const unsigned char *userKey, int bits,
    AES_KEY *key);
int aesni_set_decrypt_key(const unsigned char *userKey, int bits,
    AES_KEY *key);

void aesni_encrypt(const unsigned char *in, unsigned char *out,
    const AES_KEY *key);
void aesni_decrypt(const unsigned char *in, unsigned char *out,
    const AES_KEY *key);

void aesni_cbc_encrypt(const unsigned char *in, unsigned char *out,
    size_t len, const AES_KEY *key, unsigned char *ivec, const int enc);

void aesni_ccm64_encrypt_blocks(const unsigned char *in, unsigned char *out,
    size_t blocks, const void *key, const unsigned char ivec[16],
    unsigned char cmac[16]);

void aesni_ccm64_decrypt_blocks(const unsigned char *in, unsigned char *out,
    size_t blocks, const void *key, const unsigned char ivec[16],
    unsigned char cmac[16]);

void aesni_ctr32_encrypt_blocks(const unsigned char *in, unsigned char *out,
    size_t blocks, const void *key, const unsigned char *ivec);

void aesni_ecb_encrypt(const unsigned char *in, unsigned char *out,
    size_t length, const AES_KEY *key, int enc);

void aesni_xts_encrypt(const unsigned char *in, unsigned char *out,
    size_t length, const AES_KEY *key1, const AES_KEY *key2,
    const unsigned char iv[16]);

void aesni_xts_decrypt(const unsigned char *in, unsigned char *out,
    size_t length, const AES_KEY *key1, const AES_KEY *key2,
    const unsigned char iv[16]);

int
aes_set_encrypt_key_internal(const unsigned char *userKey, const int bits,
    AES_KEY *key)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_AES) != 0)
		return aesni_set_encrypt_key(userKey, bits, key);

	return aes_set_encrypt_key_generic(userKey, bits, key);
}

int
aes_set_decrypt_key_internal(const unsigned char *userKey, const int bits,
    AES_KEY *key)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_AES) != 0)
		return aesni_set_decrypt_key(userKey, bits, key);

	return aes_set_decrypt_key_generic(userKey, bits, key);
}

void
aes_encrypt_internal(const unsigned char *in, unsigned char *out,
    const AES_KEY *key)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_AES) != 0) {
		aesni_encrypt(in, out, key);
		return;
	}

	aes_encrypt_generic(in, out, key);
}

void
aes_decrypt_internal(const unsigned char *in, unsigned char *out,
    const AES_KEY *key)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_AES) != 0) {
		aesni_decrypt(in, out, key);
		return;
	}

	aes_decrypt_generic(in, out, key);
}

void
aes_cbc_encrypt_internal(const unsigned char *in, unsigned char *out,
    size_t len, const AES_KEY *key, unsigned char *ivec, const int enc)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_AES) != 0) {
		aesni_cbc_encrypt(in, out, len, key, ivec, enc);
		return;
	}

	aes_cbc_encrypt_generic(in, out, len, key, ivec, enc);
}

void
aes_ccm64_encrypt_internal(const unsigned char *in, unsigned char *out,
    size_t blocks, const void *key, const unsigned char ivec[16],
    unsigned char cmac[16], int encrypt)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_AES) != 0) {
		if (encrypt)
			aesni_ccm64_encrypt_blocks(in, out, blocks, key, ivec, cmac);
		else
			aesni_ccm64_decrypt_blocks(in, out, blocks, key, ivec, cmac);
		return;
	}

	aes_ccm64_encrypt_generic(in, out, blocks, key, ivec, cmac, encrypt);
}

void
aes_ctr32_encrypt_internal(const unsigned char *in, unsigned char *out,
    size_t blocks, const AES_KEY *key, const unsigned char ivec[AES_BLOCK_SIZE])
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_AES) != 0) {
		aesni_ctr32_encrypt_blocks(in, out, blocks, key, ivec);
		return;
	}

	aes_ctr32_encrypt_generic(in, out, blocks, key, ivec);
}

void
aes_ecb_encrypt_internal(const unsigned char *in, unsigned char *out,
    size_t len, const AES_KEY *key, int encrypt)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_AES) != 0) {
		aesni_ecb_encrypt(in, out, len, key, encrypt);
		return;
	}

	while (len >= AES_BLOCK_SIZE) {
		if (encrypt)
			aes_encrypt_generic(in, out, key);
		else
			aes_decrypt_generic(in, out, key);

		in += AES_BLOCK_SIZE;
		out += AES_BLOCK_SIZE;
		len -= AES_BLOCK_SIZE;
	}
}

void
aes_xts_encrypt_internal(const unsigned char *in, unsigned char *out,
    size_t len, const AES_KEY *key1, const AES_KEY *key2,
    const unsigned char iv[16], int encrypt)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_AES) != 0) {
		if (encrypt)
			aesni_xts_encrypt(in, out, len, key1, key2, iv);
		else
			aesni_xts_decrypt(in, out, len, key1, key2, iv);
		return;
	}

	aes_xts_encrypt_generic(in, out, len, key1, key2, iv, encrypt);
}
