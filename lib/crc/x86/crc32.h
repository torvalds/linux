// SPDX-License-Identifier: GPL-2.0-only
/*
 * x86-optimized CRC32 functions
 *
 * Copyright (C) 2008 Intel Corporation
 * Copyright 2012 Xyratex Technology Limited
 * Copyright 2024 Google LLC
 */

#include "crc-pclmul-template.h"

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_crc32);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_pclmulqdq);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_vpclmul_avx512);

DECLARE_CRC_PCLMUL_FUNCS(crc32_lsb, u32);

static inline u32 crc32_le_arch(u32 crc, const u8 *p, size_t len)
{
	CRC_PCLMUL(crc, p, len, crc32_lsb, crc32_lsb_0xedb88320_consts,
		   have_pclmulqdq);
	return crc32_le_base(crc, p, len);
}

#ifdef CONFIG_X86_64
#define CRC32_INST "crc32q %1, %q0"
#else
#define CRC32_INST "crc32l %1, %0"
#endif

/*
 * Use carryless multiply version of crc32c when buffer size is >= 512 to
 * account for FPU state save/restore overhead.
 */
#define CRC32C_PCLMUL_BREAKEVEN	512

asmlinkage u32 crc32c_x86_3way(u32 crc, const u8 *buffer, size_t len);

static inline u32 crc32c_arch(u32 crc, const u8 *p, size_t len)
{
	size_t num_longs;

	if (!static_branch_likely(&have_crc32))
		return crc32c_base(crc, p, len);

	if (IS_ENABLED(CONFIG_X86_64) && len >= CRC32C_PCLMUL_BREAKEVEN &&
	    static_branch_likely(&have_pclmulqdq) && likely(irq_fpu_usable())) {
		/*
		 * Long length, the vector registers are usable, and the CPU is
		 * 64-bit and supports both CRC32 and PCLMULQDQ instructions.
		 * It is worthwhile to divide the data into multiple streams,
		 * CRC them independently, and combine them using PCLMULQDQ.
		 * crc32c_x86_3way() does this using 3 streams, which is the
		 * most that x86_64 CPUs have traditionally been capable of.
		 *
		 * However, due to improved VPCLMULQDQ performance on newer
		 * CPUs, use crc32_lsb_vpclmul_avx512() instead of
		 * crc32c_x86_3way() when the CPU supports VPCLMULQDQ and has a
		 * "good" implementation of AVX-512.
		 *
		 * Future work: the optimal strategy on Zen 3--5 is actually to
		 * use both crc32q and VPCLMULQDQ in parallel.  Unfortunately,
		 * different numbers of streams and vector lengths are optimal
		 * on each CPU microarchitecture, making it challenging to take
		 * advantage of this.  (Zen 5 even supports 7 parallel crc32q, a
		 * major upgrade.)  For now, just choose between
		 * crc32c_x86_3way() and crc32_lsb_vpclmul_avx512().  The latter
		 * is needed anyway for crc32_le(), so we just reuse it here.
		 */
		kernel_fpu_begin();
		if (static_branch_likely(&have_vpclmul_avx512))
			crc = crc32_lsb_vpclmul_avx512(crc, p, len,
				       crc32_lsb_0x82f63b78_consts.fold_across_128_bits_consts);
		else
			crc = crc32c_x86_3way(crc, p, len);
		kernel_fpu_end();
		return crc;
	}

	/*
	 * Short length, XMM registers unusable, or the CPU is 32-bit; but the
	 * CPU supports CRC32 instructions.  Just issue a single stream of CRC32
	 * instructions inline.  While this doesn't use the CPU's CRC32
	 * throughput very well, it avoids the need to combine streams.  Stream
	 * combination would be inefficient here.
	 */

	for (num_longs = len / sizeof(unsigned long);
	     num_longs != 0; num_longs--, p += sizeof(unsigned long))
		asm(CRC32_INST : "+r" (crc) : ASM_INPUT_RM (*(unsigned long *)p));

	if (sizeof(unsigned long) > 4 && (len & 4)) {
		asm("crc32l %1, %0" : "+r" (crc) : ASM_INPUT_RM (*(u32 *)p));
		p += 4;
	}
	if (len & 2) {
		asm("crc32w %1, %0" : "+r" (crc) : ASM_INPUT_RM (*(u16 *)p));
		p += 2;
	}
	if (len & 1)
		asm("crc32b %1, %0" : "+r" (crc) : ASM_INPUT_RM (*p));

	return crc;
}

#define crc32_be_arch crc32_be_base /* not implemented on this arch */

#define crc32_mod_init_arch crc32_mod_init_arch
static void crc32_mod_init_arch(void)
{
	if (boot_cpu_has(X86_FEATURE_XMM4_2))
		static_branch_enable(&have_crc32);
	if (boot_cpu_has(X86_FEATURE_PCLMULQDQ)) {
		static_branch_enable(&have_pclmulqdq);
		if (have_vpclmul()) {
			if (have_avx512()) {
				static_call_update(crc32_lsb_pclmul,
						   crc32_lsb_vpclmul_avx512);
				static_branch_enable(&have_vpclmul_avx512);
			} else {
				static_call_update(crc32_lsb_pclmul,
						   crc32_lsb_vpclmul_avx2);
			}
		}
	}
}

static inline u32 crc32_optimizations_arch(void)
{
	u32 optimizations = 0;

	if (static_key_enabled(&have_crc32))
		optimizations |= CRC32C_OPTIMIZATION;
	if (static_key_enabled(&have_pclmulqdq))
		optimizations |= CRC32_LE_OPTIMIZATION;
	return optimizations;
}
