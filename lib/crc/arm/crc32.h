// SPDX-License-Identifier: GPL-2.0-only
/*
 * Accelerated CRC32(C) using ARM CRC, NEON and Crypto Extensions instructions
 *
 * Copyright (C) 2016 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/cpufeature.h>

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_crc32);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_pmull);

#define PMULL_MIN_LEN	64	/* min size of buffer for pmull functions */

asmlinkage u32 crc32_pmull_le(const u8 buf[], u32 len, u32 init_crc);
asmlinkage u32 crc32_armv8_le(u32 init_crc, const u8 buf[], u32 len);

asmlinkage u32 crc32c_pmull_le(const u8 buf[], u32 len, u32 init_crc);
asmlinkage u32 crc32c_armv8_le(u32 init_crc, const u8 buf[], u32 len);

static inline u32 crc32_le_scalar(u32 crc, const u8 *p, size_t len)
{
	if (static_branch_likely(&have_crc32))
		return crc32_armv8_le(crc, p, len);
	return crc32_le_base(crc, p, len);
}

static inline u32 crc32_le_arch(u32 crc, const u8 *p, size_t len)
{
	if (len >= PMULL_MIN_LEN + 15 &&
	    static_branch_likely(&have_pmull) && likely(may_use_simd())) {
		size_t n = -(uintptr_t)p & 15;

		/* align p to 16-byte boundary */
		if (n) {
			crc = crc32_le_scalar(crc, p, n);
			p += n;
			len -= n;
		}
		n = round_down(len, 16);
		kernel_neon_begin();
		crc = crc32_pmull_le(p, n, crc);
		kernel_neon_end();
		p += n;
		len -= n;
	}
	return crc32_le_scalar(crc, p, len);
}

static inline u32 crc32c_scalar(u32 crc, const u8 *p, size_t len)
{
	if (static_branch_likely(&have_crc32))
		return crc32c_armv8_le(crc, p, len);
	return crc32c_base(crc, p, len);
}

static inline u32 crc32c_arch(u32 crc, const u8 *p, size_t len)
{
	if (len >= PMULL_MIN_LEN + 15 &&
	    static_branch_likely(&have_pmull) && likely(may_use_simd())) {
		size_t n = -(uintptr_t)p & 15;

		/* align p to 16-byte boundary */
		if (n) {
			crc = crc32c_scalar(crc, p, n);
			p += n;
			len -= n;
		}
		n = round_down(len, 16);
		kernel_neon_begin();
		crc = crc32c_pmull_le(p, n, crc);
		kernel_neon_end();
		p += n;
		len -= n;
	}
	return crc32c_scalar(crc, p, len);
}

#define crc32_be_arch crc32_be_base /* not implemented on this arch */

#define crc32_mod_init_arch crc32_mod_init_arch
static void crc32_mod_init_arch(void)
{
	if (elf_hwcap2 & HWCAP2_CRC32)
		static_branch_enable(&have_crc32);
	if (elf_hwcap2 & HWCAP2_PMULL)
		static_branch_enable(&have_pmull);
}

static inline u32 crc32_optimizations_arch(void)
{
	if (elf_hwcap2 & (HWCAP2_CRC32 | HWCAP2_PMULL))
		return CRC32_LE_OPTIMIZATION | CRC32C_OPTIMIZATION;
	return 0;
}
