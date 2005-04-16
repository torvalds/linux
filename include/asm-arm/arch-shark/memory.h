/*
 * linux/include/asm-arm/arch-shark/memory.h
 *
 * by Alexander Schulz
 *
 * derived from:
 * linux/include/asm-arm/arch-ebsa110/memory.h
 * Copyright (c) 1996-1999 Russell King.
 */
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#include <asm/sizes.h>

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET     (0x08000000UL)

#ifndef __ASSEMBLY__

static inline void __arch_adjust_zones(int node, unsigned long *zone_size, unsigned long *zhole_size) 
{
  if (node != 0) return;
  /* Only the first 4 MB (=1024 Pages) are usable for DMA */
  zone_size[1] = zone_size[0] - 1024;
  zone_size[0] = 1024;
  zhole_size[1] = zhole_size[0];
  zhole_size[0] = 0;
}

#define arch_adjust_zones(node, size, holes) \
	__arch_adjust_zones(node, size, holes)

#define ISA_DMA_THRESHOLD	(PHYS_OFFSET + SZ_4M - 1)

#endif

#define __virt_to_bus(x)	__virt_to_phys(x)
#define __bus_to_virt(x)	__phys_to_virt(x)

#endif
