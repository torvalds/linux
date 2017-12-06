/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DMA_MAPPING_H
#define _LINUX_DMA_MAPPING_H

#ifdef CONFIG_HAS_DMA
# error Virtio userspace code does not support CONFIG_HAS_DMA
#endif

#define PCI_DMA_BUS_IS_PHYS 1

enum dma_data_direction {
	DMA_BIDIRECTIONAL = 0,
	DMA_TO_DEVICE = 1,
	DMA_FROM_DEVICE = 2,
	DMA_NONE = 3,
};

#define dma_alloc_coherent(d, s, hp, f) ({ \
	void *__dma_alloc_coherent_p = kmalloc((s), (f)); \
	*(hp) = (unsigned long)__dma_alloc_coherent_p; \
	__dma_alloc_coherent_p; \
})

#define dma_free_coherent(d, s, p, h) kfree(p)

#define dma_map_page(d, p, o, s, dir) (page_to_phys(p) + (o))

#define dma_map_single(d, p, s, dir) (virt_to_phys(p))
#define dma_mapping_error(...) (0)

#define dma_unmap_single(...) do { } while (0)
#define dma_unmap_page(...) do { } while (0)

#endif
