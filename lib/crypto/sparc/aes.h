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
