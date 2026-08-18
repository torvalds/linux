/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <asm/neon.h>

extern const struct raid6_calls raid6_neonx1;
extern const struct raid6_calls raid6_neonx2;
extern const struct raid6_calls raid6_neonx4;
extern const struct raid6_calls raid6_neonx8;
extern const struct raid6_recov_calls raid6_recov_neon;

static __always_inline void __init arch_raid6_init(void)
{
	raid6_algo_add_default();
	if (cpu_has_neon()) {
		raid6_algo_add(&raid6_neonx1);
		raid6_algo_add(&raid6_neonx2);
		raid6_algo_add(&raid6_neonx4);
		raid6_algo_add(&raid6_neonx8);
		raid6_recov_algo_add(&raid6_recov_neon);
	}
}
