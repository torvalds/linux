/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AES using the RISC-V vector crypto extensions
 *
 * Copyright (C) 2023 VRULL GmbH
 * Copyright (C) 2023 SiFive, Inc.
 * Copyright 2024 Google LLC
 */

#include <asm/simd.h>
#include <asm/vector.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_zvkned);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_zvkned_zvkb);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_xts);

/* The assembly code assumes the following offsets. */
static_assert(offsetof(struct aes_enckey, len) == 0);
static_assert(offsetof(struct aes_enckey, k.rndkeys) == 16);
static_assert(offsetof(struct aes_key, len) == 0);
static_assert(offsetof(struct aes_key, k.rndkeys) == 16);

void aes_encrypt_zvkned(const struct aes_enckey *key, u8 out[AES_BLOCK_SIZE],
			const u8 in[AES_BLOCK_SIZE]);
void aes_decrypt_zvkned(const struct aes_key *key, u8 out[AES_BLOCK_SIZE],
			const u8 in[AES_BLOCK_SIZE]);

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
		aes_encrypt_zvkned(key, out, in);
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
		aes_decrypt_zvkned(key, out, in);
		kernel_vector_end();
	} else {
		aes_decrypt_generic(key->inv_k.inv_rndkeys, key->nrounds,
				    out, in);
	}
}

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_ECB)
void aes_ecb_encrypt_zvkned(u8 *dst, const u8 *src, size_t len,
			    const struct aes_enckey *key);
void aes_ecb_decrypt_zvkned(u8 *dst, const u8 *src, size_t len,
			    const struct aes_key *key);

/* len is always a positive multiple of AES_BLOCK_SIZE here. */
#define aes_ecb_encrypt_arch aes_ecb_encrypt_arch
static bool aes_ecb_encrypt_arch(u8 *dst, const u8 *src, size_t len,
				 const struct aes_enckey *key)
{
	if (!static_branch_likely(&have_zvkned) || unlikely(!may_use_simd()))
		return false;
	kernel_vector_begin();
	aes_ecb_encrypt_zvkned(dst, src, len, key);
	kernel_vector_end();
	return true;
}

/* len is always a positive multiple of AES_BLOCK_SIZE here. */
#define aes_ecb_decrypt_arch aes_ecb_decrypt_arch
static bool aes_ecb_decrypt_arch(u8 *dst, const u8 *src, size_t len,
				 const struct aes_key *key)
{
	if (!static_branch_likely(&have_zvkned) || unlikely(!may_use_simd()))
		return false;
	kernel_vector_begin();
	aes_ecb_decrypt_zvkned(dst, src, len, key);
	kernel_vector_end();
	return true;
}
#endif /* CONFIG_CRYPTO_LIB_AES_ECB */

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_CBC)
void aes_cbc_encrypt_zvkned(u8 *dst, const u8 *src, size_t len,
			    u8 iv[AES_BLOCK_SIZE], const struct aes_enckey *key);
void aes_cbc_decrypt_zvkned(u8 *dst, const u8 *src, size_t len,
			    u8 iv[AES_BLOCK_SIZE], const struct aes_key *key);
void aes_cbc_cts_crypt_zvkned(u8 *dst, const u8 *src, size_t len,
			      const u8 iv[AES_BLOCK_SIZE],
			      aes_encrypt_arg key, bool enc);

/* len is always a positive multiple of AES_BLOCK_SIZE here. */
#define aes_cbc_encrypt_arch aes_cbc_encrypt_arch
static bool aes_cbc_encrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 iv[AES_BLOCK_SIZE],
				 const struct aes_enckey *key)
{
	if (!static_branch_likely(&have_zvkned) || unlikely(!may_use_simd()))
		return false;
	kernel_vector_begin();
	aes_cbc_encrypt_zvkned(dst, src, len, iv, key);
	kernel_vector_end();
	return true;
}

/* len is always a positive multiple of AES_BLOCK_SIZE here. */
#define aes_cbc_decrypt_arch aes_cbc_decrypt_arch
static bool aes_cbc_decrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 iv[AES_BLOCK_SIZE],
				 const struct aes_key *key)
{
	if (!static_branch_likely(&have_zvkned) || unlikely(!may_use_simd()))
		return false;
	kernel_vector_begin();
	aes_cbc_decrypt_zvkned(dst, src, len, iv, key);
	kernel_vector_end();
	return true;
}

/* len can be any value greater than AES_BLOCK_SIZE here. */
#define aes_cbc_cts_encrypt_arch aes_cbc_cts_encrypt_arch
static bool aes_cbc_cts_encrypt_arch(u8 *dst, const u8 *src, size_t len,
				     u8 iv[AES_BLOCK_SIZE],
				     const struct aes_enckey *key)
{
	if (!static_branch_likely(&have_zvkned) || unlikely(!may_use_simd()))
		return false;

	kernel_vector_begin();
	aes_cbc_cts_crypt_zvkned(dst, src, len, iv, key, true);
	kernel_vector_end();
	return true;
}

/* len can be any value greater than AES_BLOCK_SIZE here. */
#define aes_cbc_cts_decrypt_arch aes_cbc_cts_decrypt_arch
static bool aes_cbc_cts_decrypt_arch(u8 *dst, const u8 *src, size_t len,
				     u8 iv[AES_BLOCK_SIZE],
				     const struct aes_key *key)
{
	if (!static_branch_likely(&have_zvkned) || unlikely(!may_use_simd()))
		return false;

	kernel_vector_begin();
	aes_cbc_cts_crypt_zvkned(dst, src, len, iv, key, false);
	kernel_vector_end();
	return true;
}
#endif /* CONFIG_CRYPTO_LIB_AES_CBC */

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_CTR)
void aes_ctr32_crypt_zvkned_zvkb(u8 *dst, const u8 *src, size_t len, u8 iv[16],
				 const struct aes_enckey *key);

static void aes_ctr_riscv(u8 *dst, const u8 *src, size_t len,
			  u8 ctr[AES_BLOCK_SIZE], const struct aes_enckey *key)
{
	u32 ctr32 = get_unaligned_be32(&ctr[12]);
	u32 nblocks;
	size_t part1_len;

	/* This assumes len <= U32_MAX * AES_BLOCK_SIZE. */
	nblocks = DIV_ROUND_UP(len, AES_BLOCK_SIZE);
	ctr32 += nblocks;

	if (likely(ctr32 >= nblocks)) {
		/* The low 32 bits of the counter won't overflow. */
		aes_ctr32_crypt_zvkned_zvkb(dst, src, len, ctr, key);
	} else {
		/*
		 * The low 32 bits of the counter will overflow.  The
		 * assembly doesn't handle this case, so split the
		 * operation into two at the point where the overflow
		 * will occur.  After the first part, add the carry bit.
		 */
		part1_len =
			min(len, (size_t)(nblocks - ctr32) * AES_BLOCK_SIZE);
		aes_ctr32_crypt_zvkned_zvkb(dst, src, part1_len, ctr, key);
		for (int i = AES_BLOCK_SIZE - 5; i >= 0; i--) {
			if (++ctr[i] != 0)
				break;
		}
		if (part1_len < len)
			aes_ctr32_crypt_zvkned_zvkb(dst + part1_len,
						    src + part1_len,
						    len - part1_len, ctr, key);
	}
}

#define aes_ctr_arch aes_ctr_arch
static bool aes_ctr_arch(u8 *dst, const u8 *src, size_t len,
			 u8 ctr[AES_BLOCK_SIZE], const struct aes_enckey *key)
{
	if (!static_branch_likely(&have_zvkned_zvkb) ||
	    unlikely(!may_use_simd()))
		return false;
	kernel_vector_begin();
	while (len) {
		/*
		 * Split super long messages into chunks that each need at most
		 * 1 carry bit out of the low 32-bit word of the counter.  Up to
		 * U32_MAX * AES_BLOCK_SIZE bytes could be allowed per chunk,
		 * but use a lower limit to guard against other errors.
		 */
		size_t n = min(len, round_down(S32_MAX, 32 * AES_BLOCK_SIZE));

		aes_ctr_riscv(dst, src, n, ctr, key);
		dst += n;
		src += n;
		len -= n;
	}
	kernel_vector_end();
	return true;
}
#endif /* CONFIG_CRYPTO_LIB_AES_CTR */

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_XTS)
void aes_xts_encrypt_zvkned_zvbb_zvkg(u8 *dst, const u8 *src, size_t len,
				      u8 tweak[AES_BLOCK_SIZE],
				      const struct aes_key *key);
void aes_xts_decrypt_zvkned_zvbb_zvkg(u8 *dst, const u8 *src, size_t len,
				      u8 tweak[AES_BLOCK_SIZE],
				      const struct aes_key *key);

/* len is always a positive multiple of AES_BLOCK_SIZE here. */
static __always_inline bool
aes_xts_crypt_riscv(u8 *dst, const u8 *src, size_t len,
		    u8 tweak[AES_BLOCK_SIZE],
		    const struct aes_xts_key *key, bool cont, bool enc)
{
	if (!static_branch_likely(&have_xts) || unlikely(!may_use_simd()))
		return false;
	kernel_vector_begin();
	if (!cont)
		aes_encrypt_zvkned(&key->tweak_key, tweak, tweak);
	if (enc)
		aes_xts_encrypt_zvkned_zvbb_zvkg(dst, src, len, tweak,
						 &key->main_key);
	else
		aes_xts_decrypt_zvkned_zvbb_zvkg(dst, src, len, tweak,
						 &key->main_key);
	kernel_vector_end();
	return true;
}

#define aes_xts_encrypt_arch aes_xts_encrypt_arch
static bool aes_xts_encrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 tweak[AES_BLOCK_SIZE],
				 const struct aes_xts_key *key, bool cont)
{
	return aes_xts_crypt_riscv(dst, src, len, tweak, key, cont, true);
}

#define aes_xts_decrypt_arch aes_xts_decrypt_arch
static bool aes_xts_decrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 tweak[AES_BLOCK_SIZE],
				 const struct aes_xts_key *key, bool cont)
{
	return aes_xts_crypt_riscv(dst, src, len, tweak, key, cont, false);
}
#endif /* CONFIG_CRYPTO_LIB_AES_XTS */

#define aes_mod_init_arch aes_mod_init_arch
static void aes_mod_init_arch(void)
{
	if (riscv_isa_extension_available(NULL, ZVKNED) &&
	    riscv_vector_vlen() >= 128) {
		static_branch_enable(&have_zvkned);
		if (riscv_isa_extension_available(NULL, ZVKB))
			static_branch_enable(&have_zvkned_zvkb);
		if (riscv_isa_extension_available(NULL, ZVBB) &&
		    riscv_isa_extension_available(NULL, ZVKG) &&
		    riscv_vector_vlen() < 2048 /* Implementation limitation */)
			static_branch_enable(&have_xts);
	}
}
