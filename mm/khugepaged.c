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
#include <linux/swapops.h>
#include <linux/shmem_fs.h>

#include <asm/tlb.h>
#include <asm/pgalloc.h>
#include "internal.h"

enum scan_result {
	SCAN_FAIL,
	SCAN_SUCCEED,
	SCAN_PMD_NULL,
	SCAN_EXCEED_NONE_PTE,
	SCAN_EXCEED_SWAP_PTE,
	SCAN_EXCEED_SHARED_PTE,
	SCAN_PTE_NON_PRESENT,
	SCAN_PTE_UFFD_WP,
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
	SCAN_SWAP_CACHE_PAGE,
	SCAN_DEL_PAGE_LRU,
	SCAN_ALLOC_HUGE_PAGE_FAIL,
	SCAN_CGROUP_CHARGE_FAIL,
	SCAN_TRUNCATED,
	SCAN_PAGE_HAS_PRIVATE,
};

#define CREATE_TRACE_POINTS
#include <trace/events/huge_memory.h>

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
 */
static unsigned int khugepaged_max_ptes_none __read_mostly;
static unsigned int khugepaged_max_ptes_swap __read_mostly;
static unsigned int khugepaged_max_ptes_shared __read_mostly;

#define MM_SLOTS_HASH_BITS 10
static __read_mostly DEFINE_HASHTABLE(mm_slots_hash, MM_SLOTS_HASH_BITS);

static struct kmem_cache *mm_slot_cache __read_mostly;

#define MAX_PTE_MAPPED_THP 8

/**
 * struct mm_slot - hash lookup from mm to mm_slot
 * @hash: hash collision list
 * @mm_node: khugepaged scan list headed in khugepaged_scan.mm_head
 * @mm: the mm that this information is valid for
 */
struct mm_slot {
	struct hlist_node hash;
	struct list_head mm_node;
	struct mm_struct *mm;

	/* pte-mapped THP in this mm */
	int nr_pte_mapped_thp;
	unsigned long pte_mapped_thp[MAX_PTE_MAPPED_THP];
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
	struct mm_slot *mm_slot;
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
	return sprintf(buf, "%u\n", khugepaged_scan_sleep_millisecs);
}

static ssize_t scan_sleep_millisecs_store(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  const char *buf, size_t count)
{
	unsigned long msecs;
	int err;

	err = kstrtoul(buf, 10, &msecs);
	if (err || msecs > UINT_MAX)
		return -EINVAL;

	khugepaged_scan_sleep_millisecs = msecs;
	khugepaged_sleep_expire = 0;
	wake_up_interruptible(&khugepaged_wait);

	return count;
}
static struct kobj_attribute scan_sleep_millisecs_attr =
	__ATTR(scan_sleep_millisecs, 0644, scan_sleep_millisecs_show,
	       scan_sleep_millisecs_store);

static ssize_t alloc_sleep_millisecs_show(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buf)
{
	return sprintf(buf, "%u\n", khugepaged_alloc_sleep_millisecs);
}

static ssize_t alloc_sleep_millisecs_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	unsigned long msecs;
	int err;

	err = kstrtoul(buf, 10, &msecs);
	if (err || msecs > UINT_MAX)
		return -EINVAL;

	khugepaged_alloc_sleep_millisecs = msecs;
	khugepaged_sleep_expire = 0;
	wake_up_interruptible(&khugepaged_wait);

	return count;
}
static struct kobj_attribute alloc_sleep_millisecs_attr =
	__ATTR(alloc_sleep_millisecs, 0644, alloc_sleep_millisecs_show,
	       alloc_sleep_millisecs_store);

static ssize_t pages_to_scan_show(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  char *buf)
{
	return sprintf(buf, "%u\n", khugepaged_pages_to_scan);
}
static ssize_t pages_to_scan_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	int err;
	unsigned long pages;

	err = kstrtoul(buf, 10, &pages);
	if (err || !pages || pages > UINT_MAX)
		return -EINVAL;

	khugepaged_pages_to_scan = pages;

	return count;
}
static struct kobj_attribute pages_to_scan_attr =
	__ATTR(pages_to_scan, 0644, pages_to_scan_show,
	       pages_to_scan_store);

static ssize_t pages_collapsed_show(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    char *buf)
{
	return sprintf(buf, "%u\n", khugepaged_pages_collapsed);
}
static struct kobj_attribute pages_collapsed_attr =
	__ATTR_RO(pages_collapsed);

static ssize_t full_scans_show(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       char *buf)
{
	return sprintf(buf, "%u\n", khugepaged_full_scans);
}
static struct kobj_attribute full_scans_attr =
	__ATTR_RO(full_scans);

static ssize_t khugepaged_defrag_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	return single_hugepage_flag_show(kobj, attr, buf,
				TRANSPARENT_HUGEPAGE_DEFRAG_KHUGEPAGED_FLAG);
}
static ssize_t khugepaged_defrag_store(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       const char *buf, size_t count)
{
	return single_hugepage_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_DEFRAG_KHUGEPAGED_FLAG);
}
static struct kobj_attribute khugepaged_defrag_attr =
	__ATTR(defrag, 0644, khugepaged_defrag_show,
	       khugepaged_defrag_store);

/*
 * max_ptes_none controls if khugepaged should collapse hugepages over
 * any unmapped ptes in turn potentially increasing the memory
 * footprint of the vmas. When max_ptes_none is 0 khugepaged will not
 * reduce the available free memory in the system as it
 * runs. Increasing max_ptes_none will instead potentially reduce the
 * free memory in the system during the khugepaged scan.
 */
static ssize_t khugepaged_max_ptes_none_show(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     char *buf)
{
	return sprintf(buf, "%u\n", khugepaged_max_ptes_none);
}
static ssize_t khugepaged_max_ptes_none_store(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      const char *buf, size_t count)
{
	int err;
	unsigned long max_ptes_none;

	err = kstrtoul(buf, 10, &max_ptes_none);
	if (err || max_ptes_none > HPAGE_PMD_NR-1)
		return -EINVAL;

	khugepaged_max_ptes_none = max_ptes_none;

	return count;
}
static struct kobj_attribute khugepaged_max_ptes_none_attr =
	__ATTR(max_ptes_none, 0644, khugepaged_max_ptes_none_show,
	       khugepaged_max_ptes_none_store);

static ssize_t khugepaged_max_ptes_swap_show(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     char *buf)
{
	return sprintf(buf, "%u\n", khugepaged_max_ptes_swap);
}

static ssize_t khugepaged_max_ptes_swap_store(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      const char *buf, size_t count)
{
	int err;
	unsigned long max_ptes_swap;

	err  = kstrtoul(buf, 10, &max_ptes_swap);
	if (err || max_ptes_swap > HPAGE_PMD_NR-1)
		return -EINVAL;

	khugepaged_max_ptes_swap = max_ptes_swap;

	return count;
}

static struct kobj_attribute khugepaged_max_ptes_swap_attr =
	__ATTR(max_ptes_swap, 0644, khugepaged_max_ptes_swap_show,
	       khugepaged_max_ptes_swap_store);

static ssize_t khugepaged_max_ptes_shared_show(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     char *buf)
{
	return sprintf(buf, "%u\n", khugepaged_max_ptes_shared);
}

static ssize_t khugepaged_max_ptes_shared_store(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      const char *buf, size_t count)
{
	int err;
	unsigned long max_ptes_shared;

	err  = kstrtoul(buf, 10, &max_ptes_shared);
	if (err || max_ptes_shared > HPAGE_PMD_NR-1)
		return -EINVAL;

	khugepaged_max_ptes_shared = max_ptes_shared;

	return count;
}

static struct kobj_attribute khugepaged_max_ptes_shared_attr =
	__ATTR(max_ptes_shared, 0644, khugepaged_max_ptes_shared_show,
	       khugepaged_max_ptes_shared_store);

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
		if (!(*vm_flags & VM_NO_KHUGEPAGED) &&
				khugepaged_enter_vma_merge(vma, *vm_flags))
			return -ENOMEM;
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
					  sizeof(struct mm_slot),
					  __alignof__(struct mm_slot), 0, NULL);
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

static inline struct mm_slot *alloc_mm_slot(void)
{
	if (!mm_slot_cache)	/* initialization failed */
		return NULL;
	return kmem_cache_zalloc(mm_slot_cache, GFP_KERNEL);
}

static inline void free_mm_slot(struct mm_slot *mm_slot)
{
	kmem_cache_free(mm_slot_cache, mm_slot);
}

static struct mm_slot *get_mm_slot(struct mm_struct *mm)
{
	struct mm_slot *mm_slot;

	hash_for_each_possible(mm_slots_hash, mm_slot, hash, (unsigned long)mm)
		if (mm == mm_slot->mm)
			return mm_slot;

	return NULL;
}

static void insert_to_mm_slots_hash(struct mm_struct *mm,
				    struct mm_slot *mm_slot)
{
	mm_slot->mm = mm;
	hash_add(mm_slots_hash, &mm_slot->hash, (long)mm);
}

static inline int khugepaged_test_exit(struct mm_struct *mm)
{
	return atomic_read(&mm->mm_users) == 0 || !mmget_still_valid(mm);
}

static bool hugepage_vma_check(struct vm_area_struct *vma,
			       unsigned long vm_flags)
{
	if ((!(vm_flags & VM_HUGEPAGE) && !khugepaged_always()) ||
	    (vm_flags & VM_NOHUGEPAGE) ||
	    test_bit(MMF_DISABLE_THP, &vma->vm_mm->flags))
		return false;

	if (shmem_file(vma->vm_file) ||
	    (IS_ENABLED(CONFIG_READ_ONLY_THP_FOR_FS) &&
	     vma->vm_file &&
	     (vm_flags & VM_DENYWRITE))) {
		return IS_ALIGNED((vma->vm_start >> PAGE_SHIFT) - vma->vm_pgoff,
				HPAGE_PMD_NR);
	}
	if (!vma->anon_vma || vma->vm_ops)
		return false;
	if (vma_is_temporary_stack(vma))
		return false;
	return !(vm_flags & VM_NO_KHUGEPAGED);
}

int __khugepaged_enter(struct mm_struct *mm)
{
	struct mm_slot *mm_slot;
	int wakeup;

	mm_slot = alloc_mm_slot();
	if (!mm_slot)
		return -ENOMEM;

	/* __khugepaged_exit() must not run from under us */
	VM_BUG_ON_MM(atomic_read(&mm->mm_users) == 0, mm);
	if (unlikely(test_and_set_bit(MMF_VM_HUGEPAGE, &mm->flags))) {
		free_mm_slot(mm_slot);
		return 0;
	}

	spin_lock(&khugepaged_mm_lock);
	insert_to_mm_slots_hash(mm, mm_slot);
	/*
	 * Insert just behind the scanning cursor, to let the area settle
	 * down a little.
	 */
	wakeup = list_empty(&khugepaged_scan.mm_head);
	list_add_tail(&mm_slot->mm_node, &khugepaged_scan.mm_head);
	spin_unlock(&khugepaged_mm_lock);

	mmgrab(mm);
	if (wakeup)
		wake_up_interruptible(&khugepaged_wait);

	return 0;
}

int khugepaged_enter_vma_merge(struct vm_area_struct *vma,
			       unsigned long vm_flags)
{
	unsigned long hstart, hend;

	/*
	 * khugepaged only supports read-only files for non-shmem files.
	 * khugepaged does not yet work on special mappings. And
	 * file-private shmem THP is not supported.
	 */
	if (!hugepage_vma_check(vma, vm_flags))
		return 0;

	hstart = (vma->vm_start + ~HPAGE_PMD_MASK) & HPAGE_PMD_MASK;
	hend = vma->vm_end & HPAGE_PMD_MASK;
	if (hstart < hend)
		return khugepaged_enter(vma, vm_flags);
	return 0;
}

void __khugepaged_exit(struct mm_struct *mm)
{
	struct mm_slot *mm_slot;
	int free = 0;

	spin_lock(&khugepaged_mm_lock);
	mm_slot = get_mm_slot(mm);
	if (mm_slot && khugepaged_scan.mm_slot != mm_slot) {
		hash_del(&mm_slot->hash);
		list_del(&mm_slot->mm_node);
		free = 1;
	}
	spin_unlock(&khugepaged_mm_lock);

	if (free) {
		clear_bit(MMF_VM_HUGEPAGE, &mm->flags);
		free_mm_slot(mm_slot);
		mmdrop(mm);
	} else if (mm_slot) {
		/*
		 * This is required to serialize against
		 * khugepaged_test_exit() (which is guaranteed to run
		 * under mmap sem read mode). Stop here (after we
		 * return all pagetables will be destroyed) until
		 * khugepaged has finished working on the pagetables
		 * under the mmap_lock.
		 */
		mmap_write_lock(mm);
		mmap_write_unlock(mm);
	}
}

static void release_pte_page(struct page *page)
{
	mod_node_page_state(page_pgdat(page),
			NR_ISOLATED_ANON + page_is_file_lru(page),
			-compound_nr(page));
	unlock_page(page);
	putback_lru_page(page);
}

static void release_pte_pages(pte_t *pte, pte_t *_pte,
		struct list_head *compound_pagelist)
{
	struct page *page, *tmp;

	while (--_pte >= pte) {
		pte_t pteval = *_pte;

		page = pte_page(pteval);
		if (!pte_none(pteval) && !is_zero_pfn(pte_pfn(pteval)) &&
				!PageCompound(page))
			release_pte_page(page);
	}

	list_for_each_entry_safe(page, tmp, compound_pagelist, lru) {
		list_del(&page->lru);
		release_pte_page(page);
	}
}

static bool is_refcount_suitable(struct page *page)
{
	int expected_refcount;

	expected_refcount = total_mapcount(page);
	if (PageSwapCache(page))
		expected_refcount += compound_nr(page);

	return page_count(page) == expected_refcount;
}

static int __collapse_huge_page_isolate(struct vm_area_struct *vma,
					unsigned long address,
					pte_t *pte,
					struct list_head *compound_pagelist)
{
	struct page *page = NULL;
	pte_t *_pte;
	int none_or_zero = 0, shared = 0, result = 0, referenced = 0;
	bool writable = false;

	for (_pte = pte; _pte < pte+HPAGE_PMD_NR;
	     _pte++, address += PAGE_SIZE) {
		pte_t pteval = *_pte;
		if (pte_none(pteval) || (pte_present(pteval) &&
				is_zero_pfn(pte_pfn(pteval)))) {
			if (!userfaultfd_armed(vma) &&
			    ++none_or_zero <= khugepaged_max_ptes_none) {
				continue;
			} else {
				result = SCAN_EXCEED_NONE_PTE;
				goto out;
			}
		}
		if (!pte_present(pteval)) {
			result = SCAN_PTE_NON_PRESENT;
			goto out;
		}
		page = vm_normal_page(vma, address, pteval);
		if (unlikely(!page)) {
			result = SCAN_PAGE_NULL;
			goto out;
		}

		VM_BUG_ON_PAGE(!PageAnon(page), page);

		if (page_mapcount(page) > 1 &&
				++shared > khugepaged_max_ptes_shared) {
			result = SCAN_EXCEED_SHARED_PTE;
			goto out;
		}

		if (PageCompound(page)) {
			struct page *p;
			page = compound_head(page);

			/*
			 * Check if we have dealt with the compound page
			 * already
			 */
			list_for_each_entry(p, compound_pagelist, lru) {
				if (page == p)
					goto next;
			}
		}

		/*
		 * We can do it before isolate_lru_page because the
		 * page can't be freed from under us. NOTE: PG_lock
		 * is needed to serialize against split_huge_page
		 * when invoked from the VM.
		 */
		if (!trylock_page(page)) {
			result = SCAN_PAGE_LOCK;
			goto out;
		}

		/*
		 * Check if the page has any GUP (or other external) pins.
		 *
		 * The page table that maps the page has been already unlinked
		 * from the page table tree and this process cannot get
		 * an additinal pin on the page.
		 *
		 * New pins can come later if the page is shared across fork,
		 * but not from this process. The other process cannot write to
		 * the page, only trigger CoW.
		 */
		if (!is_refcount_suitable(page)) {
			unlock_page(page);
			result = SCAN_PAGE_COUNT;
			goto out;
		}
		if (!pte_write(pteval) && PageSwapCache(page) &&
				!reuse_swap_page(page, NULL)) {
			/*
			 * Page is in the swap cache and cannot be re-used.
			 * It cannot be collapsed into a THP.
			 */
			unlock_page(page);
			result = SCAN_SWAP_CACHE_PAGE;
			goto out;
		}

		/*
		 * Isolate the page to avoid collapsing an hugepage
		 * currently in use by the VM.
		 */
		if (isolate_lru_page(page)) {
			unlock_page(page);
			result = SCAN_DEL_PAGE_LRU;
			goto out;
		}
		mod_node_page_state(page_pgdat(page),
				NR_ISOLATED_ANON + page_is_file_lru(page),
				compound_nr(page));
		VM_BUG_ON_PAGE(!PageLocked(page), page);
		VM_BUG_ON_PAGE(PageLRU(page), page);

		if (PageCompound(page))
			list_add_tail(&page->lru, compound_pagelist);
next:
		/* There should be enough young pte to collapse the page */
		if (pte_young(pteval) ||
		    page_is_young(page) || PageReferenced(page) ||
		    mmu_notifier_test_young(vma->vm_mm, address))
			referenced++;

		if (pte_write(pteval))
			writable = true;
	}
	if (likely(writable)) {
		if (likely(referenced)) {
			result = SCAN_SUCCEED;
			trace_mm_collapse_huge_page_isolate(page, none_or_zero,
							    referenced, writable, result);
			return 1;
		}
	} else {
		result = SCAN_PAGE_RO;
	}

out:
	release_pte_pages(pte, _pte, compound_pagelist);
	trace_mm_collapse_huge_page_isolate(page, none_or_zero,
					    referenced, writable, result);
	return 0;
}

static void __collapse_huge_page_copy(pte_t *pte, struct page *page,
				      struct vm_area_struct *vma,
				      unsigned long address,
				      spinlock_t *ptl,
				      struct list_head *compound_pagelist)
{
	struct page *src_page, *tmp;
	pte_t *_pte;
	for (_pte = pte; _pte < pte + HPAGE_PMD_NR;
				_pte++, page++, address += PAGE_SIZE) {
		pte_t pteval = *_pte;

		if (pte_none(pteval) || is_zero_pfn(pte_pfn(pteval))) {
			clear_user_highpage(page, address);
			add_mm_counter(vma->vm_mm, MM_ANONPAGES, 1);
			if (is_zero_pfn(pte_pfn(pteval))) {
				/*
				 * ptl mostly unnecessary.
				 */
				spin_lock(ptl);
				/*
				 * paravirt calls inside pte_clear here are
				 * superfluous.
				 */
				pte_clear(vma->vm_mm, address, _pte);
				spin_unlock(ptl);
			}
		} else {
			src_page = pte_page(pteval);
			copy_user_highpage(page, src_page, address, vma);
			if (!PageCompound(src_page))
				release_pte_page(src_page);
			/*
			 * ptl mostly unnecessary, but preempt has to
			 * be disabled to update the per-cpu stats
			 * inside page_remove_rmap().
			 */
			spin_lock(ptl);
			/*
			 * paravirt calls inside pte_clear here are
			 * superfluous.
			 */
			pte_clear(vma->vm_mm, address, _pte);
			page_remove_rmap(src_page, false);
			spin_unlock(ptl);
			free_page_and_swap_cache(src_page);
		}
	}

	list_for_each_entry_safe(src_page, tmp, compound_pagelist, lru) {
		list_del(&src_page->lru);
		release_pte_page(src_page);
	}
}

static void khugepaged_alloc_sleep(void)
{
	DEFINE_WAIT(wait);

	add_wait_queue(&khugepaged_wait, &wait);
	freezable_schedule_timeout_interruptible(
		msecs_to_jiffies(khugepaged_alloc_sleep_millisecs));
	remove_wait_queue(&khugepaged_wait, &wait);
}

static int khugepaged_node_load[MAX_NUMNODES];

static bool khugepaged_scan_abort(int nid)
{
	int i;

	/*
	 * If node_reclaim_mode is disabled, then no extra effort is made to
	 * allocate memory locally.
	 */
	if (!node_reclaim_mode)
		return false;

	/* If there is a count for this node already, it must be acceptable */
	if (khugepaged_node_load[nid])
		return false;

	for (i = 0; i < MAX_NUMNODES; i++) {
		if (!khugepaged_node_load[i])
			continue;
		if (node_distance(nid, i) > node_reclaim_distance)
			return true;
	}
	return false;
}

/* Defrag for khugepaged will enter direct reclaim/compaction if necessary */
static inline gfp_t alloc_hugepage_khugepaged_gfpmask(void)
{
	return khugepaged_defrag() ? GFP_TRANSHUGE : GFP_TRANSHUGE_LIGHT;
}

#ifdef CONFIG_NUMA
static int khugepaged_find_target_node(void)
{
	static int last_khugepaged_target_node = NUMA_NO_NODE;
	int nid, target_node = 0, max_value = 0;

	/* find first node with max normal pages hit */
	for (nid = 0; nid < MAX_NUMNODES; nid++)
		if (khugepaged_node_load[nid] > max_value) {
			max_value = khugepaged_node_load[nid];
			target_node = nid;
		}

	/* do some balance if several nodes have the same hit record */
	if (target_node <= last_khugepaged_target_node)
		for (nid = last_khugepaged_target_node + 1; nid < MAX_NUMNODES;
				nid++)
			if (max_value == khugepaged_node_load[nid]) {
				target_node = nid;
				break;
			}

	last_khugepaged_target_node = target_node;
	return target_node;
}

static bool khugepaged_prealloc_page(struct page **hpage, bool *wait)
{
	if (IS_ERR(*hpage)) {
		if (!*wait)
			return false;

		*wait = false;
		*hpage = NULL;
		khugepaged_alloc_sleep();
	} else if (*hpage) {
		put_page(*hpage);
		*hpage = NULL;
	}

	return true;
}

static struct page *
khugepaged_alloc_page(struct page **hpage, gfp_t gfp, int node)
{
	VM_BUG_ON_PAGE(*hpage, *hpage);

	*hpage = __alloc_pages_node(node, gfp, HPAGE_PMD_ORDER);
	if (unlikely(!*hpage)) {
		count_vm_event(THP_COLLAPSE_ALLOC_FAILED);
		*hpage = ERR_PTR(-ENOMEM);
		return NULL;
	}

	prep_transhuge_page(*hpage);
	count_vm_event(THP_COLLAPSE_ALLOC);
	return *hpage;
}
#else
static int khugepaged_find_target_node(void)
{
	return 0;
}

static inline struct page *alloc_khugepaged_hugepage(void)
{
	struct page *page;

	page = alloc_pages(alloc_hugepage_khugepaged_gfpmask(),
			   HPAGE_PMD_ORDER);
	if (page)
		prep_transhuge_page(page);
	return page;
}

static struct page *khugepaged_alloc_hugepage(bool *wait)
{
	struct page *hpage;

	do {
		hpage = alloc_khugepaged_hugepage();
		if (!hpage) {
			count_vm_event(THP_COLLAPSE_ALLOC_FAILED);
			if (!*wait)
				return NULL;

			*wait = false;
			khugepaged_alloc_sleep();
		} else
			count_vm_event(THP_COLLAPSE_ALLOC);
	} while (unlikely(!hpage) && likely(khugepaged_enabled()));

	return hpage;
}

static bool khugepaged_prealloc_page(struct page **hpage, bool *wait)
{
	if (!*hpage)
		*hpage = khugepaged_alloc_hugepage(wait);

	if (unlikely(!*hpage))
		return false;

	return true;
}

static struct page *
khugepaged_alloc_page(struct page **hpage, gfp_t gfp, int node)
{
	VM_BUG_ON(!*hpage);

	return  *hpage;
}
#endif

/*
 * If mmap_lock temporarily dropped, revalidate vma
 * before taking mmap_lock.
 * Return 0 if succeeds, otherwise return none-zero
 * value (scan code).
 */

static int hugepage_vma_revalidate(struct mm_struct *mm, unsigned long address,
		struct vm_area_struct **vmap)
{
	struct vm_area_struct *vma;
	unsigned long hstart, hend;

	if (unlikely(khugepaged_test_exit(mm)))
		return SCAN_ANY_PROCESS;

	*vmap = vma = find_vma(mm, address);
	if (!vma)
		return SCAN_VMA_NULL;

	hstart = (vma->vm_start + ~HPAGE_PMD_MASK) & HPAGE_PMD_MASK;
	hend = vma->vm_end & HPAGE_PMD_MASK;
	if (address < hstart || address + HPAGE_PMD_SIZE > hend)
		return SCAN_ADDRESS_RANGE;
	if (!hugepage_vma_check(vma, vma->vm_flags))
		return SCAN_VMA_CHECK;
	/* Anon VMA expected */
	if (!vma->anon_vma || vma->vm_ops)
		return SCAN_VMA_CHECK;
	return 0;
}

/*
 * Bring missing pages in from swap, to complete THP collapse.
 * Only done if khugepaged_scan_pmd believes it is worthwhile.
 *
 * Called and returns without pte mapped or spinlocks held,
 * but with mmap_lock held to protect against vma changes.
 */

static bool __collapse_huge_page_swapin(struct mm_struct *mm,
					struct vm_area_struct *vma,
					unsigned long address, pmd_t *pmd,
					int referenced)
{
	int swapped_in = 0;
	vm_fault_t ret = 0;
	struct vm_fault vmf = {
		.vma = vma,
		.address = address,
		.flags = FAULT_FLAG_ALLOW_RETRY,
		.pmd = pmd,
		.pgoff = linear_page_index(vma, address),
	};

	vmf.pte = pte_offset_map(pmd, address);
	for (; vmf.address < address + HPAGE_PMD_NR*PAGE_SIZE;
			vmf.pte++, vmf.address += PAGE_SIZE) {
		vmf.orig_pte = *vmf.pte;
		if (!is_swap_pte(vmf.orig_pte))
			continue;
		swapped_in++;
		ret = do_swap_page(&vmf);

		/* do_swap_page returns VM_FAULT_RETRY with released mmap_lock */
		if (ret & VM_FAULT_RETRY) {
			mmap_read_lock(mm);
			if (hugepage_vma_revalidate(mm, address, &vmf.vma)) {
				/* vma is no longer available, don't continue to swapin */
				trace_mm_collapse_huge_page_swapin(mm, swapped_in, referenced, 0);
				return false;
			}
			/* check if the pmd is still valid */
			if (mm_find_pmd(mm, address) != pmd) {
				trace_mm_collapse_huge_page_swapin(mm, swapped_in, referenced, 0);
				return false;
			}
		}
		if (ret & VM_FAULT_ERROR) {
			trace_mm_collapse_huge_page_swapin(mm, swapped_in, referenced, 0);
			return false;
		}
		/* pte is unmapped now, we need to map it */
		vmf.pte = pte_offset_map(pmd, vmf.address);
	}
	vmf.pte--;
	pte_unmap(vmf.pte);

	/* Drain LRU add pagevec to remove extra pin on the swapped in pages */
	if (swapped_in)
		lru_add_drain();

	trace_mm_collapse_huge_page_swapin(mm, swapped_in, referenced, 1);
	return true;
}

static void collapse_huge_page(struct mm_struct *mm,
				   unsigned long address,
				   struct page **hpage,
				   int node, int referenced, int unmapped)
{
	LIST_HEAD(compound_pagelist);
	pmd_t *pmd, _pmd;
	pte_t *pte;
	pgtable_t pgtable;
	struct page *new_page;
	spinlock_t *pmd_ptl, *pte_ptl;
	int isolated = 0, result = 0;
	struct vm_area_struct *vma;
	struct mmu_notifier_range range;
	gfp_t gfp;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);

	/* Only allocate from the target node */
	gfp = alloc_hugepage_khugepaged_gfpmask() | __GFP_THISNODE;

	/*
	 * Before allocating the hugepage, release the mmap_lock read lock.
	 * The allocation can take potentially a long time if it involves
	 * sync compaction, and we do not need to hold the mmap_lock during
	 * that. We will recheck the vma after taking it again in write mode.
	 */
	mmap_read_unlock(mm);
	new_page = khugepaged_alloc_page(hpage, gfp, node);
	if (!new_page) {
		result = SCAN_ALLOC_HUGE_PAGE_FAIL;
		goto out_nolock;
	}

	if (unlikely(mem_cgroup_charge(new_page, mm, gfp))) {
		result = SCAN_CGROUP_CHARGE_FAIL;
		goto out_nolock;
	}
	count_memcg_page_event(new_page, THP_COLLAPSE_ALLOC);

	mmap_read_lock(mm);
	result = hugepage_vma_revalidate(mm, address, &vma);
	if (result) {
		mmap_read_unlock(mm);
		goto out_nolock;
	}

	pmd = mm_find_pmd(mm, address);
	if (!pmd) {
		result = SCAN_PMD_NULL;
		mmap_read_unlock(mm);
		goto out_nolock;
	}

	/*
	 * __collapse_huge_page_swapin always returns with mmap_lock locked.
	 * If it fails, we release mmap_lock and jump out_nolock.
	 * Continuing to collapse causes inconsistency.
	 */
	if (unmapped && !__collapse_huge_page_swapin(mm, vma, address,
						     pmd, referenced)) {
		mmap_read_unlock(mm);
		goto out_nolock;
	}

	mmap_read_unlock(mm);
	/*
	 * Prevent all access to pagetables with the exception of
	 * gup_fast later handled by the ptep_clear_flush and the VM
	 * handled by the anon_vma lock + PG_lock.
	 */
	mmap_write_lock(mm);
	result = hugepage_vma_revalidate(mm, address, &vma);
	if (result)
		goto out;
	/* check if the pmd is still valid */
	if (mm_find_pmd(mm, address) != pmd)
		goto out;

	anon_vma_lock_write(vma->anon_vma);

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, NULL, mm,
				address, address + HPAGE_PMD_SIZE);
	mmu_notifier_invalidate_range_start(&range);

	pte = pte_offset_map(pmd, address);
	pte_ptl = pte_lockptr(mm, pmd);

	pmd_ptl = pmd_lock(mm, pmd); /* probably unnecessary */
	/*
	 * After this gup_fast can't run anymore. This also removes
	 * any huge TLB entry from the CPU so we won't allow
	 * huge and small TLB entries for the same virtual address
	 * to avoid the risk of CPU bugs in that area.
	 */
	_pmd = pmdp_collapse_flush(vma, address, pmd);
	spin_unlock(pmd_ptl);
	mmu_notifier_invalidate_range_end(&range);

	spin_lock(pte_ptl);
	isolated = __collapse_huge_page_isolate(vma, address, pte,
			&compound_pagelist);
	spin_unlock(pte_ptl);

	if (unlikely(!isolated)) {
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
		result = SCAN_FAIL;
		goto out;
	}

	/*
	 * All pages are isolated and locked so anon_vma rmap
	 * can't run anymore.
	 */
	anon_vma_unlock_write(vma->anon_vma);

	__collapse_huge_page_copy(pte, new_page, vma, address, pte_ptl,
			&compound_pagelist);
	pte_unmap(pte);
	__SetPageUptodate(new_page);
	pgtable = pmd_pgtable(_pmd);

	_pmd = mk_huge_pmd(new_page, vma->vm_page_prot);
	_pmd = maybe_pmd_mkwrite(pmd_mkdirty(_pmd), vma);

	/*
	 * spin_lock() below is not the equivalent of smp_wmb(), so
	 * this is needed to avoid the copy_huge_page writes to become
	 * visible after the set_pmd_at() write.
	 */
	smp_wmb();

	spin_lock(pmd_ptl);
	BUG_ON(!pmd_none(*pmd));
	page_add_new_anon_rmap(new_page, vma, address, true);
	lru_cache_add_inactive_or_unevictable(new_page, vma);
	pgtable_trans_huge_deposit(mm, pmd, pgtable);
	set_pmd_at(mm, address, pmd, _pmd);
	update_mmu_cache_pmd(vma, address, pmd);
	spin_unlock(pmd_ptl);

	*hpage = NULL;

	khugepaged_pages_collapsed++;
	result = SCAN_SUCCEED;
out_up_write:
	mmap_write_unlock(mm);
out_nolock:
	if (!IS_ERR_OR_NULL(*hpage))
		mem_cgroup_uncharge(*hpage);
	trace_mm_collapse_huge_page(mm, isolated, result);
	return;
out:
	goto out_up_write;
}

static int khugepaged_scan_pmd(struct mm_struct *mm,
			       struct vm_area_struct *vma,
			       unsigned long address,
			       struct page **hpage)
{
	pmd_t *pmd;
	pte_t *pte, *_pte;
	int ret = 0, result = 0, referenced = 0;
	int none_or_zero = 0, shared = 0;
	struct page *page = NULL;
	unsigned long _address;
	spinlock_t *ptl;
	int node = NUMA_NO_NODE, unmapped = 0;
	bool writable = false;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);

	pmd = mm_find_pmd(mm, address);
	if (!pmd) {
		result = SCAN_PMD_NULL;
		goto out;
	}

	memset(khugepaged_node_load, 0, sizeof(khugepaged_node_load));
	pte = pte_offset_map_lock(mm, pmd, address, &ptl);
	for (_address = address, _pte = pte; _pte < pte+HPAGE_PMD_NR;
	     _pte++, _address += PAGE_SIZE) {
		pte_t pteval = *_pte;
		if (is_swap_pte(pteval)) {
			if (++unmapped <= khugepaged_max_ptes_swap) {
				/*
				 * Always be strict with uffd-wp
				 * enabled swap entries.  Please see
				 * comment below for pte_uffd_wp().
				 */
				if (pte_swp_uffd_wp(pteval)) {
					result = SCAN_PTE_UFFD_WP;
					goto out_unmap;
				}
				continue;
			} else {
				result = SCAN_EXCEED_SWAP_PTE;
				goto out_unmap;
			}
		}
		if (pte_none(pteval) || is_zero_pfn(pte_pfn(pteval))) {
			if (!userfaultfd_armed(vma) &&
			    ++none_or_zero <= khugepaged_max_ptes_none) {
				continue;
			} else {
				result = SCAN_EXCEED_NONE_PTE;
				goto out_unmap;
			}
		}
		if (!pte_present(pteval)) {
			result = SCAN_PTE_NON_PRESENT;
			goto out_unmap;
		}
		if (pte_uffd_wp(pteval)) {
			/*
			 * Don't collapse the page if any of the small
			 * PTEs are armed with uffd write protection.
			 * Here we can also mark the new huge pmd as
			 * write protected if any of the small ones is
			 * marked but that could bring uknown
			 * userfault messages that falls outside of
			 * the registered range.  So, just be simple.
			 */
			result = SCAN_PTE_UFFD_WP;
			goto out_unmap;
		}
		if (pte_write(pteval))
			writable = true;

		page = vm_normal_page(vma, _address, pteval);
		if (unlikely(!page)) {
			result = SCAN_PAGE_NULL;
			goto out_unmap;
		}

		if (page_mapcount(page) > 1 &&
				++shared > khugepaged_max_ptes_shared) {
			result = SCAN_EXCEED_SHARED_PTE;
			goto out_unmap;
		}

		page = compound_head(page);

		/*
		 * Record which node the original page is from and save this
		 * information to khugepaged_node_load[].
		 * Khupaged will allocate hugepage from the node has the max
		 * hit record.
		 */
		node = page_to_nid(page);
		if (khugepaged_scan_abort(node)) {
			result = SCAN_SCAN_ABORT;
			goto out_unmap;
		}
		khugepaged_node_load[node]++;
		if (!PageLRU(page)) {
			result = SCAN_PAGE_LRU;
			goto out_unmap;
		}
		if (PageLocked(page)) {
			result = SCAN_PAGE_LOCK;
			goto out_unmap;
		}
		if (!PageAnon(page)) {
			result = SCAN_PAGE_ANON;
			goto out_unmap;
		}

		/*
		 * Check if the page has any GUP (or other external) pins.
		 *
		 * Here the check is racy it may see totmal_mapcount > refcount
		 * in some cases.
		 * For example, one process with one forked child process.
		 * The parent has the PMD split due to MADV_DONTNEED, then
		 * the child is trying unmap the whole PMD, but khugepaged
		 * may be scanning the parent between the child has
		 * PageDoubleMap flag cleared and dec the mapcount.  So
		 * khugepaged may see total_mapcount > refcount.
		 *
		 * But such case is ephemeral we could always retry collapse
		 * later.  However it may report false positive if the page
		 * has excessive GUP pins (i.e. 512).  Anyway the same check
		 * will be done again later the risk seems low.
		 */
		if (!is_refcount_suitable(page)) {
			result = SCAN_PAGE_COUNT;
			goto out_unmap;
		}
		if (pte_young(pteval) ||
		    page_is_young(page) || PageReferenced(page) ||
		    mmu_notifier_test_young(vma->vm_mm, address))
			referenced++;
	}
	if (!writable) {
		result = SCAN_PAGE_RO;
	} else if (!referenced || (unmapped && referenced < HPAGE_PMD_NR/2)) {
		result = SCAN_LACK_REFERENCED_PAGE;
	} else {
		result = SCAN_SUCCEED;
		ret = 1;
	}
out_unmap:
	pte_unmap_unlock(pte, ptl);
	if (ret) {
		node = khugepaged_find_target_node();
		/* collapse_huge_page will return with the mmap_lock released */
		collapse_huge_page(mm, address, hpage, node,
				referenced, unmapped);
	}
out:
	trace_mm_khugepaged_scan_pmd(mm, page, writable, referenced,
				     none_or_zero, result, unmapped);
	return ret;
}

static void collect_mm_slot(struct mm_slot *mm_slot)
{
	struct mm_struct *mm = mm_slot->mm;

	lockdep_assert_held(&khugepaged_mm_lock);

	if (khugepaged_test_exit(mm)) {
		/* free mm_slot */
		hash_del(&mm_slot->hash);
		list_del(&mm_slot->mm_node);

		/*
		 * Not strictly needed because the mm exited already.
		 *
		 * clear_bit(MMF_VM_HUGEPAGE, &mm->flags);
		 */

		/* khugepaged_mm_lock actually not necessary for the below */
		free_mm_slot(mm_slot);
		mmdrop(mm);
	}
}

#ifdef CONFIG_SHMEM
/*
 * Notify khugepaged that given addr of the mm is pte-mapped THP. Then
 * khugepaged should try to collapse the page table.
 */
static int khugepaged_add_pte_mapped_thp(struct mm_struct *mm,
					 unsigned long addr)
{
	struct mm_slot *mm_slot;

	VM_BUG_ON(addr & ~HPAGE_PMD_MASK);

	spin_lock(&khugepaged_mm_lock);
	mm_slot = get_mm_slot(mm);
	if (likely(mm_slot && mm_slot->nr_pte_mapped_thp < MAX_PTE_MAPPED_THP))
		mm_slot->pte_mapped_thp[mm_slot->nr_pte_mapped_thp++] = addr;
	spin_unlock(&khugepaged_mm_lock);
	return 0;
}

/**
 * Try to collapse a pte-mapped THP for mm at address haddr.
 *
 * This function checks whether all the PTEs in the PMD are pointing to the
 * right THP. If so, retract the page table so the THP can refault in with
 * as pmd-mapped.
 */
void collapse_pte_mapped_thp(struct mm_struct *mm, unsigned long addr)
{
	unsigned long haddr = addr & HPAGE_PMD_MASK;
	struct vm_area_struct *vma = find_vma(mm, haddr);
	struct page *hpage;
	pte_t *start_pte, *pte;
	pmd_t *pmd, _pmd;
	spinlock_t *ptl;
	int count = 0;
	int i;

	if (!vma || !vma->vm_file ||
	    vma->vm_start > haddr || vma->vm_end < haddr + HPAGE_PMD_SIZE)
		return;

	/*
	 * This vm_flags may not have VM_HUGEPAGE if the page was not
	 * collapsed by this mm. But we can still collapse if the page is
	 * the valid THP. Add extra VM_HUGEPAGE so hugepage_vma_check()
	 * will not fail the vma for missing VM_HUGEPAGE
	 */
	if (!hugepage_vma_check(vma, vma->vm_flags | VM_HUGEPAGE))
		return;

	hpage = find_lock_page(vma->vm_file->f_mapping,
			       linear_page_index(vma, haddr));
	if (!hpage)
		return;

	if (!PageHead(hpage))
		goto drop_hpage;

	pmd = mm_find_pmd(mm, haddr);
	if (!pmd)
		goto drop_hpage;

	start_pte = pte_offset_map_lock(mm, pmd, haddr, &ptl);

	/* step 1: check all mapped PTEs are to the right huge page */
	for (i = 0, addr = haddr, pte = start_pte;
	     i < HPAGE_PMD_NR; i++, addr += PAGE_SIZE, pte++) {
		struct page *page;

		/* empty pte, skip */
		if (pte_none(*pte))
			continue;

		/* page swapped out, abort */
		if (!pte_present(*pte))
			goto abort;

		page = vm_normal_page(vma, addr, *pte);

		/*
		 * Note that uprobe, debugger, or MAP_PRIVATE may change the
		 * page table, but the new page will not be a subpage of hpage.
		 */
		if (hpage + i != page)
			goto abort;
		count++;
	}

	/* step 2: adjust rmap */
	for (i = 0, addr = haddr, pte = start_pte;
	     i < HPAGE_PMD_NR; i++, addr += PAGE_SIZE, pte++) {
		struct page *page;

		if (pte_none(*pte))
			continue;
		page = vm_normal_page(vma, addr, *pte);
		page_remove_rmap(page, false);
	}

	pte_unmap_unlock(start_pte, ptl);

	/* step 3: set proper refcount and mm_counters. */
	if (count) {
		page_ref_sub(hpage, count);
		add_mm_counter(vma->vm_mm, mm_counter_file(hpage), -count);
	}

	/* step 4: collapse pmd */
	ptl = pmd_lock(vma->vm_mm, pmd);
	_pmd = pmdp_collapse_flush(vma, haddr, pmd);
	spin_unlock(ptl);
	mm_dec_nr_ptes(mm);
	pte_free(mm, pmd_pgtable(_pmd));

drop_hpage:
	unlock_page(hpage);
	put_page(hpage);
	return;

abort:
	pte_unmap_unlock(start_pte, ptl);
	goto drop_hpage;
}

static int khugepaged_collapse_pte_mapped_thps(struct mm_slot *mm_slot)
{
	struct mm_struct *mm = mm_slot->mm;
	int i;

	if (likely(mm_slot->nr_pte_mapped_thp == 0))
		return 0;

	if (!mmap_write_trylock(mm))
		return -EBUSY;

	if (unlikely(khugepaged_test_exit(mm)))
		goto out;

	for (i = 0; i < mm_slot->nr_pte_mapped_thp; i++)
		collapse_pte_mapped_thp(mm, mm_slot->pte_mapped_thp[i]);

out:
	mm_slot->nr_pte_mapped_thp = 0;
	mmap_write_unlock(mm);
	return 0;
}

static void retract_page_tables(struct address_space *mapping, pgoff_t pgoff)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	unsigned long addr;
	pmd_t *pmd, _pmd;

	i_mmap_lock_write(mapping);
	vma_interval_tree_foreach(vma, &mapping->i_mmap, pgoff, pgoff) {
		/*
		 * Check vma->anon_vma to exclude MAP_PRIVATE mappings that
		 * got written to. These VMAs are likely not worth investing
		 * mmap_write_lock(mm) as PMD-mapping is likely to be split
		 * later.
		 *
		 * Not that vma->anon_vma check is racy: it can be set up after
		 * the check but before we took mmap_lock by the fault path.
		 * But page lock would prevent establishing any new ptes of the
		 * page, so we are safe.
		 *
		 * An alternative would be drop the check, but check that page
		 * table is clear before calling pmdp_collapse_flush() under
		 * ptl. It has higher chance to recover THP for the VMA, but
		 * has higher cost too.
		 */
		if (vma->anon_vma)
			continue;
		addr = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
		if (addr & ~HPAGE_PMD_MASK)
			continue;
		if (vma->vm_end < addr + HPAGE_PMD_SIZE)
			continue;
		mm = vma->vm_mm;
		pmd = mm_find_pmd(mm, addr);
		if (!pmd)
			continue;
		/*
		 * We need exclusive mmap_lock to retract page table.
		 *
		 * We use trylock due to lock inversion: we need to acquire
		 * mmap_lock while holding page lock. Fault path does it in
		 * reverse order. Trylock is a way to avoid deadlock.
		 */
		if (mmap_write_trylock(mm)) {
			if (!khugepaged_test_exit(mm)) {
				spinlock_t *ptl = pmd_lock(mm, pmd);
				/* assume page table is clear */
				_pmd = pmdp_collapse_flush(vma, addr, pmd);
				spin_unlock(ptl);
				mm_dec_nr_ptes(mm);
				pte_free(mm, pmd_pgtable(_pmd));
			}
			mmap_write_unlock(mm);
		} else {
			/* Try again later */
			khugepaged_add_pte_mapped_thp(mm, addr);
		}
	}
	i_mmap_unlock_write(mapping);
}

/**
 * collapse_file - collapse filemap/tmpfs/shmem pages into huge one.
 *
 * Basic scheme is simple, details are more complex:
 *  - allocate and lock a new huge page;
 *  - scan page cache replacing old pages with the new one
 *    + swap/gup in pages if necessary;
 *    + fill in gaps;
 *    + keep old pages around in case rollback is required;
 *  - if replacing succeeds:
 *    + copy data over;
 *    + free old pages;
 *    + unlock huge page;
 *  - if replacing failed;
 *    + put all pages back and unfreeze them;
 *    + restore gaps in the page cache;
 *    + unlock and free huge page;
 */
static void collapse_file(struct mm_struct *mm,
		struct file *file, pgoff_t start,
		struct page **hpage, int node)
{
	struct address_space *mapping = file->f_mapping;
	gfp_t gfp;
	struct page *new_page;
	pgoff_t index, end = start + HPAGE_PMD_NR;
	LIST_HEAD(pagelist);
	XA_STATE_ORDER(xas, &mapping->i_pages, start, HPAGE_PMD_ORDER);
	int nr_none = 0, result = SCAN_SUCCEED;
	bool is_shmem = shmem_file(file);

	VM_BUG_ON(!IS_ENABLED(CONFIG_READ_ONLY_THP_FOR_FS) && !is_shmem);
	VM_BUG_ON(start & (HPAGE_PMD_NR - 1));

	/* Only allocate from the target node */
	gfp = alloc_hugepage_khugepaged_gfpmask() | __GFP_THISNODE;

	new_page = khugepaged_alloc_page(hpage, gfp, node);
	if (!new_page) {
		result = SCAN_ALLOC_HUGE_PAGE_FAIL;
		goto out;
	}

	if (unlikely(mem_cgroup_charge(new_page, mm, gfp))) {
		result = SCAN_CGROUP_CHARGE_FAIL;
		goto out;
	}
	count_memcg_page_event(new_page, THP_COLLAPSE_ALLOC);

	/* This will be less messy when we use multi-index entries */
	do {
		xas_lock_irq(&xas);
		xas_create_range(&xas);
		if (!xas_error(&xas))
			break;
		xas_unlock_irq(&xas);
		if (!xas_nomem(&xas, GFP_KERNEL)) {
			result = SCAN_FAIL;
			goto out;
		}
	} while (1);

	__SetPageLocked(new_page);
	if (is_shmem)
		__SetPageSwapBacked(new_page);
	new_page->index = start;
	new_page->mapping = mapping;

	/*
	 * At this point the new_page is locked and not up-to-date.
	 * It's safe to insert it into the page cache, because nobody would
	 * be able to map it or use it in another way until we unlock it.
	 */

	xas_set(&xas, start);
	for (index = start; index < end; index++) {
		struct page *page = xas_next(&xas);

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
					xas_set(&xas, index);
				}
				if (!shmem_charge(mapping->host, 1)) {
					result = SCAN_FAIL;
					goto xa_locked;
				}
				xas_store(&xas, new_page);
				nr_none++;
				continue;
			}

			if (xa_is_value(page) || !PageUptodate(page)) {
				xas_unlock_irq(&xas);
				/* swap in or instantiate fallocated page */
				if (shmem_getpage(mapping->host, index, &page,
						  SGP_NOHUGE)) {
					result = SCAN_FAIL;
					goto xa_unlocked;
				}
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
							  PAGE_SIZE);
				/* drain pagevecs to help isolate_lru_page() */
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
		 */
		if (PageTransCompound(page)) {
			result = SCAN_PAGE_COMPOUND;
			goto out_unlock;
		}

		if (page_mapping(page) != mapping) {
			result = SCAN_TRUNCATED;
			goto out_unlock;
		}

		if (!is_shmem && PageDirty(page)) {
			/*
			 * khugepaged only works on read-only fd, so this
			 * page is dirty because it hasn't been flushed
			 * since first write.
			 */
			result = SCAN_FAIL;
			goto out_unlock;
		}

		if (isolate_lru_page(page)) {
			result = SCAN_DEL_PAGE_LRU;
			goto out_unlock;
		}

		if (page_has_private(page) &&
		    !try_to_release_page(page, GFP_KERNEL)) {
			result = SCAN_PAGE_HAS_PRIVATE;
			putback_lru_page(page);
			goto out_unlock;
		}

		if (page_mapped(page))
			unmap_mapping_pages(mapping, index, 1, false);

		xas_lock_irq(&xas);
		xas_set(&xas, index);

		VM_BUG_ON_PAGE(page != xas_load(&xas), page);
		VM_BUG_ON_PAGE(page_mapped(page), page);

		/*
		 * The page is expected to have page_count() == 3:
		 *  - we hold a pin on it;
		 *  - one reference from page cache;
		 *  - one from isolate_lru_page;
		 */
		if (!page_ref_freeze(page, 3)) {
			result = SCAN_PAGE_COUNT;
			xas_unlock_irq(&xas);
			putback_lru_page(page);
			goto out_unlock;
		}

		/*
		 * Add the page to the list to be able to undo the collapse if
		 * something go wrong.
		 */
		list_add_tail(&page->lru, &pagelist);

		/* Finally, replace with the new page. */
		xas_store(&xas, new_page);
		continue;
out_unlock:
		unlock_page(page);
		put_page(page);
		goto xa_unlocked;
	}

	if (is_shmem)
		__inc_node_page_state(new_page, NR_SHMEM_THPS);
	else {
		__inc_node_page_state(new_page, NR_FILE_THPS);
		filemap_nr_thps_inc(mapping);
	}

	if (nr_none) {
		__mod_lruvec_page_state(new_page, NR_FILE_PAGES, nr_none);
		if (is_shmem)
			__mod_lruvec_page_state(new_page, NR_SHMEM, nr_none);
	}

xa_locked:
	xas_unlock_irq(&xas);
xa_unlocked:

	if (result == SCAN_SUCCEED) {
		struct page *page, *tmp;

		/*
		 * Replacing old pages with new one has succeeded, now we
		 * need to copy the content and free the old pages.
		 */
		index = start;
		list_for_each_entry_safe(page, tmp, &pagelist, lru) {
			while (index < page->index) {
				clear_highpage(new_page + (index % HPAGE_PMD_NR));
				index++;
			}
			copy_highpage(new_page + (page->index % HPAGE_PMD_NR),
					page);
			list_del(&page->lru);
			page->mapping = NULL;
			page_ref_unfreeze(page, 1);
			ClearPageActive(page);
			ClearPageUnevictable(page);
			unlock_page(page);
			put_page(page);
			index++;
		}
		while (index < end) {
			clear_highpage(new_page + (index % HPAGE_PMD_NR));
			index++;
		}

		SetPageUptodate(new_page);
		page_ref_add(new_page, HPAGE_PMD_NR - 1);
		if (is_shmem)
			set_page_dirty(new_page);
		lru_cache_add(new_page);

		/*
		 * Remove pte page tables, so we can re-fault the page as huge.
		 */
		retract_page_tables(mapping, start);
		*hpage = NULL;

		khugepaged_pages_collapsed++;
	} else {
		struct page *page;

		/* Something went wrong: roll back page cache changes */
		xas_lock_irq(&xas);
		mapping->nrpages -= nr_none;

		if (is_shmem)
			shmem_uncharge(mapping->host, nr_none);

		xas_set(&xas, start);
		xas_for_each(&xas, page, end - 1) {
			page = list_first_entry_or_null(&pagelist,
					struct page, lru);
			if (!page || xas.xa_index < page->index) {
				if (!nr_none)
					break;
				nr_none--;
				/* Put holes back where they were */
				xas_store(&xas, NULL);
				continue;
			}

			VM_BUG_ON_PAGE(page->index != xas.xa_index, page);

			/* Unfreeze the page. */
			list_del(&page->lru);
			page_ref_unfreeze(page, 2);
			xas_store(&xas, page);
			xas_pause(&xas);
			xas_unlock_irq(&xas);
			unlock_page(page);
			putback_lru_page(page);
			xas_lock_irq(&xas);
		}
		VM_BUG_ON(nr_none);
		xas_unlock_irq(&xas);

		new_page->mapping = NULL;
	}

	unlock_page(new_page);
out:
	VM_BUG_ON(!list_empty(&pagelist));
	if (!IS_ERR_OR_NULL(*hpage))
		mem_cgroup_uncharge(*hpage);
	/* TODO: tracepoints */
}

static void khugepaged_scan_file(struct mm_struct *mm,
		struct file *file, pgoff_t start, struct page **hpage)
{
	struct page *page = NULL;
	struct address_space *mapping = file->f_mapping;
	XA_STATE(xas, &mapping->i_pages, start);
	int present, swap;
	int node = NUMA_NO_NODE;
	int result = SCAN_SUCCEED;

	present = 0;
	swap = 0;
	memset(khugepaged_node_load, 0, sizeof(khugepaged_node_load));
	rcu_read_lock();
	xas_for_each(&xas, page, start + HPAGE_PMD_NR - 1) {
		if (xas_retry(&xas, page))
			continue;

		if (xa_is_value(page)) {
			if (++swap > khugepaged_max_ptes_swap) {
				result = SCAN_EXCEED_SWAP_PTE;
				break;
			}
			continue;
		}

		if (PageTransCompound(page)) {
			result = SCAN_PAGE_COMPOUND;
			break;
		}

		node = page_to_nid(page);
		if (khugepaged_scan_abort(node)) {
			result = SCAN_SCAN_ABORT;
			break;
		}
		khugepaged_node_load[node]++;

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
		if (present < HPAGE_PMD_NR - khugepaged_max_ptes_none) {
			result = SCAN_EXCEED_NONE_PTE;
		} else {
			node = khugepaged_find_target_node();
			collapse_file(mm, file, start, hpage, node);
		}
	}

	/* TODO: tracepoints */
}
#else
static void khugepaged_scan_file(struct mm_struct *mm,
		struct file *file, pgoff_t start, struct page **hpage)
{
	BUILD_BUG();
}

static int khugepaged_collapse_pte_mapped_thps(struct mm_slot *mm_slot)
{
	return 0;
}
#endif

static unsigned int khugepaged_scan_mm_slot(unsigned int pages,
					    struct page **hpage)
	__releases(&khugepaged_mm_lock)
	__acquires(&khugepaged_mm_lock)
{
	struct mm_slot *mm_slot;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	int progress = 0;

	VM_BUG_ON(!pages);
	lockdep_assert_held(&khugepaged_mm_lock);

	if (khugepaged_scan.mm_slot)
		mm_slot = khugepaged_scan.mm_slot;
	else {
		mm_slot = list_entry(khugepaged_scan.mm_head.next,
				     struct mm_slot, mm_node);
		khugepaged_scan.address = 0;
		khugepaged_scan.mm_slot = mm_slot;
	}
	spin_unlock(&khugepaged_mm_lock);
	khugepaged_collapse_pte_mapped_thps(mm_slot);

	mm = mm_slot->mm;
	/*
	 * Don't wait for semaphore (to avoid long wait times).  Just move to
	 * the next mm on the list.
	 */
	vma = NULL;
	if (unlikely(!mmap_read_trylock(mm)))
		goto breakouterloop_mmap_lock;
	if (likely(!khugepaged_test_exit(mm)))
		vma = find_vma(mm, khugepaged_scan.address);

	progress++;
	for (; vma; vma = vma->vm_next) {
		unsigned long hstart, hend;

		cond_resched();
		if (unlikely(khugepaged_test_exit(mm))) {
			progress++;
			break;
		}
		if (!hugepage_vma_check(vma, vma->vm_flags)) {
skip:
			progress++;
			continue;
		}
		hstart = (vma->vm_start + ~HPAGE_PMD_MASK) & HPAGE_PMD_MASK;
		hend = vma->vm_end & HPAGE_PMD_MASK;
		if (hstart >= hend)
			goto skip;
		if (khugepaged_scan.address > hend)
			goto skip;
		if (khugepaged_scan.address < hstart)
			khugepaged_scan.address = hstart;
		VM_BUG_ON(khugepaged_scan.address & ~HPAGE_PMD_MASK);
		if (shmem_file(vma->vm_file) && !shmem_huge_enabled(vma))
			goto skip;

		while (khugepaged_scan.address < hend) {
			int ret;
			cond_resched();
			if (unlikely(khugepaged_test_exit(mm)))
				goto breakouterloop;

			VM_BUG_ON(khugepaged_scan.address < hstart ||
				  khugepaged_scan.address + HPAGE_PMD_SIZE >
				  hend);
			if (IS_ENABLED(CONFIG_SHMEM) && vma->vm_file) {
				struct file *file = get_file(vma->vm_file);
				pgoff_t pgoff = linear_page_index(vma,
						khugepaged_scan.address);

				mmap_read_unlock(mm);
				ret = 1;
				khugepaged_scan_file(mm, file, pgoff, hpage);
				fput(file);
			} else {
				ret = khugepaged_scan_pmd(mm, vma,
						khugepaged_scan.address,
						hpage);
			}
			/* move to next address */
			khugepaged_scan.address += HPAGE_PMD_SIZE;
			progress += HPAGE_PMD_NR;
			if (ret)
				/* we released mmap_lock so break loop */
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
	if (khugepaged_test_exit(mm) || !vma) {
		/*
		 * Make sure that if mm_users is reaching zero while
		 * khugepaged runs here, khugepaged_exit will find
		 * mm_slot not pointing to the exiting mm.
		 */
		if (mm_slot->mm_node.next != &khugepaged_scan.mm_head) {
			khugepaged_scan.mm_slot = list_entry(
				mm_slot->mm_node.next,
				struct mm_slot, mm_node);
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
		khugepaged_enabled();
}

static int khugepaged_wait_event(void)
{
	return !list_empty(&khugepaged_scan.mm_head) ||
		kthread_should_stop();
}

static void khugepaged_do_scan(void)
{
	struct page *hpage = NULL;
	unsigned int progress = 0, pass_through_head = 0;
	unsigned int pages = khugepaged_pages_to_scan;
	bool wait = true;

	barrier(); /* write khugepaged_pages_to_scan to local stack */

	lru_add_drain_all();

	while (progress < pages) {
		if (!khugepaged_prealloc_page(&hpage, &wait))
			break;

		cond_resched();

		if (unlikely(kthread_should_stop() || try_to_freeze()))
			break;

		spin_lock(&khugepaged_mm_lock);
		if (!khugepaged_scan.mm_slot)
			pass_through_head++;
		if (khugepaged_has_work() &&
		    pass_through_head < 2)
			progress += khugepaged_scan_mm_slot(pages - progress,
							    &hpage);
		else
			progress = pages;
		spin_unlock(&khugepaged_mm_lock);
	}

	if (!IS_ERR_OR_NULL(hpage))
		put_page(hpage);
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

	if (khugepaged_enabled())
		wait_event_freezable(khugepaged_wait, khugepaged_wait_event());
}

static int khugepaged(void *none)
{
	struct mm_slot *mm_slot;

	set_freezable();
	set_user_nice(current, MAX_NICE);

	while (!kthread_should_stop()) {
		khugepaged_do_scan();
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
	setup_per_zone_wmarks();
}

int start_stop_khugepaged(void)
{
	static struct task_struct *khugepaged_thread __read_mostly;
	static DEFINE_MUTEX(khugepaged_mutex);
	int err = 0;

	mutex_lock(&khugepaged_mutex);
	if (khugepaged_enabled()) {
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

		set_recommended_min_free_kbytes();
	} else if (khugepaged_thread) {
		kthread_stop(khugepaged_thread);
		khugepaged_thread = NULL;
	}
fail:
	mutex_unlock(&khugepaged_mutex);
	return err;
}
