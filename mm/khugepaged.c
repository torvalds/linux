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
	SCAN_PTE_NON_PRESENT,
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
	SCAN_EXCEED_SWAP_PTE,
	SCAN_TRUNCATED,
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

#define MM_SLOTS_HASH_BITS 10
static __read_mostly DEFINE_HASHTABLE(mm_slots_hash, MM_SLOTS_HASH_BITS);

static struct kmem_cache *mm_slot_cache __read_mostly;

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

static struct attribute *khugepaged_attr[] = {
	&khugepaged_defrag_attr.attr,
	&khugepaged_max_ptes_none_attr.attr,
	&pages_to_scan_attr.attr,
	&pages_collapsed_attr.attr,
	&full_scans_attr.attr,
	&scan_sleep_millisecs_attr.attr,
	&alloc_sleep_millisecs_attr.attr,
	&khugepaged_max_ptes_swap_attr.attr,
	NULL,
};

struct attribute_group khugepaged_attr_group = {
	.attrs = khugepaged_attr,
	.name = "khugepaged",
};
#endif /* CONFIG_SYSFS */

#define VM_NO_KHUGEPAGED (VM_SPECIAL | VM_HUGETLB)

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
	return atomic_read(&mm->mm_users) == 0;
}

int __khugepaged_enter(struct mm_struct *mm)
{
	struct mm_slot *mm_slot;
	int wakeup;

	mm_slot = alloc_mm_slot();
	if (!mm_slot)
		return -ENOMEM;

	/* __khugepaged_exit() must not run from under us */
	VM_BUG_ON_MM(khugepaged_test_exit(mm), mm);
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
	if (!vma->anon_vma)
		/*
		 * Not yet faulted in so we will register later in the
		 * page fault if needed.
		 */
		return 0;
	if (vma->vm_ops || (vm_flags & VM_NO_KHUGEPAGED))
		/* khugepaged not yet working on file or special mappings */
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
		 * under the mmap_sem.
		 */
		down_write(&mm->mmap_sem);
		up_write(&mm->mmap_sem);
	}
}

static void release_pte_page(struct page *page)
{
	dec_node_page_state(page, NR_ISOLATED_ANON + page_is_file_cache(page));
	unlock_page(page);
	putback_lru_page(page);
}

static void release_pte_pages(pte_t *pte, pte_t *_pte)
{
	while (--_pte >= pte) {
		pte_t pteval = *_pte;
		if (!pte_none(pteval) && !is_zero_pfn(pte_pfn(pteval)))
			release_pte_page(pte_page(pteval));
	}
}

static int __collapse_huge_page_isolate(struct vm_area_struct *vma,
					unsigned long address,
					pte_t *pte)
{
	struct page *page = NULL;
	pte_t *_pte;
	int none_or_zero = 0, result = 0, referenced = 0;
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

		/* TODO: teach khugepaged to collapse THP mapped with pte */
		if (PageCompound(page)) {
			result = SCAN_PAGE_COMPOUND;
			goto out;
		}

		VM_BUG_ON_PAGE(!PageAnon(page), page);

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
		 * cannot use mapcount: can't collapse if there's a gup pin.
		 * The page must only be referenced by the scanned process
		 * and page swap cache.
		 */
		if (page_count(page) != 1 + PageSwapCache(page)) {
			unlock_page(page);
			result = SCAN_PAGE_COUNT;
			goto out;
		}
		if (pte_write(pteval)) {
			writable = true;
		} else {
			if (PageSwapCache(page) &&
			    !reuse_swap_page(page, NULL)) {
				unlock_page(page);
				result = SCAN_SWAP_CACHE_PAGE;
				goto out;
			}
			/*
			 * Page is not in the swap cache. It can be collapsed
			 * into a THP.
			 */
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
		inc_node_page_state(page,
				NR_ISOLATED_ANON + page_is_file_cache(page));
		VM_BUG_ON_PAGE(!PageLocked(page), page);
		VM_BUG_ON_PAGE(PageLRU(page), page);

		/* There should be enough young pte to collapse the page */
		if (pte_young(pteval) ||
		    page_is_young(page) || PageReferenced(page) ||
		    mmu_notifier_test_young(vma->vm_mm, address))
			referenced++;
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
	release_pte_pages(pte, _pte);
	trace_mm_collapse_huge_page_isolate(page, none_or_zero,
					    referenced, writable, result);
	return 0;
}

static void __collapse_huge_page_copy(pte_t *pte, struct page *page,
				      struct vm_area_struct *vma,
				      unsigned long address,
				      spinlock_t *ptl)
{
	pte_t *_pte;
	for (_pte = pte; _pte < pte + HPAGE_PMD_NR;
				_pte++, page++, address += PAGE_SIZE) {
		pte_t pteval = *_pte;
		struct page *src_page;

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
			VM_BUG_ON_PAGE(page_mapcount(src_page) != 1, src_page);
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
		if (node_distance(nid, i) > RECLAIM_DISTANCE)
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

static bool hugepage_vma_check(struct vm_area_struct *vma)
{
	if ((!(vma->vm_flags & VM_HUGEPAGE) && !khugepaged_always()) ||
	    (vma->vm_flags & VM_NOHUGEPAGE) ||
	    test_bit(MMF_DISABLE_THP, &vma->vm_mm->flags))
		return false;
	if (shmem_file(vma->vm_file)) {
		if (!IS_ENABLED(CONFIG_TRANSPARENT_HUGE_PAGECACHE))
			return false;
		return IS_ALIGNED((vma->vm_start >> PAGE_SHIFT) - vma->vm_pgoff,
				HPAGE_PMD_NR);
	}
	if (!vma->anon_vma || vma->vm_ops)
		return false;
	if (is_vma_temporary_stack(vma))
		return false;
	return !(vma->vm_flags & VM_NO_KHUGEPAGED);
}

/*
 * If mmap_sem temporarily dropped, revalidate vma
 * before taking mmap_sem.
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
	if (!hugepage_vma_check(vma))
		return SCAN_VMA_CHECK;
	return 0;
}

/*
 * Bring missing pages in from swap, to complete THP collapse.
 * Only done if khugepaged_scan_pmd believes it is worthwhile.
 *
 * Called and returns without pte mapped or spinlocks held,
 * but with mmap_sem held to protect against vma changes.
 */

static bool __collapse_huge_page_swapin(struct mm_struct *mm,
					struct vm_area_struct *vma,
					unsigned long address, pmd_t *pmd,
					int referenced)
{
	int swapped_in = 0, ret = 0;
	struct vm_fault vmf = {
		.vma = vma,
		.address = address,
		.flags = FAULT_FLAG_ALLOW_RETRY,
		.pmd = pmd,
		.pgoff = linear_page_index(vma, address),
	};

	/* we only decide to swapin, if there is enough young ptes */
	if (referenced < HPAGE_PMD_NR/2) {
		trace_mm_collapse_huge_page_swapin(mm, swapped_in, referenced, 0);
		return false;
	}
	vmf.pte = pte_offset_map(pmd, address);
	for (; vmf.address < address + HPAGE_PMD_NR*PAGE_SIZE;
			vmf.pte++, vmf.address += PAGE_SIZE) {
		vmf.orig_pte = *vmf.pte;
		if (!is_swap_pte(vmf.orig_pte))
			continue;
		swapped_in++;
		ret = do_swap_page(&vmf);

		/* do_swap_page returns VM_FAULT_RETRY with released mmap_sem */
		if (ret & VM_FAULT_RETRY) {
			down_read(&mm->mmap_sem);
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
	trace_mm_collapse_huge_page_swapin(mm, swapped_in, referenced, 1);
	return true;
}

static void collapse_huge_page(struct mm_struct *mm,
				   unsigned long address,
				   struct page **hpage,
				   int node, int referenced)
{
	pmd_t *pmd, _pmd;
	pte_t *pte;
	pgtable_t pgtable;
	struct page *new_page;
	spinlock_t *pmd_ptl, *pte_ptl;
	int isolated = 0, result = 0;
	struct mem_cgroup *memcg;
	struct vm_area_struct *vma;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */
	gfp_t gfp;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);

	/* Only allocate from the target node */
	gfp = alloc_hugepage_khugepaged_gfpmask() | __GFP_THISNODE;

	/*
	 * Before allocating the hugepage, release the mmap_sem read lock.
	 * The allocation can take potentially a long time if it involves
	 * sync compaction, and we do not need to hold the mmap_sem during
	 * that. We will recheck the vma after taking it again in write mode.
	 */
	up_read(&mm->mmap_sem);
	new_page = khugepaged_alloc_page(hpage, gfp, node);
	if (!new_page) {
		result = SCAN_ALLOC_HUGE_PAGE_FAIL;
		goto out_nolock;
	}

	/* Do not oom kill for khugepaged charges */
	if (unlikely(mem_cgroup_try_charge(new_page, mm, gfp | __GFP_NORETRY,
					   &memcg, true))) {
		result = SCAN_CGROUP_CHARGE_FAIL;
		goto out_nolock;
	}

	down_read(&mm->mmap_sem);
	result = hugepage_vma_revalidate(mm, address, &vma);
	if (result) {
		mem_cgroup_cancel_charge(new_page, memcg, true);
		up_read(&mm->mmap_sem);
		goto out_nolock;
	}

	pmd = mm_find_pmd(mm, address);
	if (!pmd) {
		result = SCAN_PMD_NULL;
		mem_cgroup_cancel_charge(new_page, memcg, true);
		up_read(&mm->mmap_sem);
		goto out_nolock;
	}

	/*
	 * __collapse_huge_page_swapin always returns with mmap_sem locked.
	 * If it fails, we release mmap_sem and jump out_nolock.
	 * Continuing to collapse causes inconsistency.
	 */
	if (!__collapse_huge_page_swapin(mm, vma, address, pmd, referenced)) {
		mem_cgroup_cancel_charge(new_page, memcg, true);
		up_read(&mm->mmap_sem);
		goto out_nolock;
	}

	up_read(&mm->mmap_sem);
	/*
	 * Prevent all access to pagetables with the exception of
	 * gup_fast later handled by the ptep_clear_flush and the VM
	 * handled by the anon_vma lock + PG_lock.
	 */
	down_write(&mm->mmap_sem);
	result = hugepage_vma_revalidate(mm, address, &vma);
	if (result)
		goto out;
	/* check if the pmd is still valid */
	if (mm_find_pmd(mm, address) != pmd)
		goto out;

	anon_vma_lock_write(vma->anon_vma);

	pte = pte_offset_map(pmd, address);
	pte_ptl = pte_lockptr(mm, pmd);

	mmun_start = address;
	mmun_end   = address + HPAGE_PMD_SIZE;
	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);
	pmd_ptl = pmd_lock(mm, pmd); /* probably unnecessary */
	/*
	 * After this gup_fast can't run anymore. This also removes
	 * any huge TLB entry from the CPU so we won't allow
	 * huge and small TLB entries for the same virtual address
	 * to avoid the risk of CPU bugs in that area.
	 */
	_pmd = pmdp_collapse_flush(vma, address, pmd);
	spin_unlock(pmd_ptl);
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);

	spin_lock(pte_ptl);
	isolated = __collapse_huge_page_isolate(vma, address, pte);
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

	__collapse_huge_page_copy(pte, new_page, vma, address, pte_ptl);
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
	mem_cgroup_commit_charge(new_page, memcg, false, true);
	lru_cache_add_active_or_unevictable(new_page, vma);
	pgtable_trans_huge_deposit(mm, pmd, pgtable);
	set_pmd_at(mm, address, pmd, _pmd);
	update_mmu_cache_pmd(vma, address, pmd);
	spin_unlock(pmd_ptl);

	*hpage = NULL;

	khugepaged_pages_collapsed++;
	result = SCAN_SUCCEED;
out_up_write:
	up_write(&mm->mmap_sem);
out_nolock:
	trace_mm_collapse_huge_page(mm, isolated, result);
	return;
out:
	mem_cgroup_cancel_charge(new_page, memcg, true);
	goto out_up_write;
}

static int khugepaged_scan_pmd(struct mm_struct *mm,
			       struct vm_area_struct *vma,
			       unsigned long address,
			       struct page **hpage)
{
	pmd_t *pmd;
	pte_t *pte, *_pte;
	int ret = 0, none_or_zero = 0, result = 0, referenced = 0;
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
		if (pte_write(pteval))
			writable = true;

		page = vm_normal_page(vma, _address, pteval);
		if (unlikely(!page)) {
			result = SCAN_PAGE_NULL;
			goto out_unmap;
		}

		/* TODO: teach khugepaged to collapse THP mapped with pte */
		if (PageCompound(page)) {
			result = SCAN_PAGE_COMPOUND;
			goto out_unmap;
		}

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
		 * cannot use mapcount: can't collapse if there's a gup pin.
		 * The page must only be referenced by the scanned process
		 * and page swap cache.
		 */
		if (page_count(page) != 1 + PageSwapCache(page)) {
			result = SCAN_PAGE_COUNT;
			goto out_unmap;
		}
		if (pte_young(pteval) ||
		    page_is_young(page) || PageReferenced(page) ||
		    mmu_notifier_test_young(vma->vm_mm, address))
			referenced++;
	}
	if (writable) {
		if (referenced) {
			result = SCAN_SUCCEED;
			ret = 1;
		} else {
			result = SCAN_LACK_REFERENCED_PAGE;
		}
	} else {
		result = SCAN_PAGE_RO;
	}
out_unmap:
	pte_unmap_unlock(pte, ptl);
	if (ret) {
		node = khugepaged_find_target_node();
		/* collapse_huge_page will return with the mmap_sem released */
		collapse_huge_page(mm, address, hpage, node, referenced);
	}
out:
	trace_mm_khugepaged_scan_pmd(mm, page, writable, referenced,
				     none_or_zero, result, unmapped);
	return ret;
}

static void collect_mm_slot(struct mm_slot *mm_slot)
{
	struct mm_struct *mm = mm_slot->mm;

	VM_BUG_ON(NR_CPUS != 1 && !spin_is_locked(&khugepaged_mm_lock));

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

#if defined(CONFIG_SHMEM) && defined(CONFIG_TRANSPARENT_HUGE_PAGECACHE)
static void retract_page_tables(struct address_space *mapping, pgoff_t pgoff)
{
	struct vm_area_struct *vma;
	unsigned long addr;
	pmd_t *pmd, _pmd;

	i_mmap_lock_write(mapping);
	vma_interval_tree_foreach(vma, &mapping->i_mmap, pgoff, pgoff) {
		/* probably overkill */
		if (vma->anon_vma)
			continue;
		addr = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
		if (addr & ~HPAGE_PMD_MASK)
			continue;
		if (vma->vm_end < addr + HPAGE_PMD_SIZE)
			continue;
		pmd = mm_find_pmd(vma->vm_mm, addr);
		if (!pmd)
			continue;
		/*
		 * We need exclusive mmap_sem to retract page table.
		 * If trylock fails we would end up with pte-mapped THP after
		 * re-fault. Not ideal, but it's more important to not disturb
		 * the system too much.
		 */
		if (down_write_trylock(&vma->vm_mm->mmap_sem)) {
			spinlock_t *ptl = pmd_lock(vma->vm_mm, pmd);
			/* assume page table is clear */
			_pmd = pmdp_collapse_flush(vma, addr, pmd);
			spin_unlock(ptl);
			up_write(&vma->vm_mm->mmap_sem);
			mm_dec_nr_ptes(vma->vm_mm);
			pte_free(vma->vm_mm, pmd_pgtable(_pmd));
		}
	}
	i_mmap_unlock_write(mapping);
}

/**
 * collapse_shmem - collapse small tmpfs/shmem pages into huge one.
 *
 * Basic scheme is simple, details are more complex:
 *  - allocate and freeze a new huge page;
 *  - scan over radix tree replacing old pages the new one
 *    + swap in pages if necessary;
 *    + fill in gaps;
 *    + keep old pages around in case if rollback is required;
 *  - if replacing succeed:
 *    + copy data over;
 *    + free old pages;
 *    + unfreeze huge page;
 *  - if replacing failed;
 *    + put all pages back and unfreeze them;
 *    + restore gaps in the radix-tree;
 *    + free huge page;
 */
static void collapse_shmem(struct mm_struct *mm,
		struct address_space *mapping, pgoff_t start,
		struct page **hpage, int node)
{
	gfp_t gfp;
	struct page *page, *new_page, *tmp;
	struct mem_cgroup *memcg;
	pgoff_t index, end = start + HPAGE_PMD_NR;
	LIST_HEAD(pagelist);
	struct radix_tree_iter iter;
	void **slot;
	int nr_none = 0, result = SCAN_SUCCEED;

	VM_BUG_ON(start & (HPAGE_PMD_NR - 1));

	/* Only allocate from the target node */
	gfp = alloc_hugepage_khugepaged_gfpmask() | __GFP_THISNODE;

	new_page = khugepaged_alloc_page(hpage, gfp, node);
	if (!new_page) {
		result = SCAN_ALLOC_HUGE_PAGE_FAIL;
		goto out;
	}

	/* Do not oom kill for khugepaged charges */
	if (unlikely(mem_cgroup_try_charge(new_page, mm, gfp | __GFP_NORETRY,
					   &memcg, true))) {
		result = SCAN_CGROUP_CHARGE_FAIL;
		goto out;
	}

	new_page->index = start;
	new_page->mapping = mapping;
	__SetPageSwapBacked(new_page);
	__SetPageLocked(new_page);
	BUG_ON(!page_ref_freeze(new_page, 1));


	/*
	 * At this point the new_page is 'frozen' (page_count() is zero), locked
	 * and not up-to-date. It's safe to insert it into radix tree, because
	 * nobody would be able to map it or use it in other way until we
	 * unfreeze it.
	 */

	index = start;
	spin_lock_irq(&mapping->tree_lock);
	radix_tree_for_each_slot(slot, &mapping->page_tree, &iter, start) {
		int n = min(iter.index, end) - index;

		/*
		 * Handle holes in the radix tree: charge it from shmem and
		 * insert relevant subpage of new_page into the radix-tree.
		 */
		if (n && !shmem_charge(mapping->host, n)) {
			result = SCAN_FAIL;
			break;
		}
		nr_none += n;
		for (; index < min(iter.index, end); index++) {
			radix_tree_insert(&mapping->page_tree, index,
					new_page + (index % HPAGE_PMD_NR));
		}

		/* We are done. */
		if (index >= end)
			break;

		page = radix_tree_deref_slot_protected(slot,
				&mapping->tree_lock);
		if (radix_tree_exceptional_entry(page) || !PageUptodate(page)) {
			spin_unlock_irq(&mapping->tree_lock);
			/* swap in or instantiate fallocated page */
			if (shmem_getpage(mapping->host, index, &page,
						SGP_NOHUGE)) {
				result = SCAN_FAIL;
				goto tree_unlocked;
			}
			spin_lock_irq(&mapping->tree_lock);
		} else if (trylock_page(page)) {
			get_page(page);
		} else {
			result = SCAN_PAGE_LOCK;
			break;
		}

		/*
		 * The page must be locked, so we can drop the tree_lock
		 * without racing with truncate.
		 */
		VM_BUG_ON_PAGE(!PageLocked(page), page);
		VM_BUG_ON_PAGE(!PageUptodate(page), page);
		VM_BUG_ON_PAGE(PageTransCompound(page), page);

		if (page_mapping(page) != mapping) {
			result = SCAN_TRUNCATED;
			goto out_unlock;
		}
		spin_unlock_irq(&mapping->tree_lock);

		if (isolate_lru_page(page)) {
			result = SCAN_DEL_PAGE_LRU;
			goto out_isolate_failed;
		}

		if (page_mapped(page))
			unmap_mapping_pages(mapping, index, 1, false);

		spin_lock_irq(&mapping->tree_lock);

		slot = radix_tree_lookup_slot(&mapping->page_tree, index);
		VM_BUG_ON_PAGE(page != radix_tree_deref_slot_protected(slot,
					&mapping->tree_lock), page);
		VM_BUG_ON_PAGE(page_mapped(page), page);

		/*
		 * The page is expected to have page_count() == 3:
		 *  - we hold a pin on it;
		 *  - one reference from radix tree;
		 *  - one from isolate_lru_page;
		 */
		if (!page_ref_freeze(page, 3)) {
			result = SCAN_PAGE_COUNT;
			goto out_lru;
		}

		/*
		 * Add the page to the list to be able to undo the collapse if
		 * something go wrong.
		 */
		list_add_tail(&page->lru, &pagelist);

		/* Finally, replace with the new page. */
		radix_tree_replace_slot(&mapping->page_tree, slot,
				new_page + (index % HPAGE_PMD_NR));

		slot = radix_tree_iter_resume(slot, &iter);
		index++;
		continue;
out_lru:
		spin_unlock_irq(&mapping->tree_lock);
		putback_lru_page(page);
out_isolate_failed:
		unlock_page(page);
		put_page(page);
		goto tree_unlocked;
out_unlock:
		unlock_page(page);
		put_page(page);
		break;
	}

	/*
	 * Handle hole in radix tree at the end of the range.
	 * This code only triggers if there's nothing in radix tree
	 * beyond 'end'.
	 */
	if (result == SCAN_SUCCEED && index < end) {
		int n = end - index;

		if (!shmem_charge(mapping->host, n)) {
			result = SCAN_FAIL;
			goto tree_locked;
		}

		for (; index < end; index++) {
			radix_tree_insert(&mapping->page_tree, index,
					new_page + (index % HPAGE_PMD_NR));
		}
		nr_none += n;
	}

tree_locked:
	spin_unlock_irq(&mapping->tree_lock);
tree_unlocked:

	if (result == SCAN_SUCCEED) {
		unsigned long flags;
		struct zone *zone = page_zone(new_page);

		/*
		 * Replacing old pages with new one has succeed, now we need to
		 * copy the content and free old pages.
		 */
		list_for_each_entry_safe(page, tmp, &pagelist, lru) {
			copy_highpage(new_page + (page->index % HPAGE_PMD_NR),
					page);
			list_del(&page->lru);
			unlock_page(page);
			page_ref_unfreeze(page, 1);
			page->mapping = NULL;
			ClearPageActive(page);
			ClearPageUnevictable(page);
			put_page(page);
		}

		local_irq_save(flags);
		__inc_node_page_state(new_page, NR_SHMEM_THPS);
		if (nr_none) {
			__mod_node_page_state(zone->zone_pgdat, NR_FILE_PAGES, nr_none);
			__mod_node_page_state(zone->zone_pgdat, NR_SHMEM, nr_none);
		}
		local_irq_restore(flags);

		/*
		 * Remove pte page tables, so we can re-faulti
		 * the page as huge.
		 */
		retract_page_tables(mapping, start);

		/* Everything is ready, let's unfreeze the new_page */
		set_page_dirty(new_page);
		SetPageUptodate(new_page);
		page_ref_unfreeze(new_page, HPAGE_PMD_NR);
		mem_cgroup_commit_charge(new_page, memcg, false, true);
		lru_cache_add_anon(new_page);
		unlock_page(new_page);

		*hpage = NULL;
	} else {
		/* Something went wrong: rollback changes to the radix-tree */
		shmem_uncharge(mapping->host, nr_none);
		spin_lock_irq(&mapping->tree_lock);
		radix_tree_for_each_slot(slot, &mapping->page_tree, &iter,
				start) {
			if (iter.index >= end)
				break;
			page = list_first_entry_or_null(&pagelist,
					struct page, lru);
			if (!page || iter.index < page->index) {
				if (!nr_none)
					break;
				nr_none--;
				/* Put holes back where they were */
				radix_tree_delete(&mapping->page_tree,
						  iter.index);
				continue;
			}

			VM_BUG_ON_PAGE(page->index != iter.index, page);

			/* Unfreeze the page. */
			list_del(&page->lru);
			page_ref_unfreeze(page, 2);
			radix_tree_replace_slot(&mapping->page_tree,
						slot, page);
			slot = radix_tree_iter_resume(slot, &iter);
			spin_unlock_irq(&mapping->tree_lock);
			putback_lru_page(page);
			unlock_page(page);
			spin_lock_irq(&mapping->tree_lock);
		}
		VM_BUG_ON(nr_none);
		spin_unlock_irq(&mapping->tree_lock);

		/* Unfreeze new_page, caller would take care about freeing it */
		page_ref_unfreeze(new_page, 1);
		mem_cgroup_cancel_charge(new_page, memcg, true);
		unlock_page(new_page);
		new_page->mapping = NULL;
	}
out:
	VM_BUG_ON(!list_empty(&pagelist));
	/* TODO: tracepoints */
}

static void khugepaged_scan_shmem(struct mm_struct *mm,
		struct address_space *mapping,
		pgoff_t start, struct page **hpage)
{
	struct page *page = NULL;
	struct radix_tree_iter iter;
	void **slot;
	int present, swap;
	int node = NUMA_NO_NODE;
	int result = SCAN_SUCCEED;

	present = 0;
	swap = 0;
	memset(khugepaged_node_load, 0, sizeof(khugepaged_node_load));
	rcu_read_lock();
	radix_tree_for_each_slot(slot, &mapping->page_tree, &iter, start) {
		if (iter.index >= start + HPAGE_PMD_NR)
			break;

		page = radix_tree_deref_slot(slot);
		if (radix_tree_deref_retry(page)) {
			slot = radix_tree_iter_retry(&iter);
			continue;
		}

		if (radix_tree_exception(page)) {
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

		if (page_count(page) != 1 + page_mapcount(page)) {
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
			slot = radix_tree_iter_resume(slot, &iter);
			cond_resched_rcu();
		}
	}
	rcu_read_unlock();

	if (result == SCAN_SUCCEED) {
		if (present < HPAGE_PMD_NR - khugepaged_max_ptes_none) {
			result = SCAN_EXCEED_NONE_PTE;
		} else {
			node = khugepaged_find_target_node();
			collapse_shmem(mm, mapping, start, hpage, node);
		}
	}

	/* TODO: tracepoints */
}
#else
static void khugepaged_scan_shmem(struct mm_struct *mm,
		struct address_space *mapping,
		pgoff_t start, struct page **hpage)
{
	BUILD_BUG();
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
	VM_BUG_ON(NR_CPUS != 1 && !spin_is_locked(&khugepaged_mm_lock));

	if (khugepaged_scan.mm_slot)
		mm_slot = khugepaged_scan.mm_slot;
	else {
		mm_slot = list_entry(khugepaged_scan.mm_head.next,
				     struct mm_slot, mm_node);
		khugepaged_scan.address = 0;
		khugepaged_scan.mm_slot = mm_slot;
	}
	spin_unlock(&khugepaged_mm_lock);

	mm = mm_slot->mm;
	/*
	 * Don't wait for semaphore (to avoid long wait times).  Just move to
	 * the next mm on the list.
	 */
	vma = NULL;
	if (unlikely(!down_read_trylock(&mm->mmap_sem)))
		goto breakouterloop_mmap_sem;
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
		if (!hugepage_vma_check(vma)) {
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

		while (khugepaged_scan.address < hend) {
			int ret;
			cond_resched();
			if (unlikely(khugepaged_test_exit(mm)))
				goto breakouterloop;

			VM_BUG_ON(khugepaged_scan.address < hstart ||
				  khugepaged_scan.address + HPAGE_PMD_SIZE >
				  hend);
			if (shmem_file(vma->vm_file)) {
				struct file *file;
				pgoff_t pgoff = linear_page_index(vma,
						khugepaged_scan.address);
				if (!shmem_huge_enabled(vma))
					goto skip;
				file = get_file(vma->vm_file);
				up_read(&mm->mmap_sem);
				ret = 1;
				khugepaged_scan_shmem(mm, file->f_mapping,
						pgoff, hpage);
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
				/* we released mmap_sem so break loop */
				goto breakouterloop_mmap_sem;
			if (progress >= pages)
				goto breakouterloop;
		}
	}
breakouterloop:
	up_read(&mm->mmap_sem); /* exit_mmap will destroy ptes after this */
breakouterloop_mmap_sem:

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

	for_each_populated_zone(zone)
		nr_zones++;

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
