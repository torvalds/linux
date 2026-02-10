/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AES block cipher, optimized for ARM
 *
 * Copyright (C) 2017 Linaro Ltd.
 * Copyright 2026 Google LLC
 */

asmlinkage void __aes_arm_encrypt(const u32 rk[], int rounds,
				  const u8 in[AES_BLOCK_SIZE],
				  u8 out[AES_BLOCK_SIZE]);
asmlinkage void __aes_arm_decrypt(const u32 inv_rk[], int rounds,
				  const u8 in[AES_BLOCK_SIZE],
				  u8 out[AES_BLOCK_SIZE]);

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
	if (!IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) &&
	    !IS_ALIGNED((uintptr_t)out | (uintptr_t)in, 4)) {
		u8 bounce_buf[AES_BLOCK_SIZE] __aligned(4);

		memcpy(bounce_buf, in, AES_BLOCK_SIZE);
		__aes_arm_encrypt(key->k.rndkeys, key->nrounds, bounce_buf,
				  bounce_buf);
		memcpy(out, bounce_buf, AES_BLOCK_SIZE);
		return;
	}
	__aes_arm_encrypt(key->k.rndkeys, key->nrounds, in, out);
}

static void aes_decrypt_arch(const struct aes_key *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	if (!IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) &&
	    !IS_ALIGNED((uintptr_t)out | (uintptr_t)in, 4)) {
		u8 bounce_buf[AES_BLOCK_SIZE] __aligned(4);

		memcpy(bounce_buf, in, AES_BLOCK_SIZE);
		__aes_arm_decrypt(key->inv_k.inv_rndkeys, key->nrounds,
				  bounce_buf, bounce_buf);
		memcpy(out, bounce_buf, AES_BLOCK_SIZE);
		return;
	}
	__aes_arm_decrypt(key->inv_k.inv_rndkeys, key->nrounds, in, out);
}
