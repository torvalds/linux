/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <asm/cpu-features.h>

extern const struct raid6_calls raid6_lsx;
extern const struct raid6_calls raid6_lasx;

extern const struct raid6_recov_calls raid6_recov_lsx;
extern const struct raid6_recov_calls raid6_recov_lasx;

static __always_inline void __init arch_raid6_init(void)
{
	raid6_algo_add_default();
	if (IS_ENABLED(CONFIG_CPU_HAS_LSX) && cpu_has_lsx)
		raid6_algo_add(&raid6_lsx);
	if (IS_ENABLED(CONFIG_CPU_HAS_LASX) && cpu_has_lasx)
		raid6_algo_add(&raid6_lasx);

	if (IS_ENABLED(CONFIG_CPU_HAS_LASX) && cpu_has_lasx)
		raid6_recov_algo_add(&raid6_recov_lasx);
	else if (IS_ENABLED(CONFIG_CPU_HAS_LSX) && cpu_has_lsx)
		raid6_recov_algo_add(&raid6_recov_lsx);
}
