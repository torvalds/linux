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
#include <linux/mm_types.h>
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
#include <linux/pgalloc_tag.h>
#include <linux/pagewalk.h>

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

static struct shrinker *deferred_split_shrinker;
static unsigned long deferred_split_count(struct shrinker *shrink,
					  struct shrink_control *sc);
static unsigned long deferred_split_scan(struct shrinker *shrink,
					 struct shrink_control *sc);
static bool split_underused_thp = true;

static atomic_t huge_zero_refcount;
struct folio *huge_zero_folio __read_mostly;
unsigned long huge_zero_pfn __read_mostly = ~0UL;
unsigned long huge_anon_orders_always __read_mostly;
unsigned long huge_anon_orders_madvise __read_mostly;
unsigned long huge_anon_orders_inherit __read_mostly;
static bool anon_orders_configured __initdata;

unsigned long __thp_vma_allowable_orders(struct vm_area_struct *vma,
					 unsigned long vm_flags,
					 unsigned long tva_flags,
					 unsigned long orders)
{
	bool smaps = tva_flags & TVA_SMAPS;
	bool in_pf = tva_flags & TVA_IN_PF;
	bool enforce_sysfs = tva_flags & TVA_ENFORCE_SYSFS;
	unsigned long supported_orders;

	/* Check the intersection of requested and supported orders. */
	if (vma_is_anonymous(vma))
		supported_orders = THP_ORDERS_ALL_ANON;
	else if (vma_is_special_huge(vma))
		supported_orders = THP_ORDERS_ALL_SPECIAL;
	else
		supported_orders = THP_ORDERS_ALL_FILE_DEFAULT;

	orders &= supported_orders;
	if (!orders)
		return 0;

	if (!vma->vm_mm)		/* vdso */
		return 0;

	/*
	 * Explicitly disabled through madvise or prctl, or some
	 * architectures may disable THP for some mappings, for
	 * example, s390 kvm.
	 * */
	if ((vm_flags & VM_NOHUGEPAGE) ||
	    test_bit(MMF_DISABLE_THP, &vma->vm_mm->flags))
		return 0;
	/*
	 * If the hardware/firmware marked hugepage support disabled.
	 */
	if (transparent_hugepage_flags & (1 << TRANSPARENT_HUGEPAGE_UNSUPPORTED))
		return 0;

	/* khugepaged doesn't collapse DAX vma, but page fault is fine. */
	if (vma_is_dax(vma))
		return in_pf ? orders : 0;

	/*
	 * khugepaged special VMA and hugetlb VMA.
	 * Must be checked after dax since some dax mappings may have
	 * VM_MIXEDMAP set.
	 */
	if (!in_pf && !smaps && (vm_flags & VM_NO_KHUGEPAGED))
		return 0;

	/*
	 * Check alignment for file vma and size for both file and anon vma by
	 * filtering out the unsuitable orders.
	 *
	 * Skip the check for page fault. Huge fault does the check in fault
	 * handlers.
	 */
	if (!in_pf) {
		int order = highest_order(orders);
		unsigned long addr;

		while (orders) {
			addr = vma->vm_end - (PAGE_SIZE << order);
			if (thp_vma_suitable_order(vma, addr, order))
				break;
			order = next_order(&orders, order);
		}

		if (!orders)
			return 0;
	}

	/*
	 * Enabled via shmem mount options or sysfs settings.
	 * Must be done before hugepage flags check since shmem has its
	 * own flags.
	 */
	if (!in_pf && shmem_file(vma->vm_file))
		return shmem_allowable_huge_orders(file_inode(vma->vm_file),
						   vma, vma->vm_pgoff, 0,
						   !enforce_sysfs);

	if (!vma_is_anonymous(vma)) {
		/*
		 * Enforce sysfs THP requirements as necessary. Anonymous vmas
		 * were already handled in thp_vma_allowable_orders().
		 */
		if (enforce_sysfs &&
		    (!hugepage_global_enabled() || (!(vm_flags & VM_HUGEPAGE) &&
						    !hugepage_global_always())))
			return 0;

		/*
		 * Trust that ->huge_fault() handlers know what they are doing
		 * in fault path.
		 */
		if (((in_pf || smaps)) && vma->vm_ops->huge_fault)
			return orders;
		/* Only regular file is valid in collapse path */
		if (((!in_pf || smaps)) && file_thp_enabled(vma))
			return orders;
		return 0;
	}

	if (vma_is_temporary_stack(vma))
		return 0;

	/*
	 * THPeligible bit of smaps should show 1 for proper VMAs even
	 * though anon_vma is not initialized yet.
	 *
	 * Allow page fault since anon_vma may be not initialized until
	 * the first page fault.
	 */
	if (!vma->anon_vma)
		return (smaps || in_pf) ? orders : 0;

	return orders;
}

static bool get_huge_zero_page(void)
{
	struct folio *zero_folio;
retry:
	if (likely(atomic_inc_not_zero(&huge_zero_refcount)))
		return true;

	zero_folio = folio_alloc((GFP_TRANSHUGE | __GFP_ZERO) & ~__GFP_MOVABLE,
			HPAGE_PMD_ORDER);
	if (!zero_folio) {
		count_vm_event(THP_ZERO_PAGE_ALLOC_FAILED);
		return false;
	}
	/* Ensure zero folio won't have large_rmappable flag set. */
	folio_clear_large_rmappable(zero_folio);
	preempt_disable();
	if (cmpxchg(&huge_zero_folio, NULL, zero_folio)) {
		preempt_enable();
		folio_put(zero_folio);
		goto retry;
	}
	WRITE_ONCE(huge_zero_pfn, folio_pfn(zero_folio));

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

struct folio *mm_get_huge_zero_folio(struct mm_struct *mm)
{
	if (test_bit(MMF_HUGE_ZERO_PAGE, &mm->flags))
		return READ_ONCE(huge_zero_folio);

	if (!get_huge_zero_page())
		return NULL;

	if (test_and_set_bit(MMF_HUGE_ZERO_PAGE, &mm->flags))
		put_huge_zero_page();

	return READ_ONCE(huge_zero_folio);
}

void mm_put_huge_zero_folio(struct mm_struct *mm)
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
		struct folio *zero_folio = xchg(&huge_zero_folio, NULL);
		BUG_ON(zero_folio == NULL);
		WRITE_ONCE(huge_zero_pfn, ~0UL);
		folio_put(zero_folio);
		return HPAGE_PMD_NR;
	}

	return 0;
}

static struct shrinker *huge_zero_page_shrinker;

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

static ssize_t split_underused_thp_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", split_underused_thp);
}

static ssize_t split_underused_thp_store(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	int err = kstrtobool(buf, &split_underused_thp);

	if (err < 0)
		return err;

	return count;
}

static struct kobj_attribute split_underused_thp_attr = __ATTR(
	shrink_underused, 0644, split_underused_thp_show, split_underused_thp_store);

static struct attribute *hugepage_attr[] = {
	&enabled_attr.attr,
	&defrag_attr.attr,
	&use_zero_page_attr.attr,
	&hpage_pmd_size_attr.attr,
#ifdef CONFIG_SHMEM
	&shmem_enabled_attr.attr,
#endif
	&split_underused_thp_attr.attr,
	NULL,
};

static const struct attribute_group hugepage_attr_group = {
	.attrs = hugepage_attr,
};

static void hugepage_exit_sysfs(struct kobject *hugepage_kobj);
static void thpsize_release(struct kobject *kobj);
static DEFINE_SPINLOCK(huge_anon_orders_lock);
static LIST_HEAD(thpsize_list);

static ssize_t anon_enabled_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	int order = to_thpsize(kobj)->order;
	const char *output;

	if (test_bit(order, &huge_anon_orders_always))
		output = "[always] inherit madvise never";
	else if (test_bit(order, &huge_anon_orders_inherit))
		output = "always [inherit] madvise never";
	else if (test_bit(order, &huge_anon_orders_madvise))
		output = "always inherit [madvise] never";
	else
		output = "always inherit madvise [never]";

	return sysfs_emit(buf, "%s\n", output);
}

static ssize_t anon_enabled_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	int order = to_thpsize(kobj)->order;
	ssize_t ret = count;

	if (sysfs_streq(buf, "always")) {
		spin_lock(&huge_anon_orders_lock);
		clear_bit(order, &huge_anon_orders_inherit);
		clear_bit(order, &huge_anon_orders_madvise);
		set_bit(order, &huge_anon_orders_always);
		spin_unlock(&huge_anon_orders_lock);
	} else if (sysfs_streq(buf, "inherit")) {
		spin_lock(&huge_anon_orders_lock);
		clear_bit(order, &huge_anon_orders_always);
		clear_bit(order, &huge_anon_orders_madvise);
		set_bit(order, &huge_anon_orders_inherit);
		spin_unlock(&huge_anon_orders_lock);
	} else if (sysfs_streq(buf, "madvise")) {
		spin_lock(&huge_anon_orders_lock);
		clear_bit(order, &huge_anon_orders_always);
		clear_bit(order, &huge_anon_orders_inherit);
		set_bit(order, &huge_anon_orders_madvise);
		spin_unlock(&huge_anon_orders_lock);
	} else if (sysfs_streq(buf, "never")) {
		spin_lock(&huge_anon_orders_lock);
		clear_bit(order, &huge_anon_orders_always);
		clear_bit(order, &huge_anon_orders_inherit);
		clear_bit(order, &huge_anon_orders_madvise);
		spin_unlock(&huge_anon_orders_lock);
	} else
		ret = -EINVAL;

	if (ret > 0) {
		int err;

		err = start_stop_khugepaged();
		if (err)
			ret = err;
	}
	return ret;
}

static struct kobj_attribute anon_enabled_attr =
	__ATTR(enabled, 0644, anon_enabled_show, anon_enabled_store);

static struct attribute *anon_ctrl_attrs[] = {
	&anon_enabled_attr.attr,
	NULL,
};

static const struct attribute_group anon_ctrl_attr_grp = {
	.attrs = anon_ctrl_attrs,
};

static struct attribute *file_ctrl_attrs[] = {
#ifdef CONFIG_SHMEM
	&thpsize_shmem_enabled_attr.attr,
#endif
	NULL,
};

static const struct attribute_group file_ctrl_attr_grp = {
	.attrs = file_ctrl_attrs,
};

static struct attribute *any_ctrl_attrs[] = {
	NULL,
};

static const struct attribute_group any_ctrl_attr_grp = {
	.attrs = any_ctrl_attrs,
};

static const struct kobj_type thpsize_ktype = {
	.release = &thpsize_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

DEFINE_PER_CPU(struct mthp_stat, mthp_stats) = {{{0}}};

static unsigned long sum_mthp_stat(int order, enum mthp_stat_item item)
{
	unsigned long sum = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		struct mthp_stat *this = &per_cpu(mthp_stats, cpu);

		sum += this->stats[order][item];
	}

	return sum;
}

#define DEFINE_MTHP_STAT_ATTR(_name, _index)				\
static ssize_t _name##_show(struct kobject *kobj,			\
			struct kobj_attribute *attr, char *buf)		\
{									\
	int order = to_thpsize(kobj)->order;				\
									\
	return sysfs_emit(buf, "%lu\n", sum_mthp_stat(order, _index));	\
}									\
static struct kobj_attribute _name##_attr = __ATTR_RO(_name)

DEFINE_MTHP_STAT_ATTR(anon_fault_alloc, MTHP_STAT_ANON_FAULT_ALLOC);
DEFINE_MTHP_STAT_ATTR(anon_fault_fallback, MTHP_STAT_ANON_FAULT_FALLBACK);
DEFINE_MTHP_STAT_ATTR(anon_fault_fallback_charge, MTHP_STAT_ANON_FAULT_FALLBACK_CHARGE);
DEFINE_MTHP_STAT_ATTR(swpout, MTHP_STAT_SWPOUT);
DEFINE_MTHP_STAT_ATTR(swpout_fallback, MTHP_STAT_SWPOUT_FALLBACK);
#ifdef CONFIG_SHMEM
DEFINE_MTHP_STAT_ATTR(shmem_alloc, MTHP_STAT_SHMEM_ALLOC);
DEFINE_MTHP_STAT_ATTR(shmem_fallback, MTHP_STAT_SHMEM_FALLBACK);
DEFINE_MTHP_STAT_ATTR(shmem_fallback_charge, MTHP_STAT_SHMEM_FALLBACK_CHARGE);
#endif
DEFINE_MTHP_STAT_ATTR(split, MTHP_STAT_SPLIT);
DEFINE_MTHP_STAT_ATTR(split_failed, MTHP_STAT_SPLIT_FAILED);
DEFINE_MTHP_STAT_ATTR(split_deferred, MTHP_STAT_SPLIT_DEFERRED);
DEFINE_MTHP_STAT_ATTR(nr_anon, MTHP_STAT_NR_ANON);
DEFINE_MTHP_STAT_ATTR(nr_anon_partially_mapped, MTHP_STAT_NR_ANON_PARTIALLY_MAPPED);

static struct attribute *anon_stats_attrs[] = {
	&anon_fault_alloc_attr.attr,
	&anon_fault_fallback_attr.attr,
	&anon_fault_fallback_charge_attr.attr,
#ifndef CONFIG_SHMEM
	&swpout_attr.attr,
	&swpout_fallback_attr.attr,
#endif
	&split_deferred_attr.attr,
	&nr_anon_attr.attr,
	&nr_anon_partially_mapped_attr.attr,
	NULL,
};

static struct attribute_group anon_stats_attr_grp = {
	.name = "stats",
	.attrs = anon_stats_attrs,
};

static struct attribute *file_stats_attrs[] = {
#ifdef CONFIG_SHMEM
	&shmem_alloc_attr.attr,
	&shmem_fallback_attr.attr,
	&shmem_fallback_charge_attr.attr,
#endif
	NULL,
};

static struct attribute_group file_stats_attr_grp = {
	.name = "stats",
	.attrs = file_stats_attrs,
};

static struct attribute *any_stats_attrs[] = {
#ifdef CONFIG_SHMEM
	&swpout_attr.attr,
	&swpout_fallback_attr.attr,
#endif
	&split_attr.attr,
	&split_failed_attr.attr,
	NULL,
};

static struct attribute_group any_stats_attr_grp = {
	.name = "stats",
	.attrs = any_stats_attrs,
};

static int sysfs_add_group(struct kobject *kobj,
			   const struct attribute_group *grp)
{
	int ret = -ENOENT;

	/*
	 * If the group is named, try to merge first, assuming the subdirectory
	 * was already created. This avoids the warning emitted by
	 * sysfs_create_group() if the directory already exists.
	 */
	if (grp->name)
		ret = sysfs_merge_group(kobj, grp);
	if (ret)
		ret = sysfs_create_group(kobj, grp);

	return ret;
}

static struct thpsize *thpsize_create(int order, struct kobject *parent)
{
	unsigned long size = (PAGE_SIZE << order) / SZ_1K;
	struct thpsize *thpsize;
	int ret = -ENOMEM;

	thpsize = kzalloc(sizeof(*thpsize), GFP_KERNEL);
	if (!thpsize)
		goto err;

	thpsize->order = order;

	ret = kobject_init_and_add(&thpsize->kobj, &thpsize_ktype, parent,
				   "hugepages-%lukB", size);
	if (ret) {
		kfree(thpsize);
		goto err;
	}


	ret = sysfs_add_group(&thpsize->kobj, &any_ctrl_attr_grp);
	if (ret)
		goto err_put;

	ret = sysfs_add_group(&thpsize->kobj, &any_stats_attr_grp);
	if (ret)
		goto err_put;

	if (BIT(order) & THP_ORDERS_ALL_ANON) {
		ret = sysfs_add_group(&thpsize->kobj, &anon_ctrl_attr_grp);
		if (ret)
			goto err_put;

		ret = sysfs_add_group(&thpsize->kobj, &anon_stats_attr_grp);
		if (ret)
			goto err_put;
	}

	if (BIT(order) & THP_ORDERS_ALL_FILE_DEFAULT) {
		ret = sysfs_add_group(&thpsize->kobj, &file_ctrl_attr_grp);
		if (ret)
			goto err_put;

		ret = sysfs_add_group(&thpsize->kobj, &file_stats_attr_grp);
		if (ret)
			goto err_put;
	}

	return thpsize;
err_put:
	kobject_put(&thpsize->kobj);
err:
	return ERR_PTR(ret);
}

static void thpsize_release(struct kobject *kobj)
{
	kfree(to_thpsize(kobj));
}

static int __init hugepage_init_sysfs(struct kobject **hugepage_kobj)
{
	int err;
	struct thpsize *thpsize;
	unsigned long orders;
	int order;

	/*
	 * Default to setting PMD-sized THP to inherit the global setting and
	 * disable all other sizes. powerpc's PMD_ORDER isn't a compile-time
	 * constant so we have to do this here.
	 */
	if (!anon_orders_configured)
		huge_anon_orders_inherit = BIT(PMD_ORDER);

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

	orders = THP_ORDERS_ALL_ANON | THP_ORDERS_ALL_FILE_DEFAULT;
	order = highest_order(orders);
	while (orders) {
		thpsize = thpsize_create(order, *hugepage_kobj);
		if (IS_ERR(thpsize)) {
			pr_err("failed to create thpsize for order %d\n", order);
			err = PTR_ERR(thpsize);
			goto remove_all;
		}
		list_add(&thpsize->node, &thpsize_list);
		order = next_order(&orders, order);
	}

	return 0;

remove_all:
	hugepage_exit_sysfs(*hugepage_kobj);
	return err;
remove_hp_group:
	sysfs_remove_group(*hugepage_kobj, &hugepage_attr_group);
delete_obj:
	kobject_put(*hugepage_kobj);
	return err;
}

static void __init hugepage_exit_sysfs(struct kobject *hugepage_kobj)
{
	struct thpsize *thpsize, *tmp;

	list_for_each_entry_safe(thpsize, tmp, &thpsize_list, node) {
		list_del(&thpsize->node);
		kobject_put(&thpsize->kobj);
	}

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

static int __init thp_shrinker_init(void)
{
	huge_zero_page_shrinker = shrinker_alloc(0, "thp-zero");
	if (!huge_zero_page_shrinker)
		return -ENOMEM;

	deferred_split_shrinker = shrinker_alloc(SHRINKER_NUMA_AWARE |
						 SHRINKER_MEMCG_AWARE |
						 SHRINKER_NONSLAB,
						 "thp-deferred_split");
	if (!deferred_split_shrinker) {
		shrinker_free(huge_zero_page_shrinker);
		return -ENOMEM;
	}

	huge_zero_page_shrinker->count_objects = shrink_huge_zero_page_count;
	huge_zero_page_shrinker->scan_objects = shrink_huge_zero_page_scan;
	shrinker_register(huge_zero_page_shrinker);

	deferred_split_shrinker->count_objects = deferred_split_count;
	deferred_split_shrinker->scan_objects = deferred_split_scan;
	shrinker_register(deferred_split_shrinker);

	return 0;
}

static void __init thp_shrinker_exit(void)
{
	shrinker_free(huge_zero_page_shrinker);
	shrinker_free(deferred_split_shrinker);
}

static int __init hugepage_init(void)
{
	int err;
	struct kobject *hugepage_kobj;

	if (!has_transparent_hugepage()) {
		transparent_hugepage_flags = 1 << TRANSPARENT_HUGEPAGE_UNSUPPORTED;
		return -EINVAL;
	}

	/*
	 * hugepages can't be allocated by the buddy allocator
	 */
	MAYBE_BUILD_BUG_ON(HPAGE_PMD_ORDER > MAX_PAGE_ORDER);

	err = hugepage_init_sysfs(&hugepage_kobj);
	if (err)
		goto err_sysfs;

	err = khugepaged_init();
	if (err)
		goto err_slab;

	err = thp_shrinker_init();
	if (err)
		goto err_shrinker;

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
	thp_shrinker_exit();
err_shrinker:
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

static inline int get_order_from_str(const char *size_str)
{
	unsigned long size;
	char *endptr;
	int order;

	size = memparse(size_str, &endptr);

	if (!is_power_of_2(size))
		goto err;
	order = get_order(size);
	if (BIT(order) & ~THP_ORDERS_ALL_ANON)
		goto err;

	return order;
err:
	pr_err("invalid size %s in thp_anon boot parameter\n", size_str);
	return -EINVAL;
}

static char str_dup[PAGE_SIZE] __initdata;
static int __init setup_thp_anon(char *str)
{
	char *token, *range, *policy, *subtoken;
	unsigned long always, inherit, madvise;
	char *start_size, *end_size;
	int start, end, nr;
	char *p;

	if (!str || strlen(str) + 1 > PAGE_SIZE)
		goto err;
	strcpy(str_dup, str);

	always = huge_anon_orders_always;
	madvise = huge_anon_orders_madvise;
	inherit = huge_anon_orders_inherit;
	p = str_dup;
	while ((token = strsep(&p, ";")) != NULL) {
		range = strsep(&token, ":");
		policy = token;

		if (!policy)
			goto err;

		while ((subtoken = strsep(&range, ",")) != NULL) {
			if (strchr(subtoken, '-')) {
				start_size = strsep(&subtoken, "-");
				end_size = subtoken;

				start = get_order_from_str(start_size);
				end = get_order_from_str(end_size);
			} else {
				start = end = get_order_from_str(subtoken);
			}

			if (start < 0 || end < 0 || start > end)
				goto err;

			nr = end - start + 1;
			if (!strcmp(policy, "always")) {
				bitmap_set(&always, start, nr);
				bitmap_clear(&inherit, start, nr);
				bitmap_clear(&madvise, start, nr);
			} else if (!strcmp(policy, "madvise")) {
				bitmap_set(&madvise, start, nr);
				bitmap_clear(&inherit, start, nr);
				bitmap_clear(&always, start, nr);
			} else if (!strcmp(policy, "inherit")) {
				bitmap_set(&inherit, start, nr);
				bitmap_clear(&madvise, start, nr);
				bitmap_clear(&always, start, nr);
			} else if (!strcmp(policy, "never")) {
				bitmap_clear(&inherit, start, nr);
				bitmap_clear(&madvise, start, nr);
				bitmap_clear(&always, start, nr);
			} else {
				pr_err("invalid policy %s in thp_anon boot parameter\n", policy);
				goto err;
			}
		}
	}

	huge_anon_orders_always = always;
	huge_anon_orders_madvise = madvise;
	huge_anon_orders_inherit = inherit;
	anon_orders_configured = true;
	return 1;

err:
	pr_warn("thp_anon=%s: error parsing string, ignoring setting\n", str);
	return 0;
}
__setup("thp_anon=", setup_thp_anon);

pmd_t maybe_pmd_mkwrite(pmd_t pmd, struct vm_area_struct *vma)
{
	if (likely(vma->vm_flags & VM_WRITE))
		pmd = pmd_mkwrite(pmd, vma);
	return pmd;
}

#ifdef CONFIG_MEMCG
static inline
struct deferred_split *get_deferred_split_queue(struct folio *folio)
{
	struct mem_cgroup *memcg = folio_memcg(folio);
	struct pglist_data *pgdat = NODE_DATA(folio_nid(folio));

	if (memcg)
		return &memcg->deferred_split_queue;
	else
		return &pgdat->deferred_split_queue;
}
#else
static inline
struct deferred_split *get_deferred_split_queue(struct folio *folio)
{
	struct pglist_data *pgdat = NODE_DATA(folio_nid(folio));

	return &pgdat->deferred_split_queue;
}
#endif

static inline bool is_transparent_hugepage(const struct folio *folio)
{
	if (!folio_test_large(folio))
		return false;

	return is_huge_zero_folio(folio) ||
		folio_test_large_rmappable(folio);
}

static unsigned long __thp_get_unmapped_area(struct file *filp,
		unsigned long addr, unsigned long len,
		loff_t off, unsigned long flags, unsigned long size,
		vm_flags_t vm_flags)
{
	loff_t off_end = off + len;
	loff_t off_align = round_up(off, size);
	unsigned long len_pad, ret, off_sub;

	if (!IS_ENABLED(CONFIG_64BIT) || in_compat_syscall())
		return 0;

	if (off_end <= off_align || (off_end - off_align) < size)
		return 0;

	len_pad = len + size;
	if (len_pad < len || (off + len_pad) < off)
		return 0;

	ret = mm_get_unmapped_area_vmflags(current->mm, filp, addr, len_pad,
					   off >> PAGE_SHIFT, flags, vm_flags);

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

	off_sub = (off - ret) & (size - 1);

	if (test_bit(MMF_TOPDOWN, &current->mm->flags) && !off_sub)
		return ret + size;

	ret += off_sub;
	return ret;
}

unsigned long thp_get_unmapped_area_vmflags(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags,
		vm_flags_t vm_flags)
{
	unsigned long ret;
	loff_t off = (loff_t)pgoff << PAGE_SHIFT;

	ret = __thp_get_unmapped_area(filp, addr, len, off, flags, PMD_SIZE, vm_flags);
	if (ret)
		return ret;

	return mm_get_unmapped_area_vmflags(current->mm, filp, addr, len, pgoff, flags,
					    vm_flags);
}

unsigned long thp_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	return thp_get_unmapped_area_vmflags(filp, addr, len, pgoff, flags, 0);
}
EXPORT_SYMBOL_GPL(thp_get_unmapped_area);

static vm_fault_t __do_huge_pmd_anonymous_page(struct vm_fault *vmf,
			struct page *page, gfp_t gfp)
{
	struct vm_area_struct *vma = vmf->vma;
	struct folio *folio = page_folio(page);
	pgtable_t pgtable;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;
	vm_fault_t ret = 0;

	VM_BUG_ON_FOLIO(!folio_test_large(folio), folio);

	if (mem_cgroup_charge(folio, vma->vm_mm, gfp)) {
		folio_put(folio);
		count_vm_event(THP_FAULT_FALLBACK);
		count_vm_event(THP_FAULT_FALLBACK_CHARGE);
		count_mthp_stat(HPAGE_PMD_ORDER, MTHP_STAT_ANON_FAULT_FALLBACK);
		count_mthp_stat(HPAGE_PMD_ORDER, MTHP_STAT_ANON_FAULT_FALLBACK_CHARGE);
		return VM_FAULT_FALLBACK;
	}
	folio_throttle_swaprate(folio, gfp);

	pgtable = pte_alloc_one(vma->vm_mm);
	if (unlikely(!pgtable)) {
		ret = VM_FAULT_OOM;
		goto release;
	}

	folio_zero_user(folio, vmf->address);
	/*
	 * The memory barrier inside __folio_mark_uptodate makes sure that
	 * folio_zero_user writes become visible before the set_pmd_at()
	 * write.
	 */
	__folio_mark_uptodate(folio);

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
			folio_put(folio);
			pte_free(vma->vm_mm, pgtable);
			ret = handle_userfault(vmf, VM_UFFD_MISSING);
			VM_BUG_ON(ret & VM_FAULT_FALLBACK);
			return ret;
		}

		entry = mk_huge_pmd(page, vma->vm_page_prot);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		folio_add_new_anon_rmap(folio, vma, haddr, RMAP_EXCLUSIVE);
		folio_add_lru_vma(folio, vma);
		pgtable_trans_huge_deposit(vma->vm_mm, vmf->pmd, pgtable);
		set_pmd_at(vma->vm_mm, haddr, vmf->pmd, entry);
		update_mmu_cache_pmd(vma, vmf->address, vmf->pmd);
		add_mm_counter(vma->vm_mm, MM_ANONPAGES, HPAGE_PMD_NR);
		mm_inc_nr_ptes(vma->vm_mm);
		deferred_split_folio(folio, false);
		spin_unlock(vmf->ptl);
		count_vm_event(THP_FAULT_ALLOC);
		count_mthp_stat(HPAGE_PMD_ORDER, MTHP_STAT_ANON_FAULT_ALLOC);
		count_memcg_event_mm(vma->vm_mm, THP_FAULT_ALLOC);
	}

	return 0;
unlock_release:
	spin_unlock(vmf->ptl);
release:
	if (pgtable)
		pte_free(vma->vm_mm, pgtable);
	folio_put(folio);
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
static void set_huge_zero_folio(pgtable_t pgtable, struct mm_struct *mm,
		struct vm_area_struct *vma, unsigned long haddr, pmd_t *pmd,
		struct folio *zero_folio)
{
	pmd_t entry;
	if (!pmd_none(*pmd))
		return;
	entry = mk_pmd(&zero_folio->page, vma->vm_page_prot);
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
	vm_fault_t ret;

	if (!thp_vma_suitable_order(vma, haddr, PMD_ORDER))
		return VM_FAULT_FALLBACK;
	ret = vmf_anon_prepare(vmf);
	if (ret)
		return ret;
	khugepaged_enter_vma(vma, vma->vm_flags);

	if (!(vmf->flags & FAULT_FLAG_WRITE) &&
			!mm_forbids_zeropage(vma->vm_mm) &&
			transparent_hugepage_use_zero_page()) {
		pgtable_t pgtable;
		struct folio *zero_folio;
		vm_fault_t ret;

		pgtable = pte_alloc_one(vma->vm_mm);
		if (unlikely(!pgtable))
			return VM_FAULT_OOM;
		zero_folio = mm_get_huge_zero_folio(vma->vm_mm);
		if (unlikely(!zero_folio)) {
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
				set_huge_zero_folio(pgtable, vma->vm_mm, vma,
						   haddr, vmf->pmd, zero_folio);
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
		count_mthp_stat(HPAGE_PMD_ORDER, MTHP_STAT_ANON_FAULT_FALLBACK);
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
	else
		entry = pmd_mkspecial(entry);
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
 * vmf_insert_pfn_pmd - insert a pmd size pfn
 * @vmf: Structure describing the fault
 * @pfn: pfn to insert
 * @write: whether it's a write fault
 *
 * Insert a pmd size pfn. See vmf_insert_pfn() for additional info.
 *
 * Return: vm_fault_t value.
 */
vm_fault_t vmf_insert_pfn_pmd(struct vm_fault *vmf, pfn_t pfn, bool write)
{
	unsigned long addr = vmf->address & PMD_MASK;
	struct vm_area_struct *vma = vmf->vma;
	pgprot_t pgprot = vma->vm_page_prot;
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
EXPORT_SYMBOL_GPL(vmf_insert_pfn_pmd);

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
static pud_t maybe_pud_mkwrite(pud_t pud, struct vm_area_struct *vma)
{
	if (likely(vma->vm_flags & VM_WRITE))
		pud = pud_mkwrite(pud);
	return pud;
}

static void insert_pfn_pud(struct vm_area_struct *vma, unsigned long addr,
		pud_t *pud, pfn_t pfn, bool write)
{
	struct mm_struct *mm = vma->vm_mm;
	pgprot_t prot = vma->vm_page_prot;
	pud_t entry;
	spinlock_t *ptl;

	ptl = pud_lock(mm, pud);
	if (!pud_none(*pud)) {
		if (write) {
			if (WARN_ON_ONCE(pud_pfn(*pud) != pfn_t_to_pfn(pfn)))
				goto out_unlock;
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
	else
		entry = pud_mkspecial(entry);
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
 * vmf_insert_pfn_pud - insert a pud size pfn
 * @vmf: Structure describing the fault
 * @pfn: pfn to insert
 * @write: whether it's a write fault
 *
 * Insert a pud size pfn. See vmf_insert_pfn() for additional info.
 *
 * Return: vm_fault_t value.
 */
vm_fault_t vmf_insert_pfn_pud(struct vm_fault *vmf, pfn_t pfn, bool write)
{
	unsigned long addr = vmf->address & PUD_MASK;
	struct vm_area_struct *vma = vmf->vma;
	pgprot_t pgprot = vma->vm_page_prot;

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

	insert_pfn_pud(vma, addr, vmf->pud, pfn, write);
	return VM_FAULT_NOPAGE;
}
EXPORT_SYMBOL_GPL(vmf_insert_pfn_pud);
#endif /* CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */

void touch_pmd(struct vm_area_struct *vma, unsigned long addr,
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
	int ret;

	assert_spin_locked(pmd_lockptr(mm, pmd));

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
	ret = try_grab_folio(page_folio(page), 1, flags);
	if (ret)
		page = ERR_PTR(ret);

	return page;
}

int copy_huge_pmd(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		  pmd_t *dst_pmd, pmd_t *src_pmd, unsigned long addr,
		  struct vm_area_struct *dst_vma, struct vm_area_struct *src_vma)
{
	spinlock_t *dst_ptl, *src_ptl;
	struct page *src_page;
	struct folio *src_folio;
	pmd_t pmd;
	pgtable_t pgtable = NULL;
	int ret = -ENOMEM;

	pmd = pmdp_get_lockless(src_pmd);
	if (unlikely(pmd_special(pmd))) {
		dst_ptl = pmd_lock(dst_mm, dst_pmd);
		src_ptl = pmd_lockptr(src_mm, src_pmd);
		spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);
		/*
		 * No need to recheck the pmd, it can't change with write
		 * mmap lock held here.
		 *
		 * Meanwhile, making sure it's not a CoW VMA with writable
		 * mapping, otherwise it means either the anon page wrongly
		 * applied special bit, or we made the PRIVATE mapping be
		 * able to wrongly write to the backend MMIO.
		 */
		VM_WARN_ON_ONCE(is_cow_mapping(src_vma->vm_flags) && pmd_write(pmd));
		goto set_pmd;
	}

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
		 * mm_get_huge_zero_folio() will never allocate a new
		 * folio here, since we already have a zero page to
		 * copy. It just takes a reference.
		 */
		mm_get_huge_zero_folio(dst_mm);
		goto out_zero_page;
	}

	src_page = pmd_page(pmd);
	VM_BUG_ON_PAGE(!PageHead(src_page), src_page);
	src_folio = page_folio(src_page);

	folio_get(src_folio);
	if (unlikely(folio_try_dup_anon_rmap_pmd(src_folio, src_page, src_vma))) {
		/* Page maybe pinned: split and retry the fault on PTEs. */
		folio_put(src_folio);
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
	pmd = pmd_wrprotect(pmd);
set_pmd:
	pmd = pmd_mkold(pmd);
	set_pmd_at(dst_mm, addr, dst_pmd, pmd);

	ret = 0;
out_unlock:
	spin_unlock(src_ptl);
	spin_unlock(dst_ptl);
out:
	return ret;
}

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
void touch_pud(struct vm_area_struct *vma, unsigned long addr,
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
	 * TODO: once we support anonymous pages, use
	 * folio_try_dup_anon_rmap_*() and split if duplicating fails.
	 */
	if (is_cow_mapping(vma->vm_flags) && pud_write(pud)) {
		pudp_set_wrprotect(src_mm, addr, src_pud);
		pud = pud_wrprotect(pud);
	}
	pud = pud_mkold(pud);
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
	 * the LRU cache immediately after adding a THP.
	 */
	if (folio_ref_count(folio) >
			1 + folio_test_swapcache(folio) * folio_nr_pages(folio))
		goto unlock_fallback;
	if (folio_test_swapcache(folio))
		folio_free_swap(folio);
	if (folio_ref_count(folio) == 1) {
		pmd_t entry;

		folio_move_anon_rmap(folio, vma);
		SetPageAnonExclusive(page);
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
		return 0;
	}

unlock_fallback:
	folio_unlock(folio);
	spin_unlock(vmf->ptl);
fallback:
	__split_huge_pmd(vma, vmf->pmd, vmf->address, false, NULL);
	return VM_FAULT_FALLBACK;
}

static inline bool can_change_pmd_writable(struct vm_area_struct *vma,
					   unsigned long addr, pmd_t pmd)
{
	struct page *page;

	if (WARN_ON_ONCE(!(vma->vm_flags & VM_WRITE)))
		return false;

	/* Don't touch entries that are not even readable (NUMA hinting). */
	if (pmd_protnone(pmd))
		return false;

	/* Do we need write faults for softdirty tracking? */
	if (pmd_needs_soft_dirty_wp(vma, pmd))
		return false;

	/* Do we need write faults for uffd-wp tracking? */
	if (userfaultfd_huge_pmd_wp(vma, pmd))
		return false;

	if (!(vma->vm_flags & VM_SHARED)) {
		/* See can_change_pte_writable(). */
		page = vm_normal_page_pmd(vma, addr, pmd);
		return page && PageAnon(page) && PageAnonExclusive(page);
	}

	/* See can_change_pte_writable(). */
	return pmd_dirty(pmd);
}

/* NUMA hinting page fault entry point for trans huge pmds */
vm_fault_t do_huge_pmd_numa_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct folio *folio;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;
	int nid = NUMA_NO_NODE;
	int target_nid, last_cpupid;
	pmd_t pmd, old_pmd;
	bool writable = false;
	int flags = 0;

	vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
	old_pmd = pmdp_get(vmf->pmd);

	if (unlikely(!pmd_same(old_pmd, vmf->orig_pmd))) {
		spin_unlock(vmf->ptl);
		return 0;
	}

	pmd = pmd_modify(old_pmd, vma->vm_page_prot);

	/*
	 * Detect now whether the PMD could be writable; this information
	 * is only valid while holding the PT lock.
	 */
	writable = pmd_write(pmd);
	if (!writable && vma_wants_manual_pte_write_upgrade(vma) &&
	    can_change_pmd_writable(vma, vmf->address, pmd))
		writable = true;

	folio = vm_normal_folio_pmd(vma, haddr, pmd);
	if (!folio)
		goto out_map;

	nid = folio_nid(folio);

	target_nid = numa_migrate_check(folio, vmf, haddr, &flags, writable,
					&last_cpupid);
	if (target_nid == NUMA_NO_NODE)
		goto out_map;
	if (migrate_misplaced_folio_prepare(folio, vma, target_nid)) {
		flags |= TNF_MIGRATE_FAIL;
		goto out_map;
	}
	/* The folio is isolated and isolation code holds a folio reference. */
	spin_unlock(vmf->ptl);
	writable = false;

	if (!migrate_misplaced_folio(folio, vma, target_nid)) {
		flags |= TNF_MIGRATED;
		nid = target_nid;
		task_numa_fault(last_cpupid, nid, HPAGE_PMD_NR, flags);
		return 0;
	}

	flags |= TNF_MIGRATE_FAIL;
	vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
	if (unlikely(!pmd_same(pmdp_get(vmf->pmd), vmf->orig_pmd))) {
		spin_unlock(vmf->ptl);
		return 0;
	}
out_map:
	/* Restore the PMD */
	pmd = pmd_modify(pmdp_get(vmf->pmd), vma->vm_page_prot);
	pmd = pmd_mkyoung(pmd);
	if (writable)
		pmd = pmd_mkwrite(pmd, vma);
	set_pmd_at(vma->vm_mm, haddr, vmf->pmd, pmd);
	update_mmu_cache_pmd(vma, vmf->address, vmf->pmd);
	spin_unlock(vmf->ptl);

	if (nid != NUMA_NO_NODE)
		task_numa_fault(last_cpupid, nid, HPAGE_PMD_NR, flags);
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
	struct folio *folio;
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

	folio = pmd_folio(orig_pmd);
	/*
	 * If other processes are mapping this folio, we couldn't discard
	 * the folio unless they all do MADV_FREE so let's skip the folio.
	 */
	if (folio_likely_mapped_shared(folio))
		goto out;

	if (!folio_trylock(folio))
		goto out;

	/*
	 * If user want to discard part-pages of THP, split it so MADV_FREE
	 * will deactivate only them.
	 */
	if (next - addr != HPAGE_PMD_SIZE) {
		folio_get(folio);
		spin_unlock(ptl);
		split_folio(folio);
		folio_unlock(folio);
		folio_put(folio);
		goto out_unlocked;
	}

	if (folio_test_dirty(folio))
		folio_clear_dirty(folio);
	folio_unlock(folio);

	if (pmd_young(orig_pmd) || pmd_dirty(orig_pmd)) {
		pmdp_invalidate(vma, addr, pmd);
		orig_pmd = pmd_mkold(orig_pmd);
		orig_pmd = pmd_mkclean(orig_pmd);

		set_pmd_at(mm, addr, pmd, orig_pmd);
		tlb_remove_pmd_tlb_entry(tlb, pmd, addr);
	}

	folio_mark_lazyfree(folio);
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
	arch_check_zapped_pmd(vma, orig_pmd);
	tlb_remove_pmd_tlb_entry(tlb, pmd, addr);
	if (vma_is_special_huge(vma)) {
		if (arch_needs_pgtable_deposit())
			zap_deposited_table(tlb->mm, pmd);
		spin_unlock(ptl);
	} else if (is_huge_zero_pmd(orig_pmd)) {
		zap_deposited_table(tlb->mm, pmd);
		spin_unlock(ptl);
	} else {
		struct folio *folio = NULL;
		int flush_needed = 1;

		if (pmd_present(orig_pmd)) {
			struct page *page = pmd_page(orig_pmd);

			folio = page_folio(page);
			folio_remove_rmap_pmd(folio, page, vma);
			WARN_ON_ONCE(folio_mapcount(folio) < 0);
			VM_BUG_ON_PAGE(!PageHead(page), page);
		} else if (thp_migration_supported()) {
			swp_entry_t entry;

			VM_BUG_ON(!is_pmd_migration_entry(orig_pmd));
			entry = pmd_to_swp_entry(orig_pmd);
			folio = pfn_swap_entry_folio(entry);
			flush_needed = 0;
		} else
			WARN_ONCE(1, "Non present huge pmd without pmd migration enabled!");

		if (folio_test_anon(folio)) {
			zap_deposited_table(tlb->mm, pmd);
			add_mm_counter(tlb->mm, MM_ANONPAGES, -HPAGE_PMD_NR);
		} else {
			if (arch_needs_pgtable_deposit())
				zap_deposited_table(tlb->mm, pmd);
			add_mm_counter(tlb->mm, mm_counter_file(folio),
				       -HPAGE_PMD_NR);
		}

		spin_unlock(ptl);
		if (flush_needed)
			tlb_remove_page_size(tlb, &folio->page, HPAGE_PMD_SIZE);
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
	 * should have released it; but move_page_tables() might have already
	 * inserted a page table, if racing against shmem/file collapse.
	 */
	if (!pmd_none(*new_pmd)) {
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
	bool prot_numa = cp_flags & MM_CP_PROT_NUMA;
	bool uffd_wp = cp_flags & MM_CP_UFFD_WP;
	bool uffd_wp_resolve = cp_flags & MM_CP_UFFD_WP_RESOLVE;
	int ret = 1;

	tlb_change_page_size(tlb, HPAGE_PMD_SIZE);

	if (prot_numa && !thp_migration_supported())
		return 1;

	ptl = __pmd_trans_huge_lock(pmd, vma);
	if (!ptl)
		return 0;

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
	if (is_swap_pmd(*pmd)) {
		swp_entry_t entry = pmd_to_swp_entry(*pmd);
		struct folio *folio = pfn_swap_entry_folio(entry);
		pmd_t newpmd;

		VM_BUG_ON(!is_pmd_migration_entry(*pmd));
		if (is_writable_migration_entry(entry)) {
			/*
			 * A protection check is difficult so
			 * just be safe and disable write
			 */
			if (folio_test_anon(folio))
				entry = make_readable_exclusive_migration_entry(swp_offset(entry));
			else
				entry = make_readable_migration_entry(swp_offset(entry));
			newpmd = swp_entry_to_pmd(entry);
			if (pmd_swp_soft_dirty(*pmd))
				newpmd = pmd_swp_mksoft_dirty(newpmd);
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
		struct folio *folio;
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

		folio = pmd_folio(*pmd);
		toptier = node_is_toptier(folio_nid(folio));
		/*
		 * Skip scanning top tier node if normal numa
		 * balancing is disabled
		 */
		if (!(sysctl_numa_balancing_mode & NUMA_BALANCING_NORMAL) &&
		    toptier)
			goto unlock;

		if (folio_use_access_time(folio))
			folio_xchg_access_time(folio,
					       jiffies_to_msecs(jiffies));
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
	if (uffd_wp)
		entry = pmd_mkuffd_wp(entry);
	else if (uffd_wp_resolve)
		/*
		 * Leave the write bit to be handled by PF interrupt
		 * handler, then things like COW could be properly
		 * handled.
		 */
		entry = pmd_clear_uffd_wp(entry);

	/* See change_pte_range(). */
	if ((cp_flags & MM_CP_TRY_CHANGE_WRITABLE) && !pmd_write(entry) &&
	    can_change_pmd_writable(vma, addr, entry))
		entry = pmd_mkwrite(entry, vma);

	ret = HPAGE_PMD_NR;
	set_pmd_at(mm, addr, pmd, entry);

	if (huge_pmd_needs_flush(oldpmd, entry))
		tlb_flush_pmd_range(tlb, addr, HPAGE_PMD_SIZE);
unlock:
	spin_unlock(ptl);
	return ret;
}

/*
 * Returns:
 *
 * - 0: if pud leaf changed from under us
 * - 1: if pud can be skipped
 * - HPAGE_PUD_NR: if pud was successfully processed
 */
#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
int change_huge_pud(struct mmu_gather *tlb, struct vm_area_struct *vma,
		    pud_t *pudp, unsigned long addr, pgprot_t newprot,
		    unsigned long cp_flags)
{
	struct mm_struct *mm = vma->vm_mm;
	pud_t oldpud, entry;
	spinlock_t *ptl;

	tlb_change_page_size(tlb, HPAGE_PUD_SIZE);

	/* NUMA balancing doesn't apply to dax */
	if (cp_flags & MM_CP_PROT_NUMA)
		return 1;

	/*
	 * Huge entries on userfault-wp only works with anonymous, while we
	 * don't have anonymous PUDs yet.
	 */
	if (WARN_ON_ONCE(cp_flags & MM_CP_UFFD_WP_ALL))
		return 1;

	ptl = __pud_trans_huge_lock(pudp, vma);
	if (!ptl)
		return 0;

	/*
	 * Can't clear PUD or it can race with concurrent zapping.  See
	 * change_huge_pmd().
	 */
	oldpud = pudp_invalidate(vma, addr, pudp);
	entry = pud_modify(oldpud, newprot);
	set_pud_at(mm, addr, pudp, entry);
	tlb_flush_pud_range(tlb, addr, HPAGE_PUD_SIZE);

	spin_unlock(ptl);
	return HPAGE_PUD_NR;
}
#endif

#ifdef CONFIG_USERFAULTFD
/*
 * The PT lock for src_pmd and dst_vma/src_vma (for reading) are locked by
 * the caller, but it must return after releasing the page_table_lock.
 * Just move the page from src_pmd to dst_pmd if possible.
 * Return zero if succeeded in moving the page, -EAGAIN if it needs to be
 * repeated by the caller, or other errors in case of failure.
 */
int move_pages_huge_pmd(struct mm_struct *mm, pmd_t *dst_pmd, pmd_t *src_pmd, pmd_t dst_pmdval,
			struct vm_area_struct *dst_vma, struct vm_area_struct *src_vma,
			unsigned long dst_addr, unsigned long src_addr)
{
	pmd_t _dst_pmd, src_pmdval;
	struct page *src_page;
	struct folio *src_folio;
	struct anon_vma *src_anon_vma;
	spinlock_t *src_ptl, *dst_ptl;
	pgtable_t src_pgtable;
	struct mmu_notifier_range range;
	int err = 0;

	src_pmdval = *src_pmd;
	src_ptl = pmd_lockptr(mm, src_pmd);

	lockdep_assert_held(src_ptl);
	vma_assert_locked(src_vma);
	vma_assert_locked(dst_vma);

	/* Sanity checks before the operation */
	if (WARN_ON_ONCE(!pmd_none(dst_pmdval)) || WARN_ON_ONCE(src_addr & ~HPAGE_PMD_MASK) ||
	    WARN_ON_ONCE(dst_addr & ~HPAGE_PMD_MASK)) {
		spin_unlock(src_ptl);
		return -EINVAL;
	}

	if (!pmd_trans_huge(src_pmdval)) {
		spin_unlock(src_ptl);
		if (is_pmd_migration_entry(src_pmdval)) {
			pmd_migration_entry_wait(mm, &src_pmdval);
			return -EAGAIN;
		}
		return -ENOENT;
	}

	src_page = pmd_page(src_pmdval);

	if (!is_huge_zero_pmd(src_pmdval)) {
		if (unlikely(!PageAnonExclusive(src_page))) {
			spin_unlock(src_ptl);
			return -EBUSY;
		}

		src_folio = page_folio(src_page);
		folio_get(src_folio);
	} else
		src_folio = NULL;

	spin_unlock(src_ptl);

	flush_cache_range(src_vma, src_addr, src_addr + HPAGE_PMD_SIZE);
	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, mm, src_addr,
				src_addr + HPAGE_PMD_SIZE);
	mmu_notifier_invalidate_range_start(&range);

	if (src_folio) {
		folio_lock(src_folio);

		/*
		 * split_huge_page walks the anon_vma chain without the page
		 * lock. Serialize against it with the anon_vma lock, the page
		 * lock is not enough.
		 */
		src_anon_vma = folio_get_anon_vma(src_folio);
		if (!src_anon_vma) {
			err = -EAGAIN;
			goto unlock_folio;
		}
		anon_vma_lock_write(src_anon_vma);
	} else
		src_anon_vma = NULL;

	dst_ptl = pmd_lockptr(mm, dst_pmd);
	double_pt_lock(src_ptl, dst_ptl);
	if (unlikely(!pmd_same(*src_pmd, src_pmdval) ||
		     !pmd_same(*dst_pmd, dst_pmdval))) {
		err = -EAGAIN;
		goto unlock_ptls;
	}
	if (src_folio) {
		if (folio_maybe_dma_pinned(src_folio) ||
		    !PageAnonExclusive(&src_folio->page)) {
			err = -EBUSY;
			goto unlock_ptls;
		}

		if (WARN_ON_ONCE(!folio_test_head(src_folio)) ||
		    WARN_ON_ONCE(!folio_test_anon(src_folio))) {
			err = -EBUSY;
			goto unlock_ptls;
		}

		src_pmdval = pmdp_huge_clear_flush(src_vma, src_addr, src_pmd);
		/* Folio got pinned from under us. Put it back and fail the move. */
		if (folio_maybe_dma_pinned(src_folio)) {
			set_pmd_at(mm, src_addr, src_pmd, src_pmdval);
			err = -EBUSY;
			goto unlock_ptls;
		}

		folio_move_anon_rmap(src_folio, dst_vma);
		src_folio->index = linear_page_index(dst_vma, dst_addr);

		_dst_pmd = mk_huge_pmd(&src_folio->page, dst_vma->vm_page_prot);
		/* Follow mremap() behavior and treat the entry dirty after the move */
		_dst_pmd = pmd_mkwrite(pmd_mkdirty(_dst_pmd), dst_vma);
	} else {
		src_pmdval = pmdp_huge_clear_flush(src_vma, src_addr, src_pmd);
		_dst_pmd = mk_huge_pmd(src_page, dst_vma->vm_page_prot);
	}
	set_pmd_at(mm, dst_addr, dst_pmd, _dst_pmd);

	src_pgtable = pgtable_trans_huge_withdraw(mm, src_pmd);
	pgtable_trans_huge_deposit(mm, dst_pmd, src_pgtable);
unlock_ptls:
	double_pt_unlock(src_ptl, dst_ptl);
	if (src_anon_vma) {
		anon_vma_unlock_write(src_anon_vma);
		put_anon_vma(src_anon_vma);
	}
unlock_folio:
	/* unblock rmap walks */
	if (src_folio)
		folio_unlock(src_folio);
	mmu_notifier_invalidate_range_end(&range);
	if (src_folio)
		folio_put(src_folio);
	return err;
}
#endif /* CONFIG_USERFAULTFD */

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
	pud_t orig_pud;

	ptl = __pud_trans_huge_lock(pud, vma);
	if (!ptl)
		return 0;

	orig_pud = pudp_huge_get_and_clear_full(vma, addr, pud, tlb->fullmm);
	arch_check_zapped_pud(vma, orig_pud);
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

	pudp_huge_clear_flush(vma, haddr, pud);
}

void __split_huge_pud(struct vm_area_struct *vma, pud_t *pud,
		unsigned long address)
{
	spinlock_t *ptl;
	struct mmu_notifier_range range;

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma->vm_mm,
				address & HPAGE_PUD_MASK,
				(address & HPAGE_PUD_MASK) + HPAGE_PUD_SIZE);
	mmu_notifier_invalidate_range_start(&range);
	ptl = pud_lock(vma->vm_mm, pud);
	if (unlikely(!pud_trans_huge(*pud) && !pud_devmap(*pud)))
		goto out;
	__split_huge_pud_locked(vma, pud, range.start);

out:
	spin_unlock(ptl);
	mmu_notifier_invalidate_range_end(&range);
}
#else
void __split_huge_pud(struct vm_area_struct *vma, pud_t *pud,
		unsigned long address)
{
}
#endif /* CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */

static void __split_huge_zero_page_pmd(struct vm_area_struct *vma,
		unsigned long haddr, pmd_t *pmd)
{
	struct mm_struct *mm = vma->vm_mm;
	pgtable_t pgtable;
	pmd_t _pmd, old_pmd;
	unsigned long addr;
	pte_t *pte;
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

	pte = pte_offset_map(&_pmd, haddr);
	VM_BUG_ON(!pte);
	for (i = 0, addr = haddr; i < HPAGE_PMD_NR; i++, addr += PAGE_SIZE) {
		pte_t entry;

		entry = pfn_pte(my_zero_pfn(addr), vma->vm_page_prot);
		entry = pte_mkspecial(entry);
		if (pmd_uffd_wp(old_pmd))
			entry = pte_mkuffd_wp(entry);
		VM_BUG_ON(!pte_none(ptep_get(pte)));
		set_pte_at(mm, addr, pte, entry);
		pte++;
	}
	pte_unmap(pte - 1);
	smp_wmb(); /* make pte visible before pmd */
	pmd_populate(mm, pmd, pgtable);
}

static void __split_huge_pmd_locked(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long haddr, bool freeze)
{
	struct mm_struct *mm = vma->vm_mm;
	struct folio *folio;
	struct page *page;
	pgtable_t pgtable;
	pmd_t old_pmd, _pmd;
	bool young, write, soft_dirty, pmd_migration = false, uffd_wp = false;
	bool anon_exclusive = false, dirty = false;
	unsigned long addr;
	pte_t *pte;
	int i;

	VM_BUG_ON(haddr & ~HPAGE_PMD_MASK);
	VM_BUG_ON_VMA(vma->vm_start > haddr, vma);
	VM_BUG_ON_VMA(vma->vm_end < haddr + HPAGE_PMD_SIZE, vma);
	VM_BUG_ON(!is_pmd_migration_entry(*pmd) && !pmd_trans_huge(*pmd)
				&& !pmd_devmap(*pmd));

	count_vm_event(THP_SPLIT_PMD);

	if (!vma_is_anonymous(vma)) {
		old_pmd = pmdp_huge_clear_flush(vma, haddr, pmd);
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
			folio = pfn_swap_entry_folio(entry);
		} else {
			page = pmd_page(old_pmd);
			folio = page_folio(page);
			if (!folio_test_dirty(folio) && pmd_dirty(old_pmd))
				folio_mark_dirty(folio);
			if (!folio_test_referenced(folio) && pmd_young(old_pmd))
				folio_set_referenced(folio);
			folio_remove_rmap_pmd(folio, page, vma);
			folio_put(folio);
		}
		add_mm_counter(mm, mm_counter_file(folio), -HPAGE_PMD_NR);
		return;
	}

	if (is_huge_zero_pmd(*pmd)) {
		/*
		 * FIXME: Do we want to invalidate secondary mmu by calling
		 * mmu_notifier_arch_invalidate_secondary_tlbs() see comments below
		 * inside __split_huge_pmd() ?
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
		folio = page_folio(page);
		if (pmd_dirty(old_pmd)) {
			dirty = true;
			folio_set_dirty(folio);
		}
		write = pmd_write(old_pmd);
		young = pmd_young(old_pmd);
		soft_dirty = pmd_soft_dirty(old_pmd);
		uffd_wp = pmd_uffd_wp(old_pmd);

		VM_WARN_ON_FOLIO(!folio_ref_count(folio), folio);
		VM_WARN_ON_FOLIO(!folio_test_anon(folio), folio);

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
		 * See folio_try_share_anon_rmap_pmd(): invalidate PMD first.
		 */
		anon_exclusive = PageAnonExclusive(page);
		if (freeze && anon_exclusive &&
		    folio_try_share_anon_rmap_pmd(folio, page))
			freeze = false;
		if (!freeze) {
			rmap_t rmap_flags = RMAP_NONE;

			folio_ref_add(folio, HPAGE_PMD_NR - 1);
			if (anon_exclusive)
				rmap_flags |= RMAP_EXCLUSIVE;
			folio_add_anon_rmap_ptes(folio, page, HPAGE_PMD_NR,
						 vma, haddr, rmap_flags);
		}
	}

	/*
	 * Withdraw the table only after we mark the pmd entry invalid.
	 * This's critical for some architectures (Power).
	 */
	pgtable = pgtable_trans_huge_withdraw(mm, pmd);
	pmd_populate(mm, &_pmd, pgtable);

	pte = pte_offset_map(&_pmd, haddr);
	VM_BUG_ON(!pte);

	/*
	 * Note that NUMA hinting access restrictions are not transferred to
	 * avoid any possibility of altering permissions across VMAs.
	 */
	if (freeze || pmd_migration) {
		for (i = 0, addr = haddr; i < HPAGE_PMD_NR; i++, addr += PAGE_SIZE) {
			pte_t entry;
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

			VM_WARN_ON(!pte_none(ptep_get(pte + i)));
			set_pte_at(mm, addr, pte + i, entry);
		}
	} else {
		pte_t entry;

		entry = mk_pte(page, READ_ONCE(vma->vm_page_prot));
		if (write)
			entry = pte_mkwrite(entry, vma);
		if (!young)
			entry = pte_mkold(entry);
		/* NOTE: this may set soft-dirty too on some archs */
		if (dirty)
			entry = pte_mkdirty(entry);
		if (soft_dirty)
			entry = pte_mksoft_dirty(entry);
		if (uffd_wp)
			entry = pte_mkuffd_wp(entry);

		for (i = 0; i < HPAGE_PMD_NR; i++)
			VM_WARN_ON(!pte_none(ptep_get(pte + i)));

		set_ptes(mm, haddr, pte, entry, HPAGE_PMD_NR);
	}
	pte_unmap(pte);

	if (!pmd_migration)
		folio_remove_rmap_pmd(folio, page, vma);
	if (freeze)
		put_page(page);

	smp_wmb(); /* make pte visible before pmd */
	pmd_populate(mm, pmd, pgtable);
}

void split_huge_pmd_locked(struct vm_area_struct *vma, unsigned long address,
			   pmd_t *pmd, bool freeze, struct folio *folio)
{
	VM_WARN_ON_ONCE(folio && !folio_test_pmd_mappable(folio));
	VM_WARN_ON_ONCE(!IS_ALIGNED(address, HPAGE_PMD_SIZE));
	VM_WARN_ON_ONCE(folio && !folio_test_locked(folio));
	VM_BUG_ON(freeze && !folio);

	/*
	 * When the caller requests to set up a migration entry, we
	 * require a folio to check the PMD against. Otherwise, there
	 * is a risk of replacing the wrong folio.
	 */
	if (pmd_trans_huge(*pmd) || pmd_devmap(*pmd) ||
	    is_pmd_migration_entry(*pmd)) {
		if (folio && folio != pmd_folio(*pmd))
			return;
		__split_huge_pmd_locked(vma, pmd, address, freeze);
	}
}

void __split_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long address, bool freeze, struct folio *folio)
{
	spinlock_t *ptl;
	struct mmu_notifier_range range;

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma->vm_mm,
				address & HPAGE_PMD_MASK,
				(address & HPAGE_PMD_MASK) + HPAGE_PMD_SIZE);
	mmu_notifier_invalidate_range_start(&range);
	ptl = pmd_lock(vma->vm_mm, pmd);
	split_huge_pmd_locked(vma, range.start, pmd, freeze, folio);
	spin_unlock(ptl);
	mmu_notifier_invalidate_range_end(&range);
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
	enum ttu_flags ttu_flags = TTU_RMAP_LOCKED | TTU_SYNC |
		TTU_BATCH_FLUSH;

	VM_BUG_ON_FOLIO(!folio_test_large(folio), folio);

	if (folio_test_pmd_mappable(folio))
		ttu_flags |= TTU_SPLIT_HUGE_PMD;

	/*
	 * Anon pages need migration entries to preserve them, but file
	 * pages can simply be left unmapped, then faulted back on demand.
	 * If that is ever changed (perhaps for mlock), update remap_page().
	 */
	if (folio_test_anon(folio))
		try_to_migrate(folio, ttu_flags);
	else
		try_to_unmap(folio, ttu_flags | TTU_IGNORE_MLOCK);

	try_to_unmap_flush();
}

static bool __discard_anon_folio_pmd_locked(struct vm_area_struct *vma,
					    unsigned long addr, pmd_t *pmdp,
					    struct folio *folio)
{
	struct mm_struct *mm = vma->vm_mm;
	int ref_count, map_count;
	pmd_t orig_pmd = *pmdp;

	if (folio_test_dirty(folio) || pmd_dirty(orig_pmd))
		return false;

	orig_pmd = pmdp_huge_clear_flush(vma, addr, pmdp);

	/*
	 * Syncing against concurrent GUP-fast:
	 * - clear PMD; barrier; read refcount
	 * - inc refcount; barrier; read PMD
	 */
	smp_mb();

	ref_count = folio_ref_count(folio);
	map_count = folio_mapcount(folio);

	/*
	 * Order reads for folio refcount and dirty flag
	 * (see comments in __remove_mapping()).
	 */
	smp_rmb();

	/*
	 * If the folio or its PMD is redirtied at this point, or if there
	 * are unexpected references, we will give up to discard this folio
	 * and remap it.
	 *
	 * The only folio refs must be one from isolation plus the rmap(s).
	 */
	if (folio_test_dirty(folio) || pmd_dirty(orig_pmd) ||
	    ref_count != map_count + 1) {
		set_pmd_at(mm, addr, pmdp, orig_pmd);
		return false;
	}

	folio_remove_rmap_pmd(folio, pmd_page(orig_pmd), vma);
	zap_deposited_table(mm, pmdp);
	add_mm_counter(mm, MM_ANONPAGES, -HPAGE_PMD_NR);
	if (vma->vm_flags & VM_LOCKED)
		mlock_drain_local();
	folio_put(folio);

	return true;
}

bool unmap_huge_pmd_locked(struct vm_area_struct *vma, unsigned long addr,
			   pmd_t *pmdp, struct folio *folio)
{
	VM_WARN_ON_FOLIO(!folio_test_pmd_mappable(folio), folio);
	VM_WARN_ON_FOLIO(!folio_test_locked(folio), folio);
	VM_WARN_ON_ONCE(!IS_ALIGNED(addr, HPAGE_PMD_SIZE));

	if (folio_test_anon(folio) && !folio_test_swapbacked(folio))
		return __discard_anon_folio_pmd_locked(vma, addr, pmdp, folio);

	return false;
}

static void remap_page(struct folio *folio, unsigned long nr, int flags)
{
	int i = 0;

	/* If unmap_folio() uses try_to_migrate() on file, remove this check */
	if (!folio_test_anon(folio))
		return;
	for (;;) {
		remove_migration_ptes(folio, folio, RMP_LOCKED | flags);
		i += folio_nr_pages(folio);
		if (i >= nr)
			break;
		folio = folio_next(folio);
	}
}

static void lru_add_page_tail(struct folio *folio, struct page *tail,
		struct lruvec *lruvec, struct list_head *list)
{
	VM_BUG_ON_FOLIO(!folio_test_large(folio), folio);
	VM_BUG_ON_FOLIO(PageLRU(tail), folio);
	lockdep_assert_held(&lruvec->lru_lock);

	if (list) {
		/* page reclaim is reclaiming a huge page */
		VM_WARN_ON(folio_test_lru(folio));
		get_page(tail);
		list_add_tail(&tail->lru, list);
	} else {
		/* head is still on lru (and we have it frozen) */
		VM_WARN_ON(!folio_test_lru(folio));
		if (folio_test_unevictable(folio))
			tail->mlock_count = 0;
		else
			list_add_tail(&tail->lru, &folio->lru);
		SetPageLRU(tail);
	}
}

static void __split_huge_page_tail(struct folio *folio, int tail,
		struct lruvec *lruvec, struct list_head *list,
		unsigned int new_order)
{
	struct page *head = &folio->page;
	struct page *page_tail = head + tail;
	/*
	 * Careful: new_folio is not a "real" folio before we cleared PageTail.
	 * Don't pass it around before clear_compound_head().
	 */
	struct folio *new_folio = (struct folio *)page_tail;

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
#ifdef CONFIG_ARCH_USES_PG_ARCH_2
			 (1L << PG_arch_2) |
#endif
#ifdef CONFIG_ARCH_USES_PG_ARCH_3
			 (1L << PG_arch_3) |
#endif
			 (1L << PG_dirty) |
			 LRU_GEN_MASK | LRU_REFS_MASK));

	/* ->mapping in first and second tail page is replaced by other uses */
	VM_BUG_ON_PAGE(tail > 2 && page_tail->mapping != TAIL_MAPPING,
			page_tail);
	page_tail->mapping = head->mapping;
	page_tail->index = head->index + tail;

	/*
	 * page->private should not be set in tail pages. Fix up and warn once
	 * if private is unexpectedly set.
	 */
	if (unlikely(page_tail->private)) {
		VM_WARN_ON_ONCE_PAGE(true, page_tail);
		page_tail->private = 0;
	}
	if (folio_test_swapcache(folio))
		new_folio->swap.val = folio->swap.val + tail;

	/* Page flags must be visible before we make the page non-compound. */
	smp_wmb();

	/*
	 * Clear PageTail before unfreezing page refcount.
	 *
	 * After successful get_page_unless_zero() might follow put_page()
	 * which needs correct compound_head().
	 */
	clear_compound_head(page_tail);
	if (new_order) {
		prep_compound_page(page_tail, new_order);
		folio_set_large_rmappable(new_folio);
	}

	/* Finally unfreeze refcount. Additional reference from page cache. */
	page_ref_unfreeze(page_tail,
		1 + ((!folio_test_anon(folio) || folio_test_swapcache(folio)) ?
			     folio_nr_pages(new_folio) : 0));

	if (folio_test_young(folio))
		folio_set_young(new_folio);
	if (folio_test_idle(folio))
		folio_set_idle(new_folio);

	folio_xchg_last_cpupid(new_folio, folio_last_cpupid(folio));

	/*
	 * always add to the tail because some iterators expect new
	 * pages to show after the currently processed elements - e.g.
	 * migrate_pages
	 */
	lru_add_page_tail(folio, page_tail, lruvec, list);
}

static void __split_huge_page(struct page *page, struct list_head *list,
		pgoff_t end, unsigned int new_order)
{
	struct folio *folio = page_folio(page);
	struct page *head = &folio->page;
	struct lruvec *lruvec;
	struct address_space *swap_cache = NULL;
	unsigned long offset = 0;
	int i, nr_dropped = 0;
	unsigned int new_nr = 1 << new_order;
	int order = folio_order(folio);
	unsigned int nr = 1 << order;

	/* complete memcg works before add pages to LRU */
	split_page_memcg(head, order, new_order);

	if (folio_test_anon(folio) && folio_test_swapcache(folio)) {
		offset = swap_cache_index(folio->swap);
		swap_cache = swap_address_space(folio->swap);
		xa_lock(&swap_cache->i_pages);
	}

	/* lock lru list/PageCompound, ref frozen by page_ref_freeze */
	lruvec = folio_lruvec_lock(folio);

	ClearPageHasHWPoisoned(head);

	for (i = nr - new_nr; i >= new_nr; i -= new_nr) {
		__split_huge_page_tail(folio, i, lruvec, list, new_order);
		/* Some pages can be beyond EOF: drop them from page cache */
		if (head[i].index >= end) {
			struct folio *tail = page_folio(head + i);

			if (shmem_mapping(folio->mapping))
				nr_dropped++;
			else if (folio_test_clear_dirty(tail))
				folio_account_cleaned(tail,
					inode_to_wb(folio->mapping->host));
			__filemap_remove_folio(tail, NULL);
			folio_put(tail);
		} else if (!PageAnon(page)) {
			__xa_store(&folio->mapping->i_pages, head[i].index,
					head + i, 0);
		} else if (swap_cache) {
			__xa_store(&swap_cache->i_pages, offset + i,
					head + i, 0);
		}
	}

	if (!new_order)
		ClearPageCompound(head);
	else {
		struct folio *new_folio = (struct folio *)head;

		folio_set_order(new_folio, new_order);
	}
	unlock_page_lruvec(lruvec);
	/* Caller disabled irqs, so they are still disabled here */

	split_page_owner(head, order, new_order);
	pgalloc_tag_split(folio, order, new_order);

	/* See comment in __split_huge_page_tail() */
	if (folio_test_anon(folio)) {
		/* Additional pin to swap cache */
		if (folio_test_swapcache(folio)) {
			folio_ref_add(folio, 1 + new_nr);
			xa_unlock(&swap_cache->i_pages);
		} else {
			folio_ref_inc(folio);
		}
	} else {
		/* Additional pin to page cache */
		folio_ref_add(folio, 1 + new_nr);
		xa_unlock(&folio->mapping->i_pages);
	}
	local_irq_enable();

	if (nr_dropped)
		shmem_uncharge(folio->mapping->host, nr_dropped);
	remap_page(folio, nr, PageAnon(head) ? RMP_USE_SHARED_ZEROPAGE : 0);

	/*
	 * set page to its compound_head when split to non order-0 pages, so
	 * we can skip unlocking it below, since PG_locked is transferred to
	 * the compound_head of the page and the caller will unlock it.
	 */
	if (new_order)
		page = compound_head(page);

	for (i = 0; i < nr; i += new_nr) {
		struct page *subpage = head + i;
		struct folio *new_folio = page_folio(subpage);
		if (subpage == page)
			continue;
		folio_unlock(new_folio);

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
bool can_split_folio(struct folio *folio, int caller_pins, int *pextra_pins)
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
	return folio_mapcount(folio) == folio_ref_count(folio) - extra_pins -
					caller_pins;
}

/*
 * This function splits a large folio into smaller folios of order @new_order.
 * @page can point to any page of the large folio to split. The split operation
 * does not change the position of @page.
 *
 * Prerequisites:
 *
 * 1) The caller must hold a reference on the @page's owning folio, also known
 *    as the large folio.
 *
 * 2) The large folio must be locked.
 *
 * 3) The folio must not be pinned. Any unexpected folio references, including
 *    GUP pins, will result in the folio not getting split; instead, the caller
 *    will receive an -EAGAIN.
 *
 * 4) @new_order > 1, usually. Splitting to order-1 anonymous folios is not
 *    supported for non-file-backed folios, because folio->_deferred_list, which
 *    is used by partially mapped folios, is stored in subpage 2, but an order-1
 *    folio only has subpages 0 and 1. File-backed order-1 folios are supported,
 *    since they do not use _deferred_list.
 *
 * After splitting, the caller's folio reference will be transferred to @page,
 * resulting in a raised refcount of @page after this call. The other pages may
 * be freed if they are not mapped.
 *
 * If @list is null, tail pages will be added to LRU list, otherwise, to @list.
 *
 * Pages in @new_order will inherit the mapping, flags, and so on from the
 * huge page.
 *
 * Returns 0 if the huge page was split successfully.
 *
 * Returns -EAGAIN if the folio has unexpected reference (e.g., GUP) or if
 * the folio was concurrently removed from the page cache.
 *
 * Returns -EBUSY when trying to split the huge zeropage, if the folio is
 * under writeback, if fs-specific folio metadata cannot currently be
 * released, or if some unexpected race happened (e.g., anon VMA disappeared,
 * truncation).
 *
 * Callers should ensure that the order respects the address space mapping
 * min-order if one is set for non-anonymous folios.
 *
 * Returns -EINVAL when trying to split to an order that is incompatible
 * with the folio. Splitting to order 0 is compatible with all folios.
 */
int split_huge_page_to_list_to_order(struct page *page, struct list_head *list,
				     unsigned int new_order)
{
	struct folio *folio = page_folio(page);
	struct deferred_split *ds_queue = get_deferred_split_queue(folio);
	/* reset xarray order to new order after split */
	XA_STATE_ORDER(xas, &folio->mapping->i_pages, folio->index, new_order);
	bool is_anon = folio_test_anon(folio);
	struct address_space *mapping = NULL;
	struct anon_vma *anon_vma = NULL;
	int order = folio_order(folio);
	int extra_pins, ret;
	pgoff_t end;
	bool is_hzp;

	VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);
	VM_BUG_ON_FOLIO(!folio_test_large(folio), folio);

	if (new_order >= folio_order(folio))
		return -EINVAL;

	if (is_anon) {
		/* order-1 is not supported for anonymous THP. */
		if (new_order == 1) {
			VM_WARN_ONCE(1, "Cannot split to order-1 folio");
			return -EINVAL;
		}
	} else if (new_order) {
		/* Split shmem folio to non-zero order not supported */
		if (shmem_mapping(folio->mapping)) {
			VM_WARN_ONCE(1,
				"Cannot split shmem folio to non-0 order");
			return -EINVAL;
		}
		/*
		 * No split if the file system does not support large folio.
		 * Note that we might still have THPs in such mappings due to
		 * CONFIG_READ_ONLY_THP_FOR_FS. But in that case, the mapping
		 * does not actually support large folios properly.
		 */
		if (IS_ENABLED(CONFIG_READ_ONLY_THP_FOR_FS) &&
		    !mapping_large_folio_support(folio->mapping)) {
			VM_WARN_ONCE(1,
				"Cannot split file folio to non-0 order");
			return -EINVAL;
		}
	}

	/* Only swapping a whole PMD-mapped folio is supported */
	if (folio_test_swapcache(folio) && new_order)
		return -EINVAL;

	is_hzp = is_huge_zero_folio(folio);
	if (is_hzp) {
		pr_warn_ratelimited("Called split_huge_page for huge zero page\n");
		return -EBUSY;
	}

	if (folio_test_writeback(folio))
		return -EBUSY;

	if (is_anon) {
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
		unsigned int min_order;
		gfp_t gfp;

		mapping = folio->mapping;

		/* Truncated ? */
		if (!mapping) {
			ret = -EBUSY;
			goto out;
		}

		min_order = mapping_min_folio_order(folio->mapping);
		if (new_order < min_order) {
			VM_WARN_ONCE(1, "Cannot split mapped folio below min-order: %u",
				     min_order);
			ret = -EINVAL;
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
	if (!can_split_folio(folio, 1, &extra_pins)) {
		ret = -EAGAIN;
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
		if (folio_order(folio) > 1 &&
		    !list_empty(&folio->_deferred_list)) {
			ds_queue->split_queue_len--;
			if (folio_test_partially_mapped(folio)) {
				__folio_clear_partially_mapped(folio);
				mod_mthp_stat(folio_order(folio),
					      MTHP_STAT_NR_ANON_PARTIALLY_MAPPED, -1);
			}
			/*
			 * Reinitialize page_deferred_list after removing the
			 * page from the split_queue, otherwise a subsequent
			 * split will see list corruption when checking the
			 * page_deferred_list.
			 */
			list_del_init(&folio->_deferred_list);
		}
		spin_unlock(&ds_queue->split_queue_lock);
		if (mapping) {
			int nr = folio_nr_pages(folio);

			xas_split(&xas, folio, folio_order(folio));
			if (folio_test_pmd_mappable(folio) &&
			    new_order < HPAGE_PMD_ORDER) {
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

		if (is_anon) {
			mod_mthp_stat(order, MTHP_STAT_NR_ANON, -1);
			mod_mthp_stat(new_order, MTHP_STAT_NR_ANON, 1 << (order - new_order));
		}
		__split_huge_page(page, list, end, new_order);
		ret = 0;
	} else {
		spin_unlock(&ds_queue->split_queue_lock);
fail:
		if (mapping)
			xas_unlock(&xas);
		local_irq_enable();
		remap_page(folio, folio_nr_pages(folio), 0);
		ret = -EAGAIN;
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
	if (order == HPAGE_PMD_ORDER)
		count_vm_event(!ret ? THP_SPLIT_PAGE : THP_SPLIT_PAGE_FAILED);
	count_mthp_stat(order, !ret ? MTHP_STAT_SPLIT : MTHP_STAT_SPLIT_FAILED);
	return ret;
}

int min_order_for_split(struct folio *folio)
{
	if (folio_test_anon(folio))
		return 0;

	if (!folio->mapping) {
		if (folio_test_pmd_mappable(folio))
			count_vm_event(THP_SPLIT_PAGE_FAILED);
		return -EBUSY;
	}

	return mapping_min_folio_order(folio->mapping);
}

int split_folio_to_list(struct folio *folio, struct list_head *list)
{
	int ret = min_order_for_split(folio);

	if (ret < 0)
		return ret;

	return split_huge_page_to_list_to_order(&folio->page, list, ret);
}

void __folio_undo_large_rmappable(struct folio *folio)
{
	struct deferred_split *ds_queue;
	unsigned long flags;

	ds_queue = get_deferred_split_queue(folio);
	spin_lock_irqsave(&ds_queue->split_queue_lock, flags);
	if (!list_empty(&folio->_deferred_list)) {
		ds_queue->split_queue_len--;
		if (folio_test_partially_mapped(folio)) {
			__folio_clear_partially_mapped(folio);
			mod_mthp_stat(folio_order(folio),
				      MTHP_STAT_NR_ANON_PARTIALLY_MAPPED, -1);
		}
		list_del_init(&folio->_deferred_list);
	}
	spin_unlock_irqrestore(&ds_queue->split_queue_lock, flags);
}

/* partially_mapped=false won't clear PG_partially_mapped folio flag */
void deferred_split_folio(struct folio *folio, bool partially_mapped)
{
	struct deferred_split *ds_queue = get_deferred_split_queue(folio);
#ifdef CONFIG_MEMCG
	struct mem_cgroup *memcg = folio_memcg(folio);
#endif
	unsigned long flags;

	/*
	 * Order 1 folios have no space for a deferred list, but we also
	 * won't waste much memory by not adding them to the deferred list.
	 */
	if (folio_order(folio) <= 1)
		return;

	if (!partially_mapped && !split_underused_thp)
		return;

	/*
	 * The try_to_unmap() in page reclaim path might reach here too,
	 * this may cause a race condition to corrupt deferred split queue.
	 * And, if page reclaim is already handling the same folio, it is
	 * unnecessary to handle it again in shrinker.
	 *
	 * Check the swapcache flag to determine if the folio is being
	 * handled by page reclaim since THP swap would add the folio into
	 * swap cache before calling try_to_unmap().
	 */
	if (folio_test_swapcache(folio))
		return;

	spin_lock_irqsave(&ds_queue->split_queue_lock, flags);
	if (partially_mapped) {
		if (!folio_test_partially_mapped(folio)) {
			__folio_set_partially_mapped(folio);
			if (folio_test_pmd_mappable(folio))
				count_vm_event(THP_DEFERRED_SPLIT_PAGE);
			count_mthp_stat(folio_order(folio), MTHP_STAT_SPLIT_DEFERRED);
			mod_mthp_stat(folio_order(folio), MTHP_STAT_NR_ANON_PARTIALLY_MAPPED, 1);

		}
	} else {
		/* partially mapped folios cannot become non-partially mapped */
		VM_WARN_ON_FOLIO(folio_test_partially_mapped(folio), folio);
	}
	if (list_empty(&folio->_deferred_list)) {
		list_add_tail(&folio->_deferred_list, &ds_queue->split_queue);
		ds_queue->split_queue_len++;
#ifdef CONFIG_MEMCG
		if (memcg)
			set_shrinker_bit(memcg, folio_nid(folio),
					 deferred_split_shrinker->id);
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

static bool thp_underused(struct folio *folio)
{
	int num_zero_pages = 0, num_filled_pages = 0;
	void *kaddr;
	int i;

	if (khugepaged_max_ptes_none == HPAGE_PMD_NR - 1)
		return false;

	for (i = 0; i < folio_nr_pages(folio); i++) {
		kaddr = kmap_local_folio(folio, i * PAGE_SIZE);
		if (!memchr_inv(kaddr, 0, PAGE_SIZE)) {
			num_zero_pages++;
			if (num_zero_pages > khugepaged_max_ptes_none) {
				kunmap_local(kaddr);
				return true;
			}
		} else {
			/*
			 * Another path for early exit once the number
			 * of non-zero filled pages exceeds threshold.
			 */
			num_filled_pages++;
			if (num_filled_pages >= HPAGE_PMD_NR - khugepaged_max_ptes_none) {
				kunmap_local(kaddr);
				return false;
			}
		}
		kunmap_local(kaddr);
	}
	return false;
}

static unsigned long deferred_split_scan(struct shrinker *shrink,
		struct shrink_control *sc)
{
	struct pglist_data *pgdata = NODE_DATA(sc->nid);
	struct deferred_split *ds_queue = &pgdata->deferred_split_queue;
	unsigned long flags;
	LIST_HEAD(list);
	struct folio *folio, *next;
	int split = 0;

#ifdef CONFIG_MEMCG
	if (sc->memcg)
		ds_queue = &sc->memcg->deferred_split_queue;
#endif

	spin_lock_irqsave(&ds_queue->split_queue_lock, flags);
	/* Take pin on all head pages to avoid freeing them under us */
	list_for_each_entry_safe(folio, next, &ds_queue->split_queue,
							_deferred_list) {
		if (folio_try_get(folio)) {
			list_move(&folio->_deferred_list, &list);
		} else {
			/* We lost race with folio_put() */
			if (folio_test_partially_mapped(folio)) {
				__folio_clear_partially_mapped(folio);
				mod_mthp_stat(folio_order(folio),
					      MTHP_STAT_NR_ANON_PARTIALLY_MAPPED, -1);
			}
			list_del_init(&folio->_deferred_list);
			ds_queue->split_queue_len--;
		}
		if (!--sc->nr_to_scan)
			break;
	}
	spin_unlock_irqrestore(&ds_queue->split_queue_lock, flags);

	list_for_each_entry_safe(folio, next, &list, _deferred_list) {
		bool did_split = false;
		bool underused = false;

		if (!folio_test_partially_mapped(folio)) {
			underused = thp_underused(folio);
			if (!underused)
				goto next;
		}
		if (!folio_trylock(folio))
			goto next;
		if (!split_folio(folio)) {
			did_split = true;
			if (underused)
				count_vm_event(THP_UNDERUSED_SPLIT_PAGE);
			split++;
		}
		folio_unlock(folio);
next:
		/*
		 * split_folio() removes folio from list on success.
		 * Only add back to the queue if folio is partially mapped.
		 * If thp_underused returns false, or if split_folio fails
		 * in the case it was underused, then consider it used and
		 * don't add it back to split_queue.
		 */
		if (!did_split && !folio_test_partially_mapped(folio)) {
			list_del_init(&folio->_deferred_list);
			ds_queue->split_queue_len--;
		}
		folio_put(folio);
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

#ifdef CONFIG_DEBUG_FS
static void split_huge_pages_all(void)
{
	struct zone *zone;
	struct page *page;
	struct folio *folio;
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
			if (!page || PageTail(page))
				continue;
			folio = page_folio(page);
			if (!folio_try_get(folio))
				continue;

			if (unlikely(page_folio(page) != folio))
				goto next;

			if (zone != folio_zone(folio))
				goto next;

			if (!folio_test_large(folio)
				|| folio_test_hugetlb(folio)
				|| !folio_test_lru(folio))
				goto next;

			total++;
			folio_lock(folio);
			nr_pages = folio_nr_pages(folio);
			if (!split_folio(folio))
				split++;
			pfn += nr_pages - 1;
			folio_unlock(folio);
next:
			folio_put(folio);
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
				unsigned long vaddr_end, unsigned int new_order)
{
	int ret = 0;
	struct task_struct *task;
	struct mm_struct *mm;
	unsigned long total = 0, split = 0;
	unsigned long addr;

	vaddr_start &= PAGE_MASK;
	vaddr_end &= PAGE_MASK;

	task = find_get_task_by_vpid(pid);
	if (!task) {
		ret = -ESRCH;
		goto out;
	}

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
		struct folio_walk fw;
		struct folio *folio;
		struct address_space *mapping;
		unsigned int target_order = new_order;

		if (!vma)
			break;

		/* skip special VMA and hugetlb VMA */
		if (vma_not_suitable_for_thp_split(vma)) {
			addr = vma->vm_end;
			continue;
		}

		folio = folio_walk_start(&fw, vma, addr, 0);
		if (!folio)
			continue;

		if (!is_transparent_hugepage(folio))
			goto next;

		if (!folio_test_anon(folio)) {
			mapping = folio->mapping;
			target_order = max(new_order,
					   mapping_min_folio_order(mapping));
		}

		if (target_order >= folio_order(folio))
			goto next;

		total++;
		/*
		 * For folios with private, split_huge_page_to_list_to_order()
		 * will try to drop it before split and then check if the folio
		 * can be split or not. So skip the check here.
		 */
		if (!folio_test_private(folio) &&
		    !can_split_folio(folio, 0, NULL))
			goto next;

		if (!folio_trylock(folio))
			goto next;
		folio_get(folio);
		folio_walk_end(&fw, vma);

		if (!folio_test_anon(folio) && folio->mapping != mapping)
			goto unlock;

		if (!split_folio_to_order(folio, target_order))
			split++;

unlock:

		folio_unlock(folio);
		folio_put(folio);

		cond_resched();
		continue;
next:
		folio_walk_end(&fw, vma);
		cond_resched();
	}
	mmap_read_unlock(mm);
	mmput(mm);

	pr_debug("%lu of %lu THP split\n", split, total);

out:
	return ret;
}

static int split_huge_pages_in_file(const char *file_path, pgoff_t off_start,
				pgoff_t off_end, unsigned int new_order)
{
	struct filename *file;
	struct file *candidate;
	struct address_space *mapping;
	int ret = -EINVAL;
	pgoff_t index;
	int nr_pages = 1;
	unsigned long total = 0, split = 0;
	unsigned int min_order;
	unsigned int target_order;

	file = getname_kernel(file_path);
	if (IS_ERR(file))
		return ret;

	candidate = file_open_name(file, O_RDONLY, 0);
	if (IS_ERR(candidate))
		goto out;

	pr_debug("split file-backed THPs in file: %s, page offset: [0x%lx - 0x%lx]\n",
		 file_path, off_start, off_end);

	mapping = candidate->f_mapping;
	min_order = mapping_min_folio_order(mapping);
	target_order = max(new_order, min_order);

	for (index = off_start; index < off_end; index += nr_pages) {
		struct folio *folio = filemap_get_folio(mapping, index);

		nr_pages = 1;
		if (IS_ERR(folio))
			continue;

		if (!folio_test_large(folio))
			goto next;

		total++;
		nr_pages = folio_nr_pages(folio);

		if (target_order >= folio_order(folio))
			goto next;

		if (!folio_trylock(folio))
			goto next;

		if (folio->mapping != mapping)
			goto unlock;

		if (!split_folio_to_order(folio, target_order))
			split++;

unlock:
		folio_unlock(folio);
next:
		folio_put(folio);
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
	/*
	 * hold pid, start_vaddr, end_vaddr, new_order or
	 * file_path, off_start, off_end, new_order
	 */
	char input_buf[MAX_INPUT_BUF_SZ];
	int pid;
	unsigned long vaddr_start, vaddr_end;
	unsigned int new_order = 0;

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

		ret = sscanf(buf, "0x%lx,0x%lx,%d", &off_start, &off_end, &new_order);
		if (ret != 2 && ret != 3) {
			ret = -EINVAL;
			goto out;
		}
		ret = split_huge_pages_in_file(file_path, off_start, off_end, new_order);
		if (!ret)
			ret = input_len;

		goto out;
	}

	ret = sscanf(input_buf, "%d,0x%lx,0x%lx,%d", &pid, &vaddr_start, &vaddr_end, &new_order);
	if (ret == 1 && pid == 1) {
		split_huge_pages_all();
		ret = strlen(input_buf);
		goto out;
	} else if (ret != 3 && ret != 4) {
		ret = -EINVAL;
		goto out;
	}

	ret = split_huge_pages_pid(pid, vaddr_start, vaddr_end, new_order);
	if (!ret)
		ret = strlen(input_buf);
out:
	mutex_unlock(&split_debug_mutex);
	return ret;

}

static const struct file_operations split_huge_pages_fops = {
	.owner	 = THIS_MODULE,
	.write	 = split_huge_pages_write,
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
	struct folio *folio = page_folio(page);
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

	/* See folio_try_share_anon_rmap_pmd(): invalidate PMD first. */
	anon_exclusive = folio_test_anon(folio) && PageAnonExclusive(page);
	if (anon_exclusive && folio_try_share_anon_rmap_pmd(folio, page)) {
		set_pmd_at(mm, address, pvmw->pmd, pmdval);
		return -EBUSY;
	}

	if (pmd_dirty(pmdval))
		folio_mark_dirty(folio);
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
	folio_remove_rmap_pmd(folio, page, vma);
	folio_put(folio);
	trace_set_migration_pmd(address, pmd_val(pmdswp));

	return 0;
}

void remove_migration_pmd(struct page_vma_mapped_walk *pvmw, struct page *new)
{
	struct folio *folio = page_folio(new);
	struct vm_area_struct *vma = pvmw->vma;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address = pvmw->address;
	unsigned long haddr = address & HPAGE_PMD_MASK;
	pmd_t pmde;
	swp_entry_t entry;

	if (!(pvmw->pmd && !pvmw->pte))
		return;

	entry = pmd_to_swp_entry(*pvmw->pmd);
	folio_get(folio);
	pmde = mk_huge_pmd(new, READ_ONCE(vma->vm_page_prot));
	if (pmd_swp_soft_dirty(*pvmw->pmd))
		pmde = pmd_mksoft_dirty(pmde);
	if (is_writable_migration_entry(entry))
		pmde = pmd_mkwrite(pmde, vma);
	if (pmd_swp_uffd_wp(*pvmw->pmd))
		pmde = pmd_mkuffd_wp(pmde);
	if (!is_migration_entry_young(entry))
		pmde = pmd_mkold(pmde);
	/* NOTE: this may contain setting soft-dirty on some archs */
	if (folio_test_dirty(folio) && is_migration_entry_dirty(entry))
		pmde = pmd_mkdirty(pmde);

	if (folio_test_anon(folio)) {
		rmap_t rmap_flags = RMAP_NONE;

		if (!is_readable_migration_entry(entry))
			rmap_flags |= RMAP_EXCLUSIVE;

		folio_add_anon_rmap_pmd(folio, new, vma, haddr, rmap_flags);
	} else {
		folio_add_file_rmap_pmd(folio, new, vma);
	}
	VM_BUG_ON(pmd_write(pmde) && folio_test_anon(folio) && !PageAnonExclusive(new));
	set_pmd_at(mm, haddr, pvmw->pmd, pmde);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache_pmd(vma, address, pvmw->pmd);
	trace_remove_migration_pmd(address, pmd_val(pmde));
}
#endif
