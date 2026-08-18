/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * CRC64 using ARM PMULL instructions
 */

#include <asm/simd.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_pmull);

u64 crc64_nvme_neon(u64 crc, const u8 *p, size_t len);

#define crc64_be_arch crc64_be_generic

static inline u64 crc64_nvme_arch(u64 crc, const u8 *p, size_t len)
{
	if (len >= 128 && static_branch_likely(&have_pmull) &&
	    likely(may_use_simd())) {
		do {
			size_t chunk = min_t(size_t, len & ~15, SZ_4K);

			scoped_ksimd()
				crc = crc64_nvme_neon(crc, p, chunk);

			p += chunk;
			len -= chunk;
		} while (len >= 128);
	}
	return crc64_nvme_generic(crc, p, len);
}

#define crc64_mod_init_arch crc64_mod_init_arch
static void crc64_mod_init_arch(void)
{
	if (elf_hwcap2 & HWCAP2_PMULL)
		static_branch_enable(&have_pmull);
}
