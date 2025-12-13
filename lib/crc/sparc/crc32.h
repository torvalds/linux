// SPDX-License-Identifier: GPL-2.0-only
/* CRC32c (Castagnoli), sparc64 crc32c opcode accelerated
 *
 * This is based largely upon arch/x86/crypto/crc32c-intel.c
 *
 * Copyright (C) 2008 Intel Corporation
 * Authors: Austin Zhang <austin_zhang@linux.intel.com>
 *          Kent Liu <kent.liu@intel.com>
 */

#include <asm/pstate.h>
#include <asm/elf.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_crc32c_opcode);

#define crc32_le_arch crc32_le_base /* not implemented on this arch */
#define crc32_be_arch crc32_be_base /* not implemented on this arch */

void crc32c_sparc64(u32 *crcp, const u64 *data, size_t len);

static inline u32 crc32c_arch(u32 crc, const u8 *data, size_t len)
{
	size_t n = -(uintptr_t)data & 7;

	if (!static_branch_likely(&have_crc32c_opcode))
		return crc32c_base(crc, data, len);

	if (n) {
		/* Data isn't 8-byte aligned.  Align it. */
		n = min(n, len);
		crc = crc32c_base(crc, data, n);
		data += n;
		len -= n;
	}
	n = len & ~7U;
	if (n) {
		crc32c_sparc64(&crc, (const u64 *)data, n);
		data += n;
		len -= n;
	}
	if (len)
		crc = crc32c_base(crc, data, len);
	return crc;
}

#define crc32_mod_init_arch crc32_mod_init_arch
static void crc32_mod_init_arch(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_CRC32C))
		return;

	static_branch_enable(&have_crc32c_opcode);
	pr_info("Using sparc64 crc32c opcode optimized CRC32C implementation\n");
}

static inline u32 crc32_optimizations_arch(void)
{
	if (static_key_enabled(&have_crc32c_opcode))
		return CRC32C_OPTIMIZATION;
	return 0;
}
