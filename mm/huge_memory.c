/*
 *  Copyright (C) 2009  Red Hat, Inc.
 *
 *  This work is licensed under the terms of the GNU GPL, version 2. See
 *  the COPYING file in the top-level directory.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/coredump.h>
#include <linux/sched/numa_balancing.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/mmu_notifier.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/shrinker.h>
#include <linux/mm_inline.h>
#include <linux/swapops.h>
#include <linux/dax.h>
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
#include <linux/shmem_fs.h>
#include <linux/oom.h>

#include <asm/tlb.h>
#include <asm/pgalloc.h>
#include "internal.h"

/*
 * By default, transparent hugepage support is disabled in order to avoid
 * risking an increased memory footprint for applications that are not
 * guaranteed to benefit from it. When transparent hugepage support is
 * enabled, it is for all mappings, and khugepaged scans all mappings.
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

static struct shrinker deferred_split_shrinker;

static atomic_t huge_zero_refcount;
struct page *huge_zero_page __read_mostly;

static struct page *get_huge_zero_page(void)
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

static void put_huge_zero_page(void)
{
	/*
	 * Counter should never go to zero here. Only shrinker can put
	 * last reference.
	 */
	BUG_ON(atomic_dec_and_test(&huge_zero_refcount));
}

struct page *mm_get_huge_zero_page(struct mm_struct *mm)
{
	if (test_bit(MMF_HUGE_ZERO_PAGE, &mm->flags))
		return READ_ONCE(huge_zero_page);

	if (!get_huge_zero_page())
		return NULL;

	if (test_and_set_bit(MMF_HUGE_ZERO_PAGE, &mm->flags))
		put_huge_zero_page();

	return READ_ONCE(huge_zero_page);
}

void mm_put_huge_zero_page(struct mm_struct *mm)
{
	if (test_bit(MMF_HUGE_ZERO_PAGE, &mm->flags))
		put_huge_zero_page();
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
	ssize_t ret = count;

	if (!memcmp("always", buf,
		    min(sizeof("always")-1, count))) {
		clear_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG, &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_FLAG, &transparent_hugepage_flags);
	} else if (!memcmp("madvise", buf,
			   min(sizeof("madvise")-1, count))) {
		clear_bit(TRANSPARENT_HUGEPAGE_FLAG, &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG, &transparent_hugepage_flags);
	} else if (!memcmp("never", buf,
			   min(sizeof("never")-1, count))) {
		clear_bit(TRANSPARENT_HUGEPAGE_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG, &transparent_hugepage_flags);
	} else
		ret = -EINVAL;

	if (ret > 0) {
		int err = start_stop_khugepaged();
		if (err)
			ret = err;
	}
	return ret;
}
static struct kobj_attribute enabled_attr =
	__ATTR(enabled, 0644, enabled_show, enabled_store);

ssize_t single_hugepage_flag_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf,
				enum transparent_hugepage_flag flag)
{
	return sprintf(buf, "%d\n",
		       !!test_bit(flag, &transparent_hugepage_flags));
}

ssize_t single_hugepage_flag_store(struct kobject *kobj,
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

static ssize_t defrag_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "[always] defer defer+madvise madvise never\n");
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "always [defer] defer+madvise madvise never\n");
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "always defer [defer+madvise] madvise never\n");
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "always defer defer+madvise [madvise] never\n");
	return sprintf(buf, "always defer defer+madvise madvise [never]\n");
}

static ssize_t defrag_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	if (!memcmp("always", buf,
		    min(sizeof("always")-1, count))) {
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags);
	} else if (!memcmp("defer+madvise", buf,
		    min(sizeof("defer+madvise")-1, count))) {
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, &transparent_hugepage_flags);
	} else if (!memcmp("defer", buf,
		    min(sizeof("defer")-1, count))) {
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags);
	} else if (!memcmp("madvise", buf,
			   min(sizeof("madvise")-1, count))) {
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags);
	} else if (!memcmp("never", buf,
			   min(sizeof("never")-1, count))) {
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags);
	} else
		return -EINVAL;

	return count;
}
static struct kobj_attribute defrag_attr =
	__ATTR(defrag, 0644, defrag_show, defrag_store);

static ssize_t use_zero_page_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return single_hugepage_flag_show(kobj, attr, buf,
				TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG);
}
static ssize_t use_zero_page_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	return single_hugepage_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG);
}
static struct kobj_attribute use_zero_page_attr =
	__ATTR(use_zero_page, 0644, use_zero_page_show, use_zero_page_store);

static ssize_t hpage_pmd_size_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", HPAGE_PMD_SIZE);
}
static struct kobj_attribute hpage_pmd_size_attr =
	__ATTR_RO(hpage_pmd_size);

#ifdef CONFIG_DEBUG_VM
static ssize_t debug_cow_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return single_hugepage_flag_show(kobj, attr, buf,
				TRANSPARENT_HUGEPAGE_DEBUG_COW_FLAG);
}
static ssize_t debug_cow_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	return single_hugepage_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_DEBUG_COW_FLAG);
}
static struct kobj_attribute debug_cow_attr =
	__ATTR(debug_cow, 0644, debug_cow_show, debug_cow_store);
#endif /* CONFIG_DEBUG_VM */

static struct attribute *hugepage_attr[] = {
	&enabled_attr.attr,
	&defrag_attr.attr,
	&use_zero_page_attr.attr,
	&hpage_pmd_size_attr.attr,
#if defined(CONFIG_SHMEM) && defined(CONFIG_TRANSPARENT_HUGE_PAGECACHE)
	&shmem_enabled_attr.attr,
#endif
#ifdef CONFIG_DEBUG_VM
	&debug_cow_attr.attr,
#endif
	NULL,
};

static const struct attribute_group hugepage_attr_group = {
	.attrs = hugepage_attr,
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

	err = khugepaged_init();
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
	khugepaged_destroy();
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

unsigned long __thp_get_unmapped_area(struct file *filp, unsigned long len,
		loff_t off, unsigned long flags, unsigned long size)
{
	unsigned long addr;
	loff_t off_end = off + len;
	loff_t off_align = round_up(off, size);
	unsigned long len_pad;

	if (off_end <= off_align || (off_end - off_align) < size)
		return 0;

	len_pad = len + size;
	if (len_pad < len || (off + len_pad) < off)
		return 0;

	addr = current->mm->get_unmapped_area(filp, 0, len_pad,
					      off >> PAGE_SHIFT, flags);
	if (IS_ERR_VALUE(addr))
		return 0;

	addr += (off - addr) & (size - 1);
	return addr;
}

unsigned long thp_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	loff_t off = (loff_t)pgoff << PAGE_SHIFT;

	if (addr)
		goto out;
	if (!IS_DAX(filp->f_mapping->host) || !IS_ENABLED(CONFIG_FS_DAX_PMD))
		goto out;

	addr = __thp_get_unmapped_area(filp, len, off, flags, PMD_SIZE);
	if (addr)
		return addr;

 out:
	return current->mm->get_unmapped_area(filp, addr, len, pgoff, flags);
}
EXPORT_SYMBOL_GPL(thp_get_unmapped_area);

static int __do_huge_pmd_anonymous_page(struct vm_fault *vmf, struct page *page,
		gfp_t gfp)
{
	struct vm_area_struct *vma = vmf->vma;
	struct mem_cgroup *memcg;
	pgtable_t pgtable;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;
	int ret = 0;

	VM_BUG_ON_PAGE(!PageCompound(page), page);

	if (mem_cgroup_try_charge(page, vma->vm_mm, gfp, &memcg, true)) {
		put_page(page);
		count_vm_event(THP_FAULT_FALLBACK);
		return VM_FAULT_FALLBACK;
	}

	pgtable = pte_alloc_one(vma->vm_mm, haddr);
	if (unlikely(!pgtable)) {
		ret = VM_FAULT_OOM;
		goto release;
	}

	clear_huge_page(page, vmf->address, HPAGE_PMD_NR);
	/*
	 * The memory barrier inside __SetPageUptodate makes sure that
	 * clear_huge_page writes become visible before the set_pmd_at()
	 * write.
	 */
	__SetPageUptodate(page);

	vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
	if (unlikely(!pmd_none(*vmf->pmd))) {
		goto unlock_release;
	} else {
		pmd_t entry;

		ret = check_stable_address_space(vma->vm_mm);
		if (ret)
			goto unlock_release;

		/* Deliver the page fault to userland */
		if (userfaultfd_missing(vma)) {
			int ret;

			spin_unlock(vmf->ptl);
			mem_cgroup_cancel_charge(page, memcg, true);
			put_page(page);
			pte_free(vma->vm_mm, pgtable);
			ret = handle_userfault(vmf, VM_UFFD_MISSING);
			VM_BUG_ON(ret & VM_FAULT_FALLBACK);
			return ret;
		}

		entry = mk_huge_pmd(page, vma->vm_page_prot);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		page_add_new_anon_rmap(page, vma, haddr, true);
		mem_cgroup_commit_charge(page, memcg, false, true);
		lru_cache_add_active_or_unevictable(page, vma);
		pgtable_trans_huge_deposit(vma->vm_mm, vmf->pmd, pgtable);
		set_pmd_at(vma->vm_mm, haddr, vmf->pmd, entry);
		add_mm_counter(vma->vm_mm, MM_ANONPAGES, HPAGE_PMD_NR);
		mm_inc_nr_ptes(vma->vm_mm);
		spin_unlock(vmf->ptl);
		count_vm_event(THP_FAULT_ALLOC);
	}

	return 0;
unlock_release:
	spin_unlock(vmf->ptl);
release:
	if (pgtable)
		pte_free(vma->vm_mm, pgtable);
	mem_cgroup_cancel_charge(page, memcg, true);
	put_page(page);
	return ret;

}

/*
 * always: directly stall for all thp allocations
 * defer: wake kswapd and fail if not immediately available
 * defer+madvise: wake kswapd and directly stall for MADV_HUGEPAGE, otherwise
 *		  fail if not immediately available
 * madvise: directly stall for MADV_HUGEPAGE, otherwise fail if not immediately
 *	    available
 * never: never stall for any thp allocation
 */
static inline gfp_t alloc_hugepage_direct_gfpmask(struct vm_area_struct *vma)
{
	const bool vma_madvised = !!(vma->vm_flags & VM_HUGEPAGE);

	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags))
		return GFP_TRANSHUGE | (vma_madvised ? 0 : __GFP_NORETRY);
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags))
		return GFP_TRANSHUGE_LIGHT | __GFP_KSWAPD_RECLAIM;
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, &transparent_hugepage_flags))
		return GFP_TRANSHUGE_LIGHT | (vma_madvised ? __GFP_DIRECT_RECLAIM :
							     __GFP_KSWAPD_RECLAIM);
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags))
		return GFP_TRANSHUGE_LIGHT | (vma_madvised ? __GFP_DIRECT_RECLAIM :
							     0);
	return GFP_TRANSHUGE_LIGHT;
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
	mm_inc_nr_ptes(mm);
	return true;
}

int do_huge_pmd_anonymous_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	gfp_t gfp;
	struct page *page;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;

	if (haddr < vma->vm_start || haddr + HPAGE_PMD_SIZE > vma->vm_end)
		return VM_FAULT_FALLBACK;
	if (unlikely(anon_vma_prepare(vma)))
		return VM_FAULT_OOM;
	if (unlikely(khugepaged_enter(vma, vma->vm_flags)))
		return VM_FAULT_OOM;
	if (!(vmf->flags & FAULT_FLAG_WRITE) &&
			!mm_forbids_zeropage(vma->vm_mm) &&
			transparent_hugepage_use_zero_page()) {
		pgtable_t pgtable;
		struct page *zero_page;
		bool set;
		int ret;
		pgtable = pte_alloc_one(vma->vm_mm, haddr);
		if (unlikely(!pgtable))
			return VM_FAULT_OOM;
		zero_page = mm_get_huge_zero_page(vma->vm_mm);
		if (unlikely(!zero_page)) {
			pte_free(vma->vm_mm, pgtable);
			count_vm_event(THP_FAULT_FALLBACK);
			return VM_FAULT_FALLBACK;
		}
		vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
		ret = 0;
		set = false;
		if (pmd_none(*vmf->pmd)) {
			ret = check_stable_address_space(vma->vm_mm);
			if (ret) {
				spin_unlock(vmf->ptl);
			} else if (userfaultfd_missing(vma)) {
				spin_unlock(vmf->ptl);
				ret = handle_userfault(vmf, VM_UFFD_MISSING);
				VM_BUG_ON(ret & VM_FAULT_FALLBACK);
			} else {
				set_huge_zero_page(pgtable, vma->vm_mm, vma,
						   haddr, vmf->pmd, zero_page);
				spin_unlock(vmf->ptl);
				set = true;
			}
		} else
			spin_unlock(vmf->ptl);
		if (!set)
			pte_free(vma->vm_mm, pgtable);
		return ret;
	}
	gfp = alloc_hugepage_direct_gfpmask(vma);
	page = alloc_hugepage_vma(gfp, vma, haddr, HPAGE_PMD_ORDER);
	if (unlikely(!page)) {
		count_vm_event(THP_FAULT_FALLBACK);
		return VM_FAULT_FALLBACK;
	}
	prep_transhuge_page(page);
	return __do_huge_pmd_anonymous_page(vmf, page, gfp);
}

static void insert_pfn_pmd(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd, pfn_t pfn, pgprot_t prot, bool write,
		pgtable_t pgtable)
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

	if (pgtable) {
		pgtable_trans_huge_deposit(mm, pmd, pgtable);
		mm_inc_nr_ptes(mm);
	}

	set_pmd_at(mm, addr, pmd, entry);
	update_mmu_cache_pmd(vma, addr, pmd);
	spin_unlock(ptl);
}

int vmf_insert_pfn_pmd(struct vm_area_struct *vma, unsigned long addr,
			pmd_t *pmd, pfn_t pfn, bool write)
{
	pgprot_t pgprot = vma->vm_page_prot;
	pgtable_t pgtable = NULL;
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

	if (arch_needs_pgtable_deposit()) {
		pgtable = pte_alloc_one(vma->vm_mm, addr);
		if (!pgtable)
			return VM_FAULT_OOM;
	}

	track_pfn_insert(vma, &pgprot, pfn);

	insert_pfn_pmd(vma, addr, pmd, pfn, pgprot, write, pgtable);
	return VM_FAULT_NOPAGE;
}
EXPORT_SYMBOL_GPL(vmf_insert_pfn_pmd);

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
static pud_t maybe_pud_mkwrite(pud_t pud, struct vm_area_struct *vma)
{
	if (likely(vma->vm_flags & VM_WRITE))
		pud = pud_mkwrite(pud);
	return pud;
}

static void insert_pfn_pud(struct vm_area_struct *vma, unsigned long addr,
		pud_t *pud, pfn_t pfn, pgprot_t prot, bool write)
{
	struct mm_struct *mm = vma->vm_mm;
	pud_t entry;
	spinlock_t *ptl;

	ptl = pud_lock(mm, pud);
	entry = pud_mkhuge(pfn_t_pud(pfn, prot));
	if (pfn_t_devmap(pfn))
		entry = pud_mkdevmap(entry);
	if (write) {
		entry = pud_mkyoung(pud_mkdirty(entry));
		entry = maybe_pud_mkwrite(entry, vma);
	}
	set_pud_at(mm, addr, pud, entry);
	update_mmu_cache_pud(vma, addr, pud);
	spin_unlock(ptl);
}

int vmf_insert_pfn_pud(struct vm_area_struct *vma, unsigned long addr,
			pud_t *pud, pfn_t pfn, bool write)
{
	pgprot_t pgprot = vma->vm_page_prot;
	/*
	 * If we had pud_special, we could avoid all these restrictions,
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

	track_pfn_insert(vma, &pgprot, pfn);

	insert_pfn_pud(vma, addr, pud, pfn, pgprot, write);
	return VM_FAULT_NOPAGE;
}
EXPORT_SYMBOL_GPL(vmf_insert_pfn_pud);
#endif /* CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */

static void touch_pmd(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd, int flags)
{
	pmd_t _pmd;

	_pmd = pmd_mkyoung(*pmd);
	if (flags & FOLL_WRITE)
		_pmd = pmd_mkdirty(_pmd);
	if (pmdp_set_access_flags(vma, addr & HPAGE_PMD_MASK,
				pmd, _pmd, flags & FOLL_WRITE))
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

	/*
	 * When we COW a devmap PMD entry, we split it into PTEs, so we should
	 * not be in this function with `flags & FOLL_COW` set.
	 */
	WARN_ONCE(flags & FOLL_COW, "mm: In follow_devmap_pmd with FOLL_COW set");

	if (flags & FOLL_WRITE && !pmd_write(*pmd))
		return NULL;

	if (pmd_present(*pmd) && pmd_devmap(*pmd))
		/* pass */;
	else
		return NULL;

	if (flags & FOLL_TOUCH)
		touch_pmd(vma, addr, pmd, flags);

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
	int ret = -ENOMEM;

	/* Skip if can be re-fill on fault */
	if (!vma_is_anonymous(vma))
		return 0;

	pgtable = pte_alloc_one(dst_mm, addr);
	if (unlikely(!pgtable))
		goto out;

	dst_ptl = pmd_lock(dst_mm, dst_pmd);
	src_ptl = pmd_lockptr(src_mm, src_pmd);
	spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);

	ret = -EAGAIN;
	pmd = *src_pmd;

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
	if (unlikely(is_swap_pmd(pmd))) {
		swp_entry_t entry = pmd_to_swp_entry(pmd);

		VM_BUG_ON(!is_pmd_migration_entry(pmd));
		if (is_write_migration_entry(entry)) {
			make_migration_entry_read(&entry);
			pmd = swp_entry_to_pmd(entry);
			if (pmd_swp_soft_dirty(*src_pmd))
				pmd = pmd_swp_mksoft_dirty(pmd);
			set_pmd_at(src_mm, addr, src_pmd, pmd);
		}
		add_mm_counter(dst_mm, MM_ANONPAGES, HPAGE_PMD_NR);
		mm_inc_nr_ptes(dst_mm);
		pgtable_trans_huge_deposit(dst_mm, dst_pmd, pgtable);
		set_pmd_at(dst_mm, addr, dst_pmd, pmd);
		ret = 0;
		goto out_unlock;
	}
#endif

	if (unlikely(!pmd_trans_huge(pmd))) {
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
		zero_page = mm_get_huge_zero_page(dst_mm);
		set_huge_zero_page(pgtable, dst_mm, vma, addr, dst_pmd,
				zero_page);
		ret = 0;
		goto out_unlock;
	}

	src_page = pmd_page(pmd);
	VM_BUG_ON_PAGE(!PageHead(src_page), src_page);
	get_page(src_page);
	page_dup_rmap(src_page, true);
	add_mm_counter(dst_mm, MM_ANONPAGES, HPAGE_PMD_NR);
	mm_inc_nr_ptes(dst_mm);
	pgtable_trans_huge_deposit(dst_mm, dst_pmd, pgtable);

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

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
static void touch_pud(struct vm_area_struct *vma, unsigned long addr,
		pud_t *pud, int flags)
{
	pud_t _pud;

	_pud = pud_mkyoung(*pud);
	if (flags & FOLL_WRITE)
		_pud = pud_mkdirty(_pud);
	if (pudp_set_access_flags(vma, addr & HPAGE_PUD_MASK,
				pud, _pud, flags & FOLL_WRITE))
		update_mmu_cache_pud(vma, addr, pud);
}

struct page *follow_devmap_pud(struct vm_area_struct *vma, unsigned long addr,
		pud_t *pud, int flags)
{
	unsigned long pfn = pud_pfn(*pud);
	struct mm_struct *mm = vma->vm_mm;
	struct dev_pagemap *pgmap;
	struct page *page;

	assert_spin_locked(pud_lockptr(mm, pud));

	if (flags & FOLL_WRITE && !pud_write(*pud))
		return NULL;

	if (pud_present(*pud) && pud_devmap(*pud))
		/* pass */;
	else
		return NULL;

	if (flags & FOLL_TOUCH)
		touch_pud(vma, addr, pud, flags);

	/*
	 * device mapped pages can only be returned if the
	 * caller will manage the page reference count.
	 */
	if (!(flags & FOLL_GET))
		return ERR_PTR(-EEXIST);

	pfn += (addr & ~PUD_MASK) >> PAGE_SHIFT;
	pgmap = get_dev_pagemap(pfn, NULL);
	if (!pgmap)
		return ERR_PTR(-EFAULT);
	page = pfn_to_page(pfn);
	get_page(page);
	put_dev_pagemap(pgmap);

	return page;
}

int copy_huge_pud(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		  pud_t *dst_pud, pud_t *src_pud, unsigned long addr,
		  struct vm_area_struct *vma)
{
	spinlock_t *dst_ptl, *src_ptl;
	pud_t pud;
	int ret;

	dst_ptl = pud_lock(dst_mm, dst_pud);
	src_ptl = pud_lockptr(src_mm, src_pud);
	spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);

	ret = -EAGAIN;
	pud = *src_pud;
	if (unlikely(!pud_trans_huge(pud) && !pud_devmap(pud)))
		goto out_unlock;

	/*
	 * When page table lock is held, the huge zero pud should not be
	 * under splitting since we don't split the page itself, only pud to
	 * a page table.
	 */
	if (is_huge_zero_pud(pud)) {
		/* No huge zero pud yet */
	}

	pudp_set_wrprotect(src_mm, addr, src_pud);
	pud = pud_mkold(pud_wrprotect(pud));
	set_pud_at(dst_mm, addr, dst_pud, pud);

	ret = 0;
out_unlock:
	spin_unlock(src_ptl);
	spin_unlock(dst_ptl);
	return ret;
}

void huge_pud_set_accessed(struct vm_fault *vmf, pud_t orig_pud)
{
	pud_t entry;
	unsigned long haddr;
	bool write = vmf->flags & FAULT_FLAG_WRITE;

	vmf->ptl = pud_lock(vmf->vma->vm_mm, vmf->pud);
	if (unlikely(!pud_same(*vmf->pud, orig_pud)))
		goto unlock;

	entry = pud_mkyoung(orig_pud);
	if (write)
		entry = pud_mkdirty(entry);
	haddr = vmf->address & HPAGE_PUD_MASK;
	if (pudp_set_access_flags(vmf->vma, haddr, vmf->pud, entry, write))
		update_mmu_cache_pud(vmf->vma, vmf->address, vmf->pud);

unlock:
	spin_unlock(vmf->ptl);
}
#endif /* CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */

void huge_pmd_set_accessed(struct vm_fault *vmf, pmd_t orig_pmd)
{
	pmd_t entry;
	unsigned long haddr;
	bool write = vmf->flags & FAULT_FLAG_WRITE;

	vmf->ptl = pmd_lock(vmf->vma->vm_mm, vmf->pmd);
	if (unlikely(!pmd_same(*vmf->pmd, orig_pmd)))
		goto unlock;

	entry = pmd_mkyoung(orig_pmd);
	if (write)
		entry = pmd_mkdirty(entry);
	haddr = vmf->address & HPAGE_PMD_MASK;
	if (pmdp_set_access_flags(vmf->vma, haddr, vmf->pmd, entry, write))
		update_mmu_cache_pmd(vmf->vma, vmf->address, vmf->pmd);

unlock:
	spin_unlock(vmf->ptl);
}

static int do_huge_pmd_wp_page_fallback(struct vm_fault *vmf, pmd_t orig_pmd,
		struct page *page)
{
	struct vm_area_struct *vma = vmf->vma;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;
	struct mem_cgroup *memcg;
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
		pages[i] = alloc_page_vma_node(GFP_HIGHUSER_MOVABLE, vma,
					       vmf->address, page_to_nid(page));
		if (unlikely(!pages[i] ||
			     mem_cgroup_try_charge(pages[i], vma->vm_mm,
				     GFP_KERNEL, &memcg, false))) {
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
	mmu_notifier_invalidate_range_start(vma->vm_mm, mmun_start, mmun_end);

	vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
	if (unlikely(!pmd_same(*vmf->pmd, orig_pmd)))
		goto out_free_pages;
	VM_BUG_ON_PAGE(!PageHead(page), page);

	/*
	 * Leave pmd empty until pte is filled note we must notify here as
	 * concurrent CPU thread might write to new page before the call to
	 * mmu_notifier_invalidate_range_end() happens which can lead to a
	 * device seeing memory write in different order than CPU.
	 *
	 * See Documentation/vm/mmu_notifier.txt
	 */
	pmdp_huge_clear_flush_notify(vma, haddr, vmf->pmd);

	pgtable = pgtable_trans_huge_withdraw(vma->vm_mm, vmf->pmd);
	pmd_populate(vma->vm_mm, &_pmd, pgtable);

	for (i = 0; i < HPAGE_PMD_NR; i++, haddr += PAGE_SIZE) {
		pte_t entry;
		entry = mk_pte(pages[i], vma->vm_page_prot);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		memcg = (void *)page_private(pages[i]);
		set_page_private(pages[i], 0);
		page_add_new_anon_rmap(pages[i], vmf->vma, haddr, false);
		mem_cgroup_commit_charge(pages[i], memcg, false, false);
		lru_cache_add_active_or_unevictable(pages[i], vma);
		vmf->pte = pte_offset_map(&_pmd, haddr);
		VM_BUG_ON(!pte_none(*vmf->pte));
		set_pte_at(vma->vm_mm, haddr, vmf->pte, entry);
		pte_unmap(vmf->pte);
	}
	kfree(pages);

	smp_wmb(); /* make pte visible before pmd */
	pmd_populate(vma->vm_mm, vmf->pmd, pgtable);
	page_remove_rmap(page, true);
	spin_unlock(vmf->ptl);

	/*
	 * No need to double call mmu_notifier->invalidate_range() callback as
	 * the above pmdp_huge_clear_flush_notify() did already call it.
	 */
	mmu_notifier_invalidate_range_only_end(vma->vm_mm, mmun_start,
						mmun_end);

	ret |= VM_FAULT_WRITE;
	put_page(page);

out:
	return ret;

out_free_pages:
	spin_unlock(vmf->ptl);
	mmu_notifier_invalidate_range_end(vma->vm_mm, mmun_start, mmun_end);
	for (i = 0; i < HPAGE_PMD_NR; i++) {
		memcg = (void *)page_private(pages[i]);
		set_page_private(pages[i], 0);
		mem_cgroup_cancel_charge(pages[i], memcg, false);
		put_page(pages[i]);
	}
	kfree(pages);
	goto out;
}

int do_huge_pmd_wp_page(struct vm_fault *vmf, pmd_t orig_pmd)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page = NULL, *new_page;
	struct mem_cgroup *memcg;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */
	gfp_t huge_gfp;			/* for allocation and charge */
	int ret = 0;

	vmf->ptl = pmd_lockptr(vma->vm_mm, vmf->pmd);
	VM_BUG_ON_VMA(!vma->anon_vma, vma);
	if (is_huge_zero_pmd(orig_pmd))
		goto alloc;
	spin_lock(vmf->ptl);
	if (unlikely(!pmd_same(*vmf->pmd, orig_pmd)))
		goto out_unlock;

	page = pmd_page(orig_pmd);
	VM_BUG_ON_PAGE(!PageCompound(page) || !PageHead(page), page);
	/*
	 * We can only reuse the page if nobody else maps the huge page or it's
	 * part.
	 */
	if (!trylock_page(page)) {
		get_page(page);
		spin_unlock(vmf->ptl);
		lock_page(page);
		spin_lock(vmf->ptl);
		if (unlikely(!pmd_same(*vmf->pmd, orig_pmd))) {
			unlock_page(page);
			put_page(page);
			goto out_unlock;
		}
		put_page(page);
	}
	if (reuse_swap_page(page, NULL)) {
		pmd_t entry;
		entry = pmd_mkyoung(orig_pmd);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		if (pmdp_set_access_flags(vma, haddr, vmf->pmd, entry,  1))
			update_mmu_cache_pmd(vma, vmf->address, vmf->pmd);
		ret |= VM_FAULT_WRITE;
		unlock_page(page);
		goto out_unlock;
	}
	unlock_page(page);
	get_page(page);
	spin_unlock(vmf->ptl);
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
			split_huge_pmd(vma, vmf->pmd, vmf->address);
			ret |= VM_FAULT_FALLBACK;
		} else {
			ret = do_huge_pmd_wp_page_fallback(vmf, orig_pmd, page);
			if (ret & VM_FAULT_OOM) {
				split_huge_pmd(vma, vmf->pmd, vmf->address);
				ret |= VM_FAULT_FALLBACK;
			}
			put_page(page);
		}
		count_vm_event(THP_FAULT_FALLBACK);
		goto out;
	}

	if (unlikely(mem_cgroup_try_charge(new_page, vma->vm_mm,
					huge_gfp, &memcg, true))) {
		put_page(new_page);
		split_huge_pmd(vma, vmf->pmd, vmf->address);
		if (page)
			put_page(page);
		ret |= VM_FAULT_FALLBACK;
		count_vm_event(THP_FAULT_FALLBACK);
		goto out;
	}

	count_vm_event(THP_FAULT_ALLOC);

	if (!page)
		clear_huge_page(new_page, vmf->address, HPAGE_PMD_NR);
	else
		copy_user_huge_page(new_page, page, haddr, vma, HPAGE_PMD_NR);
	__SetPageUptodate(new_page);

	mmun_start = haddr;
	mmun_end   = haddr + HPAGE_PMD_SIZE;
	mmu_notifier_invalidate_range_start(vma->vm_mm, mmun_start, mmun_end);

	spin_lock(vmf->ptl);
	if (page)
		put_page(page);
	if (unlikely(!pmd_same(*vmf->pmd, orig_pmd))) {
		spin_unlock(vmf->ptl);
		mem_cgroup_cancel_charge(new_page, memcg, true);
		put_page(new_page);
		goto out_mn;
	} else {
		pmd_t entry;
		entry = mk_huge_pmd(new_page, vma->vm_page_prot);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		pmdp_huge_clear_flush_notify(vma, haddr, vmf->pmd);
		page_add_new_anon_rmap(new_page, vma, haddr, true);
		mem_cgroup_commit_charge(new_page, memcg, false, true);
		lru_cache_add_active_or_unevictable(new_page, vma);
		set_pmd_at(vma->vm_mm, haddr, vmf->pmd, entry);
		update_mmu_cache_pmd(vma, vmf->address, vmf->pmd);
		if (!page) {
			add_mm_counter(vma->vm_mm, MM_ANONPAGES, HPAGE_PMD_NR);
		} else {
			VM_BUG_ON_PAGE(!PageHead(page), page);
			page_remove_rmap(page, true);
			put_page(page);
		}
		ret |= VM_FAULT_WRITE;
	}
	spin_unlock(vmf->ptl);
out_mn:
	/*
	 * No need to double call mmu_notifier->invalidate_range() callback as
	 * the above pmdp_huge_clear_flush_notify() did already call it.
	 */
	mmu_notifier_invalidate_range_only_end(vma->vm_mm, mmun_start,
					       mmun_end);
out:
	return ret;
out_unlock:
	spin_unlock(vmf->ptl);
	return ret;
}

/*
 * FOLL_FORCE can write to even unwritable pmd's, but only
 * after we've gone through a COW cycle and they are dirty.
 */
static inline bool can_follow_write_pmd(pmd_t pmd, unsigned int flags)
{
	return pmd_write(pmd) ||
	       ((flags & FOLL_FORCE) && (flags & FOLL_COW) && pmd_dirty(pmd));
}

struct page *follow_trans_huge_pmd(struct vm_area_struct *vma,
				   unsigned long addr,
				   pmd_t *pmd,
				   unsigned int flags)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page *page = NULL;

	assert_spin_locked(pmd_lockptr(mm, pmd));

	if (flags & FOLL_WRITE && !can_follow_write_pmd(*pmd, flags))
		goto out;

	/* Avoid dumping huge zero page */
	if ((flags & FOLL_DUMP) && is_huge_zero_pmd(*pmd))
		return ERR_PTR(-EFAULT);

	/* Full NUMA hinting faults to serialise migration in fault paths */
	if ((flags & FOLL_NUMA) && pmd_protnone(*pmd))
		goto out;

	page = pmd_page(*pmd);
	VM_BUG_ON_PAGE(!PageHead(page) && !is_zone_device_page(page), page);
	if (flags & FOLL_TOUCH)
		touch_pmd(vma, addr, pmd, flags);
	if ((flags & FOLL_MLOCK) && (vma->vm_flags & VM_LOCKED)) {
		/*
		 * We don't mlock() pte-mapped THPs. This way we can avoid
		 * leaking mlocked pages into non-VM_LOCKED VMAs.
		 *
		 * For anon THP:
		 *
		 * In most cases the pmd is the only mapping of the page as we
		 * break COW for the mlock() -- see gup_flags |= FOLL_WRITE for
		 * writable private mappings in populate_vma_page_range().
		 *
		 * The only scenario when we have the page shared here is if we
		 * mlocking read-only mapping shared over fork(). We skip
		 * mlocking such pages.
		 *
		 * For file THP:
		 *
		 * We can expect PageDoubleMap() to be stable under page lock:
		 * for file pages we set it in page_add_file_rmap(), which
		 * requires page to be locked.
		 */

		if (PageAnon(page) && compound_mapcount(page) != 1)
			goto skip_mlock;
		if (PageDoubleMap(page) || !page->mapping)
			goto skip_mlock;
		if (!trylock_page(page))
			goto skip_mlock;
		lru_add_drain();
		if (page->mapping && !PageDoubleMap(page))
			mlock_vma_page(page);
		unlock_page(page);
	}
skip_mlock:
	page += (addr & ~HPAGE_PMD_MASK) >> PAGE_SHIFT;
	VM_BUG_ON_PAGE(!PageCompound(page) && !is_zone_device_page(page), page);
	if (flags & FOLL_GET)
		get_page(page);

out:
	return page;
}

/* NUMA hinting page fault entry point for trans huge pmds */
int do_huge_pmd_numa_page(struct vm_fault *vmf, pmd_t pmd)
{
	struct vm_area_struct *vma = vmf->vma;
	struct anon_vma *anon_vma = NULL;
	struct page *page;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;
	int page_nid = -1, this_nid = numa_node_id();
	int target_nid, last_cpupid = -1;
	bool page_locked;
	bool migrated = false;
	bool was_writable;
	int flags = 0;

	vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
	if (unlikely(!pmd_same(pmd, *vmf->pmd)))
		goto out_unlock;

	/*
	 * If there are potential migrations, wait for completion and retry
	 * without disrupting NUMA hinting information. Do not relock and
	 * check_same as the page may no longer be mapped.
	 */
	if (unlikely(pmd_trans_migrating(*vmf->pmd))) {
		page = pmd_page(*vmf->pmd);
		if (!get_page_unless_zero(page))
			goto out_unlock;
		spin_unlock(vmf->ptl);
		wait_on_page_locked(page);
		put_page(page);
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
	if (!pmd_savedwrite(pmd))
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
		page_nid = -1;
		if (!get_page_unless_zero(page))
			goto out_unlock;
		spin_unlock(vmf->ptl);
		wait_on_page_locked(page);
		put_page(page);
		goto out;
	}

	/*
	 * Page is misplaced. Page lock serialises migrations. Acquire anon_vma
	 * to serialises splits
	 */
	get_page(page);
	spin_unlock(vmf->ptl);
	anon_vma = page_lock_anon_vma_read(page);

	/* Confirm the PMD did not change while page_table_lock was released */
	spin_lock(vmf->ptl);
	if (unlikely(!pmd_same(pmd, *vmf->pmd))) {
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
	 * Since we took the NUMA fault, we must have observed the !accessible
	 * bit. Make sure all other CPUs agree with that, to avoid them
	 * modifying the page we're about to migrate.
	 *
	 * Must be done under PTL such that we'll observe the relevant
	 * inc_tlb_flush_pending().
	 *
	 * We are not sure a pending tlb flush here is for a huge page
	 * mapping or not. Hence use the tlb range variant
	 */
	if (mm_tlb_flush_pending(vma->vm_mm))
		flush_tlb_range(vma, haddr, haddr + HPAGE_PMD_SIZE);

	/*
	 * Migrate the THP to the requested node, returns with page unlocked
	 * and access rights restored.
	 */
	spin_unlock(vmf->ptl);

	migrated = migrate_misplaced_transhuge_page(vma->vm_mm, vma,
				vmf->pmd, pmd, vmf->address, page, target_nid);
	if (migrated) {
		flags |= TNF_MIGRATED;
		page_nid = target_nid;
	} else
		flags |= TNF_MIGRATE_FAIL;

	goto out;
clear_pmdnuma:
	BUG_ON(!PageLocked(page));
	was_writable = pmd_savedwrite(pmd);
	pmd = pmd_modify(pmd, vma->vm_page_prot);
	pmd = pmd_mkyoung(pmd);
	if (was_writable)
		pmd = pmd_mkwrite(pmd);
	set_pmd_at(vma->vm_mm, haddr, vmf->pmd, pmd);
	update_mmu_cache_pmd(vma, vmf->address, vmf->pmd);
	unlock_page(page);
out_unlock:
	spin_unlock(vmf->ptl);

out:
	if (anon_vma)
		page_unlock_anon_vma_read(anon_vma);

	if (page_nid != -1)
		task_numa_fault(last_cpupid, page_nid, HPAGE_PMD_NR,
				flags);

	return 0;
}

/*
 * Return true if we do MADV_FREE successfully on entire pmd page.
 * Otherwise, return false.
 */
bool madvise_free_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		pmd_t *pmd, unsigned long addr, unsigned long next)
{
	spinlock_t *ptl;
	pmd_t orig_pmd;
	struct page *page;
	struct mm_struct *mm = tlb->mm;
	bool ret = false;

	tlb_remove_check_page_size_change(tlb, HPAGE_PMD_SIZE);

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (!ptl)
		goto out_unlocked;

	orig_pmd = *pmd;
	if (is_huge_zero_pmd(orig_pmd))
		goto out;

	if (unlikely(!pmd_present(orig_pmd))) {
		VM_BUG_ON(thp_migration_supported() &&
				  !is_pmd_migration_entry(orig_pmd));
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
		split_huge_page(page);
		unlock_page(page);
		put_page(page);
		goto out_unlocked;
	}

	if (PageDirty(page))
		ClearPageDirty(page);
	unlock_page(page);

	if (pmd_young(orig_pmd) || pmd_dirty(orig_pmd)) {
		pmdp_invalidate(vma, addr, pmd);
		orig_pmd = pmd_mkold(orig_pmd);
		orig_pmd = pmd_mkclean(orig_pmd);

		set_pmd_at(mm, addr, pmd, orig_pmd);
		tlb_remove_pmd_tlb_entry(tlb, pmd, addr);
	}

	mark_page_lazyfree(page);
	ret = true;
out:
	spin_unlock(ptl);
out_unlocked:
	return ret;
}

static inline void zap_deposited_table(struct mm_struct *mm, pmd_t *pmd)
{
	pgtable_t pgtable;

	pgtable = pgtable_trans_huge_withdraw(mm, pmd);
	pte_free(mm, pgtable);
	mm_dec_nr_ptes(mm);
}

int zap_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		 pmd_t *pmd, unsigned long addr)
{
	pmd_t orig_pmd;
	spinlock_t *ptl;

	tlb_remove_check_page_size_change(tlb, HPAGE_PMD_SIZE);

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
		if (arch_needs_pgtable_deposit())
			zap_deposited_table(tlb->mm, pmd);
		spin_unlock(ptl);
		if (is_huge_zero_pmd(orig_pmd))
			tlb_remove_page_size(tlb, pmd_page(orig_pmd), HPAGE_PMD_SIZE);
	} else if (is_huge_zero_pmd(orig_pmd)) {
		zap_deposited_table(tlb->mm, pmd);
		spin_unlock(ptl);
		tlb_remove_page_size(tlb, pmd_page(orig_pmd), HPAGE_PMD_SIZE);
	} else {
		struct page *page = NULL;
		int flush_needed = 1;

		if (pmd_present(orig_pmd)) {
			page = pmd_page(orig_pmd);
			page_remove_rmap(page, true);
			VM_BUG_ON_PAGE(page_mapcount(page) < 0, page);
			VM_BUG_ON_PAGE(!PageHead(page), page);
		} else if (thp_migration_supported()) {
			swp_entry_t entry;

			VM_BUG_ON(!is_pmd_migration_entry(orig_pmd));
			entry = pmd_to_swp_entry(orig_pmd);
			page = pfn_to_page(swp_offset(entry));
			flush_needed = 0;
		} else
			WARN_ONCE(1, "Non present huge pmd without pmd migration enabled!");

		if (PageAnon(page)) {
			zap_deposited_table(tlb->mm, pmd);
			add_mm_counter(tlb->mm, MM_ANONPAGES, -HPAGE_PMD_NR);
		} else {
			if (arch_needs_pgtable_deposit())
				zap_deposited_table(tlb->mm, pmd);
			add_mm_counter(tlb->mm, MM_FILEPAGES, -HPAGE_PMD_NR);
		}

		spin_unlock(ptl);
		if (flush_needed)
			tlb_remove_page_size(tlb, page, HPAGE_PMD_SIZE);
	}
	return 1;
}

#ifndef pmd_move_must_withdraw
static inline int pmd_move_must_withdraw(spinlock_t *new_pmd_ptl,
					 spinlock_t *old_pmd_ptl,
					 struct vm_area_struct *vma)
{
	/*
	 * With split pmd lock we also need to move preallocated
	 * PTE page table if new_pmd is on different PMD page table.
	 *
	 * We also don't deposit and withdraw tables for file pages.
	 */
	return (new_pmd_ptl != old_pmd_ptl) && vma_is_anonymous(vma);
}
#endif

static pmd_t move_soft_dirty_pmd(pmd_t pmd)
{
#ifdef CONFIG_MEM_SOFT_DIRTY
	if (unlikely(is_pmd_migration_entry(pmd)))
		pmd = pmd_swp_mksoft_dirty(pmd);
	else if (pmd_present(pmd))
		pmd = pmd_mksoft_dirty(pmd);
#endif
	return pmd;
}

bool move_huge_pmd(struct vm_area_struct *vma, unsigned long old_addr,
		  unsigned long new_addr, unsigned long old_end,
		  pmd_t *old_pmd, pmd_t *new_pmd, bool *need_flush)
{
	spinlock_t *old_ptl, *new_ptl;
	pmd_t pmd;
	struct mm_struct *mm = vma->vm_mm;
	bool force_flush = false;

	if ((old_addr & ~HPAGE_PMD_MASK) ||
	    (new_addr & ~HPAGE_PMD_MASK) ||
	    old_end - old_addr < HPAGE_PMD_SIZE)
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
		if (pmd_present(pmd) && pmd_dirty(pmd))
			force_flush = true;
		VM_BUG_ON(!pmd_none(*new_pmd));

		if (pmd_move_must_withdraw(new_ptl, old_ptl, vma)) {
			pgtable_t pgtable;
			pgtable = pgtable_trans_huge_withdraw(mm, old_pmd);
			pgtable_trans_huge_deposit(mm, new_pmd, pgtable);
		}
		pmd = move_soft_dirty_pmd(pmd);
		set_pmd_at(mm, new_addr, new_pmd, pmd);
		if (new_ptl != old_ptl)
			spin_unlock(new_ptl);
		if (force_flush)
			flush_tlb_range(vma, old_addr, old_addr + PMD_SIZE);
		else
			*need_flush = true;
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
	pmd_t entry;
	bool preserve_write;
	int ret;

	ptl = __pmd_trans_huge_lock(pmd, vma);
	if (!ptl)
		return 0;

	preserve_write = prot_numa && pmd_write(*pmd);
	ret = 1;

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
	if (is_swap_pmd(*pmd)) {
		swp_entry_t entry = pmd_to_swp_entry(*pmd);

		VM_BUG_ON(!is_pmd_migration_entry(*pmd));
		if (is_write_migration_entry(entry)) {
			pmd_t newpmd;
			/*
			 * A protection check is difficult so
			 * just be safe and disable write
			 */
			make_migration_entry_read(&entry);
			newpmd = swp_entry_to_pmd(entry);
			if (pmd_swp_soft_dirty(*pmd))
				newpmd = pmd_swp_mksoft_dirty(newpmd);
			set_pmd_at(mm, addr, pmd, newpmd);
		}
		goto unlock;
	}
#endif

	/*
	 * Avoid trapping faults against the zero page. The read-only
	 * data is likely to be read-cached on the local CPU and
	 * local/remote hits to the zero page are not interesting.
	 */
	if (prot_numa && is_huge_zero_pmd(*pmd))
		goto unlock;

	if (prot_numa && pmd_protnone(*pmd))
		goto unlock;

	/*
	 * In case prot_numa, we are under down_read(mmap_sem). It's critical
	 * to not clear pmd intermittently to avoid race with MADV_DONTNEED
	 * which is also under down_read(mmap_sem):
	 *
	 *	CPU0:				CPU1:
	 *				change_huge_pmd(prot_numa=1)
	 *				 pmdp_huge_get_and_clear_notify()
	 * madvise_dontneed()
	 *  zap_pmd_range()
	 *   pmd_trans_huge(*pmd) == 0 (without ptl)
	 *   // skip the pmd
	 *				 set_pmd_at();
	 *				 // pmd is re-established
	 *
	 * The race makes MADV_DONTNEED miss the huge pmd and don't clear it
	 * which may break userspace.
	 *
	 * pmdp_invalidate() is required to make sure we don't miss
	 * dirty/young flags set by hardware.
	 */
	entry = pmdp_invalidate(vma, addr, pmd);

	entry = pmd_modify(entry, newprot);
	if (preserve_write)
		entry = pmd_mk_savedwrite(entry);
	ret = HPAGE_PMD_NR;
	set_pmd_at(mm, addr, pmd, entry);
	BUG_ON(vma_is_anonymous(vma) && !preserve_write && pmd_write(entry));
unlock:
	spin_unlock(ptl);
	return ret;
}

/*
 * Returns page table lock pointer if a given pmd maps a thp, NULL otherwise.
 *
 * Note that if it returns page table lock pointer, this routine returns without
 * unlocking page table lock. So callers must unlock it.
 */
spinlock_t *__pmd_trans_huge_lock(pmd_t *pmd, struct vm_area_struct *vma)
{
	spinlock_t *ptl;
	ptl = pmd_lock(vma->vm_mm, pmd);
	if (likely(is_swap_pmd(*pmd) || pmd_trans_huge(*pmd) ||
			pmd_devmap(*pmd)))
		return ptl;
	spin_unlock(ptl);
	return NULL;
}

/*
 * Returns true if a given pud maps a thp, false otherwise.
 *
 * Note that if it returns true, this routine returns without unlocking page
 * table lock. So callers must unlock it.
 */
spinlock_t *__pud_trans_huge_lock(pud_t *pud, struct vm_area_struct *vma)
{
	spinlock_t *ptl;

	ptl = pud_lock(vma->vm_mm, pud);
	if (likely(pud_trans_huge(*pud) || pud_devmap(*pud)))
		return ptl;
	spin_unlock(ptl);
	return NULL;
}

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
int zap_huge_pud(struct mmu_gather *tlb, struct vm_area_struct *vma,
		 pud_t *pud, unsigned long addr)
{
	pud_t orig_pud;
	spinlock_t *ptl;

	ptl = __pud_trans_huge_lock(pud, vma);
	if (!ptl)
		return 0;
	/*
	 * For architectures like ppc64 we look at deposited pgtable
	 * when calling pudp_huge_get_and_clear. So do the
	 * pgtable_trans_huge_withdraw after finishing pudp related
	 * operations.
	 */
	orig_pud = pudp_huge_get_and_clear_full(tlb->mm, addr, pud,
			tlb->fullmm);
	tlb_remove_pud_tlb_entry(tlb, pud, addr);
	if (vma_is_dax(vma)) {
		spin_unlock(ptl);
		/* No zero page support yet */
	} else {
		/* No support for anonymous PUD pages yet */
		BUG();
	}
	return 1;
}

static void __split_huge_pud_locked(struct vm_area_struct *vma, pud_t *pud,
		unsigned long haddr)
{
	VM_BUG_ON(haddr & ~HPAGE_PUD_MASK);
	VM_BUG_ON_VMA(vma->vm_start > haddr, vma);
	VM_BUG_ON_VMA(vma->vm_end < haddr + HPAGE_PUD_SIZE, vma);
	VM_BUG_ON(!pud_trans_huge(*pud) && !pud_devmap(*pud));

	count_vm_event(THP_SPLIT_PUD);

	pudp_huge_clear_flush_notify(vma, haddr, pud);
}

void __split_huge_pud(struct vm_area_struct *vma, pud_t *pud,
		unsigned long address)
{
	spinlock_t *ptl;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long haddr = address & HPAGE_PUD_MASK;

	mmu_notifier_invalidate_range_start(mm, haddr, haddr + HPAGE_PUD_SIZE);
	ptl = pud_lock(mm, pud);
	if (unlikely(!pud_trans_huge(*pud) && !pud_devmap(*pud)))
		goto out;
	__split_huge_pud_locked(vma, pud, haddr);

out:
	spin_unlock(ptl);
	/*
	 * No need to double call mmu_notifier->invalidate_range() callback as
	 * the above pudp_huge_clear_flush_notify() did already call it.
	 */
	mmu_notifier_invalidate_range_only_end(mm, haddr, haddr +
					       HPAGE_PUD_SIZE);
}
#endif /* CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */

static void __split_huge_zero_page_pmd(struct vm_area_struct *vma,
		unsigned long haddr, pmd_t *pmd)
{
	struct mm_struct *mm = vma->vm_mm;
	pgtable_t pgtable;
	pmd_t _pmd;
	int i;

	/*
	 * Leave pmd empty until pte is filled note that it is fine to delay
	 * notification until mmu_notifier_invalidate_range_end() as we are
	 * replacing a zero pmd write protected page with a zero pte write
	 * protected page.
	 *
	 * See Documentation/vm/mmu_notifier.txt
	 */
	pmdp_huge_clear_flush(vma, haddr, pmd);

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
}

static void __split_huge_pmd_locked(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long haddr, bool freeze)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page *page;
	pgtable_t pgtable;
	pmd_t old_pmd, _pmd;
	bool young, write, soft_dirty, pmd_migration = false;
	unsigned long addr;
	int i;

	VM_BUG_ON(haddr & ~HPAGE_PMD_MASK);
	VM_BUG_ON_VMA(vma->vm_start > haddr, vma);
	VM_BUG_ON_VMA(vma->vm_end < haddr + HPAGE_PMD_SIZE, vma);
	VM_BUG_ON(!is_pmd_migration_entry(*pmd) && !pmd_trans_huge(*pmd)
				&& !pmd_devmap(*pmd));

	count_vm_event(THP_SPLIT_PMD);

	if (!vma_is_anonymous(vma)) {
		_pmd = pmdp_huge_clear_flush_notify(vma, haddr, pmd);
		/*
		 * We are going to unmap this huge page. So
		 * just go ahead and zap it
		 */
		if (arch_needs_pgtable_deposit())
			zap_deposited_table(mm, pmd);
		if (vma_is_dax(vma))
			return;
		page = pmd_page(_pmd);
		if (!PageReferenced(page) && pmd_young(_pmd))
			SetPageReferenced(page);
		page_remove_rmap(page, true);
		put_page(page);
		add_mm_counter(mm, MM_FILEPAGES, -HPAGE_PMD_NR);
		return;
	} else if (is_huge_zero_pmd(*pmd)) {
		/*
		 * FIXME: Do we want to invalidate secondary mmu by calling
		 * mmu_notifier_invalidate_range() see comments below inside
		 * __split_huge_pmd() ?
		 *
		 * We are going from a zero huge page write protected to zero
		 * small page also write protected so it does not seems useful
		 * to invalidate secondary mmu at this time.
		 */
		return __split_huge_zero_page_pmd(vma, haddr, pmd);
	}

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
	 * must remain set at all times on the pmd until the split is complete
	 * for this pmd), then we flush the SMP TLB and finally we write the
	 * non-huge version of the pmd entry with pmd_populate.
	 */
	old_pmd = pmdp_invalidate(vma, haddr, pmd);

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
	pmd_migration = is_pmd_migration_entry(old_pmd);
	if (pmd_migration) {
		swp_entry_t entry;

		entry = pmd_to_swp_entry(old_pmd);
		page = pfn_to_page(swp_offset(entry));
	} else
#endif
		page = pmd_page(old_pmd);
	VM_BUG_ON_PAGE(!page_count(page), page);
	page_ref_add(page, HPAGE_PMD_NR - 1);
	if (pmd_dirty(old_pmd))
		SetPageDirty(page);
	write = pmd_write(old_pmd);
	young = pmd_young(old_pmd);
	soft_dirty = pmd_soft_dirty(old_pmd);

	/*
	 * Withdraw the table only after we mark the pmd entry invalid.
	 * This's critical for some architectures (Power).
	 */
	pgtable = pgtable_trans_huge_withdraw(mm, pmd);
	pmd_populate(mm, &_pmd, pgtable);

	for (i = 0, addr = haddr; i < HPAGE_PMD_NR; i++, addr += PAGE_SIZE) {
		pte_t entry, *pte;
		/*
		 * Note that NUMA hinting access restrictions are not
		 * transferred to avoid any possibility of altering
		 * permissions across VMAs.
		 */
		if (freeze || pmd_migration) {
			swp_entry_t swp_entry;
			swp_entry = make_migration_entry(page + i, write);
			entry = swp_entry_to_pte(swp_entry);
			if (soft_dirty)
				entry = pte_swp_mksoft_dirty(entry);
		} else {
			entry = mk_pte(page + i, READ_ONCE(vma->vm_page_prot));
			entry = maybe_mkwrite(entry, vma);
			if (!write)
				entry = pte_wrprotect(entry);
			if (!young)
				entry = pte_mkold(entry);
			if (soft_dirty)
				entry = pte_mksoft_dirty(entry);
		}
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
		__dec_node_page_state(page, NR_ANON_THPS);
		if (TestClearPageDoubleMap(page)) {
			/* No need in mapcount reference anymore */
			for (i = 0; i < HPAGE_PMD_NR; i++)
				atomic_dec(&page[i]._mapcount);
		}
	}

	smp_wmb(); /* make pte visible before pmd */
	pmd_populate(mm, pmd, pgtable);

	if (freeze) {
		for (i = 0; i < HPAGE_PMD_NR; i++) {
			page_remove_rmap(page + i, false);
			put_page(page + i);
		}
	}
}

void __split_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long address, bool freeze, struct page *page)
{
	spinlock_t *ptl;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long haddr = address & HPAGE_PMD_MASK;

	mmu_notifier_invalidate_range_start(mm, haddr, haddr + HPAGE_PMD_SIZE);
	ptl = pmd_lock(mm, pmd);

	/*
	 * If caller asks to setup a migration entries, we need a page to check
	 * pmd against. Otherwise we can end up replacing wrong page.
	 */
	VM_BUG_ON(freeze && !page);
	if (page && page != pmd_page(*pmd))
	        goto out;

	if (pmd_trans_huge(*pmd)) {
		page = pmd_page(*pmd);
		if (PageMlocked(page))
			clear_page_mlock(page);
	} else if (!(pmd_devmap(*pmd) || is_pmd_migration_entry(*pmd)))
		goto out;
	__split_huge_pmd_locked(vma, pmd, haddr, freeze);
out:
	spin_unlock(ptl);
	/*
	 * No need to double call mmu_notifier->invalidate_range() callback.
	 * They are 3 cases to consider inside __split_huge_pmd_locked():
	 *  1) pmdp_huge_clear_flush_notify() call invalidate_range() obvious
	 *  2) __split_huge_zero_page_pmd() read only zero page and any write
	 *    fault will trigger a flush_notify before pointing to a new page
	 *    (it is fine if the secondary mmu keeps pointing to the old zero
	 *    page in the meantime)
	 *  3) Split a huge pmd into pte pointing to the same page. No need
	 *     to invalidate secondary tlb entry they are all still valid.
	 *     any further changes to individual pte will notify. So no need
	 *     to call mmu_notifier->invalidate_range()
	 */
	mmu_notifier_invalidate_range_only_end(mm, haddr, haddr +
					       HPAGE_PMD_SIZE);
}

void split_huge_pmd_address(struct vm_area_struct *vma, unsigned long address,
		bool freeze, struct page *page)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(vma->vm_mm, address);
	if (!pgd_present(*pgd))
		return;

	p4d = p4d_offset(pgd, address);
	if (!p4d_present(*p4d))
		return;

	pud = pud_offset(p4d, address);
	if (!pud_present(*pud))
		return;

	pmd = pmd_offset(pud, address);

	__split_huge_pmd(vma, pmd, address, freeze, page);
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
	enum ttu_flags ttu_flags = TTU_IGNORE_MLOCK | TTU_IGNORE_ACCESS |
		TTU_RMAP_LOCKED | TTU_SPLIT_HUGE_PMD;
	bool unmap_success;

	VM_BUG_ON_PAGE(!PageHead(page), page);

	if (PageAnon(page))
		ttu_flags |= TTU_SPLIT_FREEZE;

	unmap_success = try_to_unmap(page, ttu_flags);
	VM_BUG_ON_PAGE(!unmap_success, page);
}

static void unfreeze_page(struct page *page)
{
	int i;
	if (PageTransHuge(page)) {
		remove_migration_ptes(page, page, true);
	} else {
		for (i = 0; i < HPAGE_PMD_NR; i++)
			remove_migration_ptes(page + i, page + i, true);
	}
}

static void __split_huge_page_tail(struct page *head, int tail,
		struct lruvec *lruvec, struct list_head *list)
{
	struct page *page_tail = head + tail;

	VM_BUG_ON_PAGE(atomic_read(&page_tail->_mapcount) != -1, page_tail);

	/*
	 * Clone page flags before unfreezing refcount.
	 *
	 * After successful get_page_unless_zero() might follow flags change,
	 * for exmaple lock_page() which set PG_waiters.
	 */
	page_tail->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
	page_tail->flags |= (head->flags &
			((1L << PG_referenced) |
			 (1L << PG_swapbacked) |
			 (1L << PG_swapcache) |
			 (1L << PG_mlocked) |
			 (1L << PG_uptodate) |
			 (1L << PG_active) |
			 (1L << PG_locked) |
			 (1L << PG_unevictable) |
			 (1L << PG_dirty)));

	/* Page flags must be visible before we make the page non-compound. */
	smp_wmb();

	/*
	 * Clear PageTail before unfreezing page refcount.
	 *
	 * After successful get_page_unless_zero() might follow put_page()
	 * which needs correct compound_head().
	 */
	clear_compound_head(page_tail);

	/* Finally unfreeze refcount. Additional reference from page cache. */
	page_ref_unfreeze(page_tail, 1 + (!PageAnon(head) ||
					  PageSwapCache(head)));

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

	/*
	 * always add to the tail because some iterators expect new
	 * pages to show after the currently processed elements - e.g.
	 * migrate_pages
	 */
	lru_add_page_tail(head, page_tail, lruvec, list);
}

static void __split_huge_page(struct page *page, struct list_head *list,
		unsigned long flags)
{
	struct page *head = compound_head(page);
	struct zone *zone = page_zone(head);
	struct lruvec *lruvec;
	pgoff_t end = -1;
	int i;

	lruvec = mem_cgroup_page_lruvec(head, zone->zone_pgdat);

	/* complete memcg works before add pages to LRU */
	mem_cgroup_split_huge_fixup(head);

	if (!PageAnon(page))
		end = DIV_ROUND_UP(i_size_read(head->mapping->host), PAGE_SIZE);

	for (i = HPAGE_PMD_NR - 1; i >= 1; i--) {
		__split_huge_page_tail(head, i, lruvec, list);
		/* Some pages can be beyond i_size: drop them from page cache */
		if (head[i].index >= end) {
			__ClearPageDirty(head + i);
			__delete_from_page_cache(head + i, NULL);
			if (IS_ENABLED(CONFIG_SHMEM) && PageSwapBacked(head))
				shmem_uncharge(head->mapping->host, 1);
			put_page(head + i);
		}
	}

	ClearPageCompound(head);
	/* See comment in __split_huge_page_tail() */
	if (PageAnon(head)) {
		/* Additional pin to radix tree of swap cache */
		if (PageSwapCache(head))
			page_ref_add(head, 2);
		else
			page_ref_inc(head);
	} else {
		/* Additional pin to radix tree */
		page_ref_add(head, 2);
		xa_unlock(&head->mapping->i_pages);
	}

	spin_unlock_irqrestore(zone_lru_lock(page_zone(head)), flags);

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
	int i, compound, ret;

	VM_BUG_ON_PAGE(PageTail(page), page);

	if (likely(!PageCompound(page)))
		return atomic_read(&page->_mapcount) + 1;

	compound = compound_mapcount(page);
	if (PageHuge(page))
		return compound;
	ret = compound;
	for (i = 0; i < HPAGE_PMD_NR; i++)
		ret += atomic_read(&page[i]._mapcount) + 1;
	/* File pages has compound_mapcount included in _mapcount */
	if (!PageAnon(page))
		return ret - compound * HPAGE_PMD_NR;
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

/* Racy check whether the huge page can be split */
bool can_split_huge_page(struct page *page, int *pextra_pins)
{
	int extra_pins;

	/* Additional pins from radix tree */
	if (PageAnon(page))
		extra_pins = PageSwapCache(page) ? HPAGE_PMD_NR : 0;
	else
		extra_pins = HPAGE_PMD_NR;
	if (pextra_pins)
		*pextra_pins = extra_pins;
	return total_mapcount(page) == page_count(page) - extra_pins - 1;
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
	struct anon_vma *anon_vma = NULL;
	struct address_space *mapping = NULL;
	int count, mapcount, extra_pins, ret;
	bool mlocked;
	unsigned long flags;

	VM_BUG_ON_PAGE(is_huge_zero_page(page), page);
	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(!PageCompound(page), page);

	if (PageWriteback(page))
		return -EBUSY;

	if (PageAnon(head)) {
		/*
		 * The caller does not necessarily hold an mmap_sem that would
		 * prevent the anon_vma disappearing so we first we take a
		 * reference to it and then lock the anon_vma for write. This
		 * is similar to page_lock_anon_vma_read except the write lock
		 * is taken to serialise against parallel split or collapse
		 * operations.
		 */
		anon_vma = page_get_anon_vma(head);
		if (!anon_vma) {
			ret = -EBUSY;
			goto out;
		}
		mapping = NULL;
		anon_vma_lock_write(anon_vma);
	} else {
		mapping = head->mapping;

		/* Truncated ? */
		if (!mapping) {
			ret = -EBUSY;
			goto out;
		}

		anon_vma = NULL;
		i_mmap_lock_read(mapping);
	}

	/*
	 * Racy check if we can split the page, before freeze_page() will
	 * split PMDs
	 */
	if (!can_split_huge_page(head, &extra_pins)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	mlocked = PageMlocked(page);
	freeze_page(head);
	VM_BUG_ON_PAGE(compound_mapcount(head), head);

	/* Make sure the page is not on per-CPU pagevec as it takes pin */
	if (mlocked)
		lru_add_drain();

	/* prevent PageLRU to go away from under us, and freeze lru stats */
	spin_lock_irqsave(zone_lru_lock(page_zone(head)), flags);

	if (mapping) {
		void **pslot;

		xa_lock(&mapping->i_pages);
		pslot = radix_tree_lookup_slot(&mapping->i_pages,
				page_index(head));
		/*
		 * Check if the head page is present in radix tree.
		 * We assume all tail are present too, if head is there.
		 */
		if (radix_tree_deref_slot_protected(pslot,
					&mapping->i_pages.xa_lock) != head)
			goto fail;
	}

	/* Prevent deferred_split_scan() touching ->_refcount */
	spin_lock(&pgdata->split_queue_lock);
	count = page_count(head);
	mapcount = total_mapcount(head);
	if (!mapcount && page_ref_freeze(head, 1 + extra_pins)) {
		if (!list_empty(page_deferred_list(head))) {
			pgdata->split_queue_len--;
			list_del(page_deferred_list(head));
		}
		if (mapping)
			__dec_node_page_state(page, NR_SHMEM_THPS);
		spin_unlock(&pgdata->split_queue_lock);
		__split_huge_page(page, list, flags);
		if (PageSwapCache(head)) {
			swp_entry_t entry = { .val = page_private(head) };

			ret = split_swap_cluster(entry);
		} else
			ret = 0;
	} else {
		if (IS_ENABLED(CONFIG_DEBUG_VM) && mapcount) {
			pr_alert("total_mapcount: %u, page_count(): %u\n",
					mapcount, count);
			if (PageTail(page))
				dump_page(head, NULL);
			dump_page(page, "total_mapcount(head) > 0");
			BUG();
		}
		spin_unlock(&pgdata->split_queue_lock);
fail:		if (mapping)
			xa_unlock(&mapping->i_pages);
		spin_unlock_irqrestore(zone_lru_lock(page_zone(head)), flags);
		unfreeze_page(head);
		ret = -EBUSY;
	}

out_unlock:
	if (anon_vma) {
		anon_vma_unlock_write(anon_vma);
		put_anon_vma(anon_vma);
	}
	if (mapping)
		i_mmap_unlock_read(mapping);
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
	return READ_ONCE(pgdata->split_queue_len);
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
		if (!trylock_page(page))
			goto next;
		/* split_huge_page() removes page from list on success */
		if (!split_huge_page(page))
			split++;
		unlock_page(page);
next:
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

			if (!PageHead(page) || PageHuge(page) || !PageLRU(page))
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

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
void set_pmd_migration_entry(struct page_vma_mapped_walk *pvmw,
		struct page *page)
{
	struct vm_area_struct *vma = pvmw->vma;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address = pvmw->address;
	pmd_t pmdval;
	swp_entry_t entry;
	pmd_t pmdswp;

	if (!(pvmw->pmd && !pvmw->pte))
		return;

	mmu_notifier_invalidate_range_start(mm, address,
			address + HPAGE_PMD_SIZE);

	flush_cache_range(vma, address, address + HPAGE_PMD_SIZE);
	pmdval = *pvmw->pmd;
	pmdp_invalidate(vma, address, pvmw->pmd);
	if (pmd_dirty(pmdval))
		set_page_dirty(page);
	entry = make_migration_entry(page, pmd_write(pmdval));
	pmdswp = swp_entry_to_pmd(entry);
	if (pmd_soft_dirty(pmdval))
		pmdswp = pmd_swp_mksoft_dirty(pmdswp);
	set_pmd_at(mm, address, pvmw->pmd, pmdswp);
	page_remove_rmap(page, true);
	put_page(page);

	mmu_notifier_invalidate_range_end(mm, address,
			address + HPAGE_PMD_SIZE);
}

void remove_migration_pmd(struct page_vma_mapped_walk *pvmw, struct page *new)
{
	struct vm_area_struct *vma = pvmw->vma;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address = pvmw->address;
	unsigned long mmun_start = address & HPAGE_PMD_MASK;
	pmd_t pmde;
	swp_entry_t entry;

	if (!(pvmw->pmd && !pvmw->pte))
		return;

	entry = pmd_to_swp_entry(*pvmw->pmd);
	get_page(new);
	pmde = pmd_mkold(mk_huge_pmd(new, vma->vm_page_prot));
	if (pmd_swp_soft_dirty(*pvmw->pmd))
		pmde = pmd_mksoft_dirty(pmde);
	if (is_write_migration_entry(entry))
		pmde = maybe_pmd_mkwrite(pmde, vma);

	flush_cache_range(vma, mmun_start, mmun_start + HPAGE_PMD_SIZE);
	page_add_anon_rmap(new, vma, mmun_start, true);
	set_pmd_at(mm, mmun_start, pvmw->pmd, pmde);
	if (vma->vm_flags & VM_LOCKED)
		mlock_vma_page(new);
	update_mmu_cache_pmd(vma, address, pvmw->pmd);
}
#endif
