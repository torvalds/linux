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
static __read_mostly DEFINE_HASHTABLE(mm_slots_hash, MM_SLOTS_HASH_BITS);

static struct kmem_cache *mm_slot_cache __read_mostly;

#define MAX_PTE_MAPPED_THP 8

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
 * @nr_pte_mapped_thp: number of pte mapped THP
 * @pte_mapped_thp: address array corresponding pte mapped THP
 */
struct khugepaged_mm_slot {
	struct mm_slot slot;

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

	mm_slot = mm_slot_alloc(mm_slot_cache);
	if (!mm_slot)
		return;

	slot = &mm_slot->slot;

	/* __khugepaged_exit() must not run from under us */
	VM_BUG_ON_MM(hpage_collapse_test_exit(mm), mm);
	if (unlikely(test_and_set_bit(MMF_VM_HUGEPAGE, &mm->flags))) {
		mm_slot_free(mm_slot_cache, mm_slot);
		return;
	}

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
		if (hugepage_vma_check(vma, vm_flags, false, false, true))
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
					struct collapse_control *cc,
					struct list_head *compound_pagelist)
{
	struct page *page = NULL;
	pte_t *_pte;
	int none_or_zero = 0, shared = 0, result = SCAN_FAIL, referenced = 0;
	bool writable = false;

	for (_pte = pte; _pte < pte + HPAGE_PMD_NR;
	     _pte++, address += PAGE_SIZE) {
		pte_t pteval = *_pte;
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
		page = vm_normal_page(vma, address, pteval);
		if (unlikely(!page) || unlikely(is_zone_device_page(page))) {
			result = SCAN_PAGE_NULL;
			goto out;
		}

		VM_BUG_ON_PAGE(!PageAnon(page), page);

		if (page_mapcount(page) > 1) {
			++shared;
			if (cc->is_khugepaged &&
			    shared > khugepaged_max_ptes_shared) {
				result = SCAN_EXCEED_SHARED_PTE;
				count_vm_event(THP_SCAN_EXCEED_SHARED_PTE);
				goto out;
			}
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
		 * an additional pin on the page.
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
		/*
		 * If collapse was initiated by khugepaged, check that there is
		 * enough young pte to justify collapsing the page
		 */
		if (cc->is_khugepaged &&
		    (pte_young(pteval) || page_is_young(page) ||
		     PageReferenced(page) || mmu_notifier_test_young(vma->vm_mm,
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
		trace_mm_collapse_huge_page_isolate(page, none_or_zero,
						    referenced, writable, result);
		return result;
	}
out:
	release_pte_pages(pte, _pte, compound_pagelist);
	trace_mm_collapse_huge_page_isolate(page, none_or_zero,
					    referenced, writable, result);
	return result;
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
				ptep_clear(vma->vm_mm, address, _pte);
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
			ptep_clear(vma->vm_mm, address, _pte);
			page_remove_rmap(src_page, vma, false);
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

static bool hpage_collapse_alloc_page(struct page **hpage, gfp_t gfp, int node,
				      nodemask_t *nmask)
{
	*hpage = __alloc_pages(gfp, HPAGE_PMD_ORDER, node, nmask);
	if (unlikely(!*hpage)) {
		count_vm_event(THP_COLLAPSE_ALLOC_FAILED);
		return false;
	}

	prep_transhuge_page(*hpage);
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

	if (!transhuge_vma_suitable(vma, address))
		return SCAN_ADDRESS_RANGE;
	if (!hugepage_vma_check(vma, vma->vm_flags, false, false,
				cc->is_khugepaged))
		return SCAN_VMA_CHECK;
	/*
	 * Anon VMA expected, the address may be unmapped then
	 * remapped to file after khugepaged reaquired the mmap_lock.
	 *
	 * hugepage_vma_check may return true for qualified file
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

	pmde = pmd_read_atomic(*pmd);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	/* See comments in pmd_none_or_trans_huge_or_clear_bad() */
	barrier();
#endif
	if (pmd_none(pmde))
		return SCAN_PMD_NONE;
	if (pmd_trans_huge(pmde))
		return SCAN_PMD_MAPPED;
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
 * Note that if false is returned, mmap_lock will be released.
 */

static int __collapse_huge_page_swapin(struct mm_struct *mm,
				       struct vm_area_struct *vma,
				       unsigned long haddr, pmd_t *pmd,
				       int referenced)
{
	int swapped_in = 0;
	vm_fault_t ret = 0;
	unsigned long address, end = haddr + (HPAGE_PMD_NR * PAGE_SIZE);

	for (address = haddr; address < end; address += PAGE_SIZE) {
		struct vm_fault vmf = {
			.vma = vma,
			.address = address,
			.pgoff = linear_page_index(vma, haddr),
			.flags = FAULT_FLAG_ALLOW_RETRY,
			.pmd = pmd,
		};

		vmf.pte = pte_offset_map(pmd, address);
		vmf.orig_pte = *vmf.pte;
		if (!is_swap_pte(vmf.orig_pte)) {
			pte_unmap(vmf.pte);
			continue;
		}
		ret = do_swap_page(&vmf);

		/*
		 * do_swap_page returns VM_FAULT_RETRY with released mmap_lock.
		 * Note we treat VM_FAULT_RETRY as VM_FAULT_ERROR here because
		 * we do not retry here and swap entry will remain in pagetable
		 * resulting in later failure.
		 */
		if (ret & VM_FAULT_RETRY) {
			trace_mm_collapse_huge_page_swapin(mm, swapped_in, referenced, 0);
			/* Likely, but not guaranteed, that page lock failed */
			return SCAN_PAGE_LOCK;
		}
		if (ret & VM_FAULT_ERROR) {
			mmap_read_unlock(mm);
			trace_mm_collapse_huge_page_swapin(mm, swapped_in, referenced, 0);
			return SCAN_FAIL;
		}
		swapped_in++;
	}

	/* Drain LRU add pagevec to remove extra pin on the swapped in pages */
	if (swapped_in)
		lru_add_drain();

	trace_mm_collapse_huge_page_swapin(mm, swapped_in, referenced, 1);
	return SCAN_SUCCEED;
}

static int alloc_charge_hpage(struct page **hpage, struct mm_struct *mm,
			      struct collapse_control *cc)
{
	gfp_t gfp = (cc->is_khugepaged ? alloc_hugepage_khugepaged_gfpmask() :
		     GFP_TRANSHUGE);
	int node = hpage_collapse_find_target_node(cc);

	if (!hpage_collapse_alloc_page(hpage, gfp, node, &cc->alloc_nmask))
		return SCAN_ALLOC_HUGE_PAGE_FAIL;
	if (unlikely(mem_cgroup_charge(page_folio(*hpage), mm, gfp)))
		return SCAN_CGROUP_CHARGE_FAIL;
	count_memcg_page_event(*hpage, THP_COLLAPSE_ALLOC);
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
	 */
	mmap_write_lock(mm);
	result = hugepage_vma_revalidate(mm, address, true, &vma, cc);
	if (result != SCAN_SUCCEED)
		goto out_up_write;
	/* check if the pmd is still valid */
	result = check_pmd_still_valid(mm, address, pmd);
	if (result != SCAN_SUCCEED)
		goto out_up_write;

	anon_vma_lock_write(vma->anon_vma);

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, NULL, mm,
				address, address + HPAGE_PMD_SIZE);
	mmu_notifier_invalidate_range_start(&range);

	pte = pte_offset_map(pmd, address);
	pte_ptl = pte_lockptr(mm, pmd);

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

	spin_lock(pte_ptl);
	result =  __collapse_huge_page_isolate(vma, address, pte, cc,
					       &compound_pagelist);
	spin_unlock(pte_ptl);

	if (unlikely(result != SCAN_SUCCEED)) {
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

	__collapse_huge_page_copy(pte, hpage, vma, address, pte_ptl,
				  &compound_pagelist);
	pte_unmap(pte);
	/*
	 * spin_lock() below is not the equivalent of smp_wmb(), but
	 * the smp_wmb() inside __SetPageUptodate() can be reused to
	 * avoid the copy_huge_page writes to become visible after
	 * the set_pmd_at() write.
	 */
	__SetPageUptodate(hpage);
	pgtable = pmd_pgtable(_pmd);

	_pmd = mk_huge_pmd(hpage, vma->vm_page_prot);
	_pmd = maybe_pmd_mkwrite(pmd_mkdirty(_pmd), vma);

	spin_lock(pmd_ptl);
	BUG_ON(!pmd_none(*pmd));
	page_add_new_anon_rmap(hpage, vma, address);
	lru_cache_add_inactive_or_unevictable(hpage, vma);
	pgtable_trans_huge_deposit(mm, pmd, pgtable);
	set_pmd_at(mm, address, pmd, _pmd);
	update_mmu_cache_pmd(vma, address, pmd);
	spin_unlock(pmd_ptl);

	hpage = NULL;

	result = SCAN_SUCCEED;
out_up_write:
	mmap_write_unlock(mm);
out_nolock:
	if (hpage) {
		mem_cgroup_uncharge(page_folio(hpage));
		put_page(hpage);
	}
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
	for (_address = address, _pte = pte; _pte < pte + HPAGE_PMD_NR;
	     _pte++, _address += PAGE_SIZE) {
		pte_t pteval = *_pte;
		if (is_swap_pte(pteval)) {
			++unmapped;
			if (!cc->is_khugepaged ||
			    unmapped <= khugepaged_max_ptes_swap) {
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

		page = compound_head(page);

		/*
		 * Record which node the original page is from and save this
		 * information to cc->node_load[].
		 * Khugepaged will allocate hugepage from the node has the max
		 * hit record.
		 */
		node = page_to_nid(page);
		if (hpage_collapse_scan_abort(node, cc)) {
			result = SCAN_SCAN_ABORT;
			goto out_unmap;
		}
		cc->node_load[node]++;
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
		 * Here the check is racy it may see total_mapcount > refcount
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

		/*
		 * If collapse was initiated by khugepaged, check that there is
		 * enough young pte to justify collapsing the page
		 */
		if (cc->is_khugepaged &&
		    (pte_young(pteval) || page_is_young(page) ||
		     PageReferenced(page) || mmu_notifier_test_young(vma->vm_mm,
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
	trace_mm_khugepaged_scan_pmd(mm, page, writable, referenced,
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
/*
 * Notify khugepaged that given addr of the mm is pte-mapped THP. Then
 * khugepaged should try to collapse the page table.
 *
 * Note that following race exists:
 * (1) khugepaged calls khugepaged_collapse_pte_mapped_thps() for mm_struct A,
 *     emptying the A's ->pte_mapped_thp[] array.
 * (2) MADV_COLLAPSE collapses some file extent with target mm_struct B, and
 *     retract_page_tables() finds a VMA in mm_struct A mapping the same extent
 *     (at virtual address X) and adds an entry (for X) into mm_struct A's
 *     ->pte-mapped_thp[] array.
 * (3) khugepaged calls khugepaged_collapse_scan_file() for mm_struct A at X,
 *     sees a pte-mapped THP (SCAN_PTE_MAPPED_HUGEPAGE) and adds an entry
 *     (for X) into mm_struct A's ->pte-mapped_thp[] array.
 * Thus, it's possible the same address is added multiple times for the same
 * mm_struct.  Should this happen, we'll simply attempt
 * collapse_pte_mapped_thp() multiple times for the same address, under the same
 * exclusive mmap_lock, and assuming the first call is successful, subsequent
 * attempts will return quickly (without grabbing any additional locks) when
 * a huge pmd is found in find_pmd_or_thp_or_none().  Since this is a cheap
 * check, and since this is a rare occurrence, the cost of preventing this
 * "multiple-add" is thought to be more expensive than just handling it, should
 * it occur.
 */
static bool khugepaged_add_pte_mapped_thp(struct mm_struct *mm,
					  unsigned long addr)
{
	struct khugepaged_mm_slot *mm_slot;
	struct mm_slot *slot;
	bool ret = false;

	VM_BUG_ON(addr & ~HPAGE_PMD_MASK);

	spin_lock(&khugepaged_mm_lock);
	slot = mm_slot_lookup(mm_slots_hash, mm);
	mm_slot = mm_slot_entry(slot, struct khugepaged_mm_slot, slot);
	if (likely(mm_slot && mm_slot->nr_pte_mapped_thp < MAX_PTE_MAPPED_THP)) {
		mm_slot->pte_mapped_thp[mm_slot->nr_pte_mapped_thp++] = addr;
		ret = true;
	}
	spin_unlock(&khugepaged_mm_lock);
	return ret;
}

/* hpage must be locked, and mmap_lock must be held in write */
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
	mmap_assert_write_locked(vma->vm_mm);

	if (do_set_pmd(&vmf, hpage))
		return SCAN_FAIL;

	get_page(hpage);
	return SCAN_SUCCEED;
}

/*
 * A note about locking:
 * Trying to take the page table spinlocks would be useless here because those
 * are only used to synchronize:
 *
 *  - modifying terminal entries (ones that point to a data page, not to another
 *    page table)
 *  - installing *new* non-terminal entries
 *
 * Instead, we need roughly the same kind of protection as free_pgtables() or
 * mm_take_all_locks() (but only for a single VMA):
 * The mmap lock together with this VMA's rmap locks covers all paths towards
 * the page table entries we're messing with here, except for hardware page
 * table walks and lockless_pages_from_mm().
 */
static void collapse_and_free_pmd(struct mm_struct *mm, struct vm_area_struct *vma,
				  unsigned long addr, pmd_t *pmdp)
{
	pmd_t pmd;
	struct mmu_notifier_range range;

	mmap_assert_write_locked(mm);
	if (vma->vm_file)
		lockdep_assert_held_write(&vma->vm_file->f_mapping->i_mmap_rwsem);
	/*
	 * All anon_vmas attached to the VMA have the same root and are
	 * therefore locked by the same lock.
	 */
	if (vma->anon_vma)
		lockdep_assert_held_write(&vma->anon_vma->root->rwsem);

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, NULL, mm, addr,
				addr + HPAGE_PMD_SIZE);
	mmu_notifier_invalidate_range_start(&range);
	pmd = pmdp_collapse_flush(vma, addr, pmdp);
	tlb_remove_table_sync_one();
	mmu_notifier_invalidate_range_end(&range);
	mm_dec_nr_ptes(mm);
	page_table_check_pte_clear_range(mm, addr, pmd);
	pte_free(mm, pmd_pgtable(pmd));
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
	unsigned long haddr = addr & HPAGE_PMD_MASK;
	struct vm_area_struct *vma = vma_lookup(mm, haddr);
	struct page *hpage;
	pte_t *start_pte, *pte;
	pmd_t *pmd;
	spinlock_t *ptl;
	int count = 0, result = SCAN_FAIL;
	int i;

	mmap_assert_write_locked(mm);

	/* Fast check before locking page if already PMD-mapped */
	result = find_pmd_or_thp_or_none(mm, haddr, &pmd);
	if (result == SCAN_PMD_MAPPED)
		return result;

	if (!vma || !vma->vm_file ||
	    !range_in_vma(vma, haddr, haddr + HPAGE_PMD_SIZE))
		return SCAN_VMA_CHECK;

	/*
	 * If we are here, we've succeeded in replacing all the native pages
	 * in the page cache with a single hugepage. If a mm were to fault-in
	 * this memory (mapped by a suitably aligned VMA), we'd get the hugepage
	 * and map it by a PMD, regardless of sysfs THP settings. As such, let's
	 * analogously elide sysfs THP settings here.
	 */
	if (!hugepage_vma_check(vma, vma->vm_flags, false, false, false))
		return SCAN_VMA_CHECK;

	/*
	 * Symmetry with retract_page_tables(): Exclude MAP_PRIVATE mappings
	 * that got written to. Without this, we'd have to also lock the
	 * anon_vma if one exists.
	 */
	if (vma->anon_vma)
		return SCAN_VMA_CHECK;

	/* Keep pmd pgtable for uffd-wp; see comment in retract_page_tables() */
	if (userfaultfd_wp(vma))
		return SCAN_PTE_UFFD_WP;

	hpage = find_lock_page(vma->vm_file->f_mapping,
			       linear_page_index(vma, haddr));
	if (!hpage)
		return SCAN_PAGE_NULL;

	if (!PageHead(hpage)) {
		result = SCAN_FAIL;
		goto drop_hpage;
	}

	if (compound_order(hpage) != HPAGE_PMD_ORDER) {
		result = SCAN_PAGE_COMPOUND;
		goto drop_hpage;
	}

	switch (result) {
	case SCAN_SUCCEED:
		break;
	case SCAN_PMD_NONE:
		/*
		 * In MADV_COLLAPSE path, possible race with khugepaged where
		 * all pte entries have been removed and pmd cleared.  If so,
		 * skip all the pte checks and just update the pmd mapping.
		 */
		goto maybe_install_pmd;
	default:
		goto drop_hpage;
	}

	/*
	 * We need to lock the mapping so that from here on, only GUP-fast and
	 * hardware page walks can access the parts of the page tables that
	 * we're operating on.
	 * See collapse_and_free_pmd().
	 */
	i_mmap_lock_write(vma->vm_file->f_mapping);

	/*
	 * This spinlock should be unnecessary: Nobody else should be accessing
	 * the page tables under spinlock protection here, only
	 * lockless_pages_from_mm() and the hardware page walker can access page
	 * tables while all the high-level locks are held in write mode.
	 */
	start_pte = pte_offset_map_lock(mm, pmd, haddr, &ptl);
	result = SCAN_FAIL;

	/* step 1: check all mapped PTEs are to the right huge page */
	for (i = 0, addr = haddr, pte = start_pte;
	     i < HPAGE_PMD_NR; i++, addr += PAGE_SIZE, pte++) {
		struct page *page;

		/* empty pte, skip */
		if (pte_none(*pte))
			continue;

		/* page swapped out, abort */
		if (!pte_present(*pte)) {
			result = SCAN_PTE_NON_PRESENT;
			goto abort;
		}

		page = vm_normal_page(vma, addr, *pte);
		if (WARN_ON_ONCE(page && is_zone_device_page(page)))
			page = NULL;
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
		if (WARN_ON_ONCE(page && is_zone_device_page(page)))
			goto abort;
		page_remove_rmap(page, vma, false);
	}

	pte_unmap_unlock(start_pte, ptl);

	/* step 3: set proper refcount and mm_counters. */
	if (count) {
		page_ref_sub(hpage, count);
		add_mm_counter(vma->vm_mm, mm_counter_file(hpage), -count);
	}

	/* step 4: remove pte entries */
	collapse_and_free_pmd(mm, vma, haddr, pmd);

	i_mmap_unlock_write(vma->vm_file->f_mapping);

maybe_install_pmd:
	/* step 5: install pmd entry */
	result = install_pmd
			? set_huge_pmd(vma, haddr, pmd, hpage)
			: SCAN_SUCCEED;

drop_hpage:
	unlock_page(hpage);
	put_page(hpage);
	return result;

abort:
	pte_unmap_unlock(start_pte, ptl);
	i_mmap_unlock_write(vma->vm_file->f_mapping);
	goto drop_hpage;
}

static void khugepaged_collapse_pte_mapped_thps(struct khugepaged_mm_slot *mm_slot)
{
	struct mm_slot *slot = &mm_slot->slot;
	struct mm_struct *mm = slot->mm;
	int i;

	if (likely(mm_slot->nr_pte_mapped_thp == 0))
		return;

	if (!mmap_write_trylock(mm))
		return;

	if (unlikely(hpage_collapse_test_exit(mm)))
		goto out;

	for (i = 0; i < mm_slot->nr_pte_mapped_thp; i++)
		collapse_pte_mapped_thp(mm, mm_slot->pte_mapped_thp[i], false);

out:
	mm_slot->nr_pte_mapped_thp = 0;
	mmap_write_unlock(mm);
}

static int retract_page_tables(struct address_space *mapping, pgoff_t pgoff,
			       struct mm_struct *target_mm,
			       unsigned long target_addr, struct page *hpage,
			       struct collapse_control *cc)
{
	struct vm_area_struct *vma;
	int target_result = SCAN_FAIL;

	i_mmap_lock_write(mapping);
	vma_interval_tree_foreach(vma, &mapping->i_mmap, pgoff, pgoff) {
		int result = SCAN_FAIL;
		struct mm_struct *mm = NULL;
		unsigned long addr = 0;
		pmd_t *pmd;
		bool is_target = false;

		/*
		 * Check vma->anon_vma to exclude MAP_PRIVATE mappings that
		 * got written to. These VMAs are likely not worth investing
		 * mmap_write_lock(mm) as PMD-mapping is likely to be split
		 * later.
		 *
		 * Note that vma->anon_vma check is racy: it can be set up after
		 * the check but before we took mmap_lock by the fault path.
		 * But page lock would prevent establishing any new ptes of the
		 * page, so we are safe.
		 *
		 * An alternative would be drop the check, but check that page
		 * table is clear before calling pmdp_collapse_flush() under
		 * ptl. It has higher chance to recover THP for the VMA, but
		 * has higher cost too. It would also probably require locking
		 * the anon_vma.
		 */
		if (vma->anon_vma) {
			result = SCAN_PAGE_ANON;
			goto next;
		}
		addr = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
		if (addr & ~HPAGE_PMD_MASK ||
		    vma->vm_end < addr + HPAGE_PMD_SIZE) {
			result = SCAN_VMA_CHECK;
			goto next;
		}
		mm = vma->vm_mm;
		is_target = mm == target_mm && addr == target_addr;
		result = find_pmd_or_thp_or_none(mm, addr, &pmd);
		if (result != SCAN_SUCCEED)
			goto next;
		/*
		 * We need exclusive mmap_lock to retract page table.
		 *
		 * We use trylock due to lock inversion: we need to acquire
		 * mmap_lock while holding page lock. Fault path does it in
		 * reverse order. Trylock is a way to avoid deadlock.
		 *
		 * Also, it's not MADV_COLLAPSE's job to collapse other
		 * mappings - let khugepaged take care of them later.
		 */
		result = SCAN_PTE_MAPPED_HUGEPAGE;
		if ((cc->is_khugepaged || is_target) &&
		    mmap_write_trylock(mm)) {
			/*
			 * When a vma is registered with uffd-wp, we can't
			 * recycle the pmd pgtable because there can be pte
			 * markers installed.  Skip it only, so the rest mm/vma
			 * can still have the same file mapped hugely, however
			 * it'll always mapped in small page size for uffd-wp
			 * registered ranges.
			 */
			if (hpage_collapse_test_exit(mm)) {
				result = SCAN_ANY_PROCESS;
				goto unlock_next;
			}
			if (userfaultfd_wp(vma)) {
				result = SCAN_PTE_UFFD_WP;
				goto unlock_next;
			}
			collapse_and_free_pmd(mm, vma, addr, pmd);
			if (!cc->is_khugepaged && is_target)
				result = set_huge_pmd(vma, addr, pmd, hpage);
			else
				result = SCAN_SUCCEED;

unlock_next:
			mmap_write_unlock(mm);
			goto next;
		}
		/*
		 * Calling context will handle target mm/addr. Otherwise, let
		 * khugepaged try again later.
		 */
		if (!is_target) {
			khugepaged_add_pte_mapped_thp(mm, addr);
			continue;
		}
next:
		if (is_target)
			target_result = result;
	}
	i_mmap_unlock_write(mapping);
	return target_result;
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
static int collapse_file(struct mm_struct *mm, unsigned long addr,
			 struct file *file, pgoff_t start,
			 struct collapse_control *cc)
{
	struct address_space *mapping = file->f_mapping;
	struct page *hpage;
	pgoff_t index, end = start + HPAGE_PMD_NR;
	LIST_HEAD(pagelist);
	XA_STATE_ORDER(xas, &mapping->i_pages, start, HPAGE_PMD_ORDER);
	int nr_none = 0, result = SCAN_SUCCEED;
	bool is_shmem = shmem_file(file);
	int nr;

	VM_BUG_ON(!IS_ENABLED(CONFIG_READ_ONLY_THP_FOR_FS) && !is_shmem);
	VM_BUG_ON(start & (HPAGE_PMD_NR - 1));

	result = alloc_charge_hpage(&hpage, mm, cc);
	if (result != SCAN_SUCCEED)
		goto out;

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
			goto out;
		}
	} while (1);

	__SetPageLocked(hpage);
	if (is_shmem)
		__SetPageSwapBacked(hpage);
	hpage->index = start;
	hpage->mapping = mapping;

	/*
	 * At this point the hpage is locked and not up-to-date.
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
				xas_store(&xas, hpage);
				nr_none++;
				continue;
			}

			if (xa_is_value(page) || !PageUptodate(page)) {
				struct folio *folio;

				xas_unlock_irq(&xas);
				/* swap in or instantiate fallocated page */
				if (shmem_get_folio(mapping->host, index,
						&folio, SGP_NOALLOC)) {
					result = SCAN_FAIL;
					goto xa_unlocked;
				}
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

		if (page_mapping(page) != mapping) {
			result = SCAN_TRUNCATED;
			goto out_unlock;
		}

		if (!is_shmem && (PageDirty(page) ||
				  PageWriteback(page))) {
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
			try_to_unmap(page_folio(page),
					TTU_IGNORE_MLOCK | TTU_BATCH_FLUSH);

		xas_lock_irq(&xas);
		xas_set(&xas, index);

		VM_BUG_ON_PAGE(page != xas_load(&xas), page);

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
		xas_store(&xas, hpage);
		continue;
out_unlock:
		unlock_page(page);
		put_page(page);
		goto xa_unlocked;
	}
	nr = thp_nr_pages(hpage);

	if (is_shmem)
		__mod_lruvec_page_state(hpage, NR_SHMEM_THPS, nr);
	else {
		__mod_lruvec_page_state(hpage, NR_FILE_THPS, nr);
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
			__mod_lruvec_page_state(hpage, NR_FILE_THPS, -nr);
			filemap_nr_thps_dec(mapping);
			goto xa_locked;
		}
	}

	if (nr_none) {
		__mod_lruvec_page_state(hpage, NR_FILE_PAGES, nr_none);
		/* nr_none is always 0 for non-shmem. */
		__mod_lruvec_page_state(hpage, NR_SHMEM, nr_none);
	}

	/* Join all the small entries into a single multi-index entry */
	xas_set_order(&xas, start, HPAGE_PMD_ORDER);
	xas_store(&xas, hpage);
xa_locked:
	xas_unlock_irq(&xas);
xa_unlocked:

	/*
	 * If collapse is successful, flush must be done now before copying.
	 * If collapse is unsuccessful, does flush actually need to be done?
	 * Do it anyway, to clear the state.
	 */
	try_to_unmap_flush();

	if (result == SCAN_SUCCEED) {
		struct page *page, *tmp;

		/*
		 * Replacing old pages with new one has succeeded, now we
		 * need to copy the content and free the old pages.
		 */
		index = start;
		list_for_each_entry_safe(page, tmp, &pagelist, lru) {
			while (index < page->index) {
				clear_highpage(hpage + (index % HPAGE_PMD_NR));
				index++;
			}
			copy_highpage(hpage + (page->index % HPAGE_PMD_NR),
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
			clear_highpage(hpage + (index % HPAGE_PMD_NR));
			index++;
		}

		SetPageUptodate(hpage);
		page_ref_add(hpage, HPAGE_PMD_NR - 1);
		if (is_shmem)
			set_page_dirty(hpage);
		lru_cache_add(hpage);

		/*
		 * Remove pte page tables, so we can re-fault the page as huge.
		 */
		result = retract_page_tables(mapping, start, mm, addr, hpage,
					     cc);
		unlock_page(hpage);
		hpage = NULL;
	} else {
		struct page *page;

		/* Something went wrong: roll back page cache changes */
		xas_lock_irq(&xas);
		if (nr_none) {
			mapping->nrpages -= nr_none;
			shmem_uncharge(mapping->host, nr_none);
		}

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

		hpage->mapping = NULL;
	}

	if (hpage)
		unlock_page(hpage);
out:
	VM_BUG_ON(!list_empty(&pagelist));
	if (hpage) {
		mem_cgroup_uncharge(page_folio(hpage));
		put_page(hpage);
	}
	/* TODO: tracepoints */
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

static void khugepaged_collapse_pte_mapped_thps(struct khugepaged_mm_slot *mm_slot)
{
}

static bool khugepaged_add_pte_mapped_thp(struct mm_struct *mm,
					  unsigned long addr)
{
	return false;
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
	khugepaged_collapse_pte_mapped_thps(mm_slot);

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
		if (!hugepage_vma_check(vma, vma->vm_flags, false, false, true)) {
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
				*result = hpage_collapse_scan_file(mm,
								   khugepaged_scan.address,
								   file, pgoff, cc);
				mmap_locked = false;
				fput(file);
			} else {
				*result = hpage_collapse_scan_pmd(mm, vma,
								  khugepaged_scan.address,
								  &mmap_locked,
								  cc);
			}
			switch (*result) {
			case SCAN_PTE_MAPPED_HUGEPAGE: {
				pmd_t *pmd;

				*result = find_pmd_or_thp_or_none(mm,
								  khugepaged_scan.address,
								  &pmd);
				if (*result != SCAN_SUCCEED)
					break;
				if (!khugepaged_add_pte_mapped_thp(mm,
								   khugepaged_scan.address))
					break;
			} fallthrough;
			case SCAN_SUCCEED:
				++khugepaged_pages_collapsed;
				break;
			default:
				break;
			}

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

		if (unlikely(kthread_should_stop() || try_to_freeze()))
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
		return -EBUSY;
	/* Resource temporary unavailable - trying again might succeed */
	case SCAN_PAGE_LOCK:
	case SCAN_PAGE_LRU:
	case SCAN_DEL_PAGE_LRU:
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

	if (!hugepage_vma_check(vma, vma->vm_flags, false, false, false))
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

			hend = vma->vm_end & HPAGE_PMD_MASK;
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
			mmap_write_lock(mm);
			result = collapse_pte_mapped_thp(mm, addr, true);
			mmap_write_unlock(mm);
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
