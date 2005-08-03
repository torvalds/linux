#ifndef _ABS_ADDR_H
#define _ABS_ADDR_H

#include <linux/config.h>

/*
 * c 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/types.h>
#include <asm/page.h>
#include <asm/prom.h>
#include <asm/lmb.h>
#include <asm/firmware.h>

struct mschunks_map {
        unsigned long num_chunks;
        unsigned long chunk_size;
        unsigned long chunk_shift;
        unsigned long chunk_mask;
        u32 *mapping;
};

extern struct mschunks_map mschunks_map;

/* Chunks are 256 KB */
#define MSCHUNKS_CHUNK_SHIFT	(18)
#define MSCHUNKS_CHUNK_SIZE	(1UL << MSCHUNKS_CHUNK_SHIFT)
#define MSCHUNKS_OFFSET_MASK	(MSCHUNKS_CHUNK_SIZE - 1)

static inline unsigned long chunk_to_addr(unsigned long chunk)
{
	return chunk << MSCHUNKS_CHUNK_SHIFT;
}

static inline unsigned long addr_to_chunk(unsigned long addr)
{
	return addr >> MSCHUNKS_CHUNK_SHIFT;
}

static inline unsigned long phys_to_abs(unsigned long pa)
{
	unsigned long chunk;

	/* This is a no-op on non-iSeries */
	if (!firmware_has_feature(FW_FEATURE_ISERIES))
		return pa;

	chunk = addr_to_chunk(pa);

	if (chunk < mschunks_map.num_chunks)
		chunk = mschunks_map.mapping[chunk];

	return chunk_to_addr(chunk) + (pa & MSCHUNKS_OFFSET_MASK);
}

/* Convenience macros */
#define virt_to_abs(va) phys_to_abs(__pa(va))
#define abs_to_virt(aa) __va(aa)

#endif /* _ABS_ADDR_H */
