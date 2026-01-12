/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 VRULL GmbH
 * Copyright (C) 2023 SiFive, Inc.
 * Copyright 2024 Google LLC
 */

#include <asm/simd.h>
#include <asm/vector.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_zvkned);

void aes_encrypt_zvkned(const u32 rndkeys[], int key_len,
			u8 out[AES_BLOCK_SIZE], const u8 in[AES_BLOCK_SIZE]);
void aes_decrypt_zvkned(const u32 rndkeys[], int key_len,
			u8 out[AES_BLOCK_SIZE], const u8 in[AES_BLOCK_SIZE]);

static void aes_preparekey_arch(union aes_enckey_arch *k,
				union aes_invkey_arch *inv_k,
				const u8 *in_key, int key_len, int nrounds)
{
	aes_expandkey_generic(k->rndkeys, inv_k ? inv_k->inv_rndkeys : NULL,
			      in_key, key_len);
}

static void aes_encrypt_arch(const struct aes_enckey *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	if (static_branch_likely(&have_zvkned) && likely(may_use_simd())) {
		kernel_vector_begin();
		aes_encrypt_zvkned(key->k.rndkeys, key->len, out, in);
		kernel_vector_end();
	} else {
		aes_encrypt_generic(key->k.rndkeys, key->nrounds, out, in);
	}
}

static void aes_decrypt_arch(const struct aes_key *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	/*
	 * Note that the Zvkned code uses the standard round keys, while the
	 * fallback uses the inverse round keys.  Thus both must be present.
	 */
	if (static_branch_likely(&have_zvkned) && likely(may_use_simd())) {
		kernel_vector_begin();
		aes_decrypt_zvkned(key->k.rndkeys, key->len, out, in);
		kernel_vector_end();
	} else {
		aes_decrypt_generic(key->inv_k.inv_rndkeys, key->nrounds,
				    out, in);
	}
}

#define aes_mod_init_arch aes_mod_init_arch
static void aes_mod_init_arch(void)
{
	if (riscv_isa_extension_available(NULL, ZVKNED) &&
	    riscv_vector_vlen() >= 128)
		static_branch_enable(&have_zvkned);
}
