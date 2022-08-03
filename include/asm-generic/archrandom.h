/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_ARCHRANDOM_H__
#define __ASM_GENERIC_ARCHRANDOM_H__

static inline size_t __must_check arch_get_random_longs(unsigned long *v, size_t max_longs)
{
	return 0;
}

static inline size_t __must_check arch_get_random_seed_longs(unsigned long *v, size_t max_longs)
{
	return 0;
}

#endif
