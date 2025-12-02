// SPDX-License-Identifier: GPL-2.0-only
/*
 * kexec_handover.c - kexec handover metadata processing
 * Copyright (C) 2023 Alexander Graf <graf@amazon.com>
 * Copyright (C) 2025 Microsoft Corporation, Mike Rapoport <rppt@kernel.org>
 * Copyright (C) 2025 Google LLC, Changyuan Lyu <changyuanl@google.com>
 */

#define pr_fmt(fmt) "KHO: " fmt

#include <linux/cleanup.h>
#include <linux/cma.h>
#include <linux/count_zeros.h>
#include <linux/debugfs.h>
#include <linux/kexec.h>
#include <linux/kexec_handover.h>
#include <linux/libfdt.h>
#include <linux/list.h>
#include <linux/memblock.h>
#include <linux/notifier.h>
#include <linux/page-isolation.h>
#include <linux/vmalloc.h>

#include <asm/early_ioremap.h>

#include "kexec_handover_internal.h"
/*
 * KHO is tightly coupled with mm init and needs access to some of mm
 * internal APIs.
 */
#include "../mm/internal.h"
#include "kexec_internal.h"

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

static bool kho_enable __ro_after_init;

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

struct kho_serialization {
	struct page *fdt;
	struct list_head fdt_list;
	struct dentry *sub_fdt_dir;
	struct kho_mem_track track;
	/* First chunk of serialized preserved memory map */
	struct khoser_mem_chunk *preserved_mem_map;
};

struct kho_out {
	struct blocking_notifier_head chain_head;

	struct dentry *dir;

	struct mutex lock; /* protects KHO FDT finalization */

	struct kho_serialization ser;
	bool finalized;
};

static struct kho_out kho_out = {
	.chain_head = BLOCKING_NOTIFIER_INIT(kho_out.chain_head),
	.lock = __MUTEX_INITIALIZER(kho_out.lock),
	.ser = {
		.fdt_list = LIST_HEAD_INIT(kho_out.ser.fdt_list),
		.track = {
			.orders = XARRAY_INIT(kho_out.ser.track.orders, 0),
		},
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

static void __kho_unpreserve(struct kho_mem_track *track, unsigned long pfn,
			     unsigned long end_pfn)
{
	struct kho_mem_phys_bits *bits;
	struct kho_mem_phys *physxa;

	while (pfn < end_pfn) {
		const unsigned int order =
			min(count_trailing_zeros(pfn), ilog2(end_pfn - pfn));
		const unsigned long pfn_high = pfn >> order;

		physxa = xa_load(&track->orders, order);
		if (WARN_ON_ONCE(!physxa))
			return;

		bits = xa_load(&physxa->phys_bits, pfn_high / PRESERVE_BITS);
		if (WARN_ON_ONCE(!bits))
			return;

		clear_bit(pfn_high % PRESERVE_BITS, bits->preserve);

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

	if (kho_out.finalized)
		return -EBUSY;

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

static struct page *kho_restore_page(phys_addr_t phys)
{
	struct page *page = pfn_to_online_page(PHYS_PFN(phys));
	union kho_page_info info;
	unsigned int nr_pages;

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

	/* For higher order folios, tail pages get a page count of zero. */
	for (unsigned int i = 1; i < nr_pages; i++)
		set_page_count(page + i, 0);

	if (info.order > 0)
		prep_compound_page(page, info.order);

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
	struct page *page = kho_restore_page(phys);

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
		struct page *page = kho_restore_page(PFN_PHYS(pfn));

		if (!page)
			return NULL;
		split_page(page, order);
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
		kfree(tmp);
	}
}

static int kho_mem_serialize(struct kho_serialization *ser)
{
	struct khoser_mem_chunk *first_chunk = NULL;
	struct khoser_mem_chunk *chunk = NULL;
	struct kho_mem_phys *physxa;
	unsigned long order;
	int err = -ENOMEM;

	xa_for_each(&ser->track.orders, order, physxa) {
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

	ser->preserved_mem_map = first_chunk;

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

static void __init kho_mem_deserialize(const void *fdt)
{
	struct khoser_mem_chunk *chunk;
	const phys_addr_t *mem;
	int len;

	mem = fdt_getprop(fdt, 0, PROP_PRESERVED_MEMORY_MAP, &len);

	if (!mem || len != sizeof(*mem)) {
		pr_err("failed to get preserved memory bitmaps\n");
		return;
	}

	chunk = *mem ? phys_to_virt(*mem) : NULL;
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

struct fdt_debugfs {
	struct list_head list;
	struct debugfs_blob_wrapper wrapper;
	struct dentry *file;
};

static int kho_debugfs_fdt_add(struct list_head *list, struct dentry *dir,
			       const char *name, const void *fdt)
{
	struct fdt_debugfs *f;
	struct dentry *file;

	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;

	f->wrapper.data = (void *)fdt;
	f->wrapper.size = fdt_totalsize(fdt);

	file = debugfs_create_blob(name, 0400, dir, &f->wrapper);
	if (IS_ERR(file)) {
		kfree(f);
		return PTR_ERR(file);
	}

	f->file = file;
	list_add(&f->list, list);

	return 0;
}

/**
 * kho_add_subtree - record the physical address of a sub FDT in KHO root tree.
 * @ser: serialization control object passed by KHO notifiers.
 * @name: name of the sub tree.
 * @fdt: the sub tree blob.
 *
 * Creates a new child node named @name in KHO root FDT and records
 * the physical address of @fdt. The pages of @fdt must also be preserved
 * by KHO for the new kernel to retrieve it after kexec.
 *
 * A debugfs blob entry is also created at
 * ``/sys/kernel/debug/kho/out/sub_fdts/@name``.
 *
 * Return: 0 on success, error code on failure
 */
int kho_add_subtree(struct kho_serialization *ser, const char *name, void *fdt)
{
	int err = 0;
	u64 phys = (u64)virt_to_phys(fdt);
	void *root = page_to_virt(ser->fdt);

	err |= fdt_begin_node(root, name);
	err |= fdt_property(root, PROP_SUB_FDT, &phys, sizeof(phys));
	err |= fdt_end_node(root);

	if (err)
		return err;

	return kho_debugfs_fdt_add(&ser->fdt_list, ser->sub_fdt_dir, name, fdt);
}
EXPORT_SYMBOL_GPL(kho_add_subtree);

int register_kho_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&kho_out.chain_head, nb);
}
EXPORT_SYMBOL_GPL(register_kho_notifier);

int unregister_kho_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&kho_out.chain_head, nb);
}
EXPORT_SYMBOL_GPL(unregister_kho_notifier);

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
	struct kho_mem_track *track = &kho_out.ser.track;

	if (WARN_ON(kho_scratch_overlap(pfn << PAGE_SHIFT, PAGE_SIZE << order)))
		return -EINVAL;

	return __kho_preserve_order(track, pfn, order);
}
EXPORT_SYMBOL_GPL(kho_preserve_folio);

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
	struct kho_mem_track *track = &kho_out.ser.track;
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
	struct kho_mem_track *track = &kho_out.ser.track;
	unsigned long pfn = PHYS_PFN(virt_to_phys(chunk));

	__kho_unpreserve(track, pfn, pfn + 1);

	for (int i = 0; i < ARRAY_SIZE(chunk->phys) && chunk->phys[i]; i++) {
		pfn = PHYS_PFN(chunk->phys[i]);
		__kho_unpreserve(track, pfn, pfn + (1 << order));
	}
}

static void kho_vmalloc_free_chunks(struct kho_vmalloc *kho_vmalloc)
{
	struct kho_vmalloc_chunk *chunk = KHOSER_LOAD_PTR(kho_vmalloc->first);

	while (chunk) {
		struct kho_vmalloc_chunk *tmp = chunk;

		kho_vmalloc_unpreserve_chunk(chunk, kho_vmalloc->order);

		chunk = KHOSER_LOAD_PTR(chunk->hdr.next);
		free_page((unsigned long)tmp);
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
			if (!chunk)
				goto err_free;
			idx = 0;
		}
	}

	preservation->total_pages = vm->nr_pages;
	preservation->flags = flags;
	preservation->order = order;

	return 0;

err_free:
	kho_vmalloc_free_chunks(preservation);
	return err;
}
EXPORT_SYMBOL_GPL(kho_preserve_vmalloc);

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
				pages[idx++] = page;

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

/* Handling for debug/kho/out */

static struct dentry *debugfs_root;

static int kho_out_update_debugfs_fdt(void)
{
	int err = 0;
	struct fdt_debugfs *ff, *tmp;

	if (kho_out.finalized) {
		err = kho_debugfs_fdt_add(&kho_out.ser.fdt_list, kho_out.dir,
					  "fdt", page_to_virt(kho_out.ser.fdt));
	} else {
		list_for_each_entry_safe(ff, tmp, &kho_out.ser.fdt_list, list) {
			debugfs_remove(ff->file);
			list_del(&ff->list);
			kfree(ff);
		}
	}

	return err;
}

static int kho_abort(void)
{
	int err;
	unsigned long order;
	struct kho_mem_phys *physxa;

	xa_for_each(&kho_out.ser.track.orders, order, physxa) {
		struct kho_mem_phys_bits *bits;
		unsigned long phys;

		xa_for_each(&physxa->phys_bits, phys, bits)
			kfree(bits);

		xa_destroy(&physxa->phys_bits);
		kfree(physxa);
	}
	xa_destroy(&kho_out.ser.track.orders);

	if (kho_out.ser.preserved_mem_map) {
		kho_mem_ser_free(kho_out.ser.preserved_mem_map);
		kho_out.ser.preserved_mem_map = NULL;
	}

	err = blocking_notifier_call_chain(&kho_out.chain_head, KEXEC_KHO_ABORT,
					   NULL);
	err = notifier_to_errno(err);

	if (err)
		pr_err("Failed to abort KHO finalization: %d\n", err);

	return err;
}

static int kho_finalize(void)
{
	int err = 0;
	u64 *preserved_mem_map;
	void *fdt = page_to_virt(kho_out.ser.fdt);

	err |= fdt_create(fdt, PAGE_SIZE);
	err |= fdt_finish_reservemap(fdt);
	err |= fdt_begin_node(fdt, "");
	err |= fdt_property_string(fdt, "compatible", KHO_FDT_COMPATIBLE);
	/**
	 * Reserve the preserved-memory-map property in the root FDT, so
	 * that all property definitions will precede subnodes created by
	 * KHO callers.
	 */
	err |= fdt_property_placeholder(fdt, PROP_PRESERVED_MEMORY_MAP,
					sizeof(*preserved_mem_map),
					(void **)&preserved_mem_map);
	if (err)
		goto abort;

	err = kho_preserve_folio(page_folio(kho_out.ser.fdt));
	if (err)
		goto abort;

	err = blocking_notifier_call_chain(&kho_out.chain_head,
					   KEXEC_KHO_FINALIZE, &kho_out.ser);
	err = notifier_to_errno(err);
	if (err)
		goto abort;

	err = kho_mem_serialize(&kho_out.ser);
	if (err)
		goto abort;

	*preserved_mem_map = (u64)virt_to_phys(kho_out.ser.preserved_mem_map);

	err |= fdt_end_node(fdt);
	err |= fdt_finish(fdt);

abort:
	if (err) {
		pr_err("Failed to convert KHO state tree: %d\n", err);
		kho_abort();
	}

	return err;
}

static int kho_out_finalize_get(void *data, u64 *val)
{
	mutex_lock(&kho_out.lock);
	*val = kho_out.finalized;
	mutex_unlock(&kho_out.lock);

	return 0;
}

static int kho_out_finalize_set(void *data, u64 _val)
{
	int ret = 0;
	bool val = !!_val;

	mutex_lock(&kho_out.lock);

	if (val == kho_out.finalized) {
		if (kho_out.finalized)
			ret = -EEXIST;
		else
			ret = -ENOENT;
		goto unlock;
	}

	if (val)
		ret = kho_finalize();
	else
		ret = kho_abort();

	if (ret)
		goto unlock;

	kho_out.finalized = val;
	ret = kho_out_update_debugfs_fdt();

unlock:
	mutex_unlock(&kho_out.lock);
	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_kho_out_finalize, kho_out_finalize_get,
			 kho_out_finalize_set, "%llu\n");

static int scratch_phys_show(struct seq_file *m, void *v)
{
	for (int i = 0; i < kho_scratch_cnt; i++)
		seq_printf(m, "0x%llx\n", kho_scratch[i].addr);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(scratch_phys);

static int scratch_len_show(struct seq_file *m, void *v)
{
	for (int i = 0; i < kho_scratch_cnt; i++)
		seq_printf(m, "0x%llx\n", kho_scratch[i].size);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(scratch_len);

static __init int kho_out_debugfs_init(void)
{
	struct dentry *dir, *f, *sub_fdt_dir;

	dir = debugfs_create_dir("out", debugfs_root);
	if (IS_ERR(dir))
		return -ENOMEM;

	sub_fdt_dir = debugfs_create_dir("sub_fdts", dir);
	if (IS_ERR(sub_fdt_dir))
		goto err_rmdir;

	f = debugfs_create_file("scratch_phys", 0400, dir, NULL,
				&scratch_phys_fops);
	if (IS_ERR(f))
		goto err_rmdir;

	f = debugfs_create_file("scratch_len", 0400, dir, NULL,
				&scratch_len_fops);
	if (IS_ERR(f))
		goto err_rmdir;

	f = debugfs_create_file("finalize", 0600, dir, NULL,
				&fops_kho_out_finalize);
	if (IS_ERR(f))
		goto err_rmdir;

	kho_out.dir = dir;
	kho_out.ser.sub_fdt_dir = sub_fdt_dir;
	return 0;

err_rmdir:
	debugfs_remove_recursive(dir);
	return -ENOENT;
}

struct kho_in {
	struct dentry *dir;
	phys_addr_t fdt_phys;
	phys_addr_t scratch_phys;
	struct list_head fdt_list;
};

static struct kho_in kho_in = {
	.fdt_list = LIST_HEAD_INIT(kho_in.fdt_list),
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

/* Handling for debugfs/kho/in */

static __init int kho_in_debugfs_init(const void *fdt)
{
	struct dentry *sub_fdt_dir;
	int err, child;

	kho_in.dir = debugfs_create_dir("in", debugfs_root);
	if (IS_ERR(kho_in.dir))
		return PTR_ERR(kho_in.dir);

	sub_fdt_dir = debugfs_create_dir("sub_fdts", kho_in.dir);
	if (IS_ERR(sub_fdt_dir)) {
		err = PTR_ERR(sub_fdt_dir);
		goto err_rmdir;
	}

	err = kho_debugfs_fdt_add(&kho_in.fdt_list, kho_in.dir, "fdt", fdt);
	if (err)
		goto err_rmdir;

	fdt_for_each_subnode(child, fdt, 0) {
		int len = 0;
		const char *name = fdt_get_name(fdt, child, NULL);
		const u64 *fdt_phys;

		fdt_phys = fdt_getprop(fdt, child, "fdt", &len);
		if (!fdt_phys)
			continue;
		if (len != sizeof(*fdt_phys)) {
			pr_warn("node `%s`'s prop `fdt` has invalid length: %d\n",
				name, len);
			continue;
		}
		err = kho_debugfs_fdt_add(&kho_in.fdt_list, sub_fdt_dir, name,
					  phys_to_virt(*fdt_phys));
		if (err) {
			pr_warn("failed to add fdt `%s` to debugfs: %d\n", name,
				err);
			continue;
		}
	}

	return 0;

err_rmdir:
	debugfs_remove_recursive(kho_in.dir);
	return err;
}

static __init int kho_init(void)
{
	int err = 0;
	const void *fdt = kho_get_fdt();

	if (!kho_enable)
		return 0;

	kho_out.ser.fdt = alloc_page(GFP_KERNEL);
	if (!kho_out.ser.fdt) {
		err = -ENOMEM;
		goto err_free_scratch;
	}

	debugfs_root = debugfs_create_dir("kho", NULL);
	if (IS_ERR(debugfs_root)) {
		err = -ENOENT;
		goto err_free_fdt;
	}

	err = kho_out_debugfs_init();
	if (err)
		goto err_free_fdt;

	if (fdt) {
		err = kho_in_debugfs_init(fdt);
		/*
		 * Failure to create /sys/kernel/debug/kho/in does not prevent
		 * reviving state from KHO and setting up KHO for the next
		 * kexec.
		 */
		if (err)
			pr_err("failed exposing handover FDT in debugfs: %d\n",
			       err);

		return 0;
	}

	for (int i = 0; i < kho_scratch_cnt; i++) {
		unsigned long base_pfn = PHYS_PFN(kho_scratch[i].addr);
		unsigned long count = kho_scratch[i].size >> PAGE_SHIFT;
		unsigned long pfn;

		for (pfn = base_pfn; pfn < base_pfn + count;
		     pfn += pageblock_nr_pages)
			init_cma_reserved_pageblock(pfn_to_page(pfn));
	}

	return 0;

err_free_fdt:
	put_page(kho_out.ser.fdt);
	kho_out.ser.fdt = NULL;
err_free_scratch:
	for (int i = 0; i < kho_scratch_cnt; i++) {
		void *start = __va(kho_scratch[i].addr);
		void *end = start + kho_scratch[i].size;

		free_reserved_area(start, end, -1, "");
	}
	kho_enable = false;
	return err;
}
late_initcall(kho_init);

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
	struct folio *folio;

	if (kho_in.scratch_phys) {
		kho_scratch = phys_to_virt(kho_in.scratch_phys);
		kho_release_scratch();

		kho_mem_deserialize(kho_get_fdt());
		folio = kho_restore_folio(kho_in.fdt_phys);
		if (!folio)
			pr_warn("failed to restore folio for KHO fdt\n");
	} else {
		kho_reserve_scratch();
	}
}

void __init kho_populate(phys_addr_t fdt_phys, u64 fdt_len,
			 phys_addr_t scratch_phys, u64 scratch_len)
{
	void *fdt = NULL;
	struct kho_scratch *scratch = NULL;
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
			pr_warn("failed to mark the scratch region 0x%pa+0x%pa: %d",
				&area->addr, &size, err);
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
	kho_scratch_cnt = scratch_cnt;
	pr_info("found kexec handover data. Will skip init for some devices\n");

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

	if (!kho_out.finalized)
		return 0;

	image->kho.fdt = page_to_phys(kho_out.ser.fdt);

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
