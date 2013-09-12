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
#include <linux/shrinker.h>
#include <linux/mm_inline.h>
#include <linux/kthread.h>
#include <linux/khugepaged.h>
#include <linux/freezer.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/migrate.h>
#include <linux/hashtable.h>

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
	(1<<TRANSPARENT_HUGEPAGE_DEFRAG_KHUGEPAGED_FLAG)|
	(1<<TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG);

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
static int khugepaged_slab_init(void);

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


static int set_recommended_min_free_kbytes(void)
{
	struct zone *zone;
	int nr_zones = 0;
	unsigned long recommended_min;

	if (!khugepaged_enabled())
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
		if (!khugepaged_thread)
			khugepaged_thread = kthread_run(khugepaged, NULL,
							"khugepaged");
		if (unlikely(IS_ERR(khugepaged_thread))) {
			printk(KERN_ERR
			       "khugepaged: kthread_run(khugepaged) failed\n");
			err = PTR_ERR(khugepaged_thread);
			khugepaged_thread = NULL;
		}

		if (!list_empty(&khugepaged_scan.mm_head))
			wake_up_interruptible(&khugepaged_wait);

		set_recommended_min_free_kbytes();
	} else if (khugepaged_thread) {
		kthread_stop(khugepaged_thread);
		khugepaged_thread = NULL;
	}

	return err;
}

static atomic_t huge_zero_refcount;
static struct page *huge_zero_page __read_mostly;

static inline bool is_huge_zero_page(struct page *page)
{
	return ACCESS_ONCE(huge_zero_page) == page;
}

static inline bool is_huge_zero_pmd(pmd_t pmd)
{
	return is_huge_zero_page(pmd_page(pmd));
}

static struct page *get_huge_zero_page(void)
{
	struct page *zero_page;
retry:
	if (likely(atomic_inc_not_zero(&huge_zero_refcount)))
		return ACCESS_ONCE(huge_zero_page);

	zero_page = alloc_pages((GFP_TRANSHUGE | __GFP_ZERO) & ~__GFP_MOVABLE,
			HPAGE_PMD_ORDER);
	if (!zero_page) {
		count_vm_event(THP_ZERO_PAGE_ALLOC_FAILED);
		return NULL;
	}
	count_vm_event(THP_ZERO_PAGE_ALLOC);
	preempt_disable();
	if (cmpxchg(&huge_zero_page, NULL, zero_page)) {
		preempt_enable();
		__free_page(zero_page);
		goto retry;
	}

	/* We take additional reference here. It will be put back by shrinker */
	atomic_set(&huge_zero_refcount, 2);
	preempt_enable();
	return ACCESS_ONCE(huge_zero_page);
}

static void put_huge_zero_page(void)
{
	/*
	 * Counter should never go to zero here. Only shrinker can put
	 * last reference.
	 */
	BUG_ON(atomic_dec_and_test(&huge_zero_refcount));
}

static unsigned long shrink_huge_zero_page_count(struct shrinker *shrink,
					struct shrink_control *sc)
{
	/* we can free zero page only if last reference remains */
	return atomic_read(&huge_zero_refcount) == 1 ? HPAGE_PMD_NR : 0;
}

static unsigned long shrink_huge_zero_page_scan(struct shrinker *shrink,
				       struct shrink_control *sc)
{
	if (atomic_cmpxchg(&huge_zero_refcount, 1, 0) == 1) {
		struct page *zero_page = xchg(&huge_zero_page, NULL);
		BUG_ON(zero_page == NULL);
		__free_page(zero_page);
		return HPAGE_PMD_NR;
	}

	return 0;
}

static struct shrinker huge_zero_page_shrinker = {
	.count_objects = shrink_huge_zero_page_count,
	.scan_objects = shrink_huge_zero_page_scan,
	.seeks = DEFAULT_SEEKS,
};

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
		int err;

		mutex_lock(&khugepaged_mutex);
		err = start_khugepaged();
		mutex_unlock(&khugepaged_mutex);

		if (err)
			ret = err;
	}

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

static ssize_t use_zero_page_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return single_flag_show(kobj, attr, buf,
				TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG);
}
static ssize_t use_zero_page_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	return single_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG);
}
static struct kobj_attribute use_zero_page_attr =
	__ATTR(use_zero_page, 0644, use_zero_page_show, use_zero_page_store);
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
	&use_zero_page_attr.attr,
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
		printk(KERN_ERR "hugepage: failed to create transparent hugepage kobject\n");
		return -ENOMEM;
	}

	err = sysfs_create_group(*hugepage_kobj, &hugepage_attr_group);
	if (err) {
		printk(KERN_ERR "hugepage: failed to register transparent hugepage group\n");
		goto delete_obj;
	}

	err = sysfs_create_group(*hugepage_kobj, &khugepaged_attr_group);
	if (err) {
		printk(KERN_ERR "hugepage: failed to register transparent hugepage group\n");
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

	register_shrinker(&huge_zero_page_shrinker);

	/*
	 * By default disable transparent hugepages on smaller systems,
	 * where the extra memory used could hurt more than TLB overhead
	 * is likely to save.  The admin can still enable it through /sys.
	 */
	if (totalram_pages < (512 << (20 - PAGE_SHIFT)))
		transparent_hugepage_flags = 0;

	start_khugepaged();

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

pmd_t maybe_pmd_mkwrite(pmd_t pmd, struct vm_area_struct *vma)
{
	if (likely(vma->vm_flags & VM_WRITE))
		pmd = pmd_mkwrite(pmd);
	return pmd;
}

static inline pmd_t mk_huge_pmd(struct page *page, struct vm_area_struct *vma)
{
	pmd_t entry;
	entry = mk_pmd(page, vma->vm_page_prot);
	entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
	entry = pmd_mkhuge(entry);
	return entry;
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
	/*
	 * The memory barrier inside __SetPageUptodate makes sure that
	 * clear_huge_page writes become visible before the set_pmd_at()
	 * write.
	 */
	__SetPageUptodate(page);

	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_none(*pmd))) {
		spin_unlock(&mm->page_table_lock);
		mem_cgroup_uncharge_page(page);
		put_page(page);
		pte_free(mm, pgtable);
	} else {
		pmd_t entry;
		entry = mk_huge_pmd(page, vma);
		page_add_new_anon_rmap(page, vma, haddr);
		pgtable_trans_huge_deposit(mm, pmd, pgtable);
		set_pmd_at(mm, haddr, pmd, entry);
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

static bool set_huge_zero_page(pgtable_t pgtable, struct mm_struct *mm,
		struct vm_area_struct *vma, unsigned long haddr, pmd_t *pmd,
		struct page *zero_page)
{
	pmd_t entry;
	if (!pmd_none(*pmd))
		return false;
	entry = mk_pmd(zero_page, vma->vm_page_prot);
	entry = pmd_wrprotect(entry);
	entry = pmd_mkhuge(entry);
	pgtable_trans_huge_deposit(mm, pmd, pgtable);
	set_pmd_at(mm, haddr, pmd, entry);
	mm->nr_ptes++;
	return true;
}

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
		if (!(flags & FAULT_FLAG_WRITE) &&
				transparent_hugepage_use_zero_page()) {
			pgtable_t pgtable;
			struct page *zero_page;
			bool set;
			pgtable = pte_alloc_one(mm, haddr);
			if (unlikely(!pgtable))
				return VM_FAULT_OOM;
			zero_page = get_huge_zero_page();
			if (unlikely(!zero_page)) {
				pte_free(mm, pgtable);
				count_vm_event(THP_FAULT_FALLBACK);
				goto out;
			}
			spin_lock(&mm->page_table_lock);
			set = set_huge_zero_page(pgtable, mm, vma, haddr, pmd,
					zero_page);
			spin_unlock(&mm->page_table_lock);
			if (!set) {
				pte_free(mm, pgtable);
				put_huge_zero_page();
			}
			return 0;
		}
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
	if (unlikely(pmd_none(*pmd)) &&
	    unlikely(__pte_alloc(mm, vma, pmd, address)))
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
	/*
	 * mm->page_table_lock is enough to be sure that huge zero pmd is not
	 * under splitting since we don't split the page itself, only pmd to
	 * a page table.
	 */
	if (is_huge_zero_pmd(pmd)) {
		struct page *zero_page;
		bool set;
		/*
		 * get_huge_zero_page() will never allocate a new page here,
		 * since we already have a zero page to copy. It just takes a
		 * reference.
		 */
		zero_page = get_huge_zero_page();
		set = set_huge_zero_page(pgtable, dst_mm, vma, addr, dst_pmd,
				zero_page);
		BUG_ON(!set); /* unexpected !pmd_none(dst_pmd) */
		ret = 0;
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
	pgtable_trans_huge_deposit(dst_mm, dst_pmd, pgtable);
	set_pmd_at(dst_mm, addr, dst_pmd, pmd);
	dst_mm->nr_ptes++;

	ret = 0;
out_unlock:
	spin_unlock(&src_mm->page_table_lock);
	spin_unlock(&dst_mm->page_table_lock);
out:
	return ret;
}

void huge_pmd_set_accessed(struct mm_struct *mm,
			   struct vm_area_struct *vma,
			   unsigned long address,
			   pmd_t *pmd, pmd_t orig_pmd,
			   int dirty)
{
	pmd_t entry;
	unsigned long haddr;

	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_same(*pmd, orig_pmd)))
		goto unlock;

	entry = pmd_mkyoung(orig_pmd);
	haddr = address & HPAGE_PMD_MASK;
	if (pmdp_set_access_flags(vma, haddr, pmd, entry, dirty))
		update_mmu_cache_pmd(vma, address, pmd);

unlock:
	spin_unlock(&mm->page_table_lock);
}

static int do_huge_pmd_wp_zero_page_fallback(struct mm_struct *mm,
		struct vm_area_struct *vma, unsigned long address,
		pmd_t *pmd, pmd_t orig_pmd, unsigned long haddr)
{
	pgtable_t pgtable;
	pmd_t _pmd;
	struct page *page;
	int i, ret = 0;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */

	page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, address);
	if (!page) {
		ret |= VM_FAULT_OOM;
		goto out;
	}

	if (mem_cgroup_newpage_charge(page, mm, GFP_KERNEL)) {
		put_page(page);
		ret |= VM_FAULT_OOM;
		goto out;
	}

	clear_user_highpage(page, address);
	__SetPageUptodate(page);

	mmun_start = haddr;
	mmun_end   = haddr + HPAGE_PMD_SIZE;
	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);

	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_same(*pmd, orig_pmd)))
		goto out_free_page;

	pmdp_clear_flush(vma, haddr, pmd);
	/* leave pmd empty until pte is filled */

	pgtable = pgtable_trans_huge_withdraw(mm, pmd);
	pmd_populate(mm, &_pmd, pgtable);

	for (i = 0; i < HPAGE_PMD_NR; i++, haddr += PAGE_SIZE) {
		pte_t *pte, entry;
		if (haddr == (address & PAGE_MASK)) {
			entry = mk_pte(page, vma->vm_page_prot);
			entry = maybe_mkwrite(pte_mkdirty(entry), vma);
			page_add_new_anon_rmap(page, vma, haddr);
		} else {
			entry = pfn_pte(my_zero_pfn(haddr), vma->vm_page_prot);
			entry = pte_mkspecial(entry);
		}
		pte = pte_offset_map(&_pmd, haddr);
		VM_BUG_ON(!pte_none(*pte));
		set_pte_at(mm, haddr, pte, entry);
		pte_unmap(pte);
	}
	smp_wmb(); /* make pte visible before pmd */
	pmd_populate(mm, pmd, pgtable);
	spin_unlock(&mm->page_table_lock);
	put_huge_zero_page();
	inc_mm_counter(mm, MM_ANONPAGES);

	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);

	ret |= VM_FAULT_WRITE;
out:
	return ret;
out_free_page:
	spin_unlock(&mm->page_table_lock);
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
	mem_cgroup_uncharge_page(page);
	put_page(page);
	goto out;
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
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */

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

	mmun_start = haddr;
	mmun_end   = haddr + HPAGE_PMD_SIZE;
	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);

	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_same(*pmd, orig_pmd)))
		goto out_free_pages;
	VM_BUG_ON(!PageHead(page));

	pmdp_clear_flush(vma, haddr, pmd);
	/* leave pmd empty until pte is filled */

	pgtable = pgtable_trans_huge_withdraw(mm, pmd);
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

	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);

	ret |= VM_FAULT_WRITE;
	put_page(page);

out:
	return ret;

out_free_pages:
	spin_unlock(&mm->page_table_lock);
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
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
	struct page *page = NULL, *new_page;
	unsigned long haddr;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */

	VM_BUG_ON(!vma->anon_vma);
	haddr = address & HPAGE_PMD_MASK;
	if (is_huge_zero_pmd(orig_pmd))
		goto alloc;
	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_same(*pmd, orig_pmd)))
		goto out_unlock;

	page = pmd_page(orig_pmd);
	VM_BUG_ON(!PageCompound(page) || !PageHead(page));
	if (page_mapcount(page) == 1) {
		pmd_t entry;
		entry = pmd_mkyoung(orig_pmd);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		if (pmdp_set_access_flags(vma, haddr, pmd, entry,  1))
			update_mmu_cache_pmd(vma, address, pmd);
		ret |= VM_FAULT_WRITE;
		goto out_unlock;
	}
	get_page(page);
	spin_unlock(&mm->page_table_lock);
alloc:
	if (transparent_hugepage_enabled(vma) &&
	    !transparent_hugepage_debug_cow())
		new_page = alloc_hugepage_vma(transparent_hugepage_defrag(vma),
					      vma, haddr, numa_node_id(), 0);
	else
		new_page = NULL;

	if (unlikely(!new_page)) {
		count_vm_event(THP_FAULT_FALLBACK);
		if (is_huge_zero_pmd(orig_pmd)) {
			ret = do_huge_pmd_wp_zero_page_fallback(mm, vma,
					address, pmd, orig_pmd, haddr);
		} else {
			ret = do_huge_pmd_wp_page_fallback(mm, vma, address,
					pmd, orig_pmd, page, haddr);
			if (ret & VM_FAULT_OOM)
				split_huge_page(page);
			put_page(page);
		}
		goto out;
	}
	count_vm_event(THP_FAULT_ALLOC);

	if (unlikely(mem_cgroup_newpage_charge(new_page, mm, GFP_KERNEL))) {
		put_page(new_page);
		if (page) {
			split_huge_page(page);
			put_page(page);
		}
		ret |= VM_FAULT_OOM;
		goto out;
	}

	if (is_huge_zero_pmd(orig_pmd))
		clear_huge_page(new_page, haddr, HPAGE_PMD_NR);
	else
		copy_user_huge_page(new_page, page, haddr, vma, HPAGE_PMD_NR);
	__SetPageUptodate(new_page);

	mmun_start = haddr;
	mmun_end   = haddr + HPAGE_PMD_SIZE;
	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);

	spin_lock(&mm->page_table_lock);
	if (page)
		put_page(page);
	if (unlikely(!pmd_same(*pmd, orig_pmd))) {
		spin_unlock(&mm->page_table_lock);
		mem_cgroup_uncharge_page(new_page);
		put_page(new_page);
		goto out_mn;
	} else {
		pmd_t entry;
		entry = mk_huge_pmd(new_page, vma);
		pmdp_clear_flush(vma, haddr, pmd);
		page_add_new_anon_rmap(new_page, vma, haddr);
		set_pmd_at(mm, haddr, pmd, entry);
		update_mmu_cache_pmd(vma, address, pmd);
		if (is_huge_zero_pmd(orig_pmd)) {
			add_mm_counter(mm, MM_ANONPAGES, HPAGE_PMD_NR);
			put_huge_zero_page();
		} else {
			VM_BUG_ON(!PageHead(page));
			page_remove_rmap(page);
			put_page(page);
		}
		ret |= VM_FAULT_WRITE;
	}
	spin_unlock(&mm->page_table_lock);
out_mn:
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
out:
	return ret;
out_unlock:
	spin_unlock(&mm->page_table_lock);
	return ret;
}

struct page *follow_trans_huge_pmd(struct vm_area_struct *vma,
				   unsigned long addr,
				   pmd_t *pmd,
				   unsigned int flags)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page *page = NULL;

	assert_spin_locked(&mm->page_table_lock);

	if (flags & FOLL_WRITE && !pmd_write(*pmd))
		goto out;

	/* Avoid dumping huge zero page */
	if ((flags & FOLL_DUMP) && is_huge_zero_pmd(*pmd))
		return ERR_PTR(-EFAULT);

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
		if (pmdp_set_access_flags(vma, addr & HPAGE_PMD_MASK,
					  pmd, _pmd,  1))
			update_mmu_cache_pmd(vma, addr, pmd);
	}
	if ((flags & FOLL_MLOCK) && (vma->vm_flags & VM_LOCKED)) {
		if (page->mapping && trylock_page(page)) {
			lru_add_drain();
			if (page->mapping)
				mlock_vma_page(page);
			unlock_page(page);
		}
	}
	page += (addr & ~HPAGE_PMD_MASK) >> PAGE_SHIFT;
	VM_BUG_ON(!PageCompound(page));
	if (flags & FOLL_GET)
		get_page_foll(page);

out:
	return page;
}

/* NUMA hinting page fault entry point for trans huge pmds */
int do_huge_pmd_numa_page(struct mm_struct *mm, struct vm_area_struct *vma,
				unsigned long addr, pmd_t pmd, pmd_t *pmdp)
{
	struct page *page;
	unsigned long haddr = addr & HPAGE_PMD_MASK;
	int target_nid;
	int current_nid = -1;
	bool migrated;

	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_same(pmd, *pmdp)))
		goto out_unlock;

	page = pmd_page(pmd);
	get_page(page);
	current_nid = page_to_nid(page);
	count_vm_numa_event(NUMA_HINT_FAULTS);
	if (current_nid == numa_node_id())
		count_vm_numa_event(NUMA_HINT_FAULTS_LOCAL);

	target_nid = mpol_misplaced(page, vma, haddr);
	if (target_nid == -1) {
		put_page(page);
		goto clear_pmdnuma;
	}

	/* Acquire the page lock to serialise THP migrations */
	spin_unlock(&mm->page_table_lock);
	lock_page(page);

	/* Confirm the PTE did not while locked */
	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_same(pmd, *pmdp))) {
		unlock_page(page);
		put_page(page);
		goto out_unlock;
	}
	spin_unlock(&mm->page_table_lock);

	/* Migrate the THP to the requested node */
	migrated = migrate_misplaced_transhuge_page(mm, vma,
				pmdp, pmd, addr, page, target_nid);
	if (!migrated)
		goto check_same;

	task_numa_fault(target_nid, HPAGE_PMD_NR, true);
	return 0;

check_same:
	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_same(pmd, *pmdp)))
		goto out_unlock;
clear_pmdnuma:
	pmd = pmd_mknonnuma(pmd);
	set_pmd_at(mm, haddr, pmdp, pmd);
	VM_BUG_ON(pmd_numa(*pmdp));
	update_mmu_cache_pmd(vma, addr, pmdp);
out_unlock:
	spin_unlock(&mm->page_table_lock);
	if (current_nid != -1)
		task_numa_fault(current_nid, HPAGE_PMD_NR, false);
	return 0;
}

int zap_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		 pmd_t *pmd, unsigned long addr)
{
	int ret = 0;

	if (__pmd_trans_huge_lock(pmd, vma) == 1) {
		struct page *page;
		pgtable_t pgtable;
		pmd_t orig_pmd;
		/*
		 * For architectures like ppc64 we look at deposited pgtable
		 * when calling pmdp_get_and_clear. So do the
		 * pgtable_trans_huge_withdraw after finishing pmdp related
		 * operations.
		 */
		orig_pmd = pmdp_get_and_clear(tlb->mm, addr, pmd);
		tlb_remove_pmd_tlb_entry(tlb, pmd, addr);
		pgtable = pgtable_trans_huge_withdraw(tlb->mm, pmd);
		if (is_huge_zero_pmd(orig_pmd)) {
			tlb->mm->nr_ptes--;
			spin_unlock(&tlb->mm->page_table_lock);
			put_huge_zero_page();
		} else {
			page = pmd_page(orig_pmd);
			page_remove_rmap(page);
			VM_BUG_ON(page_mapcount(page) < 0);
			add_mm_counter(tlb->mm, MM_ANONPAGES, -HPAGE_PMD_NR);
			VM_BUG_ON(!PageHead(page));
			tlb->mm->nr_ptes--;
			spin_unlock(&tlb->mm->page_table_lock);
			tlb_remove_page(tlb, page);
		}
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
		set_pmd_at(mm, new_addr, new_pmd, pmd_mksoft_dirty(pmd));
		spin_unlock(&mm->page_table_lock);
	}
out:
	return ret;
}

int change_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long addr, pgprot_t newprot, int prot_numa)
{
	struct mm_struct *mm = vma->vm_mm;
	int ret = 0;

	if (__pmd_trans_huge_lock(pmd, vma) == 1) {
		pmd_t entry;
		entry = pmdp_get_and_clear(mm, addr, pmd);
		if (!prot_numa) {
			entry = pmd_modify(entry, newprot);
			BUG_ON(pmd_write(entry));
		} else {
			struct page *page = pmd_page(*pmd);

			/* only check non-shared pages */
			if (page_mapcount(page) == 1 &&
			    !pmd_numa(*pmd)) {
				entry = pmd_mknuma(entry);
			}
		}
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
	pmd_t *pmd, *ret = NULL;

	if (address & ~HPAGE_PMD_MASK)
		goto out;

	pmd = mm_find_pmd(mm, address);
	if (!pmd)
		goto out;
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
	/* For mmu_notifiers */
	const unsigned long mmun_start = address;
	const unsigned long mmun_end   = address + HPAGE_PMD_SIZE;

	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);
	spin_lock(&mm->page_table_lock);
	pmd = page_check_address_pmd(page, mm, address,
				     PAGE_CHECK_ADDRESS_PMD_NOTSPLITTING_FLAG);
	if (pmd) {
		/*
		 * We can't temporarily set the pmd to null in order
		 * to split it, the pmd must remain marked huge at all
		 * times or the VM won't take the pmd_trans_huge paths
		 * and it won't wait on the anon_vma->root->rwsem to
		 * serialize against split_huge_page*.
		 */
		pmdp_splitting_flush(vma, address, pmd);
		ret = 1;
	}
	spin_unlock(&mm->page_table_lock);
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);

	return ret;
}

static void __split_huge_page_refcount(struct page *page,
				       struct list_head *list)
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
				      (1L << PG_uptodate) |
				      (1L << PG_active) |
				      (1L << PG_unevictable)));
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
		page_nid_xchg_last(page_tail, page_nid_last(page));

		BUG_ON(!PageAnon(page_tail));
		BUG_ON(!PageUptodate(page_tail));
		BUG_ON(!PageDirty(page_tail));
		BUG_ON(!PageSwapBacked(page_tail));

		lru_add_page_tail(page, page_tail, lruvec, list);
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
		pgtable = pgtable_trans_huge_withdraw(mm, pmd);
		pmd_populate(mm, &_pmd, pgtable);

		haddr = address;
		for (i = 0; i < HPAGE_PMD_NR; i++, haddr += PAGE_SIZE) {
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
			if (pmd_numa(*pmd))
				entry = pte_mknuma(entry);
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
		pmdp_invalidate(vma, address, pmd);
		pmd_populate(mm, pmd, pgtable);
		ret = 1;
	}
	spin_unlock(&mm->page_table_lock);

	return ret;
}

/* must be called with anon_vma->root->rwsem held */
static void __split_huge_page(struct page *page,
			      struct anon_vma *anon_vma,
			      struct list_head *list)
{
	int mapcount, mapcount2;
	pgoff_t pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);
	struct anon_vma_chain *avc;

	BUG_ON(!PageHead(page));
	BUG_ON(PageTail(page));

	mapcount = 0;
	anon_vma_interval_tree_foreach(avc, &anon_vma->rb_root, pgoff, pgoff) {
		struct vm_area_struct *vma = avc->vma;
		unsigned long addr = vma_address(page, vma);
		BUG_ON(is_vma_temporary_stack(vma));
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

	__split_huge_page_refcount(page, list);

	mapcount2 = 0;
	anon_vma_interval_tree_foreach(avc, &anon_vma->rb_root, pgoff, pgoff) {
		struct vm_area_struct *vma = avc->vma;
		unsigned long addr = vma_address(page, vma);
		BUG_ON(is_vma_temporary_stack(vma));
		mapcount2 += __split_huge_page_map(page, vma, addr);
	}
	if (mapcount != mapcount2)
		printk(KERN_ERR "mapcount %d mapcount2 %d page_mapcount %d\n",
		       mapcount, mapcount2, page_mapcount(page));
	BUG_ON(mapcount != mapcount2);
}

/*
 * Split a hugepage into normal pages. This doesn't change the position of head
 * page. If @list is null, tail pages will be added to LRU list, otherwise, to
 * @list. Both head page and tail pages will inherit mapping, flags, and so on
 * from the hugepage.
 * Return 0 if the hugepage is split successfully otherwise return 1.
 */
int split_huge_page_to_list(struct page *page, struct list_head *list)
{
	struct anon_vma *anon_vma;
	int ret = 1;

	BUG_ON(is_huge_zero_page(page));
	BUG_ON(!PageAnon(page));

	/*
	 * The caller does not necessarily hold an mmap_sem that would prevent
	 * the anon_vma disappearing so we first we take a reference to it
	 * and then lock the anon_vma for write. This is similar to
	 * page_lock_anon_vma_read except the write lock is taken to serialise
	 * against parallel split or collapse operations.
	 */
	anon_vma = page_get_anon_vma(page);
	if (!anon_vma)
		goto out;
	anon_vma_lock_write(anon_vma);

	ret = 0;
	if (!PageCompound(page))
		goto out_unlock;

	BUG_ON(!PageSwapBacked(page));
	__split_huge_page(page, anon_vma, list);
	count_vm_event(THP_SPLIT);

	BUG_ON(PageCompound(page));
out_unlock:
	anon_vma_unlock_write(anon_vma);
	put_anon_vma(anon_vma);
out:
	return ret;
}

#define VM_NO_THP (VM_SPECIAL|VM_MIXEDMAP|VM_HUGETLB|VM_SHARED|VM_MAYSHARE)

int hugepage_madvise(struct vm_area_struct *vma,
		     unsigned long *vm_flags, int advice)
{
	struct mm_struct *mm = vma->vm_mm;

	switch (advice) {
	case MADV_HUGEPAGE:
		/*
		 * Be somewhat over-protective like KSM for now!
		 */
		if (*vm_flags & (VM_HUGEPAGE | VM_NO_THP))
			return -EINVAL;
		if (mm->def_flags & VM_NOHUGEPAGE)
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
	VM_BUG_ON(vma->vm_flags & VM_NO_THP);
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

static int __collapse_huge_page_isolate(struct vm_area_struct *vma,
					unsigned long address,
					pte_t *pte)
{
	struct page *page;
	pte_t *_pte;
	int referenced = 0, none = 0;
	for (_pte = pte; _pte < pte+HPAGE_PMD_NR;
	     _pte++, address += PAGE_SIZE) {
		pte_t pteval = *_pte;
		if (pte_none(pteval)) {
			if (++none <= khugepaged_max_ptes_none)
				continue;
			else
				goto out;
		}
		if (!pte_present(pteval) || !pte_write(pteval))
			goto out;
		page = vm_normal_page(vma, address, pteval);
		if (unlikely(!page))
			goto out;

		VM_BUG_ON(PageCompound(page));
		BUG_ON(!PageAnon(page));
		VM_BUG_ON(!PageSwapBacked(page));

		/* cannot use mapcount: can't collapse if there's a gup pin */
		if (page_count(page) != 1)
			goto out;
		/*
		 * We can do it before isolate_lru_page because the
		 * page can't be freed from under us. NOTE: PG_lock
		 * is needed to serialize against split_huge_page
		 * when invoked from the VM.
		 */
		if (!trylock_page(page))
			goto out;
		/*
		 * Isolate the page to avoid collapsing an hugepage
		 * currently in use by the VM.
		 */
		if (isolate_lru_page(page)) {
			unlock_page(page);
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
	if (likely(referenced))
		return 1;
out:
	release_pte_pages(pte, _pte);
	return 0;
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

static void khugepaged_alloc_sleep(void)
{
	wait_event_freezable_timeout(khugepaged_wait, false,
			msecs_to_jiffies(khugepaged_alloc_sleep_millisecs));
}

#ifdef CONFIG_NUMA
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

static struct page
*khugepaged_alloc_page(struct page **hpage, struct mm_struct *mm,
		       struct vm_area_struct *vma, unsigned long address,
		       int node)
{
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
	*hpage  = alloc_hugepage_vma(khugepaged_defrag(), vma, address,
				      node, __GFP_OTHER_NODE);

	/*
	 * After allocating the hugepage, release the mmap_sem read lock in
	 * preparation for taking it in write mode.
	 */
	up_read(&mm->mmap_sem);
	if (unlikely(!*hpage)) {
		count_vm_event(THP_COLLAPSE_ALLOC_FAILED);
		*hpage = ERR_PTR(-ENOMEM);
		return NULL;
	}

	count_vm_event(THP_COLLAPSE_ALLOC);
	return *hpage;
}
#else
static struct page *khugepaged_alloc_hugepage(bool *wait)
{
	struct page *hpage;

	do {
		hpage = alloc_hugepage(khugepaged_defrag());
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

static struct page
*khugepaged_alloc_page(struct page **hpage, struct mm_struct *mm,
		       struct vm_area_struct *vma, unsigned long address,
		       int node)
{
	up_read(&mm->mmap_sem);
	VM_BUG_ON(!*hpage);
	return  *hpage;
}
#endif

static bool hugepage_vma_check(struct vm_area_struct *vma)
{
	if ((!(vma->vm_flags & VM_HUGEPAGE) && !khugepaged_always()) ||
	    (vma->vm_flags & VM_NOHUGEPAGE))
		return false;

	if (!vma->anon_vma || vma->vm_ops)
		return false;
	if (is_vma_temporary_stack(vma))
		return false;
	VM_BUG_ON(vma->vm_flags & VM_NO_THP);
	return true;
}

static void collapse_huge_page(struct mm_struct *mm,
				   unsigned long address,
				   struct page **hpage,
				   struct vm_area_struct *vma,
				   int node)
{
	pmd_t *pmd, _pmd;
	pte_t *pte;
	pgtable_t pgtable;
	struct page *new_page;
	spinlock_t *ptl;
	int isolated;
	unsigned long hstart, hend;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);

	/* release the mmap_sem read lock. */
	new_page = khugepaged_alloc_page(hpage, mm, vma, address, node);
	if (!new_page)
		return;

	if (unlikely(mem_cgroup_newpage_charge(new_page, mm, GFP_KERNEL)))
		return;

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
	if (!hugepage_vma_check(vma))
		goto out;
	pmd = mm_find_pmd(mm, address);
	if (!pmd)
		goto out;
	if (pmd_trans_huge(*pmd))
		goto out;

	anon_vma_lock_write(vma->anon_vma);

	pte = pte_offset_map(pmd, address);
	ptl = pte_lockptr(mm, pmd);

	mmun_start = address;
	mmun_end   = address + HPAGE_PMD_SIZE;
	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);
	spin_lock(&mm->page_table_lock); /* probably unnecessary */
	/*
	 * After this gup_fast can't run anymore. This also removes
	 * any huge TLB entry from the CPU so we won't allow
	 * huge and small TLB entries for the same virtual address
	 * to avoid the risk of CPU bugs in that area.
	 */
	_pmd = pmdp_clear_flush(vma, address, pmd);
	spin_unlock(&mm->page_table_lock);
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);

	spin_lock(ptl);
	isolated = __collapse_huge_page_isolate(vma, address, pte);
	spin_unlock(ptl);

	if (unlikely(!isolated)) {
		pte_unmap(pte);
		spin_lock(&mm->page_table_lock);
		BUG_ON(!pmd_none(*pmd));
		/*
		 * We can only use set_pmd_at when establishing
		 * hugepmds and never for establishing regular pmds that
		 * points to regular pagetables. Use pmd_populate for that
		 */
		pmd_populate(mm, pmd, pmd_pgtable(_pmd));
		spin_unlock(&mm->page_table_lock);
		anon_vma_unlock_write(vma->anon_vma);
		goto out;
	}

	/*
	 * All pages are isolated and locked so anon_vma rmap
	 * can't run anymore.
	 */
	anon_vma_unlock_write(vma->anon_vma);

	__collapse_huge_page_copy(pte, new_page, vma, address, ptl);
	pte_unmap(pte);
	__SetPageUptodate(new_page);
	pgtable = pmd_pgtable(_pmd);

	_pmd = mk_huge_pmd(new_page, vma);

	/*
	 * spin_lock() below is not the equivalent of smp_wmb(), so
	 * this is needed to avoid the copy_huge_page writes to become
	 * visible after the set_pmd_at() write.
	 */
	smp_wmb();

	spin_lock(&mm->page_table_lock);
	BUG_ON(!pmd_none(*pmd));
	page_add_new_anon_rmap(new_page, vma, address);
	pgtable_trans_huge_deposit(mm, pmd, pgtable);
	set_pmd_at(mm, address, pmd, _pmd);
	update_mmu_cache_pmd(vma, address, pmd);
	spin_unlock(&mm->page_table_lock);

	*hpage = NULL;

	khugepaged_pages_collapsed++;
out_up_write:
	up_write(&mm->mmap_sem);
	return;

out:
	mem_cgroup_uncharge_page(new_page);
	goto out_up_write;
}

static int khugepaged_scan_pmd(struct mm_struct *mm,
			       struct vm_area_struct *vma,
			       unsigned long address,
			       struct page **hpage)
{
	pmd_t *pmd;
	pte_t *pte, *_pte;
	int ret = 0, referenced = 0, none = 0;
	struct page *page;
	unsigned long _address;
	spinlock_t *ptl;
	int node = NUMA_NO_NODE;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);

	pmd = mm_find_pmd(mm, address);
	if (!pmd)
		goto out;
	if (pmd_trans_huge(*pmd))
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
		if (node == NUMA_NO_NODE)
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

		if (unlikely(kthread_should_stop() || freezing(current)))
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

static void khugepaged_wait_work(void)
{
	try_to_freeze();

	if (khugepaged_has_work()) {
		if (!khugepaged_scan_sleep_millisecs)
			return;

		wait_event_freezable_timeout(khugepaged_wait,
					     kthread_should_stop(),
			msecs_to_jiffies(khugepaged_scan_sleep_millisecs));
		return;
	}

	if (khugepaged_enabled())
		wait_event_freezable(khugepaged_wait, khugepaged_wait_event());
}

static int khugepaged(void *none)
{
	struct mm_slot *mm_slot;

	set_freezable();
	set_user_nice(current, 19);

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

static void __split_huge_zero_page_pmd(struct vm_area_struct *vma,
		unsigned long haddr, pmd_t *pmd)
{
	struct mm_struct *mm = vma->vm_mm;
	pgtable_t pgtable;
	pmd_t _pmd;
	int i;

	pmdp_clear_flush(vma, haddr, pmd);
	/* leave pmd empty until pte is filled */

	pgtable = pgtable_trans_huge_withdraw(mm, pmd);
	pmd_populate(mm, &_pmd, pgtable);

	for (i = 0; i < HPAGE_PMD_NR; i++, haddr += PAGE_SIZE) {
		pte_t *pte, entry;
		entry = pfn_pte(my_zero_pfn(haddr), vma->vm_page_prot);
		entry = pte_mkspecial(entry);
		pte = pte_offset_map(&_pmd, haddr);
		VM_BUG_ON(!pte_none(*pte));
		set_pte_at(mm, haddr, pte, entry);
		pte_unmap(pte);
	}
	smp_wmb(); /* make pte visible before pmd */
	pmd_populate(mm, pmd, pgtable);
	put_huge_zero_page();
}

void __split_huge_page_pmd(struct vm_area_struct *vma, unsigned long address,
		pmd_t *pmd)
{
	struct page *page;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long haddr = address & HPAGE_PMD_MASK;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */

	BUG_ON(vma->vm_start > haddr || vma->vm_end < haddr + HPAGE_PMD_SIZE);

	mmun_start = haddr;
	mmun_end   = haddr + HPAGE_PMD_SIZE;
	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);
	spin_lock(&mm->page_table_lock);
	if (unlikely(!pmd_trans_huge(*pmd))) {
		spin_unlock(&mm->page_table_lock);
		mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
		return;
	}
	if (is_huge_zero_pmd(*pmd)) {
		__split_huge_zero_page_pmd(vma, haddr, pmd);
		spin_unlock(&mm->page_table_lock);
		mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
		return;
	}
	page = pmd_page(*pmd);
	VM_BUG_ON(!page_count(page));
	get_page(page);
	spin_unlock(&mm->page_table_lock);
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);

	split_huge_page(page);

	put_page(page);
	BUG_ON(pmd_trans_huge(*pmd));
}

void split_huge_page_pmd_mm(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd)
{
	struct vm_area_struct *vma;

	vma = find_vma(mm, address);
	BUG_ON(vma == NULL);
	split_huge_page_pmd(vma, address, pmd);
}

static void split_huge_page_address(struct mm_struct *mm,
				    unsigned long address)
{
	pmd_t *pmd;

	VM_BUG_ON(!(address & ~HPAGE_PMD_MASK));

	pmd = mm_find_pmd(mm, address);
	if (!pmd)
		return;
	/*
	 * Caller holds the mmap_sem write mode, so a huge pmd cannot
	 * materialize from under us.
	 */
	split_huge_page_pmd_mm(mm, address, pmd);
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
