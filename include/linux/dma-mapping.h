#ifndef _LINUX_DMA_MAPPING_H
#define _LINUX_DMA_MAPPING_H

#include <linux/string.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/dma-attrs.h>
#include <linux/dma-direction.h>
#include <linux/scatterlist.h>

struct dma_map_ops {
	void* (*alloc)(struct device *dev, size_t size,
				dma_addr_t *dma_handle, gfp_t gfp,
				struct dma_attrs *attrs);
	void (*free)(struct device *dev, size_t size,
			      void *vaddr, dma_addr_t dma_handle,
			      struct dma_attrs *attrs);
	int (*mmap)(struct device *, struct vm_area_struct *,
			  void *, dma_addr_t, size_t, struct dma_attrs *attrs);

	int (*get_sgtable)(struct device *dev, struct sg_table *sgt, void *,
			   dma_addr_t, size_t, struct dma_attrs *attrs);

	dma_addr_t (*map_page)(struct device *dev, struct page *page,
			       unsigned long offset, size_t size,
			       enum dma_data_direction dir,
			       struct dma_attrs *attrs);
	void (*unmap_page)(struct device *dev, dma_addr_t dma_handle,
			   size_t size, enum dma_data_direction dir,
			   struct dma_attrs *attrs);
	int (*map_sg)(struct device *dev, struct scatterlist *sg,
		      int nents, enum dma_data_direction dir,
		      struct dma_attrs *attrs);
	void (*unmap_sg)(struct device *dev,
			 struct scatterlist *sg, int nents,
			 enum dma_data_direction dir,
			 struct dma_attrs *attrs);
	void (*sync_single_for_cpu)(struct device *dev,
				    dma_addr_t dma_handle, size_t size,
				    enum dma_data_direction dir);
	void (*sync_single_for_device)(struct device *dev,
				       dma_addr_t dma_handle, size_t size,
				       enum dma_data_direction dir);
	void (*sync_sg_for_cpu)(struct device *dev,
				struct scatterlist *sg, int nents,
				enum dma_data_direction dir);
	void (*sync_sg_for_device)(struct device *dev,
				   struct scatterlist *sg, int nents,
				   enum dma_data_direction dir);
	int (*mapping_error)(struct device *dev, dma_addr_t dma_addr);
	int (*dma_supported)(struct device *dev, u64 mask);
	int (*set_dma_mask)(struct device *dev, u64 mask);
#ifdef ARCH_HAS_DMA_GET_REQUIRED_MASK
	u64 (*get_required_mask)(struct device *dev);
#endif
	int is_phys;
};

#define DMA_BIT_MASK(n)	(((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))

#define DMA_MASK_NONE	0x0ULL

static inline int valid_dma_direction(int dma_direction)
{
	return ((dma_direction == DMA_BIDIRECTIONAL) ||
		(dma_direction == DMA_TO_DEVICE) ||
		(dma_direction == DMA_FROM_DEVICE));
}

static inline int is_device_dma_capable(struct device *dev)
{
	return dev->dma_mask != NULL && *dev->dma_mask != DMA_MASK_NONE;
}

#ifdef CONFIG_HAS_DMA
#include <asm/dma-mapping.h>
#else
#include <asm-generic/dma-mapping-broken.h>
#endif

static inline u64 dma_get_mask(struct device *dev)
{
	if (dev && dev->dma_mask && *dev->dma_mask)
		return *dev->dma_mask;
	return DMA_BIT_MASK(32);
}

#ifdef CONFIG_ARCH_HAS_DMA_SET_COHERENT_MASK
int dma_set_coherent_mask(struct device *dev, u64 mask);
#else
static inline int dma_set_coherent_mask(struct device *dev, u64 mask)
{
	if (!dma_supported(dev, mask))
		return -EIO;
	dev->coherent_dma_mask = mask;
	return 0;
}
#endif

/*
 * Set both the DMA mask and the coherent DMA mask to the same thing.
 * Note that we don't check the return value from dma_set_coherent_mask()
 * as the DMA API guarantees that the coherent DMA mask can be set to
 * the same or smaller than the streaming DMA mask.
 */
static inline int dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
	int rc = dma_set_mask(dev, mask);
	if (rc == 0)
		dma_set_coherent_mask(dev, mask);
	return rc;
}

extern u64 dma_get_required_mask(struct device *dev);

static inline unsigned int dma_get_max_seg_size(struct device *dev)
{
	return dev->dma_parms ? dev->dma_parms->max_segment_size : 65536;
}

static inline unsigned int dma_set_max_seg_size(struct device *dev,
						unsigned int size)
{
	if (dev->dma_parms) {
		dev->dma_parms->max_segment_size = size;
		return 0;
	} else
		return -EIO;
}

static inline unsigned long dma_get_seg_boundary(struct device *dev)
{
	return dev->dma_parms ?
		dev->dma_parms->segment_boundary_mask : 0xffffffff;
}

static inline int dma_set_seg_boundary(struct device *dev, unsigned long mask)
{
	if (dev->dma_parms) {
		dev->dma_parms->segment_boundary_mask = mask;
		return 0;
	} else
		return -EIO;
}

static inline void *dma_zalloc_coherent(struct device *dev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag)
{
	void *ret = dma_alloc_coherent(dev, size, dma_handle,
				       flag | __GFP_ZERO);
	return ret;
}

#ifdef CONFIG_HAS_DMA
static inline int dma_get_cache_alignment(void)
{
#ifdef ARCH_DMA_MINALIGN
	return ARCH_DMA_MINALIGN;
#endif
	return 1;
}
#endif

/* flags for the coherent memory api */
#define	DMA_MEMORY_MAP			0x01
#define DMA_MEMORY_IO			0x02
#define DMA_MEMORY_INCLUDES_CHILDREN	0x04
#define DMA_MEMORY_EXCLUSIVE		0x08

#ifndef ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY
static inline int
dma_declare_coherent_memory(struct device *dev, dma_addr_t bus_addr,
			    dma_addr_t device_addr, size_t size, int flags)
{
	return 0;
}

static inline void
dma_release_declared_memory(struct device *dev)
{
}

static inline void *
dma_mark_declared_memory_occupied(struct device *dev,
				  dma_addr_t device_addr, size_t size)
{
	return ERR_PTR(-EBUSY);
}
#endif

/*
 * Managed DMA API
 */
extern void *dmam_alloc_coherent(struct device *dev, size_t size,
				 dma_addr_t *dma_handle, gfp_t gfp);
extern void dmam_free_coherent(struct device *dev, size_t size, void *vaddr,
			       dma_addr_t dma_handle);
extern void *dmam_alloc_noncoherent(struct device *dev, size_t size,
				    dma_addr_t *dma_handle, gfp_t gfp);
extern void dmam_free_noncoherent(struct device *dev, size_t size, void *vaddr,
				  dma_addr_t dma_handle);
#ifdef ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY
extern int dmam_declare_coherent_memory(struct device *dev, dma_addr_t bus_addr,
					dma_addr_t device_addr, size_t size,
					int flags);
extern void dmam_release_declared_memory(struct device *dev);
#else /* ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY */
static inline int dmam_declare_coherent_memory(struct device *dev,
				dma_addr_t bus_addr, dma_addr_t device_addr,
				size_t size, gfp_t gfp)
{
	return 0;
}

static inline void dmam_release_declared_memory(struct device *dev)
{
}
#endif /* ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY */

#ifndef CONFIG_HAVE_DMA_ATTRS
struct dma_attrs;

#define dma_map_single_attrs(dev, cpu_addr, size, dir, attrs) \
	dma_map_single(dev, cpu_addr, size, dir)

#define dma_unmap_single_attrs(dev, dma_addr, size, dir, attrs) \
	dma_unmap_single(dev, dma_addr, size, dir)

#define dma_map_sg_attrs(dev, sgl, nents, dir, attrs) \
	dma_map_sg(dev, sgl, nents, dir)

#define dma_unmap_sg_attrs(dev, sgl, nents, dir, attrs) \
	dma_unmap_sg(dev, sgl, nents, dir)

#endif /* CONFIG_HAVE_DMA_ATTRS */

#ifdef CONFIG_NEED_DMA_MAP_STATE
#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)        dma_addr_t ADDR_NAME
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)          __u32 LEN_NAME
#define dma_unmap_addr(PTR, ADDR_NAME)           ((PTR)->ADDR_NAME)
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)  (((PTR)->ADDR_NAME) = (VAL))
#define dma_unmap_len(PTR, LEN_NAME)             ((PTR)->LEN_NAME)
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)    (((PTR)->LEN_NAME) = (VAL))
#else
#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)
#define dma_unmap_addr(PTR, ADDR_NAME)           (0)
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)  do { } while (0)
#define dma_unmap_len(PTR, LEN_NAME)             (0)
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)    do { } while (0)
#endif

#endif
