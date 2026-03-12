/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AES block cipher using AES-NI instructions
 *
 * Copyright 2026 Google LLC
 */

#include <asm/fpu/api.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_aes);

void aes128_expandkey_aesni(u32 rndkeys[], u32 *inv_rndkeys,
			    const u8 in_key[AES_KEYSIZE_128]);
void aes256_expandkey_aesni(u32 rndkeys[], u32 *inv_rndkeys,
			    const u8 in_key[AES_KEYSIZE_256]);
void aes_encrypt_aesni(const u32 rndkeys[], int nrounds,
		       u8 out[AES_BLOCK_SIZE], const u8 in[AES_BLOCK_SIZE]);
void aes_decrypt_aesni(const u32 inv_rndkeys[], int nrounds,
		       u8 out[AES_BLOCK_SIZE], const u8 in[AES_BLOCK_SIZE]);

/*
 * Expand an AES key using AES-NI if supported and usable or generic code
 * otherwise.  The expanded key format is compatible between the two cases.  The
 * outputs are @k->rndkeys (required) and @inv_k->inv_rndkeys (optional).
 *
 * We could just always use the generic key expansion code.  AES key expansion
 * is usually less performance-critical than AES en/decryption.  However,
 * there's still *some* value in speed here, as well as in non-key-dependent
 * execution time which AES-NI provides.  So, do use AES-NI to expand AES-128
 * and AES-256 keys.  (Don't bother with AES-192, as it's almost never used.)
 */
static void aes_preparekey_arch(union aes_enckey_arch *k,
				union aes_invkey_arch *inv_k,
				const u8 *in_key, int key_len, int nrounds)
{
	u32 *rndkeys = k->rndkeys;
	u32 *inv_rndkeys = inv_k ? inv_k->inv_rndkeys : NULL;

	if (static_branch_likely(&have_aes) && key_len != AES_KEYSIZE_192 &&
	    irq_fpu_usable()) {
		kernel_fpu_begin();
		if (key_len == AES_KEYSIZE_128)
			aes128_expandkey_aesni(rndkeys, inv_rndkeys, in_key);
		else
			aes256_expandkey_aesni(rndkeys, inv_rndkeys, in_key);
		kernel_fpu_end();
	} else {
		aes_expandkey_generic(rndkeys, inv_rndkeys, in_key, key_len);
	}
}

static void aes_encrypt_arch(const struct aes_enckey *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	if (static_branch_likely(&have_aes) && irq_fpu_usable()) {
		kernel_fpu_begin();
		aes_encrypt_aesni(key->k.rndkeys, key->nrounds, out, in);
		kernel_fpu_end();
	} else {
		aes_encrypt_generic(key->k.rndkeys, key->nrounds, out, in);
	}
}

static void aes_decrypt_arch(const struct aes_key *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	if (static_branch_likely(&have_aes) && irq_fpu_usable()) {
		kernel_fpu_begin();
		aes_decrypt_aesni(key->inv_k.inv_rndkeys, key->nrounds,
				  out, in);
		kernel_fpu_end();
	} else {
		aes_decrypt_generic(key->inv_k.inv_rndkeys, key->nrounds,
				    out, in);
	}
}

#define aes_mod_init_arch aes_mod_init_arch
static void aes_mod_init_arch(void)
{
	if (boot_cpu_has(X86_FEATURE_AES))
		static_branch_enable(&have_aes);
}
