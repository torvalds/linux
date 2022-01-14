/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MM_INLINE_H
#define LINUX_MM_INLINE_H

#include <linux/huge_mm.h>
#include <linux/swap.h>
#include <linux/string.h>

/**
 * folio_is_file_lru - Should the folio be on a file LRU or anon LRU?
 * @folio: The folio to test.
 *
 * We would like to get this info without a page flag, but the state
 * needs to survive until the folio is last deleted from the LRU, which
 * could be as far down as __page_cache_release.
 *
 * Return: An integer (not a boolean!) used to sort a folio onto the
 * right LRU list and to account folios correctly.
 * 1 if @folio is a regular filesystem backed page cache folio
 * or a lazily freed anonymous folio (e.g. via MADV_FREE).
 * 0 if @folio is a normal anonymous folio, a tmpfs folio or otherwise
 * ram or swap backed folio.
 */
static inline int folio_is_file_lru(struct folio *folio)
{
	return !folio_test_swapbacked(folio);
}

static inline int page_is_file_lru(struct page *page)
{
	return folio_is_file_lru(page_folio(page));
}

static __always_inline void update_lru_size(struct lruvec *lruvec,
				enum lru_list lru, enum zone_type zid,
				long nr_pages)
{
	struct pglist_data *pgdat = lruvec_pgdat(lruvec);

	__mod_lruvec_state(lruvec, NR_LRU_BASE + lru, nr_pages);
	__mod_zone_page_state(&pgdat->node_zones[zid],
				NR_ZONE_LRU_BASE + lru, nr_pages);
#ifdef CONFIG_MEMCG
	mem_cgroup_update_lru_size(lruvec, lru, zid, nr_pages);
#endif
}

/**
 * __folio_clear_lru_flags - Clear page lru flags before releasing a page.
 * @folio: The folio that was on lru and now has a zero reference.
 */
static __always_inline void __folio_clear_lru_flags(struct folio *folio)
{
	VM_BUG_ON_FOLIO(!folio_test_lru(folio), folio);

	__folio_clear_lru(folio);

	/* this shouldn't happen, so leave the flags to bad_page() */
	if (folio_test_active(folio) && folio_test_unevictable(folio))
		return;

	__folio_clear_active(folio);
	__folio_clear_unevictable(folio);
}

static __always_inline void __clear_page_lru_flags(struct page *page)
{
	__folio_clear_lru_flags(page_folio(page));
}

/**
 * folio_lru_list - Which LRU list should a folio be on?
 * @folio: The folio to test.
 *
 * Return: The LRU list a folio should be on, as an index
 * into the array of LRU lists.
 */
static __always_inline enum lru_list folio_lru_list(struct folio *folio)
{
	enum lru_list lru;

	VM_BUG_ON_FOLIO(folio_test_active(folio) && folio_test_unevictable(folio), folio);

	if (folio_test_unevictable(folio))
		return LRU_UNEVICTABLE;

	lru = folio_is_file_lru(folio) ? LRU_INACTIVE_FILE : LRU_INACTIVE_ANON;
	if (folio_test_active(folio))
		lru += LRU_ACTIVE;

	return lru;
}

static __always_inline
void lruvec_add_folio(struct lruvec *lruvec, struct folio *folio)
{
	enum lru_list lru = folio_lru_list(folio);

	update_lru_size(lruvec, lru, folio_zonenum(folio),
			folio_nr_pages(folio));
	list_add(&folio->lru, &lruvec->lists[lru]);
}

static __always_inline void add_page_to_lru_list(struct page *page,
				struct lruvec *lruvec)
{
	lruvec_add_folio(lruvec, page_folio(page));
}

static __always_inline
void lruvec_add_folio_tail(struct lruvec *lruvec, struct folio *folio)
{
	enum lru_list lru = folio_lru_list(folio);

	update_lru_size(lruvec, lru, folio_zonenum(folio),
			folio_nr_pages(folio));
	list_add_tail(&folio->lru, &lruvec->lists[lru]);
}

static __always_inline void add_page_to_lru_list_tail(struct page *page,
				struct lruvec *lruvec)
{
	lruvec_add_folio_tail(lruvec, page_folio(page));
}

static __always_inline
void lruvec_del_folio(struct lruvec *lruvec, struct folio *folio)
{
	list_del(&folio->lru);
	update_lru_size(lruvec, folio_lru_list(folio), folio_zonenum(folio),
			-folio_nr_pages(folio));
}

static __always_inline void del_page_from_lru_list(struct page *page,
				struct lruvec *lruvec)
{
	lruvec_del_folio(lruvec, page_folio(page));
}

#ifdef CONFIG_ANON_VMA_NAME
/*
 * mmap_lock should be read-locked when calling vma_anon_name() and while using
 * the returned pointer.
 */
extern const char *vma_anon_name(struct vm_area_struct *vma);

/*
 * mmap_lock should be read-locked for orig_vma->vm_mm.
 * mmap_lock should be write-locked for new_vma->vm_mm or new_vma should be
 * isolated.
 */
extern void dup_vma_anon_name(struct vm_area_struct *orig_vma,
			      struct vm_area_struct *new_vma);

/*
 * mmap_lock should be write-locked or vma should have been isolated under
 * write-locked mmap_lock protection.
 */
extern void free_vma_anon_name(struct vm_area_struct *vma);

/* mmap_lock should be read-locked */
static inline bool is_same_vma_anon_name(struct vm_area_struct *vma,
					 const char *name)
{
	const char *vma_name = vma_anon_name(vma);

	/* either both NULL, or pointers to same string */
	if (vma_name == name)
		return true;

	return name && vma_name && !strcmp(name, vma_name);
}
#else /* CONFIG_ANON_VMA_NAME */
static inline const char *vma_anon_name(struct vm_area_struct *vma)
{
	return NULL;
}
static inline void dup_vma_anon_name(struct vm_area_struct *orig_vma,
			      struct vm_area_struct *new_vma) {}
static inline void free_vma_anon_name(struct vm_area_struct *vma) {}
static inline bool is_same_vma_anon_name(struct vm_area_struct *vma,
					 const char *name)
{
	return true;
}
#endif  /* CONFIG_ANON_VMA_NAME */

#endif
