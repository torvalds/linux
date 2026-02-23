/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AES block cipher, optimized for ARM64
 *
 * Copyright (C) 2013 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 * Copyright 2026 Google LLC
 */

#include <asm/neon.h>
#include <asm/simd.h>
#include <linux/unaligned.h>
#include <linux/cpufeature.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_aes);

struct aes_block {
	u8 b[AES_BLOCK_SIZE];
};

asmlinkage void __aes_arm64_encrypt(const u32 rk[], u8 out[AES_BLOCK_SIZE],
				    const u8 in[AES_BLOCK_SIZE], int rounds);
asmlinkage void __aes_arm64_decrypt(const u32 inv_rk[], u8 out[AES_BLOCK_SIZE],
				    const u8 in[AES_BLOCK_SIZE], int rounds);
asmlinkage void __aes_ce_encrypt(const u32 rk[], u8 out[AES_BLOCK_SIZE],
				 const u8 in[AES_BLOCK_SIZE], int rounds);
asmlinkage void __aes_ce_decrypt(const u32 inv_rk[], u8 out[AES_BLOCK_SIZE],
				 const u8 in[AES_BLOCK_SIZE], int rounds);
asmlinkage u32 __aes_ce_sub(u32 l);
asmlinkage void __aes_ce_invert(struct aes_block *out,
				const struct aes_block *in);

/*
 * Expand an AES key using the crypto extensions if supported and usable or
 * generic code otherwise.  The expanded key format is compatible between the
 * two cases.  The outputs are @rndkeys (required) and @inv_rndkeys (optional).
 */
static void aes_expandkey_arm64(u32 rndkeys[], u32 *inv_rndkeys,
				const u8 *in_key, int key_len, int nrounds)
{
	/*
	 * The AES key schedule round constants
	 */
	static u8 const rcon[] = {
		0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36,
	};

	u32 kwords = key_len / sizeof(u32);
	struct aes_block *key_enc, *key_dec;
	int i, j;

	if (!IS_ENABLED(CONFIG_KERNEL_MODE_NEON) ||
	    !static_branch_likely(&have_aes) || unlikely(!may_use_simd())) {
		aes_expandkey_generic(rndkeys, inv_rndkeys, in_key, key_len);
		return;
	}

	for (i = 0; i < kwords; i++)
		rndkeys[i] = get_unaligned_le32(&in_key[i * sizeof(u32)]);

	scoped_ksimd() {
		for (i = 0; i < sizeof(rcon); i++) {
			u32 *rki = &rndkeys[i * kwords];
			u32 *rko = rki + kwords;

			rko[0] = ror32(__aes_ce_sub(rki[kwords - 1]), 8) ^
				 rcon[i] ^ rki[0];
			rko[1] = rko[0] ^ rki[1];
			rko[2] = rko[1] ^ rki[2];
			rko[3] = rko[2] ^ rki[3];

			if (key_len == AES_KEYSIZE_192) {
				if (i >= 7)
					break;
				rko[4] = rko[3] ^ rki[4];
				rko[5] = rko[4] ^ rki[5];
			} else if (key_len == AES_KEYSIZE_256) {
				if (i >= 6)
					break;
				rko[4] = __aes_ce_sub(rko[3]) ^ rki[4];
				rko[5] = rko[4] ^ rki[5];
				rko[6] = rko[5] ^ rki[6];
				rko[7] = rko[6] ^ rki[7];
			}
		}

		/*
		 * Generate the decryption keys for the Equivalent Inverse
		 * Cipher.  This involves reversing the order of the round
		 * keys, and applying the Inverse Mix Columns transformation on
		 * all but the first and the last one.
		 */
		if (inv_rndkeys) {
			key_enc = (struct aes_block *)rndkeys;
			key_dec = (struct aes_block *)inv_rndkeys;
			j = nrounds;

			key_dec[0] = key_enc[j];
			for (i = 1, j--; j > 0; i++, j--)
				__aes_ce_invert(key_dec + i, key_enc + j);
			key_dec[i] = key_enc[0];
		}
	}
}

static void aes_preparekey_arch(union aes_enckey_arch *k,
				union aes_invkey_arch *inv_k,
				const u8 *in_key, int key_len, int nrounds)
{
	aes_expandkey_arm64(k->rndkeys, inv_k ? inv_k->inv_rndkeys : NULL,
			    in_key, key_len, nrounds);
}

/*
 * This is here temporarily until the remaining AES mode implementations are
 * migrated from arch/arm64/crypto/ to lib/crypto/arm64/.
 */
int ce_aes_expandkey(struct crypto_aes_ctx *ctx, const u8 *in_key,
		     unsigned int key_len)
{
	if (aes_check_keylen(key_len) != 0)
		return -EINVAL;
	ctx->key_length = key_len;
	aes_expandkey_arm64(ctx->key_enc, ctx->key_dec, in_key, key_len,
			    6 + key_len / 4);
	return 0;
}
EXPORT_SYMBOL(ce_aes_expandkey);

static void aes_encrypt_arch(const struct aes_enckey *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    static_branch_likely(&have_aes) && likely(may_use_simd())) {
		scoped_ksimd()
			__aes_ce_encrypt(key->k.rndkeys, out, in, key->nrounds);
	} else {
		__aes_arm64_encrypt(key->k.rndkeys, out, in, key->nrounds);
	}
}

static void aes_decrypt_arch(const struct aes_key *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    static_branch_likely(&have_aes) && likely(may_use_simd())) {
		scoped_ksimd()
			__aes_ce_decrypt(key->inv_k.inv_rndkeys, out, in,
					 key->nrounds);
	} else {
		__aes_arm64_decrypt(key->inv_k.inv_rndkeys, out, in,
				    key->nrounds);
	}
}

#ifdef CONFIG_KERNEL_MODE_NEON
#define aes_mod_init_arch aes_mod_init_arch
static void aes_mod_init_arch(void)
{
	if (cpu_have_named_feature(AES))
		static_branch_enable(&have_aes);
}
#endif /* CONFIG_KERNEL_MODE_NEON */
