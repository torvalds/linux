// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/mmu_notifier.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/mm_inline.h>
#include <linux/kthread.h>
#include <linux/khugepaged.h>
#include <linux/freezer.h>
#include <linux/mman.h>
#include <linux/hashtable.h>
#include <linux/userfaultfd_k.h>
#include <linux/page_idle.h>
#include <linux/page_table_check.h>
#include <linux/swapops.h>
#include <linux/shmem_fs.h>
#include <linux/ksm.h>

#include <asm/tlb.h>
#include <asm/pgalloc.h>
#include "internal.h"
#include "mm_slot.h"

enum scan_result {
	SCAN_FAIL,
	SCAN_SUCCEED,
	SCAN_PMD_NULL,
	SCAN_PMD_NONE,
	SCAN_PMD_MAPPED,
	SCAN_EXCEED_NONE_PTE,
	SCAN_EXCEED_SWAP_PTE,
	SCAN_EXCEED_SHARED_PTE,
	SCAN_PTE_NON_PRESENT,
	SCAN_PTE_UFFD_WP,
	SCAN_PTE_MAPPED_HUGEPAGE,
	SCAN_PAGE_RO,
	SCAN_LACK_REFERENCED_PAGE,
	SCAN_PAGE_NULL,
	SCAN_SCAN_ABORT,
	SCAN_PAGE_COUNT,
	SCAN_PAGE_LRU,
	SCAN_PAGE_LOCK,
	SCAN_PAGE_ANON,
	SCAN_PAGE_COMPOUND,
	SCAN_ANY_PROCESS,
	SCAN_VMA_NULL,
	SCAN_VMA_CHECK,
	SCAN_ADDRESS_RANGE,
	SCAN_DEL_PAGE_LRU,
	SCAN_ALLOC_HUGE_PAGE_FAIL,
	SCAN_CGROUP_CHARGE_FAIL,
	SCAN_TRUNCATED,
	SCAN_PAGE_HAS_PRIVATE,
	SCAN_STORE_FAILED,
	SCAN_COPY_MC,
	SCAN_PAGE_FILLED,
};

#define CREATE_TRACE_POINTS
#include <trace/events/huge_memory.h>

static struct task_struct *khugepaged_thread __read_mostly;
static DEFINE_MUTEX(khugepaged_mutex);

/* default scan 8*512 pte (or vmas) every 30 second */
static unsigned int khugepaged_pages_to_scan __read_mostly;
static unsigned int khugepaged_pages_collapsed;
static unsigned int khugepaged_full_scans;
static unsigned int khugepaged_scan_sleep_millisecs __read_mostly = 10000;
/* during fragmentation poll the hugepage allocator once every minute */
static unsigned int khugepaged_alloc_sleep_millisecs __read_mostly = 60000;
static unsigned long khugepaged_sleep_expire;
static DEFINE_SPINLOCK(khugepaged_mm_lock);
static DECLARE_WAIT_QUEUE_HEAD(khugepaged_wait);
/*
 * default collapse hugepages if there is at least one pte mapped like
 * it would have happened if the vma was large enough during page
 * fault.
 *
 * Note that these are only respected if collapse was initiated by khugepaged.
 */
static unsigned int khugepaged_max_ptes_none __read_mostly;
static unsigned int khugepaged_max_ptes_swap __read_mostly;
static unsigned int khugepaged_max_ptes_shared __read_mostly;

#define MM_SLOTS_HASH_BITS 10
static DEFINE_READ_MOSTLY_HASHTABLE(mm_slots_hash, MM_SLOTS_HASH_BITS);

static struct kmem_cache *mm_slot_cache __ro_after_init;

struct collapse_control {
	bool is_khugepaged;

	/* Num pages scanned per node */
	u32 node_load[MAX_NUMNODES];

	/* nodemask for allocation fallback */
	nodemask_t alloc_nmask;
};

/**
 * struct khugepaged_mm_slot - khugepaged information per mm that is being scanned
 * @slot: hash lookup from mm to mm_slot
 */
struct khugepaged_mm_slot {
	struct mm_slot slot;
};

/**
 * struct khugepaged_scan - cursor for scanning
 * @mm_head: the head of the mm list to scan
 * @mm_slot: the current mm_slot we are scanning
 * @address: the next address inside that to be scanned
 *
 * There is only the one khugepaged_scan instance of this cursor structure.
 */
struct khugepaged_scan {
	struct list_head mm_head;
	struct khugepaged_mm_slot *mm_slot;
	unsigned long address;
};

static struct khugepaged_scan khugepaged_scan = {
	.mm_head = LIST_HEAD_INIT(khugepaged_scan.mm_head),
};

#ifdef CONFIG_SYSFS
static ssize_t scan_sleep_millisecs_show(struct kobject *kobj,
					 struct kobj_attribute *attr,
					 char *buf)
{
	return sysfs_emit(buf, "%u\n", khugepaged_scan_sleep_millisecs);
}

static ssize_t scan_sleep_millisecs_store(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  const char *buf, size_t count)
{
	unsigned int msecs;
	int err;

	err = kstrtouint(buf, 10, &msecs);
	if (err)
		return -EINVAL;

	khugepaged_scan_sleep_millisecs = msecs;
	khugepaged_sleep_expire = 0;
	wake_up_interruptible(&khugepaged_wait);

	return count;
}
static struct kobj_attribute scan_sleep_millisecs_attr =
	__ATTR_RW(scan_sleep_millisecs);

static ssize_t alloc_sleep_millisecs_show(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buf)
{
	return sysfs_emit(buf, "%u\n", khugepaged_alloc_sleep_millisecs);
}

static ssize_t alloc_sleep_millisecs_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	unsigned int msecs;
	int err;

	err = kstrtouint(buf, 10, &msecs);
	if (err)
		return -EINVAL;

	khugepaged_alloc_sleep_millisecs = msecs;
	khugepaged_sleep_expire = 0;
	wake_up_interruptible(&khugepaged_wait);

	return count;
}
static struct kobj_attribute alloc_sleep_millisecs_attr =
	__ATTR_RW(alloc_sleep_millisecs);

static ssize_t pages_to_scan_show(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  char *buf)
{
	return sysfs_emit(buf, "%u\n", khugepaged_pages_to_scan);
}
static ssize_t pages_to_scan_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	unsigned int pages;
	int err;

	err = kstrtouint(buf, 10, &pages);
	if (err || !pages)
		return -EINVAL;

	khugepaged_pages_to_scan = pages;

	return count;
}
static struct kobj_attribute pages_to_scan_attr =
	__ATTR_RW(pages_to_scan);

static ssize_t pages_collapsed_show(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    char *buf)
{
	return sysfs_emit(buf, "%u\n", khugepaged_pages_collapsed);
}
static struct kobj_attribute pages_collapsed_attr =
	__ATTR_RO(pages_collapsed);

static ssize_t full_scans_show(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       char *buf)
{
	return sysfs_emit(buf, "%u\n", khugepaged_full_scans);
}
static struct kobj_attribute full_scans_attr =
	__ATTR_RO(full_scans);

static ssize_t defrag_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	return single_hugepage_flag_show(kobj, attr, buf,
					 TRANSPARENT_HUGEPAGE_DEFRAG_KHUGEPAGED_FLAG);
}
static ssize_t defrag_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	return single_hugepage_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_DEFRAG_KHUGEPAGED_FLAG);
}
static struct kobj_attribute khugepaged_defrag_attr =
	__ATTR_RW(defrag);

/*
 * max_ptes_none controls if khugepaged should collapse hugepages over
 * any unmapped ptes in turn potentially increasing the memory
 * footprint of the vmas. When max_ptes_none is 0 khugepaged will not
 * reduce the available free memory in the system as it
 * runs. Increasing max_ptes_none will instead potentially reduce the
 * free memory in the system during the khugepaged scan.
 */
static ssize_t max_ptes_none_show(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  char *buf)
{
	return sysfs_emit(buf, "%u\n", khugepaged_max_ptes_none);
}
static ssize_t max_ptes_none_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	int err;
	unsigned long max_ptes_none;

	err = kstrtoul(buf, 10, &max_ptes_none);
	if (err || max_ptes_none > HPAGE_PMD_NR - 1)
		return -EINVAL;

	khugepaged_max_ptes_none = max_ptes_none;

	return count;
}
static struct kobj_attribute khugepaged_max_ptes_none_attr =
	__ATTR_RW(max_ptes_none);

static ssize_t max_ptes_swap_show(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  char *buf)
{
	return sysfs_emit(buf, "%u\n", khugepaged_max_ptes_swap);
}

static ssize_t max_ptes_swap_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	int err;
	unsigned long max_ptes_swap;

	err  = kstrtoul(buf, 10, &max_ptes_swap);
	if (err || max_ptes_swap > HPAGE_PMD_NR - 1)
		return -EINVAL;

	khugepaged_max_ptes_swap = max_ptes_swap;

	return count;
}

static struct kobj_attribute khugepaged_max_ptes_swap_attr =
	__ATTR_RW(max_ptes_swap);

static ssize_t max_ptes_shared_show(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    char *buf)
{
	return sysfs_emit(buf, "%u\n", khugepaged_max_ptes_shared);
}

static ssize_t max_ptes_shared_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	int err;
	unsigned long max_ptes_shared;

	err  = kstrtoul(buf, 10, &max_ptes_shared);
	if (err || max_ptes_shared > HPAGE_PMD_NR - 1)
		return -EINVAL;

	khugepaged_max_ptes_shared = max_ptes_shared;

	return count;
}

static struct kobj_attribute khugepaged_max_ptes_shared_attr =
	__ATTR_RW(max_ptes_shared);

static struct attribute *khugepaged_attr[] = {
	&khugepaged_defrag_attr.attr,
	&khugepaged_max_ptes_none_attr.attr,
	&khugepaged_max_ptes_swap_attr.attr,
	&khugepaged_max_ptes_shared_attr.attr,
	&pages_to_scan_attr.attr,
	&pages_collapsed_attr.attr,
	&full_scans_attr.attr,
	&scan_sleep_millisecs_attr.attr,
	&alloc_sleep_millisecs_attr.attr,
	NULL,
};

struct attribute_group khugepaged_attr_group = {
	.attrs = khugepaged_attr,
	.name = "khugepaged",
};
#endif /* CONFIG_SYSFS */

int hugepage_madvise(struct vm_area_struct *vma,
		     unsigned long *vm_flags, int advice)
{
	switch (advice) {
	case MADV_HUGEPAGE:
#ifdef CONFIG_S390
		/*
		 * qemu blindly sets MADV_HUGEPAGE on all allocations, but s390
		 * can't handle this properly after s390_enable_sie, so we simply
		 * ignore the madvise to prevent qemu from causing a SIGSEGV.
		 */
		if (mm_has_pgste(vma->vm_mm))
			return 0;
#endif
		*vm_flags &= ~VM_NOHUGEPAGE;
		*vm_flags |= VM_HUGEPAGE;
		/*
		 * If the vma become good for khugepaged to scan,
		 * register it here without waiting a page fault that
		 * may not happen any time soon.
		 */
		khugepaged_enter_vma(vma, *vm_flags);
		break;
	case MADV_NOHUGEPAGE:
		*vm_flags &= ~VM_HUGEPAGE;
		*vm_flags |= VM_NOHUGEPAGE;
		/*
		 * Setting VM_NOHUGEPAGE will prevent khugepaged from scanning
		 * this vma even if we leave the mm registered in khugepaged if
		 * it got registered before VM_NOHUGEPAGE was set.
		 */
		break;
	}

	return 0;
}

int __init khugepaged_init(void)
{
	mm_slot_cache = kmem_cache_create("khugepaged_mm_slot",
					  sizeof(struct khugepaged_mm_slot),
					  __alignof__(struct khugepaged_mm_slot),
					  0, NULL);
	if (!mm_slot_cache)
		return -ENOMEM;

	khugepaged_pages_to_scan = HPAGE_PMD_NR * 8;
	khugepaged_max_ptes_none = HPAGE_PMD_NR - 1;
	khugepaged_max_ptes_swap = HPAGE_PMD_NR / 8;
	khugepaged_max_ptes_shared = HPAGE_PMD_NR / 2;

	return 0;
}

void __init khugepaged_destroy(void)
{
	kmem_cache_destroy(mm_slot_cache);
}

static inline int hpage_collapse_test_exit(struct mm_struct *mm)
{
	return atomic_read(&mm->mm_users) == 0;
}

void __khugepaged_enter(struct mm_struct *mm)
{
	struct khugepaged_mm_slot *mm_slot;
	struct mm_slot *slot;
	int wakeup;

	/* __khugepaged_exit() must not run from under us */
	VM_BUG_ON_MM(hpage_collapse_test_exit(mm), mm);
	if (unlikely(test_and_set_bit(MMF_VM_HUGEPAGE, &mm->flags)))
		return;

	mm_slot = mm_slot_alloc(mm_slot_cache);
	if (!mm_slot)
		return;

	slot = &mm_slot->slot;

	spin_lock(&khugepaged_mm_lock);
	mm_slot_insert(mm_slots_hash, mm, slot);
	/*
	 * Insert just behind the scanning cursor, to let the area settle
	 * down a little.
	 */
	wakeup = list_empty(&khugepaged_scan.mm_head);
	list_add_tail(&slot->mm_node, &khugepaged_scan.mm_head);
	spin_unlock(&khugepaged_mm_lock);

	mmgrab(mm);
	if (wakeup)
		wake_up_interruptible(&khugepaged_wait);
}

void khugepaged_enter_vma(struct vm_area_struct *vma,
			  unsigned long vm_flags)
{
	if (!test_bit(MMF_VM_HUGEPAGE, &vma->vm_mm->flags) &&
	    hugepage_flags_enabled()) {
		if (thp_vma_allowable_order(vma, vm_flags, false, false, true,
					    PMD_ORDER))
			__khugepaged_enter(vma->vm_mm);
	}
}

void __khugepaged_exit(struct mm_struct *mm)
{
	struct khugepaged_mm_slot *mm_slot;
	struct mm_slot *slot;
	int free = 0;

	spin_lock(&khugepaged_mm_lock);
	slot = mm_slot_lookup(mm_slots_hash, mm);
	mm_slot = mm_slot_entry(slot, struct khugepaged_mm_slot, slot);
	if (mm_slot && khugepaged_scan.mm_slot != mm_slot) {
		hash_del(&slot->hash);
		list_del(&slot->mm_node);
		free = 1;
	}
	spin_unlock(&khugepaged_mm_lock);

	if (free) {
		clear_bit(MMF_VM_HUGEPAGE, &mm->flags);
		mm_slot_free(mm_slot_cache, mm_slot);
		mmdrop(mm);
	} else if (mm_slot) {
		/*
		 * This is required to serialize against
		 * hpage_collapse_test_exit() (which is guaranteed to run
		 * under mmap sem read mode). Stop here (after we return all
		 * pagetables will be destroyed) until khugepaged has finished
		 * working on the pagetables under the mmap_lock.
		 */
		mmap_write_lock(mm);
		mmap_write_unlock(mm);
	}
}

static void release_pte_folio(struct folio *folio)
{
	node_stat_mod_folio(folio,
			NR_ISOLATED_ANON + folio_is_file_lru(folio),
			-folio_nr_pages(folio));
	folio_unlock(folio);
	folio_putback_lru(folio);
}

static void release_pte_pages(pte_t *pte, pte_t *_pte,
		struct list_head *compound_pagelist)
{
	struct folio *folio, *tmp;

	while (--_pte >= pte) {
		pte_t pteval = ptep_get(_pte);
		unsigned long pfn;

		if (pte_none(pteval))
			continue;
		pfn = pte_pfn(pteval);
		if (is_zero_pfn(pfn))
			continue;
		folio = pfn_folio(pfn);
		if (folio_test_large(folio))
			continue;
		release_pte_folio(folio);
	}

	list_for_each_entry_safe(folio, tmp, compound_pagelist, lru) {
		list_del(&folio->lru);
		release_pte_folio(folio);
	}
}

static bool is_refcount_suitable(struct folio *folio)
{
	int expected_refcount;

	expected_refcount = folio_mapcount(folio);
	if (folio_test_swapcache(folio))
		expected_refcount += folio_nr_pages(folio);

	return folio_ref_count(folio) == expected_refcount;
}

static int __collapse_huge_page_isolate(struct vm_area_struct *vma,
					unsigned long address,
					pte_t *pte,
					struct collapse_control *cc,
					struct list_head *compound_pagelist)
{
	struct page *page = NULL;
	struct folio *folio = NULL;
	pte_t *_pte;
	int none_or_zero = 0, shared = 0, result = SCAN_FAIL, referenced = 0;
	bool writable = false;

	for (_pte = pte; _pte < pte + HPAGE_PMD_NR;
	     _pte++, address += PAGE_SIZE) {
		pte_t pteval = ptep_get(_pte);
		if (pte_none(pteval) || (pte_present(pteval) &&
				is_zero_pfn(pte_pfn(pteval)))) {
			++none_or_zero;
			if (!userfaultfd_armed(vma) &&
			    (!cc->is_khugepaged ||
			     none_or_zero <= khugepaged_max_ptes_none)) {
				continue;
			} else {
				result = SCAN_EXCEED_NONE_PTE;
				count_vm_event(THP_SCAN_EXCEED_NONE_PTE);
				goto out;
			}
		}
		if (!pte_present(pteval)) {
			result = SCAN_PTE_NON_PRESENT;
			goto out;
		}
		if (pte_uffd_wp(pteval)) {
			result = SCAN_PTE_UFFD_WP;
			goto out;
		}
		page = vm_normal_page(vma, address, pteval);
		if (unlikely(!page) || unlikely(is_zone_device_page(page))) {
			result = SCAN_PAGE_NULL;
			goto out;
		}

		folio = page_folio(page);
		VM_BUG_ON_FOLIO(!folio_test_anon(folio), folio);

		if (page_mapcount(page) > 1) {
			++shared;
			if (cc->is_khugepaged &&
			    shared > khugepaged_max_ptes_shared) {
				result = SCAN_EXCEED_SHARED_PTE;
				count_vm_event(THP_SCAN_EXCEED_SHARED_PTE);
				goto out;
			}
		}

		if (folio_test_large(folio)) {
			struct folio *f;

			/*
			 * Check if we have dealt with the compound page
			 * already
			 */
			list_for_each_entry(f, compound_pagelist, lru) {
				if (folio == f)
					goto next;
			}
		}

		/*
		 * We can do it before isolate_lru_page because the
		 * page can't be freed from under us. NOTE: PG_lock
		 * is needed to serialize against split_huge_page
		 * when invoked from the VM.
		 */
		if (!folio_trylock(folio)) {
			result = SCAN_PAGE_LOCK;
			goto out;
		}

		/*
		 * Check if the page has any GUP (or other external) pins.
		 *
		 * The page table that maps the page has been already unlinked
		 * from the page table tree and this process cannot get
		 * an additional pin on the page.
		 *
		 * New pins can come later if the page is shared across fork,
		 * but not from this process. The other process cannot write to
		 * the page, only trigger CoW.
		 */
		if (!is_refcount_suitable(folio)) {
			folio_unlock(folio);
			result = SCAN_PAGE_COUNT;
			goto out;
		}

		/*
		 * Isolate the page to avoid collapsing an hugepage
		 * currently in use by the VM.
		 */
		if (!folio_isolate_lru(folio)) {
			folio_unlock(folio);
			result = SCAN_DEL_PAGE_LRU;
			goto out;
		}
		node_stat_mod_folio(folio,
				NR_ISOLATED_ANON + folio_is_file_lru(folio),
				folio_nr_pages(folio));
		VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);
		VM_BUG_ON_FOLIO(folio_test_lru(folio), folio);

		if (folio_test_large(folio))
			list_add_tail(&folio->lru, compound_pagelist);
next:
		/*
		 * If collapse was initiated by khugepaged, check that there is
		 * enough young pte to justify collapsing the page
		 */
		if (cc->is_khugepaged &&
		    (pte_young(pteval) || folio_test_young(folio) ||
		     folio_test_referenced(folio) || mmu_notifier_test_young(vma->vm_mm,
								     address)))
			referenced++;

		if (pte_write(pteval))
			writable = true;
	}

	if (unlikely(!writable)) {
		result = SCAN_PAGE_RO;
	} else if (unlikely(cc->is_khugepaged && !referenced)) {
		result = SCAN_LACK_REFERENCED_PAGE;
	} else {
		result = SCAN_SUCCEED;
		trace_mm_collapse_huge_page_isolate(&folio->page, none_or_zero,
						    referenced, writable, result);
		return result;
	}
out:
	release_pte_pages(pte, _pte, compound_pagelist);
	trace_mm_collapse_huge_page_isolate(&folio->page, none_or_zero,
					    referenced, writable, result);
	return result;
}

static void __collapse_huge_page_copy_succeeded(pte_t *pte,
						struct vm_area_struct *vma,
						unsigned long address,
						spinlock_t *ptl,
						struct list_head *compound_pagelist)
{
	struct folio *src_folio;
	struct page *src_page;
	struct page *tmp;
	pte_t *_pte;
	pte_t pteval;

	for (_pte = pte; _pte < pte + HPAGE_PMD_NR;
	     _pte++, address += PAGE_SIZE) {
		pteval = ptep_get(_pte);
		if (pte_none(pteval) || is_zero_pfn(pte_pfn(pteval))) {
			add_mm_counter(vma->vm_mm, MM_ANONPAGES, 1);
			if (is_zero_pfn(pte_pfn(pteval))) {
				/*
				 * ptl mostly unnecessary.
				 */
				spin_lock(ptl);
				ptep_clear(vma->vm_mm, address, _pte);
				spin_unlock(ptl);
				ksm_might_unmap_zero_page(vma->vm_mm, pteval);
			}
		} else {
			src_page = pte_page(pteval);
			src_folio = page_folio(src_page);
			if (!folio_test_large(src_folio))
				release_pte_folio(src_folio);
			/*
			 * ptl mostly unnecessary, but preempt has to
			 * be disabled to update the per-cpu stats
			 * inside folio_remove_rmap_pte().
			 */
			spin_lock(ptl);
			ptep_clear(vma->vm_mm, address, _pte);
			folio_remove_rmap_pte(src_folio, src_page, vma);
			spin_unlock(ptl);
			free_page_and_swap_cache(src_page);
		}
	}

	list_for_each_entry_safe(src_page, tmp, compound_pagelist, lru) {
		list_del(&src_page->lru);
		mod_node_page_state(page_pgdat(src_page),
				    NR_ISOLATED_ANON + page_is_file_lru(src_page),
				    -compound_nr(src_page));
		unlock_page(src_page);
		free_swap_cache(src_page);
		putback_lru_page(src_page);
	}
}

static void __collapse_huge_page_copy_failed(pte_t *pte,
					     pmd_t *pmd,
					     pmd_t orig_pmd,
					     struct vm_area_struct *vma,
					     struct list_head *compound_pagelist)
{
	spinlock_t *pmd_ptl;

	/*
	 * Re-establish the PMD to point to the original page table
	 * entry. Restoring PMD needs to be done prior to releasing
	 * pages. Since pages are still isolated and locked here,
	 * acquiring anon_vma_lock_write is unnecessary.
	 */
	pmd_ptl = pmd_lock(vma->vm_mm, pmd);
	pmd_populate(vma->vm_mm, pmd, pmd_pgtable(orig_pmd));
	spin_unlock(pmd_ptl);
	/*
	 * Release both raw and compound pages isolated
	 * in __collapse_huge_page_isolate.
	 */
	release_pte_pages(pte, pte + HPAGE_PMD_NR, compound_pagelist);
}

/*
 * __collapse_huge_page_copy - attempts to copy memory contents from raw
 * pages to a hugepage. Cleans up the raw pages if copying succeeds;
 * otherwise restores the original page table and releases isolated raw pages.
 * Returns SCAN_SUCCEED if copying succeeds, otherwise returns SCAN_COPY_MC.
 *
 * @pte: starting of the PTEs to copy from
 * @page: the new hugepage to copy contents to
 * @pmd: pointer to the new hugepage's PMD
 * @orig_pmd: the original raw pages' PMD
 * @vma: the original raw pages' virtual memory area
 * @address: starting address to copy
 * @ptl: lock on raw pages' PTEs
 * @compound_pagelist: list that stores compound pages
 */
static int __collapse_huge_page_copy(pte_t *pte,
				     struct page *page,
				     pmd_t *pmd,
				     pmd_t orig_pmd,
				     struct vm_area_struct *vma,
				     unsigned long address,
				     spinlock_t *ptl,
				     struct list_head *compound_pagelist)
{
	struct page *src_page;
	pte_t *_pte;
	pte_t pteval;
	unsigned long _address;
	int result = SCAN_SUCCEED;

	/*
	 * Copying pages' contents is subject to memory poison at any iteration.
	 */
	for (_pte = pte, _address = address; _pte < pte + HPAGE_PMD_NR;
	     _pte++, page++, _address += PAGE_SIZE) {
		pteval = ptep_get(_pte);
		if (pte_none(pteval) || is_zero_pfn(pte_pfn(pteval))) {
			clear_user_highpage(page, _address);
			continue;
		}
		src_page = pte_page(pteval);
		if (copy_mc_user_highpage(page, src_page, _address, vma) > 0) {
			result = SCAN_COPY_MC;
			break;
		}
	}

	if (likely(result == SCAN_SUCCEED))
		__collapse_huge_page_copy_succeeded(pte, vma, address, ptl,
						    compound_pagelist);
	else
		__collapse_huge_page_copy_failed(pte, pmd, orig_pmd, vma,
						 compound_pagelist);

	return result;
}

static void khugepaged_alloc_sleep(void)
{
	DEFINE_WAIT(wait);

	add_wait_queue(&khugepaged_wait, &wait);
	__set_current_state(TASK_INTERRUPTIBLE|TASK_FREEZABLE);
	schedule_timeout(msecs_to_jiffies(khugepaged_alloc_sleep_millisecs));
	remove_wait_queue(&khugepaged_wait, &wait);
}

struct collapse_control khugepaged_collapse_control = {
	.is_khugepaged = true,
};

static bool hpage_collapse_scan_abort(int nid, struct collapse_control *cc)
{
	int i;

	/*
	 * If node_reclaim_mode is disabled, then no extra effort is made to
	 * allocate memory locally.
	 */
	if (!node_reclaim_enabled())
		return false;

	/* If there is a count for this node already, it must be acceptable */
	if (cc->node_load[nid])
		return false;

	for (i = 0; i < MAX_NUMNODES; i++) {
		if (!cc->node_load[i])
			continue;
		if (node_distance(nid, i) > node_reclaim_distance)
			return true;
	}
	return false;
}

#define khugepaged_defrag()					\
	(transparent_hugepage_flags &				\
	 (1<<TRANSPARENT_HUGEPAGE_DEFRAG_KHUGEPAGED_FLAG))

/* Defrag for khugepaged will enter direct reclaim/compaction if necessary */
static inline gfp_t alloc_hugepage_khugepaged_gfpmask(void)
{
	return khugepaged_defrag() ? GFP_TRANSHUGE : GFP_TRANSHUGE_LIGHT;
}

#ifdef CONFIG_NUMA
static int hpage_collapse_find_target_node(struct collapse_control *cc)
{
	int nid, target_node = 0, max_value = 0;

	/* find first node with max normal pages hit */
	for (nid = 0; nid < MAX_NUMNODES; nid++)
		if (cc->node_load[nid] > max_value) {
			max_value = cc->node_load[nid];
			target_node = nid;
		}

	for_each_online_node(nid) {
		if (max_value == cc->node_load[nid])
			node_set(nid, cc->alloc_nmask);
	}

	return target_node;
}
#else
static int hpage_collapse_find_target_node(struct collapse_control *cc)
{
	return 0;
}
#endif

static bool hpage_collapse_alloc_folio(struct folio **folio, gfp_t gfp, int node,
				      nodemask_t *nmask)
{
	*folio = __folio_alloc(gfp, HPAGE_PMD_ORDER, node, nmask);

	if (unlikely(!*folio)) {
		count_vm_event(THP_COLLAPSE_ALLOC_FAILED);
		return false;
	}

	count_vm_event(THP_COLLAPSE_ALLOC);
	return true;
}

/*
 * If mmap_lock temporarily dropped, revalidate vma
 * before taking mmap_lock.
 * Returns enum scan_result value.
 */

static int hugepage_vma_revalidate(struct mm_struct *mm, unsigned long address,
				   bool expect_anon,
				   struct vm_area_struct **vmap,
				   struct collapse_control *cc)
{
	struct vm_area_struct *vma;

	if (unlikely(hpage_collapse_test_exit(mm)))
		return SCAN_ANY_PROCESS;

	*vmap = vma = find_vma(mm, address);
	if (!vma)
		return SCAN_VMA_NULL;

	if (!thp_vma_suitable_order(vma, address, PMD_ORDER))
		return SCAN_ADDRESS_RANGE;
	if (!thp_vma_allowable_order(vma, vma->vm_flags, false, false,
				     cc->is_khugepaged, PMD_ORDER))
		return SCAN_VMA_CHECK;
	/*
	 * Anon VMA expected, the address may be unmapped then
	 * remapped to file after khugepaged reaquired the mmap_lock.
	 *
	 * thp_vma_allowable_order may return true for qualified file
	 * vmas.
	 */
	if (expect_anon && (!(*vmap)->anon_vma || !vma_is_anonymous(*vmap)))
		return SCAN_PAGE_ANON;
	return SCAN_SUCCEED;
}

static int find_pmd_or_thp_or_none(struct mm_struct *mm,
				   unsigned long address,
				   pmd_t **pmd)
{
	pmd_t pmde;

	*pmd = mm_find_pmd(mm, address);
	if (!*pmd)
		return SCAN_PMD_NULL;

	pmde = pmdp_get_lockless(*pmd);
	if (pmd_none(pmde))
		return SCAN_PMD_NONE;
	if (!pmd_present(pmde))
		return SCAN_PMD_NULL;
	if (pmd_trans_huge(pmde))
		return SCAN_PMD_MAPPED;
	if (pmd_devmap(pmde))
		return SCAN_PMD_NULL;
	if (pmd_bad(pmde))
		return SCAN_PMD_NULL;
	return SCAN_SUCCEED;
}

static int check_pmd_still_valid(struct mm_struct *mm,
				 unsigned long address,
				 pmd_t *pmd)
{
	pmd_t *new_pmd;
	int result = find_pmd_or_thp_or_none(mm, address, &new_pmd);

	if (result != SCAN_SUCCEED)
		return result;
	if (new_pmd != pmd)
		return SCAN_FAIL;
	return SCAN_SUCCEED;
}

/*
 * Bring missing pages in from swap, to complete THP collapse.
 * Only done if hpage_collapse_scan_pmd believes it is worthwhile.
 *
 * Called and returns without pte mapped or spinlocks held.
 * Returns result: if not SCAN_SUCCEED, mmap_lock has been released.
 */
static int __collapse_huge_page_swapin(struct mm_struct *mm,
				       struct vm_area_struct *vma,
				       unsigned long haddr, pmd_t *pmd,
				       int referenced)
{
	int swapped_in = 0;
	vm_fault_t ret = 0;
	unsigned long address, end = haddr + (HPAGE_PMD_NR * PAGE_SIZE);
	int result;
	pte_t *pte = NULL;
	spinlock_t *ptl;

	for (address = haddr; address < end; address += PAGE_SIZE) {
		struct vm_fault vmf = {
			.vma = vma,
			.address = address,
			.pgoff = linear_page_index(vma, address),
			.flags = FAULT_FLAG_ALLOW_RETRY,
			.pmd = pmd,
		};

		if (!pte++) {
			pte = pte_offset_map_nolock(mm, pmd, address, &ptl);
			if (!pte) {
				mmap_read_unlock(mm);
				result = SCAN_PMD_NULL;
				goto out;
			}
		}

		vmf.orig_pte = ptep_get_lockless(pte);
		if (!is_swap_pte(vmf.orig_pte))
			continue;

		vmf.pte = pte;
		vmf.ptl = ptl;
		ret = do_swap_page(&vmf);
		/* Which unmaps pte (after perhaps re-checking the entry) */
		pte = NULL;

		/*
		 * do_swap_page returns VM_FAULT_RETRY with released mmap_lock.
		 * Note we treat VM_FAULT_RETRY as VM_FAULT_ERROR here because
		 * we do not retry here and swap entry will remain in pagetable
		 * resulting in later failure.
		 */
		if (ret & VM_FAULT_RETRY) {
			/* Likely, but not guaranteed, that page lock failed */
			result = SCAN_PAGE_LOCK;
			goto out;
		}
		if (ret & VM_FAULT_ERROR) {
			mmap_read_unlock(mm);
			result = SCAN_FAIL;
			goto out;
		}
		swapped_in++;
	}

	if (pte)
		pte_unmap(pte);

	/* Drain LRU cache to remove extra pin on the swapped in pages */
	if (swapped_in)
		lru_add_drain();

	result = SCAN_SUCCEED;
out:
	trace_mm_collapse_huge_page_swapin(mm, swapped_in, referenced, result);
	return result;
}

static int alloc_charge_hpage(struct page **hpage, struct mm_struct *mm,
			      struct collapse_control *cc)
{
	gfp_t gfp = (cc->is_khugepaged ? alloc_hugepage_khugepaged_gfpmask() :
		     GFP_TRANSHUGE);
	int node = hpage_collapse_find_target_node(cc);
	struct folio *folio;

	if (!hpage_collapse_alloc_folio(&folio, gfp, node, &cc->alloc_nmask)) {
		*hpage = NULL;
		return SCAN_ALLOC_HUGE_PAGE_FAIL;
	}

	if (unlikely(mem_cgroup_charge(folio, mm, gfp))) {
		folio_put(folio);
		*hpage = NULL;
		return SCAN_CGROUP_CHARGE_FAIL;
	}

	count_memcg_folio_events(folio, THP_COLLAPSE_ALLOC, 1);

	*hpage = folio_page(folio, 0);
	return SCAN_SUCCEED;
}

static int collapse_huge_page(struct mm_struct *mm, unsigned long address,
			      int referenced, int unmapped,
			      struct collapse_control *cc)
{
	LIST_HEAD(compound_pagelist);
	pmd_t *pmd, _pmd;
	pte_t *pte;
	pgtable_t pgtable;
	struct folio *folio;
	struct page *hpage;
	spinlock_t *pmd_ptl, *pte_ptl;
	int result = SCAN_FAIL;
	struct vm_area_struct *vma;
	struct mmu_notifier_range range;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);

	/*
	 * Before allocating the hugepage, release the mmap_lock read lock.
	 * The allocation can take potentially a long time if it involves
	 * sync compaction, and we do not need to hold the mmap_lock during
	 * that. We will recheck the vma after taking it again in write mode.
	 */
	mmap_read_unlock(mm);

	result = alloc_charge_hpage(&hpage, mm, cc);
	if (result != SCAN_SUCCEED)
		goto out_nolock;

	mmap_read_lock(mm);
	result = hugepage_vma_revalidate(mm, address, true, &vma, cc);
	if (result != SCAN_SUCCEED) {
		mmap_read_unlock(mm);
		goto out_nolock;
	}

	result = find_pmd_or_thp_or_none(mm, address, &pmd);
	if (result != SCAN_SUCCEED) {
		mmap_read_unlock(mm);
		goto out_nolock;
	}

	if (unmapped) {
		/*
		 * __collapse_huge_page_swapin will return with mmap_lock
		 * released when it fails. So we jump out_nolock directly in
		 * that case.  Continuing to collapse causes inconsistency.
		 */
		result = __collapse_huge_page_swapin(mm, vma, address, pmd,
						     referenced);
		if (result != SCAN_SUCCEED)
			goto out_nolock;
	}

	mmap_read_unlock(mm);
	/*
	 * Prevent all access to pagetables with the exception of
	 * gup_fast later handled by the ptep_clear_flush and the VM
	 * handled by the anon_vma lock + PG_lock.
	 *
	 * UFFDIO_MOVE is prevented to race as well thanks to the
	 * mmap_lock.
	 */
	mmap_write_lock(mm);
	result = hugepage_vma_revalidate(mm, address, true, &vma, cc);
	if (result != SCAN_SUCCEED)
		goto out_up_write;
	/* check if the pmd is still valid */
	result = check_pmd_still_valid(mm, address, pmd);
	if (result != SCAN_SUCCEED)
		goto out_up_write;

	vma_start_write(vma);
	anon_vma_lock_write(vma->anon_vma);

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, mm, address,
				address + HPAGE_PMD_SIZE);
	mmu_notifier_invalidate_range_start(&range);

	pmd_ptl = pmd_lock(mm, pmd); /* probably unnecessary */
	/*
	 * This removes any huge TLB entry from the CPU so we won't allow
	 * huge and small TLB entries for the same virtual address to
	 * avoid the risk of CPU bugs in that area.
	 *
	 * Parallel fast GUP is fine since fast GUP will back off when
	 * it detects PMD is changed.
	 */
	_pmd = pmdp_collapse_flush(vma, address, pmd);
	spin_unlock(pmd_ptl);
	mmu_notifier_invalidate_range_end(&range);
	tlb_remove_table_sync_one();

	pte = pte_offset_map_lock(mm, &_pmd, address, &pte_ptl);
	if (pte) {
		result = __collapse_huge_page_isolate(vma, address, pte, cc,
						      &compound_pagelist);
		spin_unlock(pte_ptl);
	} else {
		result = SCAN_PMD_NULL;
	}

	if (unlikely(result != SCAN_SUCCEED)) {
		if (pte)
			pte_unmap(pte);
		spin_lock(pmd_ptl);
		BUG_ON(!pmd_none(*pmd));
		/*
		 * We can only use set_pmd_at when establishing
		 * hugepmds and never for establishing regular pmds that
		 * points to regular pagetables. Use pmd_populate for that
		 */
		pmd_populate(mm, pmd, pmd_pgtable(_pmd));
		spin_unlock(pmd_ptl);
		anon_vma_unlock_write(vma->anon_vma);
		goto out_up_write;
	}

	/*
	 * All pages are isolated and locked so anon_vma rmap
	 * can't run anymore.
	 */
	anon_vma_unlock_write(vma->anon_vma);

	result = __collapse_huge_page_copy(pte, hpage, pmd, _pmd,
					   vma, address, pte_ptl,
					   &compound_pagelist);
	pte_unmap(pte);
	if (unlikely(result != SCAN_SUCCEED))
		goto out_up_write;

	folio = page_folio(hpage);
	/*
	 * The smp_wmb() inside __folio_mark_uptodate() ensures the
	 * copy_huge_page writes become visible before the set_pmd_at()
	 * write.
	 */
	__folio_mark_uptodate(folio);
	pgtable = pmd_pgtable(_pmd);

	_pmd = mk_huge_pmd(hpage, vma->vm_page_prot);
	_pmd = maybe_pmd_mkwrite(pmd_mkdirty(_pmd), vma);

	spin_lock(pmd_ptl);
	BUG_ON(!pmd_none(*pmd));
	folio_add_new_anon_rmap(folio, vma, address);
	folio_add_lru_vma(folio, vma);
	pgtable_trans_huge_deposit(mm, pmd, pgtable);
	set_pmd_at(mm, address, pmd, _pmd);
	update_mmu_cache_pmd(vma, address, pmd);
	spin_unlock(pmd_ptl);

	hpage = NULL;

	result = SCAN_SUCCEED;
out_up_write:
	mmap_write_unlock(mm);
out_nolock:
	if (hpage)
		put_page(hpage);
	trace_mm_collapse_huge_page(mm, result == SCAN_SUCCEED, result);
	return result;
}

static int hpage_collapse_scan_pmd(struct mm_struct *mm,
				   struct vm_area_struct *vma,
				   unsigned long address, bool *mmap_locked,
				   struct collapse_control *cc)
{
	pmd_t *pmd;
	pte_t *pte, *_pte;
	int result = SCAN_FAIL, referenced = 0;
	int none_or_zero = 0, shared = 0;
	struct page *page = NULL;
	struct folio *folio = NULL;
	unsigned long _address;
	spinlock_t *ptl;
	int node = NUMA_NO_NODE, unmapped = 0;
	bool writable = false;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);

	result = find_pmd_or_thp_or_none(mm, address, &pmd);
	if (result != SCAN_SUCCEED)
		goto out;

	memset(cc->node_load, 0, sizeof(cc->node_load));
	nodes_clear(cc->alloc_nmask);
	pte = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (!pte) {
		result = SCAN_PMD_NULL;
		goto out;
	}

	for (_address = address, _pte = pte; _pte < pte + HPAGE_PMD_NR;
	     _pte++, _address += PAGE_SIZE) {
		pte_t pteval = ptep_get(_pte);
		if (is_swap_pte(pteval)) {
			++unmapped;
			if (!cc->is_khugepaged ||
			    unmapped <= khugepaged_max_ptes_swap) {
				/*
				 * Always be strict with uffd-wp
				 * enabled swap entries.  Please see
				 * comment below for pte_uffd_wp().
				 */
				if (pte_swp_uffd_wp_any(pteval)) {
					result = SCAN_PTE_UFFD_WP;
					goto out_unmap;
				}
				continue;
			} else {
				result = SCAN_EXCEED_SWAP_PTE;
				count_vm_event(THP_SCAN_EXCEED_SWAP_PTE);
				goto out_unmap;
			}
		}
		if (pte_none(pteval) || is_zero_pfn(pte_pfn(pteval))) {
			++none_or_zero;
			if (!userfaultfd_armed(vma) &&
			    (!cc->is_khugepaged ||
			     none_or_zero <= khugepaged_max_ptes_none)) {
				continue;
			} else {
				result = SCAN_EXCEED_NONE_PTE;
				count_vm_event(THP_SCAN_EXCEED_NONE_PTE);
				goto out_unmap;
			}
		}
		if (pte_uffd_wp(pteval)) {
			/*
			 * Don't collapse the page if any of the small
			 * PTEs are armed with uffd write protection.
			 * Here we can also mark the new huge pmd as
			 * write protected if any of the small ones is
			 * marked but that could bring unknown
			 * userfault messages that falls outside of
			 * the registered range.  So, just be simple.
			 */
			result = SCAN_PTE_UFFD_WP;
			goto out_unmap;
		}
		if (pte_write(pteval))
			writable = true;

		page = vm_normal_page(vma, _address, pteval);
		if (unlikely(!page) || unlikely(is_zone_device_page(page))) {
			result = SCAN_PAGE_NULL;
			goto out_unmap;
		}

		if (page_mapcount(page) > 1) {
			++shared;
			if (cc->is_khugepaged &&
			    shared > khugepaged_max_ptes_shared) {
				result = SCAN_EXCEED_SHARED_PTE;
				count_vm_event(THP_SCAN_EXCEED_SHARED_PTE);
				goto out_unmap;
			}
		}

		folio = page_folio(page);
		/*
		 * Record which node the original page is from and save this
		 * information to cc->node_load[].
		 * Khugepaged will allocate hugepage from the node has the max
		 * hit record.
		 */
		node = folio_nid(folio);
		if (hpage_collapse_scan_abort(node, cc)) {
			result = SCAN_SCAN_ABORT;
			goto out_unmap;
		}
		cc->node_load[node]++;
		if (!folio_test_lru(folio)) {
			result = SCAN_PAGE_LRU;
			goto out_unmap;
		}
		if (folio_test_locked(folio)) {
			result = SCAN_PAGE_LOCK;
			goto out_unmap;
		}
		if (!folio_test_anon(folio)) {
			result = SCAN_PAGE_ANON;
			goto out_unmap;
		}

		/*
		 * Check if the page has any GUP (or other external) pins.
		 *
		 * Here the check may be racy:
		 * it may see total_mapcount > refcount in some cases?
		 * But such case is ephemeral we could always retry collapse
		 * later.  However it may report false positive if the page
		 * has excessive GUP pins (i.e. 512).  Anyway the same check
		 * will be done again later the risk seems low.
		 */
		if (!is_refcount_suitable(folio)) {
			result = SCAN_PAGE_COUNT;
			goto out_unmap;
		}

		/*
		 * If collapse was initiated by khugepaged, check that there is
		 * enough young pte to justify collapsing the page
		 */
		if (cc->is_khugepaged &&
		    (pte_young(pteval) || folio_test_young(folio) ||
		     folio_test_referenced(folio) || mmu_notifier_test_young(vma->vm_mm,
								     address)))
			referenced++;
	}
	if (!writable) {
		result = SCAN_PAGE_RO;
	} else if (cc->is_khugepaged &&
		   (!referenced ||
		    (unmapped && referenced < HPAGE_PMD_NR / 2))) {
		result = SCAN_LACK_REFERENCED_PAGE;
	} else {
		result = SCAN_SUCCEED;
	}
out_unmap:
	pte_unmap_unlock(pte, ptl);
	if (result == SCAN_SUCCEED) {
		result = collapse_huge_page(mm, address, referenced,
					    unmapped, cc);
		/* collapse_huge_page will return with the mmap_lock released */
		*mmap_locked = false;
	}
out:
	trace_mm_khugepaged_scan_pmd(mm, &folio->page, writable, referenced,
				     none_or_zero, result, unmapped);
	return result;
}

static void collect_mm_slot(struct khugepaged_mm_slot *mm_slot)
{
	struct mm_slot *slot = &mm_slot->slot;
	struct mm_struct *mm = slot->mm;

	lockdep_assert_held(&khugepaged_mm_lock);

	if (hpage_collapse_test_exit(mm)) {
		/* free mm_slot */
		hash_del(&slot->hash);
		list_del(&slot->mm_node);

		/*
		 * Not strictly needed because the mm exited already.
		 *
		 * clear_bit(MMF_VM_HUGEPAGE, &mm->flags);
		 */

		/* khugepaged_mm_lock actually not necessary for the below */
		mm_slot_free(mm_slot_cache, mm_slot);
		mmdrop(mm);
	}
}

#ifdef CONFIG_SHMEM
/* hpage must be locked, and mmap_lock must be held */
static int set_huge_pmd(struct vm_area_struct *vma, unsigned long addr,
			pmd_t *pmdp, struct page *hpage)
{
	struct vm_fault vmf = {
		.vma = vma,
		.address = addr,
		.flags = 0,
		.pmd = pmdp,
	};

	VM_BUG_ON(!PageTransHuge(hpage));
	mmap_assert_locked(vma->vm_mm);

	if (do_set_pmd(&vmf, hpage))
		return SCAN_FAIL;

	get_page(hpage);
	return SCAN_SUCCEED;
}

/**
 * collapse_pte_mapped_thp - Try to collapse a pte-mapped THP for mm at
 * address haddr.
 *
 * @mm: process address space where collapse happens
 * @addr: THP collapse address
 * @install_pmd: If a huge PMD should be installed
 *
 * This function checks whether all the PTEs in the PMD are pointing to the
 * right THP. If so, retract the page table so the THP can refault in with
 * as pmd-mapped. Possibly install a huge PMD mapping the THP.
 */
int collapse_pte_mapped_thp(struct mm_struct *mm, unsigned long addr,
			    bool install_pmd)
{
	struct mmu_notifier_range range;
	bool notified = false;
	unsigned long haddr = addr & HPAGE_PMD_MASK;
	struct vm_area_struct *vma = vma_lookup(mm, haddr);
	struct folio *folio;
	pte_t *start_pte, *pte;
	pmd_t *pmd, pgt_pmd;
	spinlock_t *pml = NULL, *ptl;
	int nr_ptes = 0, result = SCAN_FAIL;
	int i;

	mmap_assert_locked(mm);

	/* First check VMA found, in case page tables are being torn down */
	if (!vma || !vma->vm_file ||
	    !range_in_vma(vma, haddr, haddr + HPAGE_PMD_SIZE))
		return SCAN_VMA_CHECK;

	/* Fast check before locking page if already PMD-mapped */
	result = find_pmd_or_thp_or_none(mm, haddr, &pmd);
	if (result == SCAN_PMD_MAPPED)
		return result;

	/*
	 * If we are here, we've succeeded in replacing all the native pages
	 * in the page cache with a single hugepage. If a mm were to fault-in
	 * this memory (mapped by a suitably aligned VMA), we'd get the hugepage
	 * and map it by a PMD, regardless of sysfs THP settings. As such, let's
	 * analogously elide sysfs THP settings here.
	 */
	if (!thp_vma_allowable_order(vma, vma->vm_flags, false, false, false,
				     PMD_ORDER))
		return SCAN_VMA_CHECK;

	/* Keep pmd pgtable for uffd-wp; see comment in retract_page_tables() */
	if (userfaultfd_wp(vma))
		return SCAN_PTE_UFFD_WP;

	folio = filemap_lock_folio(vma->vm_file->f_mapping,
			       linear_page_index(vma, haddr));
	if (IS_ERR(folio))
		return SCAN_PAGE_NULL;

	if (folio_order(folio) != HPAGE_PMD_ORDER) {
		result = SCAN_PAGE_COMPOUND;
		goto drop_folio;
	}

	result = find_pmd_or_thp_or_none(mm, haddr, &pmd);
	switch (result) {
	case SCAN_SUCCEED:
		break;
	case SCAN_PMD_NONE:
		/*
		 * All pte entries have been removed and pmd cleared.
		 * Skip all the pte checks and just update the pmd mapping.
		 */
		goto maybe_install_pmd;
	default:
		goto drop_folio;
	}

	result = SCAN_FAIL;
	start_pte = pte_offset_map_lock(mm, pmd, haddr, &ptl);
	if (!start_pte)		/* mmap_lock + page lock should prevent this */
		goto drop_folio;

	/* step 1: check all mapped PTEs are to the right huge page */
	for (i = 0, addr = haddr, pte = start_pte;
	     i < HPAGE_PMD_NR; i++, addr += PAGE_SIZE, pte++) {
		struct page *page;
		pte_t ptent = ptep_get(pte);

		/* empty pte, skip */
		if (pte_none(ptent))
			continue;

		/* page swapped out, abort */
		if (!pte_present(ptent)) {
			result = SCAN_PTE_NON_PRESENT;
			goto abort;
		}

		page = vm_normal_page(vma, addr, ptent);
		if (WARN_ON_ONCE(page && is_zone_device_page(page)))
			page = NULL;
		/*
		 * Note that uprobe, debugger, or MAP_PRIVATE may change the
		 * page table, but the new page will not be a subpage of hpage.
		 */
		if (folio_page(folio, i) != page)
			goto abort;
	}

	pte_unmap_unlock(start_pte, ptl);
	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, mm,
				haddr, haddr + HPAGE_PMD_SIZE);
	mmu_notifier_invalidate_range_start(&range);
	notified = true;

	/*
	 * pmd_lock covers a wider range than ptl, and (if split from mm's
	 * page_table_lock) ptl nests inside pml. The less time we hold pml,
	 * the better; but userfaultfd's mfill_atomic_pte() on a private VMA
	 * inserts a valid as-if-COWed PTE without even looking up page cache.
	 * So page lock of folio does not protect from it, so we must not drop
	 * ptl before pgt_pmd is removed, so uffd private needs pml taken now.
	 */
	if (userfaultfd_armed(vma) && !(vma->vm_flags & VM_SHARED))
		pml = pmd_lock(mm, pmd);

	start_pte = pte_offset_map_nolock(mm, pmd, haddr, &ptl);
	if (!start_pte)		/* mmap_lock + page lock should prevent this */
		goto abort;
	if (!pml)
		spin_lock(ptl);
	else if (ptl != pml)
		spin_lock_nested(ptl, SINGLE_DEPTH_NESTING);

	/* step 2: clear page table and adjust rmap */
	for (i = 0, addr = haddr, pte = start_pte;
	     i < HPAGE_PMD_NR; i++, addr += PAGE_SIZE, pte++) {
		struct page *page;
		pte_t ptent = ptep_get(pte);

		if (pte_none(ptent))
			continue;
		/*
		 * We dropped ptl after the first scan, to do the mmu_notifier:
		 * page lock stops more PTEs of the folio being faulted in, but
		 * does not stop write faults COWing anon copies from existing
		 * PTEs; and does not stop those being swapped out or migrated.
		 */
		if (!pte_present(ptent)) {
			result = SCAN_PTE_NON_PRESENT;
			goto abort;
		}
		page = vm_normal_page(vma, addr, ptent);
		if (folio_page(folio, i) != page)
			goto abort;

		/*
		 * Must clear entry, or a racing truncate may re-remove it.
		 * TLB flush can be left until pmdp_collapse_flush() does it.
		 * PTE dirty? Shmem page is already dirty; file is read-only.
		 */
		ptep_clear(mm, addr, pte);
		folio_remove_rmap_pte(folio, page, vma);
		nr_ptes++;
	}

	pte_unmap(start_pte);
	if (!pml)
		spin_unlock(ptl);

	/* step 3: set proper refcount and mm_counters. */
	if (nr_ptes) {
		folio_ref_sub(folio, nr_ptes);
		add_mm_counter(mm, mm_counter_file(&folio->page), -nr_ptes);
	}

	/* step 4: remove empty page table */
	if (!pml) {
		pml = pmd_lock(mm, pmd);
		if (ptl != pml)
			spin_lock_nested(ptl, SINGLE_DEPTH_NESTING);
	}
	pgt_pmd = pmdp_collapse_flush(vma, haddr, pmd);
	pmdp_get_lockless_sync();
	if (ptl != pml)
		spin_unlock(ptl);
	spin_unlock(pml);

	mmu_notifier_invalidate_range_end(&range);

	mm_dec_nr_ptes(mm);
	page_table_check_pte_clear_range(mm, haddr, pgt_pmd);
	pte_free_defer(mm, pmd_pgtable(pgt_pmd));

maybe_install_pmd:
	/* step 5: install pmd entry */
	result = install_pmd
			? set_huge_pmd(vma, haddr, pmd, &folio->page)
			: SCAN_SUCCEED;
	goto drop_folio;
abort:
	if (nr_ptes) {
		flush_tlb_mm(mm);
		folio_ref_sub(folio, nr_ptes);
		add_mm_counter(mm, mm_counter_file(&folio->page), -nr_ptes);
	}
	if (start_pte)
		pte_unmap_unlock(start_pte, ptl);
	if (pml && pml != ptl)
		spin_unlock(pml);
	if (notified)
		mmu_notifier_invalidate_range_end(&range);
drop_folio:
	folio_unlock(folio);
	folio_put(folio);
	return result;
}

static void retract_page_tables(struct address_space *mapping, pgoff_t pgoff)
{
	struct vm_area_struct *vma;

	i_mmap_lock_read(mapping);
	vma_interval_tree_foreach(vma, &mapping->i_mmap, pgoff, pgoff) {
		struct mmu_notifier_range range;
		struct mm_struct *mm;
		unsigned long addr;
		pmd_t *pmd, pgt_pmd;
		spinlock_t *pml;
		spinlock_t *ptl;
		bool skipped_uffd = false;

		/*
		 * Check vma->anon_vma to exclude MAP_PRIVATE mappings that
		 * got written to. These VMAs are likely not worth removing
		 * page tables from, as PMD-mapping is likely to be split later.
		 */
		if (READ_ONCE(vma->anon_vma))
			continue;

		addr = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
		if (addr & ~HPAGE_PMD_MASK ||
		    vma->vm_end < addr + HPAGE_PMD_SIZE)
			continue;

		mm = vma->vm_mm;
		if (find_pmd_or_thp_or_none(mm, addr, &pmd) != SCAN_SUCCEED)
			continue;

		if (hpage_collapse_test_exit(mm))
			continue;
		/*
		 * When a vma is registered with uffd-wp, we cannot recycle
		 * the page table because there may be pte markers installed.
		 * Other vmas can still have the same file mapped hugely, but
		 * skip this one: it will always be mapped in small page size
		 * for uffd-wp registered ranges.
		 */
		if (userfaultfd_wp(vma))
			continue;

		/* PTEs were notified when unmapped; but now for the PMD? */
		mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, mm,
					addr, addr + HPAGE_PMD_SIZE);
		mmu_notifier_invalidate_range_start(&range);

		pml = pmd_lock(mm, pmd);
		ptl = pte_lockptr(mm, pmd);
		if (ptl != pml)
			spin_lock_nested(ptl, SINGLE_DEPTH_NESTING);

		/*
		 * Huge page lock is still held, so normally the page table
		 * must remain empty; and we have already skipped anon_vma
		 * and userfaultfd_wp() vmas.  But since the mmap_lock is not
		 * held, it is still possible for a racing userfaultfd_ioctl()
		 * to have inserted ptes or markers.  Now that we hold ptlock,
		 * repeating the anon_vma check protects from one category,
		 * and repeating the userfaultfd_wp() check from another.
		 */
		if (unlikely(vma->anon_vma || userfaultfd_wp(vma))) {
			skipped_uffd = true;
		} else {
			pgt_pmd = pmdp_collapse_flush(vma, addr, pmd);
			pmdp_get_lockless_sync();
		}

		if (ptl != pml)
			spin_unlock(ptl);
		spin_unlock(pml);

		mmu_notifier_invalidate_range_end(&range);

		if (!skipped_uffd) {
			mm_dec_nr_ptes(mm);
			page_table_check_pte_clear_range(mm, addr, pgt_pmd);
			pte_free_defer(mm, pmd_pgtable(pgt_pmd));
		}
	}
	i_mmap_unlock_read(mapping);
}

/**
 * collapse_file - collapse filemap/tmpfs/shmem pages into huge one.
 *
 * @mm: process address space where collapse happens
 * @addr: virtual collapse start address
 * @file: file that collapse on
 * @start: collapse start address
 * @cc: collapse context and scratchpad
 *
 * Basic scheme is simple, details are more complex:
 *  - allocate and lock a new huge page;
 *  - scan page cache, locking old pages
 *    + swap/gup in pages if necessary;
 *  - copy data to new page
 *  - handle shmem holes
 *    + re-validate that holes weren't filled by someone else
 *    + check for userfaultfd
 *  - finalize updates to the page cache;
 *  - if replacing succeeds:
 *    + unlock huge page;
 *    + free old pages;
 *  - if replacing failed;
 *    + unlock old pages
 *    + unlock and free huge page;
 */
static int collapse_file(struct mm_struct *mm, unsigned long addr,
			 struct file *file, pgoff_t start,
			 struct collapse_control *cc)
{
	struct address_space *mapping = file->f_mapping;
	struct page *hpage;
	struct page *page;
	struct page *tmp;
	struct folio *folio;
	pgoff_t index = 0, end = start + HPAGE_PMD_NR;
	LIST_HEAD(pagelist);
	XA_STATE_ORDER(xas, &mapping->i_pages, start, HPAGE_PMD_ORDER);
	int nr_none = 0, result = SCAN_SUCCEED;
	bool is_shmem = shmem_file(file);
	int nr = 0;

	VM_BUG_ON(!IS_ENABLED(CONFIG_READ_ONLY_THP_FOR_FS) && !is_shmem);
	VM_BUG_ON(start & (HPAGE_PMD_NR - 1));

	result = alloc_charge_hpage(&hpage, mm, cc);
	if (result != SCAN_SUCCEED)
		goto out;

	__SetPageLocked(hpage);
	if (is_shmem)
		__SetPageSwapBacked(hpage);
	hpage->index = start;
	hpage->mapping = mapping;

	/*
	 * Ensure we have slots for all the pages in the range.  This is
	 * almost certainly a no-op because most of the pages must be present
	 */
	do {
		xas_lock_irq(&xas);
		xas_create_range(&xas);
		if (!xas_error(&xas))
			break;
		xas_unlock_irq(&xas);
		if (!xas_nomem(&xas, GFP_KERNEL)) {
			result = SCAN_FAIL;
			goto rollback;
		}
	} while (1);

	for (index = start; index < end; index++) {
		xas_set(&xas, index);
		page = xas_load(&xas);

		VM_BUG_ON(index != xas.xa_index);
		if (is_shmem) {
			if (!page) {
				/*
				 * Stop if extent has been truncated or
				 * hole-punched, and is now completely
				 * empty.
				 */
				if (index == start) {
					if (!xas_next_entry(&xas, end - 1)) {
						result = SCAN_TRUNCATED;
						goto xa_locked;
					}
				}
				nr_none++;
				continue;
			}

			if (xa_is_value(page) || !PageUptodate(page)) {
				xas_unlock_irq(&xas);
				/* swap in or instantiate fallocated page */
				if (shmem_get_folio(mapping->host, index,
						&folio, SGP_NOALLOC)) {
					result = SCAN_FAIL;
					goto xa_unlocked;
				}
				/* drain lru cache to help isolate_lru_page() */
				lru_add_drain();
				page = folio_file_page(folio, index);
			} else if (trylock_page(page)) {
				get_page(page);
				xas_unlock_irq(&xas);
			} else {
				result = SCAN_PAGE_LOCK;
				goto xa_locked;
			}
		} else {	/* !is_shmem */
			if (!page || xa_is_value(page)) {
				xas_unlock_irq(&xas);
				page_cache_sync_readahead(mapping, &file->f_ra,
							  file, index,
							  end - index);
				/* drain lru cache to help isolate_lru_page() */
				lru_add_drain();
				page = find_lock_page(mapping, index);
				if (unlikely(page == NULL)) {
					result = SCAN_FAIL;
					goto xa_unlocked;
				}
			} else if (PageDirty(page)) {
				/*
				 * khugepaged only works on read-only fd,
				 * so this page is dirty because it hasn't
				 * been flushed since first write. There
				 * won't be new dirty pages.
				 *
				 * Trigger async flush here and hope the
				 * writeback is done when khugepaged
				 * revisits this page.
				 *
				 * This is a one-off situation. We are not
				 * forcing writeback in loop.
				 */
				xas_unlock_irq(&xas);
				filemap_flush(mapping);
				result = SCAN_FAIL;
				goto xa_unlocked;
			} else if (PageWriteback(page)) {
				xas_unlock_irq(&xas);
				result = SCAN_FAIL;
				goto xa_unlocked;
			} else if (trylock_page(page)) {
				get_page(page);
				xas_unlock_irq(&xas);
			} else {
				result = SCAN_PAGE_LOCK;
				goto xa_locked;
			}
		}

		/*
		 * The page must be locked, so we can drop the i_pages lock
		 * without racing with truncate.
		 */
		VM_BUG_ON_PAGE(!PageLocked(page), page);

		/* make sure the page is up to date */
		if (unlikely(!PageUptodate(page))) {
			result = SCAN_FAIL;
			goto out_unlock;
		}

		/*
		 * If file was truncated then extended, or hole-punched, before
		 * we locked the first page, then a THP might be there already.
		 * This will be discovered on the first iteration.
		 */
		if (PageTransCompound(page)) {
			struct page *head = compound_head(page);

			result = compound_order(head) == HPAGE_PMD_ORDER &&
					head->index == start
					/* Maybe PMD-mapped */
					? SCAN_PTE_MAPPED_HUGEPAGE
					: SCAN_PAGE_COMPOUND;
			goto out_unlock;
		}

		folio = page_folio(page);

		if (folio_mapping(folio) != mapping) {
			result = SCAN_TRUNCATED;
			goto out_unlock;
		}

		if (!is_shmem && (folio_test_dirty(folio) ||
				  folio_test_writeback(folio))) {
			/*
			 * khugepaged only works on read-only fd, so this
			 * page is dirty because it hasn't been flushed
			 * since first write.
			 */
			result = SCAN_FAIL;
			goto out_unlock;
		}

		if (!folio_isolate_lru(folio)) {
			result = SCAN_DEL_PAGE_LRU;
			goto out_unlock;
		}

		if (!filemap_release_folio(folio, GFP_KERNEL)) {
			result = SCAN_PAGE_HAS_PRIVATE;
			folio_putback_lru(folio);
			goto out_unlock;
		}

		if (folio_mapped(folio))
			try_to_unmap(folio,
					TTU_IGNORE_MLOCK | TTU_BATCH_FLUSH);

		xas_lock_irq(&xas);

		VM_BUG_ON_PAGE(page != xa_load(xas.xa, index), page);

		/*
		 * We control three references to the page:
		 *  - we hold a pin on it;
		 *  - one reference from page cache;
		 *  - one from isolate_lru_page;
		 * If those are the only references, then any new usage of the
		 * page will have to fetch it from the page cache. That requires
		 * locking the page to handle truncate, so any new usage will be
		 * blocked until we unlock page after collapse/during rollback.
		 */
		if (page_count(page) != 3) {
			result = SCAN_PAGE_COUNT;
			xas_unlock_irq(&xas);
			putback_lru_page(page);
			goto out_unlock;
		}

		/*
		 * Accumulate the pages that are being collapsed.
		 */
		list_add_tail(&page->lru, &pagelist);
		continue;
out_unlock:
		unlock_page(page);
		put_page(page);
		goto xa_unlocked;
	}

	if (!is_shmem) {
		filemap_nr_thps_inc(mapping);
		/*
		 * Paired with smp_mb() in do_dentry_open() to ensure
		 * i_writecount is up to date and the update to nr_thps is
		 * visible. Ensures the page cache will be truncated if the
		 * file is opened writable.
		 */
		smp_mb();
		if (inode_is_open_for_write(mapping->host)) {
			result = SCAN_FAIL;
			filemap_nr_thps_dec(mapping);
		}
	}

xa_locked:
	xas_unlock_irq(&xas);
xa_unlocked:

	/*
	 * If collapse is successful, flush must be done now before copying.
	 * If collapse is unsuccessful, does flush actually need to be done?
	 * Do it anyway, to clear the state.
	 */
	try_to_unmap_flush();

	if (result == SCAN_SUCCEED && nr_none &&
	    !shmem_charge(mapping->host, nr_none))
		result = SCAN_FAIL;
	if (result != SCAN_SUCCEED) {
		nr_none = 0;
		goto rollback;
	}

	/*
	 * The old pages are locked, so they won't change anymore.
	 */
	index = start;
	list_for_each_entry(page, &pagelist, lru) {
		while (index < page->index) {
			clear_highpage(hpage + (index % HPAGE_PMD_NR));
			index++;
		}
		if (copy_mc_highpage(hpage + (page->index % HPAGE_PMD_NR), page) > 0) {
			result = SCAN_COPY_MC;
			goto rollback;
		}
		index++;
	}
	while (index < end) {
		clear_highpage(hpage + (index % HPAGE_PMD_NR));
		index++;
	}

	if (nr_none) {
		struct vm_area_struct *vma;
		int nr_none_check = 0;

		i_mmap_lock_read(mapping);
		xas_lock_irq(&xas);

		xas_set(&xas, start);
		for (index = start; index < end; index++) {
			if (!xas_next(&xas)) {
				xas_store(&xas, XA_RETRY_ENTRY);
				if (xas_error(&xas)) {
					result = SCAN_STORE_FAILED;
					goto immap_locked;
				}
				nr_none_check++;
			}
		}

		if (nr_none != nr_none_check) {
			result = SCAN_PAGE_FILLED;
			goto immap_locked;
		}

		/*
		 * If userspace observed a missing page in a VMA with a MODE_MISSING
		 * userfaultfd, then it might expect a UFFD_EVENT_PAGEFAULT for that
		 * page. If so, we need to roll back to avoid suppressing such an
		 * event. Since wp/minor userfaultfds don't give userspace any
		 * guarantees that the kernel doesn't fill a missing page with a zero
		 * page, so they don't matter here.
		 *
		 * Any userfaultfds registered after this point will not be able to
		 * observe any missing pages due to the previously inserted retry
		 * entries.
		 */
		vma_interval_tree_foreach(vma, &mapping->i_mmap, start, end) {
			if (userfaultfd_missing(vma)) {
				result = SCAN_EXCEED_NONE_PTE;
				goto immap_locked;
			}
		}

immap_locked:
		i_mmap_unlock_read(mapping);
		if (result != SCAN_SUCCEED) {
			xas_set(&xas, start);
			for (index = start; index < end; index++) {
				if (xas_next(&xas) == XA_RETRY_ENTRY)
					xas_store(&xas, NULL);
			}

			xas_unlock_irq(&xas);
			goto rollback;
		}
	} else {
		xas_lock_irq(&xas);
	}

	folio = page_folio(hpage);
	nr = folio_nr_pages(folio);
	if (is_shmem)
		__lruvec_stat_mod_folio(folio, NR_SHMEM_THPS, nr);
	else
		__lruvec_stat_mod_folio(folio, NR_FILE_THPS, nr);

	if (nr_none) {
		__lruvec_stat_mod_folio(folio, NR_FILE_PAGES, nr_none);
		/* nr_none is always 0 for non-shmem. */
		__lruvec_stat_mod_folio(folio, NR_SHMEM, nr_none);
	}

	/*
	 * Mark hpage as uptodate before inserting it into the page cache so
	 * that it isn't mistaken for an fallocated but unwritten page.
	 */
	folio_mark_uptodate(folio);
	folio_ref_add(folio, HPAGE_PMD_NR - 1);

	if (is_shmem)
		folio_mark_dirty(folio);
	folio_add_lru(folio);

	/* Join all the small entries into a single multi-index entry. */
	xas_set_order(&xas, start, HPAGE_PMD_ORDER);
	xas_store(&xas, folio);
	WARN_ON_ONCE(xas_error(&xas));
	xas_unlock_irq(&xas);

	/*
	 * Remove pte page tables, so we can re-fault the page as huge.
	 * If MADV_COLLAPSE, adjust result to call collapse_pte_mapped_thp().
	 */
	retract_page_tables(mapping, start);
	if (cc && !cc->is_khugepaged)
		result = SCAN_PTE_MAPPED_HUGEPAGE;
	folio_unlock(folio);

	/*
	 * The collapse has succeeded, so free the old pages.
	 */
	list_for_each_entry_safe(page, tmp, &pagelist, lru) {
		list_del(&page->lru);
		page->mapping = NULL;
		ClearPageActive(page);
		ClearPageUnevictable(page);
		unlock_page(page);
		folio_put_refs(page_folio(page), 3);
	}

	goto out;

rollback:
	/* Something went wrong: roll back page cache changes */
	if (nr_none) {
		xas_lock_irq(&xas);
		mapping->nrpages -= nr_none;
		xas_unlock_irq(&xas);
		shmem_uncharge(mapping->host, nr_none);
	}

	list_for_each_entry_safe(page, tmp, &pagelist, lru) {
		list_del(&page->lru);
		unlock_page(page);
		putback_lru_page(page);
		put_page(page);
	}
	/*
	 * Undo the updates of filemap_nr_thps_inc for non-SHMEM
	 * file only. This undo is not needed unless failure is
	 * due to SCAN_COPY_MC.
	 */
	if (!is_shmem && result == SCAN_COPY_MC) {
		filemap_nr_thps_dec(mapping);
		/*
		 * Paired with smp_mb() in do_dentry_open() to
		 * ensure the update to nr_thps is visible.
		 */
		smp_mb();
	}

	hpage->mapping = NULL;

	unlock_page(hpage);
	put_page(hpage);
out:
	VM_BUG_ON(!list_empty(&pagelist));
	trace_mm_khugepaged_collapse_file(mm, hpage, index, is_shmem, addr, file, nr, result);
	return result;
}

static int hpage_collapse_scan_file(struct mm_struct *mm, unsigned long addr,
				    struct file *file, pgoff_t start,
				    struct collapse_control *cc)
{
	struct page *page = NULL;
	struct address_space *mapping = file->f_mapping;
	XA_STATE(xas, &mapping->i_pages, start);
	int present, swap;
	int node = NUMA_NO_NODE;
	int result = SCAN_SUCCEED;

	present = 0;
	swap = 0;
	memset(cc->node_load, 0, sizeof(cc->node_load));
	nodes_clear(cc->alloc_nmask);
	rcu_read_lock();
	xas_for_each(&xas, page, start + HPAGE_PMD_NR - 1) {
		if (xas_retry(&xas, page))
			continue;

		if (xa_is_value(page)) {
			++swap;
			if (cc->is_khugepaged &&
			    swap > khugepaged_max_ptes_swap) {
				result = SCAN_EXCEED_SWAP_PTE;
				count_vm_event(THP_SCAN_EXCEED_SWAP_PTE);
				break;
			}
			continue;
		}

		/*
		 * TODO: khugepaged should compact smaller compound pages
		 * into a PMD sized page
		 */
		if (PageTransCompound(page)) {
			struct page *head = compound_head(page);

			result = compound_order(head) == HPAGE_PMD_ORDER &&
					head->index == start
					/* Maybe PMD-mapped */
					? SCAN_PTE_MAPPED_HUGEPAGE
					: SCAN_PAGE_COMPOUND;
			/*
			 * For SCAN_PTE_MAPPED_HUGEPAGE, further processing
			 * by the caller won't touch the page cache, and so
			 * it's safe to skip LRU and refcount checks before
			 * returning.
			 */
			break;
		}

		node = page_to_nid(page);
		if (hpage_collapse_scan_abort(node, cc)) {
			result = SCAN_SCAN_ABORT;
			break;
		}
		cc->node_load[node]++;

		if (!PageLRU(page)) {
			result = SCAN_PAGE_LRU;
			break;
		}

		if (page_count(page) !=
		    1 + page_mapcount(page) + page_has_private(page)) {
			result = SCAN_PAGE_COUNT;
			break;
		}

		/*
		 * We probably should check if the page is referenced here, but
		 * nobody would transfer pte_young() to PageReferenced() for us.
		 * And rmap walk here is just too costly...
		 */

		present++;

		if (need_resched()) {
			xas_pause(&xas);
			cond_resched_rcu();
		}
	}
	rcu_read_unlock();

	if (result == SCAN_SUCCEED) {
		if (cc->is_khugepaged &&
		    present < HPAGE_PMD_NR - khugepaged_max_ptes_none) {
			result = SCAN_EXCEED_NONE_PTE;
			count_vm_event(THP_SCAN_EXCEED_NONE_PTE);
		} else {
			result = collapse_file(mm, addr, file, start, cc);
		}
	}

	trace_mm_khugepaged_scan_file(mm, page, file, present, swap, result);
	return result;
}
#else
static int hpage_collapse_scan_file(struct mm_struct *mm, unsigned long addr,
				    struct file *file, pgoff_t start,
				    struct collapse_control *cc)
{
	BUILD_BUG();
}
#endif

static unsigned int khugepaged_scan_mm_slot(unsigned int pages, int *result,
					    struct collapse_control *cc)
	__releases(&khugepaged_mm_lock)
	__acquires(&khugepaged_mm_lock)
{
	struct vma_iterator vmi;
	struct khugepaged_mm_slot *mm_slot;
	struct mm_slot *slot;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	int progress = 0;

	VM_BUG_ON(!pages);
	lockdep_assert_held(&khugepaged_mm_lock);
	*result = SCAN_FAIL;

	if (khugepaged_scan.mm_slot) {
		mm_slot = khugepaged_scan.mm_slot;
		slot = &mm_slot->slot;
	} else {
		slot = list_entry(khugepaged_scan.mm_head.next,
				     struct mm_slot, mm_node);
		mm_slot = mm_slot_entry(slot, struct khugepaged_mm_slot, slot);
		khugepaged_scan.address = 0;
		khugepaged_scan.mm_slot = mm_slot;
	}
	spin_unlock(&khugepaged_mm_lock);

	mm = slot->mm;
	/*
	 * Don't wait for semaphore (to avoid long wait times).  Just move to
	 * the next mm on the list.
	 */
	vma = NULL;
	if (unlikely(!mmap_read_trylock(mm)))
		goto breakouterloop_mmap_lock;

	progress++;
	if (unlikely(hpage_collapse_test_exit(mm)))
		goto breakouterloop;

	vma_iter_init(&vmi, mm, khugepaged_scan.address);
	for_each_vma(vmi, vma) {
		unsigned long hstart, hend;

		cond_resched();
		if (unlikely(hpage_collapse_test_exit(mm))) {
			progress++;
			break;
		}
		if (!thp_vma_allowable_order(vma, vma->vm_flags, false, false,
					     true, PMD_ORDER)) {
skip:
			progress++;
			continue;
		}
		hstart = round_up(vma->vm_start, HPAGE_PMD_SIZE);
		hend = round_down(vma->vm_end, HPAGE_PMD_SIZE);
		if (khugepaged_scan.address > hend)
			goto skip;
		if (khugepaged_scan.address < hstart)
			khugepaged_scan.address = hstart;
		VM_BUG_ON(khugepaged_scan.address & ~HPAGE_PMD_MASK);

		while (khugepaged_scan.address < hend) {
			bool mmap_locked = true;

			cond_resched();
			if (unlikely(hpage_collapse_test_exit(mm)))
				goto breakouterloop;

			VM_BUG_ON(khugepaged_scan.address < hstart ||
				  khugepaged_scan.address + HPAGE_PMD_SIZE >
				  hend);
			if (IS_ENABLED(CONFIG_SHMEM) && vma->vm_file) {
				struct file *file = get_file(vma->vm_file);
				pgoff_t pgoff = linear_page_index(vma,
						khugepaged_scan.address);

				mmap_read_unlock(mm);
				mmap_locked = false;
				*result = hpage_collapse_scan_file(mm,
					khugepaged_scan.address, file, pgoff, cc);
				fput(file);
				if (*result == SCAN_PTE_MAPPED_HUGEPAGE) {
					mmap_read_lock(mm);
					if (hpage_collapse_test_exit(mm))
						goto breakouterloop;
					*result = collapse_pte_mapped_thp(mm,
						khugepaged_scan.address, false);
					if (*result == SCAN_PMD_MAPPED)
						*result = SCAN_SUCCEED;
					mmap_read_unlock(mm);
				}
			} else {
				*result = hpage_collapse_scan_pmd(mm, vma,
					khugepaged_scan.address, &mmap_locked, cc);
			}

			if (*result == SCAN_SUCCEED)
				++khugepaged_pages_collapsed;

			/* move to next address */
			khugepaged_scan.address += HPAGE_PMD_SIZE;
			progress += HPAGE_PMD_NR;
			if (!mmap_locked)
				/*
				 * We released mmap_lock so break loop.  Note
				 * that we drop mmap_lock before all hugepage
				 * allocations, so if allocation fails, we are
				 * guaranteed to break here and report the
				 * correct result back to caller.
				 */
				goto breakouterloop_mmap_lock;
			if (progress >= pages)
				goto breakouterloop;
		}
	}
breakouterloop:
	mmap_read_unlock(mm); /* exit_mmap will destroy ptes after this */
breakouterloop_mmap_lock:

	spin_lock(&khugepaged_mm_lock);
	VM_BUG_ON(khugepaged_scan.mm_slot != mm_slot);
	/*
	 * Release the current mm_slot if this mm is about to die, or
	 * if we scanned all vmas of this mm.
	 */
	if (hpage_collapse_test_exit(mm) || !vma) {
		/*
		 * Make sure that if mm_users is reaching zero while
		 * khugepaged runs here, khugepaged_exit will find
		 * mm_slot not pointing to the exiting mm.
		 */
		if (slot->mm_node.next != &khugepaged_scan.mm_head) {
			slot = list_entry(slot->mm_node.next,
					  struct mm_slot, mm_node);
			khugepaged_scan.mm_slot =
				mm_slot_entry(slot, struct khugepaged_mm_slot, slot);
			khugepaged_scan.address = 0;
		} else {
			khugepaged_scan.mm_slot = NULL;
			khugepaged_full_scans++;
		}

		collect_mm_slot(mm_slot);
	}

	return progress;
}

static int khugepaged_has_work(void)
{
	return !list_empty(&khugepaged_scan.mm_head) &&
		hugepage_flags_enabled();
}

static int khugepaged_wait_event(void)
{
	return !list_empty(&khugepaged_scan.mm_head) ||
		kthread_should_stop();
}

static void khugepaged_do_scan(struct collapse_control *cc)
{
	unsigned int progress = 0, pass_through_head = 0;
	unsigned int pages = READ_ONCE(khugepaged_pages_to_scan);
	bool wait = true;
	int result = SCAN_SUCCEED;

	lru_add_drain_all();

	while (true) {
		cond_resched();

		if (unlikely(kthread_should_stop()))
			break;

		spin_lock(&khugepaged_mm_lock);
		if (!khugepaged_scan.mm_slot)
			pass_through_head++;
		if (khugepaged_has_work() &&
		    pass_through_head < 2)
			progress += khugepaged_scan_mm_slot(pages - progress,
							    &result, cc);
		else
			progress = pages;
		spin_unlock(&khugepaged_mm_lock);

		if (progress >= pages)
			break;

		if (result == SCAN_ALLOC_HUGE_PAGE_FAIL) {
			/*
			 * If fail to allocate the first time, try to sleep for
			 * a while.  When hit again, cancel the scan.
			 */
			if (!wait)
				break;
			wait = false;
			khugepaged_alloc_sleep();
		}
	}
}

static bool khugepaged_should_wakeup(void)
{
	return kthread_should_stop() ||
	       time_after_eq(jiffies, khugepaged_sleep_expire);
}

static void khugepaged_wait_work(void)
{
	if (khugepaged_has_work()) {
		const unsigned long scan_sleep_jiffies =
			msecs_to_jiffies(khugepaged_scan_sleep_millisecs);

		if (!scan_sleep_jiffies)
			return;

		khugepaged_sleep_expire = jiffies + scan_sleep_jiffies;
		wait_event_freezable_timeout(khugepaged_wait,
					     khugepaged_should_wakeup(),
					     scan_sleep_jiffies);
		return;
	}

	if (hugepage_flags_enabled())
		wait_event_freezable(khugepaged_wait, khugepaged_wait_event());
}

static int khugepaged(void *none)
{
	struct khugepaged_mm_slot *mm_slot;

	set_freezable();
	set_user_nice(current, MAX_NICE);

	while (!kthread_should_stop()) {
		khugepaged_do_scan(&khugepaged_collapse_control);
		khugepaged_wait_work();
	}

	spin_lock(&khugepaged_mm_lock);
	mm_slot = khugepaged_scan.mm_slot;
	khugepaged_scan.mm_slot = NULL;
	if (mm_slot)
		collect_mm_slot(mm_slot);
	spin_unlock(&khugepaged_mm_lock);
	return 0;
}

static void set_recommended_min_free_kbytes(void)
{
	struct zone *zone;
	int nr_zones = 0;
	unsigned long recommended_min;

	if (!hugepage_flags_enabled()) {
		calculate_min_free_kbytes();
		goto update_wmarks;
	}

	for_each_populated_zone(zone) {
		/*
		 * We don't need to worry about fragmentation of
		 * ZONE_MOVABLE since it only has movable pages.
		 */
		if (zone_idx(zone) > gfp_zone(GFP_USER))
			continue;

		nr_zones++;
	}

	/* Ensure 2 pageblocks are free to assist fragmentation avoidance */
	recommended_min = pageblock_nr_pages * nr_zones * 2;

	/*
	 * Make sure that on average at least two pageblocks are almost free
	 * of another type, one for a migratetype to fall back to and a
	 * second to avoid subsequent fallbacks of other types There are 3
	 * MIGRATE_TYPES we care about.
	 */
	recommended_min += pageblock_nr_pages * nr_zones *
			   MIGRATE_PCPTYPES * MIGRATE_PCPTYPES;

	/* don't ever allow to reserve more than 5% of the lowmem */
	recommended_min = min(recommended_min,
			      (unsigned long) nr_free_buffer_pages() / 20);
	recommended_min <<= (PAGE_SHIFT-10);

	if (recommended_min > min_free_kbytes) {
		if (user_min_free_kbytes >= 0)
			pr_info("raising min_free_kbytes from %d to %lu to help transparent hugepage allocations\n",
				min_free_kbytes, recommended_min);

		min_free_kbytes = recommended_min;
	}

update_wmarks:
	setup_per_zone_wmarks();
}

int start_stop_khugepaged(void)
{
	int err = 0;

	mutex_lock(&khugepaged_mutex);
	if (hugepage_flags_enabled()) {
		if (!khugepaged_thread)
			khugepaged_thread = kthread_run(khugepaged, NULL,
							"khugepaged");
		if (IS_ERR(khugepaged_thread)) {
			pr_err("khugepaged: kthread_run(khugepaged) failed\n");
			err = PTR_ERR(khugepaged_thread);
			khugepaged_thread = NULL;
			goto fail;
		}

		if (!list_empty(&khugepaged_scan.mm_head))
			wake_up_interruptible(&khugepaged_wait);
	} else if (khugepaged_thread) {
		kthread_stop(khugepaged_thread);
		khugepaged_thread = NULL;
	}
	set_recommended_min_free_kbytes();
fail:
	mutex_unlock(&khugepaged_mutex);
	return err;
}

void khugepaged_min_free_kbytes_update(void)
{
	mutex_lock(&khugepaged_mutex);
	if (hugepage_flags_enabled() && khugepaged_thread)
		set_recommended_min_free_kbytes();
	mutex_unlock(&khugepaged_mutex);
}

bool current_is_khugepaged(void)
{
	return kthread_func(current) == khugepaged;
}

static int madvise_collapse_errno(enum scan_result r)
{
	/*
	 * MADV_COLLAPSE breaks from existing madvise(2) conventions to provide
	 * actionable feedback to caller, so they may take an appropriate
	 * fallback measure depending on the nature of the failure.
	 */
	switch (r) {
	case SCAN_ALLOC_HUGE_PAGE_FAIL:
		return -ENOMEM;
	case SCAN_CGROUP_CHARGE_FAIL:
	case SCAN_EXCEED_NONE_PTE:
		return -EBUSY;
	/* Resource temporary unavailable - trying again might succeed */
	case SCAN_PAGE_COUNT:
	case SCAN_PAGE_LOCK:
	case SCAN_PAGE_LRU:
	case SCAN_DEL_PAGE_LRU:
	case SCAN_PAGE_FILLED:
		return -EAGAIN;
	/*
	 * Other: Trying again likely not to succeed / error intrinsic to
	 * specified memory range. khugepaged likely won't be able to collapse
	 * either.
	 */
	default:
		return -EINVAL;
	}
}

int madvise_collapse(struct vm_area_struct *vma, struct vm_area_struct **prev,
		     unsigned long start, unsigned long end)
{
	struct collapse_control *cc;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long hstart, hend, addr;
	int thps = 0, last_fail = SCAN_FAIL;
	bool mmap_locked = true;

	BUG_ON(vma->vm_start > start);
	BUG_ON(vma->vm_end < end);

	*prev = vma;

	if (!thp_vma_allowable_order(vma, vma->vm_flags, false, false, false,
				     PMD_ORDER))
		return -EINVAL;

	cc = kmalloc(sizeof(*cc), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;
	cc->is_khugepaged = false;

	mmgrab(mm);
	lru_add_drain_all();

	hstart = (start + ~HPAGE_PMD_MASK) & HPAGE_PMD_MASK;
	hend = end & HPAGE_PMD_MASK;

	for (addr = hstart; addr < hend; addr += HPAGE_PMD_SIZE) {
		int result = SCAN_FAIL;

		if (!mmap_locked) {
			cond_resched();
			mmap_read_lock(mm);
			mmap_locked = true;
			result = hugepage_vma_revalidate(mm, addr, false, &vma,
							 cc);
			if (result  != SCAN_SUCCEED) {
				last_fail = result;
				goto out_nolock;
			}

			hend = min(hend, vma->vm_end & HPAGE_PMD_MASK);
		}
		mmap_assert_locked(mm);
		memset(cc->node_load, 0, sizeof(cc->node_load));
		nodes_clear(cc->alloc_nmask);
		if (IS_ENABLED(CONFIG_SHMEM) && vma->vm_file) {
			struct file *file = get_file(vma->vm_file);
			pgoff_t pgoff = linear_page_index(vma, addr);

			mmap_read_unlock(mm);
			mmap_locked = false;
			result = hpage_collapse_scan_file(mm, addr, file, pgoff,
							  cc);
			fput(file);
		} else {
			result = hpage_collapse_scan_pmd(mm, vma, addr,
							 &mmap_locked, cc);
		}
		if (!mmap_locked)
			*prev = NULL;  /* Tell caller we dropped mmap_lock */

handle_result:
		switch (result) {
		case SCAN_SUCCEED:
		case SCAN_PMD_MAPPED:
			++thps;
			break;
		case SCAN_PTE_MAPPED_HUGEPAGE:
			BUG_ON(mmap_locked);
			BUG_ON(*prev);
			mmap_read_lock(mm);
			result = collapse_pte_mapped_thp(mm, addr, true);
			mmap_read_unlock(mm);
			goto handle_result;
		/* Whitelisted set of results where continuing OK */
		case SCAN_PMD_NULL:
		case SCAN_PTE_NON_PRESENT:
		case SCAN_PTE_UFFD_WP:
		case SCAN_PAGE_RO:
		case SCAN_LACK_REFERENCED_PAGE:
		case SCAN_PAGE_NULL:
		case SCAN_PAGE_COUNT:
		case SCAN_PAGE_LOCK:
		case SCAN_PAGE_COMPOUND:
		case SCAN_PAGE_LRU:
		case SCAN_DEL_PAGE_LRU:
			last_fail = result;
			break;
		default:
			last_fail = result;
			/* Other error, exit */
			goto out_maybelock;
		}
	}

out_maybelock:
	/* Caller expects us to hold mmap_lock on return */
	if (!mmap_locked)
		mmap_read_lock(mm);
out_nolock:
	mmap_assert_locked(mm);
	mmdrop(mm);
	kfree(cc);

	return thps == ((hend - hstart) >> HPAGE_PMD_SHIFT) ? 0
			: madvise_collapse_errno(last_fail);
}
