#ifndef ASMARM_DMA_MAPPING_H
#define ASMARM_DMA_MAPPING_H

#ifdef __KERNEL__

#include <linux/mm.h> /* need struct page */

#include <asm/scatterlist.h>

/*
 * DMA-consistent mapping functions.  These allocate/free a region of
 * uncached, unwrite-buffered mapped memory space for use with DMA
 * devices.  This is the "generic" version.  The PCI specific version
 * is in pci.h
 *
 * Note: Drivers should NOT use this function directly, as it will break
 * platforms with CONFIG_DMABOUNCE.
 * Use the driver DMA support - see dma-mapping.h (dma_sync_*)
 */
extern void consistent_sync(const void *kaddr, size_t size, int rw);

/*
 * Return whether the given device DMA address mask can be supported
 * properly.  For example, if your device can only drive the low 24-bits
 * during bus mastering, then you would pass 0x00ffffff as the mask
 * to this function.
 *
 * FIXME: This should really be a platform specific issue - we should
 * return false if GFP_DMA allocations may not satisfy the supplied 'mask'.
 */
static inline int dma_supported(struct device *dev, u64 mask)
{
	return dev->dma_mask && *dev->dma_mask != 0;
}

static inline int dma_set_mask(struct device *dev, u64 dma_mask)
{
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;

	*dev->dma_mask = dma_mask;

	return 0;
}

static inline int dma_get_cache_alignment(void)
{
	return 32;
}

static inline int dma_is_consistent(struct device *dev, dma_addr_t handle)
{
	return !!arch_is_coherent();
}

/*
 * DMA errors are defined by all-bits-set in the DMA address.
 */
static inline int dma_mapping_error(dma_addr_t dma_addr)
{
	return dma_addr == ~0;
}

/*
 * Dummy noncoherent implementation.  We don't provide a dma_cache_sync
 * function so drivers using this API are highlighted with build warnings.
 */
static inline void *
dma_alloc_noncoherent(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp)
{
	return NULL;
}

static inline void
dma_free_noncoherent(struct device *dev, size_t size, void *cpu_addr,
		     dma_addr_t handle)
{
}

/**
 * dma_alloc_coherent - allocate consistent memory for DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @size: required memory size
 * @handle: bus-specific DMA address
 *
 * Allocate some uncached, unbuffered memory for a device for
 * performing DMA.  This function allocates pages, and will
 * return the CPU-viewed address, and sets @handle to be the
 * device-viewed address.
 */
extern void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp);

/**
 * dma_free_coherent - free memory allocated by dma_alloc_coherent
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @size: size of memory originally requested in dma_alloc_coherent
 * @cpu_addr: CPU-view address returned from dma_alloc_coherent
 * @handle: device-view address returned from dma_alloc_coherent
 *
 * Free (and unmap) a DMA buffer previously allocated by
 * dma_alloc_coherent().
 *
 * References to memory and mappings associated with cpu_addr/handle
 * during and after this call executing are illegal.
 */
extern void
dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
		  dma_addr_t handle);

/**
 * dma_mmap_coherent - map a coherent DMA allocation into user space
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @vma: vm_area_struct describing requested user mapping
 * @cpu_addr: kernel CPU-view address returned from dma_alloc_coherent
 * @handle: device-view address returned from dma_alloc_coherent
 * @size: size of memory originally requested in dma_alloc_coherent
 *
 * Map a coherent DMA buffer previously allocated by dma_alloc_coherent
 * into user space.  The coherent DMA buffer must not be freed by the
 * driver until the user space mapping has been released.
 */
int dma_mmap_coherent(struct device *dev, struct vm_area_struct *vma,
		      void *cpu_addr, dma_addr_t handle, size_t size);


/**
 * dma_alloc_writecombine - allocate writecombining memory for DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @size: required memory size
 * @handle: bus-specific DMA address
 *
 * Allocate some uncached, buffered memory for a device for
 * performing DMA.  This function allocates pages, and will
 * return the CPU-viewed address, and sets @handle to be the
 * device-viewed address.
 */
extern void *
dma_alloc_writecombine(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp);

#define dma_free_writecombine(dev,size,cpu_addr,handle) \
	dma_free_coherent(dev,size,cpu_addr,handle)

int dma_mmap_writecombine(struct device *dev, struct vm_area_struct *vma,
			  void *cpu_addr, dma_addr_t handle, size_t size);


/**
 * dma_map_single - map a single buffer for streaming DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @cpu_addr: CPU direct mapped address of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Ensure that any data held in the cache is appropriately discarded
 * or written back.
 *
 * The device owns this memory once this call has completed.  The CPU
 * can regain ownership by calling dma_unmap_single() or
 * dma_sync_single_for_cpu().
 */
#ifndef CONFIG_DMABOUNCE
static inline dma_addr_t
dma_map_single(struct device *dev, void *cpu_addr, size_t size,
	       enum dma_data_direction dir)
{
	if (!arch_is_coherent())
		consistent_sync(cpu_addr, size, dir);

	return virt_to_dma(dev, (unsigned long)cpu_addr);
}
#else
extern dma_addr_t dma_map_single(struct device *,void *, size_t, enum dma_data_direction);
#endif

/**
 * dma_map_page - map a portion of a page for streaming DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @page: page that buffer resides in
 * @offset: offset into page for start of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Ensure that any data held in the cache is appropriately discarded
 * or written back.
 *
 * The device owns this memory once this call has completed.  The CPU
 * can regain ownership by calling dma_unmap_page() or
 * dma_sync_single_for_cpu().
 */
static inline dma_addr_t
dma_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size,
	     enum dma_data_direction dir)
{
	return dma_map_single(dev, page_address(page) + offset, size, (int)dir);
}

/**
 * dma_unmap_single - unmap a single buffer previously mapped
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @handle: DMA address of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Unmap a single streaming mode DMA translation.  The handle and size
 * must match what was provided in the previous dma_map_single() call.
 * All other usages are undefined.
 *
 * After this call, reads by the CPU to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
#ifndef CONFIG_DMABOUNCE
static inline void
dma_unmap_single(struct device *dev, dma_addr_t handle, size_t size,
		 enum dma_data_direction dir)
{
	/* nothing to do */
}
#else
extern void dma_unmap_single(struct device *, dma_addr_t, size_t, enum dma_data_direction);
#endif

/**
 * dma_unmap_page - unmap a buffer previously mapped through dma_map_page()
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @handle: DMA address of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Unmap a single streaming mode DMA translation.  The handle and size
 * must match what was provided in the previous dma_map_single() call.
 * All other usages are undefined.
 *
 * After this call, reads by the CPU to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
static inline void
dma_unmap_page(struct device *dev, dma_addr_t handle, size_t size,
	       enum dma_data_direction dir)
{
	dma_unmap_single(dev, handle, size, (int)dir);
}

/**
 * dma_map_sg - map a set of SG buffers for streaming mode DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to map
 * @dir: DMA transfer direction
 *
 * Map a set of buffers described by scatterlist in streaming
 * mode for DMA.  This is the scatter-gather version of the
 * above dma_map_single interface.  Here the scatter gather list
 * elements are each tagged with the appropriate dma address
 * and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for dma_map_single are
 * the same here.
 */
#ifndef CONFIG_DMABOUNCE
static inline int
dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	   enum dma_data_direction dir)
{
	int i;

	for (i = 0; i < nents; i++, sg++) {
		char *virt;

		sg->dma_address = page_to_dma(dev, sg->page) + sg->offset;
		virt = page_address(sg->page) + sg->offset;

		if (!arch_is_coherent())
			consistent_sync(virt, sg->length, dir);
	}

	return nents;
}
#else
extern int dma_map_sg(struct device *, struct scatterlist *, int, enum dma_data_direction);
#endif

/**
 * dma_unmap_sg - unmap a set of SG buffers mapped by dma_map_sg
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to map
 * @dir: DMA transfer direction
 *
 * Unmap a set of streaming mode DMA translations.
 * Again, CPU read rules concerning calls here are the same as for
 * dma_unmap_single() above.
 */
#ifndef CONFIG_DMABOUNCE
static inline void
dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
	     enum dma_data_direction dir)
{

	/* nothing to do */
}
#else
extern void dma_unmap_sg(struct device *, struct scatterlist *, int, enum dma_data_direction);
#endif


/**
 * dma_sync_single_for_cpu
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @handle: DMA address of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Make physical memory consistent for a single streaming mode DMA
 * translation after a transfer.
 *
 * If you perform a dma_map_single() but wish to interrogate the
 * buffer using the cpu, yet do not wish to teardown the PCI dma
 * mapping, you must call this function before doing so.  At the
 * next point you give the PCI dma address back to the card, you
 * must first the perform a dma_sync_for_device, and then the
 * device again owns the buffer.
 */
#ifndef CONFIG_DMABOUNCE
static inline void
dma_sync_single_for_cpu(struct device *dev, dma_addr_t handle, size_t size,
			enum dma_data_direction dir)
{
	if (!arch_is_coherent())
		consistent_sync((void *)dma_to_virt(dev, handle), size, dir);
}

static inline void
dma_sync_single_for_device(struct device *dev, dma_addr_t handle, size_t size,
			   enum dma_data_direction dir)
{
	if (!arch_is_coherent())
		consistent_sync((void *)dma_to_virt(dev, handle), size, dir);
}
#else
extern void dma_sync_single_for_cpu(struct device*, dma_addr_t, size_t, enum dma_data_direction);
extern void dma_sync_single_for_device(struct device*, dma_addr_t, size_t, enum dma_data_direction);
#endif


/**
 * dma_sync_sg_for_cpu
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to map
 * @dir: DMA transfer direction
 *
 * Make physical memory consistent for a set of streaming
 * mode DMA translations after a transfer.
 *
 * The same as dma_sync_single_for_* but for a scatter-gather list,
 * same rules and usage.
 */
#ifndef CONFIG_DMABOUNCE
static inline void
dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nents,
		    enum dma_data_direction dir)
{
	int i;

	for (i = 0; i < nents; i++, sg++) {
		char *virt = page_address(sg->page) + sg->offset;
		if (!arch_is_coherent())
			consistent_sync(virt, sg->length, dir);
	}
}

static inline void
dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg, int nents,
		       enum dma_data_direction dir)
{
	int i;

	for (i = 0; i < nents; i++, sg++) {
		char *virt = page_address(sg->page) + sg->offset;
		if (!arch_is_coherent())
			consistent_sync(virt, sg->length, dir);
	}
}
#else
extern void dma_sync_sg_for_cpu(struct device*, struct scatterlist*, int, enum dma_data_direction);
extern void dma_sync_sg_for_device(struct device*, struct scatterlist*, int, enum dma_data_direction);
#endif

#ifdef CONFIG_DMABOUNCE
/*
 * For SA-1111, IXP425, and ADI systems  the dma-mapping functions are "magic"
 * and utilize bounce buffers as needed to work around limited DMA windows.
 *
 * On the SA-1111, a bug limits DMA to only certain regions of RAM.
 * On the IXP425, the PCI inbound window is 64MB (256MB total RAM)
 * On some ADI engineering sytems, PCI inbound window is 32MB (12MB total RAM)
 *
 * The following are helper functions used by the dmabounce subystem
 *
 */

/**
 * dmabounce_register_dev
 *
 * @dev: valid struct device pointer
 * @small_buf_size: size of buffers to use with small buffer pool
 * @large_buf_size: size of buffers to use with large buffer pool (can be 0)
 *
 * This function should be called by low-level platform code to register
 * a device as requireing DMA buffer bouncing. The function will allocate
 * appropriate DMA pools for the device.
 *
 */
extern int dmabounce_register_dev(struct device *, unsigned long, unsigned long);

/**
 * dmabounce_unregister_dev
 *
 * @dev: valid struct device pointer
 *
 * This function should be called by low-level platform code when device
 * that was previously registered with dmabounce_register_dev is removed
 * from the system.
 *
 */
extern void dmabounce_unregister_dev(struct device *);

/**
 * dma_needs_bounce
 *
 * @dev: valid struct device pointer
 * @dma_handle: dma_handle of unbounced buffer
 * @size: size of region being mapped
 *
 * Platforms that utilize the dmabounce mechanism must implement
 * this function.
 *
 * The dmabounce routines call this function whenever a dma-mapping
 * is requested to determine whether a given buffer needs to be bounced
 * or not. The function must return 0 if the the buffer is OK for
 * DMA access and 1 if the buffer needs to be bounced.
 *
 */
extern int dma_needs_bounce(struct device*, dma_addr_t, size_t);
#endif /* CONFIG_DMABOUNCE */

#endif /* __KERNEL__ */
#endif
