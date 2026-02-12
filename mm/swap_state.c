// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/mm/swap_state.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *
 *  Rewritten to use page cache, (C) 1998 Stephen Tweedie
 */
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/kernel_stat.h>
#include <linux/mempolicy.h>
#include <linux/swap.h>
#include <linux/leafops.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/migrate.h>
#include <linux/vmalloc.h>
#include <linux/huge_mm.h>
#include <linux/shmem_fs.h>
#include "internal.h"
#include "swap_table.h"
#include "swap.h"

/*
 * swapper_space is a fiction, retained to simplify the path through
 * vmscan's shrink_folio_list.
 */
static const struct address_space_operations swap_aops = {
	.dirty_folio	= noop_dirty_folio,
#ifdef CONFIG_MIGRATION
	.migrate_folio	= migrate_folio,
#endif
};

struct address_space swap_space __read_mostly = {
	.a_ops = &swap_aops,
};

static bool enable_vma_readahead __read_mostly = true;

#define SWAP_RA_ORDER_CEILING	5

#define SWAP_RA_WIN_SHIFT	(PAGE_SHIFT / 2)
#define SWAP_RA_HITS_MASK	((1UL << SWAP_RA_WIN_SHIFT) - 1)
#define SWAP_RA_HITS_MAX	SWAP_RA_HITS_MASK
#define SWAP_RA_WIN_MASK	(~PAGE_MASK & ~SWAP_RA_HITS_MASK)

#define SWAP_RA_HITS(v)		((v) & SWAP_RA_HITS_MASK)
#define SWAP_RA_WIN(v)		(((v) & SWAP_RA_WIN_MASK) >> SWAP_RA_WIN_SHIFT)
#define SWAP_RA_ADDR(v)		((v) & PAGE_MASK)

#define SWAP_RA_VAL(addr, win, hits)				\
	(((addr) & PAGE_MASK) |					\
	 (((win) << SWAP_RA_WIN_SHIFT) & SWAP_RA_WIN_MASK) |	\
	 ((hits) & SWAP_RA_HITS_MASK))

/* Initial readahead hits is 4 to start up with a small window */
#define GET_SWAP_RA_VAL(vma)					\
	(atomic_long_read(&(vma)->swap_readahead_info) ? : 4)

static atomic_t swapin_readahead_hits = ATOMIC_INIT(4);

void show_swap_cache_info(void)
{
	printk("%lu pages in swap cache\n", total_swapcache_pages());
	printk("Free swap  = %ldkB\n", K(get_nr_swap_pages()));
	printk("Total swap = %lukB\n", K(total_swap_pages));
}

/**
 * swap_cache_get_folio - Looks up a folio in the swap cache.
 * @entry: swap entry used for the lookup.
 *
 * A found folio will be returned unlocked and with its refcount increased.
 *
 * Context: Caller must ensure @entry is valid and protect the swap device
 * with reference count or locks.
 * Return: Returns the found folio on success, NULL otherwise. The caller
 * must lock and check if the folio still matches the swap entry before
 * use (e.g., folio_matches_swap_entry).
 */
struct folio *swap_cache_get_folio(swp_entry_t entry)
{
	unsigned long swp_tb;
	struct folio *folio;

	for (;;) {
		swp_tb = swap_table_get(__swap_entry_to_cluster(entry),
					swp_cluster_offset(entry));
		if (!swp_tb_is_folio(swp_tb))
			return NULL;
		folio = swp_tb_to_folio(swp_tb);
		if (likely(folio_try_get(folio)))
			return folio;
	}

	return NULL;
}

/**
 * swap_cache_has_folio - Check if a swap slot has cache.
 * @entry: swap entry indicating the slot.
 *
 * Context: Caller must ensure @entry is valid and protect the swap
 * device with reference count or locks.
 */
bool swap_cache_has_folio(swp_entry_t entry)
{
	unsigned long swp_tb;

	swp_tb = swap_table_get(__swap_entry_to_cluster(entry),
				swp_cluster_offset(entry));
	return swp_tb_is_folio(swp_tb);
}

/**
 * swap_cache_get_shadow - Looks up a shadow in the swap cache.
 * @entry: swap entry used for the lookup.
 *
 * Context: Caller must ensure @entry is valid and protect the swap device
 * with reference count or locks.
 * Return: Returns either NULL or an XA_VALUE (shadow).
 */
void *swap_cache_get_shadow(swp_entry_t entry)
{
	unsigned long swp_tb;

	swp_tb = swap_table_get(__swap_entry_to_cluster(entry),
				swp_cluster_offset(entry));
	if (swp_tb_is_shadow(swp_tb))
		return swp_tb_to_shadow(swp_tb);
	return NULL;
}

void __swap_cache_add_folio(struct swap_cluster_info *ci,
			    struct folio *folio, swp_entry_t entry)
{
	unsigned long new_tb;
	unsigned int ci_start, ci_off, ci_end;
	unsigned long nr_pages = folio_nr_pages(folio);

	VM_WARN_ON_ONCE_FOLIO(!folio_test_locked(folio), folio);
	VM_WARN_ON_ONCE_FOLIO(folio_test_swapcache(folio), folio);
	VM_WARN_ON_ONCE_FOLIO(!folio_test_swapbacked(folio), folio);

	new_tb = folio_to_swp_tb(folio);
	ci_start = swp_cluster_offset(entry);
	ci_off = ci_start;
	ci_end = ci_start + nr_pages;
	do {
		VM_WARN_ON_ONCE(swp_tb_is_folio(__swap_table_get(ci, ci_off)));
		__swap_table_set(ci, ci_off, new_tb);
	} while (++ci_off < ci_end);

	folio_ref_add(folio, nr_pages);
	folio_set_swapcache(folio);
	folio->swap = entry;

	node_stat_mod_folio(folio, NR_FILE_PAGES, nr_pages);
	lruvec_stat_mod_folio(folio, NR_SWAPCACHE, nr_pages);
}

/**
 * swap_cache_add_folio - Add a folio into the swap cache.
 * @folio: The folio to be added.
 * @entry: The swap entry corresponding to the folio.
 * @gfp: gfp_mask for XArray node allocation.
 * @shadowp: If a shadow is found, return the shadow.
 *
 * Context: Caller must ensure @entry is valid and protect the swap device
 * with reference count or locks.
 */
static int swap_cache_add_folio(struct folio *folio, swp_entry_t entry,
				void **shadowp)
{
	int err;
	void *shadow = NULL;
	unsigned long old_tb;
	struct swap_info_struct *si;
	struct swap_cluster_info *ci;
	unsigned int ci_start, ci_off, ci_end, offset;
	unsigned long nr_pages = folio_nr_pages(folio);

	si = __swap_entry_to_info(entry);
	ci_start = swp_cluster_offset(entry);
	ci_end = ci_start + nr_pages;
	ci_off = ci_start;
	offset = swp_offset(entry);
	ci = swap_cluster_lock(si, swp_offset(entry));
	if (unlikely(!ci->table)) {
		err = -ENOENT;
		goto failed;
	}
	do {
		old_tb = __swap_table_get(ci, ci_off);
		if (unlikely(swp_tb_is_folio(old_tb))) {
			err = -EEXIST;
			goto failed;
		}
		if (unlikely(!__swap_count(swp_entry(swp_type(entry), offset)))) {
			err = -ENOENT;
			goto failed;
		}
		if (swp_tb_is_shadow(old_tb))
			shadow = swp_tb_to_shadow(old_tb);
		offset++;
	} while (++ci_off < ci_end);
	__swap_cache_add_folio(ci, folio, entry);
	swap_cluster_unlock(ci);
	if (shadowp)
		*shadowp = shadow;
	return 0;

failed:
	swap_cluster_unlock(ci);
	return err;
}

/**
 * __swap_cache_del_folio - Removes a folio from the swap cache.
 * @ci: The locked swap cluster.
 * @folio: The folio.
 * @entry: The first swap entry that the folio corresponds to.
 * @shadow: shadow value to be filled in the swap cache.
 *
 * Removes a folio from the swap cache and fills a shadow in place.
 * This won't put the folio's refcount. The caller has to do that.
 *
 * Context: Caller must ensure the folio is locked and in the swap cache
 * using the index of @entry, and lock the cluster that holds the entries.
 */
void __swap_cache_del_folio(struct swap_cluster_info *ci, struct folio *folio,
			    swp_entry_t entry, void *shadow)
{
	struct swap_info_struct *si;
	unsigned long old_tb, new_tb;
	unsigned int ci_start, ci_off, ci_end;
	bool folio_swapped = false, need_free = false;
	unsigned long nr_pages = folio_nr_pages(folio);

	VM_WARN_ON_ONCE(__swap_entry_to_cluster(entry) != ci);
	VM_WARN_ON_ONCE_FOLIO(!folio_test_locked(folio), folio);
	VM_WARN_ON_ONCE_FOLIO(!folio_test_swapcache(folio), folio);
	VM_WARN_ON_ONCE_FOLIO(folio_test_writeback(folio), folio);

	si = __swap_entry_to_info(entry);
	new_tb = shadow_swp_to_tb(shadow);
	ci_start = swp_cluster_offset(entry);
	ci_end = ci_start + nr_pages;
	ci_off = ci_start;
	do {
		/* If shadow is NULL, we sets an empty shadow */
		old_tb = __swap_table_xchg(ci, ci_off, new_tb);
		WARN_ON_ONCE(!swp_tb_is_folio(old_tb) ||
			     swp_tb_to_folio(old_tb) != folio);
		if (__swap_count(swp_entry(si->type,
				 swp_offset(entry) + ci_off - ci_start)))
			folio_swapped = true;
		else
			need_free = true;
	} while (++ci_off < ci_end);

	folio->swap.val = 0;
	folio_clear_swapcache(folio);
	node_stat_mod_folio(folio, NR_FILE_PAGES, -nr_pages);
	lruvec_stat_mod_folio(folio, NR_SWAPCACHE, -nr_pages);

	if (!folio_swapped) {
		swap_entries_free(si, ci, swp_offset(entry), nr_pages);
	} else if (need_free) {
		do {
			if (!__swap_count(entry))
				swap_entries_free(si, ci, swp_offset(entry), 1);
			entry.val++;
		} while (--nr_pages);
	}
}

/**
 * swap_cache_del_folio - Removes a folio from the swap cache.
 * @folio: The folio.
 *
 * Same as __swap_cache_del_folio, but handles lock and refcount. The
 * caller must ensure the folio is either clean or has a swap count
 * equal to zero, or it may cause data loss.
 *
 * Context: Caller must ensure the folio is locked and in the swap cache.
 */
void swap_cache_del_folio(struct folio *folio)
{
	struct swap_cluster_info *ci;
	swp_entry_t entry = folio->swap;

	ci = swap_cluster_lock(__swap_entry_to_info(entry), swp_offset(entry));
	__swap_cache_del_folio(ci, folio, entry, NULL);
	swap_cluster_unlock(ci);

	folio_ref_sub(folio, folio_nr_pages(folio));
}

/**
 * __swap_cache_replace_folio - Replace a folio in the swap cache.
 * @ci: The locked swap cluster.
 * @old: The old folio to be replaced.
 * @new: The new folio.
 *
 * Replace an existing folio in the swap cache with a new folio. The
 * caller is responsible for setting up the new folio's flag and swap
 * entries. Replacement will take the new folio's swap entry value as
 * the starting offset to override all slots covered by the new folio.
 *
 * Context: Caller must ensure both folios are locked, and lock the
 * cluster that holds the old folio to be replaced.
 */
void __swap_cache_replace_folio(struct swap_cluster_info *ci,
				struct folio *old, struct folio *new)
{
	swp_entry_t entry = new->swap;
	unsigned long nr_pages = folio_nr_pages(new);
	unsigned int ci_off = swp_cluster_offset(entry);
	unsigned int ci_end = ci_off + nr_pages;
	unsigned long old_tb, new_tb;

	VM_WARN_ON_ONCE(!folio_test_swapcache(old) || !folio_test_swapcache(new));
	VM_WARN_ON_ONCE(!folio_test_locked(old) || !folio_test_locked(new));
	VM_WARN_ON_ONCE(!entry.val);

	/* Swap cache still stores N entries instead of a high-order entry */
	new_tb = folio_to_swp_tb(new);
	do {
		old_tb = __swap_table_xchg(ci, ci_off, new_tb);
		WARN_ON_ONCE(!swp_tb_is_folio(old_tb) || swp_tb_to_folio(old_tb) != old);
	} while (++ci_off < ci_end);

	/*
	 * If the old folio is partially replaced (e.g., splitting a large
	 * folio, the old folio is shrunk, and new split sub folios replace
	 * the shrunk part), ensure the new folio doesn't overlap it.
	 */
	if (IS_ENABLED(CONFIG_DEBUG_VM) &&
	    folio_order(old) != folio_order(new)) {
		ci_off = swp_cluster_offset(old->swap);
		ci_end = ci_off + folio_nr_pages(old);
		while (ci_off++ < ci_end)
			WARN_ON_ONCE(swp_tb_to_folio(__swap_table_get(ci, ci_off)) != old);
	}
}

/**
 * __swap_cache_clear_shadow - Clears a set of shadows in the swap cache.
 * @entry: The starting index entry.
 * @nr_ents: How many slots need to be cleared.
 *
 * Context: Caller must ensure the range is valid, all in one single cluster,
 * not occupied by any folio, and lock the cluster.
 */
void __swap_cache_clear_shadow(swp_entry_t entry, int nr_ents)
{
	struct swap_cluster_info *ci = __swap_entry_to_cluster(entry);
	unsigned int ci_off = swp_cluster_offset(entry), ci_end;
	unsigned long old;

	ci_end = ci_off + nr_ents;
	do {
		old = __swap_table_xchg(ci, ci_off, null_to_swp_tb());
		WARN_ON_ONCE(swp_tb_is_folio(old));
	} while (++ci_off < ci_end);
}

/*
 * If we are the only user, then try to free up the swap cache.
 *
 * Its ok to check the swapcache flag without the folio lock
 * here because we are going to recheck again inside
 * folio_free_swap() _with_ the lock.
 * 					- Marcelo
 */
void free_swap_cache(struct folio *folio)
{
	if (folio_test_swapcache(folio) && !folio_mapped(folio) &&
	    folio_trylock(folio)) {
		folio_free_swap(folio);
		folio_unlock(folio);
	}
}

/*
 * Freeing a folio and also freeing any swap cache associated with
 * this folio if it is the last user.
 */
void free_folio_and_swap_cache(struct folio *folio)
{
	free_swap_cache(folio);
	if (!is_huge_zero_folio(folio))
		folio_put(folio);
}

/*
 * Passed an array of pages, drop them all from swapcache and then release
 * them.  They are removed from the LRU and freed if this is their last use.
 */
void free_pages_and_swap_cache(struct encoded_page **pages, int nr)
{
	struct folio_batch folios;
	unsigned int refs[PAGEVEC_SIZE];

	folio_batch_init(&folios);
	for (int i = 0; i < nr; i++) {
		struct folio *folio = page_folio(encoded_page_ptr(pages[i]));

		free_swap_cache(folio);
		refs[folios.nr] = 1;
		if (unlikely(encoded_page_flags(pages[i]) &
			     ENCODED_PAGE_BIT_NR_PAGES_NEXT))
			refs[folios.nr] = encoded_nr_pages(pages[++i]);

		if (folio_batch_add(&folios, folio) == 0)
			folios_put_refs(&folios, refs);
	}
	if (folios.nr)
		folios_put_refs(&folios, refs);
}

static inline bool swap_use_vma_readahead(void)
{
	return READ_ONCE(enable_vma_readahead) && !atomic_read(&nr_rotate_swap);
}

/**
 * swap_update_readahead - Update the readahead statistics of VMA or globally.
 * @folio: the swap cache folio that just got hit.
 * @vma: the VMA that should be updated, could be NULL for global update.
 * @addr: the addr that triggered the swapin, ignored if @vma is NULL.
 */
void swap_update_readahead(struct folio *folio, struct vm_area_struct *vma,
			   unsigned long addr)
{
	bool readahead, vma_ra = swap_use_vma_readahead();

	/*
	 * At the moment, we don't support PG_readahead for anon THP
	 * so let's bail out rather than confusing the readahead stat.
	 */
	if (unlikely(folio_test_large(folio)))
		return;

	readahead = folio_test_clear_readahead(folio);
	if (vma && vma_ra) {
		unsigned long ra_val;
		int win, hits;

		ra_val = GET_SWAP_RA_VAL(vma);
		win = SWAP_RA_WIN(ra_val);
		hits = SWAP_RA_HITS(ra_val);
		if (readahead)
			hits = min_t(int, hits + 1, SWAP_RA_HITS_MAX);
		atomic_long_set(&vma->swap_readahead_info,
				SWAP_RA_VAL(addr, win, hits));
	}

	if (readahead) {
		count_vm_event(SWAP_RA_HIT);
		if (!vma || !vma_ra)
			atomic_inc(&swapin_readahead_hits);
	}
}

/**
 * __swap_cache_prepare_and_add - Prepare the folio and add it to swap cache.
 * @entry: swap entry to be bound to the folio.
 * @folio: folio to be added.
 * @gfp: memory allocation flags for charge, can be 0 if @charged if true.
 * @charged: if the folio is already charged.
 *
 * Update the swap_map and add folio as swap cache, typically before swapin.
 * All swap slots covered by the folio must have a non-zero swap count.
 *
 * Context: Caller must protect the swap device with reference count or locks.
 * Return: Returns the folio being added on success. Returns the existing folio
 * if @entry is already cached. Returns NULL if raced with swapin or swapoff.
 */
static struct folio *__swap_cache_prepare_and_add(swp_entry_t entry,
						  struct folio *folio,
						  gfp_t gfp, bool charged)
{
	struct folio *swapcache = NULL;
	void *shadow;
	int ret;

	__folio_set_locked(folio);
	__folio_set_swapbacked(folio);
	for (;;) {
		ret = swap_cache_add_folio(folio, entry, &shadow);
		if (!ret)
			break;

		/*
		 * Large order allocation needs special handling on
		 * race: if a smaller folio exists in cache, swapin needs
		 * to fallback to order 0, and doing a swap cache lookup
		 * might return a folio that is irrelevant to the faulting
		 * entry because @entry is aligned down. Just return NULL.
		 */
		if (ret != -EEXIST || folio_test_large(folio))
			goto failed;

		swapcache = swap_cache_get_folio(entry);
		if (swapcache)
			goto failed;
	}

	if (!charged && mem_cgroup_swapin_charge_folio(folio, NULL, gfp, entry)) {
		swap_cache_del_folio(folio);
		goto failed;
	}

	memcg1_swapin(entry, folio_nr_pages(folio));
	if (shadow)
		workingset_refault(folio, shadow);

	/* Caller will initiate read into locked folio */
	folio_add_lru(folio);
	return folio;

failed:
	folio_unlock(folio);
	return swapcache;
}

/**
 * swap_cache_alloc_folio - Allocate folio for swapped out slot in swap cache.
 * @entry: the swapped out swap entry to be binded to the folio.
 * @gfp_mask: memory allocation flags
 * @mpol: NUMA memory allocation policy to be applied
 * @ilx: NUMA interleave index, for use only when MPOL_INTERLEAVE
 * @new_page_allocated: sets true if allocation happened, false otherwise
 *
 * Allocate a folio in the swap cache for one swap slot, typically before
 * doing IO (e.g. swap in or zswap writeback). The swap slot indicated by
 * @entry must have a non-zero swap count (swapped out).
 * Currently only supports order 0.
 *
 * Context: Caller must protect the swap device with reference count or locks.
 * Return: Returns the existing folio if @entry is cached already. Returns
 * NULL if failed due to -ENOMEM or @entry have a swap count < 1.
 */
struct folio *swap_cache_alloc_folio(swp_entry_t entry, gfp_t gfp_mask,
				     struct mempolicy *mpol, pgoff_t ilx,
				     bool *new_page_allocated)
{
	struct swap_info_struct *si = __swap_entry_to_info(entry);
	struct folio *folio;
	struct folio *result = NULL;

	*new_page_allocated = false;
	/* Check the swap cache again for readahead path. */
	folio = swap_cache_get_folio(entry);
	if (folio)
		return folio;

	/* Skip allocation for unused and bad swap slot for readahead. */
	if (!swap_entry_swapped(si, entry))
		return NULL;

	/* Allocate a new folio to be added into the swap cache. */
	folio = folio_alloc_mpol(gfp_mask, 0, mpol, ilx, numa_node_id());
	if (!folio)
		return NULL;
	/* Try add the new folio, returns existing folio or NULL on failure. */
	result = __swap_cache_prepare_and_add(entry, folio, gfp_mask, false);
	if (result == folio)
		*new_page_allocated = true;
	else
		folio_put(folio);
	return result;
}

/**
 * swapin_folio - swap-in one or multiple entries skipping readahead.
 * @entry: starting swap entry to swap in
 * @folio: a new allocated and charged folio
 *
 * Reads @entry into @folio, @folio will be added to the swap cache.
 * If @folio is a large folio, the @entry will be rounded down to align
 * with the folio size.
 *
 * Return: returns pointer to @folio on success. If folio is a large folio
 * and this raced with another swapin, NULL will be returned to allow fallback
 * to order 0. Else, if another folio was already added to the swap cache,
 * return that swap cache folio instead.
 */
struct folio *swapin_folio(swp_entry_t entry, struct folio *folio)
{
	struct folio *swapcache;
	pgoff_t offset = swp_offset(entry);
	unsigned long nr_pages = folio_nr_pages(folio);

	entry = swp_entry(swp_type(entry), round_down(offset, nr_pages));
	swapcache = __swap_cache_prepare_and_add(entry, folio, 0, true);
	if (swapcache == folio)
		swap_read_folio(folio, NULL);
	return swapcache;
}

/*
 * Locate a page of swap in physical memory, reserving swap cache space
 * and reading the disk if it is not already cached.
 * A failure return means that either the page allocation failed or that
 * the swap entry is no longer in use.
 */
struct folio *read_swap_cache_async(swp_entry_t entry, gfp_t gfp_mask,
		struct vm_area_struct *vma, unsigned long addr,
		struct swap_iocb **plug)
{
	struct swap_info_struct *si;
	bool page_allocated;
	struct mempolicy *mpol;
	pgoff_t ilx;
	struct folio *folio;

	si = get_swap_device(entry);
	if (!si)
		return NULL;

	mpol = get_vma_policy(vma, addr, 0, &ilx);
	folio = swap_cache_alloc_folio(entry, gfp_mask, mpol, ilx,
				       &page_allocated);
	mpol_cond_put(mpol);

	if (page_allocated)
		swap_read_folio(folio, plug);

	put_swap_device(si);
	return folio;
}

static unsigned int __swapin_nr_pages(unsigned long prev_offset,
				      unsigned long offset,
				      int hits,
				      int max_pages,
				      int prev_win)
{
	unsigned int pages, last_ra;

	/*
	 * This heuristic has been found to work well on both sequential and
	 * random loads, swapping to hard disk or to SSD: please don't ask
	 * what the "+ 2" means, it just happens to work well, that's all.
	 */
	pages = hits + 2;
	if (pages == 2) {
		/*
		 * We can have no readahead hits to judge by: but must not get
		 * stuck here forever, so check for an adjacent offset instead
		 * (and don't even bother to check whether swap type is same).
		 */
		if (offset != prev_offset + 1 && offset != prev_offset - 1)
			pages = 1;
	} else {
		unsigned int roundup = 4;
		while (roundup < pages)
			roundup <<= 1;
		pages = roundup;
	}

	if (pages > max_pages)
		pages = max_pages;

	/* Don't shrink readahead too fast */
	last_ra = prev_win / 2;
	if (pages < last_ra)
		pages = last_ra;

	return pages;
}

static unsigned long swapin_nr_pages(unsigned long offset)
{
	static unsigned long prev_offset;
	unsigned int hits, pages, max_pages;
	static atomic_t last_readahead_pages;

	max_pages = 1 << READ_ONCE(page_cluster);
	if (max_pages <= 1)
		return 1;

	hits = atomic_xchg(&swapin_readahead_hits, 0);
	pages = __swapin_nr_pages(READ_ONCE(prev_offset), offset, hits,
				  max_pages,
				  atomic_read(&last_readahead_pages));
	if (!hits)
		WRITE_ONCE(prev_offset, offset);
	atomic_set(&last_readahead_pages, pages);

	return pages;
}

/**
 * swap_cluster_readahead - swap in pages in hope we need them soon
 * @entry: swap entry of this memory
 * @gfp_mask: memory allocation flags
 * @mpol: NUMA memory allocation policy to be applied
 * @ilx: NUMA interleave index, for use only when MPOL_INTERLEAVE
 *
 * Returns the struct folio for entry and addr, after queueing swapin.
 *
 * Primitive swap readahead code. We simply read an aligned block of
 * (1 << page_cluster) entries in the swap area. This method is chosen
 * because it doesn't cost us any seek time.  We also make sure to queue
 * the 'original' request together with the readahead ones...
 *
 * Note: it is intentional that the same NUMA policy and interleave index
 * are used for every page of the readahead: neighbouring pages on swap
 * are fairly likely to have been swapped out from the same node.
 */
struct folio *swap_cluster_readahead(swp_entry_t entry, gfp_t gfp_mask,
				    struct mempolicy *mpol, pgoff_t ilx)
{
	struct folio *folio;
	unsigned long entry_offset = swp_offset(entry);
	unsigned long offset = entry_offset;
	unsigned long start_offset, end_offset;
	unsigned long mask;
	struct swap_info_struct *si = __swap_entry_to_info(entry);
	struct blk_plug plug;
	struct swap_iocb *splug = NULL;
	bool page_allocated;

	mask = swapin_nr_pages(offset) - 1;
	if (!mask)
		goto skip;

	/* Read a page_cluster sized and aligned cluster around offset. */
	start_offset = offset & ~mask;
	end_offset = offset | mask;
	if (!start_offset)	/* First page is swap header. */
		start_offset++;
	if (end_offset >= si->max)
		end_offset = si->max - 1;

	blk_start_plug(&plug);
	for (offset = start_offset; offset <= end_offset ; offset++) {
		/* Ok, do the async read-ahead now */
		folio = swap_cache_alloc_folio(
			swp_entry(swp_type(entry), offset), gfp_mask, mpol, ilx,
			&page_allocated);
		if (!folio)
			continue;
		if (page_allocated) {
			swap_read_folio(folio, &splug);
			if (offset != entry_offset) {
				folio_set_readahead(folio);
				count_vm_event(SWAP_RA);
			}
		}
		folio_put(folio);
	}
	blk_finish_plug(&plug);
	swap_read_unplug(splug);
	lru_add_drain();	/* Push any new pages onto the LRU now */
skip:
	/* The page was likely read above, so no need for plugging here */
	folio = swap_cache_alloc_folio(entry, gfp_mask, mpol, ilx,
				       &page_allocated);
	if (unlikely(page_allocated))
		swap_read_folio(folio, NULL);
	return folio;
}

static int swap_vma_ra_win(struct vm_fault *vmf, unsigned long *start,
			   unsigned long *end)
{
	struct vm_area_struct *vma = vmf->vma;
	unsigned long ra_val;
	unsigned long faddr, prev_faddr, left, right;
	unsigned int max_win, hits, prev_win, win;

	max_win = 1 << min(READ_ONCE(page_cluster), SWAP_RA_ORDER_CEILING);
	if (max_win == 1)
		return 1;

	faddr = vmf->address;
	ra_val = GET_SWAP_RA_VAL(vma);
	prev_faddr = SWAP_RA_ADDR(ra_val);
	prev_win = SWAP_RA_WIN(ra_val);
	hits = SWAP_RA_HITS(ra_val);
	win = __swapin_nr_pages(PFN_DOWN(prev_faddr), PFN_DOWN(faddr), hits,
				max_win, prev_win);
	atomic_long_set(&vma->swap_readahead_info, SWAP_RA_VAL(faddr, win, 0));
	if (win == 1)
		return 1;

	if (faddr == prev_faddr + PAGE_SIZE)
		left = faddr;
	else if (prev_faddr == faddr + PAGE_SIZE)
		left = faddr - (win << PAGE_SHIFT) + PAGE_SIZE;
	else
		left = faddr - (((win - 1) / 2) << PAGE_SHIFT);
	right = left + (win << PAGE_SHIFT);
	if ((long)left < 0)
		left = 0;
	*start = max3(left, vma->vm_start, faddr & PMD_MASK);
	*end = min3(right, vma->vm_end, (faddr & PMD_MASK) + PMD_SIZE);

	return win;
}

/**
 * swap_vma_readahead - swap in pages in hope we need them soon
 * @targ_entry: swap entry of the targeted memory
 * @gfp_mask: memory allocation flags
 * @mpol: NUMA memory allocation policy to be applied
 * @targ_ilx: NUMA interleave index, for use only when MPOL_INTERLEAVE
 * @vmf: fault information
 *
 * Returns the struct folio for entry and addr, after queueing swapin.
 *
 * Primitive swap readahead code. We simply read in a few pages whose
 * virtual addresses are around the fault address in the same vma.
 *
 * Caller must hold read mmap_lock if vmf->vma is not NULL.
 *
 */
static struct folio *swap_vma_readahead(swp_entry_t targ_entry, gfp_t gfp_mask,
		struct mempolicy *mpol, pgoff_t targ_ilx, struct vm_fault *vmf)
{
	struct blk_plug plug;
	struct swap_iocb *splug = NULL;
	struct folio *folio;
	pte_t *pte = NULL, pentry;
	int win;
	unsigned long start, end, addr;
	pgoff_t ilx;
	bool page_allocated;

	win = swap_vma_ra_win(vmf, &start, &end);
	if (win == 1)
		goto skip;

	ilx = targ_ilx - PFN_DOWN(vmf->address - start);

	blk_start_plug(&plug);
	for (addr = start; addr < end; ilx++, addr += PAGE_SIZE) {
		struct swap_info_struct *si = NULL;
		softleaf_t entry;

		if (!pte++) {
			pte = pte_offset_map(vmf->pmd, addr);
			if (!pte)
				break;
		}
		pentry = ptep_get_lockless(pte);
		entry = softleaf_from_pte(pentry);

		if (!softleaf_is_swap(entry))
			continue;
		pte_unmap(pte);
		pte = NULL;
		/*
		 * Readahead entry may come from a device that we are not
		 * holding a reference to, try to grab a reference, or skip.
		 */
		if (swp_type(entry) != swp_type(targ_entry)) {
			si = get_swap_device(entry);
			if (!si)
				continue;
		}
		folio = swap_cache_alloc_folio(entry, gfp_mask, mpol, ilx,
					       &page_allocated);
		if (si)
			put_swap_device(si);
		if (!folio)
			continue;
		if (page_allocated) {
			swap_read_folio(folio, &splug);
			if (addr != vmf->address) {
				folio_set_readahead(folio);
				count_vm_event(SWAP_RA);
			}
		}
		folio_put(folio);
	}
	if (pte)
		pte_unmap(pte);
	blk_finish_plug(&plug);
	swap_read_unplug(splug);
	lru_add_drain();
skip:
	/* The folio was likely read above, so no need for plugging here */
	folio = swap_cache_alloc_folio(targ_entry, gfp_mask, mpol, targ_ilx,
				       &page_allocated);
	if (unlikely(page_allocated))
		swap_read_folio(folio, NULL);
	return folio;
}

/**
 * swapin_readahead - swap in pages in hope we need them soon
 * @entry: swap entry of this memory
 * @gfp_mask: memory allocation flags
 * @vmf: fault information
 *
 * Returns the struct folio for entry and addr, after queueing swapin.
 *
 * It's a main entry function for swap readahead. By the configuration,
 * it will read ahead blocks by cluster-based(ie, physical disk based)
 * or vma-based(ie, virtual address based on faulty address) readahead.
 */
struct folio *swapin_readahead(swp_entry_t entry, gfp_t gfp_mask,
				struct vm_fault *vmf)
{
	struct mempolicy *mpol;
	pgoff_t ilx;
	struct folio *folio;

	mpol = get_vma_policy(vmf->vma, vmf->address, 0, &ilx);
	folio = swap_use_vma_readahead() ?
		swap_vma_readahead(entry, gfp_mask, mpol, ilx, vmf) :
		swap_cluster_readahead(entry, gfp_mask, mpol, ilx);
	mpol_cond_put(mpol);

	return folio;
}

#ifdef CONFIG_SYSFS
static ssize_t vma_ra_enabled_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", str_true_false(enable_vma_readahead));
}
static ssize_t vma_ra_enabled_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	ssize_t ret;

	ret = kstrtobool(buf, &enable_vma_readahead);
	if (ret)
		return ret;

	return count;
}
static struct kobj_attribute vma_ra_enabled_attr = __ATTR_RW(vma_ra_enabled);

static struct attribute *swap_attrs[] = {
	&vma_ra_enabled_attr.attr,
	NULL,
};

static const struct attribute_group swap_attr_group = {
	.attrs = swap_attrs,
};

static int __init swap_init(void)
{
	int err;
	struct kobject *swap_kobj;

	swap_kobj = kobject_create_and_add("swap", mm_kobj);
	if (!swap_kobj) {
		pr_err("failed to create swap kobject\n");
		return -ENOMEM;
	}
	err = sysfs_create_group(swap_kobj, &swap_attr_group);
	if (err) {
		pr_err("failed to register swap group\n");
		goto delete_obj;
	}
	/* Swap cache writeback is LRU based, no tags for it */
	mapping_set_no_writeback_tags(&swap_space);
	return 0;

delete_obj:
	kobject_put(swap_kobj);
	return err;
}
subsys_initcall(swap_init);
#endif
