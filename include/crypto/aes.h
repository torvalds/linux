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
 * The POWER8 VSX optimized AES assembly code is borrowed from OpenSSL and
 * inherits OpenSSL's AES_KEY format, which stores the number of rounds after
 * the round keys.  That assembly code is difficult to change.  So for
 * compatibility purposes we reserve space for the extra nrounds field on PPC64.
 *
 * Note: when prepared for decryption, the round keys are just the reversed
 * standard round keys, not the round keys for the Equivalent Inverse Cipher.
 */
struct p8_aes_key {
	u32 rndkeys[AES_MAX_KEYLENGTH_U32];
	int nrounds;
};

union aes_enckey_arch {
	u32 rndkeys[AES_MAX_KEYLENGTH_U32];
#ifdef CONFIG_CRYPTO_LIB_AES_ARCH
#if defined(CONFIG_PPC) && defined(CONFIG_SPE)
	/* Used unconditionally (when SPE AES code is enabled in kconfig) */
	u32 spe_enc_key[AES_MAX_KEYLENGTH_U32] __aligned(8);
#elif defined(CONFIG_PPC)
	/*
	 * Kernels that include the POWER8 VSX optimized AES code use this field
	 * when that code is usable at key preparation time.  Otherwise they
	 * fall back to rndkeys.  In the latter case, p8.nrounds (which doesn't
	 * overlap rndkeys) is set to 0 to differentiate the two formats.
	 */
	struct p8_aes_key p8;
#elif defined(CONFIG_S390)
	/* Used when the CPU supports CPACF AES for this key's length */
	u8 raw_key[AES_MAX_KEY_SIZE];
#elif defined(CONFIG_SPARC64)
	/* Used when the CPU supports the SPARC64 AES opcodes */
	u64 sparc_rndkeys[AES_MAX_KEYLENGTH / sizeof(u64)];
#endif
#endif /* CONFIG_CRYPTO_LIB_AES_ARCH */
};

union aes_invkey_arch {
	u32 inv_rndkeys[AES_MAX_KEYLENGTH_U32];
#ifdef CONFIG_CRYPTO_LIB_AES_ARCH
#if defined(CONFIG_PPC) && defined(CONFIG_SPE)
	/* Used unconditionally (when SPE AES code is enabled in kconfig) */
	u32 spe_dec_key[AES_MAX_KEYLENGTH_U32] __aligned(8);
#elif defined(CONFIG_PPC)
	/* Used conditionally, analogous to aes_enckey_arch::p8 */
	struct p8_aes_key p8;
#endif
#endif /* CONFIG_CRYPTO_LIB_AES_ARCH */
};

/**
 * struct aes_enckey - An AES key prepared for encryption
 * @len: Key length in bytes: 16 for AES-128, 24 for AES-192, 32 for AES-256.
 * @nrounds: Number of rounds: 10 for AES-128, 12 for AES-192, 14 for AES-256.
 *	     This is '6 + @len / 4' and is cached so that AES implementations
 *	     that need it don't have to recompute it for each en/decryption.
 * @padding: Padding to make offsetof(@k) be a multiple of 16, so that aligning
 *	     this struct to a 16-byte boundary results in @k also being 16-byte
 *	     aligned.  Users aren't required to align this struct to 16 bytes,
 *	     but it may slightly improve performance.
 * @k: This typically contains the AES round keys as an array of '@nrounds + 1'
 *     groups of four u32 words.  However, architecture-specific implementations
 *     of AES may store something else here, e.g. just the raw key if it's all
 *     they need.
 *
 * Note that this struct is about half the size of struct aes_key.  This is
 * separate from struct aes_key so that modes that need only AES encryption
 * (e.g. AES-GCM, AES-CTR, AES-CMAC, tweak key in AES-XTS) don't incur the time
 * and space overhead of computing and caching the decryption round keys.
 *
 * Note that there's no decryption-only equivalent (i.e. "struct aes_deckey"),
 * since (a) it's rare that modes need decryption-only, and (b) some AES
 * implementations use the same @k for both encryption and decryption, either
 * always or conditionally; in the latter case both @k and @inv_k are needed.
 */
struct aes_enckey {
	u32 len;
	u32 nrounds;
	u32 padding[2];
	union aes_enckey_arch k;
};

/**
 * struct aes_key - An AES key prepared for encryption and decryption
 * @aes_enckey: Common fields and the key prepared for encryption
 * @inv_k: This generally contains the round keys for the AES Equivalent
 *	   Inverse Cipher, as an array of '@nrounds + 1' groups of four u32
 *	   words.  However, architecture-specific implementations of AES may
 *	   store something else here.  For example, they may leave this field
 *	   uninitialized if they use @k for both encryption and decryption.
 */
struct aes_key {
	struct aes_enckey; /* Include all fields of aes_enckey. */
	union aes_invkey_arch inv_k;
};

/*
 * Please ensure that the first two fields are 16-byte aligned
 * relative to the start of the structure, i.e., don't move them!
 */
struct crypto_aes_ctx {
	u32 key_enc[AES_MAX_KEYLENGTH_U32];
	u32 key_dec[AES_MAX_KEYLENGTH_U32];
	u32 key_length;
};

/*
 * validate key length for AES algorithms
 */
static inline int aes_check_keylen(size_t keylen)
{
	switch (keylen) {
	case AES_KEYSIZE_128:
	case AES_KEYSIZE_192:
	case AES_KEYSIZE_256:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

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

/*
 * The following functions are temporarily exported for use by the AES mode
 * implementations in arch/$(SRCARCH)/crypto/.  These exports will go away when
 * that code is migrated into lib/crypto/.
 */
#ifdef CONFIG_ARM64
int ce_aes_expandkey(struct crypto_aes_ctx *ctx, const u8 *in_key,
		     unsigned int key_len);
#elif defined(CONFIG_PPC)
void ppc_expand_key_128(u32 *key_enc, const u8 *key);
void ppc_expand_key_192(u32 *key_enc, const u8 *key);
void ppc_expand_key_256(u32 *key_enc, const u8 *key);
void ppc_generate_decrypt_key(u32 *key_dec, u32 *key_enc, unsigned int key_len);
void ppc_encrypt_ecb(u8 *out, const u8 *in, u32 *key_enc, u32 rounds,
		     u32 bytes);
void ppc_decrypt_ecb(u8 *out, const u8 *in, u32 *key_dec, u32 rounds,
		     u32 bytes);
void ppc_encrypt_cbc(u8 *out, const u8 *in, u32 *key_enc, u32 rounds, u32 bytes,
		     u8 *iv);
void ppc_decrypt_cbc(u8 *out, const u8 *in, u32 *key_dec, u32 rounds, u32 bytes,
		     u8 *iv);
void ppc_crypt_ctr(u8 *out, const u8 *in, u32 *key_enc, u32 rounds, u32 bytes,
		   u8 *iv);
void ppc_encrypt_xts(u8 *out, const u8 *in, u32 *key_enc, u32 rounds, u32 bytes,
		     u8 *iv, u32 *key_twk);
void ppc_decrypt_xts(u8 *out, const u8 *in, u32 *key_dec, u32 rounds, u32 bytes,
		     u8 *iv, u32 *key_twk);
int aes_p8_set_encrypt_key(const u8 *userKey, const int bits,
			   struct p8_aes_key *key);
int aes_p8_set_decrypt_key(const u8 *userKey, const int bits,
			   struct p8_aes_key *key);
void aes_p8_encrypt(const u8 *in, u8 *out, const struct p8_aes_key *key);
void aes_p8_decrypt(const u8 *in, u8 *out, const struct p8_aes_key *key);
void aes_p8_cbc_encrypt(const u8 *in, u8 *out, size_t len,
			const struct p8_aes_key *key, u8 *iv, const int enc);
void aes_p8_ctr32_encrypt_blocks(const u8 *in, u8 *out, size_t len,
				 const struct p8_aes_key *key, const u8 *iv);
void aes_p8_xts_encrypt(const u8 *in, u8 *out, size_t len,
			const struct p8_aes_key *key1,
			const struct p8_aes_key *key2, u8 *iv);
void aes_p8_xts_decrypt(const u8 *in, u8 *out, size_t len,
			const struct p8_aes_key *key1,
			const struct p8_aes_key *key2, u8 *iv);
#elif defined(CONFIG_SPARC64)
void aes_sparc64_key_expand(const u32 *in_key, u64 *output_key,
			    unsigned int key_len);
void aes_sparc64_load_encrypt_keys_128(const u64 *key);
void aes_sparc64_load_encrypt_keys_192(const u64 *key);
void aes_sparc64_load_encrypt_keys_256(const u64 *key);
void aes_sparc64_load_decrypt_keys_128(const u64 *key);
void aes_sparc64_load_decrypt_keys_192(const u64 *key);
void aes_sparc64_load_decrypt_keys_256(const u64 *key);
void aes_sparc64_ecb_encrypt_128(const u64 *key, const u64 *input, u64 *output,
				 unsigned int len);
void aes_sparc64_ecb_encrypt_192(const u64 *key, const u64 *input, u64 *output,
				 unsigned int len);
void aes_sparc64_ecb_encrypt_256(const u64 *key, const u64 *input, u64 *output,
				 unsigned int len);
void aes_sparc64_ecb_decrypt_128(const u64 *key, const u64 *input, u64 *output,
				 unsigned int len);
void aes_sparc64_ecb_decrypt_192(const u64 *key, const u64 *input, u64 *output,
				 unsigned int len);
void aes_sparc64_ecb_decrypt_256(const u64 *key, const u64 *input, u64 *output,
				 unsigned int len);
void aes_sparc64_cbc_encrypt_128(const u64 *key, const u64 *input, u64 *output,
				 unsigned int len, u64 *iv);
void aes_sparc64_cbc_encrypt_192(const u64 *key, const u64 *input, u64 *output,
				 unsigned int len, u64 *iv);
void aes_sparc64_cbc_encrypt_256(const u64 *key, const u64 *input, u64 *output,
				 unsigned int len, u64 *iv);
void aes_sparc64_cbc_decrypt_128(const u64 *key, const u64 *input, u64 *output,
				 unsigned int len, u64 *iv);
void aes_sparc64_cbc_decrypt_192(const u64 *key, const u64 *input, u64 *output,
				 unsigned int len, u64 *iv);
void aes_sparc64_cbc_decrypt_256(const u64 *key, const u64 *input, u64 *output,
				 unsigned int len, u64 *iv);
void aes_sparc64_ctr_crypt_128(const u64 *key, const u64 *input, u64 *output,
			       unsigned int len, u64 *iv);
void aes_sparc64_ctr_crypt_192(const u64 *key, const u64 *input, u64 *output,
			       unsigned int len, u64 *iv);
void aes_sparc64_ctr_crypt_256(const u64 *key, const u64 *input, u64 *output,
			       unsigned int len, u64 *iv);
#endif

/**
 * aes_preparekey() - Prepare an AES key for encryption and decryption
 * @key: (output) The key structure to initialize
 * @in_key: The raw AES key
 * @key_len: Length of the raw key in bytes.  Should be either AES_KEYSIZE_128,
 *	     AES_KEYSIZE_192, or AES_KEYSIZE_256.
 *
 * This prepares an AES key for both the encryption and decryption directions of
 * the block cipher.  Typically this involves expanding the raw key into both
 * the standard round keys and the Equivalent Inverse Cipher round keys, but
 * some architecture-specific implementations don't do the full expansion here.
 *
 * The caller is responsible for zeroizing both the struct aes_key and the raw
 * key once they are no longer needed.
 *
 * If you don't need decryption support, use aes_prepareenckey() instead.
 *
 * Return: 0 on success or -EINVAL if the given key length is invalid.  No other
 *	   errors are possible, so callers that always pass a valid key length
 *	   don't need to check for errors.
 *
 * Context: Any context.
 */
int aes_preparekey(struct aes_key *key, const u8 *in_key, size_t key_len);

/**
 * aes_prepareenckey() - Prepare an AES key for encryption-only
 * @key: (output) The key structure to initialize
 * @in_key: The raw AES key
 * @key_len: Length of the raw key in bytes.  Should be either AES_KEYSIZE_128,
 *	     AES_KEYSIZE_192, or AES_KEYSIZE_256.
 *
 * This prepares an AES key for only the encryption direction of the block
 * cipher.  Typically this involves expanding the raw key into only the standard
 * round keys, resulting in a struct about half the size of struct aes_key.
 *
 * The caller is responsible for zeroizing both the struct aes_enckey and the
 * raw key once they are no longer needed.
 *
 * Note that while the resulting prepared key supports only AES encryption, it
 * can still be used for decrypting in a mode of operation that uses AES in only
 * the encryption (forward) direction, for example counter mode.
 *
 * Return: 0 on success or -EINVAL if the given key length is invalid.  No other
 *	   errors are possible, so callers that always pass a valid key length
 *	   don't need to check for errors.
 *
 * Context: Any context.
 */
int aes_prepareenckey(struct aes_enckey *key, const u8 *in_key, size_t key_len);

typedef union {
	const struct aes_enckey *enc_key;
	const struct aes_key *full_key;
} aes_encrypt_arg __attribute__ ((__transparent_union__));

/**
 * aes_encrypt() - Encrypt a single AES block
 * @key: The AES key, as a pointer to either an encryption-only key
 *	 (struct aes_enckey) or a full, bidirectional key (struct aes_key).
 * @out: Buffer to store the ciphertext block
 * @in: Buffer containing the plaintext block
 *
 * Context: Any context.
 */
void aes_encrypt(aes_encrypt_arg key, u8 out[at_least AES_BLOCK_SIZE],
		 const u8 in[at_least AES_BLOCK_SIZE]);

/**
 * aes_decrypt() - Decrypt a single AES block
 * @key: The AES key, previously initialized by aes_preparekey()
 * @out: Buffer to store the plaintext block
 * @in: Buffer containing the ciphertext block
 *
 * Context: Any context.
 */
void aes_decrypt(const struct aes_key *key, u8 out[at_least AES_BLOCK_SIZE],
		 const u8 in[at_least AES_BLOCK_SIZE]);

extern const u8 crypto_aes_sbox[];
extern const u8 crypto_aes_inv_sbox[];
extern const u32 aes_enc_tab[256];
extern const u32 aes_dec_tab[256];

void aescfb_encrypt(const struct aes_enckey *key, u8 *dst, const u8 *src,
		    int len, const u8 iv[AES_BLOCK_SIZE]);
void aescfb_decrypt(const struct aes_enckey *key, u8 *dst, const u8 *src,
		    int len, const u8 iv[AES_BLOCK_SIZE]);

#endif
