// SPDX-License-Identifier: GPL-2.0-only
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
 * 08/12/11 beckyb	Add highmem support
 */

#define pr_fmt(fmt) "software IO TLB: " fmt

#include <linux/cache.h>
#include <linux/cc_platform.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/iommu-helper.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/pfn.h>
#include <linux/scatterlist.h>
#include <linux/set_memory.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/swiotlb.h>
#include <linux/types.h>
#ifdef CONFIG_DMA_RESTRICTED_POOL
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>
#include <linux/slab.h>
#endif

#define CREATE_TRACE_POINTS
#include <trace/events/swiotlb.h>

#define SLABS_PER_PAGE (1 << (PAGE_SHIFT - IO_TLB_SHIFT))

/*
 * Minimum IO TLB size to bother booting with.  Systems with mainly
 * 64bit capable cards will only lightly use the swiotlb.  If we can't
 * allocate a contiguous 1MB, we're probably in trouble anyway.
 */
#define IO_TLB_MIN_SLABS ((1<<20) >> IO_TLB_SHIFT)

#define INVALID_PHYS_ADDR (~(phys_addr_t)0)

struct io_tlb_slot {
	phys_addr_t orig_addr;
	size_t alloc_size;
	unsigned int list;
};

static bool swiotlb_force_bounce;
static bool swiotlb_force_disable;

struct io_tlb_mem io_tlb_default_mem;

phys_addr_t swiotlb_unencrypted_base;

static unsigned long default_nslabs = IO_TLB_DEFAULT_SIZE >> IO_TLB_SHIFT;
static unsigned long default_nareas;

/**
 * struct io_tlb_area - IO TLB memory area descriptor
 *
 * This is a single area with a single lock.
 *
 * @used:	The number of used IO TLB block.
 * @index:	The slot index to start searching in this area for next round.
 * @lock:	The lock to protect the above data structures in the map and
 *		unmap calls.
 */
struct io_tlb_area {
	unsigned long used;
	unsigned int index;
	spinlock_t lock;
};

/*
 * Round up number of slabs to the next power of 2. The last area is going
 * be smaller than the rest if default_nslabs is not power of two.
 * The number of slot in an area should be a multiple of IO_TLB_SEGSIZE,
 * otherwise a segment may span two or more areas. It conflicts with free
 * contiguous slots tracking: free slots are treated contiguous no matter
 * whether they cross an area boundary.
 *
 * Return true if default_nslabs is rounded up.
 */
static bool round_up_default_nslabs(void)
{
	if (!default_nareas)
		return false;

	if (default_nslabs < IO_TLB_SEGSIZE * default_nareas)
		default_nslabs = IO_TLB_SEGSIZE * default_nareas;
	else if (is_power_of_2(default_nslabs))
		return false;
	default_nslabs = roundup_pow_of_two(default_nslabs);
	return true;
}

static void swiotlb_adjust_nareas(unsigned int nareas)
{
	/* use a single area when non is specified */
	if (!nareas)
		nareas = 1;
	else if (!is_power_of_2(nareas))
		nareas = roundup_pow_of_two(nareas);

	default_nareas = nareas;

	pr_info("area num %d.\n", nareas);
	if (round_up_default_nslabs())
		pr_info("SWIOTLB bounce buffer size roundup to %luMB",
			(default_nslabs << IO_TLB_SHIFT) >> 20);
}

static int __init
setup_io_tlb_npages(char *str)
{
	if (isdigit(*str)) {
		/* avoid tail segment of size < IO_TLB_SEGSIZE */
		default_nslabs =
			ALIGN(simple_strtoul(str, &str, 0), IO_TLB_SEGSIZE);
	}
	if (*str == ',')
		++str;
	if (isdigit(*str))
		swiotlb_adjust_nareas(simple_strtoul(str, &str, 0));
	if (*str == ',')
		++str;
	if (!strcmp(str, "force"))
		swiotlb_force_bounce = true;
	else if (!strcmp(str, "noforce"))
		swiotlb_force_disable = true;

	return 0;
}
early_param("swiotlb", setup_io_tlb_npages);

unsigned long swiotlb_size_or_default(void)
{
	return default_nslabs << IO_TLB_SHIFT;
}

void __init swiotlb_adjust_size(unsigned long size)
{
	/*
	 * If swiotlb parameter has not been specified, give a chance to
	 * architectures such as those supporting memory encryption to
	 * adjust/expand SWIOTLB size for their use.
	 */
	if (default_nslabs != IO_TLB_DEFAULT_SIZE >> IO_TLB_SHIFT)
		return;

	size = ALIGN(size, IO_TLB_SIZE);
	default_nslabs = ALIGN(size >> IO_TLB_SHIFT, IO_TLB_SEGSIZE);
	if (round_up_default_nslabs())
		size = default_nslabs << IO_TLB_SHIFT;
	pr_info("SWIOTLB bounce buffer size adjusted to %luMB", size >> 20);
}

void swiotlb_print_info(void)
{
	struct io_tlb_mem *mem = &io_tlb_default_mem;

	if (!mem->nslabs) {
		pr_warn("No low mem\n");
		return;
	}

	pr_info("mapped [mem %pa-%pa] (%luMB)\n", &mem->start, &mem->end,
	       (mem->nslabs << IO_TLB_SHIFT) >> 20);
}

static inline unsigned long io_tlb_offset(unsigned long val)
{
	return val & (IO_TLB_SEGSIZE - 1);
}

static inline unsigned long nr_slots(u64 val)
{
	return DIV_ROUND_UP(val, IO_TLB_SIZE);
}

/*
 * Remap swioltb memory in the unencrypted physical address space
 * when swiotlb_unencrypted_base is set. (e.g. for Hyper-V AMD SEV-SNP
 * Isolation VMs).
 */
#ifdef CONFIG_HAS_IOMEM
static void *swiotlb_mem_remap(struct io_tlb_mem *mem, unsigned long bytes)
{
	void *vaddr = NULL;

	if (swiotlb_unencrypted_base) {
		phys_addr_t paddr = mem->start + swiotlb_unencrypted_base;

		vaddr = memremap(paddr, bytes, MEMREMAP_WB);
		if (!vaddr)
			pr_err("Failed to map the unencrypted memory %pa size %lx.\n",
			       &paddr, bytes);
	}

	return vaddr;
}
#else
static void *swiotlb_mem_remap(struct io_tlb_mem *mem, unsigned long bytes)
{
	return NULL;
}
#endif

/*
 * Early SWIOTLB allocation may be too early to allow an architecture to
 * perform the desired operations.  This function allows the architecture to
 * call SWIOTLB when the operations are possible.  It needs to be called
 * before the SWIOTLB memory is used.
 */
void __init swiotlb_update_mem_attributes(void)
{
	struct io_tlb_mem *mem = &io_tlb_default_mem;
	void *vaddr;
	unsigned long bytes;

	if (!mem->nslabs || mem->late_alloc)
		return;
	vaddr = phys_to_virt(mem->start);
	bytes = PAGE_ALIGN(mem->nslabs << IO_TLB_SHIFT);
	set_memory_decrypted((unsigned long)vaddr, bytes >> PAGE_SHIFT);

	mem->vaddr = swiotlb_mem_remap(mem, bytes);
	if (!mem->vaddr)
		mem->vaddr = vaddr;
}

static void swiotlb_init_io_tlb_mem(struct io_tlb_mem *mem, phys_addr_t start,
		unsigned long nslabs, unsigned int flags,
		bool late_alloc, unsigned int nareas)
{
	void *vaddr = phys_to_virt(start);
	unsigned long bytes = nslabs << IO_TLB_SHIFT, i;

	mem->nslabs = nslabs;
	mem->start = start;
	mem->end = mem->start + bytes;
	mem->late_alloc = late_alloc;
	mem->nareas = nareas;
	mem->area_nslabs = nslabs / mem->nareas;

	mem->force_bounce = swiotlb_force_bounce || (flags & SWIOTLB_FORCE);

	for (i = 0; i < mem->nareas; i++) {
		spin_lock_init(&mem->areas[i].lock);
		mem->areas[i].index = 0;
		mem->areas[i].used = 0;
	}

	for (i = 0; i < mem->nslabs; i++) {
		mem->slots[i].list = IO_TLB_SEGSIZE - io_tlb_offset(i);
		mem->slots[i].orig_addr = INVALID_PHYS_ADDR;
		mem->slots[i].alloc_size = 0;
	}

	/*
	 * If swiotlb_unencrypted_base is set, the bounce buffer memory will
	 * be remapped and cleared in swiotlb_update_mem_attributes.
	 */
	if (swiotlb_unencrypted_base)
		return;

	memset(vaddr, 0, bytes);
	mem->vaddr = vaddr;
	return;
}

static void __init *swiotlb_memblock_alloc(unsigned long nslabs,
		unsigned int flags,
		int (*remap)(void *tlb, unsigned long nslabs))
{
	size_t bytes = PAGE_ALIGN(nslabs << IO_TLB_SHIFT);
	void *tlb;

	/*
	 * By default allocate the bounce buffer memory from low memory, but
	 * allow to pick a location everywhere for hypervisors with guest
	 * memory encryption.
	 */
	if (flags & SWIOTLB_ANY)
		tlb = memblock_alloc(bytes, PAGE_SIZE);
	else
		tlb = memblock_alloc_low(bytes, PAGE_SIZE);

	if (!tlb) {
		pr_warn("%s: Failed to allocate %zu bytes tlb structure\n",
			__func__, bytes);
		return NULL;
	}

	if (remap && remap(tlb, nslabs) < 0) {
		memblock_free(tlb, PAGE_ALIGN(bytes));
		pr_warn("%s: Failed to remap %zu bytes\n", __func__, bytes);
		return NULL;
	}

	return tlb;
}

/*
 * Statically reserve bounce buffer space and initialize bounce buffer data
 * structures for the software IO TLB used to implement the DMA API.
 */
void __init swiotlb_init_remap(bool addressing_limit, unsigned int flags,
		int (*remap)(void *tlb, unsigned long nslabs))
{
	struct io_tlb_mem *mem = &io_tlb_default_mem;
	unsigned long nslabs;
	size_t alloc_size;
	void *tlb;

	if (!addressing_limit && !swiotlb_force_bounce)
		return;
	if (swiotlb_force_disable)
		return;

	/*
	 * default_nslabs maybe changed when adjust area number.
	 * So allocate bounce buffer after adjusting area number.
	 */
	if (!default_nareas)
		swiotlb_adjust_nareas(num_possible_cpus());

	nslabs = default_nslabs;
	while ((tlb = swiotlb_memblock_alloc(nslabs, flags, remap)) == NULL) {
		if (nslabs <= IO_TLB_MIN_SLABS)
			return;
		nslabs = ALIGN(nslabs >> 1, IO_TLB_SEGSIZE);
	}

	if (default_nslabs != nslabs) {
		pr_info("SWIOTLB bounce buffer size adjusted %lu -> %lu slabs",
			default_nslabs, nslabs);
		default_nslabs = nslabs;
	}

	alloc_size = PAGE_ALIGN(array_size(sizeof(*mem->slots), nslabs));
	mem->slots = memblock_alloc(alloc_size, PAGE_SIZE);
	if (!mem->slots) {
		pr_warn("%s: Failed to allocate %zu bytes align=0x%lx\n",
			__func__, alloc_size, PAGE_SIZE);
		return;
	}

	mem->areas = memblock_alloc(array_size(sizeof(struct io_tlb_area),
		default_nareas), SMP_CACHE_BYTES);
	if (!mem->areas) {
		pr_warn("%s: Failed to allocate mem->areas.\n", __func__);
		return;
	}

	swiotlb_init_io_tlb_mem(mem, __pa(tlb), nslabs, flags, false,
				default_nareas);

	if (flags & SWIOTLB_VERBOSE)
		swiotlb_print_info();
}

void __init swiotlb_init(bool addressing_limit, unsigned int flags)
{
	swiotlb_init_remap(addressing_limit, flags, NULL);
}

/*
 * Systems with larger DMA zones (those that don't support ISA) can
 * initialize the swiotlb later using the slab allocator if needed.
 * This should be just like above, but with some error catching.
 */
int swiotlb_init_late(size_t size, gfp_t gfp_mask,
		int (*remap)(void *tlb, unsigned long nslabs))
{
	struct io_tlb_mem *mem = &io_tlb_default_mem;
	unsigned long nslabs = ALIGN(size >> IO_TLB_SHIFT, IO_TLB_SEGSIZE);
	unsigned char *vstart = NULL;
	unsigned int order, area_order;
	bool retried = false;
	int rc = 0;

	if (swiotlb_force_disable)
		return 0;

retry:
	order = get_order(nslabs << IO_TLB_SHIFT);
	nslabs = SLABS_PER_PAGE << order;

	while ((SLABS_PER_PAGE << order) > IO_TLB_MIN_SLABS) {
		vstart = (void *)__get_free_pages(gfp_mask | __GFP_NOWARN,
						  order);
		if (vstart)
			break;
		order--;
		nslabs = SLABS_PER_PAGE << order;
		retried = true;
	}

	if (!vstart)
		return -ENOMEM;

	if (remap)
		rc = remap(vstart, nslabs);
	if (rc) {
		free_pages((unsigned long)vstart, order);

		nslabs = ALIGN(nslabs >> 1, IO_TLB_SEGSIZE);
		if (nslabs < IO_TLB_MIN_SLABS)
			return rc;
		retried = true;
		goto retry;
	}

	if (retried) {
		pr_warn("only able to allocate %ld MB\n",
			(PAGE_SIZE << order) >> 20);
	}

	if (!default_nareas)
		swiotlb_adjust_nareas(num_possible_cpus());

	area_order = get_order(array_size(sizeof(*mem->areas),
		default_nareas));
	mem->areas = (struct io_tlb_area *)
		__get_free_pages(GFP_KERNEL | __GFP_ZERO, area_order);
	if (!mem->areas)
		goto error_area;

	mem->slots = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
		get_order(array_size(sizeof(*mem->slots), nslabs)));
	if (!mem->slots)
		goto error_slots;

	set_memory_decrypted((unsigned long)vstart,
			     (nslabs << IO_TLB_SHIFT) >> PAGE_SHIFT);
	swiotlb_init_io_tlb_mem(mem, virt_to_phys(vstart), nslabs, 0, true,
				default_nareas);

	swiotlb_print_info();
	return 0;

error_slots:
	free_pages((unsigned long)mem->areas, area_order);
error_area:
	free_pages((unsigned long)vstart, order);
	return -ENOMEM;
}

void __init swiotlb_exit(void)
{
	struct io_tlb_mem *mem = &io_tlb_default_mem;
	unsigned long tbl_vaddr;
	size_t tbl_size, slots_size;
	unsigned int area_order;

	if (swiotlb_force_bounce)
		return;

	if (!mem->nslabs)
		return;

	pr_info("tearing down default memory pool\n");
	tbl_vaddr = (unsigned long)phys_to_virt(mem->start);
	tbl_size = PAGE_ALIGN(mem->end - mem->start);
	slots_size = PAGE_ALIGN(array_size(sizeof(*mem->slots), mem->nslabs));

	set_memory_encrypted(tbl_vaddr, tbl_size >> PAGE_SHIFT);
	if (mem->late_alloc) {
		area_order = get_order(array_size(sizeof(*mem->areas),
			mem->nareas));
		free_pages((unsigned long)mem->areas, area_order);
		free_pages(tbl_vaddr, get_order(tbl_size));
		free_pages((unsigned long)mem->slots, get_order(slots_size));
	} else {
		memblock_free_late(__pa(mem->areas),
			array_size(sizeof(*mem->areas), mem->nareas));
		memblock_free_late(mem->start, tbl_size);
		memblock_free_late(__pa(mem->slots), slots_size);
	}

	memset(mem, 0, sizeof(*mem));
}

/*
 * Return the offset into a iotlb slot required to keep the device happy.
 */
static unsigned int swiotlb_align_offset(struct device *dev, u64 addr)
{
	return addr & dma_get_min_align_mask(dev) & (IO_TLB_SIZE - 1);
}

/*
 * Bounce: copy the swiotlb buffer from or back to the original dma location
 */
static void swiotlb_bounce(struct device *dev, phys_addr_t tlb_addr, size_t size,
			   enum dma_data_direction dir)
{
	struct io_tlb_mem *mem = dev->dma_io_tlb_mem;
	int index = (tlb_addr - mem->start) >> IO_TLB_SHIFT;
	phys_addr_t orig_addr = mem->slots[index].orig_addr;
	size_t alloc_size = mem->slots[index].alloc_size;
	unsigned long pfn = PFN_DOWN(orig_addr);
	unsigned char *vaddr = mem->vaddr + tlb_addr - mem->start;
	unsigned int tlb_offset, orig_addr_offset;

	if (orig_addr == INVALID_PHYS_ADDR)
		return;

	tlb_offset = tlb_addr & (IO_TLB_SIZE - 1);
	orig_addr_offset = swiotlb_align_offset(dev, orig_addr);
	if (tlb_offset < orig_addr_offset) {
		dev_WARN_ONCE(dev, 1,
			"Access before mapping start detected. orig offset %u, requested offset %u.\n",
			orig_addr_offset, tlb_offset);
		return;
	}

	tlb_offset -= orig_addr_offset;
	if (tlb_offset > alloc_size) {
		dev_WARN_ONCE(dev, 1,
			"Buffer overflow detected. Allocation size: %zu. Mapping size: %zu+%u.\n",
			alloc_size, size, tlb_offset);
		return;
	}

	orig_addr += tlb_offset;
	alloc_size -= tlb_offset;

	if (size > alloc_size) {
		dev_WARN_ONCE(dev, 1,
			"Buffer overflow detected. Allocation size: %zu. Mapping size: %zu.\n",
			alloc_size, size);
		size = alloc_size;
	}

	if (PageHighMem(pfn_to_page(pfn))) {
		unsigned int offset = orig_addr & ~PAGE_MASK;
		struct page *page;
		unsigned int sz = 0;
		unsigned long flags;

		while (size) {
			sz = min_t(size_t, PAGE_SIZE - offset, size);

			local_irq_save(flags);
			page = pfn_to_page(pfn);
			if (dir == DMA_TO_DEVICE)
				memcpy_from_page(vaddr, page, offset, sz);
			else
				memcpy_to_page(page, offset, vaddr, sz);
			local_irq_restore(flags);

			size -= sz;
			pfn++;
			vaddr += sz;
			offset = 0;
		}
	} else if (dir == DMA_TO_DEVICE) {
		memcpy(vaddr, phys_to_virt(orig_addr), size);
	} else {
		memcpy(phys_to_virt(orig_addr), vaddr, size);
	}
}

static inline phys_addr_t slot_addr(phys_addr_t start, phys_addr_t idx)
{
	return start + (idx << IO_TLB_SHIFT);
}

/*
 * Carefully handle integer overflow which can occur when boundary_mask == ~0UL.
 */
static inline unsigned long get_max_slots(unsigned long boundary_mask)
{
	if (boundary_mask == ~0UL)
		return 1UL << (BITS_PER_LONG - IO_TLB_SHIFT);
	return nr_slots(boundary_mask + 1);
}

static unsigned int wrap_area_index(struct io_tlb_mem *mem, unsigned int index)
{
	if (index >= mem->area_nslabs)
		return 0;
	return index;
}

/*
 * Find a suitable number of IO TLB entries size that will fit this request and
 * allocate a buffer from that IO TLB pool.
 */
static int swiotlb_do_find_slots(struct device *dev, int area_index,
		phys_addr_t orig_addr, size_t alloc_size,
		unsigned int alloc_align_mask)
{
	struct io_tlb_mem *mem = dev->dma_io_tlb_mem;
	struct io_tlb_area *area = mem->areas + area_index;
	unsigned long boundary_mask = dma_get_seg_boundary(dev);
	dma_addr_t tbl_dma_addr =
		phys_to_dma_unencrypted(dev, mem->start) & boundary_mask;
	unsigned long max_slots = get_max_slots(boundary_mask);
	unsigned int iotlb_align_mask =
		dma_get_min_align_mask(dev) | alloc_align_mask;
	unsigned int nslots = nr_slots(alloc_size), stride;
	unsigned int offset = swiotlb_align_offset(dev, orig_addr);
	unsigned int index, slots_checked, count = 0, i;
	unsigned long flags;
	unsigned int slot_base;
	unsigned int slot_index;

	BUG_ON(!nslots);
	BUG_ON(area_index >= mem->nareas);

	/*
	 * For allocations of PAGE_SIZE or larger only look for page aligned
	 * allocations.
	 */
	if (alloc_size >= PAGE_SIZE)
		iotlb_align_mask |= ~PAGE_MASK;
	iotlb_align_mask &= ~(IO_TLB_SIZE - 1);

	/*
	 * For mappings with an alignment requirement don't bother looping to
	 * unaligned slots once we found an aligned one.
	 */
	stride = (iotlb_align_mask >> IO_TLB_SHIFT) + 1;

	spin_lock_irqsave(&area->lock, flags);
	if (unlikely(nslots > mem->area_nslabs - area->used))
		goto not_found;

	slot_base = area_index * mem->area_nslabs;
	index = area->index;

	for (slots_checked = 0; slots_checked < mem->area_nslabs; ) {
		slot_index = slot_base + index;

		if (orig_addr &&
		    (slot_addr(tbl_dma_addr, slot_index) &
		     iotlb_align_mask) != (orig_addr & iotlb_align_mask)) {
			index = wrap_area_index(mem, index + 1);
			slots_checked++;
			continue;
		}

		/*
		 * If we find a slot that indicates we have 'nslots' number of
		 * contiguous buffers, we allocate the buffers from that slot
		 * and mark the entries as '0' indicating unavailable.
		 */
		if (!iommu_is_span_boundary(slot_index, nslots,
					    nr_slots(tbl_dma_addr),
					    max_slots)) {
			if (mem->slots[slot_index].list >= nslots)
				goto found;
		}
		index = wrap_area_index(mem, index + stride);
		slots_checked += stride;
	}

not_found:
	spin_unlock_irqrestore(&area->lock, flags);
	return -1;

found:
	for (i = slot_index; i < slot_index + nslots; i++) {
		mem->slots[i].list = 0;
		mem->slots[i].alloc_size = alloc_size - (offset +
				((i - slot_index) << IO_TLB_SHIFT));
	}
	for (i = slot_index - 1;
	     io_tlb_offset(i) != IO_TLB_SEGSIZE - 1 &&
	     mem->slots[i].list; i--)
		mem->slots[i].list = ++count;

	/*
	 * Update the indices to avoid searching in the next round.
	 */
	area->index = wrap_area_index(mem, index + nslots);
	area->used += nslots;
	spin_unlock_irqrestore(&area->lock, flags);
	return slot_index;
}

static int swiotlb_find_slots(struct device *dev, phys_addr_t orig_addr,
		size_t alloc_size, unsigned int alloc_align_mask)
{
	struct io_tlb_mem *mem = dev->dma_io_tlb_mem;
	int start = raw_smp_processor_id() & (mem->nareas - 1);
	int i = start, index;

	do {
		index = swiotlb_do_find_slots(dev, i, orig_addr, alloc_size,
					      alloc_align_mask);
		if (index >= 0)
			return index;
		if (++i >= mem->nareas)
			i = 0;
	} while (i != start);

	return -1;
}

static unsigned long mem_used(struct io_tlb_mem *mem)
{
	int i;
	unsigned long used = 0;

	for (i = 0; i < mem->nareas; i++)
		used += mem->areas[i].used;
	return used;
}

phys_addr_t swiotlb_tbl_map_single(struct device *dev, phys_addr_t orig_addr,
		size_t mapping_size, size_t alloc_size,
		unsigned int alloc_align_mask, enum dma_data_direction dir,
		unsigned long attrs)
{
	struct io_tlb_mem *mem = dev->dma_io_tlb_mem;
	unsigned int offset = swiotlb_align_offset(dev, orig_addr);
	unsigned int i;
	int index;
	phys_addr_t tlb_addr;

	if (!mem || !mem->nslabs) {
		dev_warn_ratelimited(dev,
			"Can not allocate SWIOTLB buffer earlier and can't now provide you with the DMA bounce buffer");
		return (phys_addr_t)DMA_MAPPING_ERROR;
	}

	if (cc_platform_has(CC_ATTR_MEM_ENCRYPT))
		pr_warn_once("Memory encryption is active and system is using DMA bounce buffers\n");

	if (mapping_size > alloc_size) {
		dev_warn_once(dev, "Invalid sizes (mapping: %zd bytes, alloc: %zd bytes)",
			      mapping_size, alloc_size);
		return (phys_addr_t)DMA_MAPPING_ERROR;
	}

	index = swiotlb_find_slots(dev, orig_addr,
				   alloc_size + offset, alloc_align_mask);
	if (index == -1) {
		if (!(attrs & DMA_ATTR_NO_WARN))
			dev_warn_ratelimited(dev,
	"swiotlb buffer is full (sz: %zd bytes), total %lu (slots), used %lu (slots)\n",
				 alloc_size, mem->nslabs, mem_used(mem));
		return (phys_addr_t)DMA_MAPPING_ERROR;
	}

	/*
	 * Save away the mapping from the original address to the DMA address.
	 * This is needed when we sync the memory.  Then we sync the buffer if
	 * needed.
	 */
	for (i = 0; i < nr_slots(alloc_size + offset); i++)
		mem->slots[index + i].orig_addr = slot_addr(orig_addr, i);
	tlb_addr = slot_addr(mem->start, index) + offset;
	/*
	 * When dir == DMA_FROM_DEVICE we could omit the copy from the orig
	 * to the tlb buffer, if we knew for sure the device will
	 * overwrite the entire current content. But we don't. Thus
	 * unconditional bounce may prevent leaking swiotlb content (i.e.
	 * kernel memory) to user-space.
	 */
	swiotlb_bounce(dev, tlb_addr, mapping_size, DMA_TO_DEVICE);
	return tlb_addr;
}

static void swiotlb_release_slots(struct device *dev, phys_addr_t tlb_addr)
{
	struct io_tlb_mem *mem = dev->dma_io_tlb_mem;
	unsigned long flags;
	unsigned int offset = swiotlb_align_offset(dev, tlb_addr);
	int index = (tlb_addr - offset - mem->start) >> IO_TLB_SHIFT;
	int nslots = nr_slots(mem->slots[index].alloc_size + offset);
	int aindex = index / mem->area_nslabs;
	struct io_tlb_area *area = &mem->areas[aindex];
	int count, i;

	/*
	 * Return the buffer to the free list by setting the corresponding
	 * entries to indicate the number of contiguous entries available.
	 * While returning the entries to the free list, we merge the entries
	 * with slots below and above the pool being returned.
	 */
	BUG_ON(aindex >= mem->nareas);

	spin_lock_irqsave(&area->lock, flags);
	if (index + nslots < ALIGN(index + 1, IO_TLB_SEGSIZE))
		count = mem->slots[index + nslots].list;
	else
		count = 0;

	/*
	 * Step 1: return the slots to the free list, merging the slots with
	 * superceeding slots
	 */
	for (i = index + nslots - 1; i >= index; i--) {
		mem->slots[i].list = ++count;
		mem->slots[i].orig_addr = INVALID_PHYS_ADDR;
		mem->slots[i].alloc_size = 0;
	}

	/*
	 * Step 2: merge the returned slots with the preceding slots, if
	 * available (non zero)
	 */
	for (i = index - 1;
	     io_tlb_offset(i) != IO_TLB_SEGSIZE - 1 && mem->slots[i].list;
	     i--)
		mem->slots[i].list = ++count;
	area->used -= nslots;
	spin_unlock_irqrestore(&area->lock, flags);
}

/*
 * tlb_addr is the physical address of the bounce buffer to unmap.
 */
void swiotlb_tbl_unmap_single(struct device *dev, phys_addr_t tlb_addr,
			      size_t mapping_size, enum dma_data_direction dir,
			      unsigned long attrs)
{
	/*
	 * First, sync the memory before unmapping the entry
	 */
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC) &&
	    (dir == DMA_FROM_DEVICE || dir == DMA_BIDIRECTIONAL))
		swiotlb_bounce(dev, tlb_addr, mapping_size, DMA_FROM_DEVICE);

	swiotlb_release_slots(dev, tlb_addr);
}

void swiotlb_sync_single_for_device(struct device *dev, phys_addr_t tlb_addr,
		size_t size, enum dma_data_direction dir)
{
	if (dir == DMA_TO_DEVICE || dir == DMA_BIDIRECTIONAL)
		swiotlb_bounce(dev, tlb_addr, size, DMA_TO_DEVICE);
	else
		BUG_ON(dir != DMA_FROM_DEVICE);
}

void swiotlb_sync_single_for_cpu(struct device *dev, phys_addr_t tlb_addr,
		size_t size, enum dma_data_direction dir)
{
	if (dir == DMA_FROM_DEVICE || dir == DMA_BIDIRECTIONAL)
		swiotlb_bounce(dev, tlb_addr, size, DMA_FROM_DEVICE);
	else
		BUG_ON(dir != DMA_TO_DEVICE);
}

/*
 * Create a swiotlb mapping for the buffer at @paddr, and in case of DMAing
 * to the device copy the data into it as well.
 */
dma_addr_t swiotlb_map(struct device *dev, phys_addr_t paddr, size_t size,
		enum dma_data_direction dir, unsigned long attrs)
{
	phys_addr_t swiotlb_addr;
	dma_addr_t dma_addr;

	trace_swiotlb_bounced(dev, phys_to_dma(dev, paddr), size);

	swiotlb_addr = swiotlb_tbl_map_single(dev, paddr, size, size, 0, dir,
			attrs);
	if (swiotlb_addr == (phys_addr_t)DMA_MAPPING_ERROR)
		return DMA_MAPPING_ERROR;

	/* Ensure that the address returned is DMA'ble */
	dma_addr = phys_to_dma_unencrypted(dev, swiotlb_addr);
	if (unlikely(!dma_capable(dev, dma_addr, size, true))) {
		swiotlb_tbl_unmap_single(dev, swiotlb_addr, size, dir,
			attrs | DMA_ATTR_SKIP_CPU_SYNC);
		dev_WARN_ONCE(dev, 1,
			"swiotlb addr %pad+%zu overflow (mask %llx, bus limit %llx).\n",
			&dma_addr, size, *dev->dma_mask, dev->bus_dma_limit);
		return DMA_MAPPING_ERROR;
	}

	if (!dev_is_dma_coherent(dev) && !(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		arch_sync_dma_for_device(swiotlb_addr, size, dir);
	return dma_addr;
}

size_t swiotlb_max_mapping_size(struct device *dev)
{
	int min_align_mask = dma_get_min_align_mask(dev);
	int min_align = 0;

	/*
	 * swiotlb_find_slots() skips slots according to
	 * min align mask. This affects max mapping size.
	 * Take it into acount here.
	 */
	if (min_align_mask)
		min_align = roundup(min_align_mask, IO_TLB_SIZE);

	return ((size_t)IO_TLB_SIZE) * IO_TLB_SEGSIZE - min_align;
}

bool is_swiotlb_active(struct device *dev)
{
	struct io_tlb_mem *mem = dev->dma_io_tlb_mem;

	return mem && mem->nslabs;
}
EXPORT_SYMBOL_GPL(is_swiotlb_active);

static int io_tlb_used_get(void *data, u64 *val)
{
	*val = mem_used(&io_tlb_default_mem);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_io_tlb_used, io_tlb_used_get, NULL, "%llu\n");

static void swiotlb_create_debugfs_files(struct io_tlb_mem *mem,
					 const char *dirname)
{
	mem->debugfs = debugfs_create_dir(dirname, io_tlb_default_mem.debugfs);
	if (!mem->nslabs)
		return;

	debugfs_create_ulong("io_tlb_nslabs", 0400, mem->debugfs, &mem->nslabs);
	debugfs_create_file("io_tlb_used", 0400, mem->debugfs, NULL,
			&fops_io_tlb_used);
}

static int __init __maybe_unused swiotlb_create_default_debugfs(void)
{
	swiotlb_create_debugfs_files(&io_tlb_default_mem, "swiotlb");
	return 0;
}

#ifdef CONFIG_DEBUG_FS
late_initcall(swiotlb_create_default_debugfs);
#endif

#ifdef CONFIG_DMA_RESTRICTED_POOL

struct page *swiotlb_alloc(struct device *dev, size_t size)
{
	struct io_tlb_mem *mem = dev->dma_io_tlb_mem;
	phys_addr_t tlb_addr;
	int index;

	if (!mem)
		return NULL;

	index = swiotlb_find_slots(dev, 0, size, 0);
	if (index == -1)
		return NULL;

	tlb_addr = slot_addr(mem->start, index);

	return pfn_to_page(PFN_DOWN(tlb_addr));
}

bool swiotlb_free(struct device *dev, struct page *page, size_t size)
{
	phys_addr_t tlb_addr = page_to_phys(page);

	if (!is_swiotlb_buffer(dev, tlb_addr))
		return false;

	swiotlb_release_slots(dev, tlb_addr);

	return true;
}

static int rmem_swiotlb_device_init(struct reserved_mem *rmem,
				    struct device *dev)
{
	struct io_tlb_mem *mem = rmem->priv;
	unsigned long nslabs = rmem->size >> IO_TLB_SHIFT;

	/* Set Per-device io tlb area to one */
	unsigned int nareas = 1;

	/*
	 * Since multiple devices can share the same pool, the private data,
	 * io_tlb_mem struct, will be initialized by the first device attached
	 * to it.
	 */
	if (!mem) {
		mem = kzalloc(sizeof(*mem), GFP_KERNEL);
		if (!mem)
			return -ENOMEM;

		mem->slots = kcalloc(nslabs, sizeof(*mem->slots), GFP_KERNEL);
		if (!mem->slots) {
			kfree(mem);
			return -ENOMEM;
		}

		mem->areas = kcalloc(nareas, sizeof(*mem->areas),
				GFP_KERNEL);
		if (!mem->areas) {
			kfree(mem->slots);
			kfree(mem);
			return -ENOMEM;
		}

		set_memory_decrypted((unsigned long)phys_to_virt(rmem->base),
				     rmem->size >> PAGE_SHIFT);
		swiotlb_init_io_tlb_mem(mem, rmem->base, nslabs, SWIOTLB_FORCE,
					false, nareas);
		mem->for_alloc = true;

		rmem->priv = mem;

		swiotlb_create_debugfs_files(mem, rmem->name);
	}

	dev->dma_io_tlb_mem = mem;

	return 0;
}

static void rmem_swiotlb_device_release(struct reserved_mem *rmem,
					struct device *dev)
{
	dev->dma_io_tlb_mem = &io_tlb_default_mem;
}

static const struct reserved_mem_ops rmem_swiotlb_ops = {
	.device_init = rmem_swiotlb_device_init,
	.device_release = rmem_swiotlb_device_release,
};

static int __init rmem_swiotlb_setup(struct reserved_mem *rmem)
{
	unsigned long node = rmem->fdt_node;

	if (of_get_flat_dt_prop(node, "reusable", NULL) ||
	    of_get_flat_dt_prop(node, "linux,cma-default", NULL) ||
	    of_get_flat_dt_prop(node, "linux,dma-default", NULL) ||
	    of_get_flat_dt_prop(node, "no-map", NULL))
		return -EINVAL;

	if (PageHighMem(pfn_to_page(PHYS_PFN(rmem->base)))) {
		pr_err("Restricted DMA pool must be accessible within the linear mapping.");
		return -EINVAL;
	}

	rmem->ops = &rmem_swiotlb_ops;
	pr_info("Reserved memory: created restricted DMA pool at %pa, size %ld MiB\n",
		&rmem->base, (unsigned long)rmem->size / SZ_1M);
	return 0;
}

RESERVEDMEM_OF_DECLARE(dma, "restricted-dma-pool", rmem_swiotlb_setup);
#endif /* CONFIG_DMA_RESTRICTED_POOL */
