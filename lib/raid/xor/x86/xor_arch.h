/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <asm/cpufeature.h>

extern struct xor_block_template xor_block_pII_mmx;
extern struct xor_block_template xor_block_p5_mmx;
extern struct xor_block_template xor_block_sse;
extern struct xor_block_template xor_block_sse_pf64;
extern struct xor_block_template xor_block_avx;
extern struct xor_block_template xor_block_avx512;

static __always_inline void __init arch_xor_init(void)
{
	if (IS_ENABLED(CONFIG_X86_64) && boot_cpu_has(X86_FEATURE_AVX512F) &&
	    !boot_cpu_has(X86_FEATURE_PREFER_YMM)) {
		/*
		 * Use the AVX-512 code on CPUs that support AVX-512 without
		 * overly-eager downclocking.  On such CPUs the AVX-512 code
		 * should always work at least as well as the AVX code, so
		 * runtime selection is unnecessary.
		 *
		 * The AVX-512 code can work on X86_32.  However, due to lack of
		 * use case for that, for now it's built only for X86_64.
		 */
		xor_force(&xor_block_avx512);
	} else if (boot_cpu_has(X86_FEATURE_AVX)) {
		/* AVX will be the best; no need to try others. */
		xor_force(&xor_block_avx);
	} else if (IS_ENABLED(CONFIG_X86_64) || boot_cpu_has(X86_FEATURE_XMM)) {
		/*
		 * When SSE is available, use it as it can write around L2.  We
		 * may also be able to load into the L1 only depending on how
		 * the cpu deals with a load to a line that is being prefetched.
		 */
		xor_register(&xor_block_sse);
		xor_register(&xor_block_sse_pf64);
	} else if (boot_cpu_has(X86_FEATURE_MMX)) {
		xor_register(&xor_block_pII_mmx);
		xor_register(&xor_block_p5_mmx);
	} else {
		xor_register(&xor_block_8regs);
		xor_register(&xor_block_8regs_p);
		xor_register(&xor_block_32regs);
		xor_register(&xor_block_32regs_p);
	}
}
