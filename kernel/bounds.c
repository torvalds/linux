// SPDX-License-Identifier: GPL-2.0
/*
 * Generate definitions needed by the preprocessor.
 * This code generates raw asm output which is post-processed
 * to extract and format the required data.
 */

#define __GENERATING_BOUNDS_H
/* Include headers that define the enum constants of interest */
#include <linux/page-flags.h>
#include <linux/mmzone.h>
#include <linux/kbuild.h>
#include <linux/log2.h>
#include <linux/spinlock_types.h>

int main(void)
{
	/* The enum constants to put into include/generated/bounds.h */
	DEFINE(NR_PAGEFLAGS, __NR_PAGEFLAGS);
	DEFINE(MAX_NR_ZONES, __MAX_NR_ZONES);
#ifdef CONFIG_SMP
	DEFINE(NR_CPUS_BITS, bits_per(CONFIG_NR_CPUS));
#endif
	DEFINE(SPINLOCK_SIZE, sizeof(spinlock_t));
#ifdef CONFIG_LRU_GEN
	DEFINE(LRU_GEN_WIDTH, order_base_2(MAX_NR_GENS + 1));
	DEFINE(__LRU_REFS_WIDTH, MAX_NR_TIERS - 2);
#else
	DEFINE(LRU_GEN_WIDTH, 0);
	DEFINE(__LRU_REFS_WIDTH, 0);
#endif
	/* End of constants */

	return 0;
}
