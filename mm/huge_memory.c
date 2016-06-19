/*
 *  Copyright (C) 2009  Red Hat, Inc.
 *
 *  This work is licensed under the terms of the GNU GPL, version 2. See
 *  the COPYING file in the top-level directory.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/mmu_notifier.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/shrinker.h>
#include <linux/mm_inline.h>
#include <linux/swapops.h>
#include <linux/dax.h>
#include <linux/kthread.h>
#include <linux/khugepaged.h>
#include <linux/freezer.h>
#include <linux/pfn_t.h>
#include <linux/mman.h>
#include <linux/memremap.h>
#include <linux/pagemap.h>
#include <linux/debugfs.h>
#include <linux/migrate.h>
#include <linux/hashtable.h>
#include <linux/userfaultfd_k.h>
#include <linux/page_idle.h>

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
	SCAN_NO_REFERENCED_PAGE,
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
	SCAN_CGROUP_CHARGE_FAIL
};

#define CREATE_TRACE_POINTS
#include <trace/events/huge_memory.h>

/*
 * By default transparent hugepage support is disabled in order that avoid
 * to risk increase the memory footprint of applications without a guaranteed
 * benefit. When transparent hugepage support is enabled, is for all mappings,
 * and khugepaged scans all mappings.
 * Defrag is invoked by khugepaged hugepage allocations and by page faults
 * for all hugepage allocations.
 */
unsigned long transparent_hugepage_flags __read_mostly =
#ifdef CONFIG_TRANSPARENT_HUGEPAGE_ALWAYS
	(1<<TRANSPARENT_HUGEPAGE_FLAG)|
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE_MADVISE
	(1<<TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG)|
#endif
	(1<<TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG)|
	(1<<TRANSPARENT_HUGEPAGE_DEFRAG_KHUGEPAGED_FLAG)|
	(1<<TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG);

/* default scan 8*512 pte (or vmas) every 30 second */
static unsigned int khugepaged_pages_to_scan __read_mostly;
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
static unsigned int khugepaged_max_ptes_none __read_mostly;

static int khugepaged(void *none);
static int khugepaged_slab_init(void);
static void khugepaged_slab_exit(void);

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

static struct shrinker deferred_split_shrinker;

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

static int start_stop_khugepaged(void)
{
	int err = 0;
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
	return err;
}

static atomic_t huge_zero_refcount;
struct page *huge_zero_page __read_mostly;

struct page *get_huge_zero_page(void)
{
	struct page *zero_page;
retry:
	if (likely(atomic_inc_not_zero(&huge_zero_refcount)))
		return READ_ONCE(huge_zero_page);

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
		__free_pages(zero_page, compound_order(zero_page));
		goto retry;
	}

	/* We take additional reference here. It will be put back by shrinker */
	atomic_set(&huge_zero_refcount, 2);
	preempt_enable();
	return READ_ONCE(huge_zero_page);
}

void put_huge_zero_page(void)
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
		__free_pages(zero_page, compound_order(zero_page));
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

static ssize_t triple_flag_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count,
				 enum transparent_hugepage_flag enabled,
				 enum transparent_hugepage_flag deferred,
				 enum transparent_hugepage_flag req_madv)
{
	if (!memcmp("defer", buf,
		    min(sizeof("defer")-1, count))) {
		if (enabled == deferred)
			return -EINVAL;
		clear_bit(enabled, &transparent_hugepage_flags);
		clear_bit(req_madv, &transparent_hugepage_flags);
		set_bit(deferred, &transparent_hugepage_flags);
	} else if (!memcmp("always", buf,
		    min(sizeof("always")-1, count))) {
		clear_bit(deferred, &transparent_hugepage_flags);
		clear_bit(req_madv, &transparent_hugepage_flags);
		set_bit(enabled, &transparent_hugepage_flags);
	} else if (!memcmp("madvise", buf,
			   min(sizeof("madvise")-1, count))) {
		clear_bit(enabled, &transparent_hugepage_flags);
		clear_bit(deferred, &transparent_hugepage_flags);
		set_bit(req_madv, &transparent_hugepage_flags);
	} else if (!memcmp("never", buf,
			   min(sizeof("never")-1, count))) {
		clear_bit(enabled, &transparent_hugepage_flags);
		clear_bit(req_madv, &transparent_hugepage_flags);
		clear_bit(deferred, &transparent_hugepage_flags);
	} else
		return -EINVAL;

	return count;
}

static ssize_t enabled_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buf)
{
	if (test_bit(TRANSPARENT_HUGEPAGE_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "[always] madvise never\n");
	else if (test_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "always [madvise] never\n");
	else
		return sprintf(buf, "always madvise [never]\n");
}

static ssize_t enabled_store(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	ssize_t ret;

	ret = triple_flag_store(kobj, attr, buf, count,
				TRANSPARENT_HUGEPAGE_FLAG,
				TRANSPARENT_HUGEPAGE_FLAG,
				TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG);

	if (ret > 0) {
		int err;

		mutex_lock(&khugepaged_mutex);
		err = start_stop_khugepaged();
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
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "[always] defer madvise never\n");
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "always [defer] madvise never\n");
	else if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "always defer [madvise] never\n");
	else
		return sprintf(buf, "always defer madvise [never]\n");

}
static ssize_t defrag_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	return triple_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG,
				 TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG,
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

	err = kstrtoul(buf, 10, &msecs);
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

	err = kstrtoul(buf, 10, &msecs);
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

	err = kstrtoul(buf, 10, &max_ptes_none);
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
		pr_err("failed to create transparent hugepage kobject\n");
		return -ENOMEM;
	}

	err = sysfs_create_group(*hugepage_kobj, &hugepage_attr_group);
	if (err) {
		pr_err("failed to register transparent hugepage group\n");
		goto delete_obj;
	}

	err = sysfs_create_group(*hugepage_kobj, &khugepaged_attr_group);
	if (err) {
		pr_err("failed to register transparent hugepage group\n");
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

	khugepaged_pages_to_scan = HPAGE_PMD_NR * 8;
	khugepaged_max_ptes_none = HPAGE_PMD_NR - 1;
	/*
	 * hugepages can't be allocated by the buddy allocator
	 */
	MAYBE_BUILD_BUG_ON(HPAGE_PMD_ORDER >= MAX_ORDER);
	/*
	 * we use page->mapping and page->index in second tail page
	 * as list_head: assuming THP order >= 2
	 */
	MAYBE_BUILD_BUG_ON(HPAGE_PMD_ORDER < 2);

	err = hugepage_init_sysfs(&hugepage_kobj);
	if (err)
		goto err_sysfs;

	err = khugepaged_slab_init();
	if (err)
		goto err_slab;

	err = register_shrinker(&huge_zero_page_shrinker);
	if (err)
		goto err_hzp_shrinker;
	err = register_shrinker(&deferred_split_shrinker);
	if (err)
		goto err_split_shrinker;

	/*
	 * By default disable transparent hugepages on smaller systems,
	 * where the extra memory used could hurt more than TLB overhead
	 * is likely to save.  The admin can still enable it through /sys.
	 */
	if (totalram_pages < (512 << (20 - PAGE_SHIFT))) {
		transparent_hugepage_flags = 0;
		return 0;
	}

	err = start_stop_khugepaged();
	if (err)
		goto err_khugepaged;

	return 0;
err_khugepaged:
	unregister_shrinker(&deferred_split_shrinker);
err_split_shrinker:
	unregister_shrinker(&huge_zero_page_shrinker);
err_hzp_shrinker:
	khugepaged_slab_exit();
err_slab:
	hugepage_exit_sysfs(hugepage_kobj);
err_sysfs:
	return err;
}
subsys_initcall(hugepage_init);

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
		pr_warn("transparent_hugepage= cannot parse, ignored\n");
	return ret;
}
__setup("transparent_hugepage=", setup_transparent_hugepage);

pmd_t maybe_pmd_mkwrite(pmd_t pmd, struct vm_area_struct *vma)
{
	if (likely(vma->vm_flags & VM_WRITE))
		pmd = pmd_mkwrite(pmd);
	return pmd;
}

static inline pmd_t mk_huge_pmd(struct page *page, pgprot_t prot)
{
	pmd_t entry;
	entry = mk_pmd(page, prot);
	entry = pmd_mkhuge(entry);
	return entry;
}

static inline struct list_head *page_deferred_list(struct page *page)
{
	/*
	 * ->lru in the tail pages is occupied by compound_head.
	 * Let's use ->mapping + ->index in the second tail page as list_head.
	 */
	return (struct list_head *)&page[2].mapping;
}

void prep_transhuge_page(struct page *page)
{
	/*
	 * we use page->mapping and page->indexlru in second tail page
	 * as list_head: assuming THP order >= 2
	 */

	INIT_LIST_HEAD(page_deferred_list(page));
	set_compound_page_dtor(page, TRANSHUGE_PAGE_DTOR);
}

static int __do_huge_pmd_anonymous_page(struct mm_struct *mm,
					struct vm_area_struct *vma,
					unsigned long address, pmd_t *pmd,
					struct page *page, gfp_t gfp,
					unsigned int flags)
{
	struct mem_cgroup *memcg;
	pgtable_t pgtable;
	spinlock_t *ptl;
	unsigned long haddr = address & HPAGE_PMD_MASK;

	VM_BUG_ON_PAGE(!PageCompound(page), page);

	if (mem_cgroup_try_charge(page, mm, gfp, &memcg, true)) {
		put_page(page);
		count_vm_event(THP_FAULT_FALLBACK);
		return VM_FAULT_FALLBACK;
	}

	pgtable = pte_alloc_one(mm, haddr);
	if (unlikely(!pgtable)) {
		mem_cgroup_cancel_charge(page, memcg, true);
		put_page(page);
		return VM_FAULT_OOM;
	}

	clear_huge_page(page, haddr, HPAGE_PMD_NR);
	/*
	 * The memory barrier inside __SetPageUptodate makes sure that
	 * clear_huge_page writes become visible before the set_pmd_at()
	 * write.
	 */
	__SetPageUptodate(page);

	ptl = pmd_lock(mm, pmd);
	if (unlikely(!pmd_none(*pmd))) {
		spin_unlock(ptl);
		mem_cgroup_cancel_charge(page, memcg, true);
		put_page(page);
		pte_free(mm, pgtable);
	} else {
		pmd_t entry;

		/* Deliver the page fault to userland */
		if (userfaultfd_missing(vma)) {
			int ret;

			spin_unlock(ptl);
			mem_cgroup_cancel_charge(page, memcg, true);
			put_page(page);
			pte_free(mm, pgtable);
			ret = handle_userfault(vma, address, flags,
					       VM_UFFD_MISSING);
			VM_BUG_ON(ret & VM_FAULT_FALLBACK);
			return ret;
		}

		entry = mk_huge_pmd(page, vma->vm_page_prot);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		page_add_new_anon_rmap(page, vma, haddr, true);
		mem_cgroup_commit_charge(page, memcg, false, true);
		lru_cache_add_active_or_unevictable(page, vma);
		pgtable_trans_huge_deposit(mm, pmd, pgtable);
		set_pmd_at(mm, haddr, pmd, entry);
		add_mm_counter(mm, MM_ANONPAGES, HPAGE_PMD_NR);
		atomic_long_inc(&mm->nr_ptes);
		spin_unlock(ptl);
		count_vm_event(THP_FAULT_ALLOC);
	}

	return 0;
}

/*
 * If THP is set to always then directly reclaim/compact as necessary
 * If set to defer then do no reclaim and defer to khugepaged
 * If set to madvise and the VMA is flagged then directly reclaim/compact
 */
static inline gfp_t alloc_hugepage_direct_gfpmask(struct vm_area_struct *vma)
{
	gfp_t reclaim_flags = 0;

	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags) &&
	    (vma->vm_flags & VM_HUGEPAGE))
		reclaim_flags = __GFP_DIRECT_RECLAIM;
	else if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags))
		reclaim_flags = __GFP_KSWAPD_RECLAIM;
	else if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags))
		reclaim_flags = __GFP_DIRECT_RECLAIM;

	return GFP_TRANSHUGE | reclaim_flags;
}

/* Defrag for khugepaged will enter direct reclaim/compaction if necessary */
static inline gfp_t alloc_hugepage_khugepaged_gfpmask(void)
{
	return GFP_TRANSHUGE | (khugepaged_defrag() ? __GFP_DIRECT_RECLAIM : 0);
}

/* Caller must hold page table lock. */
static bool set_huge_zero_page(pgtable_t pgtable, struct mm_struct *mm,
		struct vm_area_struct *vma, unsigned long haddr, pmd_t *pmd,
		struct page *zero_page)
{
	pmd_t entry;
	if (!pmd_none(*pmd))
		return false;
	entry = mk_pmd(zero_page, vma->vm_page_prot);
	entry = pmd_mkhuge(entry);
	if (pgtable)
		pgtable_trans_huge_deposit(mm, pmd, pgtable);
	set_pmd_at(mm, haddr, pmd, entry);
	atomic_long_inc(&mm->nr_ptes);
	return true;
}

int do_huge_pmd_anonymous_page(struct mm_struct *mm, struct vm_area_struct *vma,
			       unsigned long address, pmd_t *pmd,
			       unsigned int flags)
{
	gfp_t gfp;
	struct page *page;
	unsigned long haddr = address & HPAGE_PMD_MASK;

	if (haddr < vma->vm_start || haddr + HPAGE_PMD_SIZE > vma->vm_end)
		return VM_FAULT_FALLBACK;
	if (unlikely(anon_vma_prepare(vma)))
		return VM_FAULT_OOM;
	if (unlikely(khugepaged_enter(vma, vma->vm_flags)))
		return VM_FAULT_OOM;
	if (!(flags & FAULT_FLAG_WRITE) && !mm_forbids_zeropage(mm) &&
			transparent_hugepage_use_zero_page()) {
		spinlock_t *ptl;
		pgtable_t pgtable;
		struct page *zero_page;
		bool set;
		int ret;
		pgtable = pte_alloc_one(mm, haddr);
		if (unlikely(!pgtable))
			return VM_FAULT_OOM;
		zero_page = get_huge_zero_page();
		if (unlikely(!zero_page)) {
			pte_free(mm, pgtable);
			count_vm_event(THP_FAULT_FALLBACK);
			return VM_FAULT_FALLBACK;
		}
		ptl = pmd_lock(mm, pmd);
		ret = 0;
		set = false;
		if (pmd_none(*pmd)) {
			if (userfaultfd_missing(vma)) {
				spin_unlock(ptl);
				ret = handle_userfault(vma, address, flags,
						       VM_UFFD_MISSING);
				VM_BUG_ON(ret & VM_FAULT_FALLBACK);
			} else {
				set_huge_zero_page(pgtable, mm, vma,
						   haddr, pmd,
						   zero_page);
				spin_unlock(ptl);
				set = true;
			}
		} else
			spin_unlock(ptl);
		if (!set) {
			pte_free(mm, pgtable);
			put_huge_zero_page();
		}
		return ret;
	}
	gfp = alloc_hugepage_direct_gfpmask(vma);
	page = alloc_hugepage_vma(gfp, vma, haddr, HPAGE_PMD_ORDER);
	if (unlikely(!page)) {
		count_vm_event(THP_FAULT_FALLBACK);
		return VM_FAULT_FALLBACK;
	}
	prep_transhuge_page(page);
	return __do_huge_pmd_anonymous_page(mm, vma, address, pmd, page, gfp,
					    flags);
}

static void insert_pfn_pmd(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd, pfn_t pfn, pgprot_t prot, bool write)
{
	struct mm_struct *mm = vma->vm_mm;
	pmd_t entry;
	spinlock_t *ptl;

	ptl = pmd_lock(mm, pmd);
	entry = pmd_mkhuge(pfn_t_pmd(pfn, prot));
	if (pfn_t_devmap(pfn))
		entry = pmd_mkdevmap(entry);
	if (write) {
		entry = pmd_mkyoung(pmd_mkdirty(entry));
		entry = maybe_pmd_mkwrite(entry, vma);
	}
	set_pmd_at(mm, addr, pmd, entry);
	update_mmu_cache_pmd(vma, addr, pmd);
	spin_unlock(ptl);
}

int vmf_insert_pfn_pmd(struct vm_area_struct *vma, unsigned long addr,
			pmd_t *pmd, pfn_t pfn, bool write)
{
	pgprot_t pgprot = vma->vm_page_prot;
	/*
	 * If we had pmd_special, we could avoid all these restrictions,
	 * but we need to be consistent with PTEs and architectures that
	 * can't support a 'special' bit.
	 */
	BUG_ON(!(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)));
	BUG_ON((vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)) ==
						(VM_PFNMAP|VM_MIXEDMAP));
	BUG_ON((vma->vm_flags & VM_PFNMAP) && is_cow_mapping(vma->vm_flags));
	BUG_ON(!pfn_t_devmap(pfn));

	if (addr < vma->vm_start || addr >= vma->vm_end)
		return VM_FAULT_SIGBUS;
	if (track_pfn_insert(vma, &pgprot, pfn))
		return VM_FAULT_SIGBUS;
	insert_pfn_pmd(vma, addr, pmd, pfn, pgprot, write);
	return VM_FAULT_NOPAGE;
}

static void touch_pmd(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd)
{
	pmd_t _pmd;

	/*
	 * We should set the dirty bit only for FOLL_WRITE but for now
	 * the dirty bit in the pmd is meaningless.  And if the dirty
	 * bit will become meaningful and we'll only set it with
	 * FOLL_WRITE, an atomic set_bit will be required on the pmd to
	 * set the young bit, instead of the current set_pmd_at.
	 */
	_pmd = pmd_mkyoung(pmd_mkdirty(*pmd));
	if (pmdp_set_access_flags(vma, addr & HPAGE_PMD_MASK,
				pmd, _pmd,  1))
		update_mmu_cache_pmd(vma, addr, pmd);
}

struct page *follow_devmap_pmd(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd, int flags)
{
	unsigned long pfn = pmd_pfn(*pmd);
	struct mm_struct *mm = vma->vm_mm;
	struct dev_pagemap *pgmap;
	struct page *page;

	assert_spin_locked(pmd_lockptr(mm, pmd));

	if (flags & FOLL_WRITE && !pmd_write(*pmd))
		return NULL;

	if (pmd_present(*pmd) && pmd_devmap(*pmd))
		/* pass */;
	else
		return NULL;

	if (flags & FOLL_TOUCH)
		touch_pmd(vma, addr, pmd);

	/*
	 * device mapped pages can only be returned if the
	 * caller will manage the page reference count.
	 */
	if (!(flags & FOLL_GET))
		return ERR_PTR(-EEXIST);

	pfn += (addr & ~PMD_MASK) >> PAGE_SHIFT;
	pgmap = get_dev_pagemap(pfn, NULL);
	if (!pgmap)
		return ERR_PTR(-EFAULT);
	page = pfn_to_page(pfn);
	get_page(page);
	put_dev_pagemap(pgmap);

	return page;
}

int copy_huge_pmd(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		  pmd_t *dst_pmd, pmd_t *src_pmd, unsigned long addr,
		  struct vm_area_struct *vma)
{
	spinlock_t *dst_ptl, *src_ptl;
	struct page *src_page;
	pmd_t pmd;
	pgtable_t pgtable = NULL;
	int ret;

	if (!vma_is_dax(vma)) {
		ret = -ENOMEM;
		pgtable = pte_alloc_one(dst_mm, addr);
		if (unlikely(!pgtable))
			goto out;
	}

	dst_ptl = pmd_lock(dst_mm, dst_pmd);
	src_ptl = pmd_lockptr(src_mm, src_pmd);
	spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);

	ret = -EAGAIN;
	pmd = *src_pmd;
	if (unlikely(!pmd_trans_huge(pmd) && !pmd_devmap(pmd))) {
		pte_free(dst_mm, pgtable);
		goto out_unlock;
	}
	/*
	 * When page table lock is held, the huge zero pmd should not be
	 * under splitting since we don't split the page itself, only pmd to
	 * a page table.
	 */
	if (is_huge_zero_pmd(pmd)) {
		struct page *zero_page;
		/*
		 * get_huge_zero_page() will never allocate a new page here,
		 * since we already have a zero page to copy. It just takes a
		 * reference.
		 */
		zero_page = get_huge_zero_page();
		set_huge_zero_page(pgtable, dst_mm, vma, addr, dst_pmd,
				zero_page);
		ret = 0;
		goto out_unlock;
	}

	if (!vma_is_dax(vma)) {
		/* thp accounting separate from pmd_devmap accounting */
		src_page = pmd_page(pmd);
		VM_BUG_ON_PAGE(!PageHead(src_page), src_page);
		get_page(src_page);
		page_dup_rmap(src_page, true);
		add_mm_counter(dst_mm, MM_ANONPAGES, HPAGE_PMD_NR);
		atomic_long_inc(&dst_mm->nr_ptes);
		pgtable_trans_huge_deposit(dst_mm, dst_pmd, pgtable);
	}

	pmdp_set_wrprotect(src_mm, addr, src_pmd);
	pmd = pmd_mkold(pmd_wrprotect(pmd));
	set_pmd_at(dst_mm, addr, dst_pmd, pmd);

	ret = 0;
out_unlock:
	spin_unlock(src_ptl);
	spin_unlock(dst_ptl);
out:
	return ret;
}

void huge_pmd_set_accessed(struct mm_struct *mm,
			   struct vm_area_struct *vma,
			   unsigned long address,
			   pmd_t *pmd, pmd_t orig_pmd,
			   int dirty)
{
	spinlock_t *ptl;
	pmd_t entry;
	unsigned long haddr;

	ptl = pmd_lock(mm, pmd);
	if (unlikely(!pmd_same(*pmd, orig_pmd)))
		goto unlock;

	entry = pmd_mkyoung(orig_pmd);
	haddr = address & HPAGE_PMD_MASK;
	if (pmdp_set_access_flags(vma, haddr, pmd, entry, dirty))
		update_mmu_cache_pmd(vma, address, pmd);

unlock:
	spin_unlock(ptl);
}

static int do_huge_pmd_wp_page_fallback(struct mm_struct *mm,
					struct vm_area_struct *vma,
					unsigned long address,
					pmd_t *pmd, pmd_t orig_pmd,
					struct page *page,
					unsigned long haddr)
{
	struct mem_cgroup *memcg;
	spinlock_t *ptl;
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
			     mem_cgroup_try_charge(pages[i], mm, GFP_KERNEL,
						   &memcg, false))) {
			if (pages[i])
				put_page(pages[i]);
			while (--i >= 0) {
				memcg = (void *)page_private(pages[i]);
				set_page_private(pages[i], 0);
				mem_cgroup_cancel_charge(pages[i], memcg,
						false);
				put_page(pages[i]);
			}
			kfree(pages);
			ret |= VM_FAULT_OOM;
			goto out;
		}
		set_page_private(pages[i], (unsigned long)memcg);
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

	ptl = pmd_lock(mm, pmd);
	if (unlikely(!pmd_same(*pmd, orig_pmd)))
		goto out_free_pages;
	VM_BUG_ON_PAGE(!PageHead(page), page);

	pmdp_huge_clear_flush_notify(vma, haddr, pmd);
	/* leave pmd empty until pte is filled */

	pgtable = pgtable_trans_huge_withdraw(mm, pmd);
	pmd_populate(mm, &_pmd, pgtable);

	for (i = 0; i < HPAGE_PMD_NR; i++, haddr += PAGE_SIZE) {
		pte_t *pte, entry;
		entry = mk_pte(pages[i], vma->vm_page_prot);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		memcg = (void *)page_private(pages[i]);
		set_page_private(pages[i], 0);
		page_add_new_anon_rmap(pages[i], vma, haddr, false);
		mem_cgroup_commit_charge(pages[i], memcg, false, false);
		lru_cache_add_active_or_unevictable(pages[i], vma);
		pte = pte_offset_map(&_pmd, haddr);
		VM_BUG_ON(!pte_none(*pte));
		set_pte_at(mm, haddr, pte, entry);
		pte_unmap(pte);
	}
	kfree(pages);

	smp_wmb(); /* make pte visible before pmd */
	pmd_populate(mm, pmd, pgtable);
	page_remove_rmap(page, true);
	spin_unlock(ptl);

	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);

	ret |= VM_FAULT_WRITE;
	put_page(page);

out:
	return ret;

out_free_pages:
	spin_unlock(ptl);
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
	for (i = 0; i < HPAGE_PMD_NR; i++) {
		memcg = (void *)page_private(pages[i]);
		set_page_private(pages[i], 0);
		mem_cgroup_cancel_charge(pages[i], memcg, false);
		put_page(pages[i]);
	}
	kfree(pages);
	goto out;
}

int do_huge_pmd_wp_page(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, pmd_t *pmd, pmd_t orig_pmd)
{
	spinlock_t *ptl;
	int ret = 0;
	struct page *page = NULL, *new_page;
	struct mem_cgroup *memcg;
	unsigned long haddr;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */
	gfp_t huge_gfp;			/* for allocation and charge */

	ptl = pmd_lockptr(mm, pmd);
	VM_BUG_ON_VMA(!vma->anon_vma, vma);
	haddr = address & HPAGE_PMD_MASK;
	if (is_huge_zero_pmd(orig_pmd))
		goto alloc;
	spin_lock(ptl);
	if (unlikely(!pmd_same(*pmd, orig_pmd)))
		goto out_unlock;

	page = pmd_page(orig_pmd);
	VM_BUG_ON_PAGE(!PageCompound(page) || !PageHead(page), page);
	/*
	 * We can only reuse the page if nobody else maps the huge page or it's
	 * part.
	 */
	if (page_trans_huge_mapcount(page, NULL) == 1) {
		pmd_t entry;
		entry = pmd_mkyoung(orig_pmd);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		if (pmdp_set_access_flags(vma, haddr, pmd, entry,  1))
			update_mmu_cache_pmd(vma, address, pmd);
		ret |= VM_FAULT_WRITE;
		goto out_unlock;
	}
	get_page(page);
	spin_unlock(ptl);
alloc:
	if (transparent_hugepage_enabled(vma) &&
	    !transparent_hugepage_debug_cow()) {
		huge_gfp = alloc_hugepage_direct_gfpmask(vma);
		new_page = alloc_hugepage_vma(huge_gfp, vma, haddr, HPAGE_PMD_ORDER);
	} else
		new_page = NULL;

	if (likely(new_page)) {
		prep_transhuge_page(new_page);
	} else {
		if (!page) {
			split_huge_pmd(vma, pmd, address);
			ret |= VM_FAULT_FALLBACK;
		} else {
			ret = do_huge_pmd_wp_page_fallback(mm, vma, address,
					pmd, orig_pmd, page, haddr);
			if (ret & VM_FAULT_OOM) {
				split_huge_pmd(vma, pmd, address);
				ret |= VM_FAULT_FALLBACK;
			}
			put_page(page);
		}
		count_vm_event(THP_FAULT_FALLBACK);
		goto out;
	}

	if (unlikely(mem_cgroup_try_charge(new_page, mm, huge_gfp, &memcg,
					   true))) {
		put_page(new_page);
		if (page) {
			split_huge_pmd(vma, pmd, address);
			put_page(page);
		} else
			split_huge_pmd(vma, pmd, address);
		ret |= VM_FAULT_FALLBACK;
		count_vm_event(THP_FAULT_FALLBACK);
		goto out;
	}

	count_vm_event(THP_FAULT_ALLOC);

	if (!page)
		clear_huge_page(new_page, haddr, HPAGE_PMD_NR);
	else
		copy_user_huge_page(new_page, page, haddr, vma, HPAGE_PMD_NR);
	__SetPageUptodate(new_page);

	mmun_start = haddr;
	mmun_end   = haddr + HPAGE_PMD_SIZE;
	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);

	spin_lock(ptl);
	if (page)
		put_page(page);
	if (unlikely(!pmd_same(*pmd, orig_pmd))) {
		spin_unlock(ptl);
		mem_cgroup_cancel_charge(new_page, memcg, true);
		put_page(new_page);
		goto out_mn;
	} else {
		pmd_t entry;
		entry = mk_huge_pmd(new_page, vma->vm_page_prot);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		pmdp_huge_clear_flush_notify(vma, haddr, pmd);
		page_add_new_anon_rmap(new_page, vma, haddr, true);
		mem_cgroup_commit_charge(new_page, memcg, false, true);
		lru_cache_add_active_or_unevictable(new_page, vma);
		set_pmd_at(mm, haddr, pmd, entry);
		update_mmu_cache_pmd(vma, address, pmd);
		if (!page) {
			add_mm_counter(mm, MM_ANONPAGES, HPAGE_PMD_NR);
			put_huge_zero_page();
		} else {
			VM_BUG_ON_PAGE(!PageHead(page), page);
			page_remove_rmap(page, true);
			put_page(page);
		}
		ret |= VM_FAULT_WRITE;
	}
	spin_unlock(ptl);
out_mn:
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
out:
	return ret;
out_unlock:
	spin_unlock(ptl);
	return ret;
}

struct page *follow_trans_huge_pmd(struct vm_area_struct *vma,
				   unsigned long addr,
				   pmd_t *pmd,
				   unsigned int flags)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page *page = NULL;

	assert_spin_locked(pmd_lockptr(mm, pmd));

	if (flags & FOLL_WRITE && !pmd_write(*pmd))
		goto out;

	/* Avoid dumping huge zero page */
	if ((flags & FOLL_DUMP) && is_huge_zero_pmd(*pmd))
		return ERR_PTR(-EFAULT);

	/* Full NUMA hinting faults to serialise migration in fault paths */
	if ((flags & FOLL_NUMA) && pmd_protnone(*pmd))
		goto out;

	page = pmd_page(*pmd);
	VM_BUG_ON_PAGE(!PageHead(page), page);
	if (flags & FOLL_TOUCH)
		touch_pmd(vma, addr, pmd);
	if ((flags & FOLL_MLOCK) && (vma->vm_flags & VM_LOCKED)) {
		/*
		 * We don't mlock() pte-mapped THPs. This way we can avoid
		 * leaking mlocked pages into non-VM_LOCKED VMAs.
		 *
		 * In most cases the pmd is the only mapping of the page as we
		 * break COW for the mlock() -- see gup_flags |= FOLL_WRITE for
		 * writable private mappings in populate_vma_page_range().
		 *
		 * The only scenario when we have the page shared here is if we
		 * mlocking read-only mapping shared over fork(). We skip
		 * mlocking such pages.
		 */
		if (compound_mapcount(page) == 1 && !PageDoubleMap(page) &&
				page->mapping && trylock_page(page)) {
			lru_add_drain();
			if (page->mapping)
				mlock_vma_page(page);
			unlock_page(page);
		}
	}
	page += (addr & ~HPAGE_PMD_MASK) >> PAGE_SHIFT;
	VM_BUG_ON_PAGE(!PageCompound(page), page);
	if (flags & FOLL_GET)
		get_page(page);

out:
	return page;
}

/* NUMA hinting page fault entry point for trans huge pmds */
int do_huge_pmd_numa_page(struct mm_struct *mm, struct vm_area_struct *vma,
				unsigned long addr, pmd_t pmd, pmd_t *pmdp)
{
	spinlock_t *ptl;
	struct anon_vma *anon_vma = NULL;
	struct page *page;
	unsigned long haddr = addr & HPAGE_PMD_MASK;
	int page_nid = -1, this_nid = numa_node_id();
	int target_nid, last_cpupid = -1;
	bool page_locked;
	bool migrated = false;
	bool was_writable;
	int flags = 0;

	/* A PROT_NONE fault should not end up here */
	BUG_ON(!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE)));

	ptl = pmd_lock(mm, pmdp);
	if (unlikely(!pmd_same(pmd, *pmdp)))
		goto out_unlock;

	/*
	 * If there are potential migrations, wait for completion and retry
	 * without disrupting NUMA hinting information. Do not relock and
	 * check_same as the page may no longer be mapped.
	 */
	if (unlikely(pmd_trans_migrating(*pmdp))) {
		page = pmd_page(*pmdp);
		spin_unlock(ptl);
		wait_on_page_locked(page);
		goto out;
	}

	page = pmd_page(pmd);
	BUG_ON(is_huge_zero_page(page));
	page_nid = page_to_nid(page);
	last_cpupid = page_cpupid_last(page);
	count_vm_numa_event(NUMA_HINT_FAULTS);
	if (page_nid == this_nid) {
		count_vm_numa_event(NUMA_HINT_FAULTS_LOCAL);
		flags |= TNF_FAULT_LOCAL;
	}

	/* See similar comment in do_numa_page for explanation */
	if (!(vma->vm_flags & VM_WRITE))
		flags |= TNF_NO_GROUP;

	/*
	 * Acquire the page lock to serialise THP migrations but avoid dropping
	 * page_table_lock if at all possible
	 */
	page_locked = trylock_page(page);
	target_nid = mpol_misplaced(page, vma, haddr);
	if (target_nid == -1) {
		/* If the page was locked, there are no parallel migrations */
		if (page_locked)
			goto clear_pmdnuma;
	}

	/* Migration could have started since the pmd_trans_migrating check */
	if (!page_locked) {
		spin_unlock(ptl);
		wait_on_page_locked(page);
		page_nid = -1;
		goto out;
	}

	/*
	 * Page is misplaced. Page lock serialises migrations. Acquire anon_vma
	 * to serialises splits
	 */
	get_page(page);
	spin_unlock(ptl);
	anon_vma = page_lock_anon_vma_read(page);

	/* Confirm the PMD did not change while page_table_lock was released */
	spin_lock(ptl);
	if (unlikely(!pmd_same(pmd, *pmdp))) {
		unlock_page(page);
		put_page(page);
		page_nid = -1;
		goto out_unlock;
	}

	/* Bail if we fail to protect against THP splits for any reason */
	if (unlikely(!anon_vma)) {
		put_page(page);
		page_nid = -1;
		goto clear_pmdnuma;
	}

	/*
	 * Migrate the THP to the requested node, returns with page unlocked
	 * and access rights restored.
	 */
	spin_unlock(ptl);
	migrated = migrate_misplaced_transhuge_page(mm, vma,
				pmdp, pmd, addr, page, target_nid);
	if (migrated) {
		flags |= TNF_MIGRATED;
		page_nid = target_nid;
	} else
		flags |= TNF_MIGRATE_FAIL;

	goto out;
clear_pmdnuma:
	BUG_ON(!PageLocked(page));
	was_writable = pmd_write(pmd);
	pmd = pmd_modify(pmd, vma->vm_page_prot);
	pmd = pmd_mkyoung(pmd);
	if (was_writable)
		pmd = pmd_mkwrite(pmd);
	set_pmd_at(mm, haddr, pmdp, pmd);
	update_mmu_cache_pmd(vma, addr, pmdp);
	unlock_page(page);
out_unlock:
	spin_unlock(ptl);

out:
	if (anon_vma)
		page_unlock_anon_vma_read(anon_vma);

	if (page_nid != -1)
		task_numa_fault(last_cpupid, page_nid, HPAGE_PMD_NR, flags);

	return 0;
}

int madvise_free_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		pmd_t *pmd, unsigned long addr, unsigned long next)

{
	spinlock_t *ptl;
	pmd_t orig_pmd;
	struct page *page;
	struct mm_struct *mm = tlb->mm;
	int ret = 0;

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (!ptl)
		goto out_unlocked;

	orig_pmd = *pmd;
	if (is_huge_zero_pmd(orig_pmd)) {
		ret = 1;
		goto out;
	}

	page = pmd_page(orig_pmd);
	/*
	 * If other processes are mapping this page, we couldn't discard
	 * the page unless they all do MADV_FREE so let's skip the page.
	 */
	if (page_mapcount(page) != 1)
		goto out;

	if (!trylock_page(page))
		goto out;

	/*
	 * If user want to discard part-pages of THP, split it so MADV_FREE
	 * will deactivate only them.
	 */
	if (next - addr != HPAGE_PMD_SIZE) {
		get_page(page);
		spin_unlock(ptl);
		if (split_huge_page(page)) {
			put_page(page);
			unlock_page(page);
			goto out_unlocked;
		}
		put_page(page);
		unlock_page(page);
		ret = 1;
		goto out_unlocked;
	}

	if (PageDirty(page))
		ClearPageDirty(page);
	unlock_page(page);

	if (PageActive(page))
		deactivate_page(page);

	if (pmd_young(orig_pmd) || pmd_dirty(orig_pmd)) {
		orig_pmd = pmdp_huge_get_and_clear_full(tlb->mm, addr, pmd,
			tlb->fullmm);
		orig_pmd = pmd_mkold(orig_pmd);
		orig_pmd = pmd_mkclean(orig_pmd);

		set_pmd_at(mm, addr, pmd, orig_pmd);
		tlb_remove_pmd_tlb_entry(tlb, pmd, addr);
	}
	ret = 1;
out:
	spin_unlock(ptl);
out_unlocked:
	return ret;
}

int zap_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		 pmd_t *pmd, unsigned long addr)
{
	pmd_t orig_pmd;
	spinlock_t *ptl;

	ptl = __pmd_trans_huge_lock(pmd, vma);
	if (!ptl)
		return 0;
	/*
	 * For architectures like ppc64 we look at deposited pgtable
	 * when calling pmdp_huge_get_and_clear. So do the
	 * pgtable_trans_huge_withdraw after finishing pmdp related
	 * operations.
	 */
	orig_pmd = pmdp_huge_get_and_clear_full(tlb->mm, addr, pmd,
			tlb->fullmm);
	tlb_remove_pmd_tlb_entry(tlb, pmd, addr);
	if (vma_is_dax(vma)) {
		spin_unlock(ptl);
		if (is_huge_zero_pmd(orig_pmd))
			tlb_remove_page(tlb, pmd_page(orig_pmd));
	} else if (is_huge_zero_pmd(orig_pmd)) {
		pte_free(tlb->mm, pgtable_trans_huge_withdraw(tlb->mm, pmd));
		atomic_long_dec(&tlb->mm->nr_ptes);
		spin_unlock(ptl);
		tlb_remove_page(tlb, pmd_page(orig_pmd));
	} else {
		struct page *page = pmd_page(orig_pmd);
		page_remove_rmap(page, true);
		VM_BUG_ON_PAGE(page_mapcount(page) < 0, page);
		add_mm_counter(tlb->mm, MM_ANONPAGES, -HPAGE_PMD_NR);
		VM_BUG_ON_PAGE(!PageHead(page), page);
		pte_free(tlb->mm, pgtable_trans_huge_withdraw(tlb->mm, pmd));
		atomic_long_dec(&tlb->mm->nr_ptes);
		spin_unlock(ptl);
		tlb_remove_page(tlb, page);
	}
	return 1;
}

bool move_huge_pmd(struct vm_area_struct *vma, struct vm_area_struct *new_vma,
		  unsigned long old_addr,
		  unsigned long new_addr, unsigned long old_end,
		  pmd_t *old_pmd, pmd_t *new_pmd)
{
	spinlock_t *old_ptl, *new_ptl;
	pmd_t pmd;

	struct mm_struct *mm = vma->vm_mm;

	if ((old_addr & ~HPAGE_PMD_MASK) ||
	    (new_addr & ~HPAGE_PMD_MASK) ||
	    old_end - old_addr < HPAGE_PMD_SIZE ||
	    (new_vma->vm_flags & VM_NOHUGEPAGE))
		return false;

	/*
	 * The destination pmd shouldn't be established, free_pgtables()
	 * should have release it.
	 */
	if (WARN_ON(!pmd_none(*new_pmd))) {
		VM_BUG_ON(pmd_trans_huge(*new_pmd));
		return false;
	}

	/*
	 * We don't have to worry about the ordering of src and dst
	 * ptlocks because exclusive mmap_sem prevents deadlock.
	 */
	old_ptl = __pmd_trans_huge_lock(old_pmd, vma);
	if (old_ptl) {
		new_ptl = pmd_lockptr(mm, new_pmd);
		if (new_ptl != old_ptl)
			spin_lock_nested(new_ptl, SINGLE_DEPTH_NESTING);
		pmd = pmdp_huge_get_and_clear(mm, old_addr, old_pmd);
		VM_BUG_ON(!pmd_none(*new_pmd));

		if (pmd_move_must_withdraw(new_ptl, old_ptl) &&
				vma_is_anonymous(vma)) {
			pgtable_t pgtable;
			pgtable = pgtable_trans_huge_withdraw(mm, old_pmd);
			pgtable_trans_huge_deposit(mm, new_pmd, pgtable);
		}
		set_pmd_at(mm, new_addr, new_pmd, pmd_mksoft_dirty(pmd));
		if (new_ptl != old_ptl)
			spin_unlock(new_ptl);
		spin_unlock(old_ptl);
		return true;
	}
	return false;
}

/*
 * Returns
 *  - 0 if PMD could not be locked
 *  - 1 if PMD was locked but protections unchange and TLB flush unnecessary
 *  - HPAGE_PMD_NR is protections changed and TLB flush necessary
 */
int change_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long addr, pgprot_t newprot, int prot_numa)
{
	struct mm_struct *mm = vma->vm_mm;
	spinlock_t *ptl;
	int ret = 0;

	ptl = __pmd_trans_huge_lock(pmd, vma);
	if (ptl) {
		pmd_t entry;
		bool preserve_write = prot_numa && pmd_write(*pmd);
		ret = 1;

		/*
		 * Avoid trapping faults against the zero page. The read-only
		 * data is likely to be read-cached on the local CPU and
		 * local/remote hits to the zero page are not interesting.
		 */
		if (prot_numa && is_huge_zero_pmd(*pmd)) {
			spin_unlock(ptl);
			return ret;
		}

		if (!prot_numa || !pmd_protnone(*pmd)) {
			entry = pmdp_huge_get_and_clear_notify(mm, addr, pmd);
			entry = pmd_modify(entry, newprot);
			if (preserve_write)
				entry = pmd_mkwrite(entry);
			ret = HPAGE_PMD_NR;
			set_pmd_at(mm, addr, pmd, entry);
			BUG_ON(!preserve_write && pmd_write(entry));
		}
		spin_unlock(ptl);
	}

	return ret;
}

/*
 * Returns true if a given pmd maps a thp, false otherwise.
 *
 * Note that if it returns true, this routine returns without unlocking page
 * table lock. So callers must unlock it.
 */
spinlock_t *__pmd_trans_huge_lock(pmd_t *pmd, struct vm_area_struct *vma)
{
	spinlock_t *ptl;
	ptl = pmd_lock(vma->vm_mm, pmd);
	if (likely(pmd_trans_huge(*pmd) || pmd_devmap(*pmd)))
		return ptl;
	spin_unlock(ptl);
	return NULL;
}

#define VM_NO_THP (VM_SPECIAL | VM_HUGETLB | VM_SHARED | VM_MAYSHARE)

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
		/*
		 * Be somewhat over-protective like KSM for now!
		 */
		if (*vm_flags & VM_NO_THP)
			return -EINVAL;
		*vm_flags &= ~VM_NOHUGEPAGE;
		*vm_flags |= VM_HUGEPAGE;
		/*
		 * If the vma become good for khugepaged to scan,
		 * register it here without waiting a page fault that
		 * may not happen any time soon.
		 */
		if (unlikely(khugepaged_enter_vma_merge(vma, *vm_flags)))
			return -ENOMEM;
		break;
	case MADV_NOHUGEPAGE:
		/*
		 * Be somewhat over-protective like KSM for now!
		 */
		if (*vm_flags & VM_NO_THP)
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

static void __init khugepaged_slab_exit(void)
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

	atomic_inc(&mm->mm_count);
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
	if (vma->vm_ops || (vm_flags & VM_NO_THP))
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
	/* 0 stands for page_is_file_cache(page) == false */
	dec_zone_page_state(page, NR_ISOLATED_ANON + 0);
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
	int none_or_zero = 0, result = 0;
	bool referenced = false, writable = false;

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

		VM_BUG_ON_PAGE(PageCompound(page), page);
		VM_BUG_ON_PAGE(!PageAnon(page), page);
		VM_BUG_ON_PAGE(!PageSwapBacked(page), page);

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
		if (page_count(page) != 1 + !!PageSwapCache(page)) {
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
		/* 0 stands for page_is_file_cache(page) == false */
		inc_zone_page_state(page, NR_ISOLATED_ANON + 0);
		VM_BUG_ON_PAGE(!PageLocked(page), page);
		VM_BUG_ON_PAGE(PageLRU(page), page);

		/* If there is no mapped pte young don't collapse the page */
		if (pte_young(pteval) ||
		    page_is_young(page) || PageReferenced(page) ||
		    mmu_notifier_test_young(vma->vm_mm, address))
			referenced = true;
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
	for (_pte = pte; _pte < pte+HPAGE_PMD_NR; _pte++) {
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

		address += PAGE_SIZE;
		page++;
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
	 * If zone_reclaim_mode is disabled, then no extra effort is made to
	 * allocate memory locally.
	 */
	if (!zone_reclaim_mode)
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
khugepaged_alloc_page(struct page **hpage, gfp_t gfp, struct mm_struct *mm,
		       unsigned long address, int node)
{
	VM_BUG_ON_PAGE(*hpage, *hpage);

	/*
	 * Before allocating the hugepage, release the mmap_sem read lock.
	 * The allocation can take potentially a long time if it involves
	 * sync compaction, and we do not need to hold the mmap_sem during
	 * that. We will recheck the vma after taking it again in write mode.
	 */
	up_read(&mm->mmap_sem);

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
khugepaged_alloc_page(struct page **hpage, gfp_t gfp, struct mm_struct *mm,
		       unsigned long address, int node)
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
	return !(vma->vm_flags & VM_NO_THP);
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
	spinlock_t *pmd_ptl, *pte_ptl;
	int isolated = 0, result = 0;
	unsigned long hstart, hend;
	struct mem_cgroup *memcg;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */
	gfp_t gfp;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);

	/* Only allocate from the target node */
	gfp = alloc_hugepage_khugepaged_gfpmask() | __GFP_OTHER_NODE | __GFP_THISNODE;

	/* release the mmap_sem read lock. */
	new_page = khugepaged_alloc_page(hpage, gfp, mm, address, node);
	if (!new_page) {
		result = SCAN_ALLOC_HUGE_PAGE_FAIL;
		goto out_nolock;
	}

	if (unlikely(mem_cgroup_try_charge(new_page, mm, gfp, &memcg, true))) {
		result = SCAN_CGROUP_CHARGE_FAIL;
		goto out_nolock;
	}

	/*
	 * Prevent all access to pagetables with the exception of
	 * gup_fast later hanlded by the ptep_clear_flush and the VM
	 * handled by the anon_vma lock + PG_lock.
	 */
	down_write(&mm->mmap_sem);
	if (unlikely(khugepaged_test_exit(mm))) {
		result = SCAN_ANY_PROCESS;
		goto out;
	}

	vma = find_vma(mm, address);
	if (!vma) {
		result = SCAN_VMA_NULL;
		goto out;
	}
	hstart = (vma->vm_start + ~HPAGE_PMD_MASK) & HPAGE_PMD_MASK;
	hend = vma->vm_end & HPAGE_PMD_MASK;
	if (address < hstart || address + HPAGE_PMD_SIZE > hend) {
		result = SCAN_ADDRESS_RANGE;
		goto out;
	}
	if (!hugepage_vma_check(vma)) {
		result = SCAN_VMA_CHECK;
		goto out;
	}
	pmd = mm_find_pmd(mm, address);
	if (!pmd) {
		result = SCAN_PMD_NULL;
		goto out;
	}

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
	trace_mm_collapse_huge_page(mm, isolated, result);
	return;

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
	int ret = 0, none_or_zero = 0, result = 0;
	struct page *page = NULL;
	unsigned long _address;
	spinlock_t *ptl;
	int node = NUMA_NO_NODE;
	bool writable = false, referenced = false;

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
		if (page_count(page) != 1 + !!PageSwapCache(page)) {
			result = SCAN_PAGE_COUNT;
			goto out_unmap;
		}
		if (pte_young(pteval) ||
		    page_is_young(page) || PageReferenced(page) ||
		    mmu_notifier_test_young(vma->vm_mm, address))
			referenced = true;
	}
	if (writable) {
		if (referenced) {
			result = SCAN_SUCCEED;
			ret = 1;
		} else {
			result = SCAN_NO_REFERENCED_PAGE;
		}
	} else {
		result = SCAN_PAGE_RO;
	}
out_unmap:
	pte_unmap_unlock(pte, ptl);
	if (ret) {
		node = khugepaged_find_target_node();
		/* collapse_huge_page will return with the mmap_sem released */
		collapse_huge_page(mm, address, hpage, vma, node);
	}
out:
	trace_mm_khugepaged_scan_pmd(mm, page, writable, referenced,
				     none_or_zero, result);
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

static void khugepaged_wait_work(void)
{
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

static void __split_huge_zero_page_pmd(struct vm_area_struct *vma,
		unsigned long haddr, pmd_t *pmd)
{
	struct mm_struct *mm = vma->vm_mm;
	pgtable_t pgtable;
	pmd_t _pmd;
	int i;

	/* leave pmd empty until pte is filled */
	pmdp_huge_clear_flush_notify(vma, haddr, pmd);

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

static void __split_huge_pmd_locked(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long haddr, bool freeze)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page *page;
	pgtable_t pgtable;
	pmd_t _pmd;
	bool young, write, dirty;
	unsigned long addr;
	int i;

	VM_BUG_ON(haddr & ~HPAGE_PMD_MASK);
	VM_BUG_ON_VMA(vma->vm_start > haddr, vma);
	VM_BUG_ON_VMA(vma->vm_end < haddr + HPAGE_PMD_SIZE, vma);
	VM_BUG_ON(!pmd_trans_huge(*pmd) && !pmd_devmap(*pmd));

	count_vm_event(THP_SPLIT_PMD);

	if (vma_is_dax(vma)) {
		pmd_t _pmd = pmdp_huge_clear_flush_notify(vma, haddr, pmd);
		if (is_huge_zero_pmd(_pmd))
			put_huge_zero_page();
		return;
	} else if (is_huge_zero_pmd(*pmd)) {
		return __split_huge_zero_page_pmd(vma, haddr, pmd);
	}

	page = pmd_page(*pmd);
	VM_BUG_ON_PAGE(!page_count(page), page);
	page_ref_add(page, HPAGE_PMD_NR - 1);
	write = pmd_write(*pmd);
	young = pmd_young(*pmd);
	dirty = pmd_dirty(*pmd);

	pmdp_huge_split_prepare(vma, haddr, pmd);
	pgtable = pgtable_trans_huge_withdraw(mm, pmd);
	pmd_populate(mm, &_pmd, pgtable);

	for (i = 0, addr = haddr; i < HPAGE_PMD_NR; i++, addr += PAGE_SIZE) {
		pte_t entry, *pte;
		/*
		 * Note that NUMA hinting access restrictions are not
		 * transferred to avoid any possibility of altering
		 * permissions across VMAs.
		 */
		if (freeze) {
			swp_entry_t swp_entry;
			swp_entry = make_migration_entry(page + i, write);
			entry = swp_entry_to_pte(swp_entry);
		} else {
			entry = mk_pte(page + i, vma->vm_page_prot);
			entry = maybe_mkwrite(entry, vma);
			if (!write)
				entry = pte_wrprotect(entry);
			if (!young)
				entry = pte_mkold(entry);
		}
		if (dirty)
			SetPageDirty(page + i);
		pte = pte_offset_map(&_pmd, addr);
		BUG_ON(!pte_none(*pte));
		set_pte_at(mm, addr, pte, entry);
		atomic_inc(&page[i]._mapcount);
		pte_unmap(pte);
	}

	/*
	 * Set PG_double_map before dropping compound_mapcount to avoid
	 * false-negative page_mapped().
	 */
	if (compound_mapcount(page) > 1 && !TestSetPageDoubleMap(page)) {
		for (i = 0; i < HPAGE_PMD_NR; i++)
			atomic_inc(&page[i]._mapcount);
	}

	if (atomic_add_negative(-1, compound_mapcount_ptr(page))) {
		/* Last compound_mapcount is gone. */
		__dec_zone_page_state(page, NR_ANON_TRANSPARENT_HUGEPAGES);
		if (TestClearPageDoubleMap(page)) {
			/* No need in mapcount reference anymore */
			for (i = 0; i < HPAGE_PMD_NR; i++)
				atomic_dec(&page[i]._mapcount);
		}
	}

	smp_wmb(); /* make pte visible before pmd */
	/*
	 * Up to this point the pmd is present and huge and userland has the
	 * whole access to the hugepage during the split (which happens in
	 * place). If we overwrite the pmd with the not-huge version pointing
	 * to the pte here (which of course we could if all CPUs were bug
	 * free), userland could trigger a small page size TLB miss on the
	 * small sized TLB while the hugepage TLB entry is still established in
	 * the huge TLB. Some CPU doesn't like that.
	 * See http://support.amd.com/us/Processor_TechDocs/41322.pdf, Erratum
	 * 383 on page 93. Intel should be safe but is also warns that it's
	 * only safe if the permission and cache attributes of the two entries
	 * loaded in the two TLB is identical (which should be the case here).
	 * But it is generally safer to never allow small and huge TLB entries
	 * for the same virtual address to be loaded simultaneously. So instead
	 * of doing "pmd_populate(); flush_pmd_tlb_range();" we first mark the
	 * current pmd notpresent (atomically because here the pmd_trans_huge
	 * and pmd_trans_splitting must remain set at all times on the pmd
	 * until the split is complete for this pmd), then we flush the SMP TLB
	 * and finally we write the non-huge version of the pmd entry with
	 * pmd_populate.
	 */
	pmdp_invalidate(vma, haddr, pmd);
	pmd_populate(mm, pmd, pgtable);

	if (freeze) {
		for (i = 0; i < HPAGE_PMD_NR; i++) {
			page_remove_rmap(page + i, false);
			put_page(page + i);
		}
	}
}

void __split_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long address, bool freeze)
{
	spinlock_t *ptl;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long haddr = address & HPAGE_PMD_MASK;

	mmu_notifier_invalidate_range_start(mm, haddr, haddr + HPAGE_PMD_SIZE);
	ptl = pmd_lock(mm, pmd);
	if (pmd_trans_huge(*pmd)) {
		struct page *page = pmd_page(*pmd);
		if (PageMlocked(page))
			clear_page_mlock(page);
	} else if (!pmd_devmap(*pmd))
		goto out;
	__split_huge_pmd_locked(vma, pmd, haddr, freeze);
out:
	spin_unlock(ptl);
	mmu_notifier_invalidate_range_end(mm, haddr, haddr + HPAGE_PMD_SIZE);
}

void split_huge_pmd_address(struct vm_area_struct *vma, unsigned long address,
		bool freeze, struct page *page)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(vma->vm_mm, address);
	if (!pgd_present(*pgd))
		return;

	pud = pud_offset(pgd, address);
	if (!pud_present(*pud))
		return;

	pmd = pmd_offset(pud, address);
	if (!pmd_present(*pmd) || (!pmd_trans_huge(*pmd) && !pmd_devmap(*pmd)))
		return;

	/*
	 * If caller asks to setup a migration entries, we need a page to check
	 * pmd against. Otherwise we can end up replacing wrong page.
	 */
	VM_BUG_ON(freeze && !page);
	if (page && page != pmd_page(*pmd))
		return;

	/*
	 * Caller holds the mmap_sem write mode, so a huge pmd cannot
	 * materialize from under us.
	 */
	__split_huge_pmd(vma, pmd, address, freeze);
}

void vma_adjust_trans_huge(struct vm_area_struct *vma,
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
		split_huge_pmd_address(vma, start, false, NULL);

	/*
	 * If the new end address isn't hpage aligned and it could
	 * previously contain an hugepage: check if we need to split
	 * an huge pmd.
	 */
	if (end & ~HPAGE_PMD_MASK &&
	    (end & HPAGE_PMD_MASK) >= vma->vm_start &&
	    (end & HPAGE_PMD_MASK) + HPAGE_PMD_SIZE <= vma->vm_end)
		split_huge_pmd_address(vma, end, false, NULL);

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
			split_huge_pmd_address(next, nstart, false, NULL);
	}
}

static void freeze_page(struct page *page)
{
	enum ttu_flags ttu_flags = TTU_MIGRATION | TTU_IGNORE_MLOCK |
		TTU_IGNORE_ACCESS | TTU_RMAP_LOCKED;
	int i, ret;

	VM_BUG_ON_PAGE(!PageHead(page), page);

	/* We only need TTU_SPLIT_HUGE_PMD once */
	ret = try_to_unmap(page, ttu_flags | TTU_SPLIT_HUGE_PMD);
	for (i = 1; !ret && i < HPAGE_PMD_NR; i++) {
		/* Cut short if the page is unmapped */
		if (page_count(page) == 1)
			return;

		ret = try_to_unmap(page + i, ttu_flags);
	}
	VM_BUG_ON(ret);
}

static void unfreeze_page(struct page *page)
{
	int i;

	for (i = 0; i < HPAGE_PMD_NR; i++)
		remove_migration_ptes(page + i, page + i, true);
}

static void __split_huge_page_tail(struct page *head, int tail,
		struct lruvec *lruvec, struct list_head *list)
{
	struct page *page_tail = head + tail;

	VM_BUG_ON_PAGE(atomic_read(&page_tail->_mapcount) != -1, page_tail);
	VM_BUG_ON_PAGE(page_ref_count(page_tail) != 0, page_tail);

	/*
	 * tail_page->_count is zero and not changing from under us. But
	 * get_page_unless_zero() may be running from under us on the
	 * tail_page. If we used atomic_set() below instead of atomic_inc(), we
	 * would then run atomic_set() concurrently with
	 * get_page_unless_zero(), and atomic_set() is implemented in C not
	 * using locked ops. spin_unlock on x86 sometime uses locked ops
	 * because of PPro errata 66, 92, so unless somebody can guarantee
	 * atomic_set() here would be safe on all archs (and not only on x86),
	 * it's safer to use atomic_inc().
	 */
	page_ref_inc(page_tail);

	page_tail->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
	page_tail->flags |= (head->flags &
			((1L << PG_referenced) |
			 (1L << PG_swapbacked) |
			 (1L << PG_mlocked) |
			 (1L << PG_uptodate) |
			 (1L << PG_active) |
			 (1L << PG_locked) |
			 (1L << PG_unevictable) |
			 (1L << PG_dirty)));

	/*
	 * After clearing PageTail the gup refcount can be released.
	 * Page flags also must be visible before we make the page non-compound.
	 */
	smp_wmb();

	clear_compound_head(page_tail);

	if (page_is_young(head))
		set_page_young(page_tail);
	if (page_is_idle(head))
		set_page_idle(page_tail);

	/* ->mapping in first tail page is compound_mapcount */
	VM_BUG_ON_PAGE(tail > 2 && page_tail->mapping != TAIL_MAPPING,
			page_tail);
	page_tail->mapping = head->mapping;

	page_tail->index = head->index + tail;
	page_cpupid_xchg_last(page_tail, page_cpupid_last(head));
	lru_add_page_tail(head, page_tail, lruvec, list);
}

static void __split_huge_page(struct page *page, struct list_head *list)
{
	struct page *head = compound_head(page);
	struct zone *zone = page_zone(head);
	struct lruvec *lruvec;
	int i;

	/* prevent PageLRU to go away from under us, and freeze lru stats */
	spin_lock_irq(&zone->lru_lock);
	lruvec = mem_cgroup_page_lruvec(head, zone);

	/* complete memcg works before add pages to LRU */
	mem_cgroup_split_huge_fixup(head);

	for (i = HPAGE_PMD_NR - 1; i >= 1; i--)
		__split_huge_page_tail(head, i, lruvec, list);

	ClearPageCompound(head);
	spin_unlock_irq(&zone->lru_lock);

	unfreeze_page(head);

	for (i = 0; i < HPAGE_PMD_NR; i++) {
		struct page *subpage = head + i;
		if (subpage == page)
			continue;
		unlock_page(subpage);

		/*
		 * Subpages may be freed if there wasn't any mapping
		 * like if add_to_swap() is running on a lru page that
		 * had its mapping zapped. And freeing these pages
		 * requires taking the lru_lock so we do the put_page
		 * of the tail pages after the split is complete.
		 */
		put_page(subpage);
	}
}

int total_mapcount(struct page *page)
{
	int i, ret;

	VM_BUG_ON_PAGE(PageTail(page), page);

	if (likely(!PageCompound(page)))
		return atomic_read(&page->_mapcount) + 1;

	ret = compound_mapcount(page);
	if (PageHuge(page))
		return ret;
	for (i = 0; i < HPAGE_PMD_NR; i++)
		ret += atomic_read(&page[i]._mapcount) + 1;
	if (PageDoubleMap(page))
		ret -= HPAGE_PMD_NR;
	return ret;
}

/*
 * This calculates accurately how many mappings a transparent hugepage
 * has (unlike page_mapcount() which isn't fully accurate). This full
 * accuracy is primarily needed to know if copy-on-write faults can
 * reuse the page and change the mapping to read-write instead of
 * copying them. At the same time this returns the total_mapcount too.
 *
 * The function returns the highest mapcount any one of the subpages
 * has. If the return value is one, even if different processes are
 * mapping different subpages of the transparent hugepage, they can
 * all reuse it, because each process is reusing a different subpage.
 *
 * The total_mapcount is instead counting all virtual mappings of the
 * subpages. If the total_mapcount is equal to "one", it tells the
 * caller all mappings belong to the same "mm" and in turn the
 * anon_vma of the transparent hugepage can become the vma->anon_vma
 * local one as no other process may be mapping any of the subpages.
 *
 * It would be more accurate to replace page_mapcount() with
 * page_trans_huge_mapcount(), however we only use
 * page_trans_huge_mapcount() in the copy-on-write faults where we
 * need full accuracy to avoid breaking page pinning, because
 * page_trans_huge_mapcount() is slower than page_mapcount().
 */
int page_trans_huge_mapcount(struct page *page, int *total_mapcount)
{
	int i, ret, _total_mapcount, mapcount;

	/* hugetlbfs shouldn't call it */
	VM_BUG_ON_PAGE(PageHuge(page), page);

	if (likely(!PageTransCompound(page))) {
		mapcount = atomic_read(&page->_mapcount) + 1;
		if (total_mapcount)
			*total_mapcount = mapcount;
		return mapcount;
	}

	page = compound_head(page);

	_total_mapcount = ret = 0;
	for (i = 0; i < HPAGE_PMD_NR; i++) {
		mapcount = atomic_read(&page[i]._mapcount) + 1;
		ret = max(ret, mapcount);
		_total_mapcount += mapcount;
	}
	if (PageDoubleMap(page)) {
		ret -= 1;
		_total_mapcount -= HPAGE_PMD_NR;
	}
	mapcount = compound_mapcount(page);
	ret += mapcount;
	_total_mapcount += mapcount;
	if (total_mapcount)
		*total_mapcount = _total_mapcount;
	return ret;
}

/*
 * This function splits huge page into normal pages. @page can point to any
 * subpage of huge page to split. Split doesn't change the position of @page.
 *
 * Only caller must hold pin on the @page, otherwise split fails with -EBUSY.
 * The huge page must be locked.
 *
 * If @list is null, tail pages will be added to LRU list, otherwise, to @list.
 *
 * Both head page and tail pages will inherit mapping, flags, and so on from
 * the hugepage.
 *
 * GUP pin and PG_locked transferred to @page. Rest subpages can be freed if
 * they are not mapped.
 *
 * Returns 0 if the hugepage is split successfully.
 * Returns -EBUSY if the page is pinned or if anon_vma disappeared from under
 * us.
 */
int split_huge_page_to_list(struct page *page, struct list_head *list)
{
	struct page *head = compound_head(page);
	struct pglist_data *pgdata = NODE_DATA(page_to_nid(head));
	struct anon_vma *anon_vma;
	int count, mapcount, ret;
	bool mlocked;
	unsigned long flags;

	VM_BUG_ON_PAGE(is_huge_zero_page(page), page);
	VM_BUG_ON_PAGE(!PageAnon(page), page);
	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(!PageSwapBacked(page), page);
	VM_BUG_ON_PAGE(!PageCompound(page), page);

	/*
	 * The caller does not necessarily hold an mmap_sem that would prevent
	 * the anon_vma disappearing so we first we take a reference to it
	 * and then lock the anon_vma for write. This is similar to
	 * page_lock_anon_vma_read except the write lock is taken to serialise
	 * against parallel split or collapse operations.
	 */
	anon_vma = page_get_anon_vma(head);
	if (!anon_vma) {
		ret = -EBUSY;
		goto out;
	}
	anon_vma_lock_write(anon_vma);

	/*
	 * Racy check if we can split the page, before freeze_page() will
	 * split PMDs
	 */
	if (total_mapcount(head) != page_count(head) - 1) {
		ret = -EBUSY;
		goto out_unlock;
	}

	mlocked = PageMlocked(page);
	freeze_page(head);
	VM_BUG_ON_PAGE(compound_mapcount(head), head);

	/* Make sure the page is not on per-CPU pagevec as it takes pin */
	if (mlocked)
		lru_add_drain();

	/* Prevent deferred_split_scan() touching ->_count */
	spin_lock_irqsave(&pgdata->split_queue_lock, flags);
	count = page_count(head);
	mapcount = total_mapcount(head);
	if (!mapcount && count == 1) {
		if (!list_empty(page_deferred_list(head))) {
			pgdata->split_queue_len--;
			list_del(page_deferred_list(head));
		}
		spin_unlock_irqrestore(&pgdata->split_queue_lock, flags);
		__split_huge_page(page, list);
		ret = 0;
	} else if (IS_ENABLED(CONFIG_DEBUG_VM) && mapcount) {
		spin_unlock_irqrestore(&pgdata->split_queue_lock, flags);
		pr_alert("total_mapcount: %u, page_count(): %u\n",
				mapcount, count);
		if (PageTail(page))
			dump_page(head, NULL);
		dump_page(page, "total_mapcount(head) > 0");
		BUG();
	} else {
		spin_unlock_irqrestore(&pgdata->split_queue_lock, flags);
		unfreeze_page(head);
		ret = -EBUSY;
	}

out_unlock:
	anon_vma_unlock_write(anon_vma);
	put_anon_vma(anon_vma);
out:
	count_vm_event(!ret ? THP_SPLIT_PAGE : THP_SPLIT_PAGE_FAILED);
	return ret;
}

void free_transhuge_page(struct page *page)
{
	struct pglist_data *pgdata = NODE_DATA(page_to_nid(page));
	unsigned long flags;

	spin_lock_irqsave(&pgdata->split_queue_lock, flags);
	if (!list_empty(page_deferred_list(page))) {
		pgdata->split_queue_len--;
		list_del(page_deferred_list(page));
	}
	spin_unlock_irqrestore(&pgdata->split_queue_lock, flags);
	free_compound_page(page);
}

void deferred_split_huge_page(struct page *page)
{
	struct pglist_data *pgdata = NODE_DATA(page_to_nid(page));
	unsigned long flags;

	VM_BUG_ON_PAGE(!PageTransHuge(page), page);

	spin_lock_irqsave(&pgdata->split_queue_lock, flags);
	if (list_empty(page_deferred_list(page))) {
		count_vm_event(THP_DEFERRED_SPLIT_PAGE);
		list_add_tail(page_deferred_list(page), &pgdata->split_queue);
		pgdata->split_queue_len++;
	}
	spin_unlock_irqrestore(&pgdata->split_queue_lock, flags);
}

static unsigned long deferred_split_count(struct shrinker *shrink,
		struct shrink_control *sc)
{
	struct pglist_data *pgdata = NODE_DATA(sc->nid);
	return ACCESS_ONCE(pgdata->split_queue_len);
}

static unsigned long deferred_split_scan(struct shrinker *shrink,
		struct shrink_control *sc)
{
	struct pglist_data *pgdata = NODE_DATA(sc->nid);
	unsigned long flags;
	LIST_HEAD(list), *pos, *next;
	struct page *page;
	int split = 0;

	spin_lock_irqsave(&pgdata->split_queue_lock, flags);
	/* Take pin on all head pages to avoid freeing them under us */
	list_for_each_safe(pos, next, &pgdata->split_queue) {
		page = list_entry((void *)pos, struct page, mapping);
		page = compound_head(page);
		if (get_page_unless_zero(page)) {
			list_move(page_deferred_list(page), &list);
		} else {
			/* We lost race with put_compound_page() */
			list_del_init(page_deferred_list(page));
			pgdata->split_queue_len--;
		}
		if (!--sc->nr_to_scan)
			break;
	}
	spin_unlock_irqrestore(&pgdata->split_queue_lock, flags);

	list_for_each_safe(pos, next, &list) {
		page = list_entry((void *)pos, struct page, mapping);
		lock_page(page);
		/* split_huge_page() removes page from list on success */
		if (!split_huge_page(page))
			split++;
		unlock_page(page);
		put_page(page);
	}

	spin_lock_irqsave(&pgdata->split_queue_lock, flags);
	list_splice_tail(&list, &pgdata->split_queue);
	spin_unlock_irqrestore(&pgdata->split_queue_lock, flags);

	/*
	 * Stop shrinker if we didn't split any page, but the queue is empty.
	 * This can happen if pages were freed under us.
	 */
	if (!split && list_empty(&pgdata->split_queue))
		return SHRINK_STOP;
	return split;
}

static struct shrinker deferred_split_shrinker = {
	.count_objects = deferred_split_count,
	.scan_objects = deferred_split_scan,
	.seeks = DEFAULT_SEEKS,
	.flags = SHRINKER_NUMA_AWARE,
};

#ifdef CONFIG_DEBUG_FS
static int split_huge_pages_set(void *data, u64 val)
{
	struct zone *zone;
	struct page *page;
	unsigned long pfn, max_zone_pfn;
	unsigned long total = 0, split = 0;

	if (val != 1)
		return -EINVAL;

	for_each_populated_zone(zone) {
		max_zone_pfn = zone_end_pfn(zone);
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++) {
			if (!pfn_valid(pfn))
				continue;

			page = pfn_to_page(pfn);
			if (!get_page_unless_zero(page))
				continue;

			if (zone != page_zone(page))
				goto next;

			if (!PageHead(page) || !PageAnon(page) ||
					PageHuge(page))
				goto next;

			total++;
			lock_page(page);
			if (!split_huge_page(page))
				split++;
			unlock_page(page);
next:
			put_page(page);
		}
	}

	pr_info("%lu of %lu THP split\n", split, total);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(split_huge_pages_fops, NULL, split_huge_pages_set,
		"%llu\n");

static int __init split_huge_pages_debugfs(void)
{
	void *ret;

	ret = debugfs_create_file("split_huge_pages", 0200, NULL, NULL,
			&split_huge_pages_fops);
	if (!ret)
		pr_warn("Failed to create split_huge_pages in debugfs");
	return 0;
}
late_initcall(split_huge_pages_debugfs);
#endif
