/*
 * Dynamic DMA mapping support.
 *
 * This implementation is a fallback for platforms that do not support
 * I/O TLBs (aka DMA address translation hardware).
 * Copyright (C) 2000 Asit Mallick <Asit.K.Mallick@intel.com>
 * Copyright (C) 2000 Goutham Rao <goutham.rao@intel.com>
 * Copyright (C) 2000, 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 03/05/07 davidm	Switch from PCI-DMA to generic device DMA API.
 * 00/12/13 davidm	Rename to swiotlb.c and add mark_clean() to avoid
 *			unnecessary i-cache flushing.
 * 04/07/.. ak		Better overflow handling. Assorted fixes.
 * 05/09/10 linville	Add support for syncing ranges, support syncing for
 *			DMA_BIDIRECTIONAL mappings, miscellaneous cleanup.
 */

#include <linux/cache.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ctype.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/scatterlist.h>

#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/iommu-helper.h>

#define OFFSET(val,align) ((unsigned long)	\
	                   ( (val) & ( (align) - 1)))

#define SG_ENT_VIRT_ADDRESS(sg)	(sg_virt((sg)))
#define SG_ENT_PHYS_ADDRESS(sg)	virt_to_bus(SG_ENT_VIRT_ADDRESS(sg))

/*
 * Maximum allowable number of contiguous slabs to map,
 * must be a power of 2.  What is the appropriate value ?
 * The complexity of {map,unmap}_single is linearly dependent on this value.
 */
#define IO_TLB_SEGSIZE	128

/*
 * log of the size of each IO TLB slab.  The number of slabs is command line
 * controllable.
 */
#define IO_TLB_SHIFT 11

#define SLABS_PER_PAGE (1 << (PAGE_SHIFT - IO_TLB_SHIFT))

/*
 * Minimum IO TLB size to bother booting with.  Systems with mainly
 * 64bit capable cards will only lightly use the swiotlb.  If we can't
 * allocate a contiguous 1MB, we're probably in trouble anyway.
 */
#define IO_TLB_MIN_SLABS ((1<<20) >> IO_TLB_SHIFT)

/*
 * Enumeration for sync targets
 */
enum dma_sync_target {
	SYNC_FOR_CPU = 0,
	SYNC_FOR_DEVICE = 1,
};

int swiotlb_force;

/*
 * Used to do a quick range check in swiotlb_unmap_single and
 * swiotlb_sync_single_*, to see if the memory was in fact allocated by this
 * API.
 */
static char *io_tlb_start, *io_tlb_end;

/*
 * The number of IO TLB blocks (in groups of 64) betweeen io_tlb_start and
 * io_tlb_end.  This is command line adjustable via setup_io_tlb_npages.
 */
static unsigned long io_tlb_nslabs;

/*
 * When the IOMMU overflows we return a fallback buffer. This sets the size.
 */
static unsigned long io_tlb_overflow = 32*1024;

void *io_tlb_overflow_buffer;

/*
 * This is a free list describing the number of free entries available from
 * each index
 */
static unsigned int *io_tlb_list;
static unsigned int io_tlb_index;

/*
 * We need to save away the original address corresponding to a mapped entry
 * for the sync operations.
 */
static unsigned char **io_tlb_orig_addr;

/*
 * Protect the above data structures in the map and unmap calls
 */
static DEFINE_SPINLOCK(io_tlb_lock);

static int __init
setup_io_tlb_npages(char *str)
{
	if (isdigit(*str)) {
		io_tlb_nslabs = simple_strtoul(str, &str, 0);
		/* avoid tail segment of size < IO_TLB_SEGSIZE */
		io_tlb_nslabs = ALIGN(io_tlb_nslabs, IO_TLB_SEGSIZE);
	}
	if (*str == ',')
		++str;
	if (!strcmp(str, "force"))
		swiotlb_force = 1;
	return 1;
}
__setup("swiotlb=", setup_io_tlb_npages);
/* make io_tlb_overflow tunable too? */

/*
 * Statically reserve bounce buffer space and initialize bounce buffer data
 * structures for the software IO TLB used to implement the DMA API.
 */
void __init
swiotlb_init_with_default_size(size_t default_size)
{
	unsigned long i, bytes;

	if (!io_tlb_nslabs) {
		io_tlb_nslabs = (default_size >> IO_TLB_SHIFT);
		io_tlb_nslabs = ALIGN(io_tlb_nslabs, IO_TLB_SEGSIZE);
	}

	bytes = io_tlb_nslabs << IO_TLB_SHIFT;

	/*
	 * Get IO TLB memory from the low pages
	 */
	io_tlb_start = alloc_bootmem_low_pages(bytes);
	if (!io_tlb_start)
		panic("Cannot allocate SWIOTLB buffer");
	io_tlb_end = io_tlb_start + bytes;

	/*
	 * Allocate and initialize the free list array.  This array is used
	 * to find contiguous free memory regions of size up to IO_TLB_SEGSIZE
	 * between io_tlb_start and io_tlb_end.
	 */
	io_tlb_list = alloc_bootmem(io_tlb_nslabs * sizeof(int));
	for (i = 0; i < io_tlb_nslabs; i++)
 		io_tlb_list[i] = IO_TLB_SEGSIZE - OFFSET(i, IO_TLB_SEGSIZE);
	io_tlb_index = 0;
	io_tlb_orig_addr = alloc_bootmem(io_tlb_nslabs * sizeof(char *));

	/*
	 * Get the overflow emergency buffer
	 */
	io_tlb_overflow_buffer = alloc_bootmem_low(io_tlb_overflow);
	if (!io_tlb_overflow_buffer)
		panic("Cannot allocate SWIOTLB overflow buffer!\n");

	printk(KERN_INFO "Placing software IO TLB between 0x%lx - 0x%lx\n",
	       virt_to_bus(io_tlb_start), virt_to_bus(io_tlb_end));
}

void __init
swiotlb_init(void)
{
	swiotlb_init_with_default_size(64 * (1<<20));	/* default to 64MB */
}

/*
 * Systems with larger DMA zones (those that don't support ISA) can
 * initialize the swiotlb later using the slab allocator if needed.
 * This should be just like above, but with some error catching.
 */
int
swiotlb_late_init_with_default_size(size_t default_size)
{
	unsigned long i, bytes, req_nslabs = io_tlb_nslabs;
	unsigned int order;

	if (!io_tlb_nslabs) {
		io_tlb_nslabs = (default_size >> IO_TLB_SHIFT);
		io_tlb_nslabs = ALIGN(io_tlb_nslabs, IO_TLB_SEGSIZE);
	}

	/*
	 * Get IO TLB memory from the low pages
	 */
	order = get_order(io_tlb_nslabs << IO_TLB_SHIFT);
	io_tlb_nslabs = SLABS_PER_PAGE << order;
	bytes = io_tlb_nslabs << IO_TLB_SHIFT;

	while ((SLABS_PER_PAGE << order) > IO_TLB_MIN_SLABS) {
		io_tlb_start = (char *)__get_free_pages(GFP_DMA | __GFP_NOWARN,
		                                        order);
		if (io_tlb_start)
			break;
		order--;
	}

	if (!io_tlb_start)
		goto cleanup1;

	if (order != get_order(bytes)) {
		printk(KERN_WARNING "Warning: only able to allocate %ld MB "
		       "for software IO TLB\n", (PAGE_SIZE << order) >> 20);
		io_tlb_nslabs = SLABS_PER_PAGE << order;
		bytes = io_tlb_nslabs << IO_TLB_SHIFT;
	}
	io_tlb_end = io_tlb_start + bytes;
	memset(io_tlb_start, 0, bytes);

	/*
	 * Allocate and initialize the free list array.  This array is used
	 * to find contiguous free memory regions of size up to IO_TLB_SEGSIZE
	 * between io_tlb_start and io_tlb_end.
	 */
	io_tlb_list = (unsigned int *)__get_free_pages(GFP_KERNEL,
	                              get_order(io_tlb_nslabs * sizeof(int)));
	if (!io_tlb_list)
		goto cleanup2;

	for (i = 0; i < io_tlb_nslabs; i++)
 		io_tlb_list[i] = IO_TLB_SEGSIZE - OFFSET(i, IO_TLB_SEGSIZE);
	io_tlb_index = 0;

	io_tlb_orig_addr = (unsigned char **)__get_free_pages(GFP_KERNEL,
	                           get_order(io_tlb_nslabs * sizeof(char *)));
	if (!io_tlb_orig_addr)
		goto cleanup3;

	memset(io_tlb_orig_addr, 0, io_tlb_nslabs * sizeof(char *));

	/*
	 * Get the overflow emergency buffer
	 */
	io_tlb_overflow_buffer = (void *)__get_free_pages(GFP_DMA,
	                                          get_order(io_tlb_overflow));
	if (!io_tlb_overflow_buffer)
		goto cleanup4;

	printk(KERN_INFO "Placing %luMB software IO TLB between 0x%lx - "
	       "0x%lx\n", bytes >> 20,
	       virt_to_bus(io_tlb_start), virt_to_bus(io_tlb_end));

	return 0;

cleanup4:
	free_pages((unsigned long)io_tlb_orig_addr, get_order(io_tlb_nslabs *
	                                                      sizeof(char *)));
	io_tlb_orig_addr = NULL;
cleanup3:
	free_pages((unsigned long)io_tlb_list, get_order(io_tlb_nslabs *
	                                                 sizeof(int)));
	io_tlb_list = NULL;
cleanup2:
	io_tlb_end = NULL;
	free_pages((unsigned long)io_tlb_start, order);
	io_tlb_start = NULL;
cleanup1:
	io_tlb_nslabs = req_nslabs;
	return -ENOMEM;
}

static int
address_needs_mapping(struct device *hwdev, dma_addr_t addr, size_t size)
{
	dma_addr_t mask = 0xffffffff;
	/* If the device has a mask, use it, otherwise default to 32 bits */
	if (hwdev && hwdev->dma_mask)
		mask = *hwdev->dma_mask;
	return !is_buffer_dma_capable(mask, addr, size);
}

static int is_swiotlb_buffer(char *addr)
{
	return addr >= io_tlb_start && addr < io_tlb_end;
}

/*
 * Allocates bounce buffer and returns its kernel virtual address.
 */
static void *
map_single(struct device *hwdev, char *buffer, size_t size, int dir)
{
	unsigned long flags;
	char *dma_addr;
	unsigned int nslots, stride, index, wrap;
	int i;
	unsigned long start_dma_addr;
	unsigned long mask;
	unsigned long offset_slots;
	unsigned long max_slots;

	mask = dma_get_seg_boundary(hwdev);
	start_dma_addr = virt_to_bus(io_tlb_start) & mask;

	offset_slots = ALIGN(start_dma_addr, 1 << IO_TLB_SHIFT) >> IO_TLB_SHIFT;
	max_slots = mask + 1
		    ? ALIGN(mask + 1, 1 << IO_TLB_SHIFT) >> IO_TLB_SHIFT
		    : 1UL << (BITS_PER_LONG - IO_TLB_SHIFT);

	/*
	 * For mappings greater than a page, we limit the stride (and
	 * hence alignment) to a page size.
	 */
	nslots = ALIGN(size, 1 << IO_TLB_SHIFT) >> IO_TLB_SHIFT;
	if (size > PAGE_SIZE)
		stride = (1 << (PAGE_SHIFT - IO_TLB_SHIFT));
	else
		stride = 1;

	BUG_ON(!nslots);

	/*
	 * Find suitable number of IO TLB entries size that will fit this
	 * request and allocate a buffer from that IO TLB pool.
	 */
	spin_lock_irqsave(&io_tlb_lock, flags);
	index = ALIGN(io_tlb_index, stride);
	if (index >= io_tlb_nslabs)
		index = 0;
	wrap = index;

	do {
		while (iommu_is_span_boundary(index, nslots, offset_slots,
					      max_slots)) {
			index += stride;
			if (index >= io_tlb_nslabs)
				index = 0;
			if (index == wrap)
				goto not_found;
		}

		/*
		 * If we find a slot that indicates we have 'nslots' number of
		 * contiguous buffers, we allocate the buffers from that slot
		 * and mark the entries as '0' indicating unavailable.
		 */
		if (io_tlb_list[index] >= nslots) {
			int count = 0;

			for (i = index; i < (int) (index + nslots); i++)
				io_tlb_list[i] = 0;
			for (i = index - 1; (OFFSET(i, IO_TLB_SEGSIZE) != IO_TLB_SEGSIZE - 1) && io_tlb_list[i]; i--)
				io_tlb_list[i] = ++count;
			dma_addr = io_tlb_start + (index << IO_TLB_SHIFT);

			/*
			 * Update the indices to avoid searching in the next
			 * round.
			 */
			io_tlb_index = ((index + nslots) < io_tlb_nslabs
					? (index + nslots) : 0);

			goto found;
		}
		index += stride;
		if (index >= io_tlb_nslabs)
			index = 0;
	} while (index != wrap);

not_found:
	spin_unlock_irqrestore(&io_tlb_lock, flags);
	return NULL;
found:
	spin_unlock_irqrestore(&io_tlb_lock, flags);

	/*
	 * Save away the mapping from the original address to the DMA address.
	 * This is needed when we sync the memory.  Then we sync the buffer if
	 * needed.
	 */
	for (i = 0; i < nslots; i++)
		io_tlb_orig_addr[index+i] = buffer + (i << IO_TLB_SHIFT);
	if (dir == DMA_TO_DEVICE || dir == DMA_BIDIRECTIONAL)
		memcpy(dma_addr, buffer, size);

	return dma_addr;
}

/*
 * dma_addr is the kernel virtual address of the bounce buffer to unmap.
 */
static void
unmap_single(struct device *hwdev, char *dma_addr, size_t size, int dir)
{
	unsigned long flags;
	int i, count, nslots = ALIGN(size, 1 << IO_TLB_SHIFT) >> IO_TLB_SHIFT;
	int index = (dma_addr - io_tlb_start) >> IO_TLB_SHIFT;
	char *buffer = io_tlb_orig_addr[index];

	/*
	 * First, sync the memory before unmapping the entry
	 */
	if (buffer && ((dir == DMA_FROM_DEVICE) || (dir == DMA_BIDIRECTIONAL)))
		/*
		 * bounce... copy the data back into the original buffer * and
		 * delete the bounce buffer.
		 */
		memcpy(buffer, dma_addr, size);

	/*
	 * Return the buffer to the free list by setting the corresponding
	 * entries to indicate the number of contigous entries available.
	 * While returning the entries to the free list, we merge the entries
	 * with slots below and above the pool being returned.
	 */
	spin_lock_irqsave(&io_tlb_lock, flags);
	{
		count = ((index + nslots) < ALIGN(index + 1, IO_TLB_SEGSIZE) ?
			 io_tlb_list[index + nslots] : 0);
		/*
		 * Step 1: return the slots to the free list, merging the
		 * slots with superceeding slots
		 */
		for (i = index + nslots - 1; i >= index; i--)
			io_tlb_list[i] = ++count;
		/*
		 * Step 2: merge the returned slots with the preceding slots,
		 * if available (non zero)
		 */
		for (i = index - 1; (OFFSET(i, IO_TLB_SEGSIZE) != IO_TLB_SEGSIZE -1) && io_tlb_list[i]; i--)
			io_tlb_list[i] = ++count;
	}
	spin_unlock_irqrestore(&io_tlb_lock, flags);
}

static void
sync_single(struct device *hwdev, char *dma_addr, size_t size,
	    int dir, int target)
{
	int index = (dma_addr - io_tlb_start) >> IO_TLB_SHIFT;
	char *buffer = io_tlb_orig_addr[index];

	buffer += ((unsigned long)dma_addr & ((1 << IO_TLB_SHIFT) - 1));

	switch (target) {
	case SYNC_FOR_CPU:
		if (likely(dir == DMA_FROM_DEVICE || dir == DMA_BIDIRECTIONAL))
			memcpy(buffer, dma_addr, size);
		else
			BUG_ON(dir != DMA_TO_DEVICE);
		break;
	case SYNC_FOR_DEVICE:
		if (likely(dir == DMA_TO_DEVICE || dir == DMA_BIDIRECTIONAL))
			memcpy(dma_addr, buffer, size);
		else
			BUG_ON(dir != DMA_FROM_DEVICE);
		break;
	default:
		BUG();
	}
}

void *
swiotlb_alloc_coherent(struct device *hwdev, size_t size,
		       dma_addr_t *dma_handle, gfp_t flags)
{
	dma_addr_t dev_addr;
	void *ret;
	int order = get_order(size);

	ret = (void *)__get_free_pages(flags, order);
	if (ret && address_needs_mapping(hwdev, virt_to_bus(ret), size)) {
		/*
		 * The allocated memory isn't reachable by the device.
		 * Fall back on swiotlb_map_single().
		 */
		free_pages((unsigned long) ret, order);
		ret = NULL;
	}
	if (!ret) {
		/*
		 * We are either out of memory or the device can't DMA
		 * to GFP_DMA memory; fall back on
		 * swiotlb_map_single(), which will grab memory from
		 * the lowest available address range.
		 */
		ret = map_single(hwdev, NULL, size, DMA_FROM_DEVICE);
		if (!ret)
			return NULL;
	}

	memset(ret, 0, size);
	dev_addr = virt_to_bus(ret);

	/* Confirm address can be DMA'd by device */
	if (address_needs_mapping(hwdev, dev_addr, size)) {
		printk("hwdev DMA mask = 0x%016Lx, dev_addr = 0x%016Lx\n",
		       (unsigned long long)*hwdev->dma_mask,
		       (unsigned long long)dev_addr);
		panic("swiotlb_alloc_coherent: allocated memory is out of "
		      "range for device");
	}
	*dma_handle = dev_addr;
	return ret;
}

void
swiotlb_free_coherent(struct device *hwdev, size_t size, void *vaddr,
		      dma_addr_t dma_handle)
{
	WARN_ON(irqs_disabled());
	if (!is_swiotlb_buffer(vaddr))
		free_pages((unsigned long) vaddr, get_order(size));
	else
		/* DMA_TO_DEVICE to avoid memcpy in unmap_single */
		unmap_single(hwdev, vaddr, size, DMA_TO_DEVICE);
}

static void
swiotlb_full(struct device *dev, size_t size, int dir, int do_panic)
{
	/*
	 * Ran out of IOMMU space for this operation. This is very bad.
	 * Unfortunately the drivers cannot handle this operation properly.
	 * unless they check for dma_mapping_error (most don't)
	 * When the mapping is small enough return a static buffer to limit
	 * the damage, or panic when the transfer is too big.
	 */
	printk(KERN_ERR "DMA: Out of SW-IOMMU space for %zu bytes at "
	       "device %s\n", size, dev ? dev->bus_id : "?");

	if (size > io_tlb_overflow && do_panic) {
		if (dir == DMA_FROM_DEVICE || dir == DMA_BIDIRECTIONAL)
			panic("DMA: Memory would be corrupted\n");
		if (dir == DMA_TO_DEVICE || dir == DMA_BIDIRECTIONAL)
			panic("DMA: Random memory would be DMAed\n");
	}
}

/*
 * Map a single buffer of the indicated size for DMA in streaming mode.  The
 * physical address to use is returned.
 *
 * Once the device is given the dma address, the device owns this memory until
 * either swiotlb_unmap_single or swiotlb_dma_sync_single is performed.
 */
dma_addr_t
swiotlb_map_single_attrs(struct device *hwdev, void *ptr, size_t size,
			 int dir, struct dma_attrs *attrs)
{
	dma_addr_t dev_addr = virt_to_bus(ptr);
	void *map;

	BUG_ON(dir == DMA_NONE);
	/*
	 * If the pointer passed in happens to be in the device's DMA window,
	 * we can safely return the device addr and not worry about bounce
	 * buffering it.
	 */
	if (!address_needs_mapping(hwdev, dev_addr, size) && !swiotlb_force)
		return dev_addr;

	/*
	 * Oh well, have to allocate and map a bounce buffer.
	 */
	map = map_single(hwdev, ptr, size, dir);
	if (!map) {
		swiotlb_full(hwdev, size, dir, 1);
		map = io_tlb_overflow_buffer;
	}

	dev_addr = virt_to_bus(map);

	/*
	 * Ensure that the address returned is DMA'ble
	 */
	if (address_needs_mapping(hwdev, dev_addr, size))
		panic("map_single: bounce buffer is not DMA'ble");

	return dev_addr;
}
EXPORT_SYMBOL(swiotlb_map_single_attrs);

dma_addr_t
swiotlb_map_single(struct device *hwdev, void *ptr, size_t size, int dir)
{
	return swiotlb_map_single_attrs(hwdev, ptr, size, dir, NULL);
}

/*
 * Unmap a single streaming mode DMA translation.  The dma_addr and size must
 * match what was provided for in a previous swiotlb_map_single call.  All
 * other usages are undefined.
 *
 * After this call, reads by the cpu to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
void
swiotlb_unmap_single_attrs(struct device *hwdev, dma_addr_t dev_addr,
			   size_t size, int dir, struct dma_attrs *attrs)
{
	char *dma_addr = bus_to_virt(dev_addr);

	BUG_ON(dir == DMA_NONE);
	if (is_swiotlb_buffer(dma_addr))
		unmap_single(hwdev, dma_addr, size, dir);
	else if (dir == DMA_FROM_DEVICE)
		dma_mark_clean(dma_addr, size);
}
EXPORT_SYMBOL(swiotlb_unmap_single_attrs);

void
swiotlb_unmap_single(struct device *hwdev, dma_addr_t dev_addr, size_t size,
		     int dir)
{
	return swiotlb_unmap_single_attrs(hwdev, dev_addr, size, dir, NULL);
}
/*
 * Make physical memory consistent for a single streaming mode DMA translation
 * after a transfer.
 *
 * If you perform a swiotlb_map_single() but wish to interrogate the buffer
 * using the cpu, yet do not wish to teardown the dma mapping, you must
 * call this function before doing so.  At the next point you give the dma
 * address back to the card, you must first perform a
 * swiotlb_dma_sync_for_device, and then the device again owns the buffer
 */
static void
swiotlb_sync_single(struct device *hwdev, dma_addr_t dev_addr,
		    size_t size, int dir, int target)
{
	char *dma_addr = bus_to_virt(dev_addr);

	BUG_ON(dir == DMA_NONE);
	if (is_swiotlb_buffer(dma_addr))
		sync_single(hwdev, dma_addr, size, dir, target);
	else if (dir == DMA_FROM_DEVICE)
		dma_mark_clean(dma_addr, size);
}

void
swiotlb_sync_single_for_cpu(struct device *hwdev, dma_addr_t dev_addr,
			    size_t size, int dir)
{
	swiotlb_sync_single(hwdev, dev_addr, size, dir, SYNC_FOR_CPU);
}

void
swiotlb_sync_single_for_device(struct device *hwdev, dma_addr_t dev_addr,
			       size_t size, int dir)
{
	swiotlb_sync_single(hwdev, dev_addr, size, dir, SYNC_FOR_DEVICE);
}

/*
 * Same as above, but for a sub-range of the mapping.
 */
static void
swiotlb_sync_single_range(struct device *hwdev, dma_addr_t dev_addr,
			  unsigned long offset, size_t size,
			  int dir, int target)
{
	char *dma_addr = bus_to_virt(dev_addr) + offset;

	BUG_ON(dir == DMA_NONE);
	if (is_swiotlb_buffer(dma_addr))
		sync_single(hwdev, dma_addr, size, dir, target);
	else if (dir == DMA_FROM_DEVICE)
		dma_mark_clean(dma_addr, size);
}

void
swiotlb_sync_single_range_for_cpu(struct device *hwdev, dma_addr_t dev_addr,
				  unsigned long offset, size_t size, int dir)
{
	swiotlb_sync_single_range(hwdev, dev_addr, offset, size, dir,
				  SYNC_FOR_CPU);
}

void
swiotlb_sync_single_range_for_device(struct device *hwdev, dma_addr_t dev_addr,
				     unsigned long offset, size_t size, int dir)
{
	swiotlb_sync_single_range(hwdev, dev_addr, offset, size, dir,
				  SYNC_FOR_DEVICE);
}

void swiotlb_unmap_sg_attrs(struct device *, struct scatterlist *, int, int,
			    struct dma_attrs *);
/*
 * Map a set of buffers described by scatterlist in streaming mode for DMA.
 * This is the scatter-gather version of the above swiotlb_map_single
 * interface.  Here the scatter gather list elements are each tagged with the
 * appropriate dma address and length.  They are obtained via
 * sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for swiotlb_map_single are the
 * same here.
 */
int
swiotlb_map_sg_attrs(struct device *hwdev, struct scatterlist *sgl, int nelems,
		     int dir, struct dma_attrs *attrs)
{
	struct scatterlist *sg;
	void *addr;
	dma_addr_t dev_addr;
	int i;

	BUG_ON(dir == DMA_NONE);

	for_each_sg(sgl, sg, nelems, i) {
		addr = SG_ENT_VIRT_ADDRESS(sg);
		dev_addr = virt_to_bus(addr);
		if (swiotlb_force ||
		    address_needs_mapping(hwdev, dev_addr, sg->length)) {
			void *map = map_single(hwdev, addr, sg->length, dir);
			if (!map) {
				/* Don't panic here, we expect map_sg users
				   to do proper error handling. */
				swiotlb_full(hwdev, sg->length, dir, 0);
				swiotlb_unmap_sg_attrs(hwdev, sgl, i, dir,
						       attrs);
				sgl[0].dma_length = 0;
				return 0;
			}
			sg->dma_address = virt_to_bus(map);
		} else
			sg->dma_address = dev_addr;
		sg->dma_length = sg->length;
	}
	return nelems;
}
EXPORT_SYMBOL(swiotlb_map_sg_attrs);

int
swiotlb_map_sg(struct device *hwdev, struct scatterlist *sgl, int nelems,
	       int dir)
{
	return swiotlb_map_sg_attrs(hwdev, sgl, nelems, dir, NULL);
}

/*
 * Unmap a set of streaming mode DMA translations.  Again, cpu read rules
 * concerning calls here are the same as for swiotlb_unmap_single() above.
 */
void
swiotlb_unmap_sg_attrs(struct device *hwdev, struct scatterlist *sgl,
		       int nelems, int dir, struct dma_attrs *attrs)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(dir == DMA_NONE);

	for_each_sg(sgl, sg, nelems, i) {
		if (sg->dma_address != SG_ENT_PHYS_ADDRESS(sg))
			unmap_single(hwdev, bus_to_virt(sg->dma_address),
				     sg->dma_length, dir);
		else if (dir == DMA_FROM_DEVICE)
			dma_mark_clean(SG_ENT_VIRT_ADDRESS(sg), sg->dma_length);
	}
}
EXPORT_SYMBOL(swiotlb_unmap_sg_attrs);

void
swiotlb_unmap_sg(struct device *hwdev, struct scatterlist *sgl, int nelems,
		 int dir)
{
	return swiotlb_unmap_sg_attrs(hwdev, sgl, nelems, dir, NULL);
}

/*
 * Make physical memory consistent for a set of streaming mode DMA translations
 * after a transfer.
 *
 * The same as swiotlb_sync_single_* but for a scatter-gather list, same rules
 * and usage.
 */
static void
swiotlb_sync_sg(struct device *hwdev, struct scatterlist *sgl,
		int nelems, int dir, int target)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(dir == DMA_NONE);

	for_each_sg(sgl, sg, nelems, i) {
		if (sg->dma_address != SG_ENT_PHYS_ADDRESS(sg))
			sync_single(hwdev, bus_to_virt(sg->dma_address),
				    sg->dma_length, dir, target);
		else if (dir == DMA_FROM_DEVICE)
			dma_mark_clean(SG_ENT_VIRT_ADDRESS(sg), sg->dma_length);
	}
}

void
swiotlb_sync_sg_for_cpu(struct device *hwdev, struct scatterlist *sg,
			int nelems, int dir)
{
	swiotlb_sync_sg(hwdev, sg, nelems, dir, SYNC_FOR_CPU);
}

void
swiotlb_sync_sg_for_device(struct device *hwdev, struct scatterlist *sg,
			   int nelems, int dir)
{
	swiotlb_sync_sg(hwdev, sg, nelems, dir, SYNC_FOR_DEVICE);
}

int
swiotlb_dma_mapping_error(struct device *hwdev, dma_addr_t dma_addr)
{
	return (dma_addr == virt_to_bus(io_tlb_overflow_buffer));
}

/*
 * Return whether the given device DMA address mask can be supported
 * properly.  For example, if your device can only drive the low 24-bits
 * during bus mastering, then you would pass 0x00ffffff as the mask to
 * this function.
 */
int
swiotlb_dma_supported(struct device *hwdev, u64 mask)
{
	return virt_to_bus(io_tlb_end - 1) <= mask;
}

EXPORT_SYMBOL(swiotlb_map_single);
EXPORT_SYMBOL(swiotlb_unmap_single);
EXPORT_SYMBOL(swiotlb_map_sg);
EXPORT_SYMBOL(swiotlb_unmap_sg);
EXPORT_SYMBOL(swiotlb_sync_single_for_cpu);
EXPORT_SYMBOL(swiotlb_sync_single_for_device);
EXPORT_SYMBOL_GPL(swiotlb_sync_single_range_for_cpu);
EXPORT_SYMBOL_GPL(swiotlb_sync_single_range_for_device);
EXPORT_SYMBOL(swiotlb_sync_sg_for_cpu);
EXPORT_SYMBOL(swiotlb_sync_sg_for_device);
EXPORT_SYMBOL(swiotlb_dma_mapping_error);
EXPORT_SYMBOL(swiotlb_alloc_coherent);
EXPORT_SYMBOL(swiotlb_free_coherent);
EXPORT_SYMBOL(swiotlb_dma_supported);
