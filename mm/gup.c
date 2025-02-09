// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/spinlock.h>

#include <linux/mm.h>
#include <linux/memfd.h>
#include <linux/memremap.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/secretmem.h>

#include <linux/sched/signal.h>
#include <linux/rwsem.h>
#include <linux/hugetlb.h>
#include <linux/migrate.h>
#include <linux/mm_inline.h>
#include <linux/pagevec.h>
#include <linux/sched/mm.h>
#include <linux/shmem_fs.h>

#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

#include "internal.h"

struct follow_page_context {
	struct dev_pagemap *pgmap;
	unsigned int page_mask;
};

static inline void sanity_check_pinned_pages(struct page **pages,
					     unsigned long npages)
{
	if (!IS_ENABLED(CONFIG_DEBUG_VM))
		return;

	/*
	 * We only pin anonymous pages if they are exclusive. Once pinned, we
	 * can no longer turn them possibly shared and PageAnonExclusive() will
	 * stick around until the page is freed.
	 *
	 * We'd like to verify that our pinned anonymous pages are still mapped
	 * exclusively. The issue with anon THP is that we don't know how
	 * they are/were mapped when pinning them. However, for anon
	 * THP we can assume that either the given page (PTE-mapped THP) or
	 * the head page (PMD-mapped THP) should be PageAnonExclusive(). If
	 * neither is the case, there is certainly something wrong.
	 */
	for (; npages; npages--, pages++) {
		struct page *page = *pages;
		struct folio *folio;

		if (!page)
			continue;

		folio = page_folio(page);

		if (is_zero_page(page) ||
		    !folio_test_anon(folio))
			continue;
		if (!folio_test_large(folio) || folio_test_hugetlb(folio))
			VM_BUG_ON_PAGE(!PageAnonExclusive(&folio->page), page);
		else
			/* Either a PTE-mapped or a PMD-mapped THP. */
			VM_BUG_ON_PAGE(!PageAnonExclusive(&folio->page) &&
				       !PageAnonExclusive(page), page);
	}
}

/*
 * Return the folio with ref appropriately incremented,
 * or NULL if that failed.
 */
static inline struct folio *try_get_folio(struct page *page, int refs)
{
	struct folio *folio;

retry:
	folio = page_folio(page);
	if (WARN_ON_ONCE(folio_ref_count(folio) < 0))
		return NULL;
	if (unlikely(!folio_ref_try_add(folio, refs)))
		return NULL;

	/*
	 * At this point we have a stable reference to the folio; but it
	 * could be that between calling page_folio() and the refcount
	 * increment, the folio was split, in which case we'd end up
	 * holding a reference on a folio that has nothing to do with the page
	 * we were given anymore.
	 * So now that the folio is stable, recheck that the page still
	 * belongs to this folio.
	 */
	if (unlikely(page_folio(page) != folio)) {
		if (!put_devmap_managed_folio_refs(folio, refs))
			folio_put_refs(folio, refs);
		goto retry;
	}

	return folio;
}

static void gup_put_folio(struct folio *folio, int refs, unsigned int flags)
{
	if (flags & FOLL_PIN) {
		if (is_zero_folio(folio))
			return;
		node_stat_mod_folio(folio, NR_FOLL_PIN_RELEASED, refs);
		if (folio_test_large(folio))
			atomic_sub(refs, &folio->_pincount);
		else
			refs *= GUP_PIN_COUNTING_BIAS;
	}

	if (!put_devmap_managed_folio_refs(folio, refs))
		folio_put_refs(folio, refs);
}

/**
 * try_grab_folio() - add a folio's refcount by a flag-dependent amount
 * @folio:    pointer to folio to be grabbed
 * @refs:     the value to (effectively) add to the folio's refcount
 * @flags:    gup flags: these are the FOLL_* flag values
 *
 * This might not do anything at all, depending on the flags argument.
 *
 * "grab" names in this file mean, "look at flags to decide whether to use
 * FOLL_PIN or FOLL_GET behavior, when incrementing the folio's refcount.
 *
 * Either FOLL_PIN or FOLL_GET (or neither) may be set, but not both at the same
 * time.
 *
 * Return: 0 for success, or if no action was required (if neither FOLL_PIN
 * nor FOLL_GET was set, nothing is done). A negative error code for failure:
 *
 *   -ENOMEM		FOLL_GET or FOLL_PIN was set, but the folio could not
 *			be grabbed.
 *
 * It is called when we have a stable reference for the folio, typically in
 * GUP slow path.
 */
int __must_check try_grab_folio(struct folio *folio, int refs,
				unsigned int flags)
{
	if (WARN_ON_ONCE(folio_ref_count(folio) <= 0))
		return -ENOMEM;

	if (unlikely(!(flags & FOLL_PCI_P2PDMA) && is_pci_p2pdma_page(&folio->page)))
		return -EREMOTEIO;

	if (flags & FOLL_GET)
		folio_ref_add(folio, refs);
	else if (flags & FOLL_PIN) {
		/*
		 * Don't take a pin on the zero page - it's not going anywhere
		 * and it is used in a *lot* of places.
		 */
		if (is_zero_folio(folio))
			return 0;

		/*
		 * Increment the normal page refcount field at least once,
		 * so that the page really is pinned.
		 */
		if (folio_test_large(folio)) {
			folio_ref_add(folio, refs);
			atomic_add(refs, &folio->_pincount);
		} else {
			folio_ref_add(folio, refs * GUP_PIN_COUNTING_BIAS);
		}

		node_stat_mod_folio(folio, NR_FOLL_PIN_ACQUIRED, refs);
	}

	return 0;
}

/**
 * unpin_user_page() - release a dma-pinned page
 * @page:            pointer to page to be released
 *
 * Pages that were pinned via pin_user_pages*() must be released via either
 * unpin_user_page(), or one of the unpin_user_pages*() routines. This is so
 * that such pages can be separately tracked and uniquely handled. In
 * particular, interactions with RDMA and filesystems need special handling.
 */
void unpin_user_page(struct page *page)
{
	sanity_check_pinned_pages(&page, 1);
	gup_put_folio(page_folio(page), 1, FOLL_PIN);
}
EXPORT_SYMBOL(unpin_user_page);

/**
 * unpin_folio() - release a dma-pinned folio
 * @folio:         pointer to folio to be released
 *
 * Folios that were pinned via memfd_pin_folios() or other similar routines
 * must be released either using unpin_folio() or unpin_folios().
 */
void unpin_folio(struct folio *folio)
{
	gup_put_folio(folio, 1, FOLL_PIN);
}
EXPORT_SYMBOL_GPL(unpin_folio);

/**
 * folio_add_pin - Try to get an additional pin on a pinned folio
 * @folio: The folio to be pinned
 *
 * Get an additional pin on a folio we already have a pin on.  Makes no change
 * if the folio is a zero_page.
 */
void folio_add_pin(struct folio *folio)
{
	if (is_zero_folio(folio))
		return;

	/*
	 * Similar to try_grab_folio(): be sure to *also* increment the normal
	 * page refcount field at least once, so that the page really is
	 * pinned.
	 */
	if (folio_test_large(folio)) {
		WARN_ON_ONCE(atomic_read(&folio->_pincount) < 1);
		folio_ref_inc(folio);
		atomic_inc(&folio->_pincount);
	} else {
		WARN_ON_ONCE(folio_ref_count(folio) < GUP_PIN_COUNTING_BIAS);
		folio_ref_add(folio, GUP_PIN_COUNTING_BIAS);
	}
}

static inline struct folio *gup_folio_range_next(struct page *start,
		unsigned long npages, unsigned long i, unsigned int *ntails)
{
	struct page *next = nth_page(start, i);
	struct folio *folio = page_folio(next);
	unsigned int nr = 1;

	if (folio_test_large(folio))
		nr = min_t(unsigned int, npages - i,
			   folio_nr_pages(folio) - folio_page_idx(folio, next));

	*ntails = nr;
	return folio;
}

static inline struct folio *gup_folio_next(struct page **list,
		unsigned long npages, unsigned long i, unsigned int *ntails)
{
	struct folio *folio = page_folio(list[i]);
	unsigned int nr;

	for (nr = i + 1; nr < npages; nr++) {
		if (page_folio(list[nr]) != folio)
			break;
	}

	*ntails = nr - i;
	return folio;
}

/**
 * unpin_user_pages_dirty_lock() - release and optionally dirty gup-pinned pages
 * @pages:  array of pages to be maybe marked dirty, and definitely released.
 * @npages: number of pages in the @pages array.
 * @make_dirty: whether to mark the pages dirty
 *
 * "gup-pinned page" refers to a page that has had one of the get_user_pages()
 * variants called on that page.
 *
 * For each page in the @pages array, make that page (or its head page, if a
 * compound page) dirty, if @make_dirty is true, and if the page was previously
 * listed as clean. In any case, releases all pages using unpin_user_page(),
 * possibly via unpin_user_pages(), for the non-dirty case.
 *
 * Please see the unpin_user_page() documentation for details.
 *
 * set_page_dirty_lock() is used internally. If instead, set_page_dirty() is
 * required, then the caller should a) verify that this is really correct,
 * because _lock() is usually required, and b) hand code it:
 * set_page_dirty_lock(), unpin_user_page().
 *
 */
void unpin_user_pages_dirty_lock(struct page **pages, unsigned long npages,
				 bool make_dirty)
{
	unsigned long i;
	struct folio *folio;
	unsigned int nr;

	if (!make_dirty) {
		unpin_user_pages(pages, npages);
		return;
	}

	sanity_check_pinned_pages(pages, npages);
	for (i = 0; i < npages; i += nr) {
		folio = gup_folio_next(pages, npages, i, &nr);
		/*
		 * Checking PageDirty at this point may race with
		 * clear_page_dirty_for_io(), but that's OK. Two key
		 * cases:
		 *
		 * 1) This code sees the page as already dirty, so it
		 * skips the call to set_page_dirty(). That could happen
		 * because clear_page_dirty_for_io() called
		 * folio_mkclean(), followed by set_page_dirty().
		 * However, now the page is going to get written back,
		 * which meets the original intention of setting it
		 * dirty, so all is well: clear_page_dirty_for_io() goes
		 * on to call TestClearPageDirty(), and write the page
		 * back.
		 *
		 * 2) This code sees the page as clean, so it calls
		 * set_page_dirty(). The page stays dirty, despite being
		 * written back, so it gets written back again in the
		 * next writeback cycle. This is harmless.
		 */
		if (!folio_test_dirty(folio)) {
			folio_lock(folio);
			folio_mark_dirty(folio);
			folio_unlock(folio);
		}
		gup_put_folio(folio, nr, FOLL_PIN);
	}
}
EXPORT_SYMBOL(unpin_user_pages_dirty_lock);

/**
 * unpin_user_page_range_dirty_lock() - release and optionally dirty
 * gup-pinned page range
 *
 * @page:  the starting page of a range maybe marked dirty, and definitely released.
 * @npages: number of consecutive pages to release.
 * @make_dirty: whether to mark the pages dirty
 *
 * "gup-pinned page range" refers to a range of pages that has had one of the
 * pin_user_pages() variants called on that page.
 *
 * For the page ranges defined by [page .. page+npages], make that range (or
 * its head pages, if a compound page) dirty, if @make_dirty is true, and if the
 * page range was previously listed as clean.
 *
 * set_page_dirty_lock() is used internally. If instead, set_page_dirty() is
 * required, then the caller should a) verify that this is really correct,
 * because _lock() is usually required, and b) hand code it:
 * set_page_dirty_lock(), unpin_user_page().
 *
 */
void unpin_user_page_range_dirty_lock(struct page *page, unsigned long npages,
				      bool make_dirty)
{
	unsigned long i;
	struct folio *folio;
	unsigned int nr;

	for (i = 0; i < npages; i += nr) {
		folio = gup_folio_range_next(page, npages, i, &nr);
		if (make_dirty && !folio_test_dirty(folio)) {
			folio_lock(folio);
			folio_mark_dirty(folio);
			folio_unlock(folio);
		}
		gup_put_folio(folio, nr, FOLL_PIN);
	}
}
EXPORT_SYMBOL(unpin_user_page_range_dirty_lock);

static void gup_fast_unpin_user_pages(struct page **pages, unsigned long npages)
{
	unsigned long i;
	struct folio *folio;
	unsigned int nr;

	/*
	 * Don't perform any sanity checks because we might have raced with
	 * fork() and some anonymous pages might now actually be shared --
	 * which is why we're unpinning after all.
	 */
	for (i = 0; i < npages; i += nr) {
		folio = gup_folio_next(pages, npages, i, &nr);
		gup_put_folio(folio, nr, FOLL_PIN);
	}
}

/**
 * unpin_user_pages() - release an array of gup-pinned pages.
 * @pages:  array of pages to be marked dirty and released.
 * @npages: number of pages in the @pages array.
 *
 * For each page in the @pages array, release the page using unpin_user_page().
 *
 * Please see the unpin_user_page() documentation for details.
 */
void unpin_user_pages(struct page **pages, unsigned long npages)
{
	unsigned long i;
	struct folio *folio;
	unsigned int nr;

	/*
	 * If this WARN_ON() fires, then the system *might* be leaking pages (by
	 * leaving them pinned), but probably not. More likely, gup/pup returned
	 * a hard -ERRNO error to the caller, who erroneously passed it here.
	 */
	if (WARN_ON(IS_ERR_VALUE(npages)))
		return;

	sanity_check_pinned_pages(pages, npages);
	for (i = 0; i < npages; i += nr) {
		if (!pages[i]) {
			nr = 1;
			continue;
		}
		folio = gup_folio_next(pages, npages, i, &nr);
		gup_put_folio(folio, nr, FOLL_PIN);
	}
}
EXPORT_SYMBOL(unpin_user_pages);

/**
 * unpin_user_folio() - release pages of a folio
 * @folio:  pointer to folio to be released
 * @npages: number of pages of same folio
 *
 * Release npages of the folio
 */
void unpin_user_folio(struct folio *folio, unsigned long npages)
{
	gup_put_folio(folio, npages, FOLL_PIN);
}
EXPORT_SYMBOL(unpin_user_folio);

/**
 * unpin_folios() - release an array of gup-pinned folios.
 * @folios:  array of folios to be marked dirty and released.
 * @nfolios: number of folios in the @folios array.
 *
 * For each folio in the @folios array, release the folio using gup_put_folio.
 *
 * Please see the unpin_folio() documentation for details.
 */
void unpin_folios(struct folio **folios, unsigned long nfolios)
{
	unsigned long i = 0, j;

	/*
	 * If this WARN_ON() fires, then the system *might* be leaking folios
	 * (by leaving them pinned), but probably not. More likely, gup/pup
	 * returned a hard -ERRNO error to the caller, who erroneously passed
	 * it here.
	 */
	if (WARN_ON(IS_ERR_VALUE(nfolios)))
		return;

	while (i < nfolios) {
		for (j = i + 1; j < nfolios; j++)
			if (folios[i] != folios[j])
				break;

		if (folios[i])
			gup_put_folio(folios[i], j - i, FOLL_PIN);
		i = j;
	}
}
EXPORT_SYMBOL_GPL(unpin_folios);

/*
 * Set the MMF_HAS_PINNED if not set yet; after set it'll be there for the mm's
 * lifecycle.  Avoid setting the bit unless necessary, or it might cause write
 * cache bouncing on large SMP machines for concurrent pinned gups.
 */
static inline void mm_set_has_pinned_flag(unsigned long *mm_flags)
{
	if (!test_bit(MMF_HAS_PINNED, mm_flags))
		set_bit(MMF_HAS_PINNED, mm_flags);
}

#ifdef CONFIG_MMU

#ifdef CONFIG_HAVE_GUP_FAST
static int record_subpages(struct page *page, unsigned long sz,
			   unsigned long addr, unsigned long end,
			   struct page **pages)
{
	struct page *start_page;
	int nr;

	start_page = nth_page(page, (addr & (sz - 1)) >> PAGE_SHIFT);
	for (nr = 0; addr != end; nr++, addr += PAGE_SIZE)
		pages[nr] = nth_page(start_page, nr);

	return nr;
}

/**
 * try_grab_folio_fast() - Attempt to get or pin a folio in fast path.
 * @page:  pointer to page to be grabbed
 * @refs:  the value to (effectively) add to the folio's refcount
 * @flags: gup flags: these are the FOLL_* flag values.
 *
 * "grab" names in this file mean, "look at flags to decide whether to use
 * FOLL_PIN or FOLL_GET behavior, when incrementing the folio's refcount.
 *
 * Either FOLL_PIN or FOLL_GET (or neither) must be set, but not both at the
 * same time. (That's true throughout the get_user_pages*() and
 * pin_user_pages*() APIs.) Cases:
 *
 *    FOLL_GET: folio's refcount will be incremented by @refs.
 *
 *    FOLL_PIN on large folios: folio's refcount will be incremented by
 *    @refs, and its pincount will be incremented by @refs.
 *
 *    FOLL_PIN on single-page folios: folio's refcount will be incremented by
 *    @refs * GUP_PIN_COUNTING_BIAS.
 *
 * Return: The folio containing @page (with refcount appropriately
 * incremented) for success, or NULL upon failure. If neither FOLL_GET
 * nor FOLL_PIN was set, that's considered failure, and furthermore,
 * a likely bug in the caller, so a warning is also emitted.
 *
 * It uses add ref unless zero to elevate the folio refcount and must be called
 * in fast path only.
 */
static struct folio *try_grab_folio_fast(struct page *page, int refs,
					 unsigned int flags)
{
	struct folio *folio;

	/* Raise warn if it is not called in fast GUP */
	VM_WARN_ON_ONCE(!irqs_disabled());

	if (WARN_ON_ONCE((flags & (FOLL_GET | FOLL_PIN)) == 0))
		return NULL;

	if (unlikely(!(flags & FOLL_PCI_P2PDMA) && is_pci_p2pdma_page(page)))
		return NULL;

	if (flags & FOLL_GET)
		return try_get_folio(page, refs);

	/* FOLL_PIN is set */

	/*
	 * Don't take a pin on the zero page - it's not going anywhere
	 * and it is used in a *lot* of places.
	 */
	if (is_zero_page(page))
		return page_folio(page);

	folio = try_get_folio(page, refs);
	if (!folio)
		return NULL;

	/*
	 * Can't do FOLL_LONGTERM + FOLL_PIN gup fast path if not in a
	 * right zone, so fail and let the caller fall back to the slow
	 * path.
	 */
	if (unlikely((flags & FOLL_LONGTERM) &&
		     !folio_is_longterm_pinnable(folio))) {
		if (!put_devmap_managed_folio_refs(folio, refs))
			folio_put_refs(folio, refs);
		return NULL;
	}

	/*
	 * When pinning a large folio, use an exact count to track it.
	 *
	 * However, be sure to *also* increment the normal folio
	 * refcount field at least once, so that the folio really
	 * is pinned.  That's why the refcount from the earlier
	 * try_get_folio() is left intact.
	 */
	if (folio_test_large(folio))
		atomic_add(refs, &folio->_pincount);
	else
		folio_ref_add(folio,
				refs * (GUP_PIN_COUNTING_BIAS - 1));
	/*
	 * Adjust the pincount before re-checking the PTE for changes.
	 * This is essentially a smp_mb() and is paired with a memory
	 * barrier in folio_try_share_anon_rmap_*().
	 */
	smp_mb__after_atomic();

	node_stat_mod_folio(folio, NR_FOLL_PIN_ACQUIRED, refs);

	return folio;
}
#endif	/* CONFIG_HAVE_GUP_FAST */

/* Common code for can_follow_write_* */
static inline bool can_follow_write_common(struct page *page,
		struct vm_area_struct *vma, unsigned int flags)
{
	/* Maybe FOLL_FORCE is set to override it? */
	if (!(flags & FOLL_FORCE))
		return false;

	/* But FOLL_FORCE has no effect on shared mappings */
	if (vma->vm_flags & (VM_MAYSHARE | VM_SHARED))
		return false;

	/* ... or read-only private ones */
	if (!(vma->vm_flags & VM_MAYWRITE))
		return false;

	/* ... or already writable ones that just need to take a write fault */
	if (vma->vm_flags & VM_WRITE)
		return false;

	/*
	 * See can_change_pte_writable(): we broke COW and could map the page
	 * writable if we have an exclusive anonymous page ...
	 */
	return page && PageAnon(page) && PageAnonExclusive(page);
}

static struct page *no_page_table(struct vm_area_struct *vma,
				  unsigned int flags, unsigned long address)
{
	if (!(flags & FOLL_DUMP))
		return NULL;

	/*
	 * When core dumping, we don't want to allocate unnecessary pages or
	 * page tables.  Return error instead of NULL to skip handle_mm_fault,
	 * then get_dump_page() will return NULL to leave a hole in the dump.
	 * But we can only make this optimization where a hole would surely
	 * be zero-filled if handle_mm_fault() actually did handle it.
	 */
	if (is_vm_hugetlb_page(vma)) {
		struct hstate *h = hstate_vma(vma);

		if (!hugetlbfs_pagecache_present(h, vma, address))
			return ERR_PTR(-EFAULT);
	} else if ((vma_is_anonymous(vma) || !vma->vm_ops->fault)) {
		return ERR_PTR(-EFAULT);
	}

	return NULL;
}

#ifdef CONFIG_PGTABLE_HAS_HUGE_LEAVES
/* FOLL_FORCE can write to even unwritable PUDs in COW mappings. */
static inline bool can_follow_write_pud(pud_t pud, struct page *page,
					struct vm_area_struct *vma,
					unsigned int flags)
{
	/* If the pud is writable, we can write to the page. */
	if (pud_write(pud))
		return true;

	return can_follow_write_common(page, vma, flags);
}

static struct page *follow_huge_pud(struct vm_area_struct *vma,
				    unsigned long addr, pud_t *pudp,
				    int flags, struct follow_page_context *ctx)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page *page;
	pud_t pud = *pudp;
	unsigned long pfn = pud_pfn(pud);
	int ret;

	assert_spin_locked(pud_lockptr(mm, pudp));

	if (!pud_present(pud))
		return NULL;

	if ((flags & FOLL_WRITE) &&
	    !can_follow_write_pud(pud, pfn_to_page(pfn), vma, flags))
		return NULL;

	pfn += (addr & ~PUD_MASK) >> PAGE_SHIFT;

	if (IS_ENABLED(CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD) &&
	    pud_devmap(pud)) {
		/*
		 * device mapped pages can only be returned if the caller
		 * will manage the page reference count.
		 *
		 * At least one of FOLL_GET | FOLL_PIN must be set, so
		 * assert that here:
		 */
		if (!(flags & (FOLL_GET | FOLL_PIN)))
			return ERR_PTR(-EEXIST);

		if (flags & FOLL_TOUCH)
			touch_pud(vma, addr, pudp, flags & FOLL_WRITE);

		ctx->pgmap = get_dev_pagemap(pfn, ctx->pgmap);
		if (!ctx->pgmap)
			return ERR_PTR(-EFAULT);
	}

	page = pfn_to_page(pfn);

	if (!pud_devmap(pud) && !pud_write(pud) &&
	    gup_must_unshare(vma, flags, page))
		return ERR_PTR(-EMLINK);

	ret = try_grab_folio(page_folio(page), 1, flags);
	if (ret)
		page = ERR_PTR(ret);
	else
		ctx->page_mask = HPAGE_PUD_NR - 1;

	return page;
}

/* FOLL_FORCE can write to even unwritable PMDs in COW mappings. */
static inline bool can_follow_write_pmd(pmd_t pmd, struct page *page,
					struct vm_area_struct *vma,
					unsigned int flags)
{
	/* If the pmd is writable, we can write to the page. */
	if (pmd_write(pmd))
		return true;

	if (!can_follow_write_common(page, vma, flags))
		return false;

	/* ... and a write-fault isn't required for other reasons. */
	if (pmd_needs_soft_dirty_wp(vma, pmd))
		return false;
	return !userfaultfd_huge_pmd_wp(vma, pmd);
}

static struct page *follow_huge_pmd(struct vm_area_struct *vma,
				    unsigned long addr, pmd_t *pmd,
				    unsigned int flags,
				    struct follow_page_context *ctx)
{
	struct mm_struct *mm = vma->vm_mm;
	pmd_t pmdval = *pmd;
	struct page *page;
	int ret;

	assert_spin_locked(pmd_lockptr(mm, pmd));

	page = pmd_page(pmdval);
	if ((flags & FOLL_WRITE) &&
	    !can_follow_write_pmd(pmdval, page, vma, flags))
		return NULL;

	/* Avoid dumping huge zero page */
	if ((flags & FOLL_DUMP) && is_huge_zero_pmd(pmdval))
		return ERR_PTR(-EFAULT);

	if (pmd_protnone(*pmd) && !gup_can_follow_protnone(vma, flags))
		return NULL;

	if (!pmd_write(pmdval) && gup_must_unshare(vma, flags, page))
		return ERR_PTR(-EMLINK);

	VM_BUG_ON_PAGE((flags & FOLL_PIN) && PageAnon(page) &&
			!PageAnonExclusive(page), page);

	ret = try_grab_folio(page_folio(page), 1, flags);
	if (ret)
		return ERR_PTR(ret);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (pmd_trans_huge(pmdval) && (flags & FOLL_TOUCH))
		touch_pmd(vma, addr, pmd, flags & FOLL_WRITE);
#endif	/* CONFIG_TRANSPARENT_HUGEPAGE */

	page += (addr & ~HPAGE_PMD_MASK) >> PAGE_SHIFT;
	ctx->page_mask = HPAGE_PMD_NR - 1;

	return page;
}

#else  /* CONFIG_PGTABLE_HAS_HUGE_LEAVES */
static struct page *follow_huge_pud(struct vm_area_struct *vma,
				    unsigned long addr, pud_t *pudp,
				    int flags, struct follow_page_context *ctx)
{
	return NULL;
}

static struct page *follow_huge_pmd(struct vm_area_struct *vma,
				    unsigned long addr, pmd_t *pmd,
				    unsigned int flags,
				    struct follow_page_context *ctx)
{
	return NULL;
}
#endif	/* CONFIG_PGTABLE_HAS_HUGE_LEAVES */

static int follow_pfn_pte(struct vm_area_struct *vma, unsigned long address,
		pte_t *pte, unsigned int flags)
{
	if (flags & FOLL_TOUCH) {
		pte_t orig_entry = ptep_get(pte);
		pte_t entry = orig_entry;

		if (flags & FOLL_WRITE)
			entry = pte_mkdirty(entry);
		entry = pte_mkyoung(entry);

		if (!pte_same(orig_entry, entry)) {
			set_pte_at(vma->vm_mm, address, pte, entry);
			update_mmu_cache(vma, address, pte);
		}
	}

	/* Proper page table entry exists, but no corresponding struct page */
	return -EEXIST;
}

/* FOLL_FORCE can write to even unwritable PTEs in COW mappings. */
static inline bool can_follow_write_pte(pte_t pte, struct page *page,
					struct vm_area_struct *vma,
					unsigned int flags)
{
	/* If the pte is writable, we can write to the page. */
	if (pte_write(pte))
		return true;

	if (!can_follow_write_common(page, vma, flags))
		return false;

	/* ... and a write-fault isn't required for other reasons. */
	if (pte_needs_soft_dirty_wp(vma, pte))
		return false;
	return !userfaultfd_pte_wp(vma, pte);
}

static struct page *follow_page_pte(struct vm_area_struct *vma,
		unsigned long address, pmd_t *pmd, unsigned int flags,
		struct dev_pagemap **pgmap)
{
	struct mm_struct *mm = vma->vm_mm;
	struct folio *folio;
	struct page *page;
	spinlock_t *ptl;
	pte_t *ptep, pte;
	int ret;

	/* FOLL_GET and FOLL_PIN are mutually exclusive. */
	if (WARN_ON_ONCE((flags & (FOLL_PIN | FOLL_GET)) ==
			 (FOLL_PIN | FOLL_GET)))
		return ERR_PTR(-EINVAL);

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (!ptep)
		return no_page_table(vma, flags, address);
	pte = ptep_get(ptep);
	if (!pte_present(pte))
		goto no_page;
	if (pte_protnone(pte) && !gup_can_follow_protnone(vma, flags))
		goto no_page;

	page = vm_normal_page(vma, address, pte);

	/*
	 * We only care about anon pages in can_follow_write_pte() and don't
	 * have to worry about pte_devmap() because they are never anon.
	 */
	if ((flags & FOLL_WRITE) &&
	    !can_follow_write_pte(pte, page, vma, flags)) {
		page = NULL;
		goto out;
	}

	if (!page && pte_devmap(pte) && (flags & (FOLL_GET | FOLL_PIN))) {
		/*
		 * Only return device mapping pages in the FOLL_GET or FOLL_PIN
		 * case since they are only valid while holding the pgmap
		 * reference.
		 */
		*pgmap = get_dev_pagemap(pte_pfn(pte), *pgmap);
		if (*pgmap)
			page = pte_page(pte);
		else
			goto no_page;
	} else if (unlikely(!page)) {
		if (flags & FOLL_DUMP) {
			/* Avoid special (like zero) pages in core dumps */
			page = ERR_PTR(-EFAULT);
			goto out;
		}

		if (is_zero_pfn(pte_pfn(pte))) {
			page = pte_page(pte);
		} else {
			ret = follow_pfn_pte(vma, address, ptep, flags);
			page = ERR_PTR(ret);
			goto out;
		}
	}
	folio = page_folio(page);

	if (!pte_write(pte) && gup_must_unshare(vma, flags, page)) {
		page = ERR_PTR(-EMLINK);
		goto out;
	}

	VM_BUG_ON_PAGE((flags & FOLL_PIN) && PageAnon(page) &&
		       !PageAnonExclusive(page), page);

	/* try_grab_folio() does nothing unless FOLL_GET or FOLL_PIN is set. */
	ret = try_grab_folio(folio, 1, flags);
	if (unlikely(ret)) {
		page = ERR_PTR(ret);
		goto out;
	}

	/*
	 * We need to make the page accessible if and only if we are going
	 * to access its content (the FOLL_PIN case).  Please see
	 * Documentation/core-api/pin_user_pages.rst for details.
	 */
	if (flags & FOLL_PIN) {
		ret = arch_make_folio_accessible(folio);
		if (ret) {
			unpin_user_page(page);
			page = ERR_PTR(ret);
			goto out;
		}
	}
	if (flags & FOLL_TOUCH) {
		if ((flags & FOLL_WRITE) &&
		    !pte_dirty(pte) && !folio_test_dirty(folio))
			folio_mark_dirty(folio);
		/*
		 * pte_mkyoung() would be more correct here, but atomic care
		 * is needed to avoid losing the dirty bit: it is easier to use
		 * folio_mark_accessed().
		 */
		folio_mark_accessed(folio);
	}
out:
	pte_unmap_unlock(ptep, ptl);
	return page;
no_page:
	pte_unmap_unlock(ptep, ptl);
	if (!pte_none(pte))
		return NULL;
	return no_page_table(vma, flags, address);
}

static struct page *follow_pmd_mask(struct vm_area_struct *vma,
				    unsigned long address, pud_t *pudp,
				    unsigned int flags,
				    struct follow_page_context *ctx)
{
	pmd_t *pmd, pmdval;
	spinlock_t *ptl;
	struct page *page;
	struct mm_struct *mm = vma->vm_mm;

	pmd = pmd_offset(pudp, address);
	pmdval = pmdp_get_lockless(pmd);
	if (pmd_none(pmdval))
		return no_page_table(vma, flags, address);
	if (!pmd_present(pmdval))
		return no_page_table(vma, flags, address);
	if (pmd_devmap(pmdval)) {
		ptl = pmd_lock(mm, pmd);
		page = follow_devmap_pmd(vma, address, pmd, flags, &ctx->pgmap);
		spin_unlock(ptl);
		if (page)
			return page;
		return no_page_table(vma, flags, address);
	}
	if (likely(!pmd_leaf(pmdval)))
		return follow_page_pte(vma, address, pmd, flags, &ctx->pgmap);

	if (pmd_protnone(pmdval) && !gup_can_follow_protnone(vma, flags))
		return no_page_table(vma, flags, address);

	ptl = pmd_lock(mm, pmd);
	pmdval = *pmd;
	if (unlikely(!pmd_present(pmdval))) {
		spin_unlock(ptl);
		return no_page_table(vma, flags, address);
	}
	if (unlikely(!pmd_leaf(pmdval))) {
		spin_unlock(ptl);
		return follow_page_pte(vma, address, pmd, flags, &ctx->pgmap);
	}
	if (pmd_trans_huge(pmdval) && (flags & FOLL_SPLIT_PMD)) {
		spin_unlock(ptl);
		split_huge_pmd(vma, pmd, address);
		/* If pmd was left empty, stuff a page table in there quickly */
		return pte_alloc(mm, pmd) ? ERR_PTR(-ENOMEM) :
			follow_page_pte(vma, address, pmd, flags, &ctx->pgmap);
	}
	page = follow_huge_pmd(vma, address, pmd, flags, ctx);
	spin_unlock(ptl);
	return page;
}

static struct page *follow_pud_mask(struct vm_area_struct *vma,
				    unsigned long address, p4d_t *p4dp,
				    unsigned int flags,
				    struct follow_page_context *ctx)
{
	pud_t *pudp, pud;
	spinlock_t *ptl;
	struct page *page;
	struct mm_struct *mm = vma->vm_mm;

	pudp = pud_offset(p4dp, address);
	pud = READ_ONCE(*pudp);
	if (!pud_present(pud))
		return no_page_table(vma, flags, address);
	if (pud_leaf(pud)) {
		ptl = pud_lock(mm, pudp);
		page = follow_huge_pud(vma, address, pudp, flags, ctx);
		spin_unlock(ptl);
		if (page)
			return page;
		return no_page_table(vma, flags, address);
	}
	if (unlikely(pud_bad(pud)))
		return no_page_table(vma, flags, address);

	return follow_pmd_mask(vma, address, pudp, flags, ctx);
}

static struct page *follow_p4d_mask(struct vm_area_struct *vma,
				    unsigned long address, pgd_t *pgdp,
				    unsigned int flags,
				    struct follow_page_context *ctx)
{
	p4d_t *p4dp, p4d;

	p4dp = p4d_offset(pgdp, address);
	p4d = READ_ONCE(*p4dp);
	BUILD_BUG_ON(p4d_leaf(p4d));

	if (!p4d_present(p4d) || p4d_bad(p4d))
		return no_page_table(vma, flags, address);

	return follow_pud_mask(vma, address, p4dp, flags, ctx);
}

/**
 * follow_page_mask - look up a page descriptor from a user-virtual address
 * @vma: vm_area_struct mapping @address
 * @address: virtual address to look up
 * @flags: flags modifying lookup behaviour
 * @ctx: contains dev_pagemap for %ZONE_DEVICE memory pinning and a
 *       pointer to output page_mask
 *
 * @flags can have FOLL_ flags set, defined in <linux/mm.h>
 *
 * When getting pages from ZONE_DEVICE memory, the @ctx->pgmap caches
 * the device's dev_pagemap metadata to avoid repeating expensive lookups.
 *
 * When getting an anonymous page and the caller has to trigger unsharing
 * of a shared anonymous page first, -EMLINK is returned. The caller should
 * trigger a fault with FAULT_FLAG_UNSHARE set. Note that unsharing is only
 * relevant with FOLL_PIN and !FOLL_WRITE.
 *
 * On output, the @ctx->page_mask is set according to the size of the page.
 *
 * Return: the mapped (struct page *), %NULL if no mapping exists, or
 * an error pointer if there is a mapping to something not represented
 * by a page descriptor (see also vm_normal_page()).
 */
static struct page *follow_page_mask(struct vm_area_struct *vma,
			      unsigned long address, unsigned int flags,
			      struct follow_page_context *ctx)
{
	pgd_t *pgd;
	struct mm_struct *mm = vma->vm_mm;
	struct page *page;

	vma_pgtable_walk_begin(vma);

	ctx->page_mask = 0;
	pgd = pgd_offset(mm, address);

	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		page = no_page_table(vma, flags, address);
	else
		page = follow_p4d_mask(vma, address, pgd, flags, ctx);

	vma_pgtable_walk_end(vma);

	return page;
}

static int get_gate_page(struct mm_struct *mm, unsigned long address,
		unsigned int gup_flags, struct vm_area_struct **vma,
		struct page **page)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;
	int ret = -EFAULT;

	/* user gate pages are read-only */
	if (gup_flags & FOLL_WRITE)
		return -EFAULT;
	if (address > TASK_SIZE)
		pgd = pgd_offset_k(address);
	else
		pgd = pgd_offset_gate(mm, address);
	if (pgd_none(*pgd))
		return -EFAULT;
	p4d = p4d_offset(pgd, address);
	if (p4d_none(*p4d))
		return -EFAULT;
	pud = pud_offset(p4d, address);
	if (pud_none(*pud))
		return -EFAULT;
	pmd = pmd_offset(pud, address);
	if (!pmd_present(*pmd))
		return -EFAULT;
	pte = pte_offset_map(pmd, address);
	if (!pte)
		return -EFAULT;
	entry = ptep_get(pte);
	if (pte_none(entry))
		goto unmap;
	*vma = get_gate_vma(mm);
	if (!page)
		goto out;
	*page = vm_normal_page(*vma, address, entry);
	if (!*page) {
		if ((gup_flags & FOLL_DUMP) || !is_zero_pfn(pte_pfn(entry)))
			goto unmap;
		*page = pte_page(entry);
	}
	ret = try_grab_folio(page_folio(*page), 1, gup_flags);
	if (unlikely(ret))
		goto unmap;
out:
	ret = 0;
unmap:
	pte_unmap(pte);
	return ret;
}

/*
 * mmap_lock must be held on entry.  If @flags has FOLL_UNLOCKABLE but not
 * FOLL_NOWAIT, the mmap_lock may be released.  If it is, *@locked will be set
 * to 0 and -EBUSY returned.
 */
static int faultin_page(struct vm_area_struct *vma,
		unsigned long address, unsigned int flags, bool unshare,
		int *locked)
{
	unsigned int fault_flags = 0;
	vm_fault_t ret;

	if (flags & FOLL_NOFAULT)
		return -EFAULT;
	if (flags & FOLL_WRITE)
		fault_flags |= FAULT_FLAG_WRITE;
	if (flags & FOLL_REMOTE)
		fault_flags |= FAULT_FLAG_REMOTE;
	if (flags & FOLL_UNLOCKABLE) {
		fault_flags |= FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;
		/*
		 * FAULT_FLAG_INTERRUPTIBLE is opt-in. GUP callers must set
		 * FOLL_INTERRUPTIBLE to enable FAULT_FLAG_INTERRUPTIBLE.
		 * That's because some callers may not be prepared to
		 * handle early exits caused by non-fatal signals.
		 */
		if (flags & FOLL_INTERRUPTIBLE)
			fault_flags |= FAULT_FLAG_INTERRUPTIBLE;
	}
	if (flags & FOLL_NOWAIT)
		fault_flags |= FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_RETRY_NOWAIT;
	if (flags & FOLL_TRIED) {
		/*
		 * Note: FAULT_FLAG_ALLOW_RETRY and FAULT_FLAG_TRIED
		 * can co-exist
		 */
		fault_flags |= FAULT_FLAG_TRIED;
	}
	if (unshare) {
		fault_flags |= FAULT_FLAG_UNSHARE;
		/* FAULT_FLAG_WRITE and FAULT_FLAG_UNSHARE are incompatible */
		VM_BUG_ON(fault_flags & FAULT_FLAG_WRITE);
	}

	ret = handle_mm_fault(vma, address, fault_flags, NULL);

	if (ret & VM_FAULT_COMPLETED) {
		/*
		 * With FAULT_FLAG_RETRY_NOWAIT we'll never release the
		 * mmap lock in the page fault handler. Sanity check this.
		 */
		WARN_ON_ONCE(fault_flags & FAULT_FLAG_RETRY_NOWAIT);
		*locked = 0;

		/*
		 * We should do the same as VM_FAULT_RETRY, but let's not
		 * return -EBUSY since that's not reflecting the reality of
		 * what has happened - we've just fully completed a page
		 * fault, with the mmap lock released.  Use -EAGAIN to show
		 * that we want to take the mmap lock _again_.
		 */
		return -EAGAIN;
	}

	if (ret & VM_FAULT_ERROR) {
		int err = vm_fault_to_errno(ret, flags);

		if (err)
			return err;
		BUG();
	}

	if (ret & VM_FAULT_RETRY) {
		if (!(fault_flags & FAULT_FLAG_RETRY_NOWAIT))
			*locked = 0;
		return -EBUSY;
	}

	return 0;
}

/*
 * Writing to file-backed mappings which require folio dirty tracking using GUP
 * is a fundamentally broken operation, as kernel write access to GUP mappings
 * do not adhere to the semantics expected by a file system.
 *
 * Consider the following scenario:-
 *
 * 1. A folio is written to via GUP which write-faults the memory, notifying
 *    the file system and dirtying the folio.
 * 2. Later, writeback is triggered, resulting in the folio being cleaned and
 *    the PTE being marked read-only.
 * 3. The GUP caller writes to the folio, as it is mapped read/write via the
 *    direct mapping.
 * 4. The GUP caller, now done with the page, unpins it and sets it dirty
 *    (though it does not have to).
 *
 * This results in both data being written to a folio without writenotify, and
 * the folio being dirtied unexpectedly (if the caller decides to do so).
 */
static bool writable_file_mapping_allowed(struct vm_area_struct *vma,
					  unsigned long gup_flags)
{
	/*
	 * If we aren't pinning then no problematic write can occur. A long term
	 * pin is the most egregious case so this is the case we disallow.
	 */
	if ((gup_flags & (FOLL_PIN | FOLL_LONGTERM)) !=
	    (FOLL_PIN | FOLL_LONGTERM))
		return true;

	/*
	 * If the VMA does not require dirty tracking then no problematic write
	 * can occur either.
	 */
	return !vma_needs_dirty_tracking(vma);
}

static int check_vma_flags(struct vm_area_struct *vma, unsigned long gup_flags)
{
	vm_flags_t vm_flags = vma->vm_flags;
	int write = (gup_flags & FOLL_WRITE);
	int foreign = (gup_flags & FOLL_REMOTE);
	bool vma_anon = vma_is_anonymous(vma);

	if (vm_flags & (VM_IO | VM_PFNMAP))
		return -EFAULT;

	if ((gup_flags & FOLL_ANON) && !vma_anon)
		return -EFAULT;

	if ((gup_flags & FOLL_LONGTERM) && vma_is_fsdax(vma))
		return -EOPNOTSUPP;

	if ((gup_flags & FOLL_SPLIT_PMD) && is_vm_hugetlb_page(vma))
		return -EOPNOTSUPP;

	if (vma_is_secretmem(vma))
		return -EFAULT;

	if (write) {
		if (!vma_anon &&
		    !writable_file_mapping_allowed(vma, gup_flags))
			return -EFAULT;

		if (!(vm_flags & VM_WRITE) || (vm_flags & VM_SHADOW_STACK)) {
			if (!(gup_flags & FOLL_FORCE))
				return -EFAULT;
			/*
			 * We used to let the write,force case do COW in a
			 * VM_MAYWRITE VM_SHARED !VM_WRITE vma, so ptrace could
			 * set a breakpoint in a read-only mapping of an
			 * executable, without corrupting the file (yet only
			 * when that file had been opened for writing!).
			 * Anon pages in shared mappings are surprising: now
			 * just reject it.
			 */
			if (!is_cow_mapping(vm_flags))
				return -EFAULT;
		}
	} else if (!(vm_flags & VM_READ)) {
		if (!(gup_flags & FOLL_FORCE))
			return -EFAULT;
		/*
		 * Is there actually any vma we can reach here which does not
		 * have VM_MAYREAD set?
		 */
		if (!(vm_flags & VM_MAYREAD))
			return -EFAULT;
	}
	/*
	 * gups are always data accesses, not instruction
	 * fetches, so execute=false here
	 */
	if (!arch_vma_access_permitted(vma, write, false, foreign))
		return -EFAULT;
	return 0;
}

/*
 * This is "vma_lookup()", but with a warning if we would have
 * historically expanded the stack in the GUP code.
 */
static struct vm_area_struct *gup_vma_lookup(struct mm_struct *mm,
	 unsigned long addr)
{
#ifdef CONFIG_STACK_GROWSUP
	return vma_lookup(mm, addr);
#else
	static volatile unsigned long next_warn;
	struct vm_area_struct *vma;
	unsigned long now, next;

	vma = find_vma(mm, addr);
	if (!vma || (addr >= vma->vm_start))
		return vma;

	/* Only warn for half-way relevant accesses */
	if (!(vma->vm_flags & VM_GROWSDOWN))
		return NULL;
	if (vma->vm_start - addr > 65536)
		return NULL;

	/* Let's not warn more than once an hour.. */
	now = jiffies; next = next_warn;
	if (next && time_before(now, next))
		return NULL;
	next_warn = now + 60*60*HZ;

	/* Let people know things may have changed. */
	pr_warn("GUP no longer grows the stack in %s (%d): %lx-%lx (%lx)\n",
		current->comm, task_pid_nr(current),
		vma->vm_start, vma->vm_end, addr);
	dump_stack();
	return NULL;
#endif
}

/**
 * __get_user_pages() - pin user pages in memory
 * @mm:		mm_struct of target mm
 * @start:	starting user address
 * @nr_pages:	number of pages from start to pin
 * @gup_flags:	flags modifying pin behaviour
 * @pages:	array that receives pointers to the pages pinned.
 *		Should be at least nr_pages long. Or NULL, if caller
 *		only intends to ensure the pages are faulted in.
 * @locked:     whether we're still with the mmap_lock held
 *
 * Returns either number of pages pinned (which may be less than the
 * number requested), or an error. Details about the return value:
 *
 * -- If nr_pages is 0, returns 0.
 * -- If nr_pages is >0, but no pages were pinned, returns -errno.
 * -- If nr_pages is >0, and some pages were pinned, returns the number of
 *    pages pinned. Again, this may be less than nr_pages.
 * -- 0 return value is possible when the fault would need to be retried.
 *
 * The caller is responsible for releasing returned @pages, via put_page().
 *
 * Must be called with mmap_lock held.  It may be released.  See below.
 *
 * __get_user_pages walks a process's page tables and takes a reference to
 * each struct page that each user address corresponds to at a given
 * instant. That is, it takes the page that would be accessed if a user
 * thread accesses the given user virtual address at that instant.
 *
 * This does not guarantee that the page exists in the user mappings when
 * __get_user_pages returns, and there may even be a completely different
 * page there in some cases (eg. if mmapped pagecache has been invalidated
 * and subsequently re-faulted). However it does guarantee that the page
 * won't be freed completely. And mostly callers simply care that the page
 * contains data that was valid *at some point in time*. Typically, an IO
 * or similar operation cannot guarantee anything stronger anyway because
 * locks can't be held over the syscall boundary.
 *
 * If @gup_flags & FOLL_WRITE == 0, the page must not be written to. If
 * the page is written to, set_page_dirty (or set_page_dirty_lock, as
 * appropriate) must be called after the page is finished with, and
 * before put_page is called.
 *
 * If FOLL_UNLOCKABLE is set without FOLL_NOWAIT then the mmap_lock may
 * be released. If this happens *@locked will be set to 0 on return.
 *
 * A caller using such a combination of @gup_flags must therefore hold the
 * mmap_lock for reading only, and recognize when it's been released. Otherwise,
 * it must be held for either reading or writing and will not be released.
 *
 * In most cases, get_user_pages or get_user_pages_fast should be used
 * instead of __get_user_pages. __get_user_pages should be used only if
 * you need some special @gup_flags.
 */
static long __get_user_pages(struct mm_struct *mm,
		unsigned long start, unsigned long nr_pages,
		unsigned int gup_flags, struct page **pages,
		int *locked)
{
	long ret = 0, i = 0;
	struct vm_area_struct *vma = NULL;
	struct follow_page_context ctx = { NULL };

	if (!nr_pages)
		return 0;

	start = untagged_addr_remote(mm, start);

	VM_BUG_ON(!!pages != !!(gup_flags & (FOLL_GET | FOLL_PIN)));

	do {
		struct page *page;
		unsigned int page_increm;

		/* first iteration or cross vma bound */
		if (!vma || start >= vma->vm_end) {
			/*
			 * MADV_POPULATE_(READ|WRITE) wants to handle VMA
			 * lookups+error reporting differently.
			 */
			if (gup_flags & FOLL_MADV_POPULATE) {
				vma = vma_lookup(mm, start);
				if (!vma) {
					ret = -ENOMEM;
					goto out;
				}
				if (check_vma_flags(vma, gup_flags)) {
					ret = -EINVAL;
					goto out;
				}
				goto retry;
			}
			vma = gup_vma_lookup(mm, start);
			if (!vma && in_gate_area(mm, start)) {
				ret = get_gate_page(mm, start & PAGE_MASK,
						gup_flags, &vma,
						pages ? &page : NULL);
				if (ret)
					goto out;
				ctx.page_mask = 0;
				goto next_page;
			}

			if (!vma) {
				ret = -EFAULT;
				goto out;
			}
			ret = check_vma_flags(vma, gup_flags);
			if (ret)
				goto out;
		}
retry:
		/*
		 * If we have a pending SIGKILL, don't keep faulting pages and
		 * potentially allocating memory.
		 */
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}
		cond_resched();

		page = follow_page_mask(vma, start, gup_flags, &ctx);
		if (!page || PTR_ERR(page) == -EMLINK) {
			ret = faultin_page(vma, start, gup_flags,
					   PTR_ERR(page) == -EMLINK, locked);
			switch (ret) {
			case 0:
				goto retry;
			case -EBUSY:
			case -EAGAIN:
				ret = 0;
				fallthrough;
			case -EFAULT:
			case -ENOMEM:
			case -EHWPOISON:
				goto out;
			}
			BUG();
		} else if (PTR_ERR(page) == -EEXIST) {
			/*
			 * Proper page table entry exists, but no corresponding
			 * struct page. If the caller expects **pages to be
			 * filled in, bail out now, because that can't be done
			 * for this page.
			 */
			if (pages) {
				ret = PTR_ERR(page);
				goto out;
			}
		} else if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			goto out;
		}
next_page:
		page_increm = 1 + (~(start >> PAGE_SHIFT) & ctx.page_mask);
		if (page_increm > nr_pages)
			page_increm = nr_pages;

		if (pages) {
			struct page *subpage;
			unsigned int j;

			/*
			 * This must be a large folio (and doesn't need to
			 * be the whole folio; it can be part of it), do
			 * the refcount work for all the subpages too.
			 *
			 * NOTE: here the page may not be the head page
			 * e.g. when start addr is not thp-size aligned.
			 * try_grab_folio() should have taken care of tail
			 * pages.
			 */
			if (page_increm > 1) {
				struct folio *folio = page_folio(page);

				/*
				 * Since we already hold refcount on the
				 * large folio, this should never fail.
				 */
				if (try_grab_folio(folio, page_increm - 1,
						   gup_flags)) {
					/*
					 * Release the 1st page ref if the
					 * folio is problematic, fail hard.
					 */
					gup_put_folio(folio, 1, gup_flags);
					ret = -EFAULT;
					goto out;
				}
			}

			for (j = 0; j < page_increm; j++) {
				subpage = nth_page(page, j);
				pages[i + j] = subpage;
				flush_anon_page(vma, subpage, start + j * PAGE_SIZE);
				flush_dcache_page(subpage);
			}
		}

		i += page_increm;
		start += page_increm * PAGE_SIZE;
		nr_pages -= page_increm;
	} while (nr_pages);
out:
	if (ctx.pgmap)
		put_dev_pagemap(ctx.pgmap);
	return i ? i : ret;
}

static bool vma_permits_fault(struct vm_area_struct *vma,
			      unsigned int fault_flags)
{
	bool write   = !!(fault_flags & FAULT_FLAG_WRITE);
	bool foreign = !!(fault_flags & FAULT_FLAG_REMOTE);
	vm_flags_t vm_flags = write ? VM_WRITE : VM_READ;

	if (!(vm_flags & vma->vm_flags))
		return false;

	/*
	 * The architecture might have a hardware protection
	 * mechanism other than read/write that can deny access.
	 *
	 * gup always represents data access, not instruction
	 * fetches, so execute=false here:
	 */
	if (!arch_vma_access_permitted(vma, write, false, foreign))
		return false;

	return true;
}

/**
 * fixup_user_fault() - manually resolve a user page fault
 * @mm:		mm_struct of target mm
 * @address:	user address
 * @fault_flags:flags to pass down to handle_mm_fault()
 * @unlocked:	did we unlock the mmap_lock while retrying, maybe NULL if caller
 *		does not allow retry. If NULL, the caller must guarantee
 *		that fault_flags does not contain FAULT_FLAG_ALLOW_RETRY.
 *
 * This is meant to be called in the specific scenario where for locking reasons
 * we try to access user memory in atomic context (within a pagefault_disable()
 * section), this returns -EFAULT, and we want to resolve the user fault before
 * trying again.
 *
 * Typically this is meant to be used by the futex code.
 *
 * The main difference with get_user_pages() is that this function will
 * unconditionally call handle_mm_fault() which will in turn perform all the
 * necessary SW fixup of the dirty and young bits in the PTE, while
 * get_user_pages() only guarantees to update these in the struct page.
 *
 * This is important for some architectures where those bits also gate the
 * access permission to the page because they are maintained in software.  On
 * such architectures, gup() will not be enough to make a subsequent access
 * succeed.
 *
 * This function will not return with an unlocked mmap_lock. So it has not the
 * same semantics wrt the @mm->mmap_lock as does filemap_fault().
 */
int fixup_user_fault(struct mm_struct *mm,
		     unsigned long address, unsigned int fault_flags,
		     bool *unlocked)
{
	struct vm_area_struct *vma;
	vm_fault_t ret;

	address = untagged_addr_remote(mm, address);

	if (unlocked)
		fault_flags |= FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;

retry:
	vma = gup_vma_lookup(mm, address);
	if (!vma)
		return -EFAULT;

	if (!vma_permits_fault(vma, fault_flags))
		return -EFAULT;

	if ((fault_flags & FAULT_FLAG_KILLABLE) &&
	    fatal_signal_pending(current))
		return -EINTR;

	ret = handle_mm_fault(vma, address, fault_flags, NULL);

	if (ret & VM_FAULT_COMPLETED) {
		/*
		 * NOTE: it's a pity that we need to retake the lock here
		 * to pair with the unlock() in the callers. Ideally we
		 * could tell the callers so they do not need to unlock.
		 */
		mmap_read_lock(mm);
		*unlocked = true;
		return 0;
	}

	if (ret & VM_FAULT_ERROR) {
		int err = vm_fault_to_errno(ret, 0);

		if (err)
			return err;
		BUG();
	}

	if (ret & VM_FAULT_RETRY) {
		mmap_read_lock(mm);
		*unlocked = true;
		fault_flags |= FAULT_FLAG_TRIED;
		goto retry;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fixup_user_fault);

/*
 * GUP always responds to fatal signals.  When FOLL_INTERRUPTIBLE is
 * specified, it'll also respond to generic signals.  The caller of GUP
 * that has FOLL_INTERRUPTIBLE should take care of the GUP interruption.
 */
static bool gup_signal_pending(unsigned int flags)
{
	if (fatal_signal_pending(current))
		return true;

	if (!(flags & FOLL_INTERRUPTIBLE))
		return false;

	return signal_pending(current);
}

/*
 * Locking: (*locked == 1) means that the mmap_lock has already been acquired by
 * the caller. This function may drop the mmap_lock. If it does so, then it will
 * set (*locked = 0).
 *
 * (*locked == 0) means that the caller expects this function to acquire and
 * drop the mmap_lock. Therefore, the value of *locked will still be zero when
 * the function returns, even though it may have changed temporarily during
 * function execution.
 *
 * Please note that this function, unlike __get_user_pages(), will not return 0
 * for nr_pages > 0, unless FOLL_NOWAIT is used.
 */
static __always_inline long __get_user_pages_locked(struct mm_struct *mm,
						unsigned long start,
						unsigned long nr_pages,
						struct page **pages,
						int *locked,
						unsigned int flags)
{
	long ret, pages_done;
	bool must_unlock = false;

	if (!nr_pages)
		return 0;

	/*
	 * The internal caller expects GUP to manage the lock internally and the
	 * lock must be released when this returns.
	 */
	if (!*locked) {
		if (mmap_read_lock_killable(mm))
			return -EAGAIN;
		must_unlock = true;
		*locked = 1;
	}
	else
		mmap_assert_locked(mm);

	if (flags & FOLL_PIN)
		mm_set_has_pinned_flag(&mm->flags);

	/*
	 * FOLL_PIN and FOLL_GET are mutually exclusive. Traditional behavior
	 * is to set FOLL_GET if the caller wants pages[] filled in (but has
	 * carelessly failed to specify FOLL_GET), so keep doing that, but only
	 * for FOLL_GET, not for the newer FOLL_PIN.
	 *
	 * FOLL_PIN always expects pages to be non-null, but no need to assert
	 * that here, as any failures will be obvious enough.
	 */
	if (pages && !(flags & FOLL_PIN))
		flags |= FOLL_GET;

	pages_done = 0;
	for (;;) {
		ret = __get_user_pages(mm, start, nr_pages, flags, pages,
				       locked);
		if (!(flags & FOLL_UNLOCKABLE)) {
			/* VM_FAULT_RETRY couldn't trigger, bypass */
			pages_done = ret;
			break;
		}

		/* VM_FAULT_RETRY or VM_FAULT_COMPLETED cannot return errors */
		if (!*locked) {
			BUG_ON(ret < 0);
			BUG_ON(ret >= nr_pages);
		}

		if (ret > 0) {
			nr_pages -= ret;
			pages_done += ret;
			if (!nr_pages)
				break;
		}
		if (*locked) {
			/*
			 * VM_FAULT_RETRY didn't trigger or it was a
			 * FOLL_NOWAIT.
			 */
			if (!pages_done)
				pages_done = ret;
			break;
		}
		/*
		 * VM_FAULT_RETRY triggered, so seek to the faulting offset.
		 * For the prefault case (!pages) we only update counts.
		 */
		if (likely(pages))
			pages += ret;
		start += ret << PAGE_SHIFT;

		/* The lock was temporarily dropped, so we must unlock later */
		must_unlock = true;

retry:
		/*
		 * Repeat on the address that fired VM_FAULT_RETRY
		 * with both FAULT_FLAG_ALLOW_RETRY and
		 * FAULT_FLAG_TRIED.  Note that GUP can be interrupted
		 * by fatal signals of even common signals, depending on
		 * the caller's request. So we need to check it before we
		 * start trying again otherwise it can loop forever.
		 */
		if (gup_signal_pending(flags)) {
			if (!pages_done)
				pages_done = -EINTR;
			break;
		}

		ret = mmap_read_lock_killable(mm);
		if (ret) {
			BUG_ON(ret > 0);
			if (!pages_done)
				pages_done = ret;
			break;
		}

		*locked = 1;
		ret = __get_user_pages(mm, start, 1, flags | FOLL_TRIED,
				       pages, locked);
		if (!*locked) {
			/* Continue to retry until we succeeded */
			BUG_ON(ret != 0);
			goto retry;
		}
		if (ret != 1) {
			BUG_ON(ret > 1);
			if (!pages_done)
				pages_done = ret;
			break;
		}
		nr_pages--;
		pages_done++;
		if (!nr_pages)
			break;
		if (likely(pages))
			pages++;
		start += PAGE_SIZE;
	}
	if (must_unlock && *locked) {
		/*
		 * We either temporarily dropped the lock, or the caller
		 * requested that we both acquire and drop the lock. Either way,
		 * we must now unlock, and notify the caller of that state.
		 */
		mmap_read_unlock(mm);
		*locked = 0;
	}

	/*
	 * Failing to pin anything implies something has gone wrong (except when
	 * FOLL_NOWAIT is specified).
	 */
	if (WARN_ON_ONCE(pages_done == 0 && !(flags & FOLL_NOWAIT)))
		return -EFAULT;

	return pages_done;
}

/**
 * populate_vma_page_range() -  populate a range of pages in the vma.
 * @vma:   target vma
 * @start: start address
 * @end:   end address
 * @locked: whether the mmap_lock is still held
 *
 * This takes care of mlocking the pages too if VM_LOCKED is set.
 *
 * Return either number of pages pinned in the vma, or a negative error
 * code on error.
 *
 * vma->vm_mm->mmap_lock must be held.
 *
 * If @locked is NULL, it may be held for read or write and will
 * be unperturbed.
 *
 * If @locked is non-NULL, it must held for read only and may be
 * released.  If it's released, *@locked will be set to 0.
 */
long populate_vma_page_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end, int *locked)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long nr_pages = (end - start) / PAGE_SIZE;
	int local_locked = 1;
	int gup_flags;
	long ret;

	VM_BUG_ON(!PAGE_ALIGNED(start));
	VM_BUG_ON(!PAGE_ALIGNED(end));
	VM_BUG_ON_VMA(start < vma->vm_start, vma);
	VM_BUG_ON_VMA(end   > vma->vm_end, vma);
	mmap_assert_locked(mm);

	/*
	 * Rightly or wrongly, the VM_LOCKONFAULT case has never used
	 * faultin_page() to break COW, so it has no work to do here.
	 */
	if (vma->vm_flags & VM_LOCKONFAULT)
		return nr_pages;

	/* ... similarly, we've never faulted in PROT_NONE pages */
	if (!vma_is_accessible(vma))
		return -EFAULT;

	gup_flags = FOLL_TOUCH;
	/*
	 * We want to touch writable mappings with a write fault in order
	 * to break COW, except for shared mappings because these don't COW
	 * and we would not want to dirty them for nothing.
	 *
	 * Otherwise, do a read fault, and use FOLL_FORCE in case it's not
	 * readable (ie write-only or executable).
	 */
	if ((vma->vm_flags & (VM_WRITE | VM_SHARED)) == VM_WRITE)
		gup_flags |= FOLL_WRITE;
	else
		gup_flags |= FOLL_FORCE;

	if (locked)
		gup_flags |= FOLL_UNLOCKABLE;

	/*
	 * We made sure addr is within a VMA, so the following will
	 * not result in a stack expansion that recurses back here.
	 */
	ret = __get_user_pages(mm, start, nr_pages, gup_flags,
			       NULL, locked ? locked : &local_locked);
	lru_add_drain();
	return ret;
}

/*
 * faultin_page_range() - populate (prefault) page tables inside the
 *			  given range readable/writable
 *
 * This takes care of mlocking the pages, too, if VM_LOCKED is set.
 *
 * @mm: the mm to populate page tables in
 * @start: start address
 * @end: end address
 * @write: whether to prefault readable or writable
 * @locked: whether the mmap_lock is still held
 *
 * Returns either number of processed pages in the MM, or a negative error
 * code on error (see __get_user_pages()). Note that this function reports
 * errors related to VMAs, such as incompatible mappings, as expected by
 * MADV_POPULATE_(READ|WRITE).
 *
 * The range must be page-aligned.
 *
 * mm->mmap_lock must be held. If it's released, *@locked will be set to 0.
 */
long faultin_page_range(struct mm_struct *mm, unsigned long start,
			unsigned long end, bool write, int *locked)
{
	unsigned long nr_pages = (end - start) / PAGE_SIZE;
	int gup_flags;
	long ret;

	VM_BUG_ON(!PAGE_ALIGNED(start));
	VM_BUG_ON(!PAGE_ALIGNED(end));
	mmap_assert_locked(mm);

	/*
	 * FOLL_TOUCH: Mark page accessed and thereby young; will also mark
	 *	       the page dirty with FOLL_WRITE -- which doesn't make a
	 *	       difference with !FOLL_FORCE, because the page is writable
	 *	       in the page table.
	 * FOLL_HWPOISON: Return -EHWPOISON instead of -EFAULT when we hit
	 *		  a poisoned page.
	 * !FOLL_FORCE: Require proper access permissions.
	 */
	gup_flags = FOLL_TOUCH | FOLL_HWPOISON | FOLL_UNLOCKABLE |
		    FOLL_MADV_POPULATE;
	if (write)
		gup_flags |= FOLL_WRITE;

	ret = __get_user_pages_locked(mm, start, nr_pages, NULL, locked,
				      gup_flags);
	lru_add_drain();
	return ret;
}

/*
 * __mm_populate - populate and/or mlock pages within a range of address space.
 *
 * This is used to implement mlock() and the MAP_POPULATE / MAP_LOCKED mmap
 * flags. VMAs must be already marked with the desired vm_flags, and
 * mmap_lock must not be held.
 */
int __mm_populate(unsigned long start, unsigned long len, int ignore_errors)
{
	struct mm_struct *mm = current->mm;
	unsigned long end, nstart, nend;
	struct vm_area_struct *vma = NULL;
	int locked = 0;
	long ret = 0;

	end = start + len;

	for (nstart = start; nstart < end; nstart = nend) {
		/*
		 * We want to fault in pages for [nstart; end) address range.
		 * Find first corresponding VMA.
		 */
		if (!locked) {
			locked = 1;
			mmap_read_lock(mm);
			vma = find_vma_intersection(mm, nstart, end);
		} else if (nstart >= vma->vm_end)
			vma = find_vma_intersection(mm, vma->vm_end, end);

		if (!vma)
			break;
		/*
		 * Set [nstart; nend) to intersection of desired address
		 * range with the first VMA. Also, skip undesirable VMA types.
		 */
		nend = min(end, vma->vm_end);
		if (vma->vm_flags & (VM_IO | VM_PFNMAP))
			continue;
		if (nstart < vma->vm_start)
			nstart = vma->vm_start;
		/*
		 * Now fault in a range of pages. populate_vma_page_range()
		 * double checks the vma flags, so that it won't mlock pages
		 * if the vma was already munlocked.
		 */
		ret = populate_vma_page_range(vma, nstart, nend, &locked);
		if (ret < 0) {
			if (ignore_errors) {
				ret = 0;
				continue;	/* continue at next VMA */
			}
			break;
		}
		nend = nstart + ret * PAGE_SIZE;
		ret = 0;
	}
	if (locked)
		mmap_read_unlock(mm);
	return ret;	/* 0 or negative error code */
}
#else /* CONFIG_MMU */
static long __get_user_pages_locked(struct mm_struct *mm, unsigned long start,
		unsigned long nr_pages, struct page **pages,
		int *locked, unsigned int foll_flags)
{
	struct vm_area_struct *vma;
	bool must_unlock = false;
	unsigned long vm_flags;
	long i;

	if (!nr_pages)
		return 0;

	/*
	 * The internal caller expects GUP to manage the lock internally and the
	 * lock must be released when this returns.
	 */
	if (!*locked) {
		if (mmap_read_lock_killable(mm))
			return -EAGAIN;
		must_unlock = true;
		*locked = 1;
	}

	/* calculate required read or write permissions.
	 * If FOLL_FORCE is set, we only require the "MAY" flags.
	 */
	vm_flags  = (foll_flags & FOLL_WRITE) ?
			(VM_WRITE | VM_MAYWRITE) : (VM_READ | VM_MAYREAD);
	vm_flags &= (foll_flags & FOLL_FORCE) ?
			(VM_MAYREAD | VM_MAYWRITE) : (VM_READ | VM_WRITE);

	for (i = 0; i < nr_pages; i++) {
		vma = find_vma(mm, start);
		if (!vma)
			break;

		/* protect what we can, including chardevs */
		if ((vma->vm_flags & (VM_IO | VM_PFNMAP)) ||
		    !(vm_flags & vma->vm_flags))
			break;

		if (pages) {
			pages[i] = virt_to_page((void *)start);
			if (pages[i])
				get_page(pages[i]);
		}

		start = (start + PAGE_SIZE) & PAGE_MASK;
	}

	if (must_unlock && *locked) {
		mmap_read_unlock(mm);
		*locked = 0;
	}

	return i ? : -EFAULT;
}
#endif /* !CONFIG_MMU */

/**
 * fault_in_writeable - fault in userspace address range for writing
 * @uaddr: start of address range
 * @size: size of address range
 *
 * Returns the number of bytes not faulted in (like copy_to_user() and
 * copy_from_user()).
 */
size_t fault_in_writeable(char __user *uaddr, size_t size)
{
	char __user *start = uaddr, *end;

	if (unlikely(size == 0))
		return 0;
	if (!user_write_access_begin(uaddr, size))
		return size;
	if (!PAGE_ALIGNED(uaddr)) {
		unsafe_put_user(0, uaddr, out);
		uaddr = (char __user *)PAGE_ALIGN((unsigned long)uaddr);
	}
	end = (char __user *)PAGE_ALIGN((unsigned long)start + size);
	if (unlikely(end < start))
		end = NULL;
	while (uaddr != end) {
		unsafe_put_user(0, uaddr, out);
		uaddr += PAGE_SIZE;
	}

out:
	user_write_access_end();
	if (size > uaddr - start)
		return size - (uaddr - start);
	return 0;
}
EXPORT_SYMBOL(fault_in_writeable);

/**
 * fault_in_subpage_writeable - fault in an address range for writing
 * @uaddr: start of address range
 * @size: size of address range
 *
 * Fault in a user address range for writing while checking for permissions at
 * sub-page granularity (e.g. arm64 MTE). This function should be used when
 * the caller cannot guarantee forward progress of a copy_to_user() loop.
 *
 * Returns the number of bytes not faulted in (like copy_to_user() and
 * copy_from_user()).
 */
size_t fault_in_subpage_writeable(char __user *uaddr, size_t size)
{
	size_t faulted_in;

	/*
	 * Attempt faulting in at page granularity first for page table
	 * permission checking. The arch-specific probe_subpage_writeable()
	 * functions may not check for this.
	 */
	faulted_in = size - fault_in_writeable(uaddr, size);
	if (faulted_in)
		faulted_in -= probe_subpage_writeable(uaddr, faulted_in);

	return size - faulted_in;
}
EXPORT_SYMBOL(fault_in_subpage_writeable);

/*
 * fault_in_safe_writeable - fault in an address range for writing
 * @uaddr: start of address range
 * @size: length of address range
 *
 * Faults in an address range for writing.  This is primarily useful when we
 * already know that some or all of the pages in the address range aren't in
 * memory.
 *
 * Unlike fault_in_writeable(), this function is non-destructive.
 *
 * Note that we don't pin or otherwise hold the pages referenced that we fault
 * in.  There's no guarantee that they'll stay in memory for any duration of
 * time.
 *
 * Returns the number of bytes not faulted in, like copy_to_user() and
 * copy_from_user().
 */
size_t fault_in_safe_writeable(const char __user *uaddr, size_t size)
{
	unsigned long start = (unsigned long)uaddr, end;
	struct mm_struct *mm = current->mm;
	bool unlocked = false;

	if (unlikely(size == 0))
		return 0;
	end = PAGE_ALIGN(start + size);
	if (end < start)
		end = 0;

	mmap_read_lock(mm);
	do {
		if (fixup_user_fault(mm, start, FAULT_FLAG_WRITE, &unlocked))
			break;
		start = (start + PAGE_SIZE) & PAGE_MASK;
	} while (start != end);
	mmap_read_unlock(mm);

	if (size > (unsigned long)uaddr - start)
		return size - ((unsigned long)uaddr - start);
	return 0;
}
EXPORT_SYMBOL(fault_in_safe_writeable);

/**
 * fault_in_readable - fault in userspace address range for reading
 * @uaddr: start of user address range
 * @size: size of user address range
 *
 * Returns the number of bytes not faulted in (like copy_to_user() and
 * copy_from_user()).
 */
size_t fault_in_readable(const char __user *uaddr, size_t size)
{
	const char __user *start = uaddr, *end;
	volatile char c;

	if (unlikely(size == 0))
		return 0;
	if (!user_read_access_begin(uaddr, size))
		return size;
	if (!PAGE_ALIGNED(uaddr)) {
		unsafe_get_user(c, uaddr, out);
		uaddr = (const char __user *)PAGE_ALIGN((unsigned long)uaddr);
	}
	end = (const char __user *)PAGE_ALIGN((unsigned long)start + size);
	if (unlikely(end < start))
		end = NULL;
	while (uaddr != end) {
		unsafe_get_user(c, uaddr, out);
		uaddr += PAGE_SIZE;
	}

out:
	user_read_access_end();
	(void)c;
	if (size > uaddr - start)
		return size - (uaddr - start);
	return 0;
}
EXPORT_SYMBOL(fault_in_readable);

/**
 * get_dump_page() - pin user page in memory while writing it to core dump
 * @addr: user address
 *
 * Returns struct page pointer of user page pinned for dump,
 * to be freed afterwards by put_page().
 *
 * Returns NULL on any kind of failure - a hole must then be inserted into
 * the corefile, to preserve alignment with its headers; and also returns
 * NULL wherever the ZERO_PAGE, or an anonymous pte_none, has been found -
 * allowing a hole to be left in the corefile to save disk space.
 *
 * Called without mmap_lock (takes and releases the mmap_lock by itself).
 */
#ifdef CONFIG_ELF_CORE
struct page *get_dump_page(unsigned long addr)
{
	struct page *page;
	int locked = 0;
	int ret;

	ret = __get_user_pages_locked(current->mm, addr, 1, &page, &locked,
				      FOLL_FORCE | FOLL_DUMP | FOLL_GET);
	return (ret == 1) ? page : NULL;
}
#endif /* CONFIG_ELF_CORE */

#ifdef CONFIG_MIGRATION

/*
 * An array of either pages or folios ("pofs"). Although it may seem tempting to
 * avoid this complication, by simply interpreting a list of folios as a list of
 * pages, that approach won't work in the longer term, because eventually the
 * layouts of struct page and struct folio will become completely different.
 * Furthermore, this pof approach avoids excessive page_folio() calls.
 */
struct pages_or_folios {
	union {
		struct page **pages;
		struct folio **folios;
		void **entries;
	};
	bool has_folios;
	long nr_entries;
};

static struct folio *pofs_get_folio(struct pages_or_folios *pofs, long i)
{
	if (pofs->has_folios)
		return pofs->folios[i];
	return page_folio(pofs->pages[i]);
}

static void pofs_clear_entry(struct pages_or_folios *pofs, long i)
{
	pofs->entries[i] = NULL;
}

static void pofs_unpin(struct pages_or_folios *pofs)
{
	if (pofs->has_folios)
		unpin_folios(pofs->folios, pofs->nr_entries);
	else
		unpin_user_pages(pofs->pages, pofs->nr_entries);
}

/*
 * Returns the number of collected folios. Return value is always >= 0.
 */
static void collect_longterm_unpinnable_folios(
		struct list_head *movable_folio_list,
		struct pages_or_folios *pofs)
{
	struct folio *prev_folio = NULL;
	bool drain_allow = true;
	unsigned long i;

	for (i = 0; i < pofs->nr_entries; i++) {
		struct folio *folio = pofs_get_folio(pofs, i);

		if (folio == prev_folio)
			continue;
		prev_folio = folio;

		if (folio_is_longterm_pinnable(folio))
			continue;

		if (folio_is_device_coherent(folio))
			continue;

		if (folio_test_hugetlb(folio)) {
			folio_isolate_hugetlb(folio, movable_folio_list);
			continue;
		}

		if (!folio_test_lru(folio) && drain_allow) {
			lru_add_drain_all();
			drain_allow = false;
		}

		if (!folio_isolate_lru(folio))
			continue;

		list_add_tail(&folio->lru, movable_folio_list);
		node_stat_mod_folio(folio,
				    NR_ISOLATED_ANON + folio_is_file_lru(folio),
				    folio_nr_pages(folio));
	}
}

/*
 * Unpins all folios and migrates device coherent folios and movable_folio_list.
 * Returns -EAGAIN if all folios were successfully migrated or -errno for
 * failure (or partial success).
 */
static int
migrate_longterm_unpinnable_folios(struct list_head *movable_folio_list,
				   struct pages_or_folios *pofs)
{
	int ret;
	unsigned long i;

	for (i = 0; i < pofs->nr_entries; i++) {
		struct folio *folio = pofs_get_folio(pofs, i);

		if (folio_is_device_coherent(folio)) {
			/*
			 * Migration will fail if the folio is pinned, so
			 * convert the pin on the source folio to a normal
			 * reference.
			 */
			pofs_clear_entry(pofs, i);
			folio_get(folio);
			gup_put_folio(folio, 1, FOLL_PIN);

			if (migrate_device_coherent_folio(folio)) {
				ret = -EBUSY;
				goto err;
			}

			continue;
		}

		/*
		 * We can't migrate folios with unexpected references, so drop
		 * the reference obtained by __get_user_pages_locked().
		 * Migrating folios have been added to movable_folio_list after
		 * calling folio_isolate_lru() which takes a reference so the
		 * folio won't be freed if it's migrating.
		 */
		unpin_folio(folio);
		pofs_clear_entry(pofs, i);
	}

	if (!list_empty(movable_folio_list)) {
		struct migration_target_control mtc = {
			.nid = NUMA_NO_NODE,
			.gfp_mask = GFP_USER | __GFP_NOWARN,
			.reason = MR_LONGTERM_PIN,
		};

		if (migrate_pages(movable_folio_list, alloc_migration_target,
				  NULL, (unsigned long)&mtc, MIGRATE_SYNC,
				  MR_LONGTERM_PIN, NULL)) {
			ret = -ENOMEM;
			goto err;
		}
	}

	putback_movable_pages(movable_folio_list);

	return -EAGAIN;

err:
	pofs_unpin(pofs);
	putback_movable_pages(movable_folio_list);

	return ret;
}

static long
check_and_migrate_movable_pages_or_folios(struct pages_or_folios *pofs)
{
	LIST_HEAD(movable_folio_list);

	collect_longterm_unpinnable_folios(&movable_folio_list, pofs);
	if (list_empty(&movable_folio_list))
		return 0;

	return migrate_longterm_unpinnable_folios(&movable_folio_list, pofs);
}

/*
 * Check whether all folios are *allowed* to be pinned indefinitely (long term).
 * Rather confusingly, all folios in the range are required to be pinned via
 * FOLL_PIN, before calling this routine.
 *
 * Return values:
 *
 * 0: if everything is OK and all folios in the range are allowed to be pinned,
 * then this routine leaves all folios pinned and returns zero for success.
 *
 * -EAGAIN: if any folios in the range are not allowed to be pinned, then this
 * routine will migrate those folios away, unpin all the folios in the range. If
 * migration of the entire set of folios succeeds, then -EAGAIN is returned. The
 * caller should re-pin the entire range with FOLL_PIN and then call this
 * routine again.
 *
 * -ENOMEM, or any other -errno: if an error *other* than -EAGAIN occurs, this
 * indicates a migration failure. The caller should give up, and propagate the
 * error back up the call stack. The caller does not need to unpin any folios in
 * that case, because this routine will do the unpinning.
 */
static long check_and_migrate_movable_folios(unsigned long nr_folios,
					     struct folio **folios)
{
	struct pages_or_folios pofs = {
		.folios = folios,
		.has_folios = true,
		.nr_entries = nr_folios,
	};

	return check_and_migrate_movable_pages_or_folios(&pofs);
}

/*
 * Return values and behavior are the same as those for
 * check_and_migrate_movable_folios().
 */
static long check_and_migrate_movable_pages(unsigned long nr_pages,
					    struct page **pages)
{
	struct pages_or_folios pofs = {
		.pages = pages,
		.has_folios = false,
		.nr_entries = nr_pages,
	};

	return check_and_migrate_movable_pages_or_folios(&pofs);
}
#else
static long check_and_migrate_movable_pages(unsigned long nr_pages,
					    struct page **pages)
{
	return 0;
}

static long check_and_migrate_movable_folios(unsigned long nr_folios,
					     struct folio **folios)
{
	return 0;
}
#endif /* CONFIG_MIGRATION */

/*
 * __gup_longterm_locked() is a wrapper for __get_user_pages_locked which
 * allows us to process the FOLL_LONGTERM flag.
 */
static long __gup_longterm_locked(struct mm_struct *mm,
				  unsigned long start,
				  unsigned long nr_pages,
				  struct page **pages,
				  int *locked,
				  unsigned int gup_flags)
{
	unsigned int flags;
	long rc, nr_pinned_pages;

	if (!(gup_flags & FOLL_LONGTERM))
		return __get_user_pages_locked(mm, start, nr_pages, pages,
					       locked, gup_flags);

	flags = memalloc_pin_save();
	do {
		nr_pinned_pages = __get_user_pages_locked(mm, start, nr_pages,
							  pages, locked,
							  gup_flags);
		if (nr_pinned_pages <= 0) {
			rc = nr_pinned_pages;
			break;
		}

		/* FOLL_LONGTERM implies FOLL_PIN */
		rc = check_and_migrate_movable_pages(nr_pinned_pages, pages);
	} while (rc == -EAGAIN);
	memalloc_pin_restore(flags);
	return rc ? rc : nr_pinned_pages;
}

/*
 * Check that the given flags are valid for the exported gup/pup interface, and
 * update them with the required flags that the caller must have set.
 */
static bool is_valid_gup_args(struct page **pages, int *locked,
			      unsigned int *gup_flags_p, unsigned int to_set)
{
	unsigned int gup_flags = *gup_flags_p;

	/*
	 * These flags not allowed to be specified externally to the gup
	 * interfaces:
	 * - FOLL_TOUCH/FOLL_PIN/FOLL_TRIED/FOLL_FAST_ONLY are internal only
	 * - FOLL_REMOTE is internal only, set in (get|pin)_user_pages_remote()
	 * - FOLL_UNLOCKABLE is internal only and used if locked is !NULL
	 */
	if (WARN_ON_ONCE(gup_flags & INTERNAL_GUP_FLAGS))
		return false;

	gup_flags |= to_set;
	if (locked) {
		/* At the external interface locked must be set */
		if (WARN_ON_ONCE(*locked != 1))
			return false;

		gup_flags |= FOLL_UNLOCKABLE;
	}

	/* FOLL_GET and FOLL_PIN are mutually exclusive. */
	if (WARN_ON_ONCE((gup_flags & (FOLL_PIN | FOLL_GET)) ==
			 (FOLL_PIN | FOLL_GET)))
		return false;

	/* LONGTERM can only be specified when pinning */
	if (WARN_ON_ONCE(!(gup_flags & FOLL_PIN) && (gup_flags & FOLL_LONGTERM)))
		return false;

	/* Pages input must be given if using GET/PIN */
	if (WARN_ON_ONCE((gup_flags & (FOLL_GET | FOLL_PIN)) && !pages))
		return false;

	/* We want to allow the pgmap to be hot-unplugged at all times */
	if (WARN_ON_ONCE((gup_flags & FOLL_LONGTERM) &&
			 (gup_flags & FOLL_PCI_P2PDMA)))
		return false;

	*gup_flags_p = gup_flags;
	return true;
}

#ifdef CONFIG_MMU
/**
 * get_user_pages_remote() - pin user pages in memory
 * @mm:		mm_struct of target mm
 * @start:	starting user address
 * @nr_pages:	number of pages from start to pin
 * @gup_flags:	flags modifying lookup behaviour
 * @pages:	array that receives pointers to the pages pinned.
 *		Should be at least nr_pages long. Or NULL, if caller
 *		only intends to ensure the pages are faulted in.
 * @locked:	pointer to lock flag indicating whether lock is held and
 *		subsequently whether VM_FAULT_RETRY functionality can be
 *		utilised. Lock must initially be held.
 *
 * Returns either number of pages pinned (which may be less than the
 * number requested), or an error. Details about the return value:
 *
 * -- If nr_pages is 0, returns 0.
 * -- If nr_pages is >0, but no pages were pinned, returns -errno.
 * -- If nr_pages is >0, and some pages were pinned, returns the number of
 *    pages pinned. Again, this may be less than nr_pages.
 *
 * The caller is responsible for releasing returned @pages, via put_page().
 *
 * Must be called with mmap_lock held for read or write.
 *
 * get_user_pages_remote walks a process's page tables and takes a reference
 * to each struct page that each user address corresponds to at a given
 * instant. That is, it takes the page that would be accessed if a user
 * thread accesses the given user virtual address at that instant.
 *
 * This does not guarantee that the page exists in the user mappings when
 * get_user_pages_remote returns, and there may even be a completely different
 * page there in some cases (eg. if mmapped pagecache has been invalidated
 * and subsequently re-faulted). However it does guarantee that the page
 * won't be freed completely. And mostly callers simply care that the page
 * contains data that was valid *at some point in time*. Typically, an IO
 * or similar operation cannot guarantee anything stronger anyway because
 * locks can't be held over the syscall boundary.
 *
 * If gup_flags & FOLL_WRITE == 0, the page must not be written to. If the page
 * is written to, set_page_dirty (or set_page_dirty_lock, as appropriate) must
 * be called after the page is finished with, and before put_page is called.
 *
 * get_user_pages_remote is typically used for fewer-copy IO operations,
 * to get a handle on the memory by some means other than accesses
 * via the user virtual addresses. The pages may be submitted for
 * DMA to devices or accessed via their kernel linear mapping (via the
 * kmap APIs). Care should be taken to use the correct cache flushing APIs.
 *
 * See also get_user_pages_fast, for performance critical applications.
 *
 * get_user_pages_remote should be phased out in favor of
 * get_user_pages_locked|unlocked or get_user_pages_fast. Nothing
 * should use get_user_pages_remote because it cannot pass
 * FAULT_FLAG_ALLOW_RETRY to handle_mm_fault.
 */
long get_user_pages_remote(struct mm_struct *mm,
		unsigned long start, unsigned long nr_pages,
		unsigned int gup_flags, struct page **pages,
		int *locked)
{
	int local_locked = 1;

	if (!is_valid_gup_args(pages, locked, &gup_flags,
			       FOLL_TOUCH | FOLL_REMOTE))
		return -EINVAL;

	return __get_user_pages_locked(mm, start, nr_pages, pages,
				       locked ? locked : &local_locked,
				       gup_flags);
}
EXPORT_SYMBOL(get_user_pages_remote);

#else /* CONFIG_MMU */
long get_user_pages_remote(struct mm_struct *mm,
			   unsigned long start, unsigned long nr_pages,
			   unsigned int gup_flags, struct page **pages,
			   int *locked)
{
	return 0;
}
#endif /* !CONFIG_MMU */

/**
 * get_user_pages() - pin user pages in memory
 * @start:      starting user address
 * @nr_pages:   number of pages from start to pin
 * @gup_flags:  flags modifying lookup behaviour
 * @pages:      array that receives pointers to the pages pinned.
 *              Should be at least nr_pages long. Or NULL, if caller
 *              only intends to ensure the pages are faulted in.
 *
 * This is the same as get_user_pages_remote(), just with a less-flexible
 * calling convention where we assume that the mm being operated on belongs to
 * the current task, and doesn't allow passing of a locked parameter.  We also
 * obviously don't pass FOLL_REMOTE in here.
 */
long get_user_pages(unsigned long start, unsigned long nr_pages,
		    unsigned int gup_flags, struct page **pages)
{
	int locked = 1;

	if (!is_valid_gup_args(pages, NULL, &gup_flags, FOLL_TOUCH))
		return -EINVAL;

	return __get_user_pages_locked(current->mm, start, nr_pages, pages,
				       &locked, gup_flags);
}
EXPORT_SYMBOL(get_user_pages);

/*
 * get_user_pages_unlocked() is suitable to replace the form:
 *
 *      mmap_read_lock(mm);
 *      get_user_pages(mm, ..., pages, NULL);
 *      mmap_read_unlock(mm);
 *
 *  with:
 *
 *      get_user_pages_unlocked(mm, ..., pages);
 *
 * It is functionally equivalent to get_user_pages_fast so
 * get_user_pages_fast should be used instead if specific gup_flags
 * (e.g. FOLL_FORCE) are not required.
 */
long get_user_pages_unlocked(unsigned long start, unsigned long nr_pages,
			     struct page **pages, unsigned int gup_flags)
{
	int locked = 0;

	if (!is_valid_gup_args(pages, NULL, &gup_flags,
			       FOLL_TOUCH | FOLL_UNLOCKABLE))
		return -EINVAL;

	return __get_user_pages_locked(current->mm, start, nr_pages, pages,
				       &locked, gup_flags);
}
EXPORT_SYMBOL(get_user_pages_unlocked);

/*
 * GUP-fast
 *
 * get_user_pages_fast attempts to pin user pages by walking the page
 * tables directly and avoids taking locks. Thus the walker needs to be
 * protected from page table pages being freed from under it, and should
 * block any THP splits.
 *
 * One way to achieve this is to have the walker disable interrupts, and
 * rely on IPIs from the TLB flushing code blocking before the page table
 * pages are freed. This is unsuitable for architectures that do not need
 * to broadcast an IPI when invalidating TLBs.
 *
 * Another way to achieve this is to batch up page table containing pages
 * belonging to more than one mm_user, then rcu_sched a callback to free those
 * pages. Disabling interrupts will allow the gup_fast() walker to both block
 * the rcu_sched callback, and an IPI that we broadcast for splitting THPs
 * (which is a relatively rare event). The code below adopts this strategy.
 *
 * Before activating this code, please be aware that the following assumptions
 * are currently made:
 *
 *  *) Either MMU_GATHER_RCU_TABLE_FREE is enabled, and tlb_remove_table() is used to
 *  free pages containing page tables or TLB flushing requires IPI broadcast.
 *
 *  *) ptes can be read atomically by the architecture.
 *
 *  *) valid user addesses are below TASK_MAX_SIZE
 *
 * The last two assumptions can be relaxed by the addition of helper functions.
 *
 * This code is based heavily on the PowerPC implementation by Nick Piggin.
 */
#ifdef CONFIG_HAVE_GUP_FAST
/*
 * Used in the GUP-fast path to determine whether GUP is permitted to work on
 * a specific folio.
 *
 * This call assumes the caller has pinned the folio, that the lowest page table
 * level still points to this folio, and that interrupts have been disabled.
 *
 * GUP-fast must reject all secretmem folios.
 *
 * Writing to pinned file-backed dirty tracked folios is inherently problematic
 * (see comment describing the writable_file_mapping_allowed() function). We
 * therefore try to avoid the most egregious case of a long-term mapping doing
 * so.
 *
 * This function cannot be as thorough as that one as the VMA is not available
 * in the fast path, so instead we whitelist known good cases and if in doubt,
 * fall back to the slow path.
 */
static bool gup_fast_folio_allowed(struct folio *folio, unsigned int flags)
{
	bool reject_file_backed = false;
	struct address_space *mapping;
	bool check_secretmem = false;
	unsigned long mapping_flags;

	/*
	 * If we aren't pinning then no problematic write can occur. A long term
	 * pin is the most egregious case so this is the one we disallow.
	 */
	if ((flags & (FOLL_PIN | FOLL_LONGTERM | FOLL_WRITE)) ==
	    (FOLL_PIN | FOLL_LONGTERM | FOLL_WRITE))
		reject_file_backed = true;

	/* We hold a folio reference, so we can safely access folio fields. */

	/* secretmem folios are always order-0 folios. */
	if (IS_ENABLED(CONFIG_SECRETMEM) && !folio_test_large(folio))
		check_secretmem = true;

	if (!reject_file_backed && !check_secretmem)
		return true;

	if (WARN_ON_ONCE(folio_test_slab(folio)))
		return false;

	/* hugetlb neither requires dirty-tracking nor can be secretmem. */
	if (folio_test_hugetlb(folio))
		return true;

	/*
	 * GUP-fast disables IRQs. When IRQS are disabled, RCU grace periods
	 * cannot proceed, which means no actions performed under RCU can
	 * proceed either.
	 *
	 * inodes and thus their mappings are freed under RCU, which means the
	 * mapping cannot be freed beneath us and thus we can safely dereference
	 * it.
	 */
	lockdep_assert_irqs_disabled();

	/*
	 * However, there may be operations which _alter_ the mapping, so ensure
	 * we read it once and only once.
	 */
	mapping = READ_ONCE(folio->mapping);

	/*
	 * The mapping may have been truncated, in any case we cannot determine
	 * if this mapping is safe - fall back to slow path to determine how to
	 * proceed.
	 */
	if (!mapping)
		return false;

	/* Anonymous folios pose no problem. */
	mapping_flags = (unsigned long)mapping & PAGE_MAPPING_FLAGS;
	if (mapping_flags)
		return mapping_flags & PAGE_MAPPING_ANON;

	/*
	 * At this point, we know the mapping is non-null and points to an
	 * address_space object.
	 */
	if (check_secretmem && secretmem_mapping(mapping))
		return false;
	/* The only remaining allowed file system is shmem. */
	return !reject_file_backed || shmem_mapping(mapping);
}

static void __maybe_unused gup_fast_undo_dev_pagemap(int *nr, int nr_start,
		unsigned int flags, struct page **pages)
{
	while ((*nr) - nr_start) {
		struct folio *folio = page_folio(pages[--(*nr)]);

		folio_clear_referenced(folio);
		gup_put_folio(folio, 1, flags);
	}
}

#ifdef CONFIG_ARCH_HAS_PTE_SPECIAL
/*
 * GUP-fast relies on pte change detection to avoid concurrent pgtable
 * operations.
 *
 * To pin the page, GUP-fast needs to do below in order:
 * (1) pin the page (by prefetching pte), then (2) check pte not changed.
 *
 * For the rest of pgtable operations where pgtable updates can be racy
 * with GUP-fast, we need to do (1) clear pte, then (2) check whether page
 * is pinned.
 *
 * Above will work for all pte-level operations, including THP split.
 *
 * For THP collapse, it's a bit more complicated because GUP-fast may be
 * walking a pgtable page that is being freed (pte is still valid but pmd
 * can be cleared already).  To avoid race in such condition, we need to
 * also check pmd here to make sure pmd doesn't change (corresponds to
 * pmdp_collapse_flush() in the THP collapse code path).
 */
static int gup_fast_pte_range(pmd_t pmd, pmd_t *pmdp, unsigned long addr,
		unsigned long end, unsigned int flags, struct page **pages,
		int *nr)
{
	struct dev_pagemap *pgmap = NULL;
	int nr_start = *nr, ret = 0;
	pte_t *ptep, *ptem;

	ptem = ptep = pte_offset_map(&pmd, addr);
	if (!ptep)
		return 0;
	do {
		pte_t pte = ptep_get_lockless(ptep);
		struct page *page;
		struct folio *folio;

		/*
		 * Always fallback to ordinary GUP on PROT_NONE-mapped pages:
		 * pte_access_permitted() better should reject these pages
		 * either way: otherwise, GUP-fast might succeed in
		 * cases where ordinary GUP would fail due to VMA access
		 * permissions.
		 */
		if (pte_protnone(pte))
			goto pte_unmap;

		if (!pte_access_permitted(pte, flags & FOLL_WRITE))
			goto pte_unmap;

		if (pte_devmap(pte)) {
			if (unlikely(flags & FOLL_LONGTERM))
				goto pte_unmap;

			pgmap = get_dev_pagemap(pte_pfn(pte), pgmap);
			if (unlikely(!pgmap)) {
				gup_fast_undo_dev_pagemap(nr, nr_start, flags, pages);
				goto pte_unmap;
			}
		} else if (pte_special(pte))
			goto pte_unmap;

		VM_BUG_ON(!pfn_valid(pte_pfn(pte)));
		page = pte_page(pte);

		folio = try_grab_folio_fast(page, 1, flags);
		if (!folio)
			goto pte_unmap;

		if (unlikely(pmd_val(pmd) != pmd_val(*pmdp)) ||
		    unlikely(pte_val(pte) != pte_val(ptep_get(ptep)))) {
			gup_put_folio(folio, 1, flags);
			goto pte_unmap;
		}

		if (!gup_fast_folio_allowed(folio, flags)) {
			gup_put_folio(folio, 1, flags);
			goto pte_unmap;
		}

		if (!pte_write(pte) && gup_must_unshare(NULL, flags, page)) {
			gup_put_folio(folio, 1, flags);
			goto pte_unmap;
		}

		/*
		 * We need to make the page accessible if and only if we are
		 * going to access its content (the FOLL_PIN case).  Please
		 * see Documentation/core-api/pin_user_pages.rst for
		 * details.
		 */
		if (flags & FOLL_PIN) {
			ret = arch_make_folio_accessible(folio);
			if (ret) {
				gup_put_folio(folio, 1, flags);
				goto pte_unmap;
			}
		}
		folio_set_referenced(folio);
		pages[*nr] = page;
		(*nr)++;
	} while (ptep++, addr += PAGE_SIZE, addr != end);

	ret = 1;

pte_unmap:
	if (pgmap)
		put_dev_pagemap(pgmap);
	pte_unmap(ptem);
	return ret;
}
#else

/*
 * If we can't determine whether or not a pte is special, then fail immediately
 * for ptes. Note, we can still pin HugeTLB and THP as these are guaranteed not
 * to be special.
 *
 * For a futex to be placed on a THP tail page, get_futex_key requires a
 * get_user_pages_fast_only implementation that can pin pages. Thus it's still
 * useful to have gup_fast_pmd_leaf even if we can't operate on ptes.
 */
static int gup_fast_pte_range(pmd_t pmd, pmd_t *pmdp, unsigned long addr,
		unsigned long end, unsigned int flags, struct page **pages,
		int *nr)
{
	return 0;
}
#endif /* CONFIG_ARCH_HAS_PTE_SPECIAL */

#if defined(CONFIG_ARCH_HAS_PTE_DEVMAP) && defined(CONFIG_TRANSPARENT_HUGEPAGE)
static int gup_fast_devmap_leaf(unsigned long pfn, unsigned long addr,
	unsigned long end, unsigned int flags, struct page **pages, int *nr)
{
	int nr_start = *nr;
	struct dev_pagemap *pgmap = NULL;

	do {
		struct folio *folio;
		struct page *page = pfn_to_page(pfn);

		pgmap = get_dev_pagemap(pfn, pgmap);
		if (unlikely(!pgmap)) {
			gup_fast_undo_dev_pagemap(nr, nr_start, flags, pages);
			break;
		}

		if (!(flags & FOLL_PCI_P2PDMA) && is_pci_p2pdma_page(page)) {
			gup_fast_undo_dev_pagemap(nr, nr_start, flags, pages);
			break;
		}

		folio = try_grab_folio_fast(page, 1, flags);
		if (!folio) {
			gup_fast_undo_dev_pagemap(nr, nr_start, flags, pages);
			break;
		}
		folio_set_referenced(folio);
		pages[*nr] = page;
		(*nr)++;
		pfn++;
	} while (addr += PAGE_SIZE, addr != end);

	put_dev_pagemap(pgmap);
	return addr == end;
}

static int gup_fast_devmap_pmd_leaf(pmd_t orig, pmd_t *pmdp, unsigned long addr,
		unsigned long end, unsigned int flags, struct page **pages,
		int *nr)
{
	unsigned long fault_pfn;
	int nr_start = *nr;

	fault_pfn = pmd_pfn(orig) + ((addr & ~PMD_MASK) >> PAGE_SHIFT);
	if (!gup_fast_devmap_leaf(fault_pfn, addr, end, flags, pages, nr))
		return 0;

	if (unlikely(pmd_val(orig) != pmd_val(*pmdp))) {
		gup_fast_undo_dev_pagemap(nr, nr_start, flags, pages);
		return 0;
	}
	return 1;
}

static int gup_fast_devmap_pud_leaf(pud_t orig, pud_t *pudp, unsigned long addr,
		unsigned long end, unsigned int flags, struct page **pages,
		int *nr)
{
	unsigned long fault_pfn;
	int nr_start = *nr;

	fault_pfn = pud_pfn(orig) + ((addr & ~PUD_MASK) >> PAGE_SHIFT);
	if (!gup_fast_devmap_leaf(fault_pfn, addr, end, flags, pages, nr))
		return 0;

	if (unlikely(pud_val(orig) != pud_val(*pudp))) {
		gup_fast_undo_dev_pagemap(nr, nr_start, flags, pages);
		return 0;
	}
	return 1;
}
#else
static int gup_fast_devmap_pmd_leaf(pmd_t orig, pmd_t *pmdp, unsigned long addr,
		unsigned long end, unsigned int flags, struct page **pages,
		int *nr)
{
	BUILD_BUG();
	return 0;
}

static int gup_fast_devmap_pud_leaf(pud_t pud, pud_t *pudp, unsigned long addr,
		unsigned long end, unsigned int flags, struct page **pages,
		int *nr)
{
	BUILD_BUG();
	return 0;
}
#endif

static int gup_fast_pmd_leaf(pmd_t orig, pmd_t *pmdp, unsigned long addr,
		unsigned long end, unsigned int flags, struct page **pages,
		int *nr)
{
	struct page *page;
	struct folio *folio;
	int refs;

	if (!pmd_access_permitted(orig, flags & FOLL_WRITE))
		return 0;

	if (pmd_special(orig))
		return 0;

	if (pmd_devmap(orig)) {
		if (unlikely(flags & FOLL_LONGTERM))
			return 0;
		return gup_fast_devmap_pmd_leaf(orig, pmdp, addr, end, flags,
					        pages, nr);
	}

	page = pmd_page(orig);
	refs = record_subpages(page, PMD_SIZE, addr, end, pages + *nr);

	folio = try_grab_folio_fast(page, refs, flags);
	if (!folio)
		return 0;

	if (unlikely(pmd_val(orig) != pmd_val(*pmdp))) {
		gup_put_folio(folio, refs, flags);
		return 0;
	}

	if (!gup_fast_folio_allowed(folio, flags)) {
		gup_put_folio(folio, refs, flags);
		return 0;
	}
	if (!pmd_write(orig) && gup_must_unshare(NULL, flags, &folio->page)) {
		gup_put_folio(folio, refs, flags);
		return 0;
	}

	*nr += refs;
	folio_set_referenced(folio);
	return 1;
}

static int gup_fast_pud_leaf(pud_t orig, pud_t *pudp, unsigned long addr,
		unsigned long end, unsigned int flags, struct page **pages,
		int *nr)
{
	struct page *page;
	struct folio *folio;
	int refs;

	if (!pud_access_permitted(orig, flags & FOLL_WRITE))
		return 0;

	if (pud_special(orig))
		return 0;

	if (pud_devmap(orig)) {
		if (unlikely(flags & FOLL_LONGTERM))
			return 0;
		return gup_fast_devmap_pud_leaf(orig, pudp, addr, end, flags,
					        pages, nr);
	}

	page = pud_page(orig);
	refs = record_subpages(page, PUD_SIZE, addr, end, pages + *nr);

	folio = try_grab_folio_fast(page, refs, flags);
	if (!folio)
		return 0;

	if (unlikely(pud_val(orig) != pud_val(*pudp))) {
		gup_put_folio(folio, refs, flags);
		return 0;
	}

	if (!gup_fast_folio_allowed(folio, flags)) {
		gup_put_folio(folio, refs, flags);
		return 0;
	}

	if (!pud_write(orig) && gup_must_unshare(NULL, flags, &folio->page)) {
		gup_put_folio(folio, refs, flags);
		return 0;
	}

	*nr += refs;
	folio_set_referenced(folio);
	return 1;
}

static int gup_fast_pgd_leaf(pgd_t orig, pgd_t *pgdp, unsigned long addr,
		unsigned long end, unsigned int flags, struct page **pages,
		int *nr)
{
	int refs;
	struct page *page;
	struct folio *folio;

	if (!pgd_access_permitted(orig, flags & FOLL_WRITE))
		return 0;

	BUILD_BUG_ON(pgd_devmap(orig));

	page = pgd_page(orig);
	refs = record_subpages(page, PGDIR_SIZE, addr, end, pages + *nr);

	folio = try_grab_folio_fast(page, refs, flags);
	if (!folio)
		return 0;

	if (unlikely(pgd_val(orig) != pgd_val(*pgdp))) {
		gup_put_folio(folio, refs, flags);
		return 0;
	}

	if (!pgd_write(orig) && gup_must_unshare(NULL, flags, &folio->page)) {
		gup_put_folio(folio, refs, flags);
		return 0;
	}

	if (!gup_fast_folio_allowed(folio, flags)) {
		gup_put_folio(folio, refs, flags);
		return 0;
	}

	*nr += refs;
	folio_set_referenced(folio);
	return 1;
}

static int gup_fast_pmd_range(pud_t *pudp, pud_t pud, unsigned long addr,
		unsigned long end, unsigned int flags, struct page **pages,
		int *nr)
{
	unsigned long next;
	pmd_t *pmdp;

	pmdp = pmd_offset_lockless(pudp, pud, addr);
	do {
		pmd_t pmd = pmdp_get_lockless(pmdp);

		next = pmd_addr_end(addr, end);
		if (!pmd_present(pmd))
			return 0;

		if (unlikely(pmd_leaf(pmd))) {
			/* See gup_fast_pte_range() */
			if (pmd_protnone(pmd))
				return 0;

			if (!gup_fast_pmd_leaf(pmd, pmdp, addr, next, flags,
				pages, nr))
				return 0;

		} else if (!gup_fast_pte_range(pmd, pmdp, addr, next, flags,
					       pages, nr))
			return 0;
	} while (pmdp++, addr = next, addr != end);

	return 1;
}

static int gup_fast_pud_range(p4d_t *p4dp, p4d_t p4d, unsigned long addr,
		unsigned long end, unsigned int flags, struct page **pages,
		int *nr)
{
	unsigned long next;
	pud_t *pudp;

	pudp = pud_offset_lockless(p4dp, p4d, addr);
	do {
		pud_t pud = READ_ONCE(*pudp);

		next = pud_addr_end(addr, end);
		if (unlikely(!pud_present(pud)))
			return 0;
		if (unlikely(pud_leaf(pud))) {
			if (!gup_fast_pud_leaf(pud, pudp, addr, next, flags,
					       pages, nr))
				return 0;
		} else if (!gup_fast_pmd_range(pudp, pud, addr, next, flags,
					       pages, nr))
			return 0;
	} while (pudp++, addr = next, addr != end);

	return 1;
}

static int gup_fast_p4d_range(pgd_t *pgdp, pgd_t pgd, unsigned long addr,
		unsigned long end, unsigned int flags, struct page **pages,
		int *nr)
{
	unsigned long next;
	p4d_t *p4dp;

	p4dp = p4d_offset_lockless(pgdp, pgd, addr);
	do {
		p4d_t p4d = READ_ONCE(*p4dp);

		next = p4d_addr_end(addr, end);
		if (!p4d_present(p4d))
			return 0;
		BUILD_BUG_ON(p4d_leaf(p4d));
		if (!gup_fast_pud_range(p4dp, p4d, addr, next, flags,
					pages, nr))
			return 0;
	} while (p4dp++, addr = next, addr != end);

	return 1;
}

static void gup_fast_pgd_range(unsigned long addr, unsigned long end,
		unsigned int flags, struct page **pages, int *nr)
{
	unsigned long next;
	pgd_t *pgdp;

	pgdp = pgd_offset(current->mm, addr);
	do {
		pgd_t pgd = READ_ONCE(*pgdp);

		next = pgd_addr_end(addr, end);
		if (pgd_none(pgd))
			return;
		if (unlikely(pgd_leaf(pgd))) {
			if (!gup_fast_pgd_leaf(pgd, pgdp, addr, next, flags,
					       pages, nr))
				return;
		} else if (!gup_fast_p4d_range(pgdp, pgd, addr, next, flags,
					       pages, nr))
			return;
	} while (pgdp++, addr = next, addr != end);
}
#else
static inline void gup_fast_pgd_range(unsigned long addr, unsigned long end,
		unsigned int flags, struct page **pages, int *nr)
{
}
#endif /* CONFIG_HAVE_GUP_FAST */

#ifndef gup_fast_permitted
/*
 * Check if it's allowed to use get_user_pages_fast_only() for the range, or
 * we need to fall back to the slow version:
 */
static bool gup_fast_permitted(unsigned long start, unsigned long end)
{
	return true;
}
#endif

static unsigned long gup_fast(unsigned long start, unsigned long end,
		unsigned int gup_flags, struct page **pages)
{
	unsigned long flags;
	int nr_pinned = 0;
	unsigned seq;

	if (!IS_ENABLED(CONFIG_HAVE_GUP_FAST) ||
	    !gup_fast_permitted(start, end))
		return 0;

	if (gup_flags & FOLL_PIN) {
		if (!raw_seqcount_try_begin(&current->mm->write_protect_seq, seq))
			return 0;
	}

	/*
	 * Disable interrupts. The nested form is used, in order to allow full,
	 * general purpose use of this routine.
	 *
	 * With interrupts disabled, we block page table pages from being freed
	 * from under us. See struct mmu_table_batch comments in
	 * include/asm-generic/tlb.h for more details.
	 *
	 * We do not adopt an rcu_read_lock() here as we also want to block IPIs
	 * that come from THPs splitting.
	 */
	local_irq_save(flags);
	gup_fast_pgd_range(start, end, gup_flags, pages, &nr_pinned);
	local_irq_restore(flags);

	/*
	 * When pinning pages for DMA there could be a concurrent write protect
	 * from fork() via copy_page_range(), in this case always fail GUP-fast.
	 */
	if (gup_flags & FOLL_PIN) {
		if (read_seqcount_retry(&current->mm->write_protect_seq, seq)) {
			gup_fast_unpin_user_pages(pages, nr_pinned);
			return 0;
		} else {
			sanity_check_pinned_pages(pages, nr_pinned);
		}
	}
	return nr_pinned;
}

static int gup_fast_fallback(unsigned long start, unsigned long nr_pages,
		unsigned int gup_flags, struct page **pages)
{
	unsigned long len, end;
	unsigned long nr_pinned;
	int locked = 0;
	int ret;

	if (WARN_ON_ONCE(gup_flags & ~(FOLL_WRITE | FOLL_LONGTERM |
				       FOLL_FORCE | FOLL_PIN | FOLL_GET |
				       FOLL_FAST_ONLY | FOLL_NOFAULT |
				       FOLL_PCI_P2PDMA | FOLL_HONOR_NUMA_FAULT)))
		return -EINVAL;

	if (gup_flags & FOLL_PIN)
		mm_set_has_pinned_flag(&current->mm->flags);

	if (!(gup_flags & FOLL_FAST_ONLY))
		might_lock_read(&current->mm->mmap_lock);

	start = untagged_addr(start) & PAGE_MASK;
	len = nr_pages << PAGE_SHIFT;
	if (check_add_overflow(start, len, &end))
		return -EOVERFLOW;
	if (end > TASK_SIZE_MAX)
		return -EFAULT;

	nr_pinned = gup_fast(start, end, gup_flags, pages);
	if (nr_pinned == nr_pages || gup_flags & FOLL_FAST_ONLY)
		return nr_pinned;

	/* Slow path: try to get the remaining pages with get_user_pages */
	start += nr_pinned << PAGE_SHIFT;
	pages += nr_pinned;
	ret = __gup_longterm_locked(current->mm, start, nr_pages - nr_pinned,
				    pages, &locked,
				    gup_flags | FOLL_TOUCH | FOLL_UNLOCKABLE);
	if (ret < 0) {
		/*
		 * The caller has to unpin the pages we already pinned so
		 * returning -errno is not an option
		 */
		if (nr_pinned)
			return nr_pinned;
		return ret;
	}
	return ret + nr_pinned;
}

/**
 * get_user_pages_fast_only() - pin user pages in memory
 * @start:      starting user address
 * @nr_pages:   number of pages from start to pin
 * @gup_flags:  flags modifying pin behaviour
 * @pages:      array that receives pointers to the pages pinned.
 *              Should be at least nr_pages long.
 *
 * Like get_user_pages_fast() except it's IRQ-safe in that it won't fall back to
 * the regular GUP.
 *
 * If the architecture does not support this function, simply return with no
 * pages pinned.
 *
 * Careful, careful! COW breaking can go either way, so a non-write
 * access can get ambiguous page results. If you call this function without
 * 'write' set, you'd better be sure that you're ok with that ambiguity.
 */
int get_user_pages_fast_only(unsigned long start, int nr_pages,
			     unsigned int gup_flags, struct page **pages)
{
	/*
	 * Internally (within mm/gup.c), gup fast variants must set FOLL_GET,
	 * because gup fast is always a "pin with a +1 page refcount" request.
	 *
	 * FOLL_FAST_ONLY is required in order to match the API description of
	 * this routine: no fall back to regular ("slow") GUP.
	 */
	if (!is_valid_gup_args(pages, NULL, &gup_flags,
			       FOLL_GET | FOLL_FAST_ONLY))
		return -EINVAL;

	return gup_fast_fallback(start, nr_pages, gup_flags, pages);
}
EXPORT_SYMBOL_GPL(get_user_pages_fast_only);

/**
 * get_user_pages_fast() - pin user pages in memory
 * @start:      starting user address
 * @nr_pages:   number of pages from start to pin
 * @gup_flags:  flags modifying pin behaviour
 * @pages:      array that receives pointers to the pages pinned.
 *              Should be at least nr_pages long.
 *
 * Attempt to pin user pages in memory without taking mm->mmap_lock.
 * If not successful, it will fall back to taking the lock and
 * calling get_user_pages().
 *
 * Returns number of pages pinned. This may be fewer than the number requested.
 * If nr_pages is 0 or negative, returns 0. If no pages were pinned, returns
 * -errno.
 */
int get_user_pages_fast(unsigned long start, int nr_pages,
			unsigned int gup_flags, struct page **pages)
{
	/*
	 * The caller may or may not have explicitly set FOLL_GET; either way is
	 * OK. However, internally (within mm/gup.c), gup fast variants must set
	 * FOLL_GET, because gup fast is always a "pin with a +1 page refcount"
	 * request.
	 */
	if (!is_valid_gup_args(pages, NULL, &gup_flags, FOLL_GET))
		return -EINVAL;
	return gup_fast_fallback(start, nr_pages, gup_flags, pages);
}
EXPORT_SYMBOL_GPL(get_user_pages_fast);

/**
 * pin_user_pages_fast() - pin user pages in memory without taking locks
 *
 * @start:      starting user address
 * @nr_pages:   number of pages from start to pin
 * @gup_flags:  flags modifying pin behaviour
 * @pages:      array that receives pointers to the pages pinned.
 *              Should be at least nr_pages long.
 *
 * Nearly the same as get_user_pages_fast(), except that FOLL_PIN is set. See
 * get_user_pages_fast() for documentation on the function arguments, because
 * the arguments here are identical.
 *
 * FOLL_PIN means that the pages must be released via unpin_user_page(). Please
 * see Documentation/core-api/pin_user_pages.rst for further details.
 *
 * Note that if a zero_page is amongst the returned pages, it will not have
 * pins in it and unpin_user_page() will not remove pins from it.
 */
int pin_user_pages_fast(unsigned long start, int nr_pages,
			unsigned int gup_flags, struct page **pages)
{
	if (!is_valid_gup_args(pages, NULL, &gup_flags, FOLL_PIN))
		return -EINVAL;
	return gup_fast_fallback(start, nr_pages, gup_flags, pages);
}
EXPORT_SYMBOL_GPL(pin_user_pages_fast);

/**
 * pin_user_pages_remote() - pin pages of a remote process
 *
 * @mm:		mm_struct of target mm
 * @start:	starting user address
 * @nr_pages:	number of pages from start to pin
 * @gup_flags:	flags modifying lookup behaviour
 * @pages:	array that receives pointers to the pages pinned.
 *		Should be at least nr_pages long.
 * @locked:	pointer to lock flag indicating whether lock is held and
 *		subsequently whether VM_FAULT_RETRY functionality can be
 *		utilised. Lock must initially be held.
 *
 * Nearly the same as get_user_pages_remote(), except that FOLL_PIN is set. See
 * get_user_pages_remote() for documentation on the function arguments, because
 * the arguments here are identical.
 *
 * FOLL_PIN means that the pages must be released via unpin_user_page(). Please
 * see Documentation/core-api/pin_user_pages.rst for details.
 *
 * Note that if a zero_page is amongst the returned pages, it will not have
 * pins in it and unpin_user_page*() will not remove pins from it.
 */
long pin_user_pages_remote(struct mm_struct *mm,
			   unsigned long start, unsigned long nr_pages,
			   unsigned int gup_flags, struct page **pages,
			   int *locked)
{
	int local_locked = 1;

	if (!is_valid_gup_args(pages, locked, &gup_flags,
			       FOLL_PIN | FOLL_TOUCH | FOLL_REMOTE))
		return 0;
	return __gup_longterm_locked(mm, start, nr_pages, pages,
				     locked ? locked : &local_locked,
				     gup_flags);
}
EXPORT_SYMBOL(pin_user_pages_remote);

/**
 * pin_user_pages() - pin user pages in memory for use by other devices
 *
 * @start:	starting user address
 * @nr_pages:	number of pages from start to pin
 * @gup_flags:	flags modifying lookup behaviour
 * @pages:	array that receives pointers to the pages pinned.
 *		Should be at least nr_pages long.
 *
 * Nearly the same as get_user_pages(), except that FOLL_TOUCH is not set, and
 * FOLL_PIN is set.
 *
 * FOLL_PIN means that the pages must be released via unpin_user_page(). Please
 * see Documentation/core-api/pin_user_pages.rst for details.
 *
 * Note that if a zero_page is amongst the returned pages, it will not have
 * pins in it and unpin_user_page*() will not remove pins from it.
 */
long pin_user_pages(unsigned long start, unsigned long nr_pages,
		    unsigned int gup_flags, struct page **pages)
{
	int locked = 1;

	if (!is_valid_gup_args(pages, NULL, &gup_flags, FOLL_PIN))
		return 0;
	return __gup_longterm_locked(current->mm, start, nr_pages,
				     pages, &locked, gup_flags);
}
EXPORT_SYMBOL(pin_user_pages);

/*
 * pin_user_pages_unlocked() is the FOLL_PIN variant of
 * get_user_pages_unlocked(). Behavior is the same, except that this one sets
 * FOLL_PIN and rejects FOLL_GET.
 *
 * Note that if a zero_page is amongst the returned pages, it will not have
 * pins in it and unpin_user_page*() will not remove pins from it.
 */
long pin_user_pages_unlocked(unsigned long start, unsigned long nr_pages,
			     struct page **pages, unsigned int gup_flags)
{
	int locked = 0;

	if (!is_valid_gup_args(pages, NULL, &gup_flags,
			       FOLL_PIN | FOLL_TOUCH | FOLL_UNLOCKABLE))
		return 0;

	return __gup_longterm_locked(current->mm, start, nr_pages, pages,
				     &locked, gup_flags);
}
EXPORT_SYMBOL(pin_user_pages_unlocked);

/**
 * memfd_pin_folios() - pin folios associated with a memfd
 * @memfd:      the memfd whose folios are to be pinned
 * @start:      the first memfd offset
 * @end:        the last memfd offset (inclusive)
 * @folios:     array that receives pointers to the folios pinned
 * @max_folios: maximum number of entries in @folios
 * @offset:     the offset into the first folio
 *
 * Attempt to pin folios associated with a memfd in the contiguous range
 * [start, end]. Given that a memfd is either backed by shmem or hugetlb,
 * the folios can either be found in the page cache or need to be allocated
 * if necessary. Once the folios are located, they are all pinned via
 * FOLL_PIN and @offset is populatedwith the offset into the first folio.
 * And, eventually, these pinned folios must be released either using
 * unpin_folios() or unpin_folio().
 *
 * It must be noted that the folios may be pinned for an indefinite amount
 * of time. And, in most cases, the duration of time they may stay pinned
 * would be controlled by the userspace. This behavior is effectively the
 * same as using FOLL_LONGTERM with other GUP APIs.
 *
 * Returns number of folios pinned, which could be less than @max_folios
 * as it depends on the folio sizes that cover the range [start, end].
 * If no folios were pinned, it returns -errno.
 */
long memfd_pin_folios(struct file *memfd, loff_t start, loff_t end,
		      struct folio **folios, unsigned int max_folios,
		      pgoff_t *offset)
{
	unsigned int flags, nr_folios, nr_found;
	unsigned int i, pgshift = PAGE_SHIFT;
	pgoff_t start_idx, end_idx, next_idx;
	struct folio *folio = NULL;
	struct folio_batch fbatch;
	struct hstate *h;
	long ret = -EINVAL;

	if (start < 0 || start > end || !max_folios)
		return -EINVAL;

	if (!memfd)
		return -EINVAL;

	if (!shmem_file(memfd) && !is_file_hugepages(memfd))
		return -EINVAL;

	if (end >= i_size_read(file_inode(memfd)))
		return -EINVAL;

	if (is_file_hugepages(memfd)) {
		h = hstate_file(memfd);
		pgshift = huge_page_shift(h);
	}

	flags = memalloc_pin_save();
	do {
		nr_folios = 0;
		start_idx = start >> pgshift;
		end_idx = end >> pgshift;
		if (is_file_hugepages(memfd)) {
			start_idx <<= huge_page_order(h);
			end_idx <<= huge_page_order(h);
		}

		folio_batch_init(&fbatch);
		while (start_idx <= end_idx && nr_folios < max_folios) {
			/*
			 * In most cases, we should be able to find the folios
			 * in the page cache. If we cannot find them for some
			 * reason, we try to allocate them and add them to the
			 * page cache.
			 */
			nr_found = filemap_get_folios_contig(memfd->f_mapping,
							     &start_idx,
							     end_idx,
							     &fbatch);
			if (folio) {
				folio_put(folio);
				folio = NULL;
			}

			next_idx = 0;
			for (i = 0; i < nr_found; i++) {
				/*
				 * As there can be multiple entries for a
				 * given folio in the batch returned by
				 * filemap_get_folios_contig(), the below
				 * check is to ensure that we pin and return a
				 * unique set of folios between start and end.
				 */
				if (next_idx &&
				    next_idx != folio_index(fbatch.folios[i]))
					continue;

				folio = page_folio(&fbatch.folios[i]->page);

				if (try_grab_folio(folio, 1, FOLL_PIN)) {
					folio_batch_release(&fbatch);
					ret = -EINVAL;
					goto err;
				}

				if (nr_folios == 0)
					*offset = offset_in_folio(folio, start);

				folios[nr_folios] = folio;
				next_idx = folio_next_index(folio);
				if (++nr_folios == max_folios)
					break;
			}

			folio = NULL;
			folio_batch_release(&fbatch);
			if (!nr_found) {
				folio = memfd_alloc_folio(memfd, start_idx);
				if (IS_ERR(folio)) {
					ret = PTR_ERR(folio);
					if (ret != -EEXIST)
						goto err;
					folio = NULL;
				}
			}
		}

		ret = check_and_migrate_movable_folios(nr_folios, folios);
	} while (ret == -EAGAIN);

	memalloc_pin_restore(flags);
	return ret ? ret : nr_folios;
err:
	memalloc_pin_restore(flags);
	unpin_folios(folios, nr_folios);

	return ret;
}
EXPORT_SYMBOL_GPL(memfd_pin_folios);

/**
 * folio_add_pins() - add pins to an already-pinned folio
 * @folio: the folio to add more pins to
 * @pins: number of pins to add
 *
 * Try to add more pins to an already-pinned folio. The semantics
 * of the pin (e.g., FOLL_WRITE) follow any existing pin and cannot
 * be changed.
 *
 * This function is helpful when having obtained a pin on a large folio
 * using memfd_pin_folios(), but wanting to logically unpin parts
 * (e.g., individual pages) of the folio later, for example, using
 * unpin_user_page_range_dirty_lock().
 *
 * This is not the right interface to initially pin a folio.
 */
int folio_add_pins(struct folio *folio, unsigned int pins)
{
	VM_WARN_ON_ONCE(!folio_maybe_dma_pinned(folio));

	return try_grab_folio(folio, pins, FOLL_PIN);
}
EXPORT_SYMBOL_GPL(folio_add_pins);
