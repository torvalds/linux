// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/spinlock.h>

#include <linux/mm.h>
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
#include <linux/sched/mm.h>

#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

#include "internal.h"

struct follow_page_context {
	struct dev_pagemap *pgmap;
	unsigned int page_mask;
};

static void hpage_pincount_add(struct page *page, int refs)
{
	VM_BUG_ON_PAGE(!hpage_pincount_available(page), page);
	VM_BUG_ON_PAGE(page != compound_head(page), page);

	atomic_add(refs, compound_pincount_ptr(page));
}

static void hpage_pincount_sub(struct page *page, int refs)
{
	VM_BUG_ON_PAGE(!hpage_pincount_available(page), page);
	VM_BUG_ON_PAGE(page != compound_head(page), page);

	atomic_sub(refs, compound_pincount_ptr(page));
}

/* Equivalent to calling put_page() @refs times. */
static void put_page_refs(struct page *page, int refs)
{
#ifdef CONFIG_DEBUG_VM
	if (VM_WARN_ON_ONCE_PAGE(page_ref_count(page) < refs, page))
		return;
#endif

	/*
	 * Calling put_page() for each ref is unnecessarily slow. Only the last
	 * ref needs a put_page().
	 */
	if (refs > 1)
		page_ref_sub(page, refs - 1);
	put_page(page);
}

/*
 * Return the compound head page with ref appropriately incremented,
 * or NULL if that failed.
 */
static inline struct page *try_get_compound_head(struct page *page, int refs)
{
	struct page *head = compound_head(page);

	if (WARN_ON_ONCE(page_ref_count(head) < 0))
		return NULL;
	if (unlikely(!page_cache_add_speculative(head, refs)))
		return NULL;

	/*
	 * At this point we have a stable reference to the head page; but it
	 * could be that between the compound_head() lookup and the refcount
	 * increment, the compound page was split, in which case we'd end up
	 * holding a reference on a page that has nothing to do with the page
	 * we were given anymore.
	 * So now that the head page is stable, recheck that the pages still
	 * belong together.
	 */
	if (unlikely(compound_head(page) != head)) {
		put_page_refs(head, refs);
		return NULL;
	}

	return head;
}

/**
 * try_grab_compound_head() - attempt to elevate a page's refcount, by a
 * flags-dependent amount.
 *
 * Even though the name includes "compound_head", this function is still
 * appropriate for callers that have a non-compound @page to get.
 *
 * @page:  pointer to page to be grabbed
 * @refs:  the value to (effectively) add to the page's refcount
 * @flags: gup flags: these are the FOLL_* flag values.
 *
 * "grab" names in this file mean, "look at flags to decide whether to use
 * FOLL_PIN or FOLL_GET behavior, when incrementing the page's refcount.
 *
 * Either FOLL_PIN or FOLL_GET (or neither) must be set, but not both at the
 * same time. (That's true throughout the get_user_pages*() and
 * pin_user_pages*() APIs.) Cases:
 *
 *    FOLL_GET: page's refcount will be incremented by @refs.
 *
 *    FOLL_PIN on compound pages that are > two pages long: page's refcount will
 *    be incremented by @refs, and page[2].hpage_pinned_refcount will be
 *    incremented by @refs * GUP_PIN_COUNTING_BIAS.
 *
 *    FOLL_PIN on normal pages, or compound pages that are two pages long:
 *    page's refcount will be incremented by @refs * GUP_PIN_COUNTING_BIAS.
 *
 * Return: head page (with refcount appropriately incremented) for success, or
 * NULL upon failure. If neither FOLL_GET nor FOLL_PIN was set, that's
 * considered failure, and furthermore, a likely bug in the caller, so a warning
 * is also emitted.
 */
__maybe_unused struct page *try_grab_compound_head(struct page *page,
						   int refs, unsigned int flags)
{
	if (flags & FOLL_GET)
		return try_get_compound_head(page, refs);
	else if (flags & FOLL_PIN) {
		/*
		 * Can't do FOLL_LONGTERM + FOLL_PIN gup fast path if not in a
		 * right zone, so fail and let the caller fall back to the slow
		 * path.
		 */
		if (unlikely((flags & FOLL_LONGTERM) &&
			     !is_pinnable_page(page)))
			return NULL;

		/*
		 * CAUTION: Don't use compound_head() on the page before this
		 * point, the result won't be stable.
		 */
		page = try_get_compound_head(page, refs);
		if (!page)
			return NULL;

		/*
		 * When pinning a compound page of order > 1 (which is what
		 * hpage_pincount_available() checks for), use an exact count to
		 * track it, via hpage_pincount_add/_sub().
		 *
		 * However, be sure to *also* increment the normal page refcount
		 * field at least once, so that the page really is pinned.
		 * That's why the refcount from the earlier
		 * try_get_compound_head() is left intact.
		 */
		if (hpage_pincount_available(page))
			hpage_pincount_add(page, refs);
		else
			page_ref_add(page, refs * (GUP_PIN_COUNTING_BIAS - 1));

		mod_node_page_state(page_pgdat(page), NR_FOLL_PIN_ACQUIRED,
				    refs);

		return page;
	}

	WARN_ON_ONCE(1);
	return NULL;
}

static void put_compound_head(struct page *page, int refs, unsigned int flags)
{
	if (flags & FOLL_PIN) {
		mod_node_page_state(page_pgdat(page), NR_FOLL_PIN_RELEASED,
				    refs);

		if (hpage_pincount_available(page))
			hpage_pincount_sub(page, refs);
		else
			refs *= GUP_PIN_COUNTING_BIAS;
	}

	put_page_refs(page, refs);
}

/**
 * try_grab_page() - elevate a page's refcount by a flag-dependent amount
 *
 * This might not do anything at all, depending on the flags argument.
 *
 * "grab" names in this file mean, "look at flags to decide whether to use
 * FOLL_PIN or FOLL_GET behavior, when incrementing the page's refcount.
 *
 * @page:    pointer to page to be grabbed
 * @flags:   gup flags: these are the FOLL_* flag values.
 *
 * Either FOLL_PIN or FOLL_GET (or neither) may be set, but not both at the same
 * time. Cases: please see the try_grab_compound_head() documentation, with
 * "refs=1".
 *
 * Return: true for success, or if no action was required (if neither FOLL_PIN
 * nor FOLL_GET was set, nothing is done). False for failure: FOLL_GET or
 * FOLL_PIN was set, but the page could not be grabbed.
 */
bool __must_check try_grab_page(struct page *page, unsigned int flags)
{
	WARN_ON_ONCE((flags & (FOLL_GET | FOLL_PIN)) == (FOLL_GET | FOLL_PIN));

	if (flags & FOLL_GET)
		return try_get_page(page);
	else if (flags & FOLL_PIN) {
		int refs = 1;

		page = compound_head(page);

		if (WARN_ON_ONCE(page_ref_count(page) <= 0))
			return false;

		if (hpage_pincount_available(page))
			hpage_pincount_add(page, 1);
		else
			refs = GUP_PIN_COUNTING_BIAS;

		/*
		 * Similar to try_grab_compound_head(): even if using the
		 * hpage_pincount_add/_sub() routines, be sure to
		 * *also* increment the normal page refcount field at least
		 * once, so that the page really is pinned.
		 */
		page_ref_add(page, refs);

		mod_node_page_state(page_pgdat(page), NR_FOLL_PIN_ACQUIRED, 1);
	}

	return true;
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
	put_compound_head(compound_head(page), 1, FOLL_PIN);
}
EXPORT_SYMBOL(unpin_user_page);

static inline void compound_range_next(unsigned long i, unsigned long npages,
				       struct page **list, struct page **head,
				       unsigned int *ntails)
{
	struct page *next, *page;
	unsigned int nr = 1;

	if (i >= npages)
		return;

	next = *list + i;
	page = compound_head(next);
	if (PageCompound(page) && compound_order(page) >= 1)
		nr = min_t(unsigned int,
			   page + compound_nr(page) - next, npages - i);

	*head = page;
	*ntails = nr;
}

#define for_each_compound_range(__i, __list, __npages, __head, __ntails) \
	for (__i = 0, \
	     compound_range_next(__i, __npages, __list, &(__head), &(__ntails)); \
	     __i < __npages; __i += __ntails, \
	     compound_range_next(__i, __npages, __list, &(__head), &(__ntails)))

static inline void compound_next(unsigned long i, unsigned long npages,
				 struct page **list, struct page **head,
				 unsigned int *ntails)
{
	struct page *page;
	unsigned int nr;

	if (i >= npages)
		return;

	page = compound_head(list[i]);
	for (nr = i + 1; nr < npages; nr++) {
		if (compound_head(list[nr]) != page)
			break;
	}

	*head = page;
	*ntails = nr - i;
}

#define for_each_compound_head(__i, __list, __npages, __head, __ntails) \
	for (__i = 0, \
	     compound_next(__i, __npages, __list, &(__head), &(__ntails)); \
	     __i < __npages; __i += __ntails, \
	     compound_next(__i, __npages, __list, &(__head), &(__ntails)))

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
	unsigned long index;
	struct page *head;
	unsigned int ntails;

	if (!make_dirty) {
		unpin_user_pages(pages, npages);
		return;
	}

	for_each_compound_head(index, pages, npages, head, ntails) {
		/*
		 * Checking PageDirty at this point may race with
		 * clear_page_dirty_for_io(), but that's OK. Two key
		 * cases:
		 *
		 * 1) This code sees the page as already dirty, so it
		 * skips the call to set_page_dirty(). That could happen
		 * because clear_page_dirty_for_io() called
		 * page_mkclean(), followed by set_page_dirty().
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
		if (!PageDirty(head))
			set_page_dirty_lock(head);
		put_compound_head(head, ntails, FOLL_PIN);
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
	unsigned long index;
	struct page *head;
	unsigned int ntails;

	for_each_compound_range(index, &page, npages, head, ntails) {
		if (make_dirty && !PageDirty(head))
			set_page_dirty_lock(head);
		put_compound_head(head, ntails, FOLL_PIN);
	}
}
EXPORT_SYMBOL(unpin_user_page_range_dirty_lock);

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
	unsigned long index;
	struct page *head;
	unsigned int ntails;

	/*
	 * If this WARN_ON() fires, then the system *might* be leaking pages (by
	 * leaving them pinned), but probably not. More likely, gup/pup returned
	 * a hard -ERRNO error to the caller, who erroneously passed it here.
	 */
	if (WARN_ON(IS_ERR_VALUE(npages)))
		return;

	for_each_compound_head(index, pages, npages, head, ntails)
		put_compound_head(head, ntails, FOLL_PIN);
}
EXPORT_SYMBOL(unpin_user_pages);

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
static struct page *no_page_table(struct vm_area_struct *vma,
		unsigned int flags)
{
	/*
	 * When core dumping an enormous anonymous area that nobody
	 * has touched so far, we don't want to allocate unnecessary pages or
	 * page tables.  Return error instead of NULL to skip handle_mm_fault,
	 * then get_dump_page() will return NULL to leave a hole in the dump.
	 * But we can only make this optimization where a hole would surely
	 * be zero-filled if handle_mm_fault() actually did handle it.
	 */
	if ((flags & FOLL_DUMP) &&
			(vma_is_anonymous(vma) || !vma->vm_ops->fault))
		return ERR_PTR(-EFAULT);
	return NULL;
}

static int follow_pfn_pte(struct vm_area_struct *vma, unsigned long address,
		pte_t *pte, unsigned int flags)
{
	/* No page to get reference */
	if (flags & (FOLL_GET | FOLL_PIN))
		return -EFAULT;

	if (flags & FOLL_TOUCH) {
		pte_t entry = *pte;

		if (flags & FOLL_WRITE)
			entry = pte_mkdirty(entry);
		entry = pte_mkyoung(entry);

		if (!pte_same(*pte, entry)) {
			set_pte_at(vma->vm_mm, address, pte, entry);
			update_mmu_cache(vma, address, pte);
		}
	}

	/* Proper page table entry exists, but no corresponding struct page */
	return -EEXIST;
}

/*
 * FOLL_FORCE can write to even unwritable pte's, but only
 * after we've gone through a COW cycle and they are dirty.
 */
static inline bool can_follow_write_pte(pte_t pte, unsigned int flags)
{
	return pte_write(pte) ||
		((flags & FOLL_FORCE) && (flags & FOLL_COW) && pte_dirty(pte));
}

static struct page *follow_page_pte(struct vm_area_struct *vma,
		unsigned long address, pmd_t *pmd, unsigned int flags,
		struct dev_pagemap **pgmap)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page *page;
	spinlock_t *ptl;
	pte_t *ptep, pte;
	int ret;

	/* FOLL_GET and FOLL_PIN are mutually exclusive. */
	if (WARN_ON_ONCE((flags & (FOLL_PIN | FOLL_GET)) ==
			 (FOLL_PIN | FOLL_GET)))
		return ERR_PTR(-EINVAL);

	/*
	 * Considering PTE level hugetlb, like continuous-PTE hugetlb on
	 * ARM64 architecture.
	 */
	if (is_vm_hugetlb_page(vma)) {
		page = follow_huge_pmd_pte(vma, address, flags);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}

retry:
	if (unlikely(pmd_bad(*pmd)))
		return no_page_table(vma, flags);

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	pte = *ptep;
	if (!pte_present(pte)) {
		swp_entry_t entry;
		/*
		 * KSM's break_ksm() relies upon recognizing a ksm page
		 * even while it is being migrated, so for that case we
		 * need migration_entry_wait().
		 */
		if (likely(!(flags & FOLL_MIGRATION)))
			goto no_page;
		if (pte_none(pte))
			goto no_page;
		entry = pte_to_swp_entry(pte);
		if (!is_migration_entry(entry))
			goto no_page;
		pte_unmap_unlock(ptep, ptl);
		migration_entry_wait(mm, pmd, address);
		goto retry;
	}
	if ((flags & FOLL_NUMA) && pte_protnone(pte))
		goto no_page;
	if ((flags & FOLL_WRITE) && !can_follow_write_pte(pte, flags)) {
		pte_unmap_unlock(ptep, ptl);
		return NULL;
	}

	page = vm_normal_page(vma, address, pte);
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

	/* try_grab_page() does nothing unless FOLL_GET or FOLL_PIN is set. */
	if (unlikely(!try_grab_page(page, flags))) {
		page = ERR_PTR(-ENOMEM);
		goto out;
	}
	/*
	 * We need to make the page accessible if and only if we are going
	 * to access its content (the FOLL_PIN case).  Please see
	 * Documentation/core-api/pin_user_pages.rst for details.
	 */
	if (flags & FOLL_PIN) {
		ret = arch_make_page_accessible(page);
		if (ret) {
			unpin_user_page(page);
			page = ERR_PTR(ret);
			goto out;
		}
	}
	if (flags & FOLL_TOUCH) {
		if ((flags & FOLL_WRITE) &&
		    !pte_dirty(pte) && !PageDirty(page))
			set_page_dirty(page);
		/*
		 * pte_mkyoung() would be more correct here, but atomic care
		 * is needed to avoid losing the dirty bit: it is easier to use
		 * mark_page_accessed().
		 */
		mark_page_accessed(page);
	}
	if ((flags & FOLL_MLOCK) && (vma->vm_flags & VM_LOCKED)) {
		/* Do not mlock pte-mapped THP */
		if (PageTransCompound(page))
			goto out;

		/*
		 * The preliminary mapping check is mainly to avoid the
		 * pointless overhead of lock_page on the ZERO_PAGE
		 * which might bounce very badly if there is contention.
		 *
		 * If the page is already locked, we don't need to
		 * handle it now - vmscan will handle it later if and
		 * when it attempts to reclaim the page.
		 */
		if (page->mapping && trylock_page(page)) {
			lru_add_drain();  /* push cached pages to LRU */
			/*
			 * Because we lock page here, and migration is
			 * blocked by the pte's page reference, and we
			 * know the page is still mapped, we don't even
			 * need to check for file-cache page truncation.
			 */
			mlock_vma_page(page);
			unlock_page(page);
		}
	}
out:
	pte_unmap_unlock(ptep, ptl);
	return page;
no_page:
	pte_unmap_unlock(ptep, ptl);
	if (!pte_none(pte))
		return NULL;
	return no_page_table(vma, flags);
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
	/*
	 * The READ_ONCE() will stabilize the pmdval in a register or
	 * on the stack so that it will stop changing under the code.
	 */
	pmdval = READ_ONCE(*pmd);
	if (pmd_none(pmdval))
		return no_page_table(vma, flags);
	if (pmd_huge(pmdval) && is_vm_hugetlb_page(vma)) {
		page = follow_huge_pmd_pte(vma, address, flags);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}
	if (is_hugepd(__hugepd(pmd_val(pmdval)))) {
		page = follow_huge_pd(vma, address,
				      __hugepd(pmd_val(pmdval)), flags,
				      PMD_SHIFT);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}
retry:
	if (!pmd_present(pmdval)) {
		if (likely(!(flags & FOLL_MIGRATION)))
			return no_page_table(vma, flags);
		VM_BUG_ON(thp_migration_supported() &&
				  !is_pmd_migration_entry(pmdval));
		if (is_pmd_migration_entry(pmdval))
			pmd_migration_entry_wait(mm, pmd);
		pmdval = READ_ONCE(*pmd);
		/*
		 * MADV_DONTNEED may convert the pmd to null because
		 * mmap_lock is held in read mode
		 */
		if (pmd_none(pmdval))
			return no_page_table(vma, flags);
		goto retry;
	}
	if (pmd_devmap(pmdval)) {
		ptl = pmd_lock(mm, pmd);
		page = follow_devmap_pmd(vma, address, pmd, flags, &ctx->pgmap);
		spin_unlock(ptl);
		if (page)
			return page;
	}
	if (likely(!pmd_trans_huge(pmdval)))
		return follow_page_pte(vma, address, pmd, flags, &ctx->pgmap);

	if ((flags & FOLL_NUMA) && pmd_protnone(pmdval))
		return no_page_table(vma, flags);

retry_locked:
	ptl = pmd_lock(mm, pmd);
	if (unlikely(pmd_none(*pmd))) {
		spin_unlock(ptl);
		return no_page_table(vma, flags);
	}
	if (unlikely(!pmd_present(*pmd))) {
		spin_unlock(ptl);
		if (likely(!(flags & FOLL_MIGRATION)))
			return no_page_table(vma, flags);
		pmd_migration_entry_wait(mm, pmd);
		goto retry_locked;
	}
	if (unlikely(!pmd_trans_huge(*pmd))) {
		spin_unlock(ptl);
		return follow_page_pte(vma, address, pmd, flags, &ctx->pgmap);
	}
	if (flags & FOLL_SPLIT_PMD) {
		int ret;
		page = pmd_page(*pmd);
		if (is_huge_zero_page(page)) {
			spin_unlock(ptl);
			ret = 0;
			split_huge_pmd(vma, pmd, address);
			if (pmd_trans_unstable(pmd))
				ret = -EBUSY;
		} else {
			spin_unlock(ptl);
			split_huge_pmd(vma, pmd, address);
			ret = pte_alloc(mm, pmd) ? -ENOMEM : 0;
		}

		return ret ? ERR_PTR(ret) :
			follow_page_pte(vma, address, pmd, flags, &ctx->pgmap);
	}
	page = follow_trans_huge_pmd(vma, address, pmd, flags);
	spin_unlock(ptl);
	ctx->page_mask = HPAGE_PMD_NR - 1;
	return page;
}

static struct page *follow_pud_mask(struct vm_area_struct *vma,
				    unsigned long address, p4d_t *p4dp,
				    unsigned int flags,
				    struct follow_page_context *ctx)
{
	pud_t *pud;
	spinlock_t *ptl;
	struct page *page;
	struct mm_struct *mm = vma->vm_mm;

	pud = pud_offset(p4dp, address);
	if (pud_none(*pud))
		return no_page_table(vma, flags);
	if (pud_huge(*pud) && is_vm_hugetlb_page(vma)) {
		page = follow_huge_pud(mm, address, pud, flags);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}
	if (is_hugepd(__hugepd(pud_val(*pud)))) {
		page = follow_huge_pd(vma, address,
				      __hugepd(pud_val(*pud)), flags,
				      PUD_SHIFT);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}
	if (pud_devmap(*pud)) {
		ptl = pud_lock(mm, pud);
		page = follow_devmap_pud(vma, address, pud, flags, &ctx->pgmap);
		spin_unlock(ptl);
		if (page)
			return page;
	}
	if (unlikely(pud_bad(*pud)))
		return no_page_table(vma, flags);

	return follow_pmd_mask(vma, address, pud, flags, ctx);
}

static struct page *follow_p4d_mask(struct vm_area_struct *vma,
				    unsigned long address, pgd_t *pgdp,
				    unsigned int flags,
				    struct follow_page_context *ctx)
{
	p4d_t *p4d;
	struct page *page;

	p4d = p4d_offset(pgdp, address);
	if (p4d_none(*p4d))
		return no_page_table(vma, flags);
	BUILD_BUG_ON(p4d_huge(*p4d));
	if (unlikely(p4d_bad(*p4d)))
		return no_page_table(vma, flags);

	if (is_hugepd(__hugepd(p4d_val(*p4d)))) {
		page = follow_huge_pd(vma, address,
				      __hugepd(p4d_val(*p4d)), flags,
				      P4D_SHIFT);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}
	return follow_pud_mask(vma, address, p4d, flags, ctx);
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
	struct page *page;
	struct mm_struct *mm = vma->vm_mm;

	ctx->page_mask = 0;

	/* make this handle hugepd */
	page = follow_huge_addr(mm, address, flags & FOLL_WRITE);
	if (!IS_ERR(page)) {
		WARN_ON_ONCE(flags & (FOLL_GET | FOLL_PIN));
		return page;
	}

	pgd = pgd_offset(mm, address);

	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		return no_page_table(vma, flags);

	if (pgd_huge(*pgd)) {
		page = follow_huge_pgd(mm, address, pgd, flags);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}
	if (is_hugepd(__hugepd(pgd_val(*pgd)))) {
		page = follow_huge_pd(vma, address,
				      __hugepd(pgd_val(*pgd)), flags,
				      PGDIR_SHIFT);
		if (page)
			return page;
		return no_page_table(vma, flags);
	}

	return follow_p4d_mask(vma, address, pgd, flags, ctx);
}

struct page *follow_page(struct vm_area_struct *vma, unsigned long address,
			 unsigned int foll_flags)
{
	struct follow_page_context ctx = { NULL };
	struct page *page;

	if (vma_is_secretmem(vma))
		return NULL;

	page = follow_page_mask(vma, address, foll_flags, &ctx);
	if (ctx.pgmap)
		put_dev_pagemap(ctx.pgmap);
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
	VM_BUG_ON(pmd_trans_huge(*pmd));
	pte = pte_offset_map(pmd, address);
	if (pte_none(*pte))
		goto unmap;
	*vma = get_gate_vma(mm);
	if (!page)
		goto out;
	*page = vm_normal_page(*vma, address, *pte);
	if (!*page) {
		if ((gup_flags & FOLL_DUMP) || !is_zero_pfn(pte_pfn(*pte)))
			goto unmap;
		*page = pte_page(*pte);
	}
	if (unlikely(!try_grab_page(*page, gup_flags))) {
		ret = -ENOMEM;
		goto unmap;
	}
out:
	ret = 0;
unmap:
	pte_unmap(pte);
	return ret;
}

/*
 * mmap_lock must be held on entry.  If @locked != NULL and *@flags
 * does not include FOLL_NOWAIT, the mmap_lock may be released.  If it
 * is, *@locked will be set to 0 and -EBUSY returned.
 */
static int faultin_page(struct vm_area_struct *vma,
		unsigned long address, unsigned int *flags, int *locked)
{
	unsigned int fault_flags = 0;
	vm_fault_t ret;

	/* mlock all present pages, but do not fault in new pages */
	if ((*flags & (FOLL_POPULATE | FOLL_MLOCK)) == FOLL_MLOCK)
		return -ENOENT;
	if (*flags & FOLL_NOFAULT)
		return -EFAULT;
	if (*flags & FOLL_WRITE)
		fault_flags |= FAULT_FLAG_WRITE;
	if (*flags & FOLL_REMOTE)
		fault_flags |= FAULT_FLAG_REMOTE;
	if (locked)
		fault_flags |= FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;
	if (*flags & FOLL_NOWAIT)
		fault_flags |= FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_RETRY_NOWAIT;
	if (*flags & FOLL_TRIED) {
		/*
		 * Note: FAULT_FLAG_ALLOW_RETRY and FAULT_FLAG_TRIED
		 * can co-exist
		 */
		fault_flags |= FAULT_FLAG_TRIED;
	}

	ret = handle_mm_fault(vma, address, fault_flags, NULL);
	if (ret & VM_FAULT_ERROR) {
		int err = vm_fault_to_errno(ret, *flags);

		if (err)
			return err;
		BUG();
	}

	if (ret & VM_FAULT_RETRY) {
		if (locked && !(fault_flags & FAULT_FLAG_RETRY_NOWAIT))
			*locked = 0;
		return -EBUSY;
	}

	/*
	 * The VM_FAULT_WRITE bit tells us that do_wp_page has broken COW when
	 * necessary, even if maybe_mkwrite decided not to set pte_write. We
	 * can thus safely do subsequent page lookups as if they were reads.
	 * But only do so when looping for pte_write is futile: in some cases
	 * userspace may also be wanting to write to the gotten user page,
	 * which a read fault here might prevent (a readonly page might get
	 * reCOWed by userspace write).
	 */
	if ((ret & VM_FAULT_WRITE) && !(vma->vm_flags & VM_WRITE))
		*flags |= FOLL_COW;
	return 0;
}

static int check_vma_flags(struct vm_area_struct *vma, unsigned long gup_flags)
{
	vm_flags_t vm_flags = vma->vm_flags;
	int write = (gup_flags & FOLL_WRITE);
	int foreign = (gup_flags & FOLL_REMOTE);

	if (vm_flags & (VM_IO | VM_PFNMAP))
		return -EFAULT;

	if (gup_flags & FOLL_ANON && !vma_is_anonymous(vma))
		return -EFAULT;

	if ((gup_flags & FOLL_LONGTERM) && vma_is_fsdax(vma))
		return -EOPNOTSUPP;

	if (vma_is_secretmem(vma))
		return -EFAULT;

	if (write) {
		if (!(vm_flags & VM_WRITE)) {
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

/**
 * __get_user_pages() - pin user pages in memory
 * @mm:		mm_struct of target mm
 * @start:	starting user address
 * @nr_pages:	number of pages from start to pin
 * @gup_flags:	flags modifying pin behaviour
 * @pages:	array that receives pointers to the pages pinned.
 *		Should be at least nr_pages long. Or NULL, if caller
 *		only intends to ensure the pages are faulted in.
 * @vmas:	array of pointers to vmas corresponding to each page.
 *		Or NULL if the caller does not require them.
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
 * @vmas are valid only as long as mmap_lock is held.
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
 * and subsequently re faulted). However it does guarantee that the page
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
 * If @locked != NULL, *@locked will be set to 0 when mmap_lock is
 * released by an up_read().  That can happen if @gup_flags does not
 * have FOLL_NOWAIT.
 *
 * A caller using such a combination of @locked and @gup_flags
 * must therefore hold the mmap_lock for reading only, and recognize
 * when it's been released.  Otherwise, it must be held for either
 * reading or writing and will not be released.
 *
 * In most cases, get_user_pages or get_user_pages_fast should be used
 * instead of __get_user_pages. __get_user_pages should be used only if
 * you need some special @gup_flags.
 */
static long __get_user_pages(struct mm_struct *mm,
		unsigned long start, unsigned long nr_pages,
		unsigned int gup_flags, struct page **pages,
		struct vm_area_struct **vmas, int *locked)
{
	long ret = 0, i = 0;
	struct vm_area_struct *vma = NULL;
	struct follow_page_context ctx = { NULL };

	if (!nr_pages)
		return 0;

	start = untagged_addr(start);

	VM_BUG_ON(!!pages != !!(gup_flags & (FOLL_GET | FOLL_PIN)));

	/*
	 * If FOLL_FORCE is set then do not force a full fault as the hinting
	 * fault information is unrelated to the reference behaviour of a task
	 * using the address space
	 */
	if (!(gup_flags & FOLL_FORCE))
		gup_flags |= FOLL_NUMA;

	do {
		struct page *page;
		unsigned int foll_flags = gup_flags;
		unsigned int page_increm;

		/* first iteration or cross vma bound */
		if (!vma || start >= vma->vm_end) {
			vma = find_extend_vma(mm, start);
			if (!vma && in_gate_area(mm, start)) {
				ret = get_gate_page(mm, start & PAGE_MASK,
						gup_flags, &vma,
						pages ? &pages[i] : NULL);
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

			if (is_vm_hugetlb_page(vma)) {
				i = follow_hugetlb_page(mm, vma, pages, vmas,
						&start, &nr_pages, i,
						gup_flags, locked);
				if (locked && *locked == 0) {
					/*
					 * We've got a VM_FAULT_RETRY
					 * and we've lost mmap_lock.
					 * We must stop here.
					 */
					BUG_ON(gup_flags & FOLL_NOWAIT);
					goto out;
				}
				continue;
			}
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

		page = follow_page_mask(vma, start, foll_flags, &ctx);
		if (!page) {
			ret = faultin_page(vma, start, &foll_flags, locked);
			switch (ret) {
			case 0:
				goto retry;
			case -EBUSY:
				ret = 0;
				fallthrough;
			case -EFAULT:
			case -ENOMEM:
			case -EHWPOISON:
				goto out;
			case -ENOENT:
				goto next_page;
			}
			BUG();
		} else if (PTR_ERR(page) == -EEXIST) {
			/*
			 * Proper page table entry exists, but no corresponding
			 * struct page.
			 */
			goto next_page;
		} else if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			goto out;
		}
		if (pages) {
			pages[i] = page;
			flush_anon_page(vma, page, start);
			flush_dcache_page(page);
			ctx.page_mask = 0;
		}
next_page:
		if (vmas) {
			vmas[i] = vma;
			ctx.page_mask = 0;
		}
		page_increm = 1 + (~(start >> PAGE_SHIFT) & ctx.page_mask);
		if (page_increm > nr_pages)
			page_increm = nr_pages;
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

	address = untagged_addr(address);

	if (unlocked)
		fault_flags |= FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;

retry:
	vma = find_extend_vma(mm, address);
	if (!vma || address < vma->vm_start)
		return -EFAULT;

	if (!vma_permits_fault(vma, fault_flags))
		return -EFAULT;

	if ((fault_flags & FAULT_FLAG_KILLABLE) &&
	    fatal_signal_pending(current))
		return -EINTR;

	ret = handle_mm_fault(vma, address, fault_flags, NULL);
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
 * Please note that this function, unlike __get_user_pages will not
 * return 0 for nr_pages > 0 without FOLL_NOWAIT
 */
static __always_inline long __get_user_pages_locked(struct mm_struct *mm,
						unsigned long start,
						unsigned long nr_pages,
						struct page **pages,
						struct vm_area_struct **vmas,
						int *locked,
						unsigned int flags)
{
	long ret, pages_done;
	bool lock_dropped;

	if (locked) {
		/* if VM_FAULT_RETRY can be returned, vmas become invalid */
		BUG_ON(vmas);
		/* check caller initialized locked */
		BUG_ON(*locked != 1);
	}

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
	lock_dropped = false;
	for (;;) {
		ret = __get_user_pages(mm, start, nr_pages, flags, pages,
				       vmas, locked);
		if (!locked)
			/* VM_FAULT_RETRY couldn't trigger, bypass */
			return ret;

		/* VM_FAULT_RETRY cannot return errors */
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
		lock_dropped = true;

retry:
		/*
		 * Repeat on the address that fired VM_FAULT_RETRY
		 * with both FAULT_FLAG_ALLOW_RETRY and
		 * FAULT_FLAG_TRIED.  Note that GUP can be interrupted
		 * by fatal signals, so we need to check it before we
		 * start trying again otherwise it can loop forever.
		 */

		if (fatal_signal_pending(current)) {
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
				       pages, NULL, locked);
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
	if (lock_dropped && *locked) {
		/*
		 * We must let the caller know we temporarily dropped the lock
		 * and so the critical section protected by it was lost.
		 */
		mmap_read_unlock(mm);
		*locked = 0;
	}
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
	int gup_flags;

	VM_BUG_ON(!PAGE_ALIGNED(start));
	VM_BUG_ON(!PAGE_ALIGNED(end));
	VM_BUG_ON_VMA(start < vma->vm_start, vma);
	VM_BUG_ON_VMA(end   > vma->vm_end, vma);
	mmap_assert_locked(mm);

	gup_flags = FOLL_TOUCH | FOLL_POPULATE | FOLL_MLOCK;
	if (vma->vm_flags & VM_LOCKONFAULT)
		gup_flags &= ~FOLL_POPULATE;
	/*
	 * We want to touch writable mappings with a write fault in order
	 * to break COW, except for shared mappings because these don't COW
	 * and we would not want to dirty them for nothing.
	 */
	if ((vma->vm_flags & (VM_WRITE | VM_SHARED)) == VM_WRITE)
		gup_flags |= FOLL_WRITE;

	/*
	 * We want mlock to succeed for regions that have any permissions
	 * other than PROT_NONE.
	 */
	if (vma_is_accessible(vma))
		gup_flags |= FOLL_FORCE;

	/*
	 * We made sure addr is within a VMA, so the following will
	 * not result in a stack expansion that recurses back here.
	 */
	return __get_user_pages(mm, start, nr_pages, gup_flags,
				NULL, NULL, locked);
}

/*
 * faultin_vma_page_range() - populate (prefault) page tables inside the
 *			      given VMA range readable/writable
 *
 * This takes care of mlocking the pages, too, if VM_LOCKED is set.
 *
 * @vma: target vma
 * @start: start address
 * @end: end address
 * @write: whether to prefault readable or writable
 * @locked: whether the mmap_lock is still held
 *
 * Returns either number of processed pages in the vma, or a negative error
 * code on error (see __get_user_pages()).
 *
 * vma->vm_mm->mmap_lock must be held. The range must be page-aligned and
 * covered by the VMA.
 *
 * If @locked is NULL, it may be held for read or write and will be unperturbed.
 *
 * If @locked is non-NULL, it must held for read only and may be released.  If
 * it's released, *@locked will be set to 0.
 */
long faultin_vma_page_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end, bool write, int *locked)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long nr_pages = (end - start) / PAGE_SIZE;
	int gup_flags;

	VM_BUG_ON(!PAGE_ALIGNED(start));
	VM_BUG_ON(!PAGE_ALIGNED(end));
	VM_BUG_ON_VMA(start < vma->vm_start, vma);
	VM_BUG_ON_VMA(end > vma->vm_end, vma);
	mmap_assert_locked(mm);

	/*
	 * FOLL_TOUCH: Mark page accessed and thereby young; will also mark
	 *	       the page dirty with FOLL_WRITE -- which doesn't make a
	 *	       difference with !FOLL_FORCE, because the page is writable
	 *	       in the page table.
	 * FOLL_HWPOISON: Return -EHWPOISON instead of -EFAULT when we hit
	 *		  a poisoned page.
	 * FOLL_POPULATE: Always populate memory with VM_LOCKONFAULT.
	 * !FOLL_FORCE: Require proper access permissions.
	 */
	gup_flags = FOLL_TOUCH | FOLL_POPULATE | FOLL_MLOCK | FOLL_HWPOISON;
	if (write)
		gup_flags |= FOLL_WRITE;

	/*
	 * We want to report -EINVAL instead of -EFAULT for any permission
	 * problems or incompatible mappings.
	 */
	if (check_vma_flags(vma, gup_flags))
		return -EINVAL;

	return __get_user_pages(mm, start, nr_pages, gup_flags,
				NULL, NULL, locked);
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
			vma = find_vma(mm, nstart);
		} else if (nstart >= vma->vm_end)
			vma = vma->vm_next;
		if (!vma || vma->vm_start >= end)
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
		struct vm_area_struct **vmas, int *locked,
		unsigned int foll_flags)
{
	struct vm_area_struct *vma;
	unsigned long vm_flags;
	long i;

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
			goto finish_or_fault;

		/* protect what we can, including chardevs */
		if ((vma->vm_flags & (VM_IO | VM_PFNMAP)) ||
		    !(vm_flags & vma->vm_flags))
			goto finish_or_fault;

		if (pages) {
			pages[i] = virt_to_page(start);
			if (pages[i])
				get_page(pages[i]);
		}
		if (vmas)
			vmas[i] = vma;
		start = (start + PAGE_SIZE) & PAGE_MASK;
	}

	return i;

finish_or_fault:
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
	if (!PAGE_ALIGNED(uaddr)) {
		if (unlikely(__put_user(0, uaddr) != 0))
			return size;
		uaddr = (char __user *)PAGE_ALIGN((unsigned long)uaddr);
	}
	end = (char __user *)PAGE_ALIGN((unsigned long)start + size);
	if (unlikely(end < start))
		end = NULL;
	while (uaddr != end) {
		if (unlikely(__put_user(0, uaddr) != 0))
			goto out;
		uaddr += PAGE_SIZE;
	}

out:
	if (size > uaddr - start)
		return size - (uaddr - start);
	return 0;
}
EXPORT_SYMBOL(fault_in_writeable);

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
	if (!PAGE_ALIGNED(uaddr)) {
		if (unlikely(__get_user(c, uaddr) != 0))
			return size;
		uaddr = (const char __user *)PAGE_ALIGN((unsigned long)uaddr);
	}
	end = (const char __user *)PAGE_ALIGN((unsigned long)start + size);
	if (unlikely(end < start))
		end = NULL;
	while (uaddr != end) {
		if (unlikely(__get_user(c, uaddr) != 0))
			goto out;
		uaddr += PAGE_SIZE;
	}

out:
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
	struct mm_struct *mm = current->mm;
	struct page *page;
	int locked = 1;
	int ret;

	if (mmap_read_lock_killable(mm))
		return NULL;
	ret = __get_user_pages_locked(mm, addr, 1, &page, NULL, &locked,
				      FOLL_FORCE | FOLL_DUMP | FOLL_GET);
	if (locked)
		mmap_read_unlock(mm);
	return (ret == 1) ? page : NULL;
}
#endif /* CONFIG_ELF_CORE */

#ifdef CONFIG_MIGRATION
/*
 * Check whether all pages are pinnable, if so return number of pages.  If some
 * pages are not pinnable, migrate them, and unpin all pages. Return zero if
 * pages were migrated, or if some pages were not successfully isolated.
 * Return negative error if migration fails.
 */
static long check_and_migrate_movable_pages(unsigned long nr_pages,
					    struct page **pages,
					    unsigned int gup_flags)
{
	unsigned long i;
	unsigned long isolation_error_count = 0;
	bool drain_allow = true;
	LIST_HEAD(movable_page_list);
	long ret = 0;
	struct page *prev_head = NULL;
	struct page *head;
	struct migration_target_control mtc = {
		.nid = NUMA_NO_NODE,
		.gfp_mask = GFP_USER | __GFP_NOWARN,
	};

	for (i = 0; i < nr_pages; i++) {
		head = compound_head(pages[i]);
		if (head == prev_head)
			continue;
		prev_head = head;
		/*
		 * If we get a movable page, since we are going to be pinning
		 * these entries, try to move them out if possible.
		 */
		if (!is_pinnable_page(head)) {
			if (PageHuge(head)) {
				if (!isolate_huge_page(head, &movable_page_list))
					isolation_error_count++;
			} else {
				if (!PageLRU(head) && drain_allow) {
					lru_add_drain_all();
					drain_allow = false;
				}

				if (isolate_lru_page(head)) {
					isolation_error_count++;
					continue;
				}
				list_add_tail(&head->lru, &movable_page_list);
				mod_node_page_state(page_pgdat(head),
						    NR_ISOLATED_ANON +
						    page_is_file_lru(head),
						    thp_nr_pages(head));
			}
		}
	}

	/*
	 * If list is empty, and no isolation errors, means that all pages are
	 * in the correct zone.
	 */
	if (list_empty(&movable_page_list) && !isolation_error_count)
		return nr_pages;

	if (gup_flags & FOLL_PIN) {
		unpin_user_pages(pages, nr_pages);
	} else {
		for (i = 0; i < nr_pages; i++)
			put_page(pages[i]);
	}
	if (!list_empty(&movable_page_list)) {
		ret = migrate_pages(&movable_page_list, alloc_migration_target,
				    NULL, (unsigned long)&mtc, MIGRATE_SYNC,
				    MR_LONGTERM_PIN, NULL);
		if (ret && !list_empty(&movable_page_list))
			putback_movable_pages(&movable_page_list);
	}

	return ret > 0 ? -ENOMEM : ret;
}
#else
static long check_and_migrate_movable_pages(unsigned long nr_pages,
					    struct page **pages,
					    unsigned int gup_flags)
{
	return nr_pages;
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
				  struct vm_area_struct **vmas,
				  unsigned int gup_flags)
{
	unsigned int flags;
	long rc;

	if (!(gup_flags & FOLL_LONGTERM))
		return __get_user_pages_locked(mm, start, nr_pages, pages, vmas,
					       NULL, gup_flags);
	flags = memalloc_pin_save();
	do {
		rc = __get_user_pages_locked(mm, start, nr_pages, pages, vmas,
					     NULL, gup_flags);
		if (rc <= 0)
			break;
		rc = check_and_migrate_movable_pages(rc, pages, gup_flags);
	} while (!rc);
	memalloc_pin_restore(flags);

	return rc;
}

static bool is_valid_gup_flags(unsigned int gup_flags)
{
	/*
	 * FOLL_PIN must only be set internally by the pin_user_pages*() APIs,
	 * never directly by the caller, so enforce that with an assertion:
	 */
	if (WARN_ON_ONCE(gup_flags & FOLL_PIN))
		return false;
	/*
	 * FOLL_PIN is a prerequisite to FOLL_LONGTERM. Another way of saying
	 * that is, FOLL_LONGTERM is a specific case, more restrictive case of
	 * FOLL_PIN.
	 */
	if (WARN_ON_ONCE(gup_flags & FOLL_LONGTERM))
		return false;

	return true;
}

#ifdef CONFIG_MMU
static long __get_user_pages_remote(struct mm_struct *mm,
				    unsigned long start, unsigned long nr_pages,
				    unsigned int gup_flags, struct page **pages,
				    struct vm_area_struct **vmas, int *locked)
{
	/*
	 * Parts of FOLL_LONGTERM behavior are incompatible with
	 * FAULT_FLAG_ALLOW_RETRY because of the FS DAX check requirement on
	 * vmas. However, this only comes up if locked is set, and there are
	 * callers that do request FOLL_LONGTERM, but do not set locked. So,
	 * allow what we can.
	 */
	if (gup_flags & FOLL_LONGTERM) {
		if (WARN_ON_ONCE(locked))
			return -EINVAL;
		/*
		 * This will check the vmas (even if our vmas arg is NULL)
		 * and return -ENOTSUPP if DAX isn't allowed in this case:
		 */
		return __gup_longterm_locked(mm, start, nr_pages, pages,
					     vmas, gup_flags | FOLL_TOUCH |
					     FOLL_REMOTE);
	}

	return __get_user_pages_locked(mm, start, nr_pages, pages, vmas,
				       locked,
				       gup_flags | FOLL_TOUCH | FOLL_REMOTE);
}

/**
 * get_user_pages_remote() - pin user pages in memory
 * @mm:		mm_struct of target mm
 * @start:	starting user address
 * @nr_pages:	number of pages from start to pin
 * @gup_flags:	flags modifying lookup behaviour
 * @pages:	array that receives pointers to the pages pinned.
 *		Should be at least nr_pages long. Or NULL, if caller
 *		only intends to ensure the pages are faulted in.
 * @vmas:	array of pointers to vmas corresponding to each page.
 *		Or NULL if the caller does not require them.
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
 * @vmas are valid only as long as mmap_lock is held.
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
 * and subsequently re faulted). However it does guarantee that the page
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
		struct vm_area_struct **vmas, int *locked)
{
	if (!is_valid_gup_flags(gup_flags))
		return -EINVAL;

	return __get_user_pages_remote(mm, start, nr_pages, gup_flags,
				       pages, vmas, locked);
}
EXPORT_SYMBOL(get_user_pages_remote);

#else /* CONFIG_MMU */
long get_user_pages_remote(struct mm_struct *mm,
			   unsigned long start, unsigned long nr_pages,
			   unsigned int gup_flags, struct page **pages,
			   struct vm_area_struct **vmas, int *locked)
{
	return 0;
}

static long __get_user_pages_remote(struct mm_struct *mm,
				    unsigned long start, unsigned long nr_pages,
				    unsigned int gup_flags, struct page **pages,
				    struct vm_area_struct **vmas, int *locked)
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
 * @vmas:       array of pointers to vmas corresponding to each page.
 *              Or NULL if the caller does not require them.
 *
 * This is the same as get_user_pages_remote(), just with a less-flexible
 * calling convention where we assume that the mm being operated on belongs to
 * the current task, and doesn't allow passing of a locked parameter.  We also
 * obviously don't pass FOLL_REMOTE in here.
 */
long get_user_pages(unsigned long start, unsigned long nr_pages,
		unsigned int gup_flags, struct page **pages,
		struct vm_area_struct **vmas)
{
	if (!is_valid_gup_flags(gup_flags))
		return -EINVAL;

	return __gup_longterm_locked(current->mm, start, nr_pages,
				     pages, vmas, gup_flags | FOLL_TOUCH);
}
EXPORT_SYMBOL(get_user_pages);

/**
 * get_user_pages_locked() - variant of get_user_pages()
 *
 * @start:      starting user address
 * @nr_pages:   number of pages from start to pin
 * @gup_flags:  flags modifying lookup behaviour
 * @pages:      array that receives pointers to the pages pinned.
 *              Should be at least nr_pages long. Or NULL, if caller
 *              only intends to ensure the pages are faulted in.
 * @locked:     pointer to lock flag indicating whether lock is held and
 *              subsequently whether VM_FAULT_RETRY functionality can be
 *              utilised. Lock must initially be held.
 *
 * It is suitable to replace the form:
 *
 *      mmap_read_lock(mm);
 *      do_something()
 *      get_user_pages(mm, ..., pages, NULL);
 *      mmap_read_unlock(mm);
 *
 *  to:
 *
 *      int locked = 1;
 *      mmap_read_lock(mm);
 *      do_something()
 *      get_user_pages_locked(mm, ..., pages, &locked);
 *      if (locked)
 *          mmap_read_unlock(mm);
 *
 * We can leverage the VM_FAULT_RETRY functionality in the page fault
 * paths better by using either get_user_pages_locked() or
 * get_user_pages_unlocked().
 *
 */
long get_user_pages_locked(unsigned long start, unsigned long nr_pages,
			   unsigned int gup_flags, struct page **pages,
			   int *locked)
{
	/*
	 * FIXME: Current FOLL_LONGTERM behavior is incompatible with
	 * FAULT_FLAG_ALLOW_RETRY because of the FS DAX check requirement on
	 * vmas.  As there are no users of this flag in this call we simply
	 * disallow this option for now.
	 */
	if (WARN_ON_ONCE(gup_flags & FOLL_LONGTERM))
		return -EINVAL;
	/*
	 * FOLL_PIN must only be set internally by the pin_user_pages*() APIs,
	 * never directly by the caller, so enforce that:
	 */
	if (WARN_ON_ONCE(gup_flags & FOLL_PIN))
		return -EINVAL;

	return __get_user_pages_locked(current->mm, start, nr_pages,
				       pages, NULL, locked,
				       gup_flags | FOLL_TOUCH);
}
EXPORT_SYMBOL(get_user_pages_locked);

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
	struct mm_struct *mm = current->mm;
	int locked = 1;
	long ret;

	/*
	 * FIXME: Current FOLL_LONGTERM behavior is incompatible with
	 * FAULT_FLAG_ALLOW_RETRY because of the FS DAX check requirement on
	 * vmas.  As there are no users of this flag in this call we simply
	 * disallow this option for now.
	 */
	if (WARN_ON_ONCE(gup_flags & FOLL_LONGTERM))
		return -EINVAL;

	mmap_read_lock(mm);
	ret = __get_user_pages_locked(mm, start, nr_pages, pages, NULL,
				      &locked, gup_flags | FOLL_TOUCH);
	if (locked)
		mmap_read_unlock(mm);
	return ret;
}
EXPORT_SYMBOL(get_user_pages_unlocked);

/*
 * Fast GUP
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
 * pages. Disabling interrupts will allow the fast_gup walker to both block
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
 *  *) access_ok is sufficient to validate userspace address ranges.
 *
 * The last two assumptions can be relaxed by the addition of helper functions.
 *
 * This code is based heavily on the PowerPC implementation by Nick Piggin.
 */
#ifdef CONFIG_HAVE_FAST_GUP

static void __maybe_unused undo_dev_pagemap(int *nr, int nr_start,
					    unsigned int flags,
					    struct page **pages)
{
	while ((*nr) - nr_start) {
		struct page *page = pages[--(*nr)];

		ClearPageReferenced(page);
		if (flags & FOLL_PIN)
			unpin_user_page(page);
		else
			put_page(page);
	}
}

#ifdef CONFIG_ARCH_HAS_PTE_SPECIAL
/*
 * Fast-gup relies on pte change detection to avoid concurrent pgtable
 * operations.
 *
 * To pin the page, fast-gup needs to do below in order:
 * (1) pin the page (by prefetching pte), then (2) check pte not changed.
 *
 * For the rest of pgtable operations where pgtable updates can be racy
 * with fast-gup, we need to do (1) clear pte, then (2) check whether page
 * is pinned.
 *
 * Above will work for all pte-level operations, including THP split.
 *
 * For THP collapse, it's a bit more complicated because fast-gup may be
 * walking a pgtable page that is being freed (pte is still valid but pmd
 * can be cleared already).  To avoid race in such condition, we need to
 * also check pmd here to make sure pmd doesn't change (corresponds to
 * pmdp_collapse_flush() in the THP collapse code path).
 */
static int gup_pte_range(pmd_t pmd, pmd_t *pmdp, unsigned long addr,
			 unsigned long end, unsigned int flags,
			 struct page **pages, int *nr)
{
	struct dev_pagemap *pgmap = NULL;
	int nr_start = *nr, ret = 0;
	pte_t *ptep, *ptem;

	ptem = ptep = pte_offset_map(&pmd, addr);
	do {
		pte_t pte = ptep_get_lockless(ptep);
		struct page *head, *page;

		/*
		 * Similar to the PMD case below, NUMA hinting must take slow
		 * path using the pte_protnone check.
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
				undo_dev_pagemap(nr, nr_start, flags, pages);
				goto pte_unmap;
			}
		} else if (pte_special(pte))
			goto pte_unmap;

		VM_BUG_ON(!pfn_valid(pte_pfn(pte)));
		page = pte_page(pte);

		head = try_grab_compound_head(page, 1, flags);
		if (!head)
			goto pte_unmap;

		if (unlikely(page_is_secretmem(page))) {
			put_compound_head(head, 1, flags);
			goto pte_unmap;
		}

		if (unlikely(pmd_val(pmd) != pmd_val(*pmdp)) ||
		    unlikely(pte_val(pte) != pte_val(*ptep))) {
			put_compound_head(head, 1, flags);
			goto pte_unmap;
		}

		VM_BUG_ON_PAGE(compound_head(page) != head, page);

		/*
		 * We need to make the page accessible if and only if we are
		 * going to access its content (the FOLL_PIN case).  Please
		 * see Documentation/core-api/pin_user_pages.rst for
		 * details.
		 */
		if (flags & FOLL_PIN) {
			ret = arch_make_page_accessible(page);
			if (ret) {
				unpin_user_page(page);
				goto pte_unmap;
			}
		}
		SetPageReferenced(page);
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
 * useful to have gup_huge_pmd even if we can't operate on ptes.
 */
static int gup_pte_range(pmd_t pmd, pmd_t *pmdp, unsigned long addr,
			 unsigned long end, unsigned int flags,
			 struct page **pages, int *nr)
{
	return 0;
}
#endif /* CONFIG_ARCH_HAS_PTE_SPECIAL */

#if defined(CONFIG_ARCH_HAS_PTE_DEVMAP) && defined(CONFIG_TRANSPARENT_HUGEPAGE)
static int __gup_device_huge(unsigned long pfn, unsigned long addr,
			     unsigned long end, unsigned int flags,
			     struct page **pages, int *nr)
{
	int nr_start = *nr;
	struct dev_pagemap *pgmap = NULL;
	int ret = 1;

	do {
		struct page *page = pfn_to_page(pfn);

		pgmap = get_dev_pagemap(pfn, pgmap);
		if (unlikely(!pgmap)) {
			undo_dev_pagemap(nr, nr_start, flags, pages);
			ret = 0;
			break;
		}
		SetPageReferenced(page);
		pages[*nr] = page;
		if (unlikely(!try_grab_page(page, flags))) {
			undo_dev_pagemap(nr, nr_start, flags, pages);
			ret = 0;
			break;
		}
		(*nr)++;
		pfn++;
	} while (addr += PAGE_SIZE, addr != end);

	put_dev_pagemap(pgmap);
	return ret;
}

static int __gup_device_huge_pmd(pmd_t orig, pmd_t *pmdp, unsigned long addr,
				 unsigned long end, unsigned int flags,
				 struct page **pages, int *nr)
{
	unsigned long fault_pfn;
	int nr_start = *nr;

	fault_pfn = pmd_pfn(orig) + ((addr & ~PMD_MASK) >> PAGE_SHIFT);
	if (!__gup_device_huge(fault_pfn, addr, end, flags, pages, nr))
		return 0;

	if (unlikely(pmd_val(orig) != pmd_val(*pmdp))) {
		undo_dev_pagemap(nr, nr_start, flags, pages);
		return 0;
	}
	return 1;
}

static int __gup_device_huge_pud(pud_t orig, pud_t *pudp, unsigned long addr,
				 unsigned long end, unsigned int flags,
				 struct page **pages, int *nr)
{
	unsigned long fault_pfn;
	int nr_start = *nr;

	fault_pfn = pud_pfn(orig) + ((addr & ~PUD_MASK) >> PAGE_SHIFT);
	if (!__gup_device_huge(fault_pfn, addr, end, flags, pages, nr))
		return 0;

	if (unlikely(pud_val(orig) != pud_val(*pudp))) {
		undo_dev_pagemap(nr, nr_start, flags, pages);
		return 0;
	}
	return 1;
}
#else
static int __gup_device_huge_pmd(pmd_t orig, pmd_t *pmdp, unsigned long addr,
				 unsigned long end, unsigned int flags,
				 struct page **pages, int *nr)
{
	BUILD_BUG();
	return 0;
}

static int __gup_device_huge_pud(pud_t pud, pud_t *pudp, unsigned long addr,
				 unsigned long end, unsigned int flags,
				 struct page **pages, int *nr)
{
	BUILD_BUG();
	return 0;
}
#endif

static int record_subpages(struct page *page, unsigned long addr,
			   unsigned long end, struct page **pages)
{
	int nr;

	for (nr = 0; addr != end; addr += PAGE_SIZE)
		pages[nr++] = page++;

	return nr;
}

#ifdef CONFIG_ARCH_HAS_HUGEPD
static unsigned long hugepte_addr_end(unsigned long addr, unsigned long end,
				      unsigned long sz)
{
	unsigned long __boundary = (addr + sz) & ~(sz-1);
	return (__boundary - 1 < end - 1) ? __boundary : end;
}

static int gup_hugepte(pte_t *ptep, unsigned long sz, unsigned long addr,
		       unsigned long end, unsigned int flags,
		       struct page **pages, int *nr)
{
	unsigned long pte_end;
	struct page *head, *page;
	pte_t pte;
	int refs;

	pte_end = (addr + sz) & ~(sz-1);
	if (pte_end < end)
		end = pte_end;

	pte = huge_ptep_get(ptep);

	if (!pte_access_permitted(pte, flags & FOLL_WRITE))
		return 0;

	/* hugepages are never "special" */
	VM_BUG_ON(!pfn_valid(pte_pfn(pte)));

	head = pte_page(pte);
	page = head + ((addr & (sz-1)) >> PAGE_SHIFT);
	refs = record_subpages(page, addr, end, pages + *nr);

	head = try_grab_compound_head(head, refs, flags);
	if (!head)
		return 0;

	if (unlikely(pte_val(pte) != pte_val(*ptep))) {
		put_compound_head(head, refs, flags);
		return 0;
	}

	*nr += refs;
	SetPageReferenced(head);
	return 1;
}

static int gup_huge_pd(hugepd_t hugepd, unsigned long addr,
		unsigned int pdshift, unsigned long end, unsigned int flags,
		struct page **pages, int *nr)
{
	pte_t *ptep;
	unsigned long sz = 1UL << hugepd_shift(hugepd);
	unsigned long next;

	ptep = hugepte_offset(hugepd, addr, pdshift);
	do {
		next = hugepte_addr_end(addr, end, sz);
		if (!gup_hugepte(ptep, sz, addr, end, flags, pages, nr))
			return 0;
	} while (ptep++, addr = next, addr != end);

	return 1;
}
#else
static inline int gup_huge_pd(hugepd_t hugepd, unsigned long addr,
		unsigned int pdshift, unsigned long end, unsigned int flags,
		struct page **pages, int *nr)
{
	return 0;
}
#endif /* CONFIG_ARCH_HAS_HUGEPD */

static int gup_huge_pmd(pmd_t orig, pmd_t *pmdp, unsigned long addr,
			unsigned long end, unsigned int flags,
			struct page **pages, int *nr)
{
	struct page *head, *page;
	int refs;

	if (!pmd_access_permitted(orig, flags & FOLL_WRITE))
		return 0;

	if (pmd_devmap(orig)) {
		if (unlikely(flags & FOLL_LONGTERM))
			return 0;
		return __gup_device_huge_pmd(orig, pmdp, addr, end, flags,
					     pages, nr);
	}

	page = pmd_page(orig) + ((addr & ~PMD_MASK) >> PAGE_SHIFT);
	refs = record_subpages(page, addr, end, pages + *nr);

	head = try_grab_compound_head(pmd_page(orig), refs, flags);
	if (!head)
		return 0;

	if (unlikely(pmd_val(orig) != pmd_val(*pmdp))) {
		put_compound_head(head, refs, flags);
		return 0;
	}

	*nr += refs;
	SetPageReferenced(head);
	return 1;
}

static int gup_huge_pud(pud_t orig, pud_t *pudp, unsigned long addr,
			unsigned long end, unsigned int flags,
			struct page **pages, int *nr)
{
	struct page *head, *page;
	int refs;

	if (!pud_access_permitted(orig, flags & FOLL_WRITE))
		return 0;

	if (pud_devmap(orig)) {
		if (unlikely(flags & FOLL_LONGTERM))
			return 0;
		return __gup_device_huge_pud(orig, pudp, addr, end, flags,
					     pages, nr);
	}

	page = pud_page(orig) + ((addr & ~PUD_MASK) >> PAGE_SHIFT);
	refs = record_subpages(page, addr, end, pages + *nr);

	head = try_grab_compound_head(pud_page(orig), refs, flags);
	if (!head)
		return 0;

	if (unlikely(pud_val(orig) != pud_val(*pudp))) {
		put_compound_head(head, refs, flags);
		return 0;
	}

	*nr += refs;
	SetPageReferenced(head);
	return 1;
}

static int gup_huge_pgd(pgd_t orig, pgd_t *pgdp, unsigned long addr,
			unsigned long end, unsigned int flags,
			struct page **pages, int *nr)
{
	int refs;
	struct page *head, *page;

	if (!pgd_access_permitted(orig, flags & FOLL_WRITE))
		return 0;

	BUILD_BUG_ON(pgd_devmap(orig));

	page = pgd_page(orig) + ((addr & ~PGDIR_MASK) >> PAGE_SHIFT);
	refs = record_subpages(page, addr, end, pages + *nr);

	head = try_grab_compound_head(pgd_page(orig), refs, flags);
	if (!head)
		return 0;

	if (unlikely(pgd_val(orig) != pgd_val(*pgdp))) {
		put_compound_head(head, refs, flags);
		return 0;
	}

	*nr += refs;
	SetPageReferenced(head);
	return 1;
}

static int gup_pmd_range(pud_t *pudp, pud_t pud, unsigned long addr, unsigned long end,
		unsigned int flags, struct page **pages, int *nr)
{
	unsigned long next;
	pmd_t *pmdp;

	pmdp = pmd_offset_lockless(pudp, pud, addr);
	do {
		pmd_t pmd = READ_ONCE(*pmdp);

		next = pmd_addr_end(addr, end);
		if (!pmd_present(pmd))
			return 0;

		if (unlikely(pmd_trans_huge(pmd) || pmd_huge(pmd) ||
			     pmd_devmap(pmd))) {
			/*
			 * NUMA hinting faults need to be handled in the GUP
			 * slowpath for accounting purposes and so that they
			 * can be serialised against THP migration.
			 */
			if (pmd_protnone(pmd))
				return 0;

			if (!gup_huge_pmd(pmd, pmdp, addr, next, flags,
				pages, nr))
				return 0;

		} else if (unlikely(is_hugepd(__hugepd(pmd_val(pmd))))) {
			/*
			 * architecture have different format for hugetlbfs
			 * pmd format and THP pmd format
			 */
			if (!gup_huge_pd(__hugepd(pmd_val(pmd)), addr,
					 PMD_SHIFT, next, flags, pages, nr))
				return 0;
		} else if (!gup_pte_range(pmd, pmdp, addr, next, flags, pages, nr))
			return 0;
	} while (pmdp++, addr = next, addr != end);

	return 1;
}

static int gup_pud_range(p4d_t *p4dp, p4d_t p4d, unsigned long addr, unsigned long end,
			 unsigned int flags, struct page **pages, int *nr)
{
	unsigned long next;
	pud_t *pudp;

	pudp = pud_offset_lockless(p4dp, p4d, addr);
	do {
		pud_t pud = READ_ONCE(*pudp);

		next = pud_addr_end(addr, end);
		if (unlikely(!pud_present(pud)))
			return 0;
		if (unlikely(pud_huge(pud))) {
			if (!gup_huge_pud(pud, pudp, addr, next, flags,
					  pages, nr))
				return 0;
		} else if (unlikely(is_hugepd(__hugepd(pud_val(pud))))) {
			if (!gup_huge_pd(__hugepd(pud_val(pud)), addr,
					 PUD_SHIFT, next, flags, pages, nr))
				return 0;
		} else if (!gup_pmd_range(pudp, pud, addr, next, flags, pages, nr))
			return 0;
	} while (pudp++, addr = next, addr != end);

	return 1;
}

static int gup_p4d_range(pgd_t *pgdp, pgd_t pgd, unsigned long addr, unsigned long end,
			 unsigned int flags, struct page **pages, int *nr)
{
	unsigned long next;
	p4d_t *p4dp;

	p4dp = p4d_offset_lockless(pgdp, pgd, addr);
	do {
		p4d_t p4d = READ_ONCE(*p4dp);

		next = p4d_addr_end(addr, end);
		if (p4d_none(p4d))
			return 0;
		BUILD_BUG_ON(p4d_huge(p4d));
		if (unlikely(is_hugepd(__hugepd(p4d_val(p4d))))) {
			if (!gup_huge_pd(__hugepd(p4d_val(p4d)), addr,
					 P4D_SHIFT, next, flags, pages, nr))
				return 0;
		} else if (!gup_pud_range(p4dp, p4d, addr, next, flags, pages, nr))
			return 0;
	} while (p4dp++, addr = next, addr != end);

	return 1;
}

static void gup_pgd_range(unsigned long addr, unsigned long end,
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
		if (unlikely(pgd_huge(pgd))) {
			if (!gup_huge_pgd(pgd, pgdp, addr, next, flags,
					  pages, nr))
				return;
		} else if (unlikely(is_hugepd(__hugepd(pgd_val(pgd))))) {
			if (!gup_huge_pd(__hugepd(pgd_val(pgd)), addr,
					 PGDIR_SHIFT, next, flags, pages, nr))
				return;
		} else if (!gup_p4d_range(pgdp, pgd, addr, next, flags, pages, nr))
			return;
	} while (pgdp++, addr = next, addr != end);
}
#else
static inline void gup_pgd_range(unsigned long addr, unsigned long end,
		unsigned int flags, struct page **pages, int *nr)
{
}
#endif /* CONFIG_HAVE_FAST_GUP */

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

static int __gup_longterm_unlocked(unsigned long start, int nr_pages,
				   unsigned int gup_flags, struct page **pages)
{
	int ret;

	/*
	 * FIXME: FOLL_LONGTERM does not work with
	 * get_user_pages_unlocked() (see comments in that function)
	 */
	if (gup_flags & FOLL_LONGTERM) {
		mmap_read_lock(current->mm);
		ret = __gup_longterm_locked(current->mm,
					    start, nr_pages,
					    pages, NULL, gup_flags);
		mmap_read_unlock(current->mm);
	} else {
		ret = get_user_pages_unlocked(start, nr_pages,
					      pages, gup_flags);
	}

	return ret;
}

static unsigned long lockless_pages_from_mm(unsigned long start,
					    unsigned long end,
					    unsigned int gup_flags,
					    struct page **pages)
{
	unsigned long flags;
	int nr_pinned = 0;
	unsigned seq;

	if (!IS_ENABLED(CONFIG_HAVE_FAST_GUP) ||
	    !gup_fast_permitted(start, end))
		return 0;

	if (gup_flags & FOLL_PIN) {
		seq = raw_read_seqcount(&current->mm->write_protect_seq);
		if (seq & 1)
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
	gup_pgd_range(start, end, gup_flags, pages, &nr_pinned);
	local_irq_restore(flags);

	/*
	 * When pinning pages for DMA there could be a concurrent write protect
	 * from fork() via copy_page_range(), in this case always fail fast GUP.
	 */
	if (gup_flags & FOLL_PIN) {
		if (read_seqcount_retry(&current->mm->write_protect_seq, seq)) {
			unpin_user_pages(pages, nr_pinned);
			return 0;
		}
	}
	return nr_pinned;
}

static int internal_get_user_pages_fast(unsigned long start,
					unsigned long nr_pages,
					unsigned int gup_flags,
					struct page **pages)
{
	unsigned long len, end;
	unsigned long nr_pinned;
	int ret;

	if (WARN_ON_ONCE(gup_flags & ~(FOLL_WRITE | FOLL_LONGTERM |
				       FOLL_FORCE | FOLL_PIN | FOLL_GET |
				       FOLL_FAST_ONLY | FOLL_NOFAULT)))
		return -EINVAL;

	if (gup_flags & FOLL_PIN)
		mm_set_has_pinned_flag(&current->mm->flags);

	if (!(gup_flags & FOLL_FAST_ONLY))
		might_lock_read(&current->mm->mmap_lock);

	start = untagged_addr(start) & PAGE_MASK;
	len = nr_pages << PAGE_SHIFT;
	if (check_add_overflow(start, len, &end))
		return 0;
	if (unlikely(!access_ok((void __user *)start, len)))
		return -EFAULT;

	nr_pinned = lockless_pages_from_mm(start, end, gup_flags, pages);
	if (nr_pinned == nr_pages || gup_flags & FOLL_FAST_ONLY)
		return nr_pinned;

	/* Slow path: try to get the remaining pages with get_user_pages */
	start += nr_pinned << PAGE_SHIFT;
	pages += nr_pinned;
	ret = __gup_longterm_unlocked(start, nr_pages - nr_pinned, gup_flags,
				      pages);
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
 * Note a difference with get_user_pages_fast: this always returns the
 * number of pages pinned, 0 if no pages were pinned.
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
	int nr_pinned;
	/*
	 * Internally (within mm/gup.c), gup fast variants must set FOLL_GET,
	 * because gup fast is always a "pin with a +1 page refcount" request.
	 *
	 * FOLL_FAST_ONLY is required in order to match the API description of
	 * this routine: no fall back to regular ("slow") GUP.
	 */
	gup_flags |= FOLL_GET | FOLL_FAST_ONLY;

	nr_pinned = internal_get_user_pages_fast(start, nr_pages, gup_flags,
						 pages);

	/*
	 * As specified in the API description above, this routine is not
	 * allowed to return negative values. However, the common core
	 * routine internal_get_user_pages_fast() *can* return -errno.
	 * Therefore, correct for that here:
	 */
	if (nr_pinned < 0)
		nr_pinned = 0;

	return nr_pinned;
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
	if (!is_valid_gup_flags(gup_flags))
		return -EINVAL;

	/*
	 * The caller may or may not have explicitly set FOLL_GET; either way is
	 * OK. However, internally (within mm/gup.c), gup fast variants must set
	 * FOLL_GET, because gup fast is always a "pin with a +1 page refcount"
	 * request.
	 */
	gup_flags |= FOLL_GET;
	return internal_get_user_pages_fast(start, nr_pages, gup_flags, pages);
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
 */
int pin_user_pages_fast(unsigned long start, int nr_pages,
			unsigned int gup_flags, struct page **pages)
{
	/* FOLL_GET and FOLL_PIN are mutually exclusive. */
	if (WARN_ON_ONCE(gup_flags & FOLL_GET))
		return -EINVAL;

	gup_flags |= FOLL_PIN;
	return internal_get_user_pages_fast(start, nr_pages, gup_flags, pages);
}
EXPORT_SYMBOL_GPL(pin_user_pages_fast);

/*
 * This is the FOLL_PIN equivalent of get_user_pages_fast_only(). Behavior
 * is the same, except that this one sets FOLL_PIN instead of FOLL_GET.
 *
 * The API rules are the same, too: no negative values may be returned.
 */
int pin_user_pages_fast_only(unsigned long start, int nr_pages,
			     unsigned int gup_flags, struct page **pages)
{
	int nr_pinned;

	/*
	 * FOLL_GET and FOLL_PIN are mutually exclusive. Note that the API
	 * rules require returning 0, rather than -errno:
	 */
	if (WARN_ON_ONCE(gup_flags & FOLL_GET))
		return 0;
	/*
	 * FOLL_FAST_ONLY is required in order to match the API description of
	 * this routine: no fall back to regular ("slow") GUP.
	 */
	gup_flags |= (FOLL_PIN | FOLL_FAST_ONLY);
	nr_pinned = internal_get_user_pages_fast(start, nr_pages, gup_flags,
						 pages);
	/*
	 * This routine is not allowed to return negative values. However,
	 * internal_get_user_pages_fast() *can* return -errno. Therefore,
	 * correct for that here:
	 */
	if (nr_pinned < 0)
		nr_pinned = 0;

	return nr_pinned;
}
EXPORT_SYMBOL_GPL(pin_user_pages_fast_only);

/**
 * pin_user_pages_remote() - pin pages of a remote process
 *
 * @mm:		mm_struct of target mm
 * @start:	starting user address
 * @nr_pages:	number of pages from start to pin
 * @gup_flags:	flags modifying lookup behaviour
 * @pages:	array that receives pointers to the pages pinned.
 *		Should be at least nr_pages long. Or NULL, if caller
 *		only intends to ensure the pages are faulted in.
 * @vmas:	array of pointers to vmas corresponding to each page.
 *		Or NULL if the caller does not require them.
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
 */
long pin_user_pages_remote(struct mm_struct *mm,
			   unsigned long start, unsigned long nr_pages,
			   unsigned int gup_flags, struct page **pages,
			   struct vm_area_struct **vmas, int *locked)
{
	/* FOLL_GET and FOLL_PIN are mutually exclusive. */
	if (WARN_ON_ONCE(gup_flags & FOLL_GET))
		return -EINVAL;

	gup_flags |= FOLL_PIN;
	return __get_user_pages_remote(mm, start, nr_pages, gup_flags,
				       pages, vmas, locked);
}
EXPORT_SYMBOL(pin_user_pages_remote);

/**
 * pin_user_pages() - pin user pages in memory for use by other devices
 *
 * @start:	starting user address
 * @nr_pages:	number of pages from start to pin
 * @gup_flags:	flags modifying lookup behaviour
 * @pages:	array that receives pointers to the pages pinned.
 *		Should be at least nr_pages long. Or NULL, if caller
 *		only intends to ensure the pages are faulted in.
 * @vmas:	array of pointers to vmas corresponding to each page.
 *		Or NULL if the caller does not require them.
 *
 * Nearly the same as get_user_pages(), except that FOLL_TOUCH is not set, and
 * FOLL_PIN is set.
 *
 * FOLL_PIN means that the pages must be released via unpin_user_page(). Please
 * see Documentation/core-api/pin_user_pages.rst for details.
 */
long pin_user_pages(unsigned long start, unsigned long nr_pages,
		    unsigned int gup_flags, struct page **pages,
		    struct vm_area_struct **vmas)
{
	/* FOLL_GET and FOLL_PIN are mutually exclusive. */
	if (WARN_ON_ONCE(gup_flags & FOLL_GET))
		return -EINVAL;

	gup_flags |= FOLL_PIN;
	return __gup_longterm_locked(current->mm, start, nr_pages,
				     pages, vmas, gup_flags);
}
EXPORT_SYMBOL(pin_user_pages);

/*
 * pin_user_pages_unlocked() is the FOLL_PIN variant of
 * get_user_pages_unlocked(). Behavior is the same, except that this one sets
 * FOLL_PIN and rejects FOLL_GET.
 */
long pin_user_pages_unlocked(unsigned long start, unsigned long nr_pages,
			     struct page **pages, unsigned int gup_flags)
{
	/* FOLL_GET and FOLL_PIN are mutually exclusive. */
	if (WARN_ON_ONCE(gup_flags & FOLL_GET))
		return -EINVAL;

	gup_flags |= FOLL_PIN;
	return get_user_pages_unlocked(start, nr_pages, pages, gup_flags);
}
EXPORT_SYMBOL(pin_user_pages_unlocked);

/*
 * pin_user_pages_locked() is the FOLL_PIN variant of get_user_pages_locked().
 * Behavior is the same, except that this one sets FOLL_PIN and rejects
 * FOLL_GET.
 */
long pin_user_pages_locked(unsigned long start, unsigned long nr_pages,
			   unsigned int gup_flags, struct page **pages,
			   int *locked)
{
	/*
	 * FIXME: Current FOLL_LONGTERM behavior is incompatible with
	 * FAULT_FLAG_ALLOW_RETRY because of the FS DAX check requirement on
	 * vmas.  As there are no users of this flag in this call we simply
	 * disallow this option for now.
	 */
	if (WARN_ON_ONCE(gup_flags & FOLL_LONGTERM))
		return -EINVAL;

	/* FOLL_GET and FOLL_PIN are mutually exclusive. */
	if (WARN_ON_ONCE(gup_flags & FOLL_GET))
		return -EINVAL;

	gup_flags |= FOLL_PIN;
	return __get_user_pages_locked(current->mm, start, nr_pages,
				       pages, NULL, locked,
				       gup_flags | FOLL_TOUCH);
}
EXPORT_SYMBOL(pin_user_pages_locked);
