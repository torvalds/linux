/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <asm/cpufeature.h>

extern const struct raid6_calls raid6_mmxx1;
extern const struct raid6_calls raid6_mmxx2;
extern const struct raid6_calls raid6_sse1x1;
extern const struct raid6_calls raid6_sse1x2;
extern const struct raid6_calls raid6_sse2x1;
extern const struct raid6_calls raid6_sse2x2;
extern const struct raid6_calls raid6_sse2x4;
extern const struct raid6_calls raid6_avx2x1;
extern const struct raid6_calls raid6_avx2x2;
extern const struct raid6_calls raid6_avx2x4;
extern const struct raid6_calls raid6_avx512x1;
extern const struct raid6_calls raid6_avx512x2;
extern const struct raid6_calls raid6_avx512x4;

extern const struct raid6_recov_calls raid6_recov_ssse3;
extern const struct raid6_recov_calls raid6_recov_avx2;
extern const struct raid6_recov_calls raid6_recov_avx512;

static inline int raid6_has_avx512(void)
{
	return boot_cpu_has(X86_FEATURE_AVX2) &&
		boot_cpu_has(X86_FEATURE_AVX) &&
		boot_cpu_has(X86_FEATURE_AVX512F) &&
		boot_cpu_has(X86_FEATURE_AVX512BW) &&
		boot_cpu_has(X86_FEATURE_AVX512VL) &&
		boot_cpu_has(X86_FEATURE_AVX512DQ);
}

static inline bool raid6_has_avx2(void)
{
	return boot_cpu_has(X86_FEATURE_AVX2) && boot_cpu_has(X86_FEATURE_AVX);
}

static inline bool raid6_has_ssse3(void)
{
	return boot_cpu_has(X86_FEATURE_XMM) &&
		boot_cpu_has(X86_FEATURE_XMM2) &&
		boot_cpu_has(X86_FEATURE_SSSE3);
}

static inline bool raid6_has_sse2(void)
{
	return boot_cpu_has(X86_FEATURE_MMX) &&
		    boot_cpu_has(X86_FEATURE_FXSR) &&
		    boot_cpu_has(X86_FEATURE_XMM) &&
		    boot_cpu_has(X86_FEATURE_XMM2);
}

static inline bool raid6_has_sse1_or_mmxext(void)
{
	return boot_cpu_has(X86_FEATURE_MMX) &&
		(boot_cpu_has(X86_FEATURE_XMM) ||
		 boot_cpu_has(X86_FEATURE_MMXEXT));
}

static __always_inline void __init arch_raid6_init(void)
{
	if (raid6_has_avx2()) {
		raid6_algo_add(&raid6_avx2x1);
		raid6_algo_add(&raid6_avx2x2);
		if (IS_ENABLED(CONFIG_X86_64))
			raid6_algo_add(&raid6_avx2x4);
		if (raid6_has_avx512()) {
			raid6_algo_add(&raid6_avx512x1);
			raid6_algo_add(&raid6_avx512x2);
			if (IS_ENABLED(CONFIG_X86_64))
				raid6_algo_add(&raid6_avx512x4);
		}
	} else if (IS_ENABLED(CONFIG_X86_64) || raid6_has_sse2()) {
		/* x86_64 can assume SSE2 as baseline */
		raid6_algo_add(&raid6_sse2x1);
		raid6_algo_add(&raid6_sse2x2);
		if (IS_ENABLED(CONFIG_X86_64))
			raid6_algo_add(&raid6_sse2x4);
	} else {
		raid6_algo_add_default();
		if (raid6_has_sse1_or_mmxext()) {
			raid6_algo_add(&raid6_sse1x1);
			raid6_algo_add(&raid6_sse1x2);
		} else if (boot_cpu_has(X86_FEATURE_MMX)) {
			raid6_algo_add(&raid6_mmxx1);
			raid6_algo_add(&raid6_mmxx2);
		}
	}

	if (raid6_has_avx512())
		raid6_recov_algo_add(&raid6_recov_avx512);
	else if (raid6_has_avx2())
		raid6_recov_algo_add(&raid6_recov_avx2);
	else if (raid6_has_ssse3())
		raid6_recov_algo_add(&raid6_recov_ssse3);
}
