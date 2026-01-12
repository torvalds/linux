/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AES optimized using the CP Assist for Cryptographic Functions (CPACF)
 *
 * Copyright 2026 Google LLC
 */
#include <asm/cpacf.h>
#include <linux/cpufeature.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_cpacf_aes128);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_cpacf_aes192);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_cpacf_aes256);

/*
 * When the CPU supports CPACF AES for the requested key length, we need only
 * save a copy of the raw AES key, as that's what the CPACF instructions need.
 *
 * When unsupported, fall back to the generic key expansion and en/decryption.
 */
static void aes_preparekey_arch(union aes_enckey_arch *k,
				union aes_invkey_arch *inv_k,
				const u8 *in_key, int key_len, int nrounds)
{
	if (key_len == AES_KEYSIZE_128) {
		if (static_branch_likely(&have_cpacf_aes128)) {
			memcpy(k->raw_key, in_key, AES_KEYSIZE_128);
			return;
		}
	} else if (key_len == AES_KEYSIZE_192) {
		if (static_branch_likely(&have_cpacf_aes192)) {
			memcpy(k->raw_key, in_key, AES_KEYSIZE_192);
			return;
		}
	} else {
		if (static_branch_likely(&have_cpacf_aes256)) {
			memcpy(k->raw_key, in_key, AES_KEYSIZE_256);
			return;
		}
	}
	aes_expandkey_generic(k->rndkeys, inv_k ? inv_k->inv_rndkeys : NULL,
			      in_key, key_len);
}

static inline bool aes_crypt_s390(const struct aes_enckey *key,
				  u8 out[AES_BLOCK_SIZE],
				  const u8 in[AES_BLOCK_SIZE], int decrypt)
{
	if (key->len == AES_KEYSIZE_128) {
		if (static_branch_likely(&have_cpacf_aes128)) {
			cpacf_km(CPACF_KM_AES_128 | decrypt,
				 (void *)key->k.raw_key, out, in,
				 AES_BLOCK_SIZE);
			return true;
		}
	} else if (key->len == AES_KEYSIZE_192) {
		if (static_branch_likely(&have_cpacf_aes192)) {
			cpacf_km(CPACF_KM_AES_192 | decrypt,
				 (void *)key->k.raw_key, out, in,
				 AES_BLOCK_SIZE);
			return true;
		}
	} else {
		if (static_branch_likely(&have_cpacf_aes256)) {
			cpacf_km(CPACF_KM_AES_256 | decrypt,
				 (void *)key->k.raw_key, out, in,
				 AES_BLOCK_SIZE);
			return true;
		}
	}
	return false;
}

static void aes_encrypt_arch(const struct aes_enckey *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	if (likely(aes_crypt_s390(key, out, in, 0)))
		return;
	aes_encrypt_generic(key->k.rndkeys, key->nrounds, out, in);
}

static void aes_decrypt_arch(const struct aes_key *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	if (likely(aes_crypt_s390((const struct aes_enckey *)key, out, in,
				  CPACF_DECRYPT)))
		return;
	aes_decrypt_generic(key->inv_k.inv_rndkeys, key->nrounds, out, in);
}

#define aes_mod_init_arch aes_mod_init_arch
static void aes_mod_init_arch(void)
{
	if (cpu_have_feature(S390_CPU_FEATURE_MSA)) {
		cpacf_mask_t km_functions;

		cpacf_query(CPACF_KM, &km_functions);
		if (cpacf_test_func(&km_functions, CPACF_KM_AES_128))
			static_branch_enable(&have_cpacf_aes128);
		if (cpacf_test_func(&km_functions, CPACF_KM_AES_192))
			static_branch_enable(&have_cpacf_aes192);
		if (cpacf_test_func(&km_functions, CPACF_KM_AES_256))
			static_branch_enable(&have_cpacf_aes256);
	}
}
