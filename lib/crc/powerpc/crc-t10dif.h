// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Calculate a CRC T10-DIF with vpmsum acceleration
 *
 * Copyright 2017, Daniel Axtens, IBM Corporation.
 * [based on crc32c-vpmsum_glue.c]
 */

#include <asm/simd.h>
#include <asm/switch_to.h>
#include <linux/cpufeature.h>
#include <linux/jump_label.h>
#include <linux/preempt.h>
#include <linux/uaccess.h>

#define VMX_ALIGN		16
#define VMX_ALIGN_MASK		(VMX_ALIGN-1)

#define VECTOR_BREAKPOINT	64

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_vec_crypto);

u32 __crct10dif_vpmsum(u32 crc, unsigned char const *p, size_t len);

static inline u16 crc_t10dif_arch(u16 crci, const u8 *p, size_t len)
{
	unsigned int prealign;
	unsigned int tail;
	u32 crc = crci;

	if (len < (VECTOR_BREAKPOINT + VMX_ALIGN) ||
	    !static_branch_likely(&have_vec_crypto) ||
	    unlikely(!may_use_simd()))
		return crc_t10dif_generic(crc, p, len);

	if ((unsigned long)p & VMX_ALIGN_MASK) {
		prealign = VMX_ALIGN - ((unsigned long)p & VMX_ALIGN_MASK);
		crc = crc_t10dif_generic(crc, p, prealign);
		len -= prealign;
		p += prealign;
	}

	if (len & ~VMX_ALIGN_MASK) {
		crc <<= 16;
		preempt_disable();
		pagefault_disable();
		enable_kernel_altivec();
		crc = __crct10dif_vpmsum(crc, p, len & ~VMX_ALIGN_MASK);
		disable_kernel_altivec();
		pagefault_enable();
		preempt_enable();
		crc >>= 16;
	}

	tail = len & VMX_ALIGN_MASK;
	if (tail) {
		p += len & ~VMX_ALIGN_MASK;
		crc = crc_t10dif_generic(crc, p, tail);
	}

	return crc & 0xffff;
}

#define crc_t10dif_mod_init_arch crc_t10dif_mod_init_arch
static void crc_t10dif_mod_init_arch(void)
{
	if (cpu_has_feature(CPU_FTR_ARCH_207S) &&
	    (cur_cpu_spec->cpu_user_features2 & PPC_FEATURE2_VEC_CRYPTO))
		static_branch_enable(&have_vec_crypto);
}
