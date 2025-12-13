// SPDX-License-Identifier: GPL-2.0-only
#include <asm/simd.h>
#include <asm/switch_to.h>
#include <linux/cpufeature.h>
#include <linux/jump_label.h>
#include <linux/preempt.h>
#include <linux/uaccess.h>

#define VMX_ALIGN		16
#define VMX_ALIGN_MASK		(VMX_ALIGN-1)

#define VECTOR_BREAKPOINT	512

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_vec_crypto);

#define crc32_le_arch crc32_le_base /* not implemented on this arch */
#define crc32_be_arch crc32_be_base /* not implemented on this arch */

u32 __crc32c_vpmsum(u32 crc, const u8 *p, size_t len);

static inline u32 crc32c_arch(u32 crc, const u8 *p, size_t len)
{
	unsigned int prealign;
	unsigned int tail;

	if (len < (VECTOR_BREAKPOINT + VMX_ALIGN) ||
	    !static_branch_likely(&have_vec_crypto) ||
	    unlikely(!may_use_simd()))
		return crc32c_base(crc, p, len);

	if ((unsigned long)p & VMX_ALIGN_MASK) {
		prealign = VMX_ALIGN - ((unsigned long)p & VMX_ALIGN_MASK);
		crc = crc32c_base(crc, p, prealign);
		len -= prealign;
		p += prealign;
	}

	if (len & ~VMX_ALIGN_MASK) {
		preempt_disable();
		pagefault_disable();
		enable_kernel_altivec();
		crc = __crc32c_vpmsum(crc, p, len & ~VMX_ALIGN_MASK);
		disable_kernel_altivec();
		pagefault_enable();
		preempt_enable();
	}

	tail = len & VMX_ALIGN_MASK;
	if (tail) {
		p += len & ~VMX_ALIGN_MASK;
		crc = crc32c_base(crc, p, tail);
	}

	return crc;
}

#define crc32_mod_init_arch crc32_mod_init_arch
static void crc32_mod_init_arch(void)
{
	if (cpu_has_feature(CPU_FTR_ARCH_207S) &&
	    (cur_cpu_spec->cpu_user_features2 & PPC_FEATURE2_VEC_CRYPTO))
		static_branch_enable(&have_vec_crypto);
}

static inline u32 crc32_optimizations_arch(void)
{
	if (static_key_enabled(&have_vec_crypto))
		return CRC32C_OPTIMIZATION;
	return 0;
}
