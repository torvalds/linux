#ifndef __LINUX_GOLDFISH_H
#define __LINUX_GOLDFISH_H

#include <linux/types.h>
#include <linux/io.h>

/* Helpers for Goldfish virtual platform */

static inline void gf_write_ptr(const void *ptr, void __iomem *portl,
				void __iomem *porth)
{
	const uintptr_t addr = (uintptr_t)ptr;

	writel((u32)addr, portl);
#ifdef CONFIG_64BIT
	writel(addr >> 32, porth);
#endif
}

static inline void gf_write_dma_addr(const dma_addr_t addr,
				     void __iomem *portl,
				     void __iomem *porth)
{
	writel((u32)addr, portl);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	writel(addr >> 32, porth);
#endif
}


#endif /* __LINUX_GOLDFISH_H */
