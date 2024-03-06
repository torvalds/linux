/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2002 David S. Miller (davem@redhat.com)
 * Copyright (c) 2005 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * Portions derived from Cryptoapi, by Alexander Kjeldaas <astor@fast.no>
 * and Nettle, by Niels Möller.
 */

#ifndef _CRYPTO_INTERNAL_CIPHER_H
#define _CRYPTO_INTERNAL_CIPHER_H

#include <crypto/algapi.h>

struct crypto_cipher {
	struct crypto_tfm base;
};

/**
 * DOC: Single Block Cipher API
 *
 * The single block cipher API is used with the ciphers of type
 * CRYPTO_ALG_TYPE_CIPHER (listed as type "cipher" in /proc/crypto).
 *
 * Using the single block cipher API calls, operations with the basic cipher
 * primitive can be implemented. These cipher primitives exclude any block
 * chaining operations including IV handling.
 *
 * The purpose of this single block cipher API is to support the implementation
 * of templates or other concepts that only need to perform the cipher operation
 * on one block at a time. Templates invoke the underlying cipher primitive
 * block-wise and process either the input or the output data of these cipher
 * operations.
 */

static inline struct crypto_cipher *__crypto_cipher_cast(struct crypto_tfm *tfm)
{
	return (struct crypto_cipher *)tfm;
}

/**
 * crypto_alloc_cipher() - allocate single block cipher handle
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	     single block cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Allocate a cipher handle for a single block cipher. The returned struct
 * crypto_cipher is the cipher handle that is required for any subsequent API
 * invocation for that single block cipher.
 *
 * Return: allocated cipher handle in case of success; IS_ERR() is true in case
 *	   of an error, PTR_ERR() returns the error code.
 */
static inline struct crypto_cipher *crypto_alloc_cipher(const char *alg_name,
							u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_CIPHER;
	mask |= CRYPTO_ALG_TYPE_MASK;

	return __crypto_cipher_cast(crypto_alloc_base(alg_name, type, mask));
}

static inline struct crypto_tfm *crypto_cipher_tfm(struct crypto_cipher *tfm)
{
	return &tfm->base;
}

/**
 * crypto_free_cipher() - zeroize and free the single block cipher handle
 * @tfm: cipher handle to be freed
 */
static inline void crypto_free_cipher(struct crypto_cipher *tfm)
{
	crypto_free_tfm(crypto_cipher_tfm(tfm));
}

/**
 * crypto_has_cipher() - Search for the availability of a single block cipher
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	     single block cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Return: true when the single block cipher is known to the kernel crypto API;
 *	   false otherwise
 */
static inline int crypto_has_cipher(const char *alg_name, u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_CIPHER;
	mask |= CRYPTO_ALG_TYPE_MASK;

	return crypto_has_alg(alg_name, type, mask);
}

/**
 * crypto_cipher_blocksize() - obtain block size for cipher
 * @tfm: cipher handle
 *
 * The block size for the single block cipher referenced with the cipher handle
 * tfm is returned. The caller may use that information to allocate appropriate
 * memory for the data returned by the encryption or decryption operation
 *
 * Return: block size of cipher
 */
static inline unsigned int crypto_cipher_blocksize(struct crypto_cipher *tfm)
{
	return crypto_tfm_alg_blocksize(crypto_cipher_tfm(tfm));
}

static inline unsigned int crypto_cipher_alignmask(struct crypto_cipher *tfm)
{
	return crypto_tfm_alg_alignmask(crypto_cipher_tfm(tfm));
}

static inline u32 crypto_cipher_get_flags(struct crypto_cipher *tfm)
{
	return crypto_tfm_get_flags(crypto_cipher_tfm(tfm));
}

static inline void crypto_cipher_set_flags(struct crypto_cipher *tfm,
					   u32 flags)
{
	crypto_tfm_set_flags(crypto_cipher_tfm(tfm), flags);
}

static inline void crypto_cipher_clear_flags(struct crypto_cipher *tfm,
					     u32 flags)
{
	crypto_tfm_clear_flags(crypto_cipher_tfm(tfm), flags);
}

/**
 * crypto_cipher_setkey() - set key for cipher
 * @tfm: cipher handle
 * @key: buffer holding the key
 * @keylen: length of the key in bytes
 *
 * The caller provided key is set for the single block cipher referenced by the
 * cipher handle.
 *
 * Note, the key length determines the cipher type. Many block ciphers implement
 * different cipher modes depending on the key size, such as AES-128 vs AES-192
 * vs. AES-256. When providing a 16 byte key for an AES cipher handle, AES-128
 * is performed.
 *
 * Return: 0 if the setting of the key was successful; < 0 if an error occurred
 */
int crypto_cipher_setkey(struct crypto_cipher *tfm,
			 const u8 *key, unsigned int keylen);

/**
 * crypto_cipher_encrypt_one() - encrypt one block of plaintext
 * @tfm: cipher handle
 * @dst: points to the buffer that will be filled with the ciphertext
 * @src: buffer holding the plaintext to be encrypted
 *
 * Invoke the encryption operation of one block. The caller must ensure that
 * the plaintext and ciphertext buffers are at least one block in size.
 */
void crypto_cipher_encrypt_one(struct crypto_cipher *tfm,
			       u8 *dst, const u8 *src);

/**
 * crypto_cipher_decrypt_one() - decrypt one block of ciphertext
 * @tfm: cipher handle
 * @dst: points to the buffer that will be filled with the plaintext
 * @src: buffer holding the ciphertext to be decrypted
 *
 * Invoke the decryption operation of one block. The caller must ensure that
 * the plaintext and ciphertext buffers are at least one block in size.
 */
void crypto_cipher_decrypt_one(struct crypto_cipher *tfm,
			       u8 *dst, const u8 *src);

struct crypto_cipher *crypto_clone_cipher(struct crypto_cipher *cipher);

struct crypto_cipher_spawn {
	struct crypto_spawn base;
};

static inline int crypto_grab_cipher(struct crypto_cipher_spawn *spawn,
				     struct crypto_instance *inst,
				     const char *name, u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_CIPHER;
	mask |= CRYPTO_ALG_TYPE_MASK;
	return crypto_grab_spawn(&spawn->base, inst, name, type, mask);
}

static inline void crypto_drop_cipher(struct crypto_cipher_spawn *spawn)
{
	crypto_drop_spawn(&spawn->base);
}

static inline struct crypto_alg *crypto_spawn_cipher_alg(
       struct crypto_cipher_spawn *spawn)
{
	return spawn->base.alg;
}

static inline struct crypto_cipher *crypto_spawn_cipher(
	struct crypto_cipher_spawn *spawn)
{
	u32 type = CRYPTO_ALG_TYPE_CIPHER;
	u32 mask = CRYPTO_ALG_TYPE_MASK;

	return __crypto_cipher_cast(crypto_spawn_tfm(&spawn->base, type, mask));
}

static inline struct cipher_alg *crypto_cipher_alg(struct crypto_cipher *tfm)
{
	return &crypto_cipher_tfm(tfm)->__crt_alg->cra_cipher;
}

#endif
