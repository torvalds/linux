/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 * Copyright 2026 Google LLC
 */
#include <asm/simd.h>
#include <asm/switch_to.h>
#include <linux/cpufeature.h>
#include <linux/jump_label.h>
#include <linux/preempt.h>
#include <linux/uaccess.h>

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
