/*
 * mm/percpu-vm.c - vmalloc area based chunk allocation
 *
 * Copyright (C) 2010		SUSE Linux Products GmbH
 * Copyright (C) 2010		Tejun Heo <tj@kernel.org>
 *
 * This file is released under the GPLv2.
 *
 * Chunks are mapped into vmalloc areas and populated page by page.
 * This is the default chunk allocator.
 */

static struct page *pcpu_chunk_page(struct pcpu_chunk *chunk,
				    unsigned int cpu, int page_idx)
{
	/* must not be used on pre-mapped chunk */
	WARN_ON(chunk->immutable);

	return vmalloc_to_page((void *)pcpu_chunk_addr(chunk, cpu, page_idx));
}

/**
 * pcpu_get_pages_and_bitmap - get temp pages array and bitmap
 * @chunk: chunk of interest
 * @bitmapp: output parameter for bitmap
 * @may_alloc: may allocate the array
 *
 * Returns pointer to array of pointers to struct page and bitmap,
 * both of which can be indexed with pcpu_page_idx().  The returned
 * array is cleared to zero and *@bitmapp is copied from
 * @chunk->populated.  Note that there is only one array and bitmap
 * and access exclusion is the caller's responsibility.
 *
 * CONTEXT:
 * pcpu_alloc_mutex and does GFP_KERNEL allocation if @may_alloc.
 * Otherwise, don't care.
 *
 * RETURNS:
 * Pointer to temp pages array on success, NULL on failure.
 */
static struct page **pcpu_get_pages_and_bitmap(struct pcpu_chunk *chunk,
					       unsigned long **bitmapp,
					       bool may_alloc)
{
	static struct page **pages;
	static unsigned long *bitmap;
	size_t pages_size = pcpu_nr_units * pcpu_unit_pages * sizeof(pages[0]);
	size_t bitmap_size = BITS_TO_LONGS(pcpu_unit_pages) *
			     sizeof(unsigned long);

	if (!pages || !bitmap) {
		if (may_alloc && !pages)
			pages = pcpu_mem_zalloc(pages_size);
		if (may_alloc && !bitmap)
			bitmap = pcpu_mem_zalloc(bitmap_size);
		if (!pages || !bitmap)
			return NULL;
	}

	bitmap_copy(bitmap, chunk->populated, pcpu_unit_pages);

	*bitmapp = bitmap;
	return pages;
}

/**
 * pcpu_free_pages - free pages which were allocated for @chunk
 * @chunk: chunk pages were allocated for
 * @pages: array of pages to be freed, indexed by pcpu_page_idx()
 * @populated: populated bitmap
 * @page_start: page index of the first page to be freed
 * @page_end: page index of the last page to be freed + 1
 *
 * Free pages [@page_start and @page_end) in @pages for all units.
 * The pages were allocated for @chunk.
 */
static void pcpu_free_pages(struct pcpu_chunk *chunk,
			    struct page **pages, unsigned long *populated,
			    int page_start, int page_end)
{
	unsigned int cpu;
	int i;

	for_each_possible_cpu(cpu) {
		for (i = page_start; i < page_end; i++) {
			struct page *page = pages[pcpu_page_idx(cpu, i)];

			if (page)
				__free_page(page);
		}
	}
}

/**
 * pcpu_alloc_pages - allocates pages for @chunk
 * @chunk: target chunk
 * @pages: array to put the allocated pages into, indexed by pcpu_page_idx()
 * @populated: populated bitmap
 * @page_start: page index of the first page to be allocated
 * @page_end: page index of the last page to be allocated + 1
 *
 * Allocate pages [@page_start,@page_end) into @pages for all units.
 * The allocation is for @chunk.  Percpu core doesn't care about the
 * content of @pages and will pass it verbatim to pcpu_map_pages().
 */
static int pcpu_alloc_pages(struct pcpu_chunk *chunk,
			    struct page **pages, unsigned long *populated,
			    int page_start, int page_end)
{
	const gfp_t gfp = GFP_KERNEL | __GFP_HIGHMEM | __GFP_COLD;
	unsigned int cpu;
	int i;

	for_each_possible_cpu(cpu) {
		for (i = page_start; i < page_end; i++) {
			struct page **pagep = &pages[pcpu_page_idx(cpu, i)];

			*pagep = alloc_pages_node(cpu_to_node(cpu), gfp, 0);
			if (!*pagep) {
				pcpu_free_pages(chunk, pages, populated,
						page_start, page_end);
				return -ENOMEM;
			}
		}
	}
	return 0;
}

/**
 * pcpu_pre_unmap_flush - flush cache prior to unmapping
 * @chunk: chunk the regions to be flushed belongs to
 * @page_start: page index of the first page to be flushed
 * @page_end: page index of the last page to be flushed + 1
 *
 * Pages in [@page_start,@page_end) of @chunk are about to be
 * unmapped.  Flush cache.  As each flushing trial can be very
 * expensive, issue flush on the whole region at once rather than
 * doing it for each cpu.  This could be an overkill but is more
 * scalable.
 */
static void pcpu_pre_unmap_flush(struct pcpu_chunk *chunk,
				 int page_start, int page_end)
{
	flush_cache_vunmap(
		pcpu_chunk_addr(chunk, pcpu_low_unit_cpu, page_start),
		pcpu_chunk_addr(chunk, pcpu_high_unit_cpu, page_end));
}

static void __pcpu_unmap_pages(unsigned long addr, int nr_pages)
{
	unmap_kernel_range_noflush(addr, nr_pages << PAGE_SHIFT);
}

/**
 * pcpu_unmap_pages - unmap pages out of a pcpu_chunk
 * @chunk: chunk of interest
 * @pages: pages array which can be used to pass information to free
 * @populated: populated bitmap
 * @page_start: page index of the first page to unmap
 * @page_end: page index of the last page to unmap + 1
 *
 * For each cpu, unmap pages [@page_start,@page_end) out of @chunk.
 * Corresponding elements in @pages were cleared by the caller and can
 * be used to carry information to pcpu_free_pages() which will be
 * called after all unmaps are finished.  The caller should call
 * proper pre/post flush functions.
 */
static void pcpu_unmap_pages(struct pcpu_chunk *chunk,
			     struct page **pages, unsigned long *populated,
			     int page_start, int page_end)
{
	unsigned int cpu;
	int i;

	for_each_possible_cpu(cpu) {
		for (i = page_start; i < page_end; i++) {
			struct page *page;

			page = pcpu_chunk_page(chunk, cpu, i);
			WARN_ON(!page);
			pages[pcpu_page_idx(cpu, i)] = page;
		}
		__pcpu_unmap_pages(pcpu_chunk_addr(chunk, cpu, page_start),
				   page_end - page_start);
	}

	bitmap_clear(populated, page_start, page_end - page_start);
}

/**
 * pcpu_post_unmap_tlb_flush - flush TLB after unmapping
 * @chunk: pcpu_chunk the regions to be flushed belong to
 * @page_start: page index of the first page to be flushed
 * @page_end: page index of the last page to be flushed + 1
 *
 * Pages [@page_start,@page_end) of @chunk have been unmapped.  Flush
 * TLB for the regions.  This can be skipped if the area is to be
 * returned to vmalloc as vmalloc will handle TLB flushing lazily.
 *
 * As with pcpu_pre_unmap_flush(), TLB flushing also is done at once
 * for the whole region.
 */
static void pcpu_post_unmap_tlb_flush(struct pcpu_chunk *chunk,
				      int page_start, int page_end)
{
	flush_tlb_kernel_range(
		pcpu_chunk_addr(chunk, pcpu_low_unit_cpu, page_start),
		pcpu_chunk_addr(chunk, pcpu_high_unit_cpu, page_end));
}

static int __pcpu_map_pages(unsigned long addr, struct page **pages,
			    int nr_pages)
{
	return map_kernel_range_noflush(addr, nr_pages << PAGE_SHIFT,
					PAGE_KERNEL, pages);
}

/**
 * pcpu_map_pages - map pages into a pcpu_chunk
 * @chunk: chunk of interest
 * @pages: pages array containing pages to be mapped
 * @populated: populated bitmap
 * @page_start: page index of the first page to map
 * @page_end: page index of the last page to map + 1
 *
 * For each cpu, map pages [@page_start,@page_end) into @chunk.  The
 * caller is responsible for calling pcpu_post_map_flush() after all
 * mappings are complete.
 *
 * This function is responsible for setting corresponding bits in
 * @chunk->populated bitmap and whatever is necessary for reverse
 * lookup (addr -> chunk).
 */
static int pcpu_map_pages(struct pcpu_chunk *chunk,
			  struct page **pages, unsigned long *populated,
			  int page_start, int page_end)
{
	unsigned int cpu, tcpu;
	int i, err;

	for_each_possible_cpu(cpu) {
		err = __pcpu_map_pages(pcpu_chunk_addr(chunk, cpu, page_start),
				       &pages[pcpu_page_idx(cpu, page_start)],
				       page_end - page_start);
		if (err < 0)
			goto err;
	}

	/* mapping successful, link chunk and mark populated */
	for (i = page_start; i < page_end; i++) {
		for_each_possible_cpu(cpu)
			pcpu_set_page_chunk(pages[pcpu_page_idx(cpu, i)],
					    chunk);
		__set_bit(i, populated);
	}

	return 0;

err:
	for_each_possible_cpu(tcpu) {
		if (tcpu == cpu)
			break;
		__pcpu_unmap_pages(pcpu_chunk_addr(chunk, tcpu, page_start),
				   page_end - page_start);
	}
	return err;
}

/**
 * pcpu_post_map_flush - flush cache after mapping
 * @chunk: pcpu_chunk the regions to be flushed belong to
 * @page_start: page index of the first page to be flushed
 * @page_end: page index of the last page to be flushed + 1
 *
 * Pages [@page_start,@page_end) of @chunk have been mapped.  Flush
 * cache.
 *
 * As with pcpu_pre_unmap_flush(), TLB flushing also is done at once
 * for the whole region.
 */
static void pcpu_post_map_flush(struct pcpu_chunk *chunk,
				int page_start, int page_end)
{
	flush_cache_vmap(
		pcpu_chunk_addr(chunk, pcpu_low_unit_cpu, page_start),
		pcpu_chunk_addr(chunk, pcpu_high_unit_cpu, page_end));
}

/**
 * pcpu_populate_chunk - populate and map an area of a pcpu_chunk
 * @chunk: chunk of interest
 * @off: offset to the area to populate
 * @size: size of the area to populate in bytes
 *
 * For each cpu, populate and map pages [@page_start,@page_end) into
 * @chunk.  The area is cleared on return.
 *
 * CONTEXT:
 * pcpu_alloc_mutex, does GFP_KERNEL allocation.
 */
static int pcpu_populate_chunk(struct pcpu_chunk *chunk, int off, int size)
{
	int page_start = PFN_DOWN(off);
	int page_end = PFN_UP(off + size);
	int free_end = page_start, unmap_end = page_start;
	struct page **pages;
	unsigned long *populated;
	unsigned int cpu;
	int rs, re, rc;

	/* quick path, check whether all pages are already there */
	rs = page_start;
	pcpu_next_pop(chunk, &rs, &re, page_end);
	if (rs == page_start && re == page_end)
		goto clear;

	/* need to allocate and map pages, this chunk can't be immutable */
	WARN_ON(chunk->immutable);

	pages = pcpu_get_pages_and_bitmap(chunk, &populated, true);
	if (!pages)
		return -ENOMEM;

	/* alloc and map */
	pcpu_for_each_unpop_region(chunk, rs, re, page_start, page_end) {
		rc = pcpu_alloc_pages(chunk, pages, populated, rs, re);
		if (rc)
			goto err_free;
		free_end = re;
	}

	pcpu_for_each_unpop_region(chunk, rs, re, page_start, page_end) {
		rc = pcpu_map_pages(chunk, pages, populated, rs, re);
		if (rc)
			goto err_unmap;
		unmap_end = re;
	}
	pcpu_post_map_flush(chunk, page_start, page_end);

	/* commit new bitmap */
	bitmap_copy(chunk->populated, populated, pcpu_unit_pages);
clear:
	for_each_possible_cpu(cpu)
		memset((void *)pcpu_chunk_addr(chunk, cpu, 0) + off, 0, size);
	return 0;

err_unmap:
	pcpu_pre_unmap_flush(chunk, page_start, unmap_end);
	pcpu_for_each_unpop_region(chunk, rs, re, page_start, unmap_end)
		pcpu_unmap_pages(chunk, pages, populated, rs, re);
	pcpu_post_unmap_tlb_flush(chunk, page_start, unmap_end);
err_free:
	pcpu_for_each_unpop_region(chunk, rs, re, page_start, free_end)
		pcpu_free_pages(chunk, pages, populated, rs, re);
	return rc;
}

/**
 * pcpu_depopulate_chunk - depopulate and unmap an area of a pcpu_chunk
 * @chunk: chunk to depopulate
 * @off: offset to the area to depopulate
 * @size: size of the area to depopulate in bytes
 *
 * For each cpu, depopulate and unmap pages [@page_start,@page_end)
 * from @chunk.  If @flush is true, vcache is flushed before unmapping
 * and tlb after.
 *
 * CONTEXT:
 * pcpu_alloc_mutex.
 */
static void pcpu_depopulate_chunk(struct pcpu_chunk *chunk, int off, int size)
{
	int page_start = PFN_DOWN(off);
	int page_end = PFN_UP(off + size);
	struct page **pages;
	unsigned long *populated;
	int rs, re;

	/* quick path, check whether it's empty already */
	rs = page_start;
	pcpu_next_unpop(chunk, &rs, &re, page_end);
	if (rs == page_start && re == page_end)
		return;

	/* immutable chunks can't be depopulated */
	WARN_ON(chunk->immutable);

	/*
	 * If control reaches here, there must have been at least one
	 * successful population attempt so the temp pages array must
	 * be available now.
	 */
	pages = pcpu_get_pages_and_bitmap(chunk, &populated, false);
	BUG_ON(!pages);

	/* unmap and free */
	pcpu_pre_unmap_flush(chunk, page_start, page_end);

	pcpu_for_each_pop_region(chunk, rs, re, page_start, page_end)
		pcpu_unmap_pages(chunk, pages, populated, rs, re);

	/* no need to flush tlb, vmalloc will handle it lazily */

	pcpu_for_each_pop_region(chunk, rs, re, page_start, page_end)
		pcpu_free_pages(chunk, pages, populated, rs, re);

	/* commit new bitmap */
	bitmap_copy(chunk->populated, populated, pcpu_unit_pages);
}

static struct pcpu_chunk *pcpu_create_chunk(void)
{
	struct pcpu_chunk *chunk;
	struct vm_struct **vms;

	chunk = pcpu_alloc_chunk();
	if (!chunk)
		return NULL;

	vms = pcpu_get_vm_areas(pcpu_group_offsets, pcpu_group_sizes,
				pcpu_nr_groups, pcpu_atom_size);
	if (!vms) {
		pcpu_free_chunk(chunk);
		return NULL;
	}

	chunk->data = vms;
	chunk->base_addr = vms[0]->addr - pcpu_group_offsets[0];
	return chunk;
}

static void pcpu_destroy_chunk(struct pcpu_chunk *chunk)
{
	if (chunk && chunk->data)
		pcpu_free_vm_areas(chunk->data, pcpu_nr_groups);
	pcpu_free_chunk(chunk);
}

static struct page *pcpu_addr_to_page(void *addr)
{
	return vmalloc_to_page(addr);
}

static int __init pcpu_verify_alloc_info(const struct pcpu_alloc_info *ai)
{
	/* no extra restriction */
	return 0;
}
