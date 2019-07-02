/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values for AES algorithms
 */

#ifndef _CRYPTO_AES_H
#define _CRYPTO_AES_H

#include <linux/types.h>
#include <linux/crypto.h>

#define AES_MIN_KEY_SIZE	16
#define AES_MAX_KEY_SIZE	32
#define AES_KEYSIZE_128		16
#define AES_KEYSIZE_192		24
#define AES_KEYSIZE_256		32
#define AES_BLOCK_SIZE		16
#define AES_MAX_KEYLENGTH	(15 * 16)
#define AES_MAX_KEYLENGTH_U32	(AES_MAX_KEYLENGTH / sizeof(u32))

/*
 * Please ensure that the first two fields are 16-byte aligned
 * relative to the start of the structure, i.e., don't move them!
 */
struct crypto_aes_ctx {
	u32 key_enc[AES_MAX_KEYLENGTH_U32];
	u32 key_dec[AES_MAX_KEYLENGTH_U32];
	u32 key_length;
};

extern const u32 crypto_ft_tab[4][256] ____cacheline_aligned;
extern const u32 crypto_fl_tab[4][256] ____cacheline_aligned;
extern const u32 crypto_it_tab[4][256] ____cacheline_aligned;
extern const u32 crypto_il_tab[4][256] ____cacheline_aligned;

int crypto_aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
		unsigned int key_len);
int crypto_aes_expand_key(struct crypto_aes_ctx *ctx, const u8 *in_key,
		unsigned int key_len);

/**
 * aes_expandkey - Expands the AES key as described in FIPS-197
 * @ctx:	The location where the computed key will be stored.
 * @in_key:	The supplied key.
 * @key_len:	The length of the supplied key.
 *
 * Returns 0 on success. The function fails only if an invalid key size (or
 * pointer) is supplied.
 * The expanded key size is 240 bytes (max of 14 rounds with a unique 16 bytes
 * key schedule plus a 16 bytes key which is used before the first round).
 * The decryption key is prepared for the "Equivalent Inverse Cipher" as
 * described in FIPS-197. The first slot (16 bytes) of each key (enc or dec) is
 * for the initial combination, the second slot for the first round and so on.
 */
int aes_expandkey(struct crypto_aes_ctx *ctx, const u8 *in_key,
		  unsigned int key_len);

/**
 * aes_encrypt - Encrypt a single AES block
 * @ctx:	Context struct containing the key schedule
 * @out:	Buffer to store the ciphertext
 * @in:		Buffer containing the plaintext
 */
void aes_encrypt(const struct crypto_aes_ctx *ctx, u8 *out, const u8 *in);

/**
 * aes_decrypt - Decrypt a single AES block
 * @ctx:	Context struct containing the key schedule
 * @out:	Buffer to store the plaintext
 * @in:		Buffer containing the ciphertext
 */
void aes_decrypt(const struct crypto_aes_ctx *ctx, u8 *out, const u8 *in);

#endif
