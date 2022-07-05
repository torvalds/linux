/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_ARCHRANDOM_H__
#define __ASM_GENERIC_ARCHRANDOM_H__

static inline bool __must_check arch_get_random_long(unsigned long *v)
{
	return false;
}

static inline bool __must_check arch_get_random_int(unsigned int *v)
{
	return false;
}

static inline bool __must_check arch_get_random_seed_long(unsigned long *v)
{
	return false;
}

static inline bool __must_check arch_get_random_seed_int(unsigned int *v)
{
	return false;
}

#endif
