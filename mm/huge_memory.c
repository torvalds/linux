// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2009  Red Hat, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
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
#include <linux/backing-dev.h>
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
#include <linux/numa.h>
#include <linux/page_owner.h>
#include <linux/sched/sysctl.h>
#include <linux/memory-tiers.h>
#include <linux/compat.h>

#include <asm/tlb.h>
#include <asm/pgalloc.h>
#include "internal.h"
#include "swap.h"

#define CREATE_TRACE_POINTS
#include <trace/events/thp.h>

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
unsigned long huge_zero_pfn __read_mostly = ~0UL;

bool hugepage_vma_check(struct vm_area_struct *vma, unsigned long vm_flags,
			bool smaps, bool in_pf, bool enforce_sysfs)
{
	if (!vma->vm_mm)		/* vdso */
		return false;

	/*
	 * Explicitly disabled through madvise or prctl, or some
	 * architectures may disable THP for some mappings, for
	 * example, s390 kvm.
	 * */
	if ((vm_flags & VM_NOHUGEPAGE) ||
	    test_bit(MMF_DISABLE_THP, &vma->vm_mm->flags))
		return false;
	/*
	 * If the hardware/firmware marked hugepage support disabled.
	 */
	if (transparent_hugepage_flags & (1 << TRANSPARENT_HUGEPAGE_NEVER_DAX))
		return false;

	/* khugepaged doesn't collapse DAX vma, but page fault is fine. */
	if (vma_is_dax(vma))
		return in_pf;

	/*
	 * Special VMA and hugetlb VMA.
	 * Must be checked after dax since some dax mappings may have
	 * VM_MIXEDMAP set.
	 */
	if (vm_flags & VM_NO_KHUGEPAGED)
		return false;

	/*
	 * Check alignment for file vma and size for both file and anon vma.
	 *
	 * Skip the check for page fault. Huge fault does the check in fault
	 * handlers. And this check is not suitable for huge PUD fault.
	 */
	if (!in_pf &&
	    !transhuge_vma_suitable(vma, (vma->vm_end - HPAGE_PMD_SIZE)))
		return false;

	/*
	 * Enabled via shmem mount options or sysfs settings.
	 * Must be done before hugepage flags check since shmem has its
	 * own flags.
	 */
	if (!in_pf && shmem_file(vma->vm_file))
		return shmem_huge_enabled(vma, !enforce_sysfs);

	/* Enforce sysfs THP requirements as necessary */
	if (enforce_sysfs &&
	    (!hugepage_flags_enabled() || (!(vm_flags & VM_HUGEPAGE) &&
					   !hugepage_flags_always())))
		return false;

	/* Only regular file is valid */
	if (!in_pf && file_thp_enabled(vma))
		return true;

	if (!vma_is_anonymous(vma))
		return false;

	if (vma_is_temporary_stack(vma))
		return false;

	/*
	 * THPeligible bit of smaps should show 1 for proper VMAs even
	 * though anon_vma is not initialized yet.
	 *
	 * Allow page fault since anon_vma may be not initialized until
	 * the first page fault.
	 */
	if (!vma->anon_vma)
		return (smaps || in_pf);

	return true;
}

static bool get_huge_zero_page(void)
{
	struct page *zero_page;
retry:
	if (likely(atomic_inc_not_zero(&huge_zero_refcount)))
		return true;

	zero_page = alloc_pages((GFP_TRANSHUGE | __GFP_ZERO) & ~__GFP_MOVABLE,
			HPAGE_PMD_ORDER);
	if (!zero_page) {
		count_vm_event(THP_ZERO_PAGE_ALLOC_FAILED);
		return false;
	}
	preempt_disable();
	if (cmpxchg(&huge_zero_page, NULL, zero_page)) {
		preempt_enable();
		__free_pages(zero_page, compound_order(zero_page));
		goto retry;
	}
	WRITE_ONCE(huge_zero_pfn, page_to_pfn(zero_page));

	/* We take additional reference here. It will be put back by shrinker */
	atomic_set(&huge_zero_refcount, 2);
	preempt_enable();
	count_vm_event(THP_ZERO_PAGE_ALLOC);
	return true;
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
		WRITE_ONCE(huge_zero_pfn, ~0UL);
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
	const char *output;

	if (test_bit(TRANSPARENT_HUGEPAGE_FLAG, &transparent_hugepage_flags))
		output = "[always] madvise never";
	else if (test_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
			  &transparent_hugepage_flags))
		output = "always [madvise] never";
	else
		output = "always madvise [never]";

	return sysfs_emit(buf, "%s\n", output);
}

static ssize_t enabled_store(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	ssize_t ret = count;

	if (sysfs_streq(buf, "always")) {
		clear_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG, &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_FLAG, &transparent_hugepage_flags);
	} else if (sysfs_streq(buf, "madvise")) {
		clear_bit(TRANSPARENT_HUGEPAGE_FLAG, &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG, &transparent_hugepage_flags);
	} else if (sysfs_streq(buf, "never")) {
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

static struct kobj_attribute enabled_attr = __ATTR_RW(enabled);

ssize_t single_hugepage_flag_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf,
				  enum transparent_hugepage_flag flag)
{
	return sysfs_emit(buf, "%d\n",
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
	const char *output;

	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG,
		     &transparent_hugepage_flags))
		output = "[always] defer defer+madvise madvise never";
	else if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG,
			  &transparent_hugepage_flags))
		output = "always [defer] defer+madvise madvise never";
	else if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG,
			  &transparent_hugepage_flags))
		output = "always defer [defer+madvise] madvise never";
	else if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG,
			  &transparent_hugepage_flags))
		output = "always defer defer+madvise [madvise] never";
	else
		output = "always defer defer+madvise madvise [never]";

	return sysfs_emit(buf, "%s\n", output);
}

static ssize_t defrag_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	if (sysfs_streq(buf, "always")) {
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags);
	} else if (sysfs_streq(buf, "defer+madvise")) {
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, &transparent_hugepage_flags);
	} else if (sysfs_streq(buf, "defer")) {
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags);
	} else if (sysfs_streq(buf, "madvise")) {
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags);
	} else if (sysfs_streq(buf, "never")) {
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags);
	} else
		return -EINVAL;

	return count;
}
static struct kobj_attribute defrag_attr = __ATTR_RW(defrag);

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
static struct kobj_attribute use_zero_page_attr = __ATTR_RW(use_zero_page);

static ssize_t hpage_pmd_size_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lu\n", HPAGE_PMD_SIZE);
}
static struct kobj_attribute hpage_pmd_size_attr =
	__ATTR_RO(hpage_pmd_size);

static struct attribute *hugepage_attr[] = {
	&enabled_attr.attr,
	&defrag_attr.attr,
	&use_zero_page_attr.attr,
	&hpage_pmd_size_attr.attr,
#ifdef CONFIG_SHMEM
	&shmem_enabled_attr.attr,
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
		/*
		 * Hardware doesn't support hugepages, hence disable
		 * DAX PMD support.
		 */
		transparent_hugepage_flags = 1 << TRANSPARENT_HUGEPAGE_NEVER_DAX;
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

	err = register_shrinker(&huge_zero_page_shrinker, "thp-zero");
	if (err)
		goto err_hzp_shrinker;
	err = register_shrinker(&deferred_split_shrinker, "thp-deferred_split");
	if (err)
		goto err_split_shrinker;

	/*
	 * By default disable transparent hugepages on smaller systems,
	 * where the extra memory used could hurt more than TLB overhead
	 * is likely to save.  The admin can still enable it through /sys.
	 */
	if (totalram_pages() < (512 << (20 - PAGE_SHIFT))) {
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

#ifdef CONFIG_MEMCG
static inline struct deferred_split *get_deferred_split_queue(struct page *page)
{
	struct mem_cgroup *memcg = page_memcg(compound_head(page));
	struct pglist_data *pgdat = NODE_DATA(page_to_nid(page));

	if (memcg)
		return &memcg->deferred_split_queue;
	else
		return &pgdat->deferred_split_queue;
}
#else
static inline struct deferred_split *get_deferred_split_queue(struct page *page)
{
	struct pglist_data *pgdat = NODE_DATA(page_to_nid(page));

	return &pgdat->deferred_split_queue;
}
#endif

void prep_transhuge_page(struct page *page)
{
	/*
	 * we use page->mapping and page->index in second tail page
	 * as list_head: assuming THP order >= 2
	 */

	INIT_LIST_HEAD(page_deferred_list(page));
	set_compound_page_dtor(page, TRANSHUGE_PAGE_DTOR);
}

static inline bool is_transparent_hugepage(struct page *page)
{
	if (!PageCompound(page))
		return false;

	page = compound_head(page);
	return is_huge_zero_page(page) ||
	       page[1].compound_dtor == TRANSHUGE_PAGE_DTOR;
}

static unsigned long __thp_get_unmapped_area(struct file *filp,
		unsigned long addr, unsigned long len,
		loff_t off, unsigned long flags, unsigned long size)
{
	loff_t off_end = off + len;
	loff_t off_align = round_up(off, size);
	unsigned long len_pad, ret;

	if (!IS_ENABLED(CONFIG_64BIT) || in_compat_syscall())
		return 0;

	if (off_end <= off_align || (off_end - off_align) < size)
		return 0;

	len_pad = len + size;
	if (len_pad < len || (off + len_pad) < off)
		return 0;

	ret = current->mm->get_unmapped_area(filp, addr, len_pad,
					      off >> PAGE_SHIFT, flags);

	/*
	 * The failure might be due to length padding. The caller will retry
	 * without the padding.
	 */
	if (IS_ERR_VALUE(ret))
		return 0;

	/*
	 * Do not try to align to THP boundary if allocation at the address
	 * hint succeeds.
	 */
	if (ret == addr)
		return addr;

	ret += (off - ret) & (size - 1);
	return ret;
}

unsigned long thp_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	unsigned long ret;
	loff_t off = (loff_t)pgoff << PAGE_SHIFT;

	ret = __thp_get_unmapped_area(filp, addr, len, off, flags, PMD_SIZE);
	if (ret)
		return ret;

	return current->mm->get_unmapped_area(filp, addr, len, pgoff, flags);
}
EXPORT_SYMBOL_GPL(thp_get_unmapped_area);

static vm_fault_t __do_huge_pmd_anonymous_page(struct vm_fault *vmf,
			struct page *page, gfp_t gfp)
{
	struct vm_area_struct *vma = vmf->vma;
	pgtable_t pgtable;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;
	vm_fault_t ret = 0;

	VM_BUG_ON_PAGE(!PageCompound(page), page);

	if (mem_cgroup_charge(page_folio(page), vma->vm_mm, gfp)) {
		put_page(page);
		count_vm_event(THP_FAULT_FALLBACK);
		count_vm_event(THP_FAULT_FALLBACK_CHARGE);
		return VM_FAULT_FALLBACK;
	}
	cgroup_throttle_swaprate(page, gfp);

	pgtable = pte_alloc_one(vma->vm_mm);
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
			spin_unlock(vmf->ptl);
			put_page(page);
			pte_free(vma->vm_mm, pgtable);
			ret = handle_userfault(vmf, VM_UFFD_MISSING);
			VM_BUG_ON(ret & VM_FAULT_FALLBACK);
			return ret;
		}

		entry = mk_huge_pmd(page, vma->vm_page_prot);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		page_add_new_anon_rmap(page, vma, haddr);
		lru_cache_add_inactive_or_unevictable(page, vma);
		pgtable_trans_huge_deposit(vma->vm_mm, vmf->pmd, pgtable);
		set_pmd_at(vma->vm_mm, haddr, vmf->pmd, entry);
		update_mmu_cache_pmd(vma, vmf->address, vmf->pmd);
		add_mm_counter(vma->vm_mm, MM_ANONPAGES, HPAGE_PMD_NR);
		mm_inc_nr_ptes(vma->vm_mm);
		spin_unlock(vmf->ptl);
		count_vm_event(THP_FAULT_ALLOC);
		count_memcg_event_mm(vma->vm_mm, THP_FAULT_ALLOC);
	}

	return 0;
unlock_release:
	spin_unlock(vmf->ptl);
release:
	if (pgtable)
		pte_free(vma->vm_mm, pgtable);
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
gfp_t vma_thp_gfp_mask(struct vm_area_struct *vma)
{
	const bool vma_madvised = vma && (vma->vm_flags & VM_HUGEPAGE);

	/* Always do synchronous compaction */
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags))
		return GFP_TRANSHUGE | (vma_madvised ? 0 : __GFP_NORETRY);

	/* Kick kcompactd and fail quickly */
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags))
		return GFP_TRANSHUGE_LIGHT | __GFP_KSWAPD_RECLAIM;

	/* Synchronous compaction if madvised, otherwise kick kcompactd */
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, &transparent_hugepage_flags))
		return GFP_TRANSHUGE_LIGHT |
			(vma_madvised ? __GFP_DIRECT_RECLAIM :
					__GFP_KSWAPD_RECLAIM);

	/* Only do synchronous compaction if madvised */
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags))
		return GFP_TRANSHUGE_LIGHT |
		       (vma_madvised ? __GFP_DIRECT_RECLAIM : 0);

	return GFP_TRANSHUGE_LIGHT;
}

/* Caller must hold page table lock. */
static void set_huge_zero_page(pgtable_t pgtable, struct mm_struct *mm,
		struct vm_area_struct *vma, unsigned long haddr, pmd_t *pmd,
		struct page *zero_page)
{
	pmd_t entry;
	if (!pmd_none(*pmd))
		return;
	entry = mk_pmd(zero_page, vma->vm_page_prot);
	entry = pmd_mkhuge(entry);
	pgtable_trans_huge_deposit(mm, pmd, pgtable);
	set_pmd_at(mm, haddr, pmd, entry);
	mm_inc_nr_ptes(mm);
}

vm_fault_t do_huge_pmd_anonymous_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	gfp_t gfp;
	struct folio *folio;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;

	if (!transhuge_vma_suitable(vma, haddr))
		return VM_FAULT_FALLBACK;
	if (unlikely(anon_vma_prepare(vma)))
		return VM_FAULT_OOM;
	khugepaged_enter_vma(vma, vma->vm_flags);

	if (!(vmf->flags & FAULT_FLAG_WRITE) &&
			!mm_forbids_zeropage(vma->vm_mm) &&
			transparent_hugepage_use_zero_page()) {
		pgtable_t pgtable;
		struct page *zero_page;
		vm_fault_t ret;
		pgtable = pte_alloc_one(vma->vm_mm);
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
		if (pmd_none(*vmf->pmd)) {
			ret = check_stable_address_space(vma->vm_mm);
			if (ret) {
				spin_unlock(vmf->ptl);
				pte_free(vma->vm_mm, pgtable);
			} else if (userfaultfd_missing(vma)) {
				spin_unlock(vmf->ptl);
				pte_free(vma->vm_mm, pgtable);
				ret = handle_userfault(vmf, VM_UFFD_MISSING);
				VM_BUG_ON(ret & VM_FAULT_FALLBACK);
			} else {
				set_huge_zero_page(pgtable, vma->vm_mm, vma,
						   haddr, vmf->pmd, zero_page);
				update_mmu_cache_pmd(vma, vmf->address, vmf->pmd);
				spin_unlock(vmf->ptl);
			}
		} else {
			spin_unlock(vmf->ptl);
			pte_free(vma->vm_mm, pgtable);
		}
		return ret;
	}
	gfp = vma_thp_gfp_mask(vma);
	folio = vma_alloc_folio(gfp, HPAGE_PMD_ORDER, vma, haddr, true);
	if (unlikely(!folio)) {
		count_vm_event(THP_FAULT_FALLBACK);
		return VM_FAULT_FALLBACK;
	}
	return __do_huge_pmd_anonymous_page(vmf, &folio->page, gfp);
}

static void insert_pfn_pmd(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd, pfn_t pfn, pgprot_t prot, bool write,
		pgtable_t pgtable)
{
	struct mm_struct *mm = vma->vm_mm;
	pmd_t entry;
	spinlock_t *ptl;

	ptl = pmd_lock(mm, pmd);
	if (!pmd_none(*pmd)) {
		if (write) {
			if (pmd_pfn(*pmd) != pfn_t_to_pfn(pfn)) {
				WARN_ON_ONCE(!is_huge_zero_pmd(*pmd));
				goto out_unlock;
			}
			entry = pmd_mkyoung(*pmd);
			entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
			if (pmdp_set_access_flags(vma, addr, pmd, entry, 1))
				update_mmu_cache_pmd(vma, addr, pmd);
		}

		goto out_unlock;
	}

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
		pgtable = NULL;
	}

	set_pmd_at(mm, addr, pmd, entry);
	update_mmu_cache_pmd(vma, addr, pmd);

out_unlock:
	spin_unlock(ptl);
	if (pgtable)
		pte_free(mm, pgtable);
}

/**
 * vmf_insert_pfn_pmd_prot - insert a pmd size pfn
 * @vmf: Structure describing the fault
 * @pfn: pfn to insert
 * @pgprot: page protection to use
 * @write: whether it's a write fault
 *
 * Insert a pmd size pfn. See vmf_insert_pfn() for additional info and
 * also consult the vmf_insert_mixed_prot() documentation when
 * @pgprot != @vmf->vma->vm_page_prot.
 *
 * Return: vm_fault_t value.
 */
vm_fault_t vmf_insert_pfn_pmd_prot(struct vm_fault *vmf, pfn_t pfn,
				   pgprot_t pgprot, bool write)
{
	unsigned long addr = vmf->address & PMD_MASK;
	struct vm_area_struct *vma = vmf->vma;
	pgtable_t pgtable = NULL;

	/*
	 * If we had pmd_special, we could avoid all these restrictions,
	 * but we need to be consistent with PTEs and architectures that
	 * can't support a 'special' bit.
	 */
	BUG_ON(!(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)) &&
			!pfn_t_devmap(pfn));
	BUG_ON((vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)) ==
						(VM_PFNMAP|VM_MIXEDMAP));
	BUG_ON((vma->vm_flags & VM_PFNMAP) && is_cow_mapping(vma->vm_flags));

	if (addr < vma->vm_start || addr >= vma->vm_end)
		return VM_FAULT_SIGBUS;

	if (arch_needs_pgtable_deposit()) {
		pgtable = pte_alloc_one(vma->vm_mm);
		if (!pgtable)
			return VM_FAULT_OOM;
	}

	track_pfn_insert(vma, &pgprot, pfn);

	insert_pfn_pmd(vma, addr, vmf->pmd, pfn, pgprot, write, pgtable);
	return VM_FAULT_NOPAGE;
}
EXPORT_SYMBOL_GPL(vmf_insert_pfn_pmd_prot);

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
	if (!pud_none(*pud)) {
		if (write) {
			if (pud_pfn(*pud) != pfn_t_to_pfn(pfn)) {
				WARN_ON_ONCE(!is_huge_zero_pud(*pud));
				goto out_unlock;
			}
			entry = pud_mkyoung(*pud);
			entry = maybe_pud_mkwrite(pud_mkdirty(entry), vma);
			if (pudp_set_access_flags(vma, addr, pud, entry, 1))
				update_mmu_cache_pud(vma, addr, pud);
		}
		goto out_unlock;
	}

	entry = pud_mkhuge(pfn_t_pud(pfn, prot));
	if (pfn_t_devmap(pfn))
		entry = pud_mkdevmap(entry);
	if (write) {
		entry = pud_mkyoung(pud_mkdirty(entry));
		entry = maybe_pud_mkwrite(entry, vma);
	}
	set_pud_at(mm, addr, pud, entry);
	update_mmu_cache_pud(vma, addr, pud);

out_unlock:
	spin_unlock(ptl);
}

/**
 * vmf_insert_pfn_pud_prot - insert a pud size pfn
 * @vmf: Structure describing the fault
 * @pfn: pfn to insert
 * @pgprot: page protection to use
 * @write: whether it's a write fault
 *
 * Insert a pud size pfn. See vmf_insert_pfn() for additional info and
 * also consult the vmf_insert_mixed_prot() documentation when
 * @pgprot != @vmf->vma->vm_page_prot.
 *
 * Return: vm_fault_t value.
 */
vm_fault_t vmf_insert_pfn_pud_prot(struct vm_fault *vmf, pfn_t pfn,
				   pgprot_t pgprot, bool write)
{
	unsigned long addr = vmf->address & PUD_MASK;
	struct vm_area_struct *vma = vmf->vma;

	/*
	 * If we had pud_special, we could avoid all these restrictions,
	 * but we need to be consistent with PTEs and architectures that
	 * can't support a 'special' bit.
	 */
	BUG_ON(!(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)) &&
			!pfn_t_devmap(pfn));
	BUG_ON((vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)) ==
						(VM_PFNMAP|VM_MIXEDMAP));
	BUG_ON((vma->vm_flags & VM_PFNMAP) && is_cow_mapping(vma->vm_flags));

	if (addr < vma->vm_start || addr >= vma->vm_end)
		return VM_FAULT_SIGBUS;

	track_pfn_insert(vma, &pgprot, pfn);

	insert_pfn_pud(vma, addr, vmf->pud, pfn, pgprot, write);
	return VM_FAULT_NOPAGE;
}
EXPORT_SYMBOL_GPL(vmf_insert_pfn_pud_prot);
#endif /* CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */

static void touch_pmd(struct vm_area_struct *vma, unsigned long addr,
		      pmd_t *pmd, bool write)
{
	pmd_t _pmd;

	_pmd = pmd_mkyoung(*pmd);
	if (write)
		_pmd = pmd_mkdirty(_pmd);
	if (pmdp_set_access_flags(vma, addr & HPAGE_PMD_MASK,
				  pmd, _pmd, write))
		update_mmu_cache_pmd(vma, addr, pmd);
}

struct page *follow_devmap_pmd(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd, int flags, struct dev_pagemap **pgmap)
{
	unsigned long pfn = pmd_pfn(*pmd);
	struct mm_struct *mm = vma->vm_mm;
	struct page *page;

	assert_spin_locked(pmd_lockptr(mm, pmd));

	/* FOLL_GET and FOLL_PIN are mutually exclusive. */
	if (WARN_ON_ONCE((flags & (FOLL_PIN | FOLL_GET)) ==
			 (FOLL_PIN | FOLL_GET)))
		return NULL;

	if (flags & FOLL_WRITE && !pmd_write(*pmd))
		return NULL;

	if (pmd_present(*pmd) && pmd_devmap(*pmd))
		/* pass */;
	else
		return NULL;

	if (flags & FOLL_TOUCH)
		touch_pmd(vma, addr, pmd, flags & FOLL_WRITE);

	/*
	 * device mapped pages can only be returned if the
	 * caller will manage the page reference count.
	 */
	if (!(flags & (FOLL_GET | FOLL_PIN)))
		return ERR_PTR(-EEXIST);

	pfn += (addr & ~PMD_MASK) >> PAGE_SHIFT;
	*pgmap = get_dev_pagemap(pfn, *pgmap);
	if (!*pgmap)
		return ERR_PTR(-EFAULT);
	page = pfn_to_page(pfn);
	if (!try_grab_page(page, flags))
		page = ERR_PTR(-ENOMEM);

	return page;
}

int copy_huge_pmd(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		  pmd_t *dst_pmd, pmd_t *src_pmd, unsigned long addr,
		  struct vm_area_struct *dst_vma, struct vm_area_struct *src_vma)
{
	spinlock_t *dst_ptl, *src_ptl;
	struct page *src_page;
	pmd_t pmd;
	pgtable_t pgtable = NULL;
	int ret = -ENOMEM;

	/* Skip if can be re-fill on fault */
	if (!vma_is_anonymous(dst_vma))
		return 0;

	pgtable = pte_alloc_one(dst_mm);
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
		if (!is_readable_migration_entry(entry)) {
			entry = make_readable_migration_entry(
							swp_offset(entry));
			pmd = swp_entry_to_pmd(entry);
			if (pmd_swp_soft_dirty(*src_pmd))
				pmd = pmd_swp_mksoft_dirty(pmd);
			if (pmd_swp_uffd_wp(*src_pmd))
				pmd = pmd_swp_mkuffd_wp(pmd);
			set_pmd_at(src_mm, addr, src_pmd, pmd);
		}
		add_mm_counter(dst_mm, MM_ANONPAGES, HPAGE_PMD_NR);
		mm_inc_nr_ptes(dst_mm);
		pgtable_trans_huge_deposit(dst_mm, dst_pmd, pgtable);
		if (!userfaultfd_wp(dst_vma))
			pmd = pmd_swp_clear_uffd_wp(pmd);
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
		/*
		 * get_huge_zero_page() will never allocate a new page here,
		 * since we already have a zero page to copy. It just takes a
		 * reference.
		 */
		mm_get_huge_zero_page(dst_mm);
		goto out_zero_page;
	}

	src_page = pmd_page(pmd);
	VM_BUG_ON_PAGE(!PageHead(src_page), src_page);

	get_page(src_page);
	if (unlikely(page_try_dup_anon_rmap(src_page, true, src_vma))) {
		/* Page maybe pinned: split and retry the fault on PTEs. */
		put_page(src_page);
		pte_free(dst_mm, pgtable);
		spin_unlock(src_ptl);
		spin_unlock(dst_ptl);
		__split_huge_pmd(src_vma, src_pmd, addr, false, NULL);
		return -EAGAIN;
	}
	add_mm_counter(dst_mm, MM_ANONPAGES, HPAGE_PMD_NR);
out_zero_page:
	mm_inc_nr_ptes(dst_mm);
	pgtable_trans_huge_deposit(dst_mm, dst_pmd, pgtable);
	pmdp_set_wrprotect(src_mm, addr, src_pmd);
	if (!userfaultfd_wp(dst_vma))
		pmd = pmd_clear_uffd_wp(pmd);
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
		      pud_t *pud, bool write)
{
	pud_t _pud;

	_pud = pud_mkyoung(*pud);
	if (write)
		_pud = pud_mkdirty(_pud);
	if (pudp_set_access_flags(vma, addr & HPAGE_PUD_MASK,
				  pud, _pud, write))
		update_mmu_cache_pud(vma, addr, pud);
}

struct page *follow_devmap_pud(struct vm_area_struct *vma, unsigned long addr,
		pud_t *pud, int flags, struct dev_pagemap **pgmap)
{
	unsigned long pfn = pud_pfn(*pud);
	struct mm_struct *mm = vma->vm_mm;
	struct page *page;

	assert_spin_locked(pud_lockptr(mm, pud));

	if (flags & FOLL_WRITE && !pud_write(*pud))
		return NULL;

	/* FOLL_GET and FOLL_PIN are mutually exclusive. */
	if (WARN_ON_ONCE((flags & (FOLL_PIN | FOLL_GET)) ==
			 (FOLL_PIN | FOLL_GET)))
		return NULL;

	if (pud_present(*pud) && pud_devmap(*pud))
		/* pass */;
	else
		return NULL;

	if (flags & FOLL_TOUCH)
		touch_pud(vma, addr, pud, flags & FOLL_WRITE);

	/*
	 * device mapped pages can only be returned if the
	 * caller will manage the page reference count.
	 *
	 * At least one of FOLL_GET | FOLL_PIN must be set, so assert that here:
	 */
	if (!(flags & (FOLL_GET | FOLL_PIN)))
		return ERR_PTR(-EEXIST);

	pfn += (addr & ~PUD_MASK) >> PAGE_SHIFT;
	*pgmap = get_dev_pagemap(pfn, *pgmap);
	if (!*pgmap)
		return ERR_PTR(-EFAULT);
	page = pfn_to_page(pfn);
	if (!try_grab_page(page, flags))
		page = ERR_PTR(-ENOMEM);

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

	/*
	 * TODO: once we support anonymous pages, use page_try_dup_anon_rmap()
	 * and split if duplicating fails.
	 */
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
	bool write = vmf->flags & FAULT_FLAG_WRITE;

	vmf->ptl = pud_lock(vmf->vma->vm_mm, vmf->pud);
	if (unlikely(!pud_same(*vmf->pud, orig_pud)))
		goto unlock;

	touch_pud(vmf->vma, vmf->address, vmf->pud, write);
unlock:
	spin_unlock(vmf->ptl);
}
#endif /* CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */

void huge_pmd_set_accessed(struct vm_fault *vmf)
{
	bool write = vmf->flags & FAULT_FLAG_WRITE;

	vmf->ptl = pmd_lock(vmf->vma->vm_mm, vmf->pmd);
	if (unlikely(!pmd_same(*vmf->pmd, vmf->orig_pmd)))
		goto unlock;

	touch_pmd(vmf->vma, vmf->address, vmf->pmd, write);

unlock:
	spin_unlock(vmf->ptl);
}

vm_fault_t do_huge_pmd_wp_page(struct vm_fault *vmf)
{
	const bool unshare = vmf->flags & FAULT_FLAG_UNSHARE;
	struct vm_area_struct *vma = vmf->vma;
	struct folio *folio;
	struct page *page;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;
	pmd_t orig_pmd = vmf->orig_pmd;

	vmf->ptl = pmd_lockptr(vma->vm_mm, vmf->pmd);
	VM_BUG_ON_VMA(!vma->anon_vma, vma);

	VM_BUG_ON(unshare && (vmf->flags & FAULT_FLAG_WRITE));
	VM_BUG_ON(!unshare && !(vmf->flags & FAULT_FLAG_WRITE));

	if (is_huge_zero_pmd(orig_pmd))
		goto fallback;

	spin_lock(vmf->ptl);

	if (unlikely(!pmd_same(*vmf->pmd, orig_pmd))) {
		spin_unlock(vmf->ptl);
		return 0;
	}

	page = pmd_page(orig_pmd);
	folio = page_folio(page);
	VM_BUG_ON_PAGE(!PageHead(page), page);

	/* Early check when only holding the PT lock. */
	if (PageAnonExclusive(page))
		goto reuse;

	if (!folio_trylock(folio)) {
		folio_get(folio);
		spin_unlock(vmf->ptl);
		folio_lock(folio);
		spin_lock(vmf->ptl);
		if (unlikely(!pmd_same(*vmf->pmd, orig_pmd))) {
			spin_unlock(vmf->ptl);
			folio_unlock(folio);
			folio_put(folio);
			return 0;
		}
		folio_put(folio);
	}

	/* Recheck after temporarily dropping the PT lock. */
	if (PageAnonExclusive(page)) {
		folio_unlock(folio);
		goto reuse;
	}

	/*
	 * See do_wp_page(): we can only reuse the folio exclusively if
	 * there are no additional references. Note that we always drain
	 * the LRU pagevecs immediately after adding a THP.
	 */
	if (folio_ref_count(folio) >
			1 + folio_test_swapcache(folio) * folio_nr_pages(folio))
		goto unlock_fallback;
	if (folio_test_swapcache(folio))
		folio_free_swap(folio);
	if (folio_ref_count(folio) == 1) {
		pmd_t entry;

		page_move_anon_rmap(page, vma);
		folio_unlock(folio);
reuse:
		if (unlikely(unshare)) {
			spin_unlock(vmf->ptl);
			return 0;
		}
		entry = pmd_mkyoung(orig_pmd);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		if (pmdp_set_access_flags(vma, haddr, vmf->pmd, entry, 1))
			update_mmu_cache_pmd(vma, vmf->address, vmf->pmd);
		spin_unlock(vmf->ptl);
		return VM_FAULT_WRITE;
	}

unlock_fallback:
	folio_unlock(folio);
	spin_unlock(vmf->ptl);
fallback:
	__split_huge_pmd(vma, vmf->pmd, vmf->address, false, NULL);
	return VM_FAULT_FALLBACK;
}

/* FOLL_FORCE can write to even unwritable PMDs in COW mappings. */
static inline bool can_follow_write_pmd(pmd_t pmd, struct page *page,
					struct vm_area_struct *vma,
					unsigned int flags)
{
	/* If the pmd is writable, we can write to the page. */
	if (pmd_write(pmd))
		return true;

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
	if (!page || !PageAnon(page) || !PageAnonExclusive(page))
		return false;

	/* ... and a write-fault isn't required for other reasons. */
	if (vma_soft_dirty_enabled(vma) && !pmd_soft_dirty(pmd))
		return false;
	return !userfaultfd_huge_pmd_wp(vma, pmd);
}

struct page *follow_trans_huge_pmd(struct vm_area_struct *vma,
				   unsigned long addr,
				   pmd_t *pmd,
				   unsigned int flags)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page *page;

	assert_spin_locked(pmd_lockptr(mm, pmd));

	page = pmd_page(*pmd);
	VM_BUG_ON_PAGE(!PageHead(page) && !is_zone_device_page(page), page);

	if ((flags & FOLL_WRITE) &&
	    !can_follow_write_pmd(*pmd, page, vma, flags))
		return NULL;

	/* Avoid dumping huge zero page */
	if ((flags & FOLL_DUMP) && is_huge_zero_pmd(*pmd))
		return ERR_PTR(-EFAULT);

	/* Full NUMA hinting faults to serialise migration in fault paths */
	if (pmd_protnone(*pmd) && !gup_can_follow_protnone(flags))
		return NULL;

	if (!pmd_write(*pmd) && gup_must_unshare(flags, page))
		return ERR_PTR(-EMLINK);

	VM_BUG_ON_PAGE((flags & FOLL_PIN) && PageAnon(page) &&
			!PageAnonExclusive(page), page);

	if (!try_grab_page(page, flags))
		return ERR_PTR(-ENOMEM);

	if (flags & FOLL_TOUCH)
		touch_pmd(vma, addr, pmd, flags & FOLL_WRITE);

	page += (addr & ~HPAGE_PMD_MASK) >> PAGE_SHIFT;
	VM_BUG_ON_PAGE(!PageCompound(page) && !is_zone_device_page(page), page);

	return page;
}

/* NUMA hinting page fault entry point for trans huge pmds */
vm_fault_t do_huge_pmd_numa_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	pmd_t oldpmd = vmf->orig_pmd;
	pmd_t pmd;
	struct page *page;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;
	int page_nid = NUMA_NO_NODE;
	int target_nid, last_cpupid = (-1 & LAST_CPUPID_MASK);
	bool migrated = false;
	bool was_writable = pmd_savedwrite(oldpmd);
	int flags = 0;

	vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
	if (unlikely(!pmd_same(oldpmd, *vmf->pmd))) {
		spin_unlock(vmf->ptl);
		return 0;
	}

	pmd = pmd_modify(oldpmd, vma->vm_page_prot);
	page = vm_normal_page_pmd(vma, haddr, pmd);
	if (!page)
		goto out_map;

	/* See similar comment in do_numa_page for explanation */
	if (!was_writable)
		flags |= TNF_NO_GROUP;

	page_nid = page_to_nid(page);
	/*
	 * For memory tiering mode, cpupid of slow memory page is used
	 * to record page access time.  So use default value.
	 */
	if (node_is_toptier(page_nid))
		last_cpupid = page_cpupid_last(page);
	target_nid = numa_migrate_prep(page, vma, haddr, page_nid,
				       &flags);

	if (target_nid == NUMA_NO_NODE) {
		put_page(page);
		goto out_map;
	}

	spin_unlock(vmf->ptl);

	migrated = migrate_misplaced_page(page, vma, target_nid);
	if (migrated) {
		flags |= TNF_MIGRATED;
		page_nid = target_nid;
		task_numa_fault(last_cpupid, page_nid, HPAGE_PMD_NR, flags);
		return 0;
	}

	flags |= TNF_MIGRATE_FAIL;
	vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
	if (unlikely(!pmd_same(oldpmd, *vmf->pmd))) {
		spin_unlock(vmf->ptl);
		return 0;
	}
out_map:
	/* Restore the PMD */
	pmd = pmd_modify(oldpmd, vma->vm_page_prot);
	pmd = pmd_mkyoung(pmd);
	if (was_writable)
		pmd = pmd_mkwrite(pmd);
	set_pmd_at(vma->vm_mm, haddr, vmf->pmd, pmd);
	update_mmu_cache_pmd(vma, vmf->address, vmf->pmd);
	spin_unlock(vmf->ptl);

	if (page_nid != NUMA_NO_NODE)
		task_numa_fault(last_cpupid, page_nid, HPAGE_PMD_NR, flags);
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

	tlb_change_page_size(tlb, HPAGE_PMD_SIZE);

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
	if (total_mapcount(page) != 1)
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

	tlb_change_page_size(tlb, HPAGE_PMD_SIZE);

	ptl = __pmd_trans_huge_lock(pmd, vma);
	if (!ptl)
		return 0;
	/*
	 * For architectures like ppc64 we look at deposited pgtable
	 * when calling pmdp_huge_get_and_clear. So do the
	 * pgtable_trans_huge_withdraw after finishing pmdp related
	 * operations.
	 */
	orig_pmd = pmdp_huge_get_and_clear_full(vma, addr, pmd,
						tlb->fullmm);
	tlb_remove_pmd_tlb_entry(tlb, pmd, addr);
	if (vma_is_special_huge(vma)) {
		if (arch_needs_pgtable_deposit())
			zap_deposited_table(tlb->mm, pmd);
		spin_unlock(ptl);
	} else if (is_huge_zero_pmd(orig_pmd)) {
		zap_deposited_table(tlb->mm, pmd);
		spin_unlock(ptl);
	} else {
		struct page *page = NULL;
		int flush_needed = 1;

		if (pmd_present(orig_pmd)) {
			page = pmd_page(orig_pmd);
			page_remove_rmap(page, vma, true);
			VM_BUG_ON_PAGE(page_mapcount(page) < 0, page);
			VM_BUG_ON_PAGE(!PageHead(page), page);
		} else if (thp_migration_supported()) {
			swp_entry_t entry;

			VM_BUG_ON(!is_pmd_migration_entry(orig_pmd));
			entry = pmd_to_swp_entry(orig_pmd);
			page = pfn_swap_entry_to_page(entry);
			flush_needed = 0;
		} else
			WARN_ONCE(1, "Non present huge pmd without pmd migration enabled!");

		if (PageAnon(page)) {
			zap_deposited_table(tlb->mm, pmd);
			add_mm_counter(tlb->mm, MM_ANONPAGES, -HPAGE_PMD_NR);
		} else {
			if (arch_needs_pgtable_deposit())
				zap_deposited_table(tlb->mm, pmd);
			add_mm_counter(tlb->mm, mm_counter_file(page), -HPAGE_PMD_NR);
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
		  unsigned long new_addr, pmd_t *old_pmd, pmd_t *new_pmd)
{
	spinlock_t *old_ptl, *new_ptl;
	pmd_t pmd;
	struct mm_struct *mm = vma->vm_mm;
	bool force_flush = false;

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
	 * ptlocks because exclusive mmap_lock prevents deadlock.
	 */
	old_ptl = __pmd_trans_huge_lock(old_pmd, vma);
	if (old_ptl) {
		new_ptl = pmd_lockptr(mm, new_pmd);
		if (new_ptl != old_ptl)
			spin_lock_nested(new_ptl, SINGLE_DEPTH_NESTING);
		pmd = pmdp_huge_get_and_clear(mm, old_addr, old_pmd);
		if (pmd_present(pmd))
			force_flush = true;
		VM_BUG_ON(!pmd_none(*new_pmd));

		if (pmd_move_must_withdraw(new_ptl, old_ptl, vma)) {
			pgtable_t pgtable;
			pgtable = pgtable_trans_huge_withdraw(mm, old_pmd);
			pgtable_trans_huge_deposit(mm, new_pmd, pgtable);
		}
		pmd = move_soft_dirty_pmd(pmd);
		set_pmd_at(mm, new_addr, new_pmd, pmd);
		if (force_flush)
			flush_pmd_tlb_range(vma, old_addr, old_addr + PMD_SIZE);
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
 *  - 1 if PMD was locked but protections unchanged and TLB flush unnecessary
 *      or if prot_numa but THP migration is not supported
 *  - HPAGE_PMD_NR if protections changed and TLB flush necessary
 */
int change_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		    pmd_t *pmd, unsigned long addr, pgprot_t newprot,
		    unsigned long cp_flags)
{
	struct mm_struct *mm = vma->vm_mm;
	spinlock_t *ptl;
	pmd_t oldpmd, entry;
	bool preserve_write;
	int ret;
	bool prot_numa = cp_flags & MM_CP_PROT_NUMA;
	bool uffd_wp = cp_flags & MM_CP_UFFD_WP;
	bool uffd_wp_resolve = cp_flags & MM_CP_UFFD_WP_RESOLVE;

	tlb_change_page_size(tlb, HPAGE_PMD_SIZE);

	if (prot_numa && !thp_migration_supported())
		return 1;

	ptl = __pmd_trans_huge_lock(pmd, vma);
	if (!ptl)
		return 0;

	preserve_write = prot_numa && pmd_write(*pmd);
	ret = 1;

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
	if (is_swap_pmd(*pmd)) {
		swp_entry_t entry = pmd_to_swp_entry(*pmd);
		struct page *page = pfn_swap_entry_to_page(entry);
		pmd_t newpmd;

		VM_BUG_ON(!is_pmd_migration_entry(*pmd));
		if (is_writable_migration_entry(entry)) {
			/*
			 * A protection check is difficult so
			 * just be safe and disable write
			 */
			if (PageAnon(page))
				entry = make_readable_exclusive_migration_entry(swp_offset(entry));
			else
				entry = make_readable_migration_entry(swp_offset(entry));
			newpmd = swp_entry_to_pmd(entry);
			if (pmd_swp_soft_dirty(*pmd))
				newpmd = pmd_swp_mksoft_dirty(newpmd);
			if (pmd_swp_uffd_wp(*pmd))
				newpmd = pmd_swp_mkuffd_wp(newpmd);
		} else {
			newpmd = *pmd;
		}

		if (uffd_wp)
			newpmd = pmd_swp_mkuffd_wp(newpmd);
		else if (uffd_wp_resolve)
			newpmd = pmd_swp_clear_uffd_wp(newpmd);
		if (!pmd_same(*pmd, newpmd))
			set_pmd_at(mm, addr, pmd, newpmd);
		goto unlock;
	}
#endif

	if (prot_numa) {
		struct page *page;
		bool toptier;
		/*
		 * Avoid trapping faults against the zero page. The read-only
		 * data is likely to be read-cached on the local CPU and
		 * local/remote hits to the zero page are not interesting.
		 */
		if (is_huge_zero_pmd(*pmd))
			goto unlock;

		if (pmd_protnone(*pmd))
			goto unlock;

		page = pmd_page(*pmd);
		toptier = node_is_toptier(page_to_nid(page));
		/*
		 * Skip scanning top tier node if normal numa
		 * balancing is disabled
		 */
		if (!(sysctl_numa_balancing_mode & NUMA_BALANCING_NORMAL) &&
		    toptier)
			goto unlock;

		if (sysctl_numa_balancing_mode & NUMA_BALANCING_MEMORY_TIERING &&
		    !toptier)
			xchg_page_access_time(page, jiffies_to_msecs(jiffies));
	}
	/*
	 * In case prot_numa, we are under mmap_read_lock(mm). It's critical
	 * to not clear pmd intermittently to avoid race with MADV_DONTNEED
	 * which is also under mmap_read_lock(mm):
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
	 * pmdp_invalidate_ad() is required to make sure we don't miss
	 * dirty/young flags set by hardware.
	 */
	oldpmd = pmdp_invalidate_ad(vma, addr, pmd);

	entry = pmd_modify(oldpmd, newprot);
	if (preserve_write)
		entry = pmd_mk_savedwrite(entry);
	if (uffd_wp) {
		entry = pmd_wrprotect(entry);
		entry = pmd_mkuffd_wp(entry);
	} else if (uffd_wp_resolve) {
		/*
		 * Leave the write bit to be handled by PF interrupt
		 * handler, then things like COW could be properly
		 * handled.
		 */
		entry = pmd_clear_uffd_wp(entry);
	}
	ret = HPAGE_PMD_NR;
	set_pmd_at(mm, addr, pmd, entry);

	if (huge_pmd_needs_flush(oldpmd, entry))
		tlb_flush_pmd_range(tlb, addr, HPAGE_PMD_SIZE);

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
 * Returns page table lock pointer if a given pud maps a thp, NULL otherwise.
 *
 * Note that if it returns page table lock pointer, this routine returns without
 * unlocking page table lock. So callers must unlock it.
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
	spinlock_t *ptl;

	ptl = __pud_trans_huge_lock(pud, vma);
	if (!ptl)
		return 0;

	pudp_huge_get_and_clear_full(tlb->mm, addr, pud, tlb->fullmm);
	tlb_remove_pud_tlb_entry(tlb, pud, addr);
	if (vma_is_special_huge(vma)) {
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
	struct mmu_notifier_range range;

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma, vma->vm_mm,
				address & HPAGE_PUD_MASK,
				(address & HPAGE_PUD_MASK) + HPAGE_PUD_SIZE);
	mmu_notifier_invalidate_range_start(&range);
	ptl = pud_lock(vma->vm_mm, pud);
	if (unlikely(!pud_trans_huge(*pud) && !pud_devmap(*pud)))
		goto out;
	__split_huge_pud_locked(vma, pud, range.start);

out:
	spin_unlock(ptl);
	/*
	 * No need to double call mmu_notifier->invalidate_range() callback as
	 * the above pudp_huge_clear_flush_notify() did already call it.
	 */
	mmu_notifier_invalidate_range_only_end(&range);
}
#endif /* CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */

static void __split_huge_zero_page_pmd(struct vm_area_struct *vma,
		unsigned long haddr, pmd_t *pmd)
{
	struct mm_struct *mm = vma->vm_mm;
	pgtable_t pgtable;
	pmd_t _pmd, old_pmd;
	int i;

	/*
	 * Leave pmd empty until pte is filled note that it is fine to delay
	 * notification until mmu_notifier_invalidate_range_end() as we are
	 * replacing a zero pmd write protected page with a zero pte write
	 * protected page.
	 *
	 * See Documentation/mm/mmu_notifier.rst
	 */
	old_pmd = pmdp_huge_clear_flush(vma, haddr, pmd);

	pgtable = pgtable_trans_huge_withdraw(mm, pmd);
	pmd_populate(mm, &_pmd, pgtable);

	for (i = 0; i < HPAGE_PMD_NR; i++, haddr += PAGE_SIZE) {
		pte_t *pte, entry;
		entry = pfn_pte(my_zero_pfn(haddr), vma->vm_page_prot);
		entry = pte_mkspecial(entry);
		if (pmd_uffd_wp(old_pmd))
			entry = pte_mkuffd_wp(entry);
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
	bool young, write, soft_dirty, pmd_migration = false, uffd_wp = false;
	bool anon_exclusive = false, dirty = false;
	unsigned long addr;
	int i;

	VM_BUG_ON(haddr & ~HPAGE_PMD_MASK);
	VM_BUG_ON_VMA(vma->vm_start > haddr, vma);
	VM_BUG_ON_VMA(vma->vm_end < haddr + HPAGE_PMD_SIZE, vma);
	VM_BUG_ON(!is_pmd_migration_entry(*pmd) && !pmd_trans_huge(*pmd)
				&& !pmd_devmap(*pmd));

	count_vm_event(THP_SPLIT_PMD);

	if (!vma_is_anonymous(vma)) {
		old_pmd = pmdp_huge_clear_flush_notify(vma, haddr, pmd);
		/*
		 * We are going to unmap this huge page. So
		 * just go ahead and zap it
		 */
		if (arch_needs_pgtable_deposit())
			zap_deposited_table(mm, pmd);
		if (vma_is_special_huge(vma))
			return;
		if (unlikely(is_pmd_migration_entry(old_pmd))) {
			swp_entry_t entry;

			entry = pmd_to_swp_entry(old_pmd);
			page = pfn_swap_entry_to_page(entry);
		} else {
			page = pmd_page(old_pmd);
			if (!PageDirty(page) && pmd_dirty(old_pmd))
				set_page_dirty(page);
			if (!PageReferenced(page) && pmd_young(old_pmd))
				SetPageReferenced(page);
			page_remove_rmap(page, vma, true);
			put_page(page);
		}
		add_mm_counter(mm, mm_counter_file(page), -HPAGE_PMD_NR);
		return;
	}

	if (is_huge_zero_pmd(*pmd)) {
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

	pmd_migration = is_pmd_migration_entry(*pmd);
	if (unlikely(pmd_migration)) {
		swp_entry_t entry;

		old_pmd = *pmd;
		entry = pmd_to_swp_entry(old_pmd);
		page = pfn_swap_entry_to_page(entry);
		write = is_writable_migration_entry(entry);
		if (PageAnon(page))
			anon_exclusive = is_readable_exclusive_migration_entry(entry);
		young = is_migration_entry_young(entry);
		dirty = is_migration_entry_dirty(entry);
		soft_dirty = pmd_swp_soft_dirty(old_pmd);
		uffd_wp = pmd_swp_uffd_wp(old_pmd);
	} else {
		/*
		 * Up to this point the pmd is present and huge and userland has
		 * the whole access to the hugepage during the split (which
		 * happens in place). If we overwrite the pmd with the not-huge
		 * version pointing to the pte here (which of course we could if
		 * all CPUs were bug free), userland could trigger a small page
		 * size TLB miss on the small sized TLB while the hugepage TLB
		 * entry is still established in the huge TLB. Some CPU doesn't
		 * like that. See
		 * http://support.amd.com/TechDocs/41322_10h_Rev_Gd.pdf, Erratum
		 * 383 on page 105. Intel should be safe but is also warns that
		 * it's only safe if the permission and cache attributes of the
		 * two entries loaded in the two TLB is identical (which should
		 * be the case here). But it is generally safer to never allow
		 * small and huge TLB entries for the same virtual address to be
		 * loaded simultaneously. So instead of doing "pmd_populate();
		 * flush_pmd_tlb_range();" we first mark the current pmd
		 * notpresent (atomically because here the pmd_trans_huge must
		 * remain set at all times on the pmd until the split is
		 * complete for this pmd), then we flush the SMP TLB and finally
		 * we write the non-huge version of the pmd entry with
		 * pmd_populate.
		 */
		old_pmd = pmdp_invalidate(vma, haddr, pmd);
		page = pmd_page(old_pmd);
		if (pmd_dirty(old_pmd)) {
			dirty = true;
			SetPageDirty(page);
		}
		write = pmd_write(old_pmd);
		young = pmd_young(old_pmd);
		soft_dirty = pmd_soft_dirty(old_pmd);
		uffd_wp = pmd_uffd_wp(old_pmd);

		VM_BUG_ON_PAGE(!page_count(page), page);
		page_ref_add(page, HPAGE_PMD_NR - 1);

		/*
		 * Without "freeze", we'll simply split the PMD, propagating the
		 * PageAnonExclusive() flag for each PTE by setting it for
		 * each subpage -- no need to (temporarily) clear.
		 *
		 * With "freeze" we want to replace mapped pages by
		 * migration entries right away. This is only possible if we
		 * managed to clear PageAnonExclusive() -- see
		 * set_pmd_migration_entry().
		 *
		 * In case we cannot clear PageAnonExclusive(), split the PMD
		 * only and let try_to_migrate_one() fail later.
		 *
		 * See page_try_share_anon_rmap(): invalidate PMD first.
		 */
		anon_exclusive = PageAnon(page) && PageAnonExclusive(page);
		if (freeze && anon_exclusive && page_try_share_anon_rmap(page))
			freeze = false;
	}

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
			if (write)
				swp_entry = make_writable_migration_entry(
							page_to_pfn(page + i));
			else if (anon_exclusive)
				swp_entry = make_readable_exclusive_migration_entry(
							page_to_pfn(page + i));
			else
				swp_entry = make_readable_migration_entry(
							page_to_pfn(page + i));
			if (young)
				swp_entry = make_migration_entry_young(swp_entry);
			if (dirty)
				swp_entry = make_migration_entry_dirty(swp_entry);
			entry = swp_entry_to_pte(swp_entry);
			if (soft_dirty)
				entry = pte_swp_mksoft_dirty(entry);
			if (uffd_wp)
				entry = pte_swp_mkuffd_wp(entry);
		} else {
			entry = mk_pte(page + i, READ_ONCE(vma->vm_page_prot));
			entry = maybe_mkwrite(entry, vma);
			if (anon_exclusive)
				SetPageAnonExclusive(page + i);
			if (!write)
				entry = pte_wrprotect(entry);
			if (!young)
				entry = pte_mkold(entry);
			/*
			 * NOTE: we don't do pte_mkdirty when dirty==true
			 * because it breaks sparc64 which can sigsegv
			 * random process.  Need to revisit when we figure
			 * out what is special with sparc64.
			 */
			if (soft_dirty)
				entry = pte_mksoft_dirty(entry);
			if (uffd_wp)
				entry = pte_mkuffd_wp(entry);
		}
		pte = pte_offset_map(&_pmd, addr);
		BUG_ON(!pte_none(*pte));
		set_pte_at(mm, addr, pte, entry);
		if (!pmd_migration)
			atomic_inc(&page[i]._mapcount);
		pte_unmap(pte);
	}

	if (!pmd_migration) {
		/*
		 * Set PG_double_map before dropping compound_mapcount to avoid
		 * false-negative page_mapped().
		 */
		if (compound_mapcount(page) > 1 &&
		    !TestSetPageDoubleMap(page)) {
			for (i = 0; i < HPAGE_PMD_NR; i++)
				atomic_inc(&page[i]._mapcount);
		}

		lock_page_memcg(page);
		if (atomic_add_negative(-1, compound_mapcount_ptr(page))) {
			/* Last compound_mapcount is gone. */
			__mod_lruvec_page_state(page, NR_ANON_THPS,
						-HPAGE_PMD_NR);
			if (TestClearPageDoubleMap(page)) {
				/* No need in mapcount reference anymore */
				for (i = 0; i < HPAGE_PMD_NR; i++)
					atomic_dec(&page[i]._mapcount);
			}
		}
		unlock_page_memcg(page);

		/* Above is effectively page_remove_rmap(page, vma, true) */
		munlock_vma_page(page, vma, true);
	}

	smp_wmb(); /* make pte visible before pmd */
	pmd_populate(mm, pmd, pgtable);

	if (freeze) {
		for (i = 0; i < HPAGE_PMD_NR; i++) {
			page_remove_rmap(page + i, vma, false);
			put_page(page + i);
		}
	}
}

void __split_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long address, bool freeze, struct folio *folio)
{
	spinlock_t *ptl;
	struct mmu_notifier_range range;

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma, vma->vm_mm,
				address & HPAGE_PMD_MASK,
				(address & HPAGE_PMD_MASK) + HPAGE_PMD_SIZE);
	mmu_notifier_invalidate_range_start(&range);
	ptl = pmd_lock(vma->vm_mm, pmd);

	/*
	 * If caller asks to setup a migration entry, we need a folio to check
	 * pmd against. Otherwise we can end up replacing wrong folio.
	 */
	VM_BUG_ON(freeze && !folio);
	VM_WARN_ON_ONCE(folio && !folio_test_locked(folio));

	if (pmd_trans_huge(*pmd) || pmd_devmap(*pmd) ||
	    is_pmd_migration_entry(*pmd)) {
		/*
		 * It's safe to call pmd_page when folio is set because it's
		 * guaranteed that pmd is present.
		 */
		if (folio && folio != page_folio(pmd_page(*pmd)))
			goto out;
		__split_huge_pmd_locked(vma, pmd, range.start, freeze);
	}

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
	mmu_notifier_invalidate_range_only_end(&range);
}

void split_huge_pmd_address(struct vm_area_struct *vma, unsigned long address,
		bool freeze, struct folio *folio)
{
	pmd_t *pmd = mm_find_pmd(vma->vm_mm, address);

	if (!pmd)
		return;

	__split_huge_pmd(vma, pmd, address, freeze, folio);
}

static inline void split_huge_pmd_if_needed(struct vm_area_struct *vma, unsigned long address)
{
	/*
	 * If the new address isn't hpage aligned and it could previously
	 * contain an hugepage: check if we need to split an huge pmd.
	 */
	if (!IS_ALIGNED(address, HPAGE_PMD_SIZE) &&
	    range_in_vma(vma, ALIGN_DOWN(address, HPAGE_PMD_SIZE),
			 ALIGN(address, HPAGE_PMD_SIZE)))
		split_huge_pmd_address(vma, address, false, NULL);
}

void vma_adjust_trans_huge(struct vm_area_struct *vma,
			     unsigned long start,
			     unsigned long end,
			     long adjust_next)
{
	/* Check if we need to split start first. */
	split_huge_pmd_if_needed(vma, start);

	/* Check if we need to split end next. */
	split_huge_pmd_if_needed(vma, end);

	/*
	 * If we're also updating the next vma vm_start,
	 * check if we need to split it.
	 */
	if (adjust_next > 0) {
		struct vm_area_struct *next = find_vma(vma->vm_mm, vma->vm_end);
		unsigned long nstart = next->vm_start;
		nstart += adjust_next;
		split_huge_pmd_if_needed(next, nstart);
	}
}

static void unmap_folio(struct folio *folio)
{
	enum ttu_flags ttu_flags = TTU_RMAP_LOCKED | TTU_SPLIT_HUGE_PMD |
		TTU_SYNC;

	VM_BUG_ON_FOLIO(!folio_test_large(folio), folio);

	/*
	 * Anon pages need migration entries to preserve them, but file
	 * pages can simply be left unmapped, then faulted back on demand.
	 * If that is ever changed (perhaps for mlock), update remap_page().
	 */
	if (folio_test_anon(folio))
		try_to_migrate(folio, ttu_flags);
	else
		try_to_unmap(folio, ttu_flags | TTU_IGNORE_MLOCK);
}

static void remap_page(struct folio *folio, unsigned long nr)
{
	int i = 0;

	/* If unmap_folio() uses try_to_migrate() on file, remove this check */
	if (!folio_test_anon(folio))
		return;
	for (;;) {
		remove_migration_ptes(folio, folio, true);
		i += folio_nr_pages(folio);
		if (i >= nr)
			break;
		folio = folio_next(folio);
	}
}

static void lru_add_page_tail(struct page *head, struct page *tail,
		struct lruvec *lruvec, struct list_head *list)
{
	VM_BUG_ON_PAGE(!PageHead(head), head);
	VM_BUG_ON_PAGE(PageCompound(tail), head);
	VM_BUG_ON_PAGE(PageLRU(tail), head);
	lockdep_assert_held(&lruvec->lru_lock);

	if (list) {
		/* page reclaim is reclaiming a huge page */
		VM_WARN_ON(PageLRU(head));
		get_page(tail);
		list_add_tail(&tail->lru, list);
	} else {
		/* head is still on lru (and we have it frozen) */
		VM_WARN_ON(!PageLRU(head));
		if (PageUnevictable(tail))
			tail->mlock_count = 0;
		else
			list_add_tail(&tail->lru, &head->lru);
		SetPageLRU(tail);
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
	 * for example lock_page() which set PG_waiters.
	 *
	 * Note that for mapped sub-pages of an anonymous THP,
	 * PG_anon_exclusive has been cleared in unmap_folio() and is stored in
	 * the migration entry instead from where remap_page() will restore it.
	 * We can still have PG_anon_exclusive set on effectively unmapped and
	 * unreferenced sub-pages of an anonymous THP: we can simply drop
	 * PG_anon_exclusive (-> PG_mappedtodisk) for these here.
	 */
	page_tail->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
	page_tail->flags |= (head->flags &
			((1L << PG_referenced) |
			 (1L << PG_swapbacked) |
			 (1L << PG_swapcache) |
			 (1L << PG_mlocked) |
			 (1L << PG_uptodate) |
			 (1L << PG_active) |
			 (1L << PG_workingset) |
			 (1L << PG_locked) |
			 (1L << PG_unevictable) |
#ifdef CONFIG_64BIT
			 (1L << PG_arch_2) |
#endif
			 (1L << PG_dirty) |
			 LRU_GEN_MASK | LRU_REFS_MASK));

	/* ->mapping in first tail page is compound_mapcount */
	VM_BUG_ON_PAGE(tail > 2 && page_tail->mapping != TAIL_MAPPING,
			page_tail);
	page_tail->mapping = head->mapping;
	page_tail->index = head->index + tail;

	/*
	 * page->private should not be set in tail pages with the exception
	 * of swap cache pages that store the swp_entry_t in tail pages.
	 * Fix up and warn once if private is unexpectedly set.
	 */
	if (!folio_test_swapcache(page_folio(head))) {
		VM_WARN_ON_ONCE_PAGE(page_tail->private != 0, page_tail);
		page_tail->private = 0;
	}

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

	page_cpupid_xchg_last(page_tail, page_cpupid_last(head));

	/*
	 * always add to the tail because some iterators expect new
	 * pages to show after the currently processed elements - e.g.
	 * migrate_pages
	 */
	lru_add_page_tail(head, page_tail, lruvec, list);
}

static void __split_huge_page(struct page *page, struct list_head *list,
		pgoff_t end)
{
	struct folio *folio = page_folio(page);
	struct page *head = &folio->page;
	struct lruvec *lruvec;
	struct address_space *swap_cache = NULL;
	unsigned long offset = 0;
	unsigned int nr = thp_nr_pages(head);
	int i;

	/* complete memcg works before add pages to LRU */
	split_page_memcg(head, nr);

	if (PageAnon(head) && PageSwapCache(head)) {
		swp_entry_t entry = { .val = page_private(head) };

		offset = swp_offset(entry);
		swap_cache = swap_address_space(entry);
		xa_lock(&swap_cache->i_pages);
	}

	/* lock lru list/PageCompound, ref frozen by page_ref_freeze */
	lruvec = folio_lruvec_lock(folio);

	ClearPageHasHWPoisoned(head);

	for (i = nr - 1; i >= 1; i--) {
		__split_huge_page_tail(head, i, lruvec, list);
		/* Some pages can be beyond EOF: drop them from page cache */
		if (head[i].index >= end) {
			struct folio *tail = page_folio(head + i);

			if (shmem_mapping(head->mapping))
				shmem_uncharge(head->mapping->host, 1);
			else if (folio_test_clear_dirty(tail))
				folio_account_cleaned(tail,
					inode_to_wb(folio->mapping->host));
			__filemap_remove_folio(tail, NULL);
			folio_put(tail);
		} else if (!PageAnon(page)) {
			__xa_store(&head->mapping->i_pages, head[i].index,
					head + i, 0);
		} else if (swap_cache) {
			__xa_store(&swap_cache->i_pages, offset + i,
					head + i, 0);
		}
	}

	ClearPageCompound(head);
	unlock_page_lruvec(lruvec);
	/* Caller disabled irqs, so they are still disabled here */

	split_page_owner(head, nr);

	/* See comment in __split_huge_page_tail() */
	if (PageAnon(head)) {
		/* Additional pin to swap cache */
		if (PageSwapCache(head)) {
			page_ref_add(head, 2);
			xa_unlock(&swap_cache->i_pages);
		} else {
			page_ref_inc(head);
		}
	} else {
		/* Additional pin to page cache */
		page_ref_add(head, 2);
		xa_unlock(&head->mapping->i_pages);
	}
	local_irq_enable();

	remap_page(folio, nr);

	if (PageSwapCache(head)) {
		swp_entry_t entry = { .val = page_private(head) };

		split_swap_cluster(entry);
	}

	for (i = 0; i < nr; i++) {
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
		free_page_and_swap_cache(subpage);
	}
}

/* Racy check whether the huge page can be split */
bool can_split_folio(struct folio *folio, int *pextra_pins)
{
	int extra_pins;

	/* Additional pins from page cache */
	if (folio_test_anon(folio))
		extra_pins = folio_test_swapcache(folio) ?
				folio_nr_pages(folio) : 0;
	else
		extra_pins = folio_nr_pages(folio);
	if (pextra_pins)
		*pextra_pins = extra_pins;
	return folio_mapcount(folio) == folio_ref_count(folio) - extra_pins - 1;
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
	struct folio *folio = page_folio(page);
	struct deferred_split *ds_queue = get_deferred_split_queue(&folio->page);
	XA_STATE(xas, &folio->mapping->i_pages, folio->index);
	struct anon_vma *anon_vma = NULL;
	struct address_space *mapping = NULL;
	int extra_pins, ret;
	pgoff_t end;
	bool is_hzp;

	VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);
	VM_BUG_ON_FOLIO(!folio_test_large(folio), folio);

	is_hzp = is_huge_zero_page(&folio->page);
	if (is_hzp) {
		pr_warn_ratelimited("Called split_huge_page for huge zero page\n");
		return -EBUSY;
	}

	if (folio_test_writeback(folio))
		return -EBUSY;

	if (folio_test_anon(folio)) {
		/*
		 * The caller does not necessarily hold an mmap_lock that would
		 * prevent the anon_vma disappearing so we first we take a
		 * reference to it and then lock the anon_vma for write. This
		 * is similar to folio_lock_anon_vma_read except the write lock
		 * is taken to serialise against parallel split or collapse
		 * operations.
		 */
		anon_vma = folio_get_anon_vma(folio);
		if (!anon_vma) {
			ret = -EBUSY;
			goto out;
		}
		end = -1;
		mapping = NULL;
		anon_vma_lock_write(anon_vma);
	} else {
		gfp_t gfp;

		mapping = folio->mapping;

		/* Truncated ? */
		if (!mapping) {
			ret = -EBUSY;
			goto out;
		}

		gfp = current_gfp_context(mapping_gfp_mask(mapping) &
							GFP_RECLAIM_MASK);

		if (!filemap_release_folio(folio, gfp)) {
			ret = -EBUSY;
			goto out;
		}

		xas_split_alloc(&xas, folio, folio_order(folio), gfp);
		if (xas_error(&xas)) {
			ret = xas_error(&xas);
			goto out;
		}

		anon_vma = NULL;
		i_mmap_lock_read(mapping);

		/*
		 *__split_huge_page() may need to trim off pages beyond EOF:
		 * but on 32-bit, i_size_read() takes an irq-unsafe seqlock,
		 * which cannot be nested inside the page tree lock. So note
		 * end now: i_size itself may be changed at any moment, but
		 * folio lock is good enough to serialize the trimming.
		 */
		end = DIV_ROUND_UP(i_size_read(mapping->host), PAGE_SIZE);
		if (shmem_mapping(mapping))
			end = shmem_fallocend(mapping->host, end);
	}

	/*
	 * Racy check if we can split the page, before unmap_folio() will
	 * split PMDs
	 */
	if (!can_split_folio(folio, &extra_pins)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	unmap_folio(folio);

	/* block interrupt reentry in xa_lock and spinlock */
	local_irq_disable();
	if (mapping) {
		/*
		 * Check if the folio is present in page cache.
		 * We assume all tail are present too, if folio is there.
		 */
		xas_lock(&xas);
		xas_reset(&xas);
		if (xas_load(&xas) != folio)
			goto fail;
	}

	/* Prevent deferred_split_scan() touching ->_refcount */
	spin_lock(&ds_queue->split_queue_lock);
	if (folio_ref_freeze(folio, 1 + extra_pins)) {
		if (!list_empty(page_deferred_list(&folio->page))) {
			ds_queue->split_queue_len--;
			list_del(page_deferred_list(&folio->page));
		}
		spin_unlock(&ds_queue->split_queue_lock);
		if (mapping) {
			int nr = folio_nr_pages(folio);

			xas_split(&xas, folio, folio_order(folio));
			if (folio_test_pmd_mappable(folio)) {
				if (folio_test_swapbacked(folio)) {
					__lruvec_stat_mod_folio(folio,
							NR_SHMEM_THPS, -nr);
				} else {
					__lruvec_stat_mod_folio(folio,
							NR_FILE_THPS, -nr);
					filemap_nr_thps_dec(mapping);
				}
			}
		}

		__split_huge_page(page, list, end);
		ret = 0;
	} else {
		spin_unlock(&ds_queue->split_queue_lock);
fail:
		if (mapping)
			xas_unlock(&xas);
		local_irq_enable();
		remap_page(folio, folio_nr_pages(folio));
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
	xas_destroy(&xas);
	count_vm_event(!ret ? THP_SPLIT_PAGE : THP_SPLIT_PAGE_FAILED);
	return ret;
}

void free_transhuge_page(struct page *page)
{
	struct deferred_split *ds_queue = get_deferred_split_queue(page);
	unsigned long flags;

	spin_lock_irqsave(&ds_queue->split_queue_lock, flags);
	if (!list_empty(page_deferred_list(page))) {
		ds_queue->split_queue_len--;
		list_del(page_deferred_list(page));
	}
	spin_unlock_irqrestore(&ds_queue->split_queue_lock, flags);
	free_compound_page(page);
}

void deferred_split_huge_page(struct page *page)
{
	struct deferred_split *ds_queue = get_deferred_split_queue(page);
#ifdef CONFIG_MEMCG
	struct mem_cgroup *memcg = page_memcg(compound_head(page));
#endif
	unsigned long flags;

	VM_BUG_ON_PAGE(!PageTransHuge(page), page);

	/*
	 * The try_to_unmap() in page reclaim path might reach here too,
	 * this may cause a race condition to corrupt deferred split queue.
	 * And, if page reclaim is already handling the same page, it is
	 * unnecessary to handle it again in shrinker.
	 *
	 * Check PageSwapCache to determine if the page is being
	 * handled by page reclaim since THP swap would add the page into
	 * swap cache before calling try_to_unmap().
	 */
	if (PageSwapCache(page))
		return;

	if (!list_empty(page_deferred_list(page)))
		return;

	spin_lock_irqsave(&ds_queue->split_queue_lock, flags);
	if (list_empty(page_deferred_list(page))) {
		count_vm_event(THP_DEFERRED_SPLIT_PAGE);
		list_add_tail(page_deferred_list(page), &ds_queue->split_queue);
		ds_queue->split_queue_len++;
#ifdef CONFIG_MEMCG
		if (memcg)
			set_shrinker_bit(memcg, page_to_nid(page),
					 deferred_split_shrinker.id);
#endif
	}
	spin_unlock_irqrestore(&ds_queue->split_queue_lock, flags);
}

static unsigned long deferred_split_count(struct shrinker *shrink,
		struct shrink_control *sc)
{
	struct pglist_data *pgdata = NODE_DATA(sc->nid);
	struct deferred_split *ds_queue = &pgdata->deferred_split_queue;

#ifdef CONFIG_MEMCG
	if (sc->memcg)
		ds_queue = &sc->memcg->deferred_split_queue;
#endif
	return READ_ONCE(ds_queue->split_queue_len);
}

static unsigned long deferred_split_scan(struct shrinker *shrink,
		struct shrink_control *sc)
{
	struct pglist_data *pgdata = NODE_DATA(sc->nid);
	struct deferred_split *ds_queue = &pgdata->deferred_split_queue;
	unsigned long flags;
	LIST_HEAD(list), *pos, *next;
	struct page *page;
	int split = 0;

#ifdef CONFIG_MEMCG
	if (sc->memcg)
		ds_queue = &sc->memcg->deferred_split_queue;
#endif

	spin_lock_irqsave(&ds_queue->split_queue_lock, flags);
	/* Take pin on all head pages to avoid freeing them under us */
	list_for_each_safe(pos, next, &ds_queue->split_queue) {
		page = list_entry((void *)pos, struct page, deferred_list);
		page = compound_head(page);
		if (get_page_unless_zero(page)) {
			list_move(page_deferred_list(page), &list);
		} else {
			/* We lost race with put_compound_page() */
			list_del_init(page_deferred_list(page));
			ds_queue->split_queue_len--;
		}
		if (!--sc->nr_to_scan)
			break;
	}
	spin_unlock_irqrestore(&ds_queue->split_queue_lock, flags);

	list_for_each_safe(pos, next, &list) {
		page = list_entry((void *)pos, struct page, deferred_list);
		if (!trylock_page(page))
			goto next;
		/* split_huge_page() removes page from list on success */
		if (!split_huge_page(page))
			split++;
		unlock_page(page);
next:
		put_page(page);
	}

	spin_lock_irqsave(&ds_queue->split_queue_lock, flags);
	list_splice_tail(&list, &ds_queue->split_queue);
	spin_unlock_irqrestore(&ds_queue->split_queue_lock, flags);

	/*
	 * Stop shrinker if we didn't split any page, but the queue is empty.
	 * This can happen if pages were freed under us.
	 */
	if (!split && list_empty(&ds_queue->split_queue))
		return SHRINK_STOP;
	return split;
}

static struct shrinker deferred_split_shrinker = {
	.count_objects = deferred_split_count,
	.scan_objects = deferred_split_scan,
	.seeks = DEFAULT_SEEKS,
	.flags = SHRINKER_NUMA_AWARE | SHRINKER_MEMCG_AWARE |
		 SHRINKER_NONSLAB,
};

#ifdef CONFIG_DEBUG_FS
static void split_huge_pages_all(void)
{
	struct zone *zone;
	struct page *page;
	unsigned long pfn, max_zone_pfn;
	unsigned long total = 0, split = 0;

	pr_debug("Split all THPs\n");
	for_each_zone(zone) {
		if (!managed_zone(zone))
			continue;
		max_zone_pfn = zone_end_pfn(zone);
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++) {
			int nr_pages;

			page = pfn_to_online_page(pfn);
			if (!page || !get_page_unless_zero(page))
				continue;

			if (zone != page_zone(page))
				goto next;

			if (!PageHead(page) || PageHuge(page) || !PageLRU(page))
				goto next;

			total++;
			lock_page(page);
			nr_pages = thp_nr_pages(page);
			if (!split_huge_page(page))
				split++;
			pfn += nr_pages - 1;
			unlock_page(page);
next:
			put_page(page);
			cond_resched();
		}
	}

	pr_debug("%lu of %lu THP split\n", split, total);
}

static inline bool vma_not_suitable_for_thp_split(struct vm_area_struct *vma)
{
	return vma_is_special_huge(vma) || (vma->vm_flags & VM_IO) ||
		    is_vm_hugetlb_page(vma);
}

static int split_huge_pages_pid(int pid, unsigned long vaddr_start,
				unsigned long vaddr_end)
{
	int ret = 0;
	struct task_struct *task;
	struct mm_struct *mm;
	unsigned long total = 0, split = 0;
	unsigned long addr;

	vaddr_start &= PAGE_MASK;
	vaddr_end &= PAGE_MASK;

	/* Find the task_struct from pid */
	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (!task) {
		rcu_read_unlock();
		ret = -ESRCH;
		goto out;
	}
	get_task_struct(task);
	rcu_read_unlock();

	/* Find the mm_struct */
	mm = get_task_mm(task);
	put_task_struct(task);

	if (!mm) {
		ret = -EINVAL;
		goto out;
	}

	pr_debug("Split huge pages in pid: %d, vaddr: [0x%lx - 0x%lx]\n",
		 pid, vaddr_start, vaddr_end);

	mmap_read_lock(mm);
	/*
	 * always increase addr by PAGE_SIZE, since we could have a PTE page
	 * table filled with PTE-mapped THPs, each of which is distinct.
	 */
	for (addr = vaddr_start; addr < vaddr_end; addr += PAGE_SIZE) {
		struct vm_area_struct *vma = vma_lookup(mm, addr);
		struct page *page;

		if (!vma)
			break;

		/* skip special VMA and hugetlb VMA */
		if (vma_not_suitable_for_thp_split(vma)) {
			addr = vma->vm_end;
			continue;
		}

		/* FOLL_DUMP to ignore special (like zero) pages */
		page = follow_page(vma, addr, FOLL_GET | FOLL_DUMP);

		if (IS_ERR_OR_NULL(page))
			continue;

		if (!is_transparent_hugepage(page))
			goto next;

		total++;
		if (!can_split_folio(page_folio(page), NULL))
			goto next;

		if (!trylock_page(page))
			goto next;

		if (!split_huge_page(page))
			split++;

		unlock_page(page);
next:
		put_page(page);
		cond_resched();
	}
	mmap_read_unlock(mm);
	mmput(mm);

	pr_debug("%lu of %lu THP split\n", split, total);

out:
	return ret;
}

static int split_huge_pages_in_file(const char *file_path, pgoff_t off_start,
				pgoff_t off_end)
{
	struct filename *file;
	struct file *candidate;
	struct address_space *mapping;
	int ret = -EINVAL;
	pgoff_t index;
	int nr_pages = 1;
	unsigned long total = 0, split = 0;

	file = getname_kernel(file_path);
	if (IS_ERR(file))
		return ret;

	candidate = file_open_name(file, O_RDONLY, 0);
	if (IS_ERR(candidate))
		goto out;

	pr_debug("split file-backed THPs in file: %s, page offset: [0x%lx - 0x%lx]\n",
		 file_path, off_start, off_end);

	mapping = candidate->f_mapping;

	for (index = off_start; index < off_end; index += nr_pages) {
		struct page *fpage = pagecache_get_page(mapping, index,
						FGP_ENTRY | FGP_HEAD, 0);

		nr_pages = 1;
		if (xa_is_value(fpage) || !fpage)
			continue;

		if (!is_transparent_hugepage(fpage))
			goto next;

		total++;
		nr_pages = thp_nr_pages(fpage);

		if (!trylock_page(fpage))
			goto next;

		if (!split_huge_page(fpage))
			split++;

		unlock_page(fpage);
next:
		put_page(fpage);
		cond_resched();
	}

	filp_close(candidate, NULL);
	ret = 0;

	pr_debug("%lu of %lu file-backed THP split\n", split, total);
out:
	putname(file);
	return ret;
}

#define MAX_INPUT_BUF_SZ 255

static ssize_t split_huge_pages_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppops)
{
	static DEFINE_MUTEX(split_debug_mutex);
	ssize_t ret;
	/* hold pid, start_vaddr, end_vaddr or file_path, off_start, off_end */
	char input_buf[MAX_INPUT_BUF_SZ];
	int pid;
	unsigned long vaddr_start, vaddr_end;

	ret = mutex_lock_interruptible(&split_debug_mutex);
	if (ret)
		return ret;

	ret = -EFAULT;

	memset(input_buf, 0, MAX_INPUT_BUF_SZ);
	if (copy_from_user(input_buf, buf, min_t(size_t, count, MAX_INPUT_BUF_SZ)))
		goto out;

	input_buf[MAX_INPUT_BUF_SZ - 1] = '\0';

	if (input_buf[0] == '/') {
		char *tok;
		char *buf = input_buf;
		char file_path[MAX_INPUT_BUF_SZ];
		pgoff_t off_start = 0, off_end = 0;
		size_t input_len = strlen(input_buf);

		tok = strsep(&buf, ",");
		if (tok) {
			strcpy(file_path, tok);
		} else {
			ret = -EINVAL;
			goto out;
		}

		ret = sscanf(buf, "0x%lx,0x%lx", &off_start, &off_end);
		if (ret != 2) {
			ret = -EINVAL;
			goto out;
		}
		ret = split_huge_pages_in_file(file_path, off_start, off_end);
		if (!ret)
			ret = input_len;

		goto out;
	}

	ret = sscanf(input_buf, "%d,0x%lx,0x%lx", &pid, &vaddr_start, &vaddr_end);
	if (ret == 1 && pid == 1) {
		split_huge_pages_all();
		ret = strlen(input_buf);
		goto out;
	} else if (ret != 3) {
		ret = -EINVAL;
		goto out;
	}

	ret = split_huge_pages_pid(pid, vaddr_start, vaddr_end);
	if (!ret)
		ret = strlen(input_buf);
out:
	mutex_unlock(&split_debug_mutex);
	return ret;

}

static const struct file_operations split_huge_pages_fops = {
	.owner	 = THIS_MODULE,
	.write	 = split_huge_pages_write,
	.llseek  = no_llseek,
};

static int __init split_huge_pages_debugfs(void)
{
	debugfs_create_file("split_huge_pages", 0200, NULL, NULL,
			    &split_huge_pages_fops);
	return 0;
}
late_initcall(split_huge_pages_debugfs);
#endif

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
int set_pmd_migration_entry(struct page_vma_mapped_walk *pvmw,
		struct page *page)
{
	struct vm_area_struct *vma = pvmw->vma;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address = pvmw->address;
	bool anon_exclusive;
	pmd_t pmdval;
	swp_entry_t entry;
	pmd_t pmdswp;

	if (!(pvmw->pmd && !pvmw->pte))
		return 0;

	flush_cache_range(vma, address, address + HPAGE_PMD_SIZE);
	pmdval = pmdp_invalidate(vma, address, pvmw->pmd);

	/* See page_try_share_anon_rmap(): invalidate PMD first. */
	anon_exclusive = PageAnon(page) && PageAnonExclusive(page);
	if (anon_exclusive && page_try_share_anon_rmap(page)) {
		set_pmd_at(mm, address, pvmw->pmd, pmdval);
		return -EBUSY;
	}

	if (pmd_dirty(pmdval))
		set_page_dirty(page);
	if (pmd_write(pmdval))
		entry = make_writable_migration_entry(page_to_pfn(page));
	else if (anon_exclusive)
		entry = make_readable_exclusive_migration_entry(page_to_pfn(page));
	else
		entry = make_readable_migration_entry(page_to_pfn(page));
	if (pmd_young(pmdval))
		entry = make_migration_entry_young(entry);
	if (pmd_dirty(pmdval))
		entry = make_migration_entry_dirty(entry);
	pmdswp = swp_entry_to_pmd(entry);
	if (pmd_soft_dirty(pmdval))
		pmdswp = pmd_swp_mksoft_dirty(pmdswp);
	if (pmd_uffd_wp(pmdval))
		pmdswp = pmd_swp_mkuffd_wp(pmdswp);
	set_pmd_at(mm, address, pvmw->pmd, pmdswp);
	page_remove_rmap(page, vma, true);
	put_page(page);
	trace_set_migration_pmd(address, pmd_val(pmdswp));

	return 0;
}

void remove_migration_pmd(struct page_vma_mapped_walk *pvmw, struct page *new)
{
	struct vm_area_struct *vma = pvmw->vma;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address = pvmw->address;
	unsigned long haddr = address & HPAGE_PMD_MASK;
	pmd_t pmde;
	swp_entry_t entry;

	if (!(pvmw->pmd && !pvmw->pte))
		return;

	entry = pmd_to_swp_entry(*pvmw->pmd);
	get_page(new);
	pmde = mk_huge_pmd(new, READ_ONCE(vma->vm_page_prot));
	if (pmd_swp_soft_dirty(*pvmw->pmd))
		pmde = pmd_mksoft_dirty(pmde);
	if (pmd_swp_uffd_wp(*pvmw->pmd))
		pmde = pmd_wrprotect(pmd_mkuffd_wp(pmde));
	if (!is_migration_entry_young(entry))
		pmde = pmd_mkold(pmde);
	/* NOTE: this may contain setting soft-dirty on some archs */
	if (PageDirty(new) && is_migration_entry_dirty(entry))
		pmde = pmd_mkdirty(pmde);
	if (is_writable_migration_entry(entry))
		pmde = maybe_pmd_mkwrite(pmde, vma);
	else
		pmde = pmd_wrprotect(pmde);

	if (PageAnon(new)) {
		rmap_t rmap_flags = RMAP_COMPOUND;

		if (!is_readable_migration_entry(entry))
			rmap_flags |= RMAP_EXCLUSIVE;

		page_add_anon_rmap(new, vma, haddr, rmap_flags);
	} else {
		page_add_file_rmap(new, vma, true);
	}
	VM_BUG_ON(pmd_write(pmde) && PageAnon(new) && !PageAnonExclusive(new));
	set_pmd_at(mm, haddr, pvmw->pmd, pmde);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache_pmd(vma, address, pvmw->pmd);
	trace_remove_migration_pmd(address, pmd_val(pmde));
}
#endif
