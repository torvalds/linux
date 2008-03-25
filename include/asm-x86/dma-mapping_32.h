#ifndef _ASM_I386_DMA_MAPPING_H
#define _ASM_I386_DMA_MAPPING_H

#include <linux/mm.h>
#include <linux/scatterlist.h>

#include <asm/cache.h>
#include <asm/io.h>
#include <asm/bug.h>

extern int forbid_dac;

static inline int
dma_get_cache_alignment(void)
{
	/* no easy way to get cache size on all x86, so return the
	 * maximum possible, to be safe */
	return boot_cpu_data.x86_clflush_size;
}

#define dma_is_consistent(d, h)	(1)

#define ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY
extern int
dma_declare_coherent_memory(struct device *dev, dma_addr_t bus_addr,
			    dma_addr_t device_addr, size_t size, int flags);

extern void
dma_release_declared_memory(struct device *dev);

extern void *
dma_mark_declared_memory_occupied(struct device *dev,
				  dma_addr_t device_addr, size_t size);

#endif
