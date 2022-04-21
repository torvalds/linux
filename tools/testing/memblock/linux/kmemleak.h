/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _KMEMLEAK_H
#define _KMEMLEAK_H

static inline void kmemleak_free_part_phys(phys_addr_t phys, size_t size)
{
}

static inline void kmemleak_alloc_phys(phys_addr_t phys, size_t size,
				       int min_count, gfp_t gfp)
{
}

static inline void dump_stack(void)
{
}

#endif
