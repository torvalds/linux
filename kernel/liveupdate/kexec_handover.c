// SPDX-License-Identifier: GPL-2.0-only
/*
 * kexec_handover.c - kexec handover metadata processing
 * Copyright (C) 2023 Alexander Graf <graf@amazon.com>
 * Copyright (C) 2025 Microsoft Corporation, Mike Rapoport <rppt@kernel.org>
 * Copyright (C) 2025 Google LLC, Changyuan Lyu <changyuanl@google.com>
 * Copyright (C) 2025 Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#define pr_fmt(fmt) "KHO: " fmt

#include <linux/cleanup.h>
#include <linux/cma.h>
#include <linux/kmemleak.h>
#include <linux/count_zeros.h>
#include <linux/kexec.h>
#include <linux/kexec_handover.h>
#include <linux/libfdt.h>
#include <linux/list.h>
#include <linux/memblock.h>
#include <linux/page-isolation.h>
#include <linux/unaligned.h>
#include <linux/vmalloc.h>

#include <asm/early_ioremap.h>

#include "kexec_handover_internal.h"
/*
 * KHO is tightly coupled with mm init and needs access to some of mm
 * internal APIs.
 */
#include "../../mm/internal.h"
#include "../kexec_internal.h"
#include "kexec_handover_internal.h"

#define KHO_FDT_COMPATIBLE "kho-v1"
#define PROP_PRESERVED_MEMORY_MAP "preserved-memory-map"
#define PROP_SUB_FDT "fdt"

#define KHO_PAGE_MAGIC 0x4b484f50U /* ASCII for 'KHOP' */

/*
 * KHO uses page->private, which is an unsigned long, to store page metadata.
 * Use it to store both the magic and the order.
 */
union kho_page_info {
	unsigned long page_private;
	struct {
		unsigned int order;
		unsigned int magic;
	};
};

static_assert(sizeof(union kho_page_info) == sizeof(((struct page *)0)->private));

static bool kho_enable __ro_after_init = IS_ENABLED(CONFIG_KEXEC_HANDOVER_ENABLE_DEFAULT);

bool kho_is_enabled(void)
{
	return kho_enable;
}
EXPORT_SYMBOL_GPL(kho_is_enabled);

static int __init kho_parse_enable(char *p)
{
	return kstrtobool(p, &kho_enable);
}
early_param("kho", kho_parse_enable);

/*
 * Keep track of memory that is to be preserved across KHO.
 *
 * The serializing side uses two levels of xarrays to manage chunks of per-order
 * PAGE_SIZE byte bitmaps. For instance if PAGE_SIZE = 4096, the entire 1G order
 * of a 8TB system would fit inside a single 4096 byte bitmap. For order 0
 * allocations each bitmap will cover 128M of address space. Thus, for 16G of
 * memory at most 512K of bitmap memory will be needed for order 0.
 *
 * This approach is fully incremental, as the serialization progresses folios
 * can continue be aggregated to the tracker. The final step, immediately prior
 * to kexec would serialize the xarray information into a linked list for the
 * successor kernel to parse.
 */

#define PRESERVE_BITS (PAGE_SIZE * 8)

struct kho_mem_phys_bits {
	DECLARE_BITMAP(preserve, PRESERVE_BITS);
};

static_assert(sizeof(struct kho_mem_phys_bits) == PAGE_SIZE);

struct kho_mem_phys {
	/*
	 * Points to kho_mem_phys_bits, a sparse bitmap array. Each bit is sized
	 * to order.
	 */
	struct xarray phys_bits;
};

struct kho_mem_track {
	/* Points to kho_mem_phys, each order gets its own bitmap tree */
	struct xarray orders;
};

struct khoser_mem_chunk;

struct kho_out {
	void *fdt;
	bool finalized;
	struct mutex lock; /* protects KHO FDT finalization */

	struct kho_mem_track track;
	struct kho_debugfs dbg;
};

static struct kho_out kho_out = {
	.lock = __MUTEX_INITIALIZER(kho_out.lock),
	.track = {
		.orders = XARRAY_INIT(kho_out.track.orders, 0),
	},
	.finalized = false,
};

static void *xa_load_or_alloc(struct xarray *xa, unsigned long index)
{
	void *res = xa_load(xa, index);

	if (res)
		return res;

	void *elm __free(free_page) = (void *)get_zeroed_page(GFP_KERNEL);

	if (!elm)
		return ERR_PTR(-ENOMEM);

	if (WARN_ON(kho_scratch_overlap(virt_to_phys(elm), PAGE_SIZE)))
		return ERR_PTR(-EINVAL);

	res = xa_cmpxchg(xa, index, NULL, elm, GFP_KERNEL);
	if (xa_is_err(res))
		return ERR_PTR(xa_err(res));
	else if (res)
		return res;

	return no_free_ptr(elm);
}

static void __kho_unpreserve_order(struct kho_mem_track *track, unsigned long pfn,
				   unsigned int order)
{
	struct kho_mem_phys_bits *bits;
	struct kho_mem_phys *physxa;
	const unsigned long pfn_high = pfn >> order;

	physxa = xa_load(&track->orders, order);
	if (WARN_ON_ONCE(!physxa))
		return;

	bits = xa_load(&physxa->phys_bits, pfn_high / PRESERVE_BITS);
	if (WARN_ON_ONCE(!bits))
		return;

	clear_bit(pfn_high % PRESERVE_BITS, bits->preserve);
}

static void __kho_unpreserve(struct kho_mem_track *track, unsigned long pfn,
			     unsigned long end_pfn)
{
	unsigned int order;

	while (pfn < end_pfn) {
		order = min(count_trailing_zeros(pfn), ilog2(end_pfn - pfn));

		__kho_unpreserve_order(track, pfn, order);

		pfn += 1 << order;
	}
}

static int __kho_preserve_order(struct kho_mem_track *track, unsigned long pfn,
				unsigned int order)
{
	struct kho_mem_phys_bits *bits;
	struct kho_mem_phys *physxa, *new_physxa;
	const unsigned long pfn_high = pfn >> order;

	might_sleep();
	physxa = xa_load(&track->orders, order);
	if (!physxa) {
		int err;

		new_physxa = kzalloc(sizeof(*physxa), GFP_KERNEL);
		if (!new_physxa)
			return -ENOMEM;

		xa_init(&new_physxa->phys_bits);
		physxa = xa_cmpxchg(&track->orders, order, NULL, new_physxa,
				    GFP_KERNEL);

		err = xa_err(physxa);
		if (err || physxa) {
			xa_destroy(&new_physxa->phys_bits);
			kfree(new_physxa);

			if (err)
				return err;
		} else {
			physxa = new_physxa;
		}
	}

	bits = xa_load_or_alloc(&physxa->phys_bits, pfn_high / PRESERVE_BITS);
	if (IS_ERR(bits))
		return PTR_ERR(bits);

	set_bit(pfn_high % PRESERVE_BITS, bits->preserve);

	return 0;
}

static struct page *kho_restore_page(phys_addr_t phys, bool is_folio)
{
	struct page *page = pfn_to_online_page(PHYS_PFN(phys));
	unsigned int nr_pages, ref_cnt;
	union kho_page_info info;

	if (!page)
		return NULL;

	info.page_private = page->private;
	/*
	 * deserialize_bitmap() only sets the magic on the head page. This magic
	 * check also implicitly makes sure phys is order-aligned since for
	 * non-order-aligned phys addresses, magic will never be set.
	 */
	if (WARN_ON_ONCE(info.magic != KHO_PAGE_MAGIC || info.order > MAX_PAGE_ORDER))
		return NULL;
	nr_pages = (1 << info.order);

	/* Clear private to make sure later restores on this page error out. */
	page->private = 0;
	/* Head page gets refcount of 1. */
	set_page_count(page, 1);

	/*
	 * For higher order folios, tail pages get a page count of zero.
	 * For physically contiguous order-0 pages every pages gets a page
	 * count of 1
	 */
	ref_cnt = is_folio ? 0 : 1;
	for (unsigned int i = 1; i < nr_pages; i++)
		set_page_count(page + i, ref_cnt);

	if (is_folio && info.order)
		prep_compound_page(page, info.order);

	/* Always mark headpage's codetag as empty to avoid accounting mismatch */
	clear_page_tag_ref(page);
	if (!is_folio) {
		/* Also do that for the non-compound tail pages */
		for (unsigned int i = 1; i < nr_pages; i++)
			clear_page_tag_ref(page + i);
	}

	adjust_managed_page_count(page, nr_pages);
	return page;
}

/**
 * kho_restore_folio - recreates the folio from the preserved memory.
 * @phys: physical address of the folio.
 *
 * Return: pointer to the struct folio on success, NULL on failure.
 */
struct folio *kho_restore_folio(phys_addr_t phys)
{
	struct page *page = kho_restore_page(phys, true);

	return page ? page_folio(page) : NULL;
}
EXPORT_SYMBOL_GPL(kho_restore_folio);

/**
 * kho_restore_pages - restore list of contiguous order 0 pages.
 * @phys: physical address of the first page.
 * @nr_pages: number of pages.
 *
 * Restore a contiguous list of order 0 pages that was preserved with
 * kho_preserve_pages().
 *
 * Return: 0 on success, error code on failure
 */
struct page *kho_restore_pages(phys_addr_t phys, unsigned int nr_pages)
{
	const unsigned long start_pfn = PHYS_PFN(phys);
	const unsigned long end_pfn = start_pfn + nr_pages;
	unsigned long pfn = start_pfn;

	while (pfn < end_pfn) {
		const unsigned int order =
			min(count_trailing_zeros(pfn), ilog2(end_pfn - pfn));
		struct page *page = kho_restore_page(PFN_PHYS(pfn), false);

		if (!page)
			return NULL;
		pfn += 1 << order;
	}

	return pfn_to_page(start_pfn);
}
EXPORT_SYMBOL_GPL(kho_restore_pages);

/* Serialize and deserialize struct kho_mem_phys across kexec
 *
 * Record all the bitmaps in a linked list of pages for the next kernel to
 * process. Each chunk holds bitmaps of the same order and each block of bitmaps
 * starts at a given physical address. This allows the bitmaps to be sparse. The
 * xarray is used to store them in a tree while building up the data structure,
 * but the KHO successor kernel only needs to process them once in order.
 *
 * All of this memory is normal kmalloc() memory and is not marked for
 * preservation. The successor kernel will remain isolated to the scratch space
 * until it completes processing this list. Once processed all the memory
 * storing these ranges will be marked as free.
 */

struct khoser_mem_bitmap_ptr {
	phys_addr_t phys_start;
	DECLARE_KHOSER_PTR(bitmap, struct kho_mem_phys_bits *);
};

struct khoser_mem_chunk_hdr {
	DECLARE_KHOSER_PTR(next, struct khoser_mem_chunk *);
	unsigned int order;
	unsigned int num_elms;
};

#define KHOSER_BITMAP_SIZE                                   \
	((PAGE_SIZE - sizeof(struct khoser_mem_chunk_hdr)) / \
	 sizeof(struct khoser_mem_bitmap_ptr))

struct khoser_mem_chunk {
	struct khoser_mem_chunk_hdr hdr;
	struct khoser_mem_bitmap_ptr bitmaps[KHOSER_BITMAP_SIZE];
};

static_assert(sizeof(struct khoser_mem_chunk) == PAGE_SIZE);

static struct khoser_mem_chunk *new_chunk(struct khoser_mem_chunk *cur_chunk,
					  unsigned long order)
{
	struct khoser_mem_chunk *chunk __free(free_page) = NULL;

	chunk = (void *)get_zeroed_page(GFP_KERNEL);
	if (!chunk)
		return ERR_PTR(-ENOMEM);

	if (WARN_ON(kho_scratch_overlap(virt_to_phys(chunk), PAGE_SIZE)))
		return ERR_PTR(-EINVAL);

	chunk->hdr.order = order;
	if (cur_chunk)
		KHOSER_STORE_PTR(cur_chunk->hdr.next, chunk);
	return no_free_ptr(chunk);
}

static void kho_mem_ser_free(struct khoser_mem_chunk *first_chunk)
{
	struct khoser_mem_chunk *chunk = first_chunk;

	while (chunk) {
		struct khoser_mem_chunk *tmp = chunk;

		chunk = KHOSER_LOAD_PTR(chunk->hdr.next);
		free_page((unsigned long)tmp);
	}
}

/*
 *  Update memory map property, if old one is found discard it via
 *  kho_mem_ser_free().
 */
static void kho_update_memory_map(struct khoser_mem_chunk *first_chunk)
{
	void *ptr;
	u64 phys;

	ptr = fdt_getprop_w(kho_out.fdt, 0, PROP_PRESERVED_MEMORY_MAP, NULL);

	/* Check and discard previous memory map */
	phys = get_unaligned((u64 *)ptr);
	if (phys)
		kho_mem_ser_free((struct khoser_mem_chunk *)phys_to_virt(phys));

	/* Update with the new value */
	phys = first_chunk ? (u64)virt_to_phys(first_chunk) : 0;
	put_unaligned(phys, (u64 *)ptr);
}

static int kho_mem_serialize(struct kho_out *kho_out)
{
	struct khoser_mem_chunk *first_chunk = NULL;
	struct khoser_mem_chunk *chunk = NULL;
	struct kho_mem_phys *physxa;
	unsigned long order;
	int err = -ENOMEM;

	xa_for_each(&kho_out->track.orders, order, physxa) {
		struct kho_mem_phys_bits *bits;
		unsigned long phys;

		chunk = new_chunk(chunk, order);
		if (IS_ERR(chunk)) {
			err = PTR_ERR(chunk);
			goto err_free;
		}

		if (!first_chunk)
			first_chunk = chunk;

		xa_for_each(&physxa->phys_bits, phys, bits) {
			struct khoser_mem_bitmap_ptr *elm;

			if (chunk->hdr.num_elms == ARRAY_SIZE(chunk->bitmaps)) {
				chunk = new_chunk(chunk, order);
				if (IS_ERR(chunk)) {
					err = PTR_ERR(chunk);
					goto err_free;
				}
			}

			elm = &chunk->bitmaps[chunk->hdr.num_elms];
			chunk->hdr.num_elms++;
			elm->phys_start = (phys * PRESERVE_BITS)
					  << (order + PAGE_SHIFT);
			KHOSER_STORE_PTR(elm->bitmap, bits);
		}
	}

	kho_update_memory_map(first_chunk);

	return 0;

err_free:
	kho_mem_ser_free(first_chunk);
	return err;
}

static void __init deserialize_bitmap(unsigned int order,
				      struct khoser_mem_bitmap_ptr *elm)
{
	struct kho_mem_phys_bits *bitmap = KHOSER_LOAD_PTR(elm->bitmap);
	unsigned long bit;

	for_each_set_bit(bit, bitmap->preserve, PRESERVE_BITS) {
		int sz = 1 << (order + PAGE_SHIFT);
		phys_addr_t phys =
			elm->phys_start + (bit << (order + PAGE_SHIFT));
		struct page *page = phys_to_page(phys);
		union kho_page_info info;

		memblock_reserve(phys, sz);
		memblock_reserved_mark_noinit(phys, sz);
		info.magic = KHO_PAGE_MAGIC;
		info.order = order;
		page->private = info.page_private;
	}
}

/* Returns physical address of the preserved memory map from FDT */
static phys_addr_t __init kho_get_mem_map_phys(const void *fdt)
{
	const void *mem_ptr;
	int len;

	mem_ptr = fdt_getprop(fdt, 0, PROP_PRESERVED_MEMORY_MAP, &len);
	if (!mem_ptr || len != sizeof(u64)) {
		pr_err("failed to get preserved memory bitmaps\n");
		return 0;
	}

	return get_unaligned((const u64 *)mem_ptr);
}

static void __init kho_mem_deserialize(struct khoser_mem_chunk *chunk)
{
	while (chunk) {
		unsigned int i;

		for (i = 0; i != chunk->hdr.num_elms; i++)
			deserialize_bitmap(chunk->hdr.order,
					   &chunk->bitmaps[i]);
		chunk = KHOSER_LOAD_PTR(chunk->hdr.next);
	}
}

/*
 * With KHO enabled, memory can become fragmented because KHO regions may
 * be anywhere in physical address space. The scratch regions give us a
 * safe zones that we will never see KHO allocations from. This is where we
 * can later safely load our new kexec images into and then use the scratch
 * area for early allocations that happen before page allocator is
 * initialized.
 */
struct kho_scratch *kho_scratch;
unsigned int kho_scratch_cnt;

/*
 * The scratch areas are scaled by default as percent of memory allocated from
 * memblock. A user can override the scale with command line parameter:
 *
 * kho_scratch=N%
 *
 * It is also possible to explicitly define size for a lowmem, a global and
 * per-node scratch areas:
 *
 * kho_scratch=l[KMG],n[KMG],m[KMG]
 *
 * The explicit size definition takes precedence over scale definition.
 */
static unsigned int scratch_scale __initdata = 200;
static phys_addr_t scratch_size_global __initdata;
static phys_addr_t scratch_size_pernode __initdata;
static phys_addr_t scratch_size_lowmem __initdata;

static int __init kho_parse_scratch_size(char *p)
{
	size_t len;
	unsigned long sizes[3];
	size_t total_size = 0;
	int i;

	if (!p)
		return -EINVAL;

	len = strlen(p);
	if (!len)
		return -EINVAL;

	/* parse nn% */
	if (p[len - 1] == '%') {
		/* unsigned int max is 4,294,967,295, 10 chars */
		char s_scale[11] = {};
		int ret = 0;

		if (len > ARRAY_SIZE(s_scale))
			return -EINVAL;

		memcpy(s_scale, p, len - 1);
		ret = kstrtouint(s_scale, 10, &scratch_scale);
		if (!ret)
			pr_notice("scratch scale is %d%%\n", scratch_scale);
		return ret;
	}

	/* parse ll[KMG],mm[KMG],nn[KMG] */
	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		char *endp = p;

		if (i > 0) {
			if (*p != ',')
				return -EINVAL;
			p += 1;
		}

		sizes[i] = memparse(p, &endp);
		if (endp == p)
			return -EINVAL;
		p = endp;
		total_size += sizes[i];
	}

	if (!total_size)
		return -EINVAL;

	/* The string should be fully consumed by now. */
	if (*p)
		return -EINVAL;

	scratch_size_lowmem = sizes[0];
	scratch_size_global = sizes[1];
	scratch_size_pernode = sizes[2];
	scratch_scale = 0;

	pr_notice("scratch areas: lowmem: %lluMiB global: %lluMiB pernode: %lldMiB\n",
		  (u64)(scratch_size_lowmem >> 20),
		  (u64)(scratch_size_global >> 20),
		  (u64)(scratch_size_pernode >> 20));

	return 0;
}
early_param("kho_scratch", kho_parse_scratch_size);

static void __init scratch_size_update(void)
{
	phys_addr_t size;

	if (!scratch_scale)
		return;

	size = memblock_reserved_kern_size(ARCH_LOW_ADDRESS_LIMIT,
					   NUMA_NO_NODE);
	size = size * scratch_scale / 100;
	scratch_size_lowmem = round_up(size, CMA_MIN_ALIGNMENT_BYTES);

	size = memblock_reserved_kern_size(MEMBLOCK_ALLOC_ANYWHERE,
					   NUMA_NO_NODE);
	size = size * scratch_scale / 100 - scratch_size_lowmem;
	scratch_size_global = round_up(size, CMA_MIN_ALIGNMENT_BYTES);
}

static phys_addr_t __init scratch_size_node(int nid)
{
	phys_addr_t size;

	if (scratch_scale) {
		size = memblock_reserved_kern_size(MEMBLOCK_ALLOC_ANYWHERE,
						   nid);
		size = size * scratch_scale / 100;
	} else {
		size = scratch_size_pernode;
	}

	return round_up(size, CMA_MIN_ALIGNMENT_BYTES);
}

/**
 * kho_reserve_scratch - Reserve a contiguous chunk of memory for kexec
 *
 * With KHO we can preserve arbitrary pages in the system. To ensure we still
 * have a large contiguous region of memory when we search the physical address
 * space for target memory, let's make sure we always have a large CMA region
 * active. This CMA region will only be used for movable pages which are not a
 * problem for us during KHO because we can just move them somewhere else.
 */
static void __init kho_reserve_scratch(void)
{
	phys_addr_t addr, size;
	int nid, i = 0;

	if (!kho_enable)
		return;

	scratch_size_update();

	/* FIXME: deal with node hot-plug/remove */
	kho_scratch_cnt = num_online_nodes() + 2;
	size = kho_scratch_cnt * sizeof(*kho_scratch);
	kho_scratch = memblock_alloc(size, PAGE_SIZE);
	if (!kho_scratch)
		goto err_disable_kho;

	/*
	 * reserve scratch area in low memory for lowmem allocations in the
	 * next kernel
	 */
	size = scratch_size_lowmem;
	addr = memblock_phys_alloc_range(size, CMA_MIN_ALIGNMENT_BYTES, 0,
					 ARCH_LOW_ADDRESS_LIMIT);
	if (!addr)
		goto err_free_scratch_desc;

	kho_scratch[i].addr = addr;
	kho_scratch[i].size = size;
	i++;

	/* reserve large contiguous area for allocations without nid */
	size = scratch_size_global;
	addr = memblock_phys_alloc(size, CMA_MIN_ALIGNMENT_BYTES);
	if (!addr)
		goto err_free_scratch_areas;

	kho_scratch[i].addr = addr;
	kho_scratch[i].size = size;
	i++;

	for_each_online_node(nid) {
		size = scratch_size_node(nid);
		addr = memblock_alloc_range_nid(size, CMA_MIN_ALIGNMENT_BYTES,
						0, MEMBLOCK_ALLOC_ACCESSIBLE,
						nid, true);
		if (!addr)
			goto err_free_scratch_areas;

		kho_scratch[i].addr = addr;
		kho_scratch[i].size = size;
		i++;
	}

	return;

err_free_scratch_areas:
	for (i--; i >= 0; i--)
		memblock_phys_free(kho_scratch[i].addr, kho_scratch[i].size);
err_free_scratch_desc:
	memblock_free(kho_scratch, kho_scratch_cnt * sizeof(*kho_scratch));
err_disable_kho:
	pr_warn("Failed to reserve scratch area, disabling kexec handover\n");
	kho_enable = false;
}

/**
 * kho_add_subtree - record the physical address of a sub FDT in KHO root tree.
 * @name: name of the sub tree.
 * @fdt: the sub tree blob.
 *
 * Creates a new child node named @name in KHO root FDT and records
 * the physical address of @fdt. The pages of @fdt must also be preserved
 * by KHO for the new kernel to retrieve it after kexec.
 *
 * A debugfs blob entry is also created at
 * ``/sys/kernel/debug/kho/out/sub_fdts/@name`` when kernel is configured with
 * CONFIG_KEXEC_HANDOVER_DEBUGFS
 *
 * Return: 0 on success, error code on failure
 */
int kho_add_subtree(const char *name, void *fdt)
{
	phys_addr_t phys = virt_to_phys(fdt);
	void *root_fdt = kho_out.fdt;
	int err = -ENOMEM;
	int off, fdt_err;

	guard(mutex)(&kho_out.lock);

	fdt_err = fdt_open_into(root_fdt, root_fdt, PAGE_SIZE);
	if (fdt_err < 0)
		return err;

	off = fdt_add_subnode(root_fdt, 0, name);
	if (off < 0) {
		if (off == -FDT_ERR_EXISTS)
			err = -EEXIST;
		goto out_pack;
	}

	err = fdt_setprop(root_fdt, off, PROP_SUB_FDT, &phys, sizeof(phys));
	if (err < 0)
		goto out_pack;

	WARN_ON_ONCE(kho_debugfs_fdt_add(&kho_out.dbg, name, fdt, false));

out_pack:
	fdt_pack(root_fdt);

	return err;
}
EXPORT_SYMBOL_GPL(kho_add_subtree);

void kho_remove_subtree(void *fdt)
{
	phys_addr_t target_phys = virt_to_phys(fdt);
	void *root_fdt = kho_out.fdt;
	int off;
	int err;

	guard(mutex)(&kho_out.lock);

	err = fdt_open_into(root_fdt, root_fdt, PAGE_SIZE);
	if (err < 0)
		return;

	for (off = fdt_first_subnode(root_fdt, 0); off >= 0;
	     off = fdt_next_subnode(root_fdt, off)) {
		const u64 *val;
		int len;

		val = fdt_getprop(root_fdt, off, PROP_SUB_FDT, &len);
		if (!val || len != sizeof(phys_addr_t))
			continue;

		if ((phys_addr_t)*val == target_phys) {
			fdt_del_node(root_fdt, off);
			kho_debugfs_fdt_remove(&kho_out.dbg, fdt);
			break;
		}
	}

	fdt_pack(root_fdt);
}
EXPORT_SYMBOL_GPL(kho_remove_subtree);

/**
 * kho_preserve_folio - preserve a folio across kexec.
 * @folio: folio to preserve.
 *
 * Instructs KHO to preserve the whole folio across kexec. The order
 * will be preserved as well.
 *
 * Return: 0 on success, error code on failure
 */
int kho_preserve_folio(struct folio *folio)
{
	const unsigned long pfn = folio_pfn(folio);
	const unsigned int order = folio_order(folio);
	struct kho_mem_track *track = &kho_out.track;

	if (WARN_ON(kho_scratch_overlap(pfn << PAGE_SHIFT, PAGE_SIZE << order)))
		return -EINVAL;

	return __kho_preserve_order(track, pfn, order);
}
EXPORT_SYMBOL_GPL(kho_preserve_folio);

/**
 * kho_unpreserve_folio - unpreserve a folio.
 * @folio: folio to unpreserve.
 *
 * Instructs KHO to unpreserve a folio that was preserved by
 * kho_preserve_folio() before. The provided @folio (pfn and order)
 * must exactly match a previously preserved folio.
 */
void kho_unpreserve_folio(struct folio *folio)
{
	const unsigned long pfn = folio_pfn(folio);
	const unsigned int order = folio_order(folio);
	struct kho_mem_track *track = &kho_out.track;

	__kho_unpreserve_order(track, pfn, order);
}
EXPORT_SYMBOL_GPL(kho_unpreserve_folio);

/**
 * kho_preserve_pages - preserve contiguous pages across kexec
 * @page: first page in the list.
 * @nr_pages: number of pages.
 *
 * Preserve a contiguous list of order 0 pages. Must be restored using
 * kho_restore_pages() to ensure the pages are restored properly as order 0.
 *
 * Return: 0 on success, error code on failure
 */
int kho_preserve_pages(struct page *page, unsigned int nr_pages)
{
	struct kho_mem_track *track = &kho_out.track;
	const unsigned long start_pfn = page_to_pfn(page);
	const unsigned long end_pfn = start_pfn + nr_pages;
	unsigned long pfn = start_pfn;
	unsigned long failed_pfn = 0;
	int err = 0;

	if (WARN_ON(kho_scratch_overlap(start_pfn << PAGE_SHIFT,
					nr_pages << PAGE_SHIFT))) {
		return -EINVAL;
	}

	while (pfn < end_pfn) {
		const unsigned int order =
			min(count_trailing_zeros(pfn), ilog2(end_pfn - pfn));

		err = __kho_preserve_order(track, pfn, order);
		if (err) {
			failed_pfn = pfn;
			break;
		}

		pfn += 1 << order;
	}

	if (err)
		__kho_unpreserve(track, start_pfn, failed_pfn);

	return err;
}
EXPORT_SYMBOL_GPL(kho_preserve_pages);

/**
 * kho_unpreserve_pages - unpreserve contiguous pages.
 * @page: first page in the list.
 * @nr_pages: number of pages.
 *
 * Instructs KHO to unpreserve @nr_pages contiguous pages starting from @page.
 * This must be called with the same @page and @nr_pages as the corresponding
 * kho_preserve_pages() call. Unpreserving arbitrary sub-ranges of larger
 * preserved blocks is not supported.
 */
void kho_unpreserve_pages(struct page *page, unsigned int nr_pages)
{
	struct kho_mem_track *track = &kho_out.track;
	const unsigned long start_pfn = page_to_pfn(page);
	const unsigned long end_pfn = start_pfn + nr_pages;

	__kho_unpreserve(track, start_pfn, end_pfn);
}
EXPORT_SYMBOL_GPL(kho_unpreserve_pages);

struct kho_vmalloc_hdr {
	DECLARE_KHOSER_PTR(next, struct kho_vmalloc_chunk *);
};

#define KHO_VMALLOC_SIZE				\
	((PAGE_SIZE - sizeof(struct kho_vmalloc_hdr)) / \
	 sizeof(phys_addr_t))

struct kho_vmalloc_chunk {
	struct kho_vmalloc_hdr hdr;
	phys_addr_t phys[KHO_VMALLOC_SIZE];
};

static_assert(sizeof(struct kho_vmalloc_chunk) == PAGE_SIZE);

/* vmalloc flags KHO supports */
#define KHO_VMALLOC_SUPPORTED_FLAGS	(VM_ALLOC | VM_ALLOW_HUGE_VMAP)

/* KHO internal flags for vmalloc preservations */
#define KHO_VMALLOC_ALLOC	0x0001
#define KHO_VMALLOC_HUGE_VMAP	0x0002

static unsigned short vmalloc_flags_to_kho(unsigned int vm_flags)
{
	unsigned short kho_flags = 0;

	if (vm_flags & VM_ALLOC)
		kho_flags |= KHO_VMALLOC_ALLOC;
	if (vm_flags & VM_ALLOW_HUGE_VMAP)
		kho_flags |= KHO_VMALLOC_HUGE_VMAP;

	return kho_flags;
}

static unsigned int kho_flags_to_vmalloc(unsigned short kho_flags)
{
	unsigned int vm_flags = 0;

	if (kho_flags & KHO_VMALLOC_ALLOC)
		vm_flags |= VM_ALLOC;
	if (kho_flags & KHO_VMALLOC_HUGE_VMAP)
		vm_flags |= VM_ALLOW_HUGE_VMAP;

	return vm_flags;
}

static struct kho_vmalloc_chunk *new_vmalloc_chunk(struct kho_vmalloc_chunk *cur)
{
	struct kho_vmalloc_chunk *chunk;
	int err;

	chunk = (struct kho_vmalloc_chunk *)get_zeroed_page(GFP_KERNEL);
	if (!chunk)
		return NULL;

	err = kho_preserve_pages(virt_to_page(chunk), 1);
	if (err)
		goto err_free;
	if (cur)
		KHOSER_STORE_PTR(cur->hdr.next, chunk);
	return chunk;

err_free:
	free_page((unsigned long)chunk);
	return NULL;
}

static void kho_vmalloc_unpreserve_chunk(struct kho_vmalloc_chunk *chunk,
					 unsigned short order)
{
	struct kho_mem_track *track = &kho_out.track;
	unsigned long pfn = PHYS_PFN(virt_to_phys(chunk));

	__kho_unpreserve(track, pfn, pfn + 1);

	for (int i = 0; i < ARRAY_SIZE(chunk->phys) && chunk->phys[i]; i++) {
		pfn = PHYS_PFN(chunk->phys[i]);
		__kho_unpreserve(track, pfn, pfn + (1 << order));
	}
}

/**
 * kho_preserve_vmalloc - preserve memory allocated with vmalloc() across kexec
 * @ptr: pointer to the area in vmalloc address space
 * @preservation: placeholder for preservation metadata
 *
 * Instructs KHO to preserve the area in vmalloc address space at @ptr. The
 * physical pages mapped at @ptr will be preserved and on successful return
 * @preservation will hold the physical address of a structure that describes
 * the preservation.
 *
 * NOTE: The memory allocated with vmalloc_node() variants cannot be reliably
 * restored on the same node
 *
 * Return: 0 on success, error code on failure
 */
int kho_preserve_vmalloc(void *ptr, struct kho_vmalloc *preservation)
{
	struct kho_vmalloc_chunk *chunk;
	struct vm_struct *vm = find_vm_area(ptr);
	unsigned int order, flags, nr_contig_pages;
	unsigned int idx = 0;
	int err;

	if (!vm)
		return -EINVAL;

	if (vm->flags & ~KHO_VMALLOC_SUPPORTED_FLAGS)
		return -EOPNOTSUPP;

	flags = vmalloc_flags_to_kho(vm->flags);
	order = get_vm_area_page_order(vm);

	chunk = new_vmalloc_chunk(NULL);
	if (!chunk)
		return -ENOMEM;
	KHOSER_STORE_PTR(preservation->first, chunk);

	nr_contig_pages = (1 << order);
	for (int i = 0; i < vm->nr_pages; i += nr_contig_pages) {
		phys_addr_t phys = page_to_phys(vm->pages[i]);

		err = kho_preserve_pages(vm->pages[i], nr_contig_pages);
		if (err)
			goto err_free;

		chunk->phys[idx++] = phys;
		if (idx == ARRAY_SIZE(chunk->phys)) {
			chunk = new_vmalloc_chunk(chunk);
			if (!chunk) {
				err = -ENOMEM;
				goto err_free;
			}
			idx = 0;
		}
	}

	preservation->total_pages = vm->nr_pages;
	preservation->flags = flags;
	preservation->order = order;

	return 0;

err_free:
	kho_unpreserve_vmalloc(preservation);
	return err;
}
EXPORT_SYMBOL_GPL(kho_preserve_vmalloc);

/**
 * kho_unpreserve_vmalloc - unpreserve memory allocated with vmalloc()
 * @preservation: preservation metadata returned by kho_preserve_vmalloc()
 *
 * Instructs KHO to unpreserve the area in vmalloc address space that was
 * previously preserved with kho_preserve_vmalloc().
 */
void kho_unpreserve_vmalloc(struct kho_vmalloc *preservation)
{
	struct kho_vmalloc_chunk *chunk = KHOSER_LOAD_PTR(preservation->first);

	while (chunk) {
		struct kho_vmalloc_chunk *tmp = chunk;

		kho_vmalloc_unpreserve_chunk(chunk, preservation->order);

		chunk = KHOSER_LOAD_PTR(chunk->hdr.next);
		free_page((unsigned long)tmp);
	}
}
EXPORT_SYMBOL_GPL(kho_unpreserve_vmalloc);

/**
 * kho_restore_vmalloc - recreates and populates an area in vmalloc address
 * space from the preserved memory.
 * @preservation: preservation metadata.
 *
 * Recreates an area in vmalloc address space and populates it with memory that
 * was preserved using kho_preserve_vmalloc().
 *
 * Return: pointer to the area in the vmalloc address space, NULL on failure.
 */
void *kho_restore_vmalloc(const struct kho_vmalloc *preservation)
{
	struct kho_vmalloc_chunk *chunk = KHOSER_LOAD_PTR(preservation->first);
	unsigned int align, order, shift, vm_flags;
	unsigned long total_pages, contig_pages;
	unsigned long addr, size;
	struct vm_struct *area;
	struct page **pages;
	unsigned int idx = 0;
	int err;

	vm_flags = kho_flags_to_vmalloc(preservation->flags);
	if (vm_flags & ~KHO_VMALLOC_SUPPORTED_FLAGS)
		return NULL;

	total_pages = preservation->total_pages;
	pages = kvmalloc_array(total_pages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return NULL;
	order = preservation->order;
	contig_pages = (1 << order);
	shift = PAGE_SHIFT + order;
	align = 1 << shift;

	while (chunk) {
		struct page *page;

		for (int i = 0; i < ARRAY_SIZE(chunk->phys) && chunk->phys[i]; i++) {
			phys_addr_t phys = chunk->phys[i];

			if (idx + contig_pages > total_pages)
				goto err_free_pages_array;

			page = kho_restore_pages(phys, contig_pages);
			if (!page)
				goto err_free_pages_array;

			for (int j = 0; j < contig_pages; j++)
				pages[idx++] = page + j;

			phys += contig_pages * PAGE_SIZE;
		}

		page = kho_restore_pages(virt_to_phys(chunk), 1);
		if (!page)
			goto err_free_pages_array;
		chunk = KHOSER_LOAD_PTR(chunk->hdr.next);
		__free_page(page);
	}

	if (idx != total_pages)
		goto err_free_pages_array;

	area = __get_vm_area_node(total_pages * PAGE_SIZE, align, shift,
				  vm_flags, VMALLOC_START, VMALLOC_END,
				  NUMA_NO_NODE, GFP_KERNEL,
				  __builtin_return_address(0));
	if (!area)
		goto err_free_pages_array;

	addr = (unsigned long)area->addr;
	size = get_vm_area_size(area);
	err = vmap_pages_range(addr, addr + size, PAGE_KERNEL, pages, shift);
	if (err)
		goto err_free_vm_area;

	area->nr_pages = total_pages;
	area->pages = pages;

	return area->addr;

err_free_vm_area:
	free_vm_area(area);
err_free_pages_array:
	kvfree(pages);
	return NULL;
}
EXPORT_SYMBOL_GPL(kho_restore_vmalloc);

/**
 * kho_alloc_preserve - Allocate, zero, and preserve memory.
 * @size: The number of bytes to allocate.
 *
 * Allocates a physically contiguous block of zeroed pages that is large
 * enough to hold @size bytes. The allocated memory is then registered with
 * KHO for preservation across a kexec.
 *
 * Note: The actual allocated size will be rounded up to the nearest
 * power-of-two page boundary.
 *
 * @return A virtual pointer to the allocated and preserved memory on success,
 * or an ERR_PTR() encoded error on failure.
 */
void *kho_alloc_preserve(size_t size)
{
	struct folio *folio;
	int order, ret;

	if (!size)
		return ERR_PTR(-EINVAL);

	order = get_order(size);
	if (order > MAX_PAGE_ORDER)
		return ERR_PTR(-E2BIG);

	folio = folio_alloc(GFP_KERNEL | __GFP_ZERO, order);
	if (!folio)
		return ERR_PTR(-ENOMEM);

	ret = kho_preserve_folio(folio);
	if (ret) {
		folio_put(folio);
		return ERR_PTR(ret);
	}

	return folio_address(folio);
}
EXPORT_SYMBOL_GPL(kho_alloc_preserve);

/**
 * kho_unpreserve_free - Unpreserve and free memory.
 * @mem:  Pointer to the memory allocated by kho_alloc_preserve().
 *
 * Unregisters the memory from KHO preservation and frees the underlying
 * pages back to the system. This function should be called to clean up
 * memory allocated with kho_alloc_preserve().
 */
void kho_unpreserve_free(void *mem)
{
	struct folio *folio;

	if (!mem)
		return;

	folio = virt_to_folio(mem);
	kho_unpreserve_folio(folio);
	folio_put(folio);
}
EXPORT_SYMBOL_GPL(kho_unpreserve_free);

/**
 * kho_restore_free - Restore and free memory after kexec.
 * @mem:  Pointer to the memory (in the new kernel's address space)
 * that was allocated by the old kernel.
 *
 * This function is intended to be called in the new kernel (post-kexec)
 * to take ownership of and free a memory region that was preserved by the
 * old kernel using kho_alloc_preserve().
 *
 * It first restores the pages from KHO (using their physical address)
 * and then frees the pages back to the new kernel's page allocator.
 */
void kho_restore_free(void *mem)
{
	struct folio *folio;

	if (!mem)
		return;

	folio = kho_restore_folio(__pa(mem));
	if (!WARN_ON(!folio))
		folio_put(folio);
}
EXPORT_SYMBOL_GPL(kho_restore_free);

int kho_finalize(void)
{
	int ret;

	if (!kho_enable)
		return -EOPNOTSUPP;

	guard(mutex)(&kho_out.lock);
	ret = kho_mem_serialize(&kho_out);
	if (ret)
		return ret;

	kho_out.finalized = true;

	return 0;
}

bool kho_finalized(void)
{
	guard(mutex)(&kho_out.lock);
	return kho_out.finalized;
}

struct kho_in {
	phys_addr_t fdt_phys;
	phys_addr_t scratch_phys;
	phys_addr_t mem_map_phys;
	struct kho_debugfs dbg;
};

static struct kho_in kho_in = {
};

static const void *kho_get_fdt(void)
{
	return kho_in.fdt_phys ? phys_to_virt(kho_in.fdt_phys) : NULL;
}

/**
 * is_kho_boot - check if current kernel was booted via KHO-enabled
 * kexec
 *
 * This function checks if the current kernel was loaded through a kexec
 * operation with KHO enabled, by verifying that a valid KHO FDT
 * was passed.
 *
 * Note: This function returns reliable results only after
 * kho_populate() has been called during early boot. Before that,
 * it may return false even if KHO data is present.
 *
 * Return: true if booted via KHO-enabled kexec, false otherwise
 */
bool is_kho_boot(void)
{
	return !!kho_get_fdt();
}
EXPORT_SYMBOL_GPL(is_kho_boot);

/**
 * kho_retrieve_subtree - retrieve a preserved sub FDT by its name.
 * @name: the name of the sub FDT passed to kho_add_subtree().
 * @phys: if found, the physical address of the sub FDT is stored in @phys.
 *
 * Retrieve a preserved sub FDT named @name and store its physical
 * address in @phys.
 *
 * Return: 0 on success, error code on failure
 */
int kho_retrieve_subtree(const char *name, phys_addr_t *phys)
{
	const void *fdt = kho_get_fdt();
	const u64 *val;
	int offset, len;

	if (!fdt)
		return -ENOENT;

	if (!phys)
		return -EINVAL;

	offset = fdt_subnode_offset(fdt, 0, name);
	if (offset < 0)
		return -ENOENT;

	val = fdt_getprop(fdt, offset, PROP_SUB_FDT, &len);
	if (!val || len != sizeof(*val))
		return -EINVAL;

	*phys = (phys_addr_t)*val;

	return 0;
}
EXPORT_SYMBOL_GPL(kho_retrieve_subtree);

static __init int kho_out_fdt_setup(void)
{
	void *root = kho_out.fdt;
	u64 empty_mem_map = 0;
	int err;

	err = fdt_create(root, PAGE_SIZE);
	err |= fdt_finish_reservemap(root);
	err |= fdt_begin_node(root, "");
	err |= fdt_property_string(root, "compatible", KHO_FDT_COMPATIBLE);
	err |= fdt_property(root, PROP_PRESERVED_MEMORY_MAP, &empty_mem_map,
			    sizeof(empty_mem_map));
	err |= fdt_end_node(root);
	err |= fdt_finish(root);

	return err;
}

static __init int kho_init(void)
{
	const void *fdt = kho_get_fdt();
	int err = 0;

	if (!kho_enable)
		return 0;

	kho_out.fdt = kho_alloc_preserve(PAGE_SIZE);
	if (IS_ERR(kho_out.fdt)) {
		err = PTR_ERR(kho_out.fdt);
		goto err_free_scratch;
	}

	err = kho_debugfs_init();
	if (err)
		goto err_free_fdt;

	err = kho_out_debugfs_init(&kho_out.dbg);
	if (err)
		goto err_free_fdt;

	err = kho_out_fdt_setup();
	if (err)
		goto err_free_fdt;

	if (fdt) {
		kho_in_debugfs_init(&kho_in.dbg, fdt);
		return 0;
	}

	for (int i = 0; i < kho_scratch_cnt; i++) {
		unsigned long base_pfn = PHYS_PFN(kho_scratch[i].addr);
		unsigned long count = kho_scratch[i].size >> PAGE_SHIFT;
		unsigned long pfn;

		/*
		 * When debug_pagealloc is enabled, __free_pages() clears the
		 * corresponding PRESENT bit in the kernel page table.
		 * Subsequent kmemleak scans of these pages cause the
		 * non-PRESENT page faults.
		 * Mark scratch areas with kmemleak_ignore_phys() to exclude
		 * them from kmemleak scanning.
		 */
		kmemleak_ignore_phys(kho_scratch[i].addr);
		for (pfn = base_pfn; pfn < base_pfn + count;
		     pfn += pageblock_nr_pages)
			init_cma_reserved_pageblock(pfn_to_page(pfn));
	}

	WARN_ON_ONCE(kho_debugfs_fdt_add(&kho_out.dbg, "fdt",
					 kho_out.fdt, true));

	return 0;

err_free_fdt:
	kho_unpreserve_free(kho_out.fdt);
err_free_scratch:
	kho_out.fdt = NULL;
	for (int i = 0; i < kho_scratch_cnt; i++) {
		void *start = __va(kho_scratch[i].addr);
		void *end = start + kho_scratch[i].size;

		free_reserved_area(start, end, -1, "");
	}
	kho_enable = false;
	return err;
}
fs_initcall(kho_init);

static void __init kho_release_scratch(void)
{
	phys_addr_t start, end;
	u64 i;

	memmap_init_kho_scratch_pages();

	/*
	 * Mark scratch mem as CMA before we return it. That way we
	 * ensure that no kernel allocations happen on it. That means
	 * we can reuse it as scratch memory again later.
	 */
	__for_each_mem_range(i, &memblock.memory, NULL, NUMA_NO_NODE,
			     MEMBLOCK_KHO_SCRATCH, &start, &end, NULL) {
		ulong start_pfn = pageblock_start_pfn(PFN_DOWN(start));
		ulong end_pfn = pageblock_align(PFN_UP(end));
		ulong pfn;

		for (pfn = start_pfn; pfn < end_pfn; pfn += pageblock_nr_pages)
			init_pageblock_migratetype(pfn_to_page(pfn),
						   MIGRATE_CMA, false);
	}
}

void __init kho_memory_init(void)
{
	if (kho_in.mem_map_phys) {
		kho_scratch = phys_to_virt(kho_in.scratch_phys);
		kho_release_scratch();
		kho_mem_deserialize(phys_to_virt(kho_in.mem_map_phys));
	} else {
		kho_reserve_scratch();
	}
}

void __init kho_populate(phys_addr_t fdt_phys, u64 fdt_len,
			 phys_addr_t scratch_phys, u64 scratch_len)
{
	struct kho_scratch *scratch = NULL;
	phys_addr_t mem_map_phys;
	void *fdt = NULL;
	int err = 0;
	unsigned int scratch_cnt = scratch_len / sizeof(*kho_scratch);

	/* Validate the input FDT */
	fdt = early_memremap(fdt_phys, fdt_len);
	if (!fdt) {
		pr_warn("setup: failed to memremap FDT (0x%llx)\n", fdt_phys);
		err = -EFAULT;
		goto out;
	}
	err = fdt_check_header(fdt);
	if (err) {
		pr_warn("setup: handover FDT (0x%llx) is invalid: %d\n",
			fdt_phys, err);
		err = -EINVAL;
		goto out;
	}
	err = fdt_node_check_compatible(fdt, 0, KHO_FDT_COMPATIBLE);
	if (err) {
		pr_warn("setup: handover FDT (0x%llx) is incompatible with '%s': %d\n",
			fdt_phys, KHO_FDT_COMPATIBLE, err);
		err = -EINVAL;
		goto out;
	}

	mem_map_phys = kho_get_mem_map_phys(fdt);
	if (!mem_map_phys) {
		err = -ENOENT;
		goto out;
	}

	scratch = early_memremap(scratch_phys, scratch_len);
	if (!scratch) {
		pr_warn("setup: failed to memremap scratch (phys=0x%llx, len=%lld)\n",
			scratch_phys, scratch_len);
		err = -EFAULT;
		goto out;
	}

	/*
	 * We pass a safe contiguous blocks of memory to use for early boot
	 * purporses from the previous kernel so that we can resize the
	 * memblock array as needed.
	 */
	for (int i = 0; i < scratch_cnt; i++) {
		struct kho_scratch *area = &scratch[i];
		u64 size = area->size;

		memblock_add(area->addr, size);
		err = memblock_mark_kho_scratch(area->addr, size);
		if (WARN_ON(err)) {
			pr_warn("failed to mark the scratch region 0x%pa+0x%pa: %pe",
				&area->addr, &size, ERR_PTR(err));
			goto out;
		}
		pr_debug("Marked 0x%pa+0x%pa as scratch", &area->addr, &size);
	}

	memblock_reserve(scratch_phys, scratch_len);

	/*
	 * Now that we have a viable region of scratch memory, let's tell
	 * the memblocks allocator to only use that for any allocations.
	 * That way we ensure that nothing scribbles over in use data while
	 * we initialize the page tables which we will need to ingest all
	 * memory reservations from the previous kernel.
	 */
	memblock_set_kho_scratch_only();

	kho_in.fdt_phys = fdt_phys;
	kho_in.scratch_phys = scratch_phys;
	kho_in.mem_map_phys = mem_map_phys;
	kho_scratch_cnt = scratch_cnt;
	pr_info("found kexec handover data.\n");

out:
	if (fdt)
		early_memunmap(fdt, fdt_len);
	if (scratch)
		early_memunmap(scratch, scratch_len);
	if (err)
		pr_warn("disabling KHO revival: %d\n", err);
}

/* Helper functions for kexec_file_load */

int kho_fill_kimage(struct kimage *image)
{
	ssize_t scratch_size;
	int err = 0;
	struct kexec_buf scratch;

	if (!kho_enable)
		return 0;

	image->kho.fdt = virt_to_phys(kho_out.fdt);

	scratch_size = sizeof(*kho_scratch) * kho_scratch_cnt;
	scratch = (struct kexec_buf){
		.image = image,
		.buffer = kho_scratch,
		.bufsz = scratch_size,
		.mem = KEXEC_BUF_MEM_UNKNOWN,
		.memsz = scratch_size,
		.buf_align = SZ_64K, /* Makes it easier to map */
		.buf_max = ULONG_MAX,
		.top_down = true,
	};
	err = kexec_add_buffer(&scratch);
	if (err)
		return err;
	image->kho.scratch = &image->segment[image->nr_segments - 1];

	return 0;
}

static int kho_walk_scratch(struct kexec_buf *kbuf,
			    int (*func)(struct resource *, void *))
{
	int ret = 0;
	int i;

	for (i = 0; i < kho_scratch_cnt; i++) {
		struct resource res = {
			.start = kho_scratch[i].addr,
			.end = kho_scratch[i].addr + kho_scratch[i].size - 1,
		};

		/* Try to fit the kimage into our KHO scratch region */
		ret = func(&res, kbuf);
		if (ret)
			break;
	}

	return ret;
}

int kho_locate_mem_hole(struct kexec_buf *kbuf,
			int (*func)(struct resource *, void *))
{
	int ret;

	if (!kho_enable || kbuf->image->type == KEXEC_TYPE_CRASH)
		return 1;

	ret = kho_walk_scratch(kbuf, func);

	return ret == 1 ? 0 : -EADDRNOTAVAIL;
}
