/*
 *  Copyright (C) 2009  Red Hat, Inc.
 *
 *  This work is licensed under the terms of the GNU GPL, version 2. See
 *  the COPYING file in the top-level directory.
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/mmu_notifier.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/mm_inline.h>
#include <linux/kthread.h>
#include <linux/khugepaged.h>
#include <linux/freezer.h>
#include <linux/mman.h>
#include <asm/tlb.h>
#include <asm/pgalloc.h>
#include "internal.h"

/*
 * By default transparent hugepage support is enabled for all mappings
 * and khugepaged scans all mappings. Defrag is only invoked by
 * khugepaged hugepage allocations and by page faults inside
 * MADV_HUGEPAGE regions to avoid the risk of slowing down short lived
 * allocations.
 */
unsigned long transparent_hugepage_flags __read_mostly =
#ifdef CONFIG_TRANSPARENT_HUGEPAGE_ALWAYS
	(1<<TRANSPARENT_HUGEPAGE_FLAG)|
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE_MADVISE
	(1<<TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG)|
#endif
	(1<<TRANSPARENT_HUGEPAGE_DEFRAG_FLAG)|
	(1<<TRANSPARENT_HUGEPAGE_DEFRAG_KHUGEPAGED_FLAG);

/* default scan 8*512 pte (or vmas) every 30 second */
static unsigned int khugepaged_pages_to_scan __read_mostly = HPAGE_PMD_NR*8;
static unsigned int khugepaged_pages_collapsed;
static unsigned int khugepaged_full_scans;
static unsigned int khugepaged_scan_sleep_millisecs __read_mostly = 10000;
/* during fragmentation poll the hugepage allocator once every minute */
static unsigned int khugepaged_alloc_sleep_millisecs __read_mostly = 60000;
static struct task_struct *khugepaged_thread __read_mostly;
static DEFINE_MUTEX(khugepaged_mutex);
static DEFINE_SPINLOCK(khugepaged_mm_lock);
static DECLARE_WAIT_QUEUE_HEAD(khugepaged_wait);
/*
 * default collapse hugepages if there is at least one pte mapped like
 * it would have happened if the vma was large enough during page
 * fault.
 */
static unsigned int khugepaged_max_ptes_none __read_mostly = HPAGE_PMD_NR-1;

static int khugepaged(void *none);
static int mm_slots_hash_init(void);
static int khugepaged_slab_init(void);
static void khugepaged_slab_free(void);

#define MM_SLOTS_HASH_HEADS 1024
static struct hlist_head *mm_slots_hash __read_mostly;
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


static int set_recommended_min_free_kbytes(void)
{
	struct zone *zone;
	int nr_zones = 0;
	unsigned long recommended_min;
	extern int min_free_kbytes;

	if (!test_bit(TRANSPARENT_HUGEPAGE_FLAG,
		      &transparent_hugepage_flags) &&
	    !test_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
		      &transparent_hugepage_flags))
		return 0;

	for_each_populated_zone(zone)
		nr_zones++;

	/* Make sure at least 2 hugepages are free for MIGRATE_RESERVE */
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

	if (recommended_min > min_free_kbytes)
		min_free_kbytes = recommended_min;
	setup_per_zone_wmarks();
	return 0;
}
late_initcall(set_recommended_min_free_kbytes);

static int start_khugepaged(void)
{
	int err = 0;
	if (khugepaged_enabled()) {
		int wakeup;
		if (unlikely(!mm_slot_cache || !mm_slots_hash)) {
			err = -ENOMEM;
			goto out;
		}
		mutex_lock(&khugepaged_mutex);
		if (!khugepaged_thread)
			khugepaged_thread = kthread_run(khugepaged, NULL,
							"khugepaged");
		if (unlikely(IS_ERR(khugepaged_thread))) {
			printk(KERN_ERR
			       "khugepaged: kthread_run(khugepaged) failed\n");
			err = PTR_ERR(khugepaged_thread);
			khugepaged_thread = NULL;
		}
		wakeup = !list_empty(&khugepaged_scan.mm_head);
		mutex_unlock(&khugepaged_mutex);
		if (wakeup)
			wake_up_interruptible(&khugepaged_wait);

		set_recommended_min_free_kbytes();
	} else
		/* wakeup to exit */
		wake_up_interruptible(&khugepaged_wait);
out:
	return err;
}

#ifdef CONFIG_SYSFS

static ssize_t double_flag_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf,
				enum transparent_hugepage_flag enabled,
				enum transparent_hugepage_flag req_madv)
{
	if (test_bit(enabled, &transparent_hugepage_flags)) {
		VM_BUG_ON(test_bit(req_madv, &transparent_hugepage_flags));
		return sprintf(buf, "[always] madvise never\n");
	} else if (test_bit(req_madv, &transparent_hugepage_flags))
		return sprintf(buf, "always [madvise] never\n");
	else
		return sprintf(buf, "always madvise [never]\n");
}
static ssize_t double_flag_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count,
				 enum transparent_hugepage_flag enabled,
				 enum transparent_hugepage_flag req_madv)
{
	if (!memcmp("always", buf,
		    min(sizeof("always")-1, count))) {
		set_bit(enabled, &transparent_hugepage_flags);
		clear_bit(req_madv, &transparent_hugepage_flags);
	} else if (!memcmp("madvise", buf,
			   min(sizeof("madvise")-1, count))) {
		clear_bit(enabled, &transparent_hugepage_flags);
		set_bit(req_madv, &transparent_hugepage_flags);
	} else if (!memcmp("never", buf,
			   min(sizeof("never")-1, count))) {
		clear_bit(enabled, &transparent_hugepage_flags);
		clear_bit(req_madv, &transparent_hugepage_flags);
	} else
		return -EINVAL;

	return count;
}

static ssize_t enabled_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buf)
{
	return double_flag_show(kobj, attr, buf,
				TRANSPARENT_HUGEPAGE_FLAG,
				TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG);
}
static ssize_t enabled_store(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	ssize_t ret;

	ret = double_flag_store(kobj, attr, buf, count,
				TRANSPARENT_HUGEPAGE_FLAG,
				TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG);

	if (ret > 0) {
		int err = start_khugepaged();
		if (err)
			ret = err;
	}

	if (ret > 0 &&
	    (test_bit(TRANSPARENT_HUGEPAGE_FLAG,
		      &transparent_hugepage_flags) ||
	     test_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
		      &transparent_hugepage_flags)))
		set_recommended_min_free_kbytes();

	return ret;
}
static struct kobj_attribute enabled_attr =
	__ATTR(enabled, 0644, enabled_show, enabled_store);

static ssize_t single_flag_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf,
				enum transparent_hugepage_flag flag)
{
	return sprintf(buf, "%d\n",
		       !!test_bit(flag, &transparent_hugepage_flags));
}

static ssize_t single_flag_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count,
				 enum transparent_hugepage_flag flag)
{
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 10, &value);
	if (ret < 0)
		return ret;
	if (value > 1)
		return -EINVAL;

	if (value)
		set_bit(flag, &transparent_hugepage_flags);
	else
		clear_bit(flag, &transparent_hugepage_flags);

	return count;
}

/*
 * Currently defrag only disables __GFP_NOWAIT for allocation. A blind
 * __GFP_REPEAT is too aggressive, it's never worth swapping tons of
 * memory just to allocate one more hugepage.
 */
static ssize_t defrag_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	return double_flag_show(kobj, attr, buf,
				TRANSPARENT_HUGEPAGE_DEFRAG_FLAG,
				TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG);
}
static ssize_t defrag_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	return double_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_DEFRAG_FLAG,
				 TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG);
}
static struct kobj_attribute defrag_attr =
	__ATTR(defrag, 0644, defrag_show, defrag_store);

#ifdef CONFIG_DEBUG_VM
static ssize_t debug_cow_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return single_flag_show(kobj, attr, buf,
				TRANSPARENT_HUGEPAGE_DEBUG_COW_FLAG);
}
static ssize_t debug_cow_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	return single_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_DEBUG_COW_FLAG);
}
static struct kobj_attribute debug_cow_attr =
	__ATTR(debug_cow, 0644, debug_cow_show, debug_cow_store);
#endif /* CONFIG_DEBUG_VM */

static struct attribute *hugepage_attr[] = {
	&enabled_attr.attr,
	&defrag_attr.attr,
#ifdef CONFIG_DEBUG_VM
	&debug_cow_attr.attr,
#endif
	NULL,
};

static struct attribute_group hugepage_attr_group = {
	.attrs = hugepage_attr,
};

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

	err = strict_strtoul(buf, 10, &msecs);
	if (err || msecs > UINT_MAX)
		return -EINVAL;

	khugepaged_scan_sleep_millisecs = msecs;
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

	err = strict_strtoul(buf, 10, &msecs);
	if (err || msecs > UINT_MAX)
		return -EINVAL;

	khugepaged_alloc_sleep_millisecs = msecs;
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

	err = strict_strtoul(buf, 10, &pages);
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
	return single_flag_show(kobj, attr, buf,
				TRANSPARENT_HUGEPAGE_DEFRAG_KHUGEPAGED_FLAG);
}
static ssize_t khugepaged_defrag_store(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       const char *buf, size_t count)
{
	return single_flag_store(kobj, attr, buf, count,
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

	err = strict_strtoul(buf, 10, &max_ptes_none);
	if (err || max_ptes_none > HPAGE_PMD_NR-1)
		return -EINVAL;

	khugepaged_max_ptes_none = max_ptes_none;

	return count;
}
static struct kobj_attribute khugepaged_max_ptes_none_attr =
	__ATTR(max_ptes_none, 0644, khugepaged_max_ptes_none_show,
	       khugepaged_max_ptes_none_store);

static struct attribute *khugepaged_attr[] = {
	&khugepaged_defrag_attr.attr,
	&khugepaged_max_ptes_none_attr.attr,
	&pages_to_scan_attr.attr,
	&pages_collapsed_attr.attr,
	&full_scans_attr.attr,
	&scan_sleep_millisecs_attr.attr,
	&alloc_sleep_millisecs_attr.attr,
	NULL,
};

static struct attribute_group khugepaged_attr_group = {
	.attrs = khugepaged_attr,
	.name = "khugepaged",
};

static int __init hugepage_init_sysfs(struct kobject **hugepage_kobj)
{
	int err;

	*hugepage_kobj = kobject_create_and_add("transparent_hugepage", mm_kobj);
	if (unlikely(!*hugepage_kobj)) {
		printk(KERN_ERR "hugepage: failed kobject create\n");
		return -ENOMEM;
	}

	err = sysfs_create_group(*hugepage_kobj, &hugepage_attr_group);
	if (err) {
		printk(KERN_ERR "hugepage: failed register hugeage group\n");
		goto delete_obj;
	}

	err = sysfs_create_group(*hugepage_kobj, &khugepaged_attr_group);
	if (err) {
		printk(KERN_ERR "hugepage: failed register hugeage group\n");
		goto remove_hp_group;
	}

	return 0;

remove_hp_group:
	sysfs_remove_group(*hugepage_kobj, &hugepage_attr_group);
delete_obj:
	kobject_put(*hugepage_kobj);
	return err;
}

static void __init hugepage_exit_sysfs(struct kobject *hugepage_kobj)
{
	sysfs_remove_group(hugepage_kobj, &khugepaged_attr_group);
	sysfs_remove_group(hugepage_kobj, &hugepage_attr_group);
	kobject_put(hugepage_kobj);
}
#else
static inline int hugepage_init_sysfs(struct kobject **hugepage_kobj)
{
	return 0;
}

static inline void hugepage_exit_sysfs(struct kobject *hugepage_kobj)
{
}
#endif /* CONFIG_SYSFS */

static int __init hugepage_init(void)
{
	int err;
	struct kobject *hugepage_kobj;

	if (!has_transparent_hugepage()) {
		transparent_hugepage_flags = 0;
		return -EINVAL;
	}

	err = hugepage_init_sysfs(&hugepage_kobj);
	if (err)
		return err;

	err = khugepaged_slab_init();
	if (err)
		goto out;

	err = mm_slots_hash_init();
	if (err) {
		khugepaged_slab_free();
		goto out;
	}

	/*
	 * By default disable transparent hugepages on smaller systems,
	 * where the extra memory used could hurt more than TLB overhead
	 * is likely to save.  The admin can still enable it through /sys.
	 */
	if (totalram_pages < (512 << (20 - PAGE_SHIFT)))
		transparent_hugepage_flags = 0;

	start_khugepaged();

	set_recommended_min_free_kbytes();

	return 0;
out:
	hugepage_exit_sysfs(hugepage_kobj);
	return err;
}
module_init(hugepage_init)

static int __init setup_transparent_hugepage(char *str)
{
	int ret = 0;
	if (!str)
		goto out;
	if (!strcmp(str, "always")) {
		set_bit(TRANSPARENT_HUGEPAGE_FLAG,
			&transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
			  &transparent_hugepage_flags);
		ret = 1;
	} else if (!strcmp(str, "madvise")) {
		clear_bit(TRANSPARENT_HUGEPAGE_FLAG,
			  &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
			&transparent_hugepage_flags);
		ret = 1;
	} else if (!strcmp(str, "never")) {
		clear_bit(TRANSPARENT_HUGEPAGE_FLAG,
			  &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
			  &transparent_hugepage_flags);
		ret = 1;
	}
out:
	if (!ret)
		printk(KERN_WARNING
		       "transparent_hugepage= cannot parse, ignored\n");
	return ret;
}
__setup("transparent_hugepage=", setup_transparent_hugepage);

static void prepare_pmd_huge_pte(pgtable_t pgtable,
				 struct mm_struct *mm)
{
	assert_spin_locked(&mm->page_table_lock);

	/* FIFO */
	if (!mm->pmd_huge_pte)
		INIT_LIST_HEAD(&pgtable->lru);
	else
		list_add(&pgtable->lru, &mm->pmd_huge_pte->lru);
	mm->pmd_huge_pte = pgtable;
}

static inline pmd_t maybe_pmd_mkwrite(pmd_t pmd, struct vm_area_struct *vma)
{
	if (likely(vma->vm_flags & VM_WRITE))
		pmd = pmd_mkwrite(pmd);
	return pmd;
}

static int __do_huge_pmd_anonymous_page(struct mm_struct *mm,
					struct vm_area_struct *vma,
					unsigned long haddr, pmd_t *pmd,
					struct page *page)
{
	pgtable_t pgtable;

	VM_BUG_ON(!PageCompound(page));
	pgtable = pte_alloc_one(mm, haddr);
	if (unlikely(!pgtable))
		return VM_FAULT_OOM;

	clear_huge_page(page, haddr, HPAGE_PMD_NR);
	__SetPageUptodate(page);

	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_none(*pmd))) {
		spin_unlock(&mm->page_table_lock);
		mem_cgroup_uncharge_page(page);
		put_page(page);
		pte_free(mm, pgtable);
	} else {
		pmd_t entry;
		entry = mk_pmd(page, vma->vm_page_prot);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		entry = pmd_mkhuge(entry);
		/*
		 * The spinlocking to take the lru_lock inside
		 * page_add_new_anon_rmap() acts as a full memory
		 * barrier to be sure clear_huge_page writes become
		 * visible after the set_pmd_at() write.
		 */
		page_add_new_anon_rmap(page, vma, haddr);
		set_pmd_at(mm, haddr, pmd, entry);
		prepare_pmd_huge_pte(pgtable, mm);
		add_mm_counter(mm, MM_ANONPAGES, HPAGE_PMD_NR);
		mm->nr_ptes++;
		spin_unlock(&mm->page_table_lock);
	}

	return 0;
}

static inline gfp_t alloc_hugepage_gfpmask(int defrag, gfp_t extra_gfp)
{
	return (GFP_TRANSHUGE & ~(defrag ? 0 : __GFP_WAIT)) | extra_gfp;
}

static inline struct page *alloc_hugepage_vma(int defrag,
					      struct vm_area_struct *vma,
					      unsigned long haddr, int nd,
					      gfp_t extra_gfp)
{
	return alloc_pages_vma(alloc_hugepage_gfpmask(defrag, extra_gfp),
			       HPAGE_PMD_ORDER, vma, haddr, nd);
}

#ifndef CONFIG_NUMA
static inline struct page *alloc_hugepage(int defrag)
{
	return alloc_pages(alloc_hugepage_gfpmask(defrag, 0),
			   HPAGE_PMD_ORDER);
}
#endif

int do_huge_pmd_anonymous_page(struct mm_struct *mm, struct vm_area_struct *vma,
			       unsigned long address, pmd_t *pmd,
			       unsigned int flags)
{
	struct page *page;
	unsigned long haddr = address & HPAGE_PMD_MASK;
	pte_t *pte;

	if (haddr >= vma->vm_start && haddr + HPAGE_PMD_SIZE <= vma->vm_end) {
		if (unlikely(anon_vma_prepare(vma)))
			return VM_FAULT_OOM;
		if (unlikely(khugepaged_enter(vma)))
			return VM_FAULT_OOM;
		page = alloc_hugepage_vma(transparent_hugepage_defrag(vma),
					  vma, haddr, numa_node_id(), 0);
		if (unlikely(!page)) {
			count_vm_event(THP_FAULT_FALLBACK);
			goto out;
		}
		count_vm_event(THP_FAULT_ALLOC);
		if (unlikely(mem_cgroup_newpage_charge(page, mm, GFP_KERNEL))) {
			put_page(page);
			goto out;
		}
		if (unlikely(__do_huge_pmd_anonymous_page(mm, vma, haddr, pmd,
							  page))) {
			mem_cgroup_uncharge_page(page);
			put_page(page);
			goto out;
		}

		return 0;
	}
out:
	/*
	 * Use __pte_alloc instead of pte_alloc_map, because we can't
	 * run pte_offset_map on the pmd, if an huge pmd could
	 * materialize from under us from a different thread.
	 */
	if (unlikely(__pte_alloc(mm, vma, pmd, address)))
		return VM_FAULT_OOM;
	/* if an huge pmd materialized from under us just retry later */
	if (unlikely(pmd_trans_huge(*pmd)))
		return 0;
	/*
	 * A regular pmd is established and it can't morph into a huge pmd
	 * from under us anymore at this point because we hold the mmap_sem
	 * read mode and khugepaged takes it in write mode. So now it's
	 * safe to run pte_offset_map().
	 */
	pte = pte_offset_map(pmd, address);
	return handle_pte_fault(mm, vma, address, pte, pmd, flags);
}

int copy_huge_pmd(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		  pmd_t *dst_pmd, pmd_t *src_pmd, unsigned long addr,
		  struct vm_area_struct *vma)
{
	struct page *src_page;
	pmd_t pmd;
	pgtable_t pgtable;
	int ret;

	ret = -ENOMEM;
	pgtable = pte_alloc_one(dst_mm, addr);
	if (unlikely(!pgtable))
		goto out;

	spin_lock(&dst_mm->page_table_lock);
	spin_lock_nested(&src_mm->page_table_lock, SINGLE_DEPTH_NESTING);

	ret = -EAGAIN;
	pmd = *src_pmd;
	if (unlikely(!pmd_trans_huge(pmd))) {
		pte_free(dst_mm, pgtable);
		goto out_unlock;
	}
	if (unlikely(pmd_trans_splitting(pmd))) {
		/* split huge page running from under us */
		spin_unlock(&src_mm->page_table_lock);
		spin_unlock(&dst_mm->page_table_lock);
		pte_free(dst_mm, pgtable);

		wait_split_huge_page(vma->anon_vma, src_pmd); /* src_vma */
		goto out;
	}
	src_page = pmd_page(pmd);
	VM_BUG_ON(!PageHead(src_page));
	get_page(src_page);
	page_dup_rmap(src_page);
	add_mm_counter(dst_mm, MM_ANONPAGES, HPAGE_PMD_NR);

	pmdp_set_wrprotect(src_mm, addr, src_pmd);
	pmd = pmd_mkold(pmd_wrprotect(pmd));
	set_pmd_at(dst_mm, addr, dst_pmd, pmd);
	prepare_pmd_huge_pte(pgtable, dst_mm);
	dst_mm->nr_ptes++;

	ret = 0;
out_unlock:
	spin_unlock(&src_mm->page_table_lock);
	spin_unlock(&dst_mm->page_table_lock);
out:
	return ret;
}

/* no "address" argument so destroys page coloring of some arch */
pgtable_t get_pmd_huge_pte(struct mm_struct *mm)
{
	pgtable_t pgtable;

	assert_spin_locked(&mm->page_table_lock);

	/* FIFO */
	pgtable = mm->pmd_huge_pte;
	if (list_empty(&pgtable->lru))
		mm->pmd_huge_pte = NULL;
	else {
		mm->pmd_huge_pte = list_entry(pgtable->lru.next,
					      struct page, lru);
		list_del(&pgtable->lru);
	}
	return pgtable;
}

static int do_huge_pmd_wp_page_fallback(struct mm_struct *mm,
					struct vm_area_struct *vma,
					unsigned long address,
					pmd_t *pmd, pmd_t orig_pmd,
					struct page *page,
					unsigned long haddr)
{
	pgtable_t pgtable;
	pmd_t _pmd;
	int ret = 0, i;
	struct page **pages;

	pages = kmalloc(sizeof(struct page *) * HPAGE_PMD_NR,
			GFP_KERNEL);
	if (unlikely(!pages)) {
		ret |= VM_FAULT_OOM;
		goto out;
	}

	for (i = 0; i < HPAGE_PMD_NR; i++) {
		pages[i] = alloc_page_vma_node(GFP_HIGHUSER_MOVABLE |
					       __GFP_OTHER_NODE,
					       vma, address, page_to_nid(page));
		if (unlikely(!pages[i] ||
			     mem_cgroup_newpage_charge(pages[i], mm,
						       GFP_KERNEL))) {
			if (pages[i])
				put_page(pages[i]);
			mem_cgroup_uncharge_start();
			while (--i >= 0) {
				mem_cgroup_uncharge_page(pages[i]);
				put_page(pages[i]);
			}
			mem_cgroup_uncharge_end();
			kfree(pages);
			ret |= VM_FAULT_OOM;
			goto out;
		}
	}

	for (i = 0; i < HPAGE_PMD_NR; i++) {
		copy_user_highpage(pages[i], page + i,
				   haddr + PAGE_SIZE * i, vma);
		__SetPageUptodate(pages[i]);
		cond_resched();
	}

	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_same(*pmd, orig_pmd)))
		goto out_free_pages;
	VM_BUG_ON(!PageHead(page));

	pmdp_clear_flush_notify(vma, haddr, pmd);
	/* leave pmd empty until pte is filled */

	pgtable = get_pmd_huge_pte(mm);
	pmd_populate(mm, &_pmd, pgtable);

	for (i = 0; i < HPAGE_PMD_NR; i++, haddr += PAGE_SIZE) {
		pte_t *pte, entry;
		entry = mk_pte(pages[i], vma->vm_page_prot);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		page_add_new_anon_rmap(pages[i], vma, haddr);
		pte = pte_offset_map(&_pmd, haddr);
		VM_BUG_ON(!pte_none(*pte));
		set_pte_at(mm, haddr, pte, entry);
		pte_unmap(pte);
	}
	kfree(pages);

	smp_wmb(); /* make pte visible before pmd */
	pmd_populate(mm, pmd, pgtable);
	page_remove_rmap(page);
	spin_unlock(&mm->page_table_lock);

	ret |= VM_FAULT_WRITE;
	put_page(page);

out:
	return ret;

out_free_pages:
	spin_unlock(&mm->page_table_lock);
	mem_cgroup_uncharge_start();
	for (i = 0; i < HPAGE_PMD_NR; i++) {
		mem_cgroup_uncharge_page(pages[i]);
		put_page(pages[i]);
	}
	mem_cgroup_uncharge_end();
	kfree(pages);
	goto out;
}

int do_huge_pmd_wp_page(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, pmd_t *pmd, pmd_t orig_pmd)
{
	int ret = 0;
	struct page *page, *new_page;
	unsigned long haddr;

	VM_BUG_ON(!vma->anon_vma);
	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_same(*pmd, orig_pmd)))
		goto out_unlock;

	page = pmd_page(orig_pmd);
	VM_BUG_ON(!PageCompound(page) || !PageHead(page));
	haddr = address & HPAGE_PMD_MASK;
	if (page_mapcount(page) == 1) {
		pmd_t entry;
		entry = pmd_mkyoung(orig_pmd);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		if (pmdp_set_access_flags(vma, haddr, pmd, entry,  1))
			update_mmu_cache(vma, address, entry);
		ret |= VM_FAULT_WRITE;
		goto out_unlock;
	}
	get_page(page);
	spin_unlock(&mm->page_table_lock);

	if (transparent_hugepage_enabled(vma) &&
	    !transparent_hugepage_debug_cow())
		new_page = alloc_hugepage_vma(transparent_hugepage_defrag(vma),
					      vma, haddr, numa_node_id(), 0);
	else
		new_page = NULL;

	if (unlikely(!new_page)) {
		count_vm_event(THP_FAULT_FALLBACK);
		ret = do_huge_pmd_wp_page_fallback(mm, vma, address,
						   pmd, orig_pmd, page, haddr);
		if (ret & VM_FAULT_OOM)
			split_huge_page(page);
		put_page(page);
		goto out;
	}
	count_vm_event(THP_FAULT_ALLOC);

	if (unlikely(mem_cgroup_newpage_charge(new_page, mm, GFP_KERNEL))) {
		put_page(new_page);
		split_huge_page(page);
		put_page(page);
		ret |= VM_FAULT_OOM;
		goto out;
	}

	copy_user_huge_page(new_page, page, haddr, vma, HPAGE_PMD_NR);
	__SetPageUptodate(new_page);

	spin_lock(&mm->page_table_lock);
	put_page(page);
	if (unlikely(!pmd_same(*pmd, orig_pmd))) {
		spin_unlock(&mm->page_table_lock);
		mem_cgroup_uncharge_page(new_page);
		put_page(new_page);
		goto out;
	} else {
		pmd_t entry;
		VM_BUG_ON(!PageHead(page));
		entry = mk_pmd(new_page, vma->vm_page_prot);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		entry = pmd_mkhuge(entry);
		pmdp_clear_flush_notify(vma, haddr, pmd);
		page_add_new_anon_rmap(new_page, vma, haddr);
		set_pmd_at(mm, haddr, pmd, entry);
		update_mmu_cache(vma, address, entry);
		page_remove_rmap(page);
		put_page(page);
		ret |= VM_FAULT_WRITE;
	}
out_unlock:
	spin_unlock(&mm->page_table_lock);
out:
	return ret;
}

struct page *follow_trans_huge_pmd(struct mm_struct *mm,
				   unsigned long addr,
				   pmd_t *pmd,
				   unsigned int flags)
{
	struct page *page = NULL;

	assert_spin_locked(&mm->page_table_lock);

	if (flags & FOLL_WRITE && !pmd_write(*pmd))
		goto out;

	page = pmd_page(*pmd);
	VM_BUG_ON(!PageHead(page));
	if (flags & FOLL_TOUCH) {
		pmd_t _pmd;
		/*
		 * We should set the dirty bit only for FOLL_WRITE but
		 * for now the dirty bit in the pmd is meaningless.
		 * And if the dirty bit will become meaningful and
		 * we'll only set it with FOLL_WRITE, an atomic
		 * set_bit will be required on the pmd to set the
		 * young bit, instead of the current set_pmd_at.
		 */
		_pmd = pmd_mkyoung(pmd_mkdirty(*pmd));
		set_pmd_at(mm, addr & HPAGE_PMD_MASK, pmd, _pmd);
	}
	page += (addr & ~HPAGE_PMD_MASK) >> PAGE_SHIFT;
	VM_BUG_ON(!PageCompound(page));
	if (flags & FOLL_GET)
		get_page_foll(page);

out:
	return page;
}

int zap_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		 pmd_t *pmd, unsigned long addr)
{
	int ret = 0;

	if (__pmd_trans_huge_lock(pmd, vma) == 1) {
		struct page *page;
		pgtable_t pgtable;
		pgtable = get_pmd_huge_pte(tlb->mm);
		page = pmd_page(*pmd);
		pmd_clear(pmd);
		tlb_remove_pmd_tlb_entry(tlb, pmd, addr);
		page_remove_rmap(page);
		VM_BUG_ON(page_mapcount(page) < 0);
		add_mm_counter(tlb->mm, MM_ANONPAGES, -HPAGE_PMD_NR);
		VM_BUG_ON(!PageHead(page));
		tlb->mm->nr_ptes--;
		spin_unlock(&tlb->mm->page_table_lock);
		tlb_remove_page(tlb, page);
		pte_free(tlb->mm, pgtable);
		ret = 1;
	}
	return ret;
}

int mincore_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long addr, unsigned long end,
		unsigned char *vec)
{
	int ret = 0;

	if (__pmd_trans_huge_lock(pmd, vma) == 1) {
		/*
		 * All logical pages in the range are present
		 * if backed by a huge page.
		 */
		spin_unlock(&vma->vm_mm->page_table_lock);
		memset(vec, 1, (end - addr) >> PAGE_SHIFT);
		ret = 1;
	}

	return ret;
}

int move_huge_pmd(struct vm_area_struct *vma, struct vm_area_struct *new_vma,
		  unsigned long old_addr,
		  unsigned long new_addr, unsigned long old_end,
		  pmd_t *old_pmd, pmd_t *new_pmd)
{
	int ret = 0;
	pmd_t pmd;

	struct mm_struct *mm = vma->vm_mm;

	if ((old_addr & ~HPAGE_PMD_MASK) ||
	    (new_addr & ~HPAGE_PMD_MASK) ||
	    old_end - old_addr < HPAGE_PMD_SIZE ||
	    (new_vma->vm_flags & VM_NOHUGEPAGE))
		goto out;

	/*
	 * The destination pmd shouldn't be established, free_pgtables()
	 * should have release it.
	 */
	if (WARN_ON(!pmd_none(*new_pmd))) {
		VM_BUG_ON(pmd_trans_huge(*new_pmd));
		goto out;
	}

	ret = __pmd_trans_huge_lock(old_pmd, vma);
	if (ret == 1) {
		pmd = pmdp_get_and_clear(mm, old_addr, old_pmd);
		VM_BUG_ON(!pmd_none(*new_pmd));
		set_pmd_at(mm, new_addr, new_pmd, pmd);
		spin_unlock(&mm->page_table_lock);
	}
out:
	return ret;
}

int change_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long addr, pgprot_t newprot)
{
	struct mm_struct *mm = vma->vm_mm;
	int ret = 0;

	if (__pmd_trans_huge_lock(pmd, vma) == 1) {
		pmd_t entry;
		entry = pmdp_get_and_clear(mm, addr, pmd);
		entry = pmd_modify(entry, newprot);
		set_pmd_at(mm, addr, pmd, entry);
		spin_unlock(&vma->vm_mm->page_table_lock);
		ret = 1;
	}

	return ret;
}

/*
 * Returns 1 if a given pmd maps a stable (not under splitting) thp.
 * Returns -1 if it maps a thp under splitting. Returns 0 otherwise.
 *
 * Note that if it returns 1, this routine returns without unlocking page
 * table locks. So callers must unlock them.
 */
int __pmd_trans_huge_lock(pmd_t *pmd, struct vm_area_struct *vma)
{
	spin_lock(&vma->vm_mm->page_table_lock);
	if (likely(pmd_trans_huge(*pmd))) {
		if (unlikely(pmd_trans_splitting(*pmd))) {
			spin_unlock(&vma->vm_mm->page_table_lock);
			wait_split_huge_page(vma->anon_vma, pmd);
			return -1;
		} else {
			/* Thp mapped by 'pmd' is stable, so we can
			 * handle it as it is. */
			return 1;
		}
	}
	spin_unlock(&vma->vm_mm->page_table_lock);
	return 0;
}

pmd_t *page_check_address_pmd(struct page *page,
			      struct mm_struct *mm,
			      unsigned long address,
			      enum page_check_address_pmd_flag flag)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd, *ret = NULL;

	if (address & ~HPAGE_PMD_MASK)
		goto out;

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto out;

	pud = pud_offset(pgd, address);
	if (!pud_present(*pud))
		goto out;

	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd))
		goto out;
	if (pmd_page(*pmd) != page)
		goto out;
	/*
	 * split_vma() may create temporary aliased mappings. There is
	 * no risk as long as all huge pmd are found and have their
	 * splitting bit set before __split_huge_page_refcount
	 * runs. Finding the same huge pmd more than once during the
	 * same rmap walk is not a problem.
	 */
	if (flag == PAGE_CHECK_ADDRESS_PMD_NOTSPLITTING_FLAG &&
	    pmd_trans_splitting(*pmd))
		goto out;
	if (pmd_trans_huge(*pmd)) {
		VM_BUG_ON(flag == PAGE_CHECK_ADDRESS_PMD_SPLITTING_FLAG &&
			  !pmd_trans_splitting(*pmd));
		ret = pmd;
	}
out:
	return ret;
}

static int __split_huge_page_splitting(struct page *page,
				       struct vm_area_struct *vma,
				       unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;
	pmd_t *pmd;
	int ret = 0;

	spin_lock(&mm->page_table_lock);
	pmd = page_check_address_pmd(page, mm, address,
				     PAGE_CHECK_ADDRESS_PMD_NOTSPLITTING_FLAG);
	if (pmd) {
		/*
		 * We can't temporarily set the pmd to null in order
		 * to split it, the pmd must remain marked huge at all
		 * times or the VM won't take the pmd_trans_huge paths
		 * and it won't wait on the anon_vma->root->mutex to
		 * serialize against split_huge_page*.
		 */
		pmdp_splitting_flush_notify(vma, address, pmd);
		ret = 1;
	}
	spin_unlock(&mm->page_table_lock);

	return ret;
}

static void __split_huge_page_refcount(struct page *page)
{
	int i;
	struct zone *zone = page_zone(page);
	struct lruvec *lruvec;
	int tail_count = 0;

	/* prevent PageLRU to go away from under us, and freeze lru stats */
	spin_lock_irq(&zone->lru_lock);
	lruvec = mem_cgroup_page_lruvec(page, zone);

	compound_lock(page);
	/* complete memcg works before add pages to LRU */
	mem_cgroup_split_huge_fixup(page);

	for (i = HPAGE_PMD_NR - 1; i >= 1; i--) {
		struct page *page_tail = page + i;

		/* tail_page->_mapcount cannot change */
		BUG_ON(page_mapcount(page_tail) < 0);
		tail_count += page_mapcount(page_tail);
		/* check for overflow */
		BUG_ON(tail_count < 0);
		BUG_ON(atomic_read(&page_tail->_count) != 0);
		/*
		 * tail_page->_count is zero and not changing from
		 * under us. But get_page_unless_zero() may be running
		 * from under us on the tail_page. If we used
		 * atomic_set() below instead of atomic_add(), we
		 * would then run atomic_set() concurrently with
		 * get_page_unless_zero(), and atomic_set() is
		 * implemented in C not using locked ops. spin_unlock
		 * on x86 sometime uses locked ops because of PPro
		 * errata 66, 92, so unless somebody can guarantee
		 * atomic_set() here would be safe on all archs (and
		 * not only on x86), it's safer to use atomic_add().
		 */
		atomic_add(page_mapcount(page) + page_mapcount(page_tail) + 1,
			   &page_tail->_count);

		/* after clearing PageTail the gup refcount can be released */
		smp_mb();

		/*
		 * retain hwpoison flag of the poisoned tail page:
		 *   fix for the unsuitable process killed on Guest Machine(KVM)
		 *   by the memory-failure.
		 */
		page_tail->flags &= ~PAGE_FLAGS_CHECK_AT_PREP | __PG_HWPOISON;
		page_tail->flags |= (page->flags &
				     ((1L << PG_referenced) |
				      (1L << PG_swapbacked) |
				      (1L << PG_mlocked) |
				      (1L << PG_uptodate)));
		page_tail->flags |= (1L << PG_dirty);

		/* clear PageTail before overwriting first_page */
		smp_wmb();

		/*
		 * __split_huge_page_splitting() already set the
		 * splitting bit in all pmd that could map this
		 * hugepage, that will ensure no CPU can alter the
		 * mapcount on the head page. The mapcount is only
		 * accounted in the head page and it has to be
		 * transferred to all tail pages in the below code. So
		 * for this code to be safe, the split the mapcount
		 * can't change. But that doesn't mean userland can't
		 * keep changing and reading the page contents while
		 * we transfer the mapcount, so the pmd splitting
		 * status is achieved setting a reserved bit in the
		 * pmd, not by clearing the present bit.
		*/
		page_tail->_mapcount = page->_mapcount;

		BUG_ON(page_tail->mapping);
		page_tail->mapping = page->mapping;

		page_tail->index = page->index + i;

		BUG_ON(!PageAnon(page_tail));
		BUG_ON(!PageUptodate(page_tail));
		BUG_ON(!PageDirty(page_tail));
		BUG_ON(!PageSwapBacked(page_tail));

		lru_add_page_tail(page, page_tail, lruvec);
	}
	atomic_sub(tail_count, &page->_count);
	BUG_ON(atomic_read(&page->_count) <= 0);

	__mod_zone_page_state(zone, NR_ANON_TRANSPARENT_HUGEPAGES, -1);
	__mod_zone_page_state(zone, NR_ANON_PAGES, HPAGE_PMD_NR);

	ClearPageCompound(page);
	compound_unlock(page);
	spin_unlock_irq(&zone->lru_lock);

	for (i = 1; i < HPAGE_PMD_NR; i++) {
		struct page *page_tail = page + i;
		BUG_ON(page_count(page_tail) <= 0);
		/*
		 * Tail pages may be freed if there wasn't any mapping
		 * like if add_to_swap() is running on a lru page that
		 * had its mapping zapped. And freeing these pages
		 * requires taking the lru_lock so we do the put_page
		 * of the tail pages after the split is complete.
		 */
		put_page(page_tail);
	}

	/*
	 * Only the head page (now become a regular page) is required
	 * to be pinned by the caller.
	 */
	BUG_ON(page_count(page) <= 0);
}

static int __split_huge_page_map(struct page *page,
				 struct vm_area_struct *vma,
				 unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;
	pmd_t *pmd, _pmd;
	int ret = 0, i;
	pgtable_t pgtable;
	unsigned long haddr;

	spin_lock(&mm->page_table_lock);
	pmd = page_check_address_pmd(page, mm, address,
				     PAGE_CHECK_ADDRESS_PMD_SPLITTING_FLAG);
	if (pmd) {
		pgtable = get_pmd_huge_pte(mm);
		pmd_populate(mm, &_pmd, pgtable);

		for (i = 0, haddr = address; i < HPAGE_PMD_NR;
		     i++, haddr += PAGE_SIZE) {
			pte_t *pte, entry;
			BUG_ON(PageCompound(page+i));
			entry = mk_pte(page + i, vma->vm_page_prot);
			entry = maybe_mkwrite(pte_mkdirty(entry), vma);
			if (!pmd_write(*pmd))
				entry = pte_wrprotect(entry);
			else
				BUG_ON(page_mapcount(page) != 1);
			if (!pmd_young(*pmd))
				entry = pte_mkold(entry);
			pte = pte_offset_map(&_pmd, haddr);
			BUG_ON(!pte_none(*pte));
			set_pte_at(mm, haddr, pte, entry);
			pte_unmap(pte);
		}

		smp_wmb(); /* make pte visible before pmd */
		/*
		 * Up to this point the pmd is present and huge and
		 * userland has the whole access to the hugepage
		 * during the split (which happens in place). If we
		 * overwrite the pmd with the not-huge version
		 * pointing to the pte here (which of course we could
		 * if all CPUs were bug free), userland could trigger
		 * a small page size TLB miss on the small sized TLB
		 * while the hugepage TLB entry is still established
		 * in the huge TLB. Some CPU doesn't like that. See
		 * http://support.amd.com/us/Processor_TechDocs/41322.pdf,
		 * Erratum 383 on page 93. Intel should be safe but is
		 * also warns that it's only safe if the permission
		 * and cache attributes of the two entries loaded in
		 * the two TLB is identical (which should be the case
		 * here). But it is generally safer to never allow
		 * small and huge TLB entries for the same virtual
		 * address to be loaded simultaneously. So instead of
		 * doing "pmd_populate(); flush_tlb_range();" we first
		 * mark the current pmd notpresent (atomically because
		 * here the pmd_trans_huge and pmd_trans_splitting
		 * must remain set at all times on the pmd until the
		 * split is complete for this pmd), then we flush the
		 * SMP TLB and finally we write the non-huge version
		 * of the pmd entry with pmd_populate.
		 */
		set_pmd_at(mm, address, pmd, pmd_mknotpresent(*pmd));
		flush_tlb_range(vma, address, address + HPAGE_PMD_SIZE);
		pmd_populate(mm, pmd, pgtable);
		ret = 1;
	}
	spin_unlock(&mm->page_table_lock);

	return ret;
}

/* must be called with anon_vma->root->mutex hold */
static void __split_huge_page(struct page *page,
			      struct anon_vma *anon_vma)
{
	int mapcount, mapcount2;
	struct anon_vma_chain *avc;

	BUG_ON(!PageHead(page));
	BUG_ON(PageTail(page));

	mapcount = 0;
	list_for_each_entry(avc, &anon_vma->head, same_anon_vma) {
		struct vm_area_struct *vma = avc->vma;
		unsigned long addr = vma_address(page, vma);
		BUG_ON(is_vma_temporary_stack(vma));
		if (addr == -EFAULT)
			continue;
		mapcount += __split_huge_page_splitting(page, vma, addr);
	}
	/*
	 * It is critical that new vmas are added to the tail of the
	 * anon_vma list. This guarantes that if copy_huge_pmd() runs
	 * and establishes a child pmd before
	 * __split_huge_page_splitting() freezes the parent pmd (so if
	 * we fail to prevent copy_huge_pmd() from running until the
	 * whole __split_huge_page() is complete), we will still see
	 * the newly established pmd of the child later during the
	 * walk, to be able to set it as pmd_trans_splitting too.
	 */
	if (mapcount != page_mapcount(page))
		printk(KERN_ERR "mapcount %d page_mapcount %d\n",
		       mapcount, page_mapcount(page));
	BUG_ON(mapcount != page_mapcount(page));

	__split_huge_page_refcount(page);

	mapcount2 = 0;
	list_for_each_entry(avc, &anon_vma->head, same_anon_vma) {
		struct vm_area_struct *vma = avc->vma;
		unsigned long addr = vma_address(page, vma);
		BUG_ON(is_vma_temporary_stack(vma));
		if (addr == -EFAULT)
			continue;
		mapcount2 += __split_huge_page_map(page, vma, addr);
	}
	if (mapcount != mapcount2)
		printk(KERN_ERR "mapcount %d mapcount2 %d page_mapcount %d\n",
		       mapcount, mapcount2, page_mapcount(page));
	BUG_ON(mapcount != mapcount2);
}

int split_huge_page(struct page *page)
{
	struct anon_vma *anon_vma;
	int ret = 1;

	BUG_ON(!PageAnon(page));
	anon_vma = page_lock_anon_vma(page);
	if (!anon_vma)
		goto out;
	ret = 0;
	if (!PageCompound(page))
		goto out_unlock;

	BUG_ON(!PageSwapBacked(page));
	__split_huge_page(page, anon_vma);
	count_vm_event(THP_SPLIT);

	BUG_ON(PageCompound(page));
out_unlock:
	page_unlock_anon_vma(anon_vma);
out:
	return ret;
}

#define VM_NO_THP (VM_SPECIAL|VM_INSERTPAGE|VM_MIXEDMAP|VM_SAO| \
		   VM_HUGETLB|VM_SHARED|VM_MAYSHARE)

int hugepage_madvise(struct vm_area_struct *vma,
		     unsigned long *vm_flags, int advice)
{
	switch (advice) {
	case MADV_HUGEPAGE:
		/*
		 * Be somewhat over-protective like KSM for now!
		 */
		if (*vm_flags & (VM_HUGEPAGE | VM_NO_THP))
			return -EINVAL;
		*vm_flags &= ~VM_NOHUGEPAGE;
		*vm_flags |= VM_HUGEPAGE;
		/*
		 * If the vma become good for khugepaged to scan,
		 * register it here without waiting a page fault that
		 * may not happen any time soon.
		 */
		if (unlikely(khugepaged_enter_vma_merge(vma)))
			return -ENOMEM;
		break;
	case MADV_NOHUGEPAGE:
		/*
		 * Be somewhat over-protective like KSM for now!
		 */
		if (*vm_flags & (VM_NOHUGEPAGE | VM_NO_THP))
			return -EINVAL;
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

static int __init khugepaged_slab_init(void)
{
	mm_slot_cache = kmem_cache_create("khugepaged_mm_slot",
					  sizeof(struct mm_slot),
					  __alignof__(struct mm_slot), 0, NULL);
	if (!mm_slot_cache)
		return -ENOMEM;

	return 0;
}

static void __init khugepaged_slab_free(void)
{
	kmem_cache_destroy(mm_slot_cache);
	mm_slot_cache = NULL;
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

static int __init mm_slots_hash_init(void)
{
	mm_slots_hash = kzalloc(MM_SLOTS_HASH_HEADS * sizeof(struct hlist_head),
				GFP_KERNEL);
	if (!mm_slots_hash)
		return -ENOMEM;
	return 0;
}

#if 0
static void __init mm_slots_hash_free(void)
{
	kfree(mm_slots_hash);
	mm_slots_hash = NULL;
}
#endif

static struct mm_slot *get_mm_slot(struct mm_struct *mm)
{
	struct mm_slot *mm_slot;
	struct hlist_head *bucket;
	struct hlist_node *node;

	bucket = &mm_slots_hash[((unsigned long)mm / sizeof(struct mm_struct))
				% MM_SLOTS_HASH_HEADS];
	hlist_for_each_entry(mm_slot, node, bucket, hash) {
		if (mm == mm_slot->mm)
			return mm_slot;
	}
	return NULL;
}

static void insert_to_mm_slots_hash(struct mm_struct *mm,
				    struct mm_slot *mm_slot)
{
	struct hlist_head *bucket;

	bucket = &mm_slots_hash[((unsigned long)mm / sizeof(struct mm_struct))
				% MM_SLOTS_HASH_HEADS];
	mm_slot->mm = mm;
	hlist_add_head(&mm_slot->hash, bucket);
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
	VM_BUG_ON(khugepaged_test_exit(mm));
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

	atomic_inc(&mm->mm_count);
	if (wakeup)
		wake_up_interruptible(&khugepaged_wait);

	return 0;
}

int khugepaged_enter_vma_merge(struct vm_area_struct *vma)
{
	unsigned long hstart, hend;
	if (!vma->anon_vma)
		/*
		 * Not yet faulted in so we will register later in the
		 * page fault if needed.
		 */
		return 0;
	if (vma->vm_ops)
		/* khugepaged not yet working on file or special mappings */
		return 0;
	/*
	 * If is_pfn_mapping() is true is_learn_pfn_mapping() must be
	 * true too, verify it here.
	 */
	VM_BUG_ON(is_linear_pfn_mapping(vma) || vma->vm_flags & VM_NO_THP);
	hstart = (vma->vm_start + ~HPAGE_PMD_MASK) & HPAGE_PMD_MASK;
	hend = vma->vm_end & HPAGE_PMD_MASK;
	if (hstart < hend)
		return khugepaged_enter(vma);
	return 0;
}

void __khugepaged_exit(struct mm_struct *mm)
{
	struct mm_slot *mm_slot;
	int free = 0;

	spin_lock(&khugepaged_mm_lock);
	mm_slot = get_mm_slot(mm);
	if (mm_slot && khugepaged_scan.mm_slot != mm_slot) {
		hlist_del(&mm_slot->hash);
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
	/* 0 stands for page_is_file_cache(page) == false */
	dec_zone_page_state(page, NR_ISOLATED_ANON + 0);
	unlock_page(page);
	putback_lru_page(page);
}

static void release_pte_pages(pte_t *pte, pte_t *_pte)
{
	while (--_pte >= pte) {
		pte_t pteval = *_pte;
		if (!pte_none(pteval))
			release_pte_page(pte_page(pteval));
	}
}

static void release_all_pte_pages(pte_t *pte)
{
	release_pte_pages(pte, pte + HPAGE_PMD_NR);
}

static int __collapse_huge_page_isolate(struct vm_area_struct *vma,
					unsigned long address,
					pte_t *pte)
{
	struct page *page;
	pte_t *_pte;
	int referenced = 0, isolated = 0, none = 0;
	for (_pte = pte; _pte < pte+HPAGE_PMD_NR;
	     _pte++, address += PAGE_SIZE) {
		pte_t pteval = *_pte;
		if (pte_none(pteval)) {
			if (++none <= khugepaged_max_ptes_none)
				continue;
			else {
				release_pte_pages(pte, _pte);
				goto out;
			}
		}
		if (!pte_present(pteval) || !pte_write(pteval)) {
			release_pte_pages(pte, _pte);
			goto out;
		}
		page = vm_normal_page(vma, address, pteval);
		if (unlikely(!page)) {
			release_pte_pages(pte, _pte);
			goto out;
		}
		VM_BUG_ON(PageCompound(page));
		BUG_ON(!PageAnon(page));
		VM_BUG_ON(!PageSwapBacked(page));

		/* cannot use mapcount: can't collapse if there's a gup pin */
		if (page_count(page) != 1) {
			release_pte_pages(pte, _pte);
			goto out;
		}
		/*
		 * We can do it before isolate_lru_page because the
		 * page can't be freed from under us. NOTE: PG_lock
		 * is needed to serialize against split_huge_page
		 * when invoked from the VM.
		 */
		if (!trylock_page(page)) {
			release_pte_pages(pte, _pte);
			goto out;
		}
		/*
		 * Isolate the page to avoid collapsing an hugepage
		 * currently in use by the VM.
		 */
		if (isolate_lru_page(page)) {
			unlock_page(page);
			release_pte_pages(pte, _pte);
			goto out;
		}
		/* 0 stands for page_is_file_cache(page) == false */
		inc_zone_page_state(page, NR_ISOLATED_ANON + 0);
		VM_BUG_ON(!PageLocked(page));
		VM_BUG_ON(PageLRU(page));

		/* If there is no mapped pte young don't collapse the page */
		if (pte_young(pteval) || PageReferenced(page) ||
		    mmu_notifier_test_young(vma->vm_mm, address))
			referenced = 1;
	}
	if (unlikely(!referenced))
		release_all_pte_pages(pte);
	else
		isolated = 1;
out:
	return isolated;
}

static void __collapse_huge_page_copy(pte_t *pte, struct page *page,
				      struct vm_area_struct *vma,
				      unsigned long address,
				      spinlock_t *ptl)
{
	pte_t *_pte;
	for (_pte = pte; _pte < pte+HPAGE_PMD_NR; _pte++) {
		pte_t pteval = *_pte;
		struct page *src_page;

		if (pte_none(pteval)) {
			clear_user_highpage(page, address);
			add_mm_counter(vma->vm_mm, MM_ANONPAGES, 1);
		} else {
			src_page = pte_page(pteval);
			copy_user_highpage(page, src_page, address, vma);
			VM_BUG_ON(page_mapcount(src_page) != 1);
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
			page_remove_rmap(src_page);
			spin_unlock(ptl);
			free_page_and_swap_cache(src_page);
		}

		address += PAGE_SIZE;
		page++;
	}
}

static void collapse_huge_page(struct mm_struct *mm,
			       unsigned long address,
			       struct page **hpage,
			       struct vm_area_struct *vma,
			       int node)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd, _pmd;
	pte_t *pte;
	pgtable_t pgtable;
	struct page *new_page;
	spinlock_t *ptl;
	int isolated;
	unsigned long hstart, hend;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);
#ifndef CONFIG_NUMA
	up_read(&mm->mmap_sem);
	VM_BUG_ON(!*hpage);
	new_page = *hpage;
#else
	VM_BUG_ON(*hpage);
	/*
	 * Allocate the page while the vma is still valid and under
	 * the mmap_sem read mode so there is no memory allocation
	 * later when we take the mmap_sem in write mode. This is more
	 * friendly behavior (OTOH it may actually hide bugs) to
	 * filesystems in userland with daemons allocating memory in
	 * the userland I/O paths.  Allocating memory with the
	 * mmap_sem in read mode is good idea also to allow greater
	 * scalability.
	 */
	new_page = alloc_hugepage_vma(khugepaged_defrag(), vma, address,
				      node, __GFP_OTHER_NODE);

	/*
	 * After allocating the hugepage, release the mmap_sem read lock in
	 * preparation for taking it in write mode.
	 */
	up_read(&mm->mmap_sem);
	if (unlikely(!new_page)) {
		count_vm_event(THP_COLLAPSE_ALLOC_FAILED);
		*hpage = ERR_PTR(-ENOMEM);
		return;
	}
#endif

	count_vm_event(THP_COLLAPSE_ALLOC);
	if (unlikely(mem_cgroup_newpage_charge(new_page, mm, GFP_KERNEL))) {
#ifdef CONFIG_NUMA
		put_page(new_page);
#endif
		return;
	}

	/*
	 * Prevent all access to pagetables with the exception of
	 * gup_fast later hanlded by the ptep_clear_flush and the VM
	 * handled by the anon_vma lock + PG_lock.
	 */
	down_write(&mm->mmap_sem);
	if (unlikely(khugepaged_test_exit(mm)))
		goto out;

	vma = find_vma(mm, address);
	hstart = (vma->vm_start + ~HPAGE_PMD_MASK) & HPAGE_PMD_MASK;
	hend = vma->vm_end & HPAGE_PMD_MASK;
	if (address < hstart || address + HPAGE_PMD_SIZE > hend)
		goto out;

	if ((!(vma->vm_flags & VM_HUGEPAGE) && !khugepaged_always()) ||
	    (vma->vm_flags & VM_NOHUGEPAGE))
		goto out;

	if (!vma->anon_vma || vma->vm_ops)
		goto out;
	if (is_vma_temporary_stack(vma))
		goto out;
	/*
	 * If is_pfn_mapping() is true is_learn_pfn_mapping() must be
	 * true too, verify it here.
	 */
	VM_BUG_ON(is_linear_pfn_mapping(vma) || vma->vm_flags & VM_NO_THP);

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto out;

	pud = pud_offset(pgd, address);
	if (!pud_present(*pud))
		goto out;

	pmd = pmd_offset(pud, address);
	/* pmd can't go away or become huge under us */
	if (!pmd_present(*pmd) || pmd_trans_huge(*pmd))
		goto out;

	anon_vma_lock(vma->anon_vma);

	pte = pte_offset_map(pmd, address);
	ptl = pte_lockptr(mm, pmd);

	spin_lock(&mm->page_table_lock); /* probably unnecessary */
	/*
	 * After this gup_fast can't run anymore. This also removes
	 * any huge TLB entry from the CPU so we won't allow
	 * huge and small TLB entries for the same virtual address
	 * to avoid the risk of CPU bugs in that area.
	 */
	_pmd = pmdp_clear_flush_notify(vma, address, pmd);
	spin_unlock(&mm->page_table_lock);

	spin_lock(ptl);
	isolated = __collapse_huge_page_isolate(vma, address, pte);
	spin_unlock(ptl);

	if (unlikely(!isolated)) {
		pte_unmap(pte);
		spin_lock(&mm->page_table_lock);
		BUG_ON(!pmd_none(*pmd));
		set_pmd_at(mm, address, pmd, _pmd);
		spin_unlock(&mm->page_table_lock);
		anon_vma_unlock(vma->anon_vma);
		goto out;
	}

	/*
	 * All pages are isolated and locked so anon_vma rmap
	 * can't run anymore.
	 */
	anon_vma_unlock(vma->anon_vma);

	__collapse_huge_page_copy(pte, new_page, vma, address, ptl);
	pte_unmap(pte);
	__SetPageUptodate(new_page);
	pgtable = pmd_pgtable(_pmd);
	VM_BUG_ON(page_count(pgtable) != 1);
	VM_BUG_ON(page_mapcount(pgtable) != 0);

	_pmd = mk_pmd(new_page, vma->vm_page_prot);
	_pmd = maybe_pmd_mkwrite(pmd_mkdirty(_pmd), vma);
	_pmd = pmd_mkhuge(_pmd);

	/*
	 * spin_lock() below is not the equivalent of smp_wmb(), so
	 * this is needed to avoid the copy_huge_page writes to become
	 * visible after the set_pmd_at() write.
	 */
	smp_wmb();

	spin_lock(&mm->page_table_lock);
	BUG_ON(!pmd_none(*pmd));
	page_add_new_anon_rmap(new_page, vma, address);
	set_pmd_at(mm, address, pmd, _pmd);
	update_mmu_cache(vma, address, _pmd);
	prepare_pmd_huge_pte(pgtable, mm);
	spin_unlock(&mm->page_table_lock);

#ifndef CONFIG_NUMA
	*hpage = NULL;
#endif
	khugepaged_pages_collapsed++;
out_up_write:
	up_write(&mm->mmap_sem);
	return;

out:
	mem_cgroup_uncharge_page(new_page);
#ifdef CONFIG_NUMA
	put_page(new_page);
#endif
	goto out_up_write;
}

static int khugepaged_scan_pmd(struct mm_struct *mm,
			       struct vm_area_struct *vma,
			       unsigned long address,
			       struct page **hpage)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte, *_pte;
	int ret = 0, referenced = 0, none = 0;
	struct page *page;
	unsigned long _address;
	spinlock_t *ptl;
	int node = -1;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto out;

	pud = pud_offset(pgd, address);
	if (!pud_present(*pud))
		goto out;

	pmd = pmd_offset(pud, address);
	if (!pmd_present(*pmd) || pmd_trans_huge(*pmd))
		goto out;

	pte = pte_offset_map_lock(mm, pmd, address, &ptl);
	for (_address = address, _pte = pte; _pte < pte+HPAGE_PMD_NR;
	     _pte++, _address += PAGE_SIZE) {
		pte_t pteval = *_pte;
		if (pte_none(pteval)) {
			if (++none <= khugepaged_max_ptes_none)
				continue;
			else
				goto out_unmap;
		}
		if (!pte_present(pteval) || !pte_write(pteval))
			goto out_unmap;
		page = vm_normal_page(vma, _address, pteval);
		if (unlikely(!page))
			goto out_unmap;
		/*
		 * Chose the node of the first page. This could
		 * be more sophisticated and look at more pages,
		 * but isn't for now.
		 */
		if (node == -1)
			node = page_to_nid(page);
		VM_BUG_ON(PageCompound(page));
		if (!PageLRU(page) || PageLocked(page) || !PageAnon(page))
			goto out_unmap;
		/* cannot use mapcount: can't collapse if there's a gup pin */
		if (page_count(page) != 1)
			goto out_unmap;
		if (pte_young(pteval) || PageReferenced(page) ||
		    mmu_notifier_test_young(vma->vm_mm, address))
			referenced = 1;
	}
	if (referenced)
		ret = 1;
out_unmap:
	pte_unmap_unlock(pte, ptl);
	if (ret)
		/* collapse_huge_page will return with the mmap_sem released */
		collapse_huge_page(mm, address, hpage, vma, node);
out:
	return ret;
}

static void collect_mm_slot(struct mm_slot *mm_slot)
{
	struct mm_struct *mm = mm_slot->mm;

	VM_BUG_ON(NR_CPUS != 1 && !spin_is_locked(&khugepaged_mm_lock));

	if (khugepaged_test_exit(mm)) {
		/* free mm_slot */
		hlist_del(&mm_slot->hash);
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
	down_read(&mm->mmap_sem);
	if (unlikely(khugepaged_test_exit(mm)))
		vma = NULL;
	else
		vma = find_vma(mm, khugepaged_scan.address);

	progress++;
	for (; vma; vma = vma->vm_next) {
		unsigned long hstart, hend;

		cond_resched();
		if (unlikely(khugepaged_test_exit(mm))) {
			progress++;
			break;
		}

		if ((!(vma->vm_flags & VM_HUGEPAGE) &&
		     !khugepaged_always()) ||
		    (vma->vm_flags & VM_NOHUGEPAGE)) {
		skip:
			progress++;
			continue;
		}
		if (!vma->anon_vma || vma->vm_ops)
			goto skip;
		if (is_vma_temporary_stack(vma))
			goto skip;
		/*
		 * If is_pfn_mapping() is true is_learn_pfn_mapping()
		 * must be true too, verify it here.
		 */
		VM_BUG_ON(is_linear_pfn_mapping(vma) ||
			  vma->vm_flags & VM_NO_THP);

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
			ret = khugepaged_scan_pmd(mm, vma,
						  khugepaged_scan.address,
						  hpage);
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
		!khugepaged_enabled();
}

static void khugepaged_do_scan(struct page **hpage)
{
	unsigned int progress = 0, pass_through_head = 0;
	unsigned int pages = khugepaged_pages_to_scan;

	barrier(); /* write khugepaged_pages_to_scan to local stack */

	while (progress < pages) {
		cond_resched();

#ifndef CONFIG_NUMA
		if (!*hpage) {
			*hpage = alloc_hugepage(khugepaged_defrag());
			if (unlikely(!*hpage)) {
				count_vm_event(THP_COLLAPSE_ALLOC_FAILED);
				break;
			}
			count_vm_event(THP_COLLAPSE_ALLOC);
		}
#else
		if (IS_ERR(*hpage))
			break;
#endif

		if (unlikely(kthread_should_stop() || freezing(current)))
			break;

		spin_lock(&khugepaged_mm_lock);
		if (!khugepaged_scan.mm_slot)
			pass_through_head++;
		if (khugepaged_has_work() &&
		    pass_through_head < 2)
			progress += khugepaged_scan_mm_slot(pages - progress,
							    hpage);
		else
			progress = pages;
		spin_unlock(&khugepaged_mm_lock);
	}
}

static void khugepaged_alloc_sleep(void)
{
	wait_event_freezable_timeout(khugepaged_wait, false,
			msecs_to_jiffies(khugepaged_alloc_sleep_millisecs));
}

#ifndef CONFIG_NUMA
static struct page *khugepaged_alloc_hugepage(void)
{
	struct page *hpage;

	do {
		hpage = alloc_hugepage(khugepaged_defrag());
		if (!hpage) {
			count_vm_event(THP_COLLAPSE_ALLOC_FAILED);
			khugepaged_alloc_sleep();
		} else
			count_vm_event(THP_COLLAPSE_ALLOC);
	} while (unlikely(!hpage) &&
		 likely(khugepaged_enabled()));
	return hpage;
}
#endif

static void khugepaged_loop(void)
{
	struct page *hpage;

#ifdef CONFIG_NUMA
	hpage = NULL;
#endif
	while (likely(khugepaged_enabled())) {
#ifndef CONFIG_NUMA
		hpage = khugepaged_alloc_hugepage();
		if (unlikely(!hpage))
			break;
#else
		if (IS_ERR(hpage)) {
			khugepaged_alloc_sleep();
			hpage = NULL;
		}
#endif

		khugepaged_do_scan(&hpage);
#ifndef CONFIG_NUMA
		if (hpage)
			put_page(hpage);
#endif
		try_to_freeze();
		if (unlikely(kthread_should_stop()))
			break;
		if (khugepaged_has_work()) {
			if (!khugepaged_scan_sleep_millisecs)
				continue;
			wait_event_freezable_timeout(khugepaged_wait, false,
			    msecs_to_jiffies(khugepaged_scan_sleep_millisecs));
		} else if (khugepaged_enabled())
			wait_event_freezable(khugepaged_wait,
					     khugepaged_wait_event());
	}
}

static int khugepaged(void *none)
{
	struct mm_slot *mm_slot;

	set_freezable();
	set_user_nice(current, 19);

	/* serialize with start_khugepaged() */
	mutex_lock(&khugepaged_mutex);

	for (;;) {
		mutex_unlock(&khugepaged_mutex);
		VM_BUG_ON(khugepaged_thread != current);
		khugepaged_loop();
		VM_BUG_ON(khugepaged_thread != current);

		mutex_lock(&khugepaged_mutex);
		if (!khugepaged_enabled())
			break;
		if (unlikely(kthread_should_stop()))
			break;
	}

	spin_lock(&khugepaged_mm_lock);
	mm_slot = khugepaged_scan.mm_slot;
	khugepaged_scan.mm_slot = NULL;
	if (mm_slot)
		collect_mm_slot(mm_slot);
	spin_unlock(&khugepaged_mm_lock);

	khugepaged_thread = NULL;
	mutex_unlock(&khugepaged_mutex);

	return 0;
}

void __split_huge_page_pmd(struct mm_struct *mm, pmd_t *pmd)
{
	struct page *page;

	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_trans_huge(*pmd))) {
		spin_unlock(&mm->page_table_lock);
		return;
	}
	page = pmd_page(*pmd);
	VM_BUG_ON(!page_count(page));
	get_page(page);
	spin_unlock(&mm->page_table_lock);

	split_huge_page(page);

	put_page(page);
	BUG_ON(pmd_trans_huge(*pmd));
}

static void split_huge_page_address(struct mm_struct *mm,
				    unsigned long address)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	VM_BUG_ON(!(address & ~HPAGE_PMD_MASK));

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		return;

	pud = pud_offset(pgd, address);
	if (!pud_present(*pud))
		return;

	pmd = pmd_offset(pud, address);
	if (!pmd_present(*pmd))
		return;
	/*
	 * Caller holds the mmap_sem write mode, so a huge pmd cannot
	 * materialize from under us.
	 */
	split_huge_page_pmd(mm, pmd);
}

void __vma_adjust_trans_huge(struct vm_area_struct *vma,
			     unsigned long start,
			     unsigned long end,
			     long adjust_next)
{
	/*
	 * If the new start address isn't hpage aligned and it could
	 * previously contain an hugepage: check if we need to split
	 * an huge pmd.
	 */
	if (start & ~HPAGE_PMD_MASK &&
	    (start & HPAGE_PMD_MASK) >= vma->vm_start &&
	    (start & HPAGE_PMD_MASK) + HPAGE_PMD_SIZE <= vma->vm_end)
		split_huge_page_address(vma->vm_mm, start);

	/*
	 * If the new end address isn't hpage aligned and it could
	 * previously contain an hugepage: check if we need to split
	 * an huge pmd.
	 */
	if (end & ~HPAGE_PMD_MASK &&
	    (end & HPAGE_PMD_MASK) >= vma->vm_start &&
	    (end & HPAGE_PMD_MASK) + HPAGE_PMD_SIZE <= vma->vm_end)
		split_huge_page_address(vma->vm_mm, end);

	/*
	 * If we're also updating the vma->vm_next->vm_start, if the new
	 * vm_next->vm_start isn't page aligned and it could previously
	 * contain an hugepage: check if we need to split an huge pmd.
	 */
	if (adjust_next > 0) {
		struct vm_area_struct *next = vma->vm_next;
		unsigned long nstart = next->vm_start;
		nstart += adjust_next << PAGE_SHIFT;
		if (nstart & ~HPAGE_PMD_MASK &&
		    (nstart & HPAGE_PMD_MASK) >= next->vm_start &&
		    (nstart & HPAGE_PMD_MASK) + HPAGE_PMD_SIZE <= next->vm_end)
			split_huge_page_address(next->vm_mm, nstart);
	}
}
