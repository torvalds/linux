/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AES block cipher using AES-NI instructions
 *
 * Copyright 2026 Google LLC
 */

#include <asm/fpu/api.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_aesni);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_aesni_avx);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_vaes_avx2);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_vaes_avx512);

/* The assembly code assumes the following offsets. */
static_assert(offsetof(struct aes_enckey, len) == 0);
static_assert(offsetof(struct aes_enckey, nrounds) == 4);
static_assert(offsetof(struct aes_enckey, k.rndkeys) == 16);
static_assert(offsetof(struct aes_key, len) == 0);
static_assert(offsetof(struct aes_key, nrounds) == 4);
static_assert(offsetof(struct aes_key, k.rndkeys) == 16);
static_assert(offsetof(struct aes_key, inv_k.inv_rndkeys) == 256);

void aes128_expandkey_aesni(u32 rndkeys[], u32 *inv_rndkeys,
			    const u8 in_key[AES_KEYSIZE_128]);
void aes256_expandkey_aesni(u32 rndkeys[], u32 *inv_rndkeys,
			    const u8 in_key[AES_KEYSIZE_256]);
void aes_encrypt_aesni(u8 dst[AES_BLOCK_SIZE], const u8 src[AES_BLOCK_SIZE],
		       const struct aes_enckey *key);
void aes_decrypt_aesni(u8 dst[AES_BLOCK_SIZE], const u8 src[AES_BLOCK_SIZE],
		       const struct aes_key *key);

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

	if (static_branch_likely(&have_aesni) && key_len != AES_KEYSIZE_192 &&
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
	if (static_branch_likely(&have_aesni) && irq_fpu_usable()) {
		kernel_fpu_begin();
		aes_encrypt_aesni(out, in, key);
		kernel_fpu_end();
	} else {
		aes_encrypt_generic(key->k.rndkeys, key->nrounds, out, in);
	}
}

static void aes_decrypt_arch(const struct aes_key *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	if (static_branch_likely(&have_aesni) && irq_fpu_usable()) {
		kernel_fpu_begin();
		aes_decrypt_aesni(out, in, key);
		kernel_fpu_end();
	} else {
		aes_decrypt_generic(key->inv_k.inv_rndkeys, key->nrounds,
				    out, in);
	}
}

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_ECB)
void aes_ecb_encrypt_aesni(u8 *dst, const u8 *src, long nblocks,
			   const struct aes_enckey *key);
void aes_ecb_decrypt_aesni(u8 *dst, const u8 *src, long nblocks,
			   const struct aes_key *key);

/* len is always a positive multiple of AES_BLOCK_SIZE here. */
#define aes_ecb_encrypt_arch aes_ecb_encrypt_arch
static bool aes_ecb_encrypt_arch(u8 *dst, const u8 *src, size_t len,
				 const struct aes_enckey *key)
{
	if (!static_branch_likely(&have_aesni) || unlikely(!irq_fpu_usable()))
		return false;
	kernel_fpu_begin();
	aes_ecb_encrypt_aesni(dst, src, len / AES_BLOCK_SIZE, key);
	kernel_fpu_end();
	return true;
}

/* len is always a positive multiple of AES_BLOCK_SIZE here. */
#define aes_ecb_decrypt_arch aes_ecb_decrypt_arch
static bool aes_ecb_decrypt_arch(u8 *dst, const u8 *src, size_t len,
				 const struct aes_key *key)
{
	if (!static_branch_likely(&have_aesni) || unlikely(!irq_fpu_usable()))
		return false;
	kernel_fpu_begin();
	aes_ecb_decrypt_aesni(dst, src, len / AES_BLOCK_SIZE, key);
	kernel_fpu_end();
	return true;
}
#endif /* CONFIG_CRYPTO_LIB_AES_ECB */

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_CBC)
void aes_cbc_encrypt_aesni(u8 *dst, const u8 *src, long nblocks,
			   u8 iv[AES_BLOCK_SIZE], const struct aes_enckey *key);
void aes_cbc_decrypt_aesni(u8 *dst, const u8 *src, long nblocks,
			   u8 iv[AES_BLOCK_SIZE], const struct aes_key *key);
void aes_cbc_cts_encrypt_aesni(u8 *dst, const u8 *src, long pn_len,
			       const u8 iv[AES_BLOCK_SIZE],
			       const struct aes_enckey *key);
void aes_cbc_cts_decrypt_aesni(u8 *dst, const u8 *src, long pn_len,
			       const u8 iv[AES_BLOCK_SIZE],
			       const struct aes_key *key);

/* len is always a positive multiple of AES_BLOCK_SIZE here. */
#define aes_cbc_encrypt_arch aes_cbc_encrypt_arch
static bool aes_cbc_encrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 iv[AES_BLOCK_SIZE],
				 const struct aes_enckey *key)
{
	if (!static_branch_likely(&have_aesni) || unlikely(!irq_fpu_usable()))
		return false;
	kernel_fpu_begin();
	aes_cbc_encrypt_aesni(dst, src, len / AES_BLOCK_SIZE, iv, key);
	kernel_fpu_end();
	return true;
}

/* len is always a positive multiple of AES_BLOCK_SIZE here. */
#define aes_cbc_decrypt_arch aes_cbc_decrypt_arch
static bool aes_cbc_decrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 iv[AES_BLOCK_SIZE],
				 const struct aes_key *key)
{
	if (!static_branch_likely(&have_aesni) || unlikely(!irq_fpu_usable()))
		return false;
	kernel_fpu_begin();
	aes_cbc_decrypt_aesni(dst, src, len / AES_BLOCK_SIZE, iv, key);
	kernel_fpu_end();
	return true;
}

/* len can be any value greater than AES_BLOCK_SIZE here. */
#define aes_cbc_cts_encrypt_arch aes_cbc_cts_encrypt_arch
static bool aes_cbc_cts_encrypt_arch(u8 *dst, const u8 *src, size_t len,
				     u8 iv[AES_BLOCK_SIZE],
				     const struct aes_enckey *key)
{
	const size_t cbc_blocks = (len - AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE;
	const size_t pn_len = ((len - 1) % AES_BLOCK_SIZE) + 1;

	if (!static_branch_likely(&have_aesni) || unlikely(!irq_fpu_usable()))
		return false;

	kernel_fpu_begin();
	if (cbc_blocks) {
		aes_cbc_encrypt_aesni(dst, src, cbc_blocks, iv, key);
		dst += cbc_blocks * AES_BLOCK_SIZE;
		src += cbc_blocks * AES_BLOCK_SIZE;
	}
	/* Encrypt the last 17 to 32 bytes. */
	aes_cbc_cts_encrypt_aesni(dst, src, pn_len, iv, key);
	kernel_fpu_end();
	return true;
}

/* len can be any value greater than AES_BLOCK_SIZE here. */
#define aes_cbc_cts_decrypt_arch aes_cbc_cts_decrypt_arch
static bool aes_cbc_cts_decrypt_arch(u8 *dst, const u8 *src, size_t len,
				     u8 iv[AES_BLOCK_SIZE],
				     const struct aes_key *key)
{
	const size_t cbc_blocks = (len - AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE;
	const size_t pn_len = ((len - 1) % AES_BLOCK_SIZE) + 1;

	if (!static_branch_likely(&have_aesni) || unlikely(!irq_fpu_usable()))
		return false;

	kernel_fpu_begin();
	if (cbc_blocks) {
		aes_cbc_decrypt_aesni(dst, src, cbc_blocks, iv, key);
		dst += cbc_blocks * AES_BLOCK_SIZE;
		src += cbc_blocks * AES_BLOCK_SIZE;
	}
	/* Decrypt the last 17 to 32 bytes. */
	aes_cbc_cts_decrypt_aesni(dst, src, pn_len, iv, key);
	kernel_fpu_end();
	return true;
}
#endif /* CONFIG_CRYPTO_LIB_AES_CBC */

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_CTR) && IS_ENABLED(CONFIG_X86_64)
void aes_ctr64_crypt_aesni(u8 *dst, const u8 *src, s64 len, const u64 le_ctr[2],
			   const struct aes_enckey *key);
void aes_ctr64_crypt_aesni_avx(u8 *dst, const u8 *src, s64 len,
			       const u64 le_ctr[2],
			       const struct aes_enckey *key);
void aes_ctr64_crypt_vaes_avx2(u8 *dst, const u8 *src, s64 len,
			       const u64 le_ctr[2],
			       const struct aes_enckey *key);
void aes_ctr64_crypt_vaes_avx512(u8 *dst, const u8 *src, s64 len,
				 const u64 le_ctr[2],
				 const struct aes_enckey *key);
void aes_xctr_crypt_aesni_avx(u8 *dst, const u8 *src, s64 len, u64 ctr,
			      const u8 iv[AES_BLOCK_SIZE],
			      const struct aes_enckey *key);
void aes_xctr_crypt_vaes_avx2(u8 *dst, const u8 *src, s64 len, u64 ctr,
			      const u8 iv[AES_BLOCK_SIZE],
			      const struct aes_enckey *key);
void aes_xctr_crypt_vaes_avx512(u8 *dst, const u8 *src, s64 len, u64 ctr,
				const u8 iv[AES_BLOCK_SIZE],
				const struct aes_enckey *key);

static void aes_ctr64_x86(u8 *dst, const u8 *src, size_t len,
			  const u64 le_ctr[2], const struct aes_enckey *key)
{
	if (static_branch_likely(&have_vaes_avx512))
		aes_ctr64_crypt_vaes_avx512(dst, src, len, le_ctr, key);
	else if (static_branch_likely(&have_vaes_avx2))
		aes_ctr64_crypt_vaes_avx2(dst, src, len, le_ctr, key);
	else if (static_branch_likely(&have_aesni_avx))
		aes_ctr64_crypt_aesni_avx(dst, src, len, le_ctr, key);
	else
		aes_ctr64_crypt_aesni(dst, src, len, le_ctr, key);
}

#define aes_ctr_arch aes_ctr_arch
static bool aes_ctr_arch(u8 *dst, const u8 *src, size_t len,
			 u8 ctr[AES_BLOCK_SIZE], const struct aes_enckey *key)
{
	u64 le_ctr[2];
	u64 ctr64;
	size_t nblocks;
	size_t part1_len;

	if (!static_branch_likely(&have_aesni) || unlikely(!irq_fpu_usable()))
		return false;

	ctr64 = le_ctr[0] = get_unaligned_be64(&ctr[8]);
	le_ctr[1] = get_unaligned_be64(&ctr[0]);

	kernel_fpu_begin();

	nblocks = DIV_ROUND_UP(len, AES_BLOCK_SIZE);
	ctr64 += nblocks;

	if (likely(ctr64 >= nblocks)) {
		/* The low 64 bits of the counter won't overflow. */
		aes_ctr64_x86(dst, src, len, le_ctr, key);
	} else {
		/*
		 * The low 64 bits of the counter will overflow.  The
		 * assembly doesn't handle this case, so split the
		 * operation into two at the point where the overflow
		 * will occur.  After the first part, add the carry bit.
		 */
		part1_len = min(len, (nblocks - ctr64) * AES_BLOCK_SIZE);
		aes_ctr64_x86(dst, src, part1_len, le_ctr, key);
		le_ctr[0] = 0;
		le_ctr[1]++;
		aes_ctr64_x86(dst + part1_len, src + part1_len, len - part1_len,
			      le_ctr, key);
	}
	kernel_fpu_end();
	put_unaligned_be64(ctr64, &ctr[8]);
	put_unaligned_be64(le_ctr[1], &ctr[0]);
	return true;
}

#define aes_xctr_arch aes_xctr_arch
static bool aes_xctr_arch(u8 *dst, const u8 *src, size_t len, u64 ctr,
			  const u8 iv[AES_BLOCK_SIZE],
			  const struct aes_enckey *key)
{
	if (!static_branch_likely(&have_aesni_avx) ||
	    unlikely(!irq_fpu_usable()))
		return false;
	kernel_fpu_begin();
	if (static_branch_likely(&have_vaes_avx512))
		aes_xctr_crypt_vaes_avx512(dst, src, len, ctr, iv, key);
	else if (static_branch_likely(&have_vaes_avx2))
		aes_xctr_crypt_vaes_avx2(dst, src, len, ctr, iv, key);
	else
		aes_xctr_crypt_aesni_avx(dst, src, len, ctr, iv, key);
	kernel_fpu_end();
	return true;
}
#endif /* CONFIG_CRYPTO_LIB_AES_CTR && CONFIG_X86_64 */

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_XTS)
void aes_xts_encrypt_aesni(u8 *dst, const u8 *src, long nblocks,
			   u8 tweak[AES_BLOCK_SIZE], const struct aes_key *key);
void aes_xts_decrypt_aesni(u8 *dst, const u8 *src, long nblocks,
			   u8 tweak[AES_BLOCK_SIZE], const struct aes_key *key);
void aes_xts_encrypt_iv(const struct aes_enckey *tweak_key,
			u8 iv[AES_BLOCK_SIZE]);
void aes_xts_encrypt_aesni_avx(u8 *dst, const u8 *src, long nblocks,
			       u8 tweak[AES_BLOCK_SIZE],
			       const struct aes_key *key);
void aes_xts_decrypt_aesni_avx(u8 *dst, const u8 *src, long nblocks,
			       u8 tweak[AES_BLOCK_SIZE],
			       const struct aes_key *key);
void aes_xts_encrypt_vaes_avx2(u8 *dst, const u8 *src, long nblocks,
			       u8 tweak[AES_BLOCK_SIZE],
			       const struct aes_key *key);
void aes_xts_decrypt_vaes_avx2(u8 *dst, const u8 *src, long nblocks,
			       u8 tweak[AES_BLOCK_SIZE],
			       const struct aes_key *key);
void aes_xts_encrypt_vaes_avx512(u8 *dst, const u8 *src, long nblocks,
				 u8 tweak[AES_BLOCK_SIZE],
				 const struct aes_key *key);
void aes_xts_decrypt_vaes_avx512(u8 *dst, const u8 *src, long nblocks,
				 u8 tweak[AES_BLOCK_SIZE],
				 const struct aes_key *key);

/* len is always a positive multiple of AES_BLOCK_SIZE here. */
static __always_inline bool
aes_xts_crypt_x86(u8 *dst, const u8 *src, size_t len, u8 tweak[AES_BLOCK_SIZE],
		  const struct aes_xts_key *key, bool cont, bool enc)
{
	const long nblocks = len / AES_BLOCK_SIZE;

	if (!static_branch_likely(&have_aesni) || unlikely(!irq_fpu_usable()))
		return false;

	kernel_fpu_begin();
	if (IS_ENABLED(CONFIG_X86_64) &&
	    static_branch_likely(&have_vaes_avx512)) {
		if (!cont)
			aes_xts_encrypt_iv(&key->tweak_key, tweak);
		if (enc)
			aes_xts_encrypt_vaes_avx512(dst, src, nblocks, tweak,
						    &key->main_key);
		else
			aes_xts_decrypt_vaes_avx512(dst, src, nblocks, tweak,
						    &key->main_key);
	} else if (IS_ENABLED(CONFIG_X86_64) &&
		   static_branch_likely(&have_vaes_avx2)) {
		if (!cont)
			aes_xts_encrypt_iv(&key->tweak_key, tweak);
		if (enc)
			aes_xts_encrypt_vaes_avx2(dst, src, nblocks, tweak,
						  &key->main_key);
		else
			aes_xts_decrypt_vaes_avx2(dst, src, nblocks, tweak,
						  &key->main_key);
	} else if (IS_ENABLED(CONFIG_X86_64) &&
		   static_branch_likely(&have_aesni_avx)) {
		if (!cont)
			aes_xts_encrypt_iv(&key->tweak_key, tweak);
		if (enc)
			aes_xts_encrypt_aesni_avx(dst, src, nblocks, tweak,
						  &key->main_key);
		else
			aes_xts_decrypt_aesni_avx(dst, src, nblocks, tweak,
						  &key->main_key);
	} else {
		if (!cont)
			aes_encrypt_aesni(tweak, tweak, &key->tweak_key);
		if (enc)
			aes_xts_encrypt_aesni(dst, src, nblocks, tweak,
					      &key->main_key);
		else
			aes_xts_decrypt_aesni(dst, src, nblocks, tweak,
					      &key->main_key);
	}
	kernel_fpu_end();
	return true;
}

#define aes_xts_encrypt_arch aes_xts_encrypt_arch
static bool aes_xts_encrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 tweak[AES_BLOCK_SIZE],
				 const struct aes_xts_key *key, bool cont)
{
	return aes_xts_crypt_x86(dst, src, len, tweak, key, cont, true);
}

#define aes_xts_decrypt_arch aes_xts_decrypt_arch
static bool aes_xts_decrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 tweak[AES_BLOCK_SIZE],
				 const struct aes_xts_key *key, bool cont)
{
	return aes_xts_crypt_x86(dst, src, len, tweak, key, cont, false);
}
#endif /* CONFIG_CRYPTO_LIB_AES_XTS */

#define aes_mod_init_arch aes_mod_init_arch
static void aes_mod_init_arch(void)
{
	/* Everything below requires AES-NI. */
	if (!boot_cpu_has(X86_FEATURE_AES))
		return;
	static_branch_enable(&have_aesni);

	/* Everything below requires AVX and is also 64-bit only. */
	if (!boot_cpu_has(X86_FEATURE_AVX) || !IS_ENABLED(CONFIG_X86_64))
		return;
	static_branch_enable(&have_aesni_avx);

	/*
	 * Everything below requires VAES, and also sometimes AVX2, VPCLMULQDQ,
	 * and PCLMULQDQ.  Use a single static key for all of them, since in
	 * practice every CPU with VAES also has the others.
	 */
	if (!boot_cpu_has(X86_FEATURE_VAES) ||
	    !boot_cpu_has(X86_FEATURE_AVX2) ||
	    !boot_cpu_has(X86_FEATURE_VPCLMULQDQ) ||
	    !boot_cpu_has(X86_FEATURE_PCLMULQDQ))
		return;
	static_branch_enable(&have_vaes_avx2);

	if (!boot_cpu_has(X86_FEATURE_AVX512BW) ||
	    !boot_cpu_has(X86_FEATURE_AVX512VL) ||
	    !boot_cpu_has(X86_FEATURE_BMI2) ||
	    boot_cpu_has(X86_FEATURE_PREFER_YMM))
		return;
	static_branch_enable(&have_vaes_avx512);
}
