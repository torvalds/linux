/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header is for implementations of dma_map_ops and related code.
 * It should not be included in drivers just using the DMA API.
 */
#ifndef _LINUX_DMA_MAP_OPS_H
#define _LINUX_DMA_MAP_OPS_H

#include <linux/dma-mapping.h>
#include <linux/pgtable.h>
#include <linux/slab.h>

struct cma;
struct iommu_ops;

struct dma_map_ops {
	void *(*alloc)(struct device *dev, size_t size,
			dma_addr_t *dma_handle, gfp_t gfp,
			unsigned long attrs);
	void (*free)(struct device *dev, size_t size, void *vaddr,
			dma_addr_t dma_handle, unsigned long attrs);
	struct page *(*alloc_pages_op)(struct device *dev, size_t size,
			dma_addr_t *dma_handle, enum dma_data_direction dir,
			gfp_t gfp);
	void (*free_pages)(struct device *dev, size_t size, struct page *vaddr,
			dma_addr_t dma_handle, enum dma_data_direction dir);
	int (*mmap)(struct device *, struct vm_area_struct *,
			void *, dma_addr_t, size_t, unsigned long attrs);

	int (*get_sgtable)(struct device *dev, struct sg_table *sgt,
			void *cpu_addr, dma_addr_t dma_addr, size_t size,
			unsigned long attrs);

	dma_addr_t (*map_page)(struct device *dev, struct page *page,
			unsigned long offset, size_t size,
			enum dma_data_direction dir, unsigned long attrs);
	void (*unmap_page)(struct device *dev, dma_addr_t dma_handle,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs);
	/*
	 * map_sg should return a negative error code on error. See
	 * dma_map_sgtable() for a list of appropriate error codes
	 * and their meanings.
	 */
	int (*map_sg)(struct device *dev, struct scatterlist *sg, int nents,
			enum dma_data_direction dir, unsigned long attrs);
	void (*unmap_sg)(struct device *dev, struct scatterlist *sg, int nents,
			enum dma_data_direction dir, unsigned long attrs);
	dma_addr_t (*map_resource)(struct device *dev, phys_addr_t phys_addr,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs);
	void (*unmap_resource)(struct device *dev, dma_addr_t dma_handle,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs);
	void (*sync_single_for_cpu)(struct device *dev, dma_addr_t dma_handle,
			size_t size, enum dma_data_direction dir);
	void (*sync_single_for_device)(struct device *dev,
			dma_addr_t dma_handle, size_t size,
			enum dma_data_direction dir);
	void (*sync_sg_for_cpu)(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir);
	void (*sync_sg_for_device)(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir);
	void (*cache_sync)(struct device *dev, void *vaddr, size_t size,
			enum dma_data_direction direction);
	int (*dma_supported)(struct device *dev, u64 mask);
	u64 (*get_required_mask)(struct device *dev);
	size_t (*max_mapping_size)(struct device *dev);
	size_t (*opt_mapping_size)(void);
	unsigned long (*get_merge_boundary)(struct device *dev);
};

#ifdef CONFIG_ARCH_HAS_DMA_OPS
#include <asm/dma-mapping.h>

static inline const struct dma_map_ops *get_dma_ops(struct device *dev)
{
	if (dev->dma_ops)
		return dev->dma_ops;
	return get_arch_dma_ops();
}

static inline void set_dma_ops(struct device *dev,
			       const struct dma_map_ops *dma_ops)
{
	dev->dma_ops = dma_ops;
}
#else /* CONFIG_ARCH_HAS_DMA_OPS */
static inline const struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return NULL;
}
static inline void set_dma_ops(struct device *dev,
			       const struct dma_map_ops *dma_ops)
{
}
#endif /* CONFIG_ARCH_HAS_DMA_OPS */

#ifdef CONFIG_DMA_CMA
extern struct cma *dma_contiguous_default_area;

static inline struct cma *dev_get_cma_area(struct device *dev)
{
	if (dev && dev->cma_area)
		return dev->cma_area;
	return dma_contiguous_default_area;
}

void dma_contiguous_reserve(phys_addr_t addr_limit);
int __init dma_contiguous_reserve_area(phys_addr_t size, phys_addr_t base,
		phys_addr_t limit, struct cma **res_cma, bool fixed);

struct page *dma_alloc_from_contiguous(struct device *dev, size_t count,
				       unsigned int order, bool no_warn);
bool dma_release_from_contiguous(struct device *dev, struct page *pages,
				 int count);
struct page *dma_alloc_contiguous(struct device *dev, size_t size, gfp_t gfp);
void dma_free_contiguous(struct device *dev, struct page *page, size_t size);

void dma_contiguous_early_fixup(phys_addr_t base, unsigned long size);
#else /* CONFIG_DMA_CMA */
static inline struct cma *dev_get_cma_area(struct device *dev)
{
	return NULL;
}
static inline void dma_contiguous_reserve(phys_addr_t limit)
{
}
static inline int dma_contiguous_reserve_area(phys_addr_t size,
		phys_addr_t base, phys_addr_t limit, struct cma **res_cma,
		bool fixed)
{
	return -ENOSYS;
}
static inline struct page *dma_alloc_from_contiguous(struct device *dev,
		size_t count, unsigned int order, bool no_warn)
{
	return NULL;
}
static inline bool dma_release_from_contiguous(struct device *dev,
		struct page *pages, int count)
{
	return false;
}
/* Use fallback alloc() and free() when CONFIG_DMA_CMA=n */
static inline struct page *dma_alloc_contiguous(struct device *dev, size_t size,
		gfp_t gfp)
{
	return NULL;
}
static inline void dma_free_contiguous(struct device *dev, struct page *page,
		size_t size)
{
	__free_pages(page, get_order(size));
}
static inline void dma_contiguous_early_fixup(phys_addr_t base, unsigned long size)
{
}
#endif /* CONFIG_DMA_CMA*/

#ifdef CONFIG_DMA_DECLARE_COHERENT
int dma_declare_coherent_memory(struct device *dev, phys_addr_t phys_addr,
		dma_addr_t device_addr, size_t size);
void dma_release_coherent_memory(struct device *dev);
int dma_alloc_from_dev_coherent(struct device *dev, ssize_t size,
		dma_addr_t *dma_handle, void **ret);
int dma_release_from_dev_coherent(struct device *dev, int order, void *vaddr);
int dma_mmap_from_dev_coherent(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, size_t size, int *ret);
#else
static inline int dma_declare_coherent_memory(struct device *dev,
		phys_addr_t phys_addr, dma_addr_t device_addr, size_t size)
{
	return -ENOSYS;
}

#define dma_alloc_from_dev_coherent(dev, size, handle, ret) (0)
#define dma_release_from_dev_coherent(dev, order, vaddr) (0)
#define dma_mmap_from_dev_coherent(dev, vma, vaddr, order, ret) (0)
static inline void dma_release_coherent_memory(struct device *dev) { }
#endif /* CONFIG_DMA_DECLARE_COHERENT */

#ifdef CONFIG_DMA_GLOBAL_POOL
void *dma_alloc_from_global_coherent(struct device *dev, ssize_t size,
		dma_addr_t *dma_handle);
int dma_release_from_global_coherent(int order, void *vaddr);
int dma_mmap_from_global_coherent(struct vm_area_struct *vma, void *cpu_addr,
		size_t size, int *ret);
int dma_init_global_coherent(phys_addr_t phys_addr, size_t size);
#else
static inline void *dma_alloc_from_global_coherent(struct device *dev,
		ssize_t size, dma_addr_t *dma_handle)
{
	return NULL;
}
static inline int dma_release_from_global_coherent(int order, void *vaddr)
{
	return 0;
}
static inline int dma_mmap_from_global_coherent(struct vm_area_struct *vma,
		void *cpu_addr, size_t size, int *ret)
{
	return 0;
}
#endif /* CONFIG_DMA_GLOBAL_POOL */

int dma_common_get_sgtable(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
int dma_common_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
struct page *dma_common_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, enum dma_data_direction dir, gfp_t gfp);
void dma_common_free_pages(struct device *dev, size_t size, struct page *vaddr,
		dma_addr_t dma_handle, enum dma_data_direction dir);

struct page **dma_common_find_pages(void *cpu_addr);
void *dma_common_contiguous_remap(struct page *page, size_t size, pgprot_t prot,
		const void *caller);
void *dma_common_pages_remap(struct page **pages, size_t size, pgprot_t prot,
		const void *caller);
void dma_common_free_remap(void *cpu_addr, size_t size);

struct page *dma_alloc_from_pool(struct device *dev, size_t size,
		void **cpu_addr, gfp_t flags,
		bool (*phys_addr_ok)(struct device *, phys_addr_t, size_t));
bool dma_free_from_pool(struct device *dev, void *start, size_t size);

int dma_direct_set_offset(struct device *dev, phys_addr_t cpu_start,
		dma_addr_t dma_start, u64 size);

#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) || \
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL)
extern bool dma_default_coherent;
static inline bool dev_is_dma_coherent(struct device *dev)
{
	return dev->dma_coherent;
}
#else
#define dma_default_coherent true

static inline bool dev_is_dma_coherent(struct device *dev)
{
	return true;
}
#endif

static inline void dma_reset_need_sync(struct device *dev)
{
#ifdef CONFIG_DMA_NEED_SYNC
	/* Reset it only once so that the function can be called on hotpath */
	if (unlikely(dev->dma_skip_sync))
		dev->dma_skip_sync = false;
#endif
}

/*
 * Check whether potential kmalloc() buffers are safe for non-coherent DMA.
 */
static inline bool dma_kmalloc_safe(struct device *dev,
				    enum dma_data_direction dir)
{
	/*
	 * If DMA bouncing of kmalloc() buffers is disabled, the kmalloc()
	 * caches have already been aligned to a DMA-safe size.
	 */
	if (!IS_ENABLED(CONFIG_DMA_BOUNCE_UNALIGNED_KMALLOC))
		return true;

	/*
	 * kmalloc() buffers are DMA-safe irrespective of size if the device
	 * is coherent or the direction is DMA_TO_DEVICE (non-desctructive
	 * cache maintenance and benign cache line evictions).
	 */
	if (dev_is_dma_coherent(dev) || dir == DMA_TO_DEVICE)
		return true;

	return false;
}

/*
 * Check whether the given size, assuming it is for a kmalloc()'ed buffer, is
 * sufficiently aligned for non-coherent DMA.
 */
static inline bool dma_kmalloc_size_aligned(size_t size)
{
	/*
	 * Larger kmalloc() sizes are guaranteed to be aligned to
	 * ARCH_DMA_MINALIGN.
	 */
	if (size >= 2 * ARCH_DMA_MINALIGN ||
	    IS_ALIGNED(kmalloc_size_roundup(size), dma_get_cache_alignment()))
		return true;

	return false;
}

/*
 * Check whether the given object size may have originated from a kmalloc()
 * buffer with a slab alignment below the DMA-safe alignment and needs
 * bouncing for non-coherent DMA. The pointer alignment is not considered and
 * in-structure DMA-safe offsets are the responsibility of the caller. Such
 * code should use the static ARCH_DMA_MINALIGN for compiler annotations.
 *
 * The heuristics can have false positives, bouncing unnecessarily, though the
 * buffers would be small. False negatives are theoretically possible if, for
 * example, multiple small kmalloc() buffers are coalesced into a larger
 * buffer that passes the alignment check. There are no such known constructs
 * in the kernel.
 */
static inline bool dma_kmalloc_needs_bounce(struct device *dev, size_t size,
					    enum dma_data_direction dir)
{
	return !dma_kmalloc_safe(dev, dir) && !dma_kmalloc_size_aligned(size);
}

void *arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs);
void arch_dma_free(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_addr, unsigned long attrs);

#ifdef CONFIG_ARCH_HAS_DMA_SET_MASK
void arch_dma_set_mask(struct device *dev, u64 mask);
#else
#define arch_dma_set_mask(dev, mask)	do { } while (0)
#endif

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

#ifdef CONFIG_ARCH_HAS_DMA_MAP_DIRECT
bool arch_dma_map_phys_direct(struct device *dev, phys_addr_t addr);
bool arch_dma_unmap_phys_direct(struct device *dev, dma_addr_t dma_handle);
bool arch_dma_map_sg_direct(struct device *dev, struct scatterlist *sg,
		int nents);
bool arch_dma_unmap_sg_direct(struct device *dev, struct scatterlist *sg,
		int nents);
#else
#define arch_dma_map_phys_direct(d, a)		(false)
#define arch_dma_unmap_phys_direct(d, a)	(false)
#define arch_dma_map_sg_direct(d, s, n)		(false)
#define arch_dma_unmap_sg_direct(d, s, n)	(false)
#endif

#ifdef CONFIG_ARCH_HAS_SETUP_DMA_OPS
void arch_setup_dma_ops(struct device *dev, bool coherent);
#else
static inline void arch_setup_dma_ops(struct device *dev, bool coherent)
{
}
#endif /* CONFIG_ARCH_HAS_SETUP_DMA_OPS */

#ifdef CONFIG_ARCH_HAS_TEARDOWN_DMA_OPS
void arch_teardown_dma_ops(struct device *dev);
#else
static inline void arch_teardown_dma_ops(struct device *dev)
{
}
#endif /* CONFIG_ARCH_HAS_TEARDOWN_DMA_OPS */

#ifdef CONFIG_DMA_API_DEBUG
void dma_debug_add_bus(const struct bus_type *bus);
void debug_dma_dump_mappings(struct device *dev);
#else
static inline void dma_debug_add_bus(const struct bus_type *bus)
{
}
static inline void debug_dma_dump_mappings(struct device *dev)
{
}
#endif /* CONFIG_DMA_API_DEBUG */

extern const struct dma_map_ops dma_dummy_ops;
#endif /* _LINUX_DMA_MAP_OPS_H */
