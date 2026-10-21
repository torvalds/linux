/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AES accelerated using the sparc64 aes opcodes
 *
 * Copyright (C) 2008, Intel Corp.
 * Copyright (c) 2010, Intel Corporation.
 * Copyright 2026 Google LLC
 */

#include <asm/fpumacro.h>
#include <asm/opcodes.h>
#include <asm/pstate.h>
#include <asm/elf.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_aes_opcodes);

EXPORT_SYMBOL_GPL(aes_sparc64_key_expand);
EXPORT_SYMBOL_GPL(aes_sparc64_load_encrypt_keys_128);
EXPORT_SYMBOL_GPL(aes_sparc64_load_encrypt_keys_192);
EXPORT_SYMBOL_GPL(aes_sparc64_load_encrypt_keys_256);
EXPORT_SYMBOL_GPL(aes_sparc64_load_decrypt_keys_128);
EXPORT_SYMBOL_GPL(aes_sparc64_load_decrypt_keys_192);
EXPORT_SYMBOL_GPL(aes_sparc64_load_decrypt_keys_256);
EXPORT_SYMBOL_GPL(aes_sparc64_ecb_encrypt_128);
EXPORT_SYMBOL_GPL(aes_sparc64_ecb_encrypt_192);
EXPORT_SYMBOL_GPL(aes_sparc64_ecb_encrypt_256);
EXPORT_SYMBOL_GPL(aes_sparc64_ecb_decrypt_128);
EXPORT_SYMBOL_GPL(aes_sparc64_ecb_decrypt_192);
EXPORT_SYMBOL_GPL(aes_sparc64_ecb_decrypt_256);
EXPORT_SYMBOL_GPL(aes_sparc64_cbc_encrypt_128);
EXPORT_SYMBOL_GPL(aes_sparc64_cbc_encrypt_192);
EXPORT_SYMBOL_GPL(aes_sparc64_cbc_encrypt_256);
EXPORT_SYMBOL_GPL(aes_sparc64_cbc_decrypt_128);
EXPORT_SYMBOL_GPL(aes_sparc64_cbc_decrypt_192);
EXPORT_SYMBOL_GPL(aes_sparc64_cbc_decrypt_256);
EXPORT_SYMBOL_GPL(aes_sparc64_ctr_crypt_128);
EXPORT_SYMBOL_GPL(aes_sparc64_ctr_crypt_192);
EXPORT_SYMBOL_GPL(aes_sparc64_ctr_crypt_256);

void aes_sparc64_encrypt_128(const u64 *key, const u32 *input, u32 *output);
void aes_sparc64_encrypt_192(const u64 *key, const u32 *input, u32 *output);
void aes_sparc64_encrypt_256(const u64 *key, const u32 *input, u32 *output);
void aes_sparc64_decrypt_128(const u64 *key, const u32 *input, u32 *output);
void aes_sparc64_decrypt_192(const u64 *key, const u32 *input, u32 *output);
void aes_sparc64_decrypt_256(const u64 *key, const u32 *input, u32 *output);

static void aes_preparekey_arch(union aes_enckey_arch *k,
				union aes_invkey_arch *inv_k,
				const u8 *in_key, int key_len, int nrounds)
{
	if (static_branch_likely(&have_aes_opcodes)) {
		u32 aligned_key[AES_MAX_KEY_SIZE / 4];

		if (IS_ALIGNED((uintptr_t)in_key, 4)) {
			aes_sparc64_key_expand((const u32 *)in_key,
					       k->sparc_rndkeys, key_len);
		} else {
			memcpy(aligned_key, in_key, key_len);
			aes_sparc64_key_expand(aligned_key,
					       k->sparc_rndkeys, key_len);
			memzero_explicit(aligned_key, key_len);
		}
		/*
		 * Note that nothing needs to be written to inv_k (if it's
		 * non-NULL) here, since the SPARC64 assembly code uses
		 * k->sparc_rndkeys for both encryption and decryption.
		 */
	} else {
		aes_expandkey_generic(k->rndkeys,
				      inv_k ? inv_k->inv_rndkeys : NULL,
				      in_key, key_len);
	}
}

static void aes_sparc64_encrypt(const struct aes_enckey *key,
				const u32 *input, u32 *output)
{
	if (key->len == AES_KEYSIZE_128)
		aes_sparc64_encrypt_128(key->k.sparc_rndkeys, input, output);
	else if (key->len == AES_KEYSIZE_192)
		aes_sparc64_encrypt_192(key->k.sparc_rndkeys, input, output);
	else
		aes_sparc64_encrypt_256(key->k.sparc_rndkeys, input, output);
}

static void aes_encrypt_arch(const struct aes_enckey *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	u32 bounce_buf[AES_BLOCK_SIZE / 4];

	if (static_branch_likely(&have_aes_opcodes)) {
		if (IS_ALIGNED((uintptr_t)in | (uintptr_t)out, 4)) {
			aes_sparc64_encrypt(key, (const u32 *)in, (u32 *)out);
		} else {
			memcpy(bounce_buf, in, AES_BLOCK_SIZE);
			aes_sparc64_encrypt(key, bounce_buf, bounce_buf);
			memcpy(out, bounce_buf, AES_BLOCK_SIZE);
		}
	} else {
		aes_encrypt_generic(key->k.rndkeys, key->nrounds, out, in);
	}
}

static void aes_sparc64_decrypt(const struct aes_key *key,
				const u32 *input, u32 *output)
{
	if (key->len == AES_KEYSIZE_128)
		aes_sparc64_decrypt_128(key->k.sparc_rndkeys, input, output);
	else if (key->len == AES_KEYSIZE_192)
		aes_sparc64_decrypt_192(key->k.sparc_rndkeys, input, output);
	else
		aes_sparc64_decrypt_256(key->k.sparc_rndkeys, input, output);
}

static void aes_decrypt_arch(const struct aes_key *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	u32 bounce_buf[AES_BLOCK_SIZE / 4];

	if (static_branch_likely(&have_aes_opcodes)) {
		if (IS_ALIGNED((uintptr_t)in | (uintptr_t)out, 4)) {
			aes_sparc64_decrypt(key, (const u32 *)in, (u32 *)out);
		} else {
			memcpy(bounce_buf, in, AES_BLOCK_SIZE);
			aes_sparc64_decrypt(key, bounce_buf, bounce_buf);
			memcpy(out, bounce_buf, AES_BLOCK_SIZE);
		}
	} else {
		aes_decrypt_generic(key->inv_k.inv_rndkeys, key->nrounds,
				    out, in);
	}
}

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_XTS)
void aes_sparc64_xts_encrypt_128(const u64 *key, const u64 *input, u64 *output,
				 size_t len, u64 tweak[2]);
void aes_sparc64_xts_encrypt_256(const u64 *key, const u64 *input, u64 *output,
				 size_t len, u64 tweak[2]);
void aes_sparc64_xts_decrypt_128(const u64 *key_end, const u64 *input,
				 u64 *output, size_t len, u64 tweak[2]);
void aes_sparc64_xts_decrypt_256(const u64 *key_end, const u64 *input,
				 u64 *output, size_t len, u64 tweak[2]);

/* len is always a positive multiple of AES_BLOCK_SIZE here. */
static __always_inline bool
aes_xts_crypt_sparc64(u8 *dst, const u8 *src, size_t len,
		      u8 tweak[AES_BLOCK_SIZE],
		      const struct aes_xts_key *key, bool cont, bool enc)
{
	const struct aes_key *k = &key->main_key;
	const u64 *rk = k->k.sparc_rndkeys;
	const u64 *rk_end = rk + 2 * (k->nrounds + 1);
	const u64 *in = (const u64 *)src;
	u64 *out = (u64 *)dst;
	u64 t[2];

	/* The assembly code has no AES-192 and needs 8-byte aligned data. */
	if (!static_branch_likely(&have_aes_opcodes) ||
	    k->len == AES_KEYSIZE_192 ||
	    !IS_ALIGNED((uintptr_t)dst | (uintptr_t)src, 8))
		return false;

	if (cont)
		memcpy(t, tweak, sizeof(t));
	else
		aes_encrypt_arch(&key->tweak_key, (u8 *)t, tweak);

	if (k->len == AES_KEYSIZE_128) {
		if (enc) {
			aes_sparc64_load_encrypt_keys_128(rk);
			aes_sparc64_xts_encrypt_128(rk, in, out, len, t);
		} else {
			aes_sparc64_load_decrypt_keys_128(rk);
			aes_sparc64_xts_decrypt_128(rk_end, in, out, len, t);
		}
	} else {
		if (enc) {
			aes_sparc64_load_encrypt_keys_256(rk);
			aes_sparc64_xts_encrypt_256(rk, in, out, len, t);
		} else {
			aes_sparc64_load_decrypt_keys_256(rk);
			aes_sparc64_xts_decrypt_256(rk_end, in, out, len, t);
		}
	}
	fprs_write(0);

	memcpy(tweak, t, sizeof(t));
	memzero_explicit(t, sizeof(t));
	return true;
}

#define aes_xts_encrypt_arch aes_xts_encrypt_arch
static bool aes_xts_encrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 tweak[AES_BLOCK_SIZE],
				 const struct aes_xts_key *key, bool cont)
{
	return aes_xts_crypt_sparc64(dst, src, len, tweak, key, cont, true);
}

#define aes_xts_decrypt_arch aes_xts_decrypt_arch
static bool aes_xts_decrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 tweak[AES_BLOCK_SIZE],
				 const struct aes_xts_key *key, bool cont)
{
	return aes_xts_crypt_sparc64(dst, src, len, tweak, key, cont, false);
}
#endif /* CONFIG_CRYPTO_LIB_AES_XTS */

#define aes_mod_init_arch aes_mod_init_arch
static void aes_mod_init_arch(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_AES))
		return;

	static_branch_enable(&have_aes_opcodes);
}
