/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DMA_NONCOHERENT_H
#define _LINUX_DMA_NONCOHERENT_H 1

#include <linux/dma-mapping.h>
#include <linux/pgtable.h>

#ifdef CONFIG_ARCH_HAS_DMA_COHERENCE_H
#include <asm/dma-coherence.h>
#elif defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) || \
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL)
static inline bool dev_is_dma_coherent(struct device *dev)
{
	return dev->dma_coherent;
}
#else
static inline bool dev_is_dma_coherent(struct device *dev)
{
	return true;
}
#endif /* CONFIG_ARCH_HAS_DMA_COHERENCE_H */

/*
 * Check if an allocation needs to be marked uncached to be coherent.
 */
static __always_inline bool dma_alloc_need_uncached(struct device *dev,
		unsigned long attrs)
{
	if (dev_is_dma_coherent(dev))
		return false;
	if (attrs & DMA_ATTR_NO_KERNEL_MAPPING)
		return false;
	return true;
}

void *arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs);
void arch_dma_free(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_addr, unsigned long attrs);

#ifdef CONFIG_MMU
/*
 * Page protection so that devices that can't snoop CPU caches can use the
 * memory coherently.  We default to pgprot_noncached which is usually used
 * for ioremap as a safe bet, but architectures can override this with less
 * strict semantics if possible.
 */
#ifndef pgprot_dmacoherent
#define pgprot_dmacoherent(prot)	pgprot_noncached(prot)
#endif

pgprot_t dma_pgprot(struct device *dev, pgprot_t prot, unsigned long attrs);
#else
static inline pgprot_t dma_pgprot(struct device *dev, pgprot_t prot,
		unsigned long attrs)
{
	return prot;	/* no protection bits supported without page tables */
}
#endif /* CONFIG_MMU */

#ifdef CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE
void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir);
#else
static inline void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
}
#endif /* ARCH_HAS_SYNC_DMA_FOR_DEVICE */

#ifdef CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU
void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir);
#else
static inline void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
}
#endif /* ARCH_HAS_SYNC_DMA_FOR_CPU */

#ifdef CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL
void arch_sync_dma_for_cpu_all(void);
#else
static inline void arch_sync_dma_for_cpu_all(void)
{
}
#endif /* CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL */

#ifdef CONFIG_ARCH_HAS_DMA_PREP_COHERENT
void arch_dma_prep_coherent(struct page *page, size_t size);
#else
static inline void arch_dma_prep_coherent(struct page *page, size_t size)
{
}
#endif /* CONFIG_ARCH_HAS_DMA_PREP_COHERENT */

#ifdef CONFIG_ARCH_HAS_DMA_MARK_CLEAN
void arch_dma_mark_clean(phys_addr_t paddr, size_t size);
#else
static inline void arch_dma_mark_clean(phys_addr_t paddr, size_t size)
{
}
#endif /* ARCH_HAS_DMA_MARK_CLEAN */

void *arch_dma_set_uncached(void *addr, size_t size);
void arch_dma_clear_uncached(void *addr, size_t size);

#endif /* _LINUX_DMA_NONCOHERENT_H */
