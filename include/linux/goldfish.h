/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_GOLDFISH_H
#define __LINUX_GOLDFISH_H

/* Helpers for Goldfish virtual platform */

static inline void gf_write_ptr(const void *ptr, void __iomem *portl,
				void __iomem *porth)
{
	writel((u32)(unsigned long)ptr, portl);
#ifdef CONFIG_64BIT
	writel((unsigned long)ptr >> 32, porth);
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
