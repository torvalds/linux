/*
 * WPA Supplicant / Crypto wrapper for internal crypto implementation
 * Copyright (c) 2006, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto.h"
#include "md5.h"
#include "sha1.h"
#include "rc4.h"
#include "aes.h"
#include "rsa.h"
#include "bignum.h"


#ifdef EAP_TLS_FUNCS

#ifdef CONFIG_TLS_INTERNAL

/* from des.c */
struct des3_key_s {
	u32 ek[3][32];
	u32 dk[3][32];
};

void des3_key_setup(const u8 *key, struct des3_key_s *dkey);
void des3_encrypt(const u8 *plain, const struct des3_key_s *key, u8 *crypt);
void des3_decrypt(const u8 *crypt, const struct des3_key_s *key, u8 *plain);


struct MD5Context {
	u32 buf[4];
	u32 bits[2];
	u8 in[64];
};

struct SHA1Context {
	u32 state[5];
	u32 count[2];
	unsigned char buffer[64];
};


struct crypto_hash {
	enum crypto_hash_alg alg;
	union {
		struct MD5Context md5;
		struct SHA1Context sha1;
	} u;
	u8 key[64];
	size_t key_len;
};


struct crypto_hash * crypto_hash_init(enum crypto_hash_alg alg, const u8 *key,
				      size_t key_len)
{
	struct crypto_hash *ctx;
	u8 k_pad[64];
	u8 tk[20];
	size_t i;

	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	ctx->alg = alg;

	switch (alg) {
	case CRYPTO_HASH_ALG_MD5:
		MD5Init(&ctx->u.md5);
		break;
	case CRYPTO_HASH_ALG_SHA1:
		SHA1Init(&ctx->u.sha1);
		break;
	case CRYPTO_HASH_ALG_HMAC_MD5:
		if (key_len > sizeof(k_pad)) {
			MD5Init(&ctx->u.md5);
			MD5Update(&ctx->u.md5, key, key_len);
			MD5Final(tk, &ctx->u.md5);
			key = tk;
			key_len = 16;
		}
		os_memcpy(ctx->key, key, key_len);
		ctx->key_len = key_len;

		os_memcpy(k_pad, key, key_len);
		os_memset(k_pad + key_len, 0, sizeof(k_pad) - key_len);
		for (i = 0; i < sizeof(k_pad); i++)
			k_pad[i] ^= 0x36;
		MD5Init(&ctx->u.md5);
		MD5Update(&ctx->u.md5, k_pad, sizeof(k_pad));
		break;
	case CRYPTO_HASH_ALG_HMAC_SHA1:
		if (key_len > sizeof(k_pad)) {
			SHA1Init(&ctx->u.sha1);
			SHA1Update(&ctx->u.sha1, key, key_len);
			SHA1Final(tk, &ctx->u.sha1);
			key = tk;
			key_len = 20;
		}
		os_memcpy(ctx->key, key, key_len);
		ctx->key_len = key_len;

		os_memcpy(k_pad, key, key_len);
		os_memset(k_pad + key_len, 0, sizeof(k_pad) - key_len);
		for (i = 0; i < sizeof(k_pad); i++)
			k_pad[i] ^= 0x36;
		SHA1Init(&ctx->u.sha1);
		SHA1Update(&ctx->u.sha1, k_pad, sizeof(k_pad));
		break;
	default:
		os_free(ctx);
		return NULL;
	}

	return ctx;
}


void crypto_hash_update(struct crypto_hash *ctx, const u8 *data, size_t len)
{
	if (ctx == NULL)
		return;

	switch (ctx->alg) {
	case CRYPTO_HASH_ALG_MD5:
	case CRYPTO_HASH_ALG_HMAC_MD5:
		MD5Update(&ctx->u.md5, data, len);
		break;
	case CRYPTO_HASH_ALG_SHA1:
	case CRYPTO_HASH_ALG_HMAC_SHA1:
		SHA1Update(&ctx->u.sha1, data, len);
		break;
	}
}


int crypto_hash_finish(struct crypto_hash *ctx, u8 *mac, size_t *len)
{
	u8 k_pad[64];
	size_t i;

	if (ctx == NULL)
		return -2;

	if (mac == NULL || len == NULL) {
		os_free(ctx);
		return 0;
	}

	switch (ctx->alg) {
	case CRYPTO_HASH_ALG_MD5:
		if (*len < 16) {
			*len = 16;
			os_free(ctx);
			return -1;
		}
		*len = 16;
		MD5Final(mac, &ctx->u.md5);
		break;
	case CRYPTO_HASH_ALG_SHA1:
		if (*len < 20) {
			*len = 20;
			os_free(ctx);
			return -1;
		}
		*len = 20;
		SHA1Final(mac, &ctx->u.sha1);
		break;
	case CRYPTO_HASH_ALG_HMAC_MD5:
		if (*len < 16) {
			*len = 16;
			os_free(ctx);
			return -1;
		}
		*len = 16;

		MD5Final(mac, &ctx->u.md5);

		os_memcpy(k_pad, ctx->key, ctx->key_len);
		os_memset(k_pad + ctx->key_len, 0,
			  sizeof(k_pad) - ctx->key_len);
		for (i = 0; i < sizeof(k_pad); i++)
			k_pad[i] ^= 0x5c;
		MD5Init(&ctx->u.md5);
		MD5Update(&ctx->u.md5, k_pad, sizeof(k_pad));
		MD5Update(&ctx->u.md5, mac, 16);
		MD5Final(mac, &ctx->u.md5);
		break;
	case CRYPTO_HASH_ALG_HMAC_SHA1:
		if (*len < 20) {
			*len = 20;
			os_free(ctx);
			return -1;
		}
		*len = 20;

		SHA1Final(mac, &ctx->u.sha1);

		os_memcpy(k_pad, ctx->key, ctx->key_len);
		os_memset(k_pad + ctx->key_len, 0,
			  sizeof(k_pad) - ctx->key_len);
		for (i = 0; i < sizeof(k_pad); i++)
			k_pad[i] ^= 0x5c;
		SHA1Init(&ctx->u.sha1);
		SHA1Update(&ctx->u.sha1, k_pad, sizeof(k_pad));
		SHA1Update(&ctx->u.sha1, mac, 20);
		SHA1Final(mac, &ctx->u.sha1);
		break;
	}

	os_free(ctx);

	return 0;
}


struct crypto_cipher {
	enum crypto_cipher_alg alg;
	union {
		struct {
			size_t used_bytes;
			u8 key[16];
			size_t keylen;
		} rc4;
		struct {
			u8 cbc[32];
			size_t block_size;
			void *ctx_enc;
			void *ctx_dec;
		} aes;
		struct {
			struct des3_key_s key;
			u8 cbc[8];
		} des3;
	} u;
};


struct crypto_cipher * crypto_cipher_init(enum crypto_cipher_alg alg,
					  const u8 *iv, const u8 *key,
					  size_t key_len)
{
	struct crypto_cipher *ctx;

	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	ctx->alg = alg;

	switch (alg) {
	case CRYPTO_CIPHER_ALG_RC4:
		if (key_len > sizeof(ctx->u.rc4.key)) {
			os_free(ctx);
			return NULL;
		}
		ctx->u.rc4.keylen = key_len;
		os_memcpy(ctx->u.rc4.key, key, key_len);
		break;
	case CRYPTO_CIPHER_ALG_AES:
		if (key_len > sizeof(ctx->u.aes.cbc)) {
			os_free(ctx);
			return NULL;
		}
		ctx->u.aes.ctx_enc = aes_encrypt_init(key, key_len);
		if (ctx->u.aes.ctx_enc == NULL) {
			os_free(ctx);
			return NULL;
		}
		ctx->u.aes.ctx_dec = aes_decrypt_init(key, key_len);
		if (ctx->u.aes.ctx_dec == NULL) {
			aes_encrypt_deinit(ctx->u.aes.ctx_enc);
			os_free(ctx);
			return NULL;
		}
		ctx->u.aes.block_size = key_len;
		os_memcpy(ctx->u.aes.cbc, iv, ctx->u.aes.block_size);
		break;
	case CRYPTO_CIPHER_ALG_3DES:
		if (key_len != 24) {
			os_free(ctx);
			return NULL;
		}
		des3_key_setup(key, &ctx->u.des3.key);
		os_memcpy(ctx->u.des3.cbc, iv, 8);
		break;
	default:
		os_free(ctx);
		return NULL;
	}

	return ctx;
}


int crypto_cipher_encrypt(struct crypto_cipher *ctx, const u8 *plain,
			  u8 *crypt, size_t len)
{
	size_t i, j, blocks;

	switch (ctx->alg) {
	case CRYPTO_CIPHER_ALG_RC4:
		if (plain != crypt)
			os_memcpy(crypt, plain, len);
		rc4_skip(ctx->u.rc4.key, ctx->u.rc4.keylen,
			 ctx->u.rc4.used_bytes, crypt, len);
		ctx->u.rc4.used_bytes += len;
		break;
	case CRYPTO_CIPHER_ALG_AES:
		if (len % ctx->u.aes.block_size)
			return -1;
		blocks = len / ctx->u.aes.block_size;
		for (i = 0; i < blocks; i++) {
			for (j = 0; j < ctx->u.aes.block_size; j++)
				ctx->u.aes.cbc[j] ^= plain[j];
			aes_encrypt(ctx->u.aes.ctx_enc, ctx->u.aes.cbc,
				    ctx->u.aes.cbc);
			os_memcpy(crypt, ctx->u.aes.cbc,
				  ctx->u.aes.block_size);
			plain += ctx->u.aes.block_size;
			crypt += ctx->u.aes.block_size;
		}
		break;
	case CRYPTO_CIPHER_ALG_3DES:
		if (len % 8)
			return -1;
		blocks = len / 8;
		for (i = 0; i < blocks; i++) {
			for (j = 0; j < 8; j++)
				ctx->u.des3.cbc[j] ^= plain[j];
			des3_encrypt(ctx->u.des3.cbc, &ctx->u.des3.key,
				     ctx->u.des3.cbc);
			os_memcpy(crypt, ctx->u.des3.cbc, 8);
			plain += 8;
			crypt += 8;
		}
		break;
	default:
		return -1;
	}

	return 0;
}


int crypto_cipher_decrypt(struct crypto_cipher *ctx, const u8 *crypt,
			  u8 *plain, size_t len)
{
	size_t i, j, blocks;
	u8 tmp[32];

	switch (ctx->alg) {
	case CRYPTO_CIPHER_ALG_RC4:
		if (plain != crypt)
			os_memcpy(plain, crypt, len);
		rc4_skip(ctx->u.rc4.key, ctx->u.rc4.keylen,
			 ctx->u.rc4.used_bytes, plain, len);
		ctx->u.rc4.used_bytes += len;
		break;
	case CRYPTO_CIPHER_ALG_AES:
		if (len % ctx->u.aes.block_size)
			return -1;
		blocks = len / ctx->u.aes.block_size;
		for (i = 0; i < blocks; i++) {
			os_memcpy(tmp, crypt, ctx->u.aes.block_size);
			aes_decrypt(ctx->u.aes.ctx_dec, crypt, plain);
			for (j = 0; j < ctx->u.aes.block_size; j++)
				plain[j] ^= ctx->u.aes.cbc[j];
			os_memcpy(ctx->u.aes.cbc, tmp, ctx->u.aes.block_size);
			plain += ctx->u.aes.block_size;
			crypt += ctx->u.aes.block_size;
		}
		break;
	case CRYPTO_CIPHER_ALG_3DES:
		if (len % 8)
			return -1;
		blocks = len / 8;
		for (i = 0; i < blocks; i++) {
			os_memcpy(tmp, crypt, 8);
			des3_decrypt(crypt, &ctx->u.des3.key, plain);
			for (j = 0; j < 8; j++)
				plain[j] ^= ctx->u.des3.cbc[j];
			os_memcpy(ctx->u.des3.cbc, tmp, 8);
			plain += 8;
			crypt += 8;
		}
		break;
	default:
		return -1;
	}

	return 0;
}


void crypto_cipher_deinit(struct crypto_cipher *ctx)
{
	switch (ctx->alg) {
	case CRYPTO_CIPHER_ALG_AES:
		aes_encrypt_deinit(ctx->u.aes.ctx_enc);
		aes_decrypt_deinit(ctx->u.aes.ctx_dec);
		break;
	case CRYPTO_CIPHER_ALG_3DES:
		break;
	default:
		break;
	}
	os_free(ctx);
}


/* Dummy structures; these are just typecast to struct crypto_rsa_key */
struct crypto_public_key;
struct crypto_private_key;


struct crypto_public_key * crypto_public_key_import(const u8 *key, size_t len)
{
	return (struct crypto_public_key *)
		crypto_rsa_import_public_key(key, len);
}


struct crypto_private_key * crypto_private_key_import(const u8 *key,
						      size_t len)
{
	return (struct crypto_private_key *)
		crypto_rsa_import_private_key(key, len);
}


struct crypto_public_key * crypto_public_key_from_cert(const u8 *buf,
						       size_t len)
{
	/* No X.509 support in crypto_internal.c */
	return NULL;
}


static int pkcs1_generate_encryption_block(u8 block_type, size_t modlen,
					   const u8 *in, size_t inlen,
					   u8 *out, size_t *outlen)
{
	size_t ps_len;
	u8 *pos;

	/*
	 * PKCS #1 v1.5, 8.1:
	 *
	 * EB = 00 || BT || PS || 00 || D
	 * BT = 00 or 01 for private-key operation; 02 for public-key operation
	 * PS = k-3-||D||; at least eight octets
	 * (BT=0: PS=0x00, BT=1: PS=0xff, BT=2: PS=pseudorandom non-zero)
	 * k = length of modulus in octets (modlen)
	 */

	if (modlen < 12 || modlen > *outlen || inlen > modlen - 11) {
		wpa_printf(MSG_DEBUG, "PKCS #1: %s - Invalid buffer "
			   "lengths (modlen=%lu outlen=%lu inlen=%lu)",
			   __func__, (unsigned long) modlen,
			   (unsigned long) *outlen,
			   (unsigned long) inlen);
		return -1;
	}

	pos = out;
	*pos++ = 0x00;
	*pos++ = block_type; /* BT */
	ps_len = modlen - inlen - 3;
	switch (block_type) {
	case 0:
		os_memset(pos, 0x00, ps_len);
		pos += ps_len;
		break;
	case 1:
		os_memset(pos, 0xff, ps_len);
		pos += ps_len;
		break;
	case 2:
		if (os_get_random(pos, ps_len) < 0) {
			wpa_printf(MSG_DEBUG, "PKCS #1: %s - Failed to get "
				   "random data for PS", __func__);
			return -1;
		}
		while (ps_len--) {
			if (*pos == 0x00)
				*pos = 0x01;
			pos++;
		}
		break;
	default:
		wpa_printf(MSG_DEBUG, "PKCS #1: %s - Unsupported block type "
			   "%d", __func__, block_type);
		return -1;
	}
	*pos++ = 0x00;
	os_memcpy(pos, in, inlen); /* D */

	return 0;
}


static int crypto_rsa_encrypt_pkcs1(int block_type, struct crypto_rsa_key *key,
				    int use_private,
				    const u8 *in, size_t inlen,
				    u8 *out, size_t *outlen)
{
	size_t modlen;

	modlen = crypto_rsa_get_modulus_len(key);

	if (pkcs1_generate_encryption_block(block_type, modlen, in, inlen,
					    out, outlen) < 0)
		return -1;

	return crypto_rsa_exptmod(out, modlen, out, outlen, key, use_private);
}


int crypto_public_key_encrypt_pkcs1_v15(struct crypto_public_key *key,
					const u8 *in, size_t inlen,
					u8 *out, size_t *outlen)
{
	return crypto_rsa_encrypt_pkcs1(2, (struct crypto_rsa_key *) key,
					0, in, inlen, out, outlen);
}


int crypto_private_key_sign_pkcs1(struct crypto_private_key *key,
				  const u8 *in, size_t inlen,
				  u8 *out, size_t *outlen)
{
	return crypto_rsa_encrypt_pkcs1(1, (struct crypto_rsa_key *) key,
					1, in, inlen, out, outlen);
}


void crypto_public_key_free(struct crypto_public_key *key)
{
	crypto_rsa_free((struct crypto_rsa_key *) key);
}


void crypto_private_key_free(struct crypto_private_key *key)
{
	crypto_rsa_free((struct crypto_rsa_key *) key);
}


int crypto_public_key_decrypt_pkcs1(struct crypto_public_key *key,
				    const u8 *crypt, size_t crypt_len,
				    u8 *plain, size_t *plain_len)
{
	size_t len;
	u8 *pos;

	len = *plain_len;
	if (crypto_rsa_exptmod(crypt, crypt_len, plain, &len,
			       (struct crypto_rsa_key *) key, 0) < 0)
		return -1;

	/*
	 * PKCS #1 v1.5, 8.1:
	 *
	 * EB = 00 || BT || PS || 00 || D
	 * BT = 01
	 * PS = k-3-||D|| times FF
	 * k = length of modulus in octets
	 */

	if (len < 3 + 8 + 16 /* min hash len */ ||
	    plain[0] != 0x00 || plain[1] != 0x01 || plain[2] != 0xff) {
		wpa_printf(MSG_INFO, "LibTomCrypt: Invalid signature EB "
			   "structure");
		return -1;
	}

	pos = plain + 3;
	while (pos < plain + len && *pos == 0xff)
		pos++;
	if (pos - plain - 2 < 8) {
		/* PKCS #1 v1.5, 8.1: At least eight octets long PS */
		wpa_printf(MSG_INFO, "LibTomCrypt: Too short signature "
			   "padding");
		return -1;
	}

	if (pos + 16 /* min hash len */ >= plain + len || *pos != 0x00) {
		wpa_printf(MSG_INFO, "LibTomCrypt: Invalid signature EB "
			   "structure (2)");
		return -1;
	}
	pos++;
	len -= pos - plain;

	/* Strip PKCS #1 header */
	os_memmove(plain, pos, len);
	*plain_len = len;

	return 0;
}


int crypto_global_init(void)
{
	return 0;
}


void crypto_global_deinit(void)
{
}


#ifdef EAP_FAST

int crypto_mod_exp(const u8 *base, size_t base_len,
		   const u8 *power, size_t power_len,
		   const u8 *modulus, size_t modulus_len,
		   u8 *result, size_t *result_len)
{
	struct bignum *bn_base, *bn_exp, *bn_modulus, *bn_result;
	int ret = 0;

	bn_base = bignum_init();
	bn_exp = bignum_init();
	bn_modulus = bignum_init();
	bn_result = bignum_init();

	if (bn_base == NULL || bn_exp == NULL || bn_modulus == NULL ||
	    bn_result == NULL)
		goto error;

	if (bignum_set_unsigned_bin(bn_base, base, base_len) < 0 ||
	    bignum_set_unsigned_bin(bn_exp, power, power_len) < 0 ||
	    bignum_set_unsigned_bin(bn_modulus, modulus, modulus_len) < 0)
		goto error;

	if (bignum_exptmod(bn_base, bn_exp, bn_modulus, bn_result) < 0)
		goto error;

	ret = bignum_get_unsigned_bin(bn_result, result, result_len);

error:
	bignum_deinit(bn_base);
	bignum_deinit(bn_exp);
	bignum_deinit(bn_modulus);
	bignum_deinit(bn_result);
	return ret;
}

#endif /* EAP_FAST */


#endif /* CONFIG_TLS_INTERNAL */

#endif /* EAP_TLS_FUNCS */
