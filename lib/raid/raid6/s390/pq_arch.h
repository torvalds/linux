/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <linux/cpufeature.h>

extern const struct raid6_calls raid6_s390vx8;
extern const struct raid6_recov_calls raid6_recov_s390xc;

static __always_inline void __init arch_raid6_init(void)
{
	if (cpu_has_vx())
		raid6_algo_add(&raid6_s390vx8);
	else
		raid6_algo_add_default();
	raid6_recov_algo_add(&raid6_recov_s390xc);
}
