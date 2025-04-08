/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DMA_MAPPING_H
#define _LINUX_DMA_MAPPING_H

#ifdef CONFIG_HAS_DMA
# error Virtio userspace code does not support CONFIG_HAS_DMA
#endif

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
#define dma_map_single_attrs(d, p, s, dir, a) (virt_to_phys(p))
#define dma_mapping_error(...) (0)

#define dma_unmap_single(d, a, s, r) do { (void)(d); (void)(a); (void)(s); (void)(r); } while (0)
#define dma_unmap_page(d, a, s, r) do { (void)(d); (void)(a); (void)(s); (void)(r); } while (0)

#define sg_dma_address(sg) (0)
#define sg_dma_len(sg) (0)
#define dma_need_sync(v, a) (0)
#define dma_unmap_single_attrs(d, a, s, r, t) do { \
	(void)(d); (void)(a); (void)(s); (void)(r); (void)(t); \
} while (0)
#define dma_sync_single_range_for_cpu(d, a, o, s, r) do { \
	(void)(d); (void)(a); (void)(o); (void)(s); (void)(r); \
} while (0)
#define dma_sync_single_range_for_device(d, a, o, s, r) do { \
	(void)(d); (void)(a); (void)(o); (void)(s); (void)(r); \
} while (0)
#define dma_max_mapping_size(...) SIZE_MAX

/*
 * A dma_addr_t can hold any valid DMA or bus address for the platform.  It can
 * be given to a device to use as a DMA source or target.  It is specific to a
 * given device and there may be a translation between the CPU physical address
 * space and the bus address space.
 *
 * DMA_MAPPING_ERROR is the magic error code if a mapping failed.  It should not
 * be used directly in drivers, but checked for using dma_mapping_error()
 * instead.
 */
#define DMA_MAPPING_ERROR		(~(dma_addr_t)0)

#endif
