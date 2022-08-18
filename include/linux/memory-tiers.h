/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MEMORY_TIERS_H
#define _LINUX_MEMORY_TIERS_H

/*
 * Each tier cover a abstrace distance chunk size of 128
 */
#define MEMTIER_CHUNK_BITS	7
#define MEMTIER_CHUNK_SIZE	(1 << MEMTIER_CHUNK_BITS)
/*
 * Smaller abstract distance values imply faster (higher) memory tiers. Offset
 * the DRAM adistance so that we can accommodate devices with a slightly lower
 * adistance value (slightly faster) than default DRAM adistance to be part of
 * the same memory tier.
 */
#define MEMTIER_ADISTANCE_DRAM	((4 * MEMTIER_CHUNK_SIZE) + (MEMTIER_CHUNK_SIZE >> 1))

#endif  /* _LINUX_MEMORY_TIERS_H */
