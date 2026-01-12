/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 * Copyright (C) 2015 International Business Machines Inc.
 * Copyright 2026 Google LLC
 */
#include <asm/simd.h>
#include <asm/switch_to.h>
#include <linux/cpufeature.h>
#include <linux/jump_label.h>
#include <linux/preempt.h>
#include <linux/uaccess.h>

#ifdef CONFIG_SPE

EXPORT_SYMBOL_GPL(ppc_expand_key_128);
EXPORT_SYMBOL_GPL(ppc_expand_key_192);
EXPORT_SYMBOL_GPL(ppc_expand_key_256);
EXPORT_SYMBOL_GPL(ppc_generate_decrypt_key);
EXPORT_SYMBOL_GPL(ppc_encrypt_ecb);
EXPORT_SYMBOL_GPL(ppc_decrypt_ecb);
EXPORT_SYMBOL_GPL(ppc_encrypt_cbc);
EXPORT_SYMBOL_GPL(ppc_decrypt_cbc);
EXPORT_SYMBOL_GPL(ppc_crypt_ctr);
EXPORT_SYMBOL_GPL(ppc_encrypt_xts);
EXPORT_SYMBOL_GPL(ppc_decrypt_xts);

void ppc_encrypt_aes(u8 *out, const u8 *in, const u32 *key_enc, u32 rounds);
void ppc_decrypt_aes(u8 *out, const u8 *in, const u32 *key_dec, u32 rounds);

static void spe_begin(void)
{
	/* disable preemption and save users SPE registers if required */
	preempt_disable();
	enable_kernel_spe();
}

static void spe_end(void)
{
	disable_kernel_spe();
	/* reenable preemption */
	preempt_enable();
}

static void aes_preparekey_arch(union aes_enckey_arch *k,
				union aes_invkey_arch *inv_k,
				const u8 *in_key, int key_len, int nrounds)
{
	if (key_len == AES_KEYSIZE_128)
		ppc_expand_key_128(k->spe_enc_key, in_key);
	else if (key_len == AES_KEYSIZE_192)
		ppc_expand_key_192(k->spe_enc_key, in_key);
	else
		ppc_expand_key_256(k->spe_enc_key, in_key);

	if (inv_k)
		ppc_generate_decrypt_key(inv_k->spe_dec_key, k->spe_enc_key,
					 key_len);
}

static void aes_encrypt_arch(const struct aes_enckey *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	spe_begin();
	ppc_encrypt_aes(out, in, key->k.spe_enc_key, key->nrounds / 2 - 1);
	spe_end();
}

static void aes_decrypt_arch(const struct aes_key *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	spe_begin();
	ppc_decrypt_aes(out, in, key->inv_k.spe_dec_key, key->nrounds / 2 - 1);
	spe_end();
}

#else /* CONFIG_SPE */

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_vec_crypto);

EXPORT_SYMBOL_GPL(aes_p8_set_encrypt_key);
EXPORT_SYMBOL_GPL(aes_p8_set_decrypt_key);
EXPORT_SYMBOL_GPL(aes_p8_encrypt);
EXPORT_SYMBOL_GPL(aes_p8_decrypt);
EXPORT_SYMBOL_GPL(aes_p8_cbc_encrypt);
EXPORT_SYMBOL_GPL(aes_p8_ctr32_encrypt_blocks);
EXPORT_SYMBOL_GPL(aes_p8_xts_encrypt);
EXPORT_SYMBOL_GPL(aes_p8_xts_decrypt);

static inline bool is_vsx_format(const struct p8_aes_key *key)
{
	return key->nrounds != 0;
}

/*
 * Convert a round key from VSX to generic format by reflecting the 16 bytes,
 * and (if apply_inv_mix=true) applying InvMixColumn to each column.
 *
 * It would be nice if the VSX and generic key formats would be compatible.  But
 * that's very difficult to do, with the assembly code having been borrowed from
 * OpenSSL and also targeted to POWER8 rather than POWER9.
 *
 * Fortunately, this conversion should only be needed in extremely rare cases,
 * possibly not at all in practice.  It's just included for full correctness.
 */
static void rndkey_from_vsx(u32 out[4], const u32 in[4], bool apply_inv_mix)
{
	u32 k0 = swab32(in[0]);
	u32 k1 = swab32(in[1]);
	u32 k2 = swab32(in[2]);
	u32 k3 = swab32(in[3]);

	if (apply_inv_mix) {
		k0 = inv_mix_columns(k0);
		k1 = inv_mix_columns(k1);
		k2 = inv_mix_columns(k2);
		k3 = inv_mix_columns(k3);
	}
	out[0] = k3;
	out[1] = k2;
	out[2] = k1;
	out[3] = k0;
}

static void aes_preparekey_arch(union aes_enckey_arch *k,
				union aes_invkey_arch *inv_k,
				const u8 *in_key, int key_len, int nrounds)
{
	const int keybits = 8 * key_len;
	int ret;

	if (static_branch_likely(&have_vec_crypto) && likely(may_use_simd())) {
		preempt_disable();
		pagefault_disable();
		enable_kernel_vsx();
		ret = aes_p8_set_encrypt_key(in_key, keybits, &k->p8);
		/*
		 * aes_p8_set_encrypt_key() should never fail here, since the
		 * key length was already validated.
		 */
		WARN_ON_ONCE(ret);
		if (inv_k) {
			ret = aes_p8_set_decrypt_key(in_key, keybits,
						     &inv_k->p8);
			/* ... and likewise for aes_p8_set_decrypt_key(). */
			WARN_ON_ONCE(ret);
		}
		disable_kernel_vsx();
		pagefault_enable();
		preempt_enable();
	} else {
		aes_expandkey_generic(k->rndkeys,
				      inv_k ? inv_k->inv_rndkeys : NULL,
				      in_key, key_len);
		/* Mark the key as using the generic format. */
		k->p8.nrounds = 0;
		if (inv_k)
			inv_k->p8.nrounds = 0;
	}
}

static void aes_encrypt_arch(const struct aes_enckey *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	if (static_branch_likely(&have_vec_crypto) &&
	    likely(is_vsx_format(&key->k.p8) && may_use_simd())) {
		preempt_disable();
		pagefault_disable();
		enable_kernel_vsx();
		aes_p8_encrypt(in, out, &key->k.p8);
		disable_kernel_vsx();
		pagefault_enable();
		preempt_enable();
	} else if (unlikely(is_vsx_format(&key->k.p8))) {
		/*
		 * This handles (the hopefully extremely rare) case where a key
		 * was prepared using the VSX optimized format, then encryption
		 * is done in a context that cannot use VSX instructions.
		 */
		u32 rndkeys[AES_MAX_KEYLENGTH_U32];

		for (int i = 0; i < 4 * (key->nrounds + 1); i += 4)
			rndkey_from_vsx(&rndkeys[i],
					&key->k.p8.rndkeys[i], false);
		aes_encrypt_generic(rndkeys, key->nrounds, out, in);
	} else {
		aes_encrypt_generic(key->k.rndkeys, key->nrounds, out, in);
	}
}

static void aes_decrypt_arch(const struct aes_key *key, u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	if (static_branch_likely(&have_vec_crypto) &&
	    likely(is_vsx_format(&key->inv_k.p8) && may_use_simd())) {
		preempt_disable();
		pagefault_disable();
		enable_kernel_vsx();
		aes_p8_decrypt(in, out, &key->inv_k.p8);
		disable_kernel_vsx();
		pagefault_enable();
		preempt_enable();
	} else if (unlikely(is_vsx_format(&key->inv_k.p8))) {
		/*
		 * This handles (the hopefully extremely rare) case where a key
		 * was prepared using the VSX optimized format, then decryption
		 * is done in a context that cannot use VSX instructions.
		 */
		u32 inv_rndkeys[AES_MAX_KEYLENGTH_U32];
		int i;

		rndkey_from_vsx(&inv_rndkeys[0],
				&key->inv_k.p8.rndkeys[0], false);
		for (i = 4; i < 4 * key->nrounds; i += 4) {
			rndkey_from_vsx(&inv_rndkeys[i],
					&key->inv_k.p8.rndkeys[i], true);
		}
		rndkey_from_vsx(&inv_rndkeys[i],
				&key->inv_k.p8.rndkeys[i], false);
		aes_decrypt_generic(inv_rndkeys, key->nrounds, out, in);
	} else {
		aes_decrypt_generic(key->inv_k.inv_rndkeys, key->nrounds,
				    out, in);
	}
}

#define aes_mod_init_arch aes_mod_init_arch
static void aes_mod_init_arch(void)
{
	if (cpu_has_feature(CPU_FTR_ARCH_207S) &&
	    (cur_cpu_spec->cpu_user_features2 & PPC_FEATURE2_VEC_CRYPTO))
		static_branch_enable(&have_vec_crypto);
}

#endif /* !CONFIG_SPE */
