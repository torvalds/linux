// SPDX-License-Identifier: GPL-2.0-only
/*
 * kexec_handover.c - kexec handover metadata processing
 * Copyright (C) 2023 Alexander Graf <graf@amazon.com>
 * Copyright (C) 2025 Microsoft Corporation, Mike Rapoport <rppt@kernel.org>
 * Copyright (C) 2025 Google LLC, Changyuan Lyu <changyuanl@google.com>
 * Copyright (C) 2025 Pasha Tatashin <pasha.tatashin@soleen.com>
 * Copyright (C) 2026 Google LLC, Jason Miu <jasonmiu@google.com>
 */

#define pr_fmt(fmt) "KHO: " fmt

#include <linux/cleanup.h>
#include <linux/cma.h>
#include <linux/kmemleak.h>
#include <linux/count_zeros.h>
#include <linux/kexec.h>
#include <linux/kexec_handover.h>
#include <linux/kho_radix_tree.h>
#include <linux/kho/abi/kexec_handover.h>
#include <linux/libfdt.h>
#include <linux/list.h>
#include <linux/memblock.h>
#include <linux/page-isolation.h>
#include <linux/unaligned.h>
#include <linux/vmalloc.h>

#include <asm/early_ioremap.h>

/*
 * KHO is tightly coupled with mm init and needs access to some of mm
 * internal APIs.
 */
#include "../../mm/internal.h"
#include "../kexec_internal.h"
#include "kexec_handover_internal.h"

/* The magic token for preserved pages */
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

struct kho_out {
	void *fdt;
	struct mutex lock; /* protects KHO FDT */

	struct kho_radix_tree radix_tree;
	struct kho_debugfs dbg;
};

static struct kho_out kho_out = {
	.lock = __MUTEX_INITIALIZER(kho_out.lock),
	.radix_tree = {
		.lock = __MUTEX_INITIALIZER(kho_out.radix_tree.lock),
	},
};

/**
 * kho_radix_encode_key - Encodes a physical address and order into a radix key.
 * @phys: The physical address of the page.
 * @order: The order of the page.
 *
 * This function combines a page's physical address and its order into a
 * single unsigned long, which is used as a key for all radix tree
 * operations.
 *
 * Return: The encoded unsigned long radix key.
 */
static unsigned long kho_radix_encode_key(phys_addr_t phys, unsigned int order)
{
	/* Order bits part */
	unsigned long h = 1UL << (KHO_ORDER_0_LOG2 - order);
	/* Shifted physical address part */
	unsigned long l = phys >> (PAGE_SHIFT + order);

	return h | l;
}

/**
 * kho_radix_decode_key - Decodes a radix key back into a physical address and order.
 * @key: The unsigned long key to decode.
 * @order: An output parameter, a pointer to an unsigned int where the decoded
 *         page order will be stored.
 *
 * This function reverses the encoding performed by kho_radix_encode_key(),
 * extracting the original physical address and page order from a given key.
 *
 * Return: The decoded physical address.
 */
static phys_addr_t kho_radix_decode_key(unsigned long key, unsigned int *order)
{
	unsigned int order_bit = fls64(key);
	phys_addr_t phys;

	/* order_bit is numbered starting at 1 from fls64 */
	*order = KHO_ORDER_0_LOG2 - order_bit + 1;
	/* The order is discarded by the shift */
	phys = key << (PAGE_SHIFT + *order);

	return phys;
}

static unsigned long kho_radix_get_bitmap_index(unsigned long key)
{
	return key % (1 << KHO_BITMAP_SIZE_LOG2);
}

static unsigned long kho_radix_get_table_index(unsigned long key,
					       unsigned int level)
{
	int s;

	s = ((level - 1) * KHO_TABLE_SIZE_LOG2) + KHO_BITMAP_SIZE_LOG2;
	return (key >> s) % (1 << KHO_TABLE_SIZE_LOG2);
}

/**
 * kho_radix_add_page - Marks a page as preserved in the radix tree.
 * @tree: The KHO radix tree.
 * @pfn: The page frame number of the page to preserve.
 * @order: The order of the page.
 *
 * This function traverses the radix tree based on the key derived from @pfn
 * and @order. It sets the corresponding bit in the leaf bitmap to mark the
 * page for preservation. If intermediate nodes do not exist along the path,
 * they are allocated and added to the tree.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int kho_radix_add_page(struct kho_radix_tree *tree,
		       unsigned long pfn, unsigned int order)
{
	/* Newly allocated nodes for error cleanup */
	struct kho_radix_node *intermediate_nodes[KHO_TREE_MAX_DEPTH] = { 0 };
	unsigned long key = kho_radix_encode_key(PFN_PHYS(pfn), order);
	struct kho_radix_node *anchor_node = NULL;
	struct kho_radix_node *node = tree->root;
	struct kho_radix_node *new_node;
	unsigned int i, idx, anchor_idx;
	struct kho_radix_leaf *leaf;
	int err = 0;

	if (WARN_ON_ONCE(!tree->root))
		return -EINVAL;

	might_sleep();

	guard(mutex)(&tree->lock);

	/* Go from high levels to low levels */
	for (i = KHO_TREE_MAX_DEPTH - 1; i > 0; i--) {
		idx = kho_radix_get_table_index(key, i);

		if (node->table[idx]) {
			node = phys_to_virt(node->table[idx]);
			continue;
		}

		/* Next node is empty, create a new node for it */
		new_node = (struct kho_radix_node *)get_zeroed_page(GFP_KERNEL);
		if (!new_node) {
			err = -ENOMEM;
			goto err_free_nodes;
		}

		node->table[idx] = virt_to_phys(new_node);

		/*
		 * Capture the node where the new branch starts for cleanup
		 * if allocation fails.
		 */
		if (!anchor_node) {
			anchor_node = node;
			anchor_idx = idx;
		}
		intermediate_nodes[i] = new_node;

		node = new_node;
	}

	/* Handle the leaf level bitmap (level 0) */
	idx = kho_radix_get_bitmap_index(key);
	leaf = (struct kho_radix_leaf *)node;
	__set_bit(idx, leaf->bitmap);

	return 0;

err_free_nodes:
	for (i = KHO_TREE_MAX_DEPTH - 1; i > 0; i--) {
		if (intermediate_nodes[i])
			free_page((unsigned long)intermediate_nodes[i]);
	}
	if (anchor_node)
		anchor_node->table[anchor_idx] = 0;

	return err;
}
EXPORT_SYMBOL_GPL(kho_radix_add_page);

/**
 * kho_radix_del_page - Removes a page's preservation status from the radix tree.
 * @tree: The KHO radix tree.
 * @pfn: The page frame number of the page to unpreserve.
 * @order: The order of the page.
 *
 * This function traverses the radix tree and clears the bit corresponding to
 * the page, effectively removing its "preserved" status. It does not free
 * the tree's intermediate nodes, even if they become empty.
 */
void kho_radix_del_page(struct kho_radix_tree *tree, unsigned long pfn,
			unsigned int order)
{
	unsigned long key = kho_radix_encode_key(PFN_PHYS(pfn), order);
	struct kho_radix_node *node = tree->root;
	struct kho_radix_leaf *leaf;
	unsigned int i, idx;

	if (WARN_ON_ONCE(!tree->root))
		return;

	might_sleep();

	guard(mutex)(&tree->lock);

	/* Go from high levels to low levels */
	for (i = KHO_TREE_MAX_DEPTH - 1; i > 0; i--) {
		idx = kho_radix_get_table_index(key, i);

		/*
		 * Attempting to delete a page that has not been preserved,
		 * return with a warning.
		 */
		if (WARN_ON(!node->table[idx]))
			return;

		node = phys_to_virt(node->table[idx]);
	}

	/* Handle the leaf level bitmap (level 0) */
	leaf = (struct kho_radix_leaf *)node;
	idx = kho_radix_get_bitmap_index(key);
	__clear_bit(idx, leaf->bitmap);
}
EXPORT_SYMBOL_GPL(kho_radix_del_page);

static int kho_radix_walk_leaf(struct kho_radix_leaf *leaf,
			       unsigned long key,
			       kho_radix_tree_walk_callback_t cb)
{
	unsigned long *bitmap = (unsigned long *)leaf;
	unsigned int order;
	phys_addr_t phys;
	unsigned int i;
	int err;

	for_each_set_bit(i, bitmap, PAGE_SIZE * BITS_PER_BYTE) {
		phys = kho_radix_decode_key(key | i, &order);
		err = cb(phys, order);
		if (err)
			return err;
	}

	return 0;
}

static int __kho_radix_walk_tree(struct kho_radix_node *root,
				 unsigned int level, unsigned long start,
				 kho_radix_tree_walk_callback_t cb)
{
	struct kho_radix_node *node;
	struct kho_radix_leaf *leaf;
	unsigned long key, i;
	unsigned int shift;
	int err;

	for (i = 0; i < PAGE_SIZE / sizeof(phys_addr_t); i++) {
		if (!root->table[i])
			continue;

		shift = ((level - 1) * KHO_TABLE_SIZE_LOG2) +
			KHO_BITMAP_SIZE_LOG2;
		key = start | (i << shift);

		node = phys_to_virt(root->table[i]);

		if (level == 1) {
			/*
			 * we are at level 1,
			 * node is pointing to the level 0 bitmap.
			 */
			leaf = (struct kho_radix_leaf *)node;
			err = kho_radix_walk_leaf(leaf, key, cb);
		} else {
			err  = __kho_radix_walk_tree(node, level - 1,
						     key, cb);
		}

		if (err)
			return err;
	}

	return 0;
}

/**
 * kho_radix_walk_tree - Traverses the radix tree and calls a callback for each preserved page.
 * @tree: A pointer to the KHO radix tree to walk.
 * @cb: A callback function of type kho_radix_tree_walk_callback_t that will be
 *      invoked for each preserved page found in the tree. The callback receives
 *      the physical address and order of the preserved page.
 *
 * This function walks the radix tree, searching from the specified top level
 * down to the lowest level (level 0). For each preserved page found, it invokes
 * the provided callback, passing the page's physical address and order.
 *
 * Return: 0 if the walk completed the specified tree, or the non-zero return
 *         value from the callback that stopped the walk.
 */
int kho_radix_walk_tree(struct kho_radix_tree *tree,
			kho_radix_tree_walk_callback_t cb)
{
	if (WARN_ON_ONCE(!tree->root))
		return -EINVAL;

	guard(mutex)(&tree->lock);

	return __kho_radix_walk_tree(tree->root, KHO_TREE_MAX_DEPTH - 1, 0, cb);
}
EXPORT_SYMBOL_GPL(kho_radix_walk_tree);

static void __kho_unpreserve(struct kho_radix_tree *tree,
			     unsigned long pfn, unsigned long end_pfn)
{
	unsigned int order;

	while (pfn < end_pfn) {
		order = min(count_trailing_zeros(pfn), ilog2(end_pfn - pfn));

		kho_radix_del_page(tree, pfn, order);

		pfn += 1 << order;
	}
}

/* For physically contiguous 0-order pages. */
static void kho_init_pages(struct page *page, unsigned long nr_pages)
{
	for (unsigned long i = 0; i < nr_pages; i++) {
		set_page_count(page + i, 1);
		/* Clear each page's codetag to avoid accounting mismatch. */
		clear_page_tag_ref(page + i);
	}
}

static void kho_init_folio(struct page *page, unsigned int order)
{
	unsigned long nr_pages = (1 << order);

	/* Head page gets refcount of 1. */
	set_page_count(page, 1);
	/* Clear head page's codetag to avoid accounting mismatch. */
	clear_page_tag_ref(page);

	/* For higher order folios, tail pages get a page count of zero. */
	for (unsigned long i = 1; i < nr_pages; i++)
		set_page_count(page + i, 0);

	if (order > 0)
		prep_compound_page(page, order);
}

static struct page *kho_restore_page(phys_addr_t phys, bool is_folio)
{
	struct page *page = pfn_to_online_page(PHYS_PFN(phys));
	unsigned long nr_pages;
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

	if (is_folio)
		kho_init_folio(page, info.order);
	else
		kho_init_pages(page, nr_pages);

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
 * Return: the first page on success, NULL on failure.
 */
struct page *kho_restore_pages(phys_addr_t phys, unsigned long nr_pages)
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

static int __init kho_preserved_memory_reserve(phys_addr_t phys,
					       unsigned int order)
{
	union kho_page_info info;
	struct page *page;
	u64 sz;

	sz = 1 << (order + PAGE_SHIFT);
	page = phys_to_page(phys);

	/* Reserve the memory preserved in KHO in memblock */
	memblock_reserve(phys, sz);
	memblock_reserved_mark_noinit(phys, sz);
	info.magic = KHO_PAGE_MAGIC;
	info.order = order;
	page->private = info.page_private;

	return 0;
}

/* Returns physical address of the preserved memory map from FDT */
static phys_addr_t __init kho_get_mem_map_phys(const void *fdt)
{
	const void *mem_ptr;
	int len;

	mem_ptr = fdt_getprop(fdt, 0, KHO_FDT_MEMORY_MAP_PROP_NAME, &len);
	if (!mem_ptr || len != sizeof(u64)) {
		pr_err("failed to get preserved memory map\n");
		return 0;
	}

	return get_unaligned((const u64 *)mem_ptr);
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
	kho_scratch_cnt = nodes_weight(node_states[N_MEMORY]) + 2;
	size = kho_scratch_cnt * sizeof(*kho_scratch);
	kho_scratch = memblock_alloc(size, PAGE_SIZE);
	if (!kho_scratch) {
		pr_err("Failed to reserve scratch array\n");
		goto err_disable_kho;
	}

	/*
	 * reserve scratch area in low memory for lowmem allocations in the
	 * next kernel
	 */
	size = scratch_size_lowmem;
	addr = memblock_phys_alloc_range(size, CMA_MIN_ALIGNMENT_BYTES, 0,
					 ARCH_LOW_ADDRESS_LIMIT);
	if (!addr) {
		pr_err("Failed to reserve lowmem scratch buffer\n");
		goto err_free_scratch_desc;
	}

	kho_scratch[i].addr = addr;
	kho_scratch[i].size = size;
	i++;

	/* reserve large contiguous area for allocations without nid */
	size = scratch_size_global;
	addr = memblock_phys_alloc(size, CMA_MIN_ALIGNMENT_BYTES);
	if (!addr) {
		pr_err("Failed to reserve global scratch buffer\n");
		goto err_free_scratch_areas;
	}

	kho_scratch[i].addr = addr;
	kho_scratch[i].size = size;
	i++;

	/*
	 * Loop over nodes that have both memory and are online. Skip
	 * memoryless nodes, as we can not allocate scratch areas there.
	 */
	for_each_node_state(nid, N_MEMORY) {
		size = scratch_size_node(nid);
		addr = memblock_alloc_range_nid(size, CMA_MIN_ALIGNMENT_BYTES,
						0, MEMBLOCK_ALLOC_ACCESSIBLE,
						nid, true);
		if (!addr) {
			pr_err("Failed to reserve nid %d scratch buffer\n", nid);
			goto err_free_scratch_areas;
		}

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

	err = fdt_setprop(root_fdt, off, KHO_FDT_SUB_TREE_PROP_NAME,
			  &phys, sizeof(phys));
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

		val = fdt_getprop(root_fdt, off, KHO_FDT_SUB_TREE_PROP_NAME, &len);
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
	struct kho_radix_tree *tree = &kho_out.radix_tree;
	const unsigned long pfn = folio_pfn(folio);
	const unsigned int order = folio_order(folio);

	if (WARN_ON(kho_scratch_overlap(pfn << PAGE_SHIFT, PAGE_SIZE << order)))
		return -EINVAL;

	return kho_radix_add_page(tree, pfn, order);
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
	struct kho_radix_tree *tree = &kho_out.radix_tree;
	const unsigned long pfn = folio_pfn(folio);
	const unsigned int order = folio_order(folio);

	kho_radix_del_page(tree, pfn, order);
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
int kho_preserve_pages(struct page *page, unsigned long nr_pages)
{
	struct kho_radix_tree *tree = &kho_out.radix_tree;
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

		err = kho_radix_add_page(tree, pfn, order);
		if (err) {
			failed_pfn = pfn;
			break;
		}

		pfn += 1 << order;
	}

	if (err)
		__kho_unpreserve(tree, start_pfn, failed_pfn);

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
void kho_unpreserve_pages(struct page *page, unsigned long nr_pages)
{
	struct kho_radix_tree *tree = &kho_out.radix_tree;
	const unsigned long start_pfn = page_to_pfn(page);
	const unsigned long end_pfn = start_pfn + nr_pages;

	__kho_unpreserve(tree, start_pfn, end_pfn);
}
EXPORT_SYMBOL_GPL(kho_unpreserve_pages);

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
	struct kho_radix_tree *tree = &kho_out.radix_tree;
	unsigned long pfn = PHYS_PFN(virt_to_phys(chunk));

	__kho_unpreserve(tree, pfn, pfn + 1);

	for (int i = 0; i < ARRAY_SIZE(chunk->phys) && chunk->phys[i]; i++) {
		pfn = PHYS_PFN(chunk->phys[i]);
		__kho_unpreserve(tree, pfn, pfn + (1 << order));
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
	pages = kvmalloc_objs(*pages, total_pages);
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

struct kho_in {
	phys_addr_t fdt_phys;
	phys_addr_t scratch_phys;
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

	val = fdt_getprop(fdt, offset, KHO_FDT_SUB_TREE_PROP_NAME, &len);
	if (!val || len != sizeof(*val))
		return -EINVAL;

	*phys = (phys_addr_t)*val;

	return 0;
}
EXPORT_SYMBOL_GPL(kho_retrieve_subtree);

static int __init kho_mem_retrieve(const void *fdt)
{
	struct kho_radix_tree tree;
	const phys_addr_t *mem;
	int len;

	/* Retrieve the KHO radix tree from passed-in FDT. */
	mem = fdt_getprop(fdt, 0, KHO_FDT_MEMORY_MAP_PROP_NAME, &len);

	if (!mem || len != sizeof(*mem)) {
		pr_err("failed to get preserved KHO memory tree\n");
		return -ENOENT;
	}

	if (!*mem)
		return -EINVAL;

	tree.root = phys_to_virt(*mem);
	mutex_init(&tree.lock);
	return kho_radix_walk_tree(&tree, kho_preserved_memory_reserve);
}

static __init int kho_out_fdt_setup(void)
{
	struct kho_radix_tree *tree = &kho_out.radix_tree;
	void *root = kho_out.fdt;
	u64 preserved_mem_tree_pa;
	int err;

	err = fdt_create(root, PAGE_SIZE);
	err |= fdt_finish_reservemap(root);
	err |= fdt_begin_node(root, "");
	err |= fdt_property_string(root, "compatible", KHO_FDT_COMPATIBLE);

	preserved_mem_tree_pa = virt_to_phys(tree->root);

	err |= fdt_property(root, KHO_FDT_MEMORY_MAP_PROP_NAME,
			    &preserved_mem_tree_pa,
			    sizeof(preserved_mem_tree_pa));

	err |= fdt_end_node(root);
	err |= fdt_finish(root);

	return err;
}

static __init int kho_init(void)
{
	struct kho_radix_tree *tree = &kho_out.radix_tree;
	const void *fdt = kho_get_fdt();
	int err = 0;

	if (!kho_enable)
		return 0;

	tree->root = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tree->root) {
		err = -ENOMEM;
		goto err_free_scratch;
	}

	kho_out.fdt = kho_alloc_preserve(PAGE_SIZE);
	if (IS_ERR(kho_out.fdt)) {
		err = PTR_ERR(kho_out.fdt);
		goto err_free_kho_radix_tree_root;
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
err_free_kho_radix_tree_root:
	kfree(tree->root);
	tree->root = NULL;
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
	if (kho_in.scratch_phys) {
		kho_scratch = phys_to_virt(kho_in.scratch_phys);
		kho_release_scratch();

		if (kho_mem_retrieve(kho_get_fdt()))
			kho_in.fdt_phys = 0;
	} else {
		kho_reserve_scratch();
	}
}

void __init kho_populate(phys_addr_t fdt_phys, u64 fdt_len,
			 phys_addr_t scratch_phys, u64 scratch_len)
{
	unsigned int scratch_cnt = scratch_len / sizeof(*kho_scratch);
	struct kho_scratch *scratch = NULL;
	phys_addr_t mem_map_phys;
	void *fdt = NULL;
	bool populated = false;
	int err;

	/* Validate the input FDT */
	fdt = early_memremap(fdt_phys, fdt_len);
	if (!fdt) {
		pr_warn("setup: failed to memremap FDT (0x%llx)\n", fdt_phys);
		goto report;
	}
	err = fdt_check_header(fdt);
	if (err) {
		pr_warn("setup: handover FDT (0x%llx) is invalid: %d\n",
			fdt_phys, err);
		goto unmap_fdt;
	}
	err = fdt_node_check_compatible(fdt, 0, KHO_FDT_COMPATIBLE);
	if (err) {
		pr_warn("setup: handover FDT (0x%llx) is incompatible with '%s': %d\n",
			fdt_phys, KHO_FDT_COMPATIBLE, err);
		goto unmap_fdt;
	}

	mem_map_phys = kho_get_mem_map_phys(fdt);
	if (!mem_map_phys)
		goto unmap_fdt;

	scratch = early_memremap(scratch_phys, scratch_len);
	if (!scratch) {
		pr_warn("setup: failed to memremap scratch (phys=0x%llx, len=%lld)\n",
			scratch_phys, scratch_len);
		goto unmap_fdt;
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
		if (err) {
			pr_warn("failed to mark the scratch region 0x%pa+0x%pa: %pe",
				&area->addr, &size, ERR_PTR(err));
			goto unmap_scratch;
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

	populated = true;
	pr_info("found kexec handover data.\n");

unmap_scratch:
	early_memunmap(scratch, scratch_len);
unmap_fdt:
	early_memunmap(fdt, fdt_len);
report:
	if (!populated)
		pr_warn("disabling KHO revival\n");
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
