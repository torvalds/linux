/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <asm/vector.h>

extern const struct raid6_calls raid6_rvvx1;
extern const struct raid6_calls raid6_rvvx2;
extern const struct raid6_calls raid6_rvvx4;
extern const struct raid6_calls raid6_rvvx8;
extern const struct raid6_recov_calls raid6_recov_rvv;

static __always_inline void __init arch_raid6_init(void)
{
	raid6_algo_add_default();
	if (has_vector()) {
		raid6_algo_add(&raid6_rvvx1);
		raid6_algo_add(&raid6_rvvx2);
		raid6_algo_add(&raid6_rvvx4);
		raid6_algo_add(&raid6_rvvx8);
		raid6_recov_algo_add(&raid6_recov_rvv);
	}
}
