/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MM_SWAP_H
#define _MM_SWAP_H

#include <linux/atomic.h> /* for atomic_long_t */
struct mempolicy;
struct swap_iocb;

extern int page_cluster;

#ifdef CONFIG_THP_SWAP
#define SWAPFILE_CLUSTER	HPAGE_PMD_NR
#define swap_entry_order(order)	(order)
#else
#define SWAPFILE_CLUSTER	256
#define swap_entry_order(order)	0
#endif

extern struct swap_info_struct *swap_info[];

/*
 * We use this to track usage of a cluster. A cluster is a block of swap disk
 * space with SWAPFILE_CLUSTER pages long and naturally aligns in disk. All
 * free clusters are organized into a list. We fetch an entry from the list to
 * get a free cluster.
 *
 * The flags field determines if a cluster is free. This is
 * protected by cluster lock.
 */
struct swap_cluster_info {
	spinlock_t lock;	/*
				 * Protect swap_cluster_info fields
				 * other than list, and swap_info_struct->swap_map
				 * elements corresponding to the swap cluster.
				 */
	u16 count;
	u8 flags;
	u8 order;
	atomic_long_t __rcu *table;	/* Swap table entries, see mm/swap_table.h */
	struct list_head list;
};

/* All on-list cluster must have a non-zero flag. */
enum swap_cluster_flags {
	CLUSTER_FLAG_NONE = 0, /* For temporary off-list cluster */
	CLUSTER_FLAG_FREE,
	CLUSTER_FLAG_NONFULL,
	CLUSTER_FLAG_FRAG,
	/* Clusters with flags above are allocatable */
	CLUSTER_FLAG_USABLE = CLUSTER_FLAG_FRAG,
	CLUSTER_FLAG_FULL,
	CLUSTER_FLAG_DISCARD,
	CLUSTER_FLAG_MAX,
};

#ifdef CONFIG_SWAP
#include <linux/swapops.h> /* for swp_offset */
#include <linux/blk_types.h> /* for bio_end_io_t */

static inline unsigned int swp_cluster_offset(swp_entry_t entry)
{
	return swp_offset(entry) % SWAPFILE_CLUSTER;
}

/*
 * Callers of all helpers below must ensure the entry, type, or offset is
 * valid, and protect the swap device with reference count or locks.
 */
static inline struct swap_info_struct *__swap_type_to_info(int type)
{
	struct swap_info_struct *si;

	si = READ_ONCE(swap_info[type]); /* rcu_dereference() */
	VM_WARN_ON_ONCE(percpu_ref_is_zero(&si->users)); /* race with swapoff */
	return si;
}

static inline struct swap_info_struct *__swap_entry_to_info(swp_entry_t entry)
{
	return __swap_type_to_info(swp_type(entry));
}

static inline struct swap_cluster_info *__swap_offset_to_cluster(
		struct swap_info_struct *si, pgoff_t offset)
{
	VM_WARN_ON_ONCE(percpu_ref_is_zero(&si->users)); /* race with swapoff */
	VM_WARN_ON_ONCE(offset >= si->max);
	return &si->cluster_info[offset / SWAPFILE_CLUSTER];
}

static inline struct swap_cluster_info *__swap_entry_to_cluster(swp_entry_t entry)
{
	return __swap_offset_to_cluster(__swap_entry_to_info(entry),
					swp_offset(entry));
}

static __always_inline struct swap_cluster_info *__swap_cluster_lock(
		struct swap_info_struct *si, unsigned long offset, bool irq)
{
	struct swap_cluster_info *ci = __swap_offset_to_cluster(si, offset);

	/*
	 * Nothing modifies swap cache in an IRQ context. All access to
	 * swap cache is wrapped by swap_cache_* helpers, and swap cache
	 * writeback is handled outside of IRQs. Swapin or swapout never
	 * occurs in IRQ, and neither does in-place split or replace.
	 *
	 * Besides, modifying swap cache requires synchronization with
	 * swap_map, which was never IRQ safe.
	 */
	VM_WARN_ON_ONCE(!in_task());
	VM_WARN_ON_ONCE(percpu_ref_is_zero(&si->users)); /* race with swapoff */
	if (irq)
		spin_lock_irq(&ci->lock);
	else
		spin_lock(&ci->lock);
	return ci;
}

/**
 * swap_cluster_lock - Lock and return the swap cluster of given offset.
 * @si: swap device the cluster belongs to.
 * @offset: the swap entry offset, pointing to a valid slot.
 *
 * Context: The caller must ensure the offset is in the valid range and
 * protect the swap device with reference count or locks.
 */
static inline struct swap_cluster_info *swap_cluster_lock(
		struct swap_info_struct *si, unsigned long offset)
{
	return __swap_cluster_lock(si, offset, false);
}

static inline struct swap_cluster_info *__swap_cluster_get_and_lock(
		const struct folio *folio, bool irq)
{
	VM_WARN_ON_ONCE_FOLIO(!folio_test_locked(folio), folio);
	VM_WARN_ON_ONCE_FOLIO(!folio_test_swapcache(folio), folio);
	return __swap_cluster_lock(__swap_entry_to_info(folio->swap),
				   swp_offset(folio->swap), irq);
}

/*
 * swap_cluster_get_and_lock - Locks the cluster that holds a folio's entries.
 * @folio: The folio.
 *
 * This locks and returns the swap cluster that contains a folio's swap
 * entries. The swap entries of a folio are always in one single cluster.
 * The folio has to be locked so its swap entries won't change and the
 * cluster won't be freed.
 *
 * Context: Caller must ensure the folio is locked and in the swap cache.
 * Return: Pointer to the swap cluster.
 */
static inline struct swap_cluster_info *swap_cluster_get_and_lock(
		const struct folio *folio)
{
	return __swap_cluster_get_and_lock(folio, false);
}

/*
 * swap_cluster_get_and_lock_irq - Locks the cluster that holds a folio's entries.
 * @folio: The folio.
 *
 * Same as swap_cluster_get_and_lock but also disable IRQ.
 *
 * Context: Caller must ensure the folio is locked and in the swap cache.
 * Return: Pointer to the swap cluster.
 */
static inline struct swap_cluster_info *swap_cluster_get_and_lock_irq(
		const struct folio *folio)
{
	return __swap_cluster_get_and_lock(folio, true);
}

static inline void swap_cluster_unlock(struct swap_cluster_info *ci)
{
	spin_unlock(&ci->lock);
}

static inline void swap_cluster_unlock_irq(struct swap_cluster_info *ci)
{
	spin_unlock_irq(&ci->lock);
}

/* linux/mm/page_io.c */
int sio_pool_init(void);
struct swap_iocb;
void swap_read_folio(struct folio *folio, struct swap_iocb **plug);
void __swap_read_unplug(struct swap_iocb *plug);
static inline void swap_read_unplug(struct swap_iocb *plug)
{
	if (unlikely(plug))
		__swap_read_unplug(plug);
}
void swap_write_unplug(struct swap_iocb *sio);
int swap_writeout(struct folio *folio, struct swap_iocb **swap_plug);
void __swap_writepage(struct folio *folio, struct swap_iocb **swap_plug);

/* linux/mm/swap_state.c */
extern struct address_space swap_space __ro_after_init;
static inline struct address_space *swap_address_space(swp_entry_t entry)
{
	return &swap_space;
}

/*
 * Return the swap device position of the swap entry.
 */
static inline loff_t swap_dev_pos(swp_entry_t entry)
{
	return ((loff_t)swp_offset(entry)) << PAGE_SHIFT;
}

/**
 * folio_matches_swap_entry - Check if a folio matches a given swap entry.
 * @folio: The folio.
 * @entry: The swap entry to check against.
 *
 * Context: The caller should have the folio locked to ensure it's stable
 * and nothing will move it in or out of the swap cache.
 * Return: true or false.
 */
static inline bool folio_matches_swap_entry(const struct folio *folio,
					    swp_entry_t entry)
{
	swp_entry_t folio_entry = folio->swap;
	long nr_pages = folio_nr_pages(folio);

	VM_WARN_ON_ONCE_FOLIO(!folio_test_locked(folio), folio);
	if (!folio_test_swapcache(folio))
		return false;
	VM_WARN_ON_ONCE_FOLIO(!IS_ALIGNED(folio_entry.val, nr_pages), folio);
	return folio_entry.val == round_down(entry.val, nr_pages);
}

/*
 * All swap cache helpers below require the caller to ensure the swap entries
 * used are valid and stablize the device by any of the following ways:
 * - Hold a reference by get_swap_device(): this ensures a single entry is
 *   valid and increases the swap device's refcount.
 * - Locking a folio in the swap cache: this ensures the folio's swap entries
 *   are valid and pinned, also implies reference to the device.
 * - Locking anything referencing the swap entry: e.g. PTL that protects
 *   swap entries in the page table, similar to locking swap cache folio.
 * - See the comment of get_swap_device() for more complex usage.
 */
struct folio *swap_cache_get_folio(swp_entry_t entry);
void *swap_cache_get_shadow(swp_entry_t entry);
void swap_cache_add_folio(struct folio *folio, swp_entry_t entry, void **shadow);
void swap_cache_del_folio(struct folio *folio);
/* Below helpers require the caller to lock and pass in the swap cluster. */
void __swap_cache_del_folio(struct swap_cluster_info *ci,
			    struct folio *folio, swp_entry_t entry, void *shadow);
void __swap_cache_replace_folio(struct swap_cluster_info *ci,
				struct folio *old, struct folio *new);
void __swap_cache_clear_shadow(swp_entry_t entry, int nr_ents);

void show_swap_cache_info(void);
void swapcache_clear(struct swap_info_struct *si, swp_entry_t entry, int nr);
struct folio *read_swap_cache_async(swp_entry_t entry, gfp_t gfp_mask,
		struct vm_area_struct *vma, unsigned long addr,
		struct swap_iocb **plug);
struct folio *__read_swap_cache_async(swp_entry_t entry, gfp_t gfp_flags,
		struct mempolicy *mpol, pgoff_t ilx, bool *new_page_allocated,
		bool skip_if_exists);
struct folio *swap_cluster_readahead(swp_entry_t entry, gfp_t flag,
		struct mempolicy *mpol, pgoff_t ilx);
struct folio *swapin_readahead(swp_entry_t entry, gfp_t flag,
		struct vm_fault *vmf);
void swap_update_readahead(struct folio *folio, struct vm_area_struct *vma,
			   unsigned long addr);

static inline unsigned int folio_swap_flags(struct folio *folio)
{
	return __swap_entry_to_info(folio->swap)->flags;
}

/*
 * Return the count of contiguous swap entries that share the same
 * zeromap status as the starting entry. If is_zeromap is not NULL,
 * it will return the zeromap status of the starting entry.
 */
static inline int swap_zeromap_batch(swp_entry_t entry, int max_nr,
		bool *is_zeromap)
{
	struct swap_info_struct *sis = __swap_entry_to_info(entry);
	unsigned long start = swp_offset(entry);
	unsigned long end = start + max_nr;
	bool first_bit;

	first_bit = test_bit(start, sis->zeromap);
	if (is_zeromap)
		*is_zeromap = first_bit;

	if (max_nr <= 1)
		return max_nr;
	if (first_bit)
		return find_next_zero_bit(sis->zeromap, end, start) - start;
	else
		return find_next_bit(sis->zeromap, end, start) - start;
}

static inline int non_swapcache_batch(swp_entry_t entry, int max_nr)
{
	struct swap_info_struct *si = __swap_entry_to_info(entry);
	pgoff_t offset = swp_offset(entry);
	int i;

	/*
	 * While allocating a large folio and doing mTHP swapin, we need to
	 * ensure all entries are not cached, otherwise, the mTHP folio will
	 * be in conflict with the folio in swap cache.
	 */
	for (i = 0; i < max_nr; i++) {
		if ((si->swap_map[offset + i] & SWAP_HAS_CACHE))
			return i;
	}

	return i;
}

#else /* CONFIG_SWAP */
struct swap_iocb;
static inline struct swap_cluster_info *swap_cluster_lock(
	struct swap_info_struct *si, pgoff_t offset, bool irq)
{
	return NULL;
}

static inline struct swap_cluster_info *swap_cluster_get_and_lock(
		struct folio *folio)
{
	return NULL;
}

static inline struct swap_cluster_info *swap_cluster_get_and_lock_irq(
		struct folio *folio)
{
	return NULL;
}

static inline void swap_cluster_unlock(struct swap_cluster_info *ci)
{
}

static inline void swap_cluster_unlock_irq(struct swap_cluster_info *ci)
{
}

static inline struct swap_info_struct *__swap_entry_to_info(swp_entry_t entry)
{
	return NULL;
}

static inline void swap_read_folio(struct folio *folio, struct swap_iocb **plug)
{
}
static inline void swap_write_unplug(struct swap_iocb *sio)
{
}

static inline struct address_space *swap_address_space(swp_entry_t entry)
{
	return NULL;
}

static inline bool folio_matches_swap_entry(const struct folio *folio, swp_entry_t entry)
{
	return false;
}

static inline void show_swap_cache_info(void)
{
}

static inline struct folio *swap_cluster_readahead(swp_entry_t entry,
			gfp_t gfp_mask, struct mempolicy *mpol, pgoff_t ilx)
{
	return NULL;
}

static inline struct folio *swapin_readahead(swp_entry_t swp, gfp_t gfp_mask,
			struct vm_fault *vmf)
{
	return NULL;
}

static inline void swap_update_readahead(struct folio *folio,
		struct vm_area_struct *vma, unsigned long addr)
{
}

static inline int swap_writeout(struct folio *folio,
		struct swap_iocb **swap_plug)
{
	return 0;
}

static inline void swapcache_clear(struct swap_info_struct *si, swp_entry_t entry, int nr)
{
}

static inline struct folio *swap_cache_get_folio(swp_entry_t entry)
{
	return NULL;
}

static inline void *swap_cache_get_shadow(swp_entry_t entry)
{
	return NULL;
}

static inline void swap_cache_add_folio(struct folio *folio, swp_entry_t entry, void **shadow)
{
}

static inline void swap_cache_del_folio(struct folio *folio)
{
}

static inline void __swap_cache_del_folio(struct swap_cluster_info *ci,
		struct folio *folio, swp_entry_t entry, void *shadow)
{
}

static inline void __swap_cache_replace_folio(struct swap_cluster_info *ci,
		struct folio *old, struct folio *new)
{
}

static inline unsigned int folio_swap_flags(struct folio *folio)
{
	return 0;
}

static inline int swap_zeromap_batch(swp_entry_t entry, int max_nr,
		bool *has_zeromap)
{
	return 0;
}

static inline int non_swapcache_batch(swp_entry_t entry, int max_nr)
{
	return 0;
}
#endif /* CONFIG_SWAP */

/**
 * folio_index - File index of a folio.
 * @folio: The folio.
 *
 * For a folio which is either in the page cache or the swap cache,
 * return its index within the address_space it belongs to.  If you know
 * the folio is definitely in the page cache, you can look at the folio's
 * index directly.
 *
 * Return: The index (offset in units of pages) of a folio in its file.
 */
static inline pgoff_t folio_index(struct folio *folio)
{
#ifdef CONFIG_SWAP
	if (unlikely(folio_test_swapcache(folio)))
		return swp_offset(folio->swap);
#endif
	return folio->index;
}

#endif /* _MM_SWAP_H */
