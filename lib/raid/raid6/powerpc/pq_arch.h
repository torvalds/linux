/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <asm/cputable.h>

extern const struct raid6_calls raid6_altivec1;
extern const struct raid6_calls raid6_altivec2;
extern const struct raid6_calls raid6_altivec4;
extern const struct raid6_calls raid6_altivec8;
extern const struct raid6_calls raid6_vpermxor1;
extern const struct raid6_calls raid6_vpermxor2;
extern const struct raid6_calls raid6_vpermxor4;
extern const struct raid6_calls raid6_vpermxor8;

static __always_inline void __init arch_raid6_init(void)
{
	raid6_algo_add_default();

	/* This assumes either all CPUs have Altivec or none does */
	if (cpu_has_feature(CPU_FTR_ALTIVEC)) {
		raid6_algo_add(&raid6_altivec1);
		raid6_algo_add(&raid6_altivec2);
		raid6_algo_add(&raid6_altivec4);
		raid6_algo_add(&raid6_altivec8);
	}
	if (cpu_has_feature(CPU_FTR_ALTIVEC_COMP) &&
	    cpu_has_feature(CPU_FTR_ARCH_207S)) {
		raid6_algo_add(&raid6_vpermxor1);
		raid6_algo_add(&raid6_vpermxor2);
		raid6_algo_add(&raid6_vpermxor4);
		raid6_algo_add(&raid6_vpermxor8);
	}
}
