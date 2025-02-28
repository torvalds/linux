// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic hugetlb support.
 * (C) Nadia Yvette Chambers, April 2004
 */
#include <linux/list.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/sysctl.h>
#include <linux/highmem.h>
#include <linux/mmu_notifier.h>
#include <linux/nodemask.h>
#include <linux/pagemap.h>
#include <linux/mempolicy.h>
#include <linux/compiler.h>
#include <linux/cpuset.h>
#include <linux/mutex.h>
#include <linux/memblock.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/mmdebug.h>
#include <linux/sched/signal.h>
#include <linux/rmap.h>
#include <linux/string_helpers.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/jhash.h>
#include <linux/numa.h>
#include <linux/llist.h>
#include <linux/cma.h>
#include <linux/migrate.h>
#include <linux/nospec.h>
#include <linux/delayacct.h>
#include <linux/memory.h>
#include <linux/mm_inline.h>
#include <linux/padata.h>

#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/setup.h>

#include <linux/io.h>
#include <linux/hugetlb.h>
#include <linux/hugetlb_cgroup.h>
#include <linux/node.h>
#include <linux/page_owner.h>
#include "internal.h"
#include "hugetlb_vmemmap.h"
#include <linux/page-isolation.h>

int hugetlb_max_hstate __read_mostly;
unsigned int default_hstate_idx;
struct hstate hstates[HUGE_MAX_HSTATE];

#ifdef CONFIG_CMA
static struct cma *hugetlb_cma[MAX_NUMNODES];
static unsigned long hugetlb_cma_size_in_node[MAX_NUMNODES] __initdata;
#endif
static bool hugetlb_cma_only;
static unsigned long hugetlb_cma_size __initdata;

__initdata struct list_head huge_boot_pages[MAX_NUMNODES];
static unsigned long hstate_boot_nrinvalid[HUGE_MAX_HSTATE] __initdata;

/*
 * Due to ordering constraints across the init code for various
 * architectures, hugetlb hstate cmdline parameters can't simply
 * be early_param. early_param might call the setup function
 * before valid hugetlb page sizes are determined, leading to
 * incorrect rejection of valid hugepagesz= options.
 *
 * So, record the parameters early and consume them whenever the
 * init code is ready for them, by calling hugetlb_parse_params().
 */

/* one (hugepagesz=,hugepages=) pair per hstate, one default_hugepagesz */
#define HUGE_MAX_CMDLINE_ARGS	(2 * HUGE_MAX_HSTATE + 1)
struct hugetlb_cmdline {
	char *val;
	int (*setup)(char *val);
};

/* for command line parsing */
static struct hstate * __initdata parsed_hstate;
static unsigned long __initdata default_hstate_max_huge_pages;
static bool __initdata parsed_valid_hugepagesz = true;
static bool __initdata parsed_default_hugepagesz;
static unsigned int default_hugepages_in_node[MAX_NUMNODES] __initdata;

static char hstate_cmdline_buf[COMMAND_LINE_SIZE] __initdata;
static int hstate_cmdline_index __initdata;
static struct hugetlb_cmdline hugetlb_params[HUGE_MAX_CMDLINE_ARGS] __initdata;
static int hugetlb_param_index __initdata;
static __init int hugetlb_add_param(char *s, int (*setup)(char *val));
static __init void hugetlb_parse_params(void);

#define hugetlb_early_param(str, func) \
static __init int func##args(char *s) \
{ \
	return hugetlb_add_param(s, func); \
} \
early_param(str, func##args)

/*
 * Protects updates to hugepage_freelists, hugepage_activelist, nr_huge_pages,
 * free_huge_pages, and surplus_huge_pages.
 */
__cacheline_aligned_in_smp DEFINE_SPINLOCK(hugetlb_lock);

/*
 * Serializes faults on the same logical page.  This is used to
 * prevent spurious OOMs when the hugepage pool is fully utilized.
 */
static int num_fault_mutexes __ro_after_init;
struct mutex *hugetlb_fault_mutex_table __ro_after_init;

/* Forward declaration */
static int hugetlb_acct_memory(struct hstate *h, long delta);
static void hugetlb_vma_lock_free(struct vm_area_struct *vma);
static void hugetlb_vma_lock_alloc(struct vm_area_struct *vma);
static void __hugetlb_vma_unlock_write_free(struct vm_area_struct *vma);
static void hugetlb_unshare_pmds(struct vm_area_struct *vma,
		unsigned long start, unsigned long end);
static struct resv_map *vma_resv_map(struct vm_area_struct *vma);

static void hugetlb_free_folio(struct folio *folio)
{
#ifdef CONFIG_CMA
	int nid = folio_nid(folio);

	if (cma_free_folio(hugetlb_cma[nid], folio))
		return;
#endif
	folio_put(folio);
}

static inline bool subpool_is_free(struct hugepage_subpool *spool)
{
	if (spool->count)
		return false;
	if (spool->max_hpages != -1)
		return spool->used_hpages == 0;
	if (spool->min_hpages != -1)
		return spool->rsv_hpages == spool->min_hpages;

	return true;
}

static inline void unlock_or_release_subpool(struct hugepage_subpool *spool,
						unsigned long irq_flags)
{
	spin_unlock_irqrestore(&spool->lock, irq_flags);

	/* If no pages are used, and no other handles to the subpool
	 * remain, give up any reservations based on minimum size and
	 * free the subpool */
	if (subpool_is_free(spool)) {
		if (spool->min_hpages != -1)
			hugetlb_acct_memory(spool->hstate,
						-spool->min_hpages);
		kfree(spool);
	}
}

struct hugepage_subpool *hugepage_new_subpool(struct hstate *h, long max_hpages,
						long min_hpages)
{
	struct hugepage_subpool *spool;

	spool = kzalloc(sizeof(*spool), GFP_KERNEL);
	if (!spool)
		return NULL;

	spin_lock_init(&spool->lock);
	spool->count = 1;
	spool->max_hpages = max_hpages;
	spool->hstate = h;
	spool->min_hpages = min_hpages;

	if (min_hpages != -1 && hugetlb_acct_memory(h, min_hpages)) {
		kfree(spool);
		return NULL;
	}
	spool->rsv_hpages = min_hpages;

	return spool;
}

void hugepage_put_subpool(struct hugepage_subpool *spool)
{
	unsigned long flags;

	spin_lock_irqsave(&spool->lock, flags);
	BUG_ON(!spool->count);
	spool->count--;
	unlock_or_release_subpool(spool, flags);
}

/*
 * Subpool accounting for allocating and reserving pages.
 * Return -ENOMEM if there are not enough resources to satisfy the
 * request.  Otherwise, return the number of pages by which the
 * global pools must be adjusted (upward).  The returned value may
 * only be different than the passed value (delta) in the case where
 * a subpool minimum size must be maintained.
 */
static long hugepage_subpool_get_pages(struct hugepage_subpool *spool,
				      long delta)
{
	long ret = delta;

	if (!spool)
		return ret;

	spin_lock_irq(&spool->lock);

	if (spool->max_hpages != -1) {		/* maximum size accounting */
		if ((spool->used_hpages + delta) <= spool->max_hpages)
			spool->used_hpages += delta;
		else {
			ret = -ENOMEM;
			goto unlock_ret;
		}
	}

	/* minimum size accounting */
	if (spool->min_hpages != -1 && spool->rsv_hpages) {
		if (delta > spool->rsv_hpages) {
			/*
			 * Asking for more reserves than those already taken on
			 * behalf of subpool.  Return difference.
			 */
			ret = delta - spool->rsv_hpages;
			spool->rsv_hpages = 0;
		} else {
			ret = 0;	/* reserves already accounted for */
			spool->rsv_hpages -= delta;
		}
	}

unlock_ret:
	spin_unlock_irq(&spool->lock);
	return ret;
}

/*
 * Subpool accounting for freeing and unreserving pages.
 * Return the number of global page reservations that must be dropped.
 * The return value may only be different than the passed value (delta)
 * in the case where a subpool minimum size must be maintained.
 */
static long hugepage_subpool_put_pages(struct hugepage_subpool *spool,
				       long delta)
{
	long ret = delta;
	unsigned long flags;

	if (!spool)
		return delta;

	spin_lock_irqsave(&spool->lock, flags);

	if (spool->max_hpages != -1)		/* maximum size accounting */
		spool->used_hpages -= delta;

	 /* minimum size accounting */
	if (spool->min_hpages != -1 && spool->used_hpages < spool->min_hpages) {
		if (spool->rsv_hpages + delta <= spool->min_hpages)
			ret = 0;
		else
			ret = spool->rsv_hpages + delta - spool->min_hpages;

		spool->rsv_hpages += delta;
		if (spool->rsv_hpages > spool->min_hpages)
			spool->rsv_hpages = spool->min_hpages;
	}

	/*
	 * If hugetlbfs_put_super couldn't free spool due to an outstanding
	 * quota reference, free it now.
	 */
	unlock_or_release_subpool(spool, flags);

	return ret;
}

static inline struct hugepage_subpool *subpool_inode(struct inode *inode)
{
	return HUGETLBFS_SB(inode->i_sb)->spool;
}

static inline struct hugepage_subpool *subpool_vma(struct vm_area_struct *vma)
{
	return subpool_inode(file_inode(vma->vm_file));
}

/*
 * hugetlb vma_lock helper routines
 */
void hugetlb_vma_lock_read(struct vm_area_struct *vma)
{
	if (__vma_shareable_lock(vma)) {
		struct hugetlb_vma_lock *vma_lock = vma->vm_private_data;

		down_read(&vma_lock->rw_sema);
	} else if (__vma_private_lock(vma)) {
		struct resv_map *resv_map = vma_resv_map(vma);

		down_read(&resv_map->rw_sema);
	}
}

void hugetlb_vma_unlock_read(struct vm_area_struct *vma)
{
	if (__vma_shareable_lock(vma)) {
		struct hugetlb_vma_lock *vma_lock = vma->vm_private_data;

		up_read(&vma_lock->rw_sema);
	} else if (__vma_private_lock(vma)) {
		struct resv_map *resv_map = vma_resv_map(vma);

		up_read(&resv_map->rw_sema);
	}
}

void hugetlb_vma_lock_write(struct vm_area_struct *vma)
{
	if (__vma_shareable_lock(vma)) {
		struct hugetlb_vma_lock *vma_lock = vma->vm_private_data;

		down_write(&vma_lock->rw_sema);
	} else if (__vma_private_lock(vma)) {
		struct resv_map *resv_map = vma_resv_map(vma);

		down_write(&resv_map->rw_sema);
	}
}

void hugetlb_vma_unlock_write(struct vm_area_struct *vma)
{
	if (__vma_shareable_lock(vma)) {
		struct hugetlb_vma_lock *vma_lock = vma->vm_private_data;

		up_write(&vma_lock->rw_sema);
	} else if (__vma_private_lock(vma)) {
		struct resv_map *resv_map = vma_resv_map(vma);

		up_write(&resv_map->rw_sema);
	}
}

int hugetlb_vma_trylock_write(struct vm_area_struct *vma)
{

	if (__vma_shareable_lock(vma)) {
		struct hugetlb_vma_lock *vma_lock = vma->vm_private_data;

		return down_write_trylock(&vma_lock->rw_sema);
	} else if (__vma_private_lock(vma)) {
		struct resv_map *resv_map = vma_resv_map(vma);

		return down_write_trylock(&resv_map->rw_sema);
	}

	return 1;
}

void hugetlb_vma_assert_locked(struct vm_area_struct *vma)
{
	if (__vma_shareable_lock(vma)) {
		struct hugetlb_vma_lock *vma_lock = vma->vm_private_data;

		lockdep_assert_held(&vma_lock->rw_sema);
	} else if (__vma_private_lock(vma)) {
		struct resv_map *resv_map = vma_resv_map(vma);

		lockdep_assert_held(&resv_map->rw_sema);
	}
}

void hugetlb_vma_lock_release(struct kref *kref)
{
	struct hugetlb_vma_lock *vma_lock = container_of(kref,
			struct hugetlb_vma_lock, refs);

	kfree(vma_lock);
}

static void __hugetlb_vma_unlock_write_put(struct hugetlb_vma_lock *vma_lock)
{
	struct vm_area_struct *vma = vma_lock->vma;

	/*
	 * vma_lock structure may or not be released as a result of put,
	 * it certainly will no longer be attached to vma so clear pointer.
	 * Semaphore synchronizes access to vma_lock->vma field.
	 */
	vma_lock->vma = NULL;
	vma->vm_private_data = NULL;
	up_write(&vma_lock->rw_sema);
	kref_put(&vma_lock->refs, hugetlb_vma_lock_release);
}

static void __hugetlb_vma_unlock_write_free(struct vm_area_struct *vma)
{
	if (__vma_shareable_lock(vma)) {
		struct hugetlb_vma_lock *vma_lock = vma->vm_private_data;

		__hugetlb_vma_unlock_write_put(vma_lock);
	} else if (__vma_private_lock(vma)) {
		struct resv_map *resv_map = vma_resv_map(vma);

		/* no free for anon vmas, but still need to unlock */
		up_write(&resv_map->rw_sema);
	}
}

static void hugetlb_vma_lock_free(struct vm_area_struct *vma)
{
	/*
	 * Only present in sharable vmas.
	 */
	if (!vma || !__vma_shareable_lock(vma))
		return;

	if (vma->vm_private_data) {
		struct hugetlb_vma_lock *vma_lock = vma->vm_private_data;

		down_write(&vma_lock->rw_sema);
		__hugetlb_vma_unlock_write_put(vma_lock);
	}
}

static void hugetlb_vma_lock_alloc(struct vm_area_struct *vma)
{
	struct hugetlb_vma_lock *vma_lock;

	/* Only establish in (flags) sharable vmas */
	if (!vma || !(vma->vm_flags & VM_MAYSHARE))
		return;

	/* Should never get here with non-NULL vm_private_data */
	if (vma->vm_private_data)
		return;

	vma_lock = kmalloc(sizeof(*vma_lock), GFP_KERNEL);
	if (!vma_lock) {
		/*
		 * If we can not allocate structure, then vma can not
		 * participate in pmd sharing.  This is only a possible
		 * performance enhancement and memory saving issue.
		 * However, the lock is also used to synchronize page
		 * faults with truncation.  If the lock is not present,
		 * unlikely races could leave pages in a file past i_size
		 * until the file is removed.  Warn in the unlikely case of
		 * allocation failure.
		 */
		pr_warn_once("HugeTLB: unable to allocate vma specific lock\n");
		return;
	}

	kref_init(&vma_lock->refs);
	init_rwsem(&vma_lock->rw_sema);
	vma_lock->vma = vma;
	vma->vm_private_data = vma_lock;
}

/* Helper that removes a struct file_region from the resv_map cache and returns
 * it for use.
 */
static struct file_region *
get_file_region_entry_from_cache(struct resv_map *resv, long from, long to)
{
	struct file_region *nrg;

	VM_BUG_ON(resv->region_cache_count <= 0);

	resv->region_cache_count--;
	nrg = list_first_entry(&resv->region_cache, struct file_region, link);
	list_del(&nrg->link);

	nrg->from = from;
	nrg->to = to;

	return nrg;
}

static void copy_hugetlb_cgroup_uncharge_info(struct file_region *nrg,
					      struct file_region *rg)
{
#ifdef CONFIG_CGROUP_HUGETLB
	nrg->reservation_counter = rg->reservation_counter;
	nrg->css = rg->css;
	if (rg->css)
		css_get(rg->css);
#endif
}

/* Helper that records hugetlb_cgroup uncharge info. */
static void record_hugetlb_cgroup_uncharge_info(struct hugetlb_cgroup *h_cg,
						struct hstate *h,
						struct resv_map *resv,
						struct file_region *nrg)
{
#ifdef CONFIG_CGROUP_HUGETLB
	if (h_cg) {
		nrg->reservation_counter =
			&h_cg->rsvd_hugepage[hstate_index(h)];
		nrg->css = &h_cg->css;
		/*
		 * The caller will hold exactly one h_cg->css reference for the
		 * whole contiguous reservation region. But this area might be
		 * scattered when there are already some file_regions reside in
		 * it. As a result, many file_regions may share only one css
		 * reference. In order to ensure that one file_region must hold
		 * exactly one h_cg->css reference, we should do css_get for
		 * each file_region and leave the reference held by caller
		 * untouched.
		 */
		css_get(&h_cg->css);
		if (!resv->pages_per_hpage)
			resv->pages_per_hpage = pages_per_huge_page(h);
		/* pages_per_hpage should be the same for all entries in
		 * a resv_map.
		 */
		VM_BUG_ON(resv->pages_per_hpage != pages_per_huge_page(h));
	} else {
		nrg->reservation_counter = NULL;
		nrg->css = NULL;
	}
#endif
}

static void put_uncharge_info(struct file_region *rg)
{
#ifdef CONFIG_CGROUP_HUGETLB
	if (rg->css)
		css_put(rg->css);
#endif
}

static bool has_same_uncharge_info(struct file_region *rg,
				   struct file_region *org)
{
#ifdef CONFIG_CGROUP_HUGETLB
	return rg->reservation_counter == org->reservation_counter &&
	       rg->css == org->css;

#else
	return true;
#endif
}

static void coalesce_file_region(struct resv_map *resv, struct file_region *rg)
{
	struct file_region *nrg, *prg;

	prg = list_prev_entry(rg, link);
	if (&prg->link != &resv->regions && prg->to == rg->from &&
	    has_same_uncharge_info(prg, rg)) {
		prg->to = rg->to;

		list_del(&rg->link);
		put_uncharge_info(rg);
		kfree(rg);

		rg = prg;
	}

	nrg = list_next_entry(rg, link);
	if (&nrg->link != &resv->regions && nrg->from == rg->to &&
	    has_same_uncharge_info(nrg, rg)) {
		nrg->from = rg->from;

		list_del(&rg->link);
		put_uncharge_info(rg);
		kfree(rg);
	}
}

static inline long
hugetlb_resv_map_add(struct resv_map *map, struct list_head *rg, long from,
		     long to, struct hstate *h, struct hugetlb_cgroup *cg,
		     long *regions_needed)
{
	struct file_region *nrg;

	if (!regions_needed) {
		nrg = get_file_region_entry_from_cache(map, from, to);
		record_hugetlb_cgroup_uncharge_info(cg, h, map, nrg);
		list_add(&nrg->link, rg);
		coalesce_file_region(map, nrg);
	} else
		*regions_needed += 1;

	return to - from;
}

/*
 * Must be called with resv->lock held.
 *
 * Calling this with regions_needed != NULL will count the number of pages
 * to be added but will not modify the linked list. And regions_needed will
 * indicate the number of file_regions needed in the cache to carry out to add
 * the regions for this range.
 */
static long add_reservation_in_range(struct resv_map *resv, long f, long t,
				     struct hugetlb_cgroup *h_cg,
				     struct hstate *h, long *regions_needed)
{
	long add = 0;
	struct list_head *head = &resv->regions;
	long last_accounted_offset = f;
	struct file_region *iter, *trg = NULL;
	struct list_head *rg = NULL;

	if (regions_needed)
		*regions_needed = 0;

	/* In this loop, we essentially handle an entry for the range
	 * [last_accounted_offset, iter->from), at every iteration, with some
	 * bounds checking.
	 */
	list_for_each_entry_safe(iter, trg, head, link) {
		/* Skip irrelevant regions that start before our range. */
		if (iter->from < f) {
			/* If this region ends after the last accounted offset,
			 * then we need to update last_accounted_offset.
			 */
			if (iter->to > last_accounted_offset)
				last_accounted_offset = iter->to;
			continue;
		}

		/* When we find a region that starts beyond our range, we've
		 * finished.
		 */
		if (iter->from >= t) {
			rg = iter->link.prev;
			break;
		}

		/* Add an entry for last_accounted_offset -> iter->from, and
		 * update last_accounted_offset.
		 */
		if (iter->from > last_accounted_offset)
			add += hugetlb_resv_map_add(resv, iter->link.prev,
						    last_accounted_offset,
						    iter->from, h, h_cg,
						    regions_needed);

		last_accounted_offset = iter->to;
	}

	/* Handle the case where our range extends beyond
	 * last_accounted_offset.
	 */
	if (!rg)
		rg = head->prev;
	if (last_accounted_offset < t)
		add += hugetlb_resv_map_add(resv, rg, last_accounted_offset,
					    t, h, h_cg, regions_needed);

	return add;
}

/* Must be called with resv->lock acquired. Will drop lock to allocate entries.
 */
static int allocate_file_region_entries(struct resv_map *resv,
					int regions_needed)
	__must_hold(&resv->lock)
{
	LIST_HEAD(allocated_regions);
	int to_allocate = 0, i = 0;
	struct file_region *trg = NULL, *rg = NULL;

	VM_BUG_ON(regions_needed < 0);

	/*
	 * Check for sufficient descriptors in the cache to accommodate
	 * the number of in progress add operations plus regions_needed.
	 *
	 * This is a while loop because when we drop the lock, some other call
	 * to region_add or region_del may have consumed some region_entries,
	 * so we keep looping here until we finally have enough entries for
	 * (adds_in_progress + regions_needed).
	 */
	while (resv->region_cache_count <
	       (resv->adds_in_progress + regions_needed)) {
		to_allocate = resv->adds_in_progress + regions_needed -
			      resv->region_cache_count;

		/* At this point, we should have enough entries in the cache
		 * for all the existing adds_in_progress. We should only be
		 * needing to allocate for regions_needed.
		 */
		VM_BUG_ON(resv->region_cache_count < resv->adds_in_progress);

		spin_unlock(&resv->lock);
		for (i = 0; i < to_allocate; i++) {
			trg = kmalloc(sizeof(*trg), GFP_KERNEL);
			if (!trg)
				goto out_of_memory;
			list_add(&trg->link, &allocated_regions);
		}

		spin_lock(&resv->lock);

		list_splice(&allocated_regions, &resv->region_cache);
		resv->region_cache_count += to_allocate;
	}

	return 0;

out_of_memory:
	list_for_each_entry_safe(rg, trg, &allocated_regions, link) {
		list_del(&rg->link);
		kfree(rg);
	}
	return -ENOMEM;
}

/*
 * Add the huge page range represented by [f, t) to the reserve
 * map.  Regions will be taken from the cache to fill in this range.
 * Sufficient regions should exist in the cache due to the previous
 * call to region_chg with the same range, but in some cases the cache will not
 * have sufficient entries due to races with other code doing region_add or
 * region_del.  The extra needed entries will be allocated.
 *
 * regions_needed is the out value provided by a previous call to region_chg.
 *
 * Return the number of new huge pages added to the map.  This number is greater
 * than or equal to zero.  If file_region entries needed to be allocated for
 * this operation and we were not able to allocate, it returns -ENOMEM.
 * region_add of regions of length 1 never allocate file_regions and cannot
 * fail; region_chg will always allocate at least 1 entry and a region_add for
 * 1 page will only require at most 1 entry.
 */
static long region_add(struct resv_map *resv, long f, long t,
		       long in_regions_needed, struct hstate *h,
		       struct hugetlb_cgroup *h_cg)
{
	long add = 0, actual_regions_needed = 0;

	spin_lock(&resv->lock);
retry:

	/* Count how many regions are actually needed to execute this add. */
	add_reservation_in_range(resv, f, t, NULL, NULL,
				 &actual_regions_needed);

	/*
	 * Check for sufficient descriptors in the cache to accommodate
	 * this add operation. Note that actual_regions_needed may be greater
	 * than in_regions_needed, as the resv_map may have been modified since
	 * the region_chg call. In this case, we need to make sure that we
	 * allocate extra entries, such that we have enough for all the
	 * existing adds_in_progress, plus the excess needed for this
	 * operation.
	 */
	if (actual_regions_needed > in_regions_needed &&
	    resv->region_cache_count <
		    resv->adds_in_progress +
			    (actual_regions_needed - in_regions_needed)) {
		/* region_add operation of range 1 should never need to
		 * allocate file_region entries.
		 */
		VM_BUG_ON(t - f <= 1);

		if (allocate_file_region_entries(
			    resv, actual_regions_needed - in_regions_needed)) {
			return -ENOMEM;
		}

		goto retry;
	}

	add = add_reservation_in_range(resv, f, t, h_cg, h, NULL);

	resv->adds_in_progress -= in_regions_needed;

	spin_unlock(&resv->lock);
	return add;
}

/*
 * Examine the existing reserve map and determine how many
 * huge pages in the specified range [f, t) are NOT currently
 * represented.  This routine is called before a subsequent
 * call to region_add that will actually modify the reserve
 * map to add the specified range [f, t).  region_chg does
 * not change the number of huge pages represented by the
 * map.  A number of new file_region structures is added to the cache as a
 * placeholder, for the subsequent region_add call to use. At least 1
 * file_region structure is added.
 *
 * out_regions_needed is the number of regions added to the
 * resv->adds_in_progress.  This value needs to be provided to a follow up call
 * to region_add or region_abort for proper accounting.
 *
 * Returns the number of huge pages that need to be added to the existing
 * reservation map for the range [f, t).  This number is greater or equal to
 * zero.  -ENOMEM is returned if a new file_region structure or cache entry
 * is needed and can not be allocated.
 */
static long region_chg(struct resv_map *resv, long f, long t,
		       long *out_regions_needed)
{
	long chg = 0;

	spin_lock(&resv->lock);

	/* Count how many hugepages in this range are NOT represented. */
	chg = add_reservation_in_range(resv, f, t, NULL, NULL,
				       out_regions_needed);

	if (*out_regions_needed == 0)
		*out_regions_needed = 1;

	if (allocate_file_region_entries(resv, *out_regions_needed))
		return -ENOMEM;

	resv->adds_in_progress += *out_regions_needed;

	spin_unlock(&resv->lock);
	return chg;
}

/*
 * Abort the in progress add operation.  The adds_in_progress field
 * of the resv_map keeps track of the operations in progress between
 * calls to region_chg and region_add.  Operations are sometimes
 * aborted after the call to region_chg.  In such cases, region_abort
 * is called to decrement the adds_in_progress counter. regions_needed
 * is the value returned by the region_chg call, it is used to decrement
 * the adds_in_progress counter.
 *
 * NOTE: The range arguments [f, t) are not needed or used in this
 * routine.  They are kept to make reading the calling code easier as
 * arguments will match the associated region_chg call.
 */
static void region_abort(struct resv_map *resv, long f, long t,
			 long regions_needed)
{
	spin_lock(&resv->lock);
	VM_BUG_ON(!resv->region_cache_count);
	resv->adds_in_progress -= regions_needed;
	spin_unlock(&resv->lock);
}

/*
 * Delete the specified range [f, t) from the reserve map.  If the
 * t parameter is LONG_MAX, this indicates that ALL regions after f
 * should be deleted.  Locate the regions which intersect [f, t)
 * and either trim, delete or split the existing regions.
 *
 * Returns the number of huge pages deleted from the reserve map.
 * In the normal case, the return value is zero or more.  In the
 * case where a region must be split, a new region descriptor must
 * be allocated.  If the allocation fails, -ENOMEM will be returned.
 * NOTE: If the parameter t == LONG_MAX, then we will never split
 * a region and possibly return -ENOMEM.  Callers specifying
 * t == LONG_MAX do not need to check for -ENOMEM error.
 */
static long region_del(struct resv_map *resv, long f, long t)
{
	struct list_head *head = &resv->regions;
	struct file_region *rg, *trg;
	struct file_region *nrg = NULL;
	long del = 0;

retry:
	spin_lock(&resv->lock);
	list_for_each_entry_safe(rg, trg, head, link) {
		/*
		 * Skip regions before the range to be deleted.  file_region
		 * ranges are normally of the form [from, to).  However, there
		 * may be a "placeholder" entry in the map which is of the form
		 * (from, to) with from == to.  Check for placeholder entries
		 * at the beginning of the range to be deleted.
		 */
		if (rg->to <= f && (rg->to != rg->from || rg->to != f))
			continue;

		if (rg->from >= t)
			break;

		if (f > rg->from && t < rg->to) { /* Must split region */
			/*
			 * Check for an entry in the cache before dropping
			 * lock and attempting allocation.
			 */
			if (!nrg &&
			    resv->region_cache_count > resv->adds_in_progress) {
				nrg = list_first_entry(&resv->region_cache,
							struct file_region,
							link);
				list_del(&nrg->link);
				resv->region_cache_count--;
			}

			if (!nrg) {
				spin_unlock(&resv->lock);
				nrg = kmalloc(sizeof(*nrg), GFP_KERNEL);
				if (!nrg)
					return -ENOMEM;
				goto retry;
			}

			del += t - f;
			hugetlb_cgroup_uncharge_file_region(
				resv, rg, t - f, false);

			/* New entry for end of split region */
			nrg->from = t;
			nrg->to = rg->to;

			copy_hugetlb_cgroup_uncharge_info(nrg, rg);

			INIT_LIST_HEAD(&nrg->link);

			/* Original entry is trimmed */
			rg->to = f;

			list_add(&nrg->link, &rg->link);
			nrg = NULL;
			break;
		}

		if (f <= rg->from && t >= rg->to) { /* Remove entire region */
			del += rg->to - rg->from;
			hugetlb_cgroup_uncharge_file_region(resv, rg,
							    rg->to - rg->from, true);
			list_del(&rg->link);
			kfree(rg);
			continue;
		}

		if (f <= rg->from) {	/* Trim beginning of region */
			hugetlb_cgroup_uncharge_file_region(resv, rg,
							    t - rg->from, false);

			del += t - rg->from;
			rg->from = t;
		} else {		/* Trim end of region */
			hugetlb_cgroup_uncharge_file_region(resv, rg,
							    rg->to - f, false);

			del += rg->to - f;
			rg->to = f;
		}
	}

	spin_unlock(&resv->lock);
	kfree(nrg);
	return del;
}

/*
 * A rare out of memory error was encountered which prevented removal of
 * the reserve map region for a page.  The huge page itself was free'ed
 * and removed from the page cache.  This routine will adjust the subpool
 * usage count, and the global reserve count if needed.  By incrementing
 * these counts, the reserve map entry which could not be deleted will
 * appear as a "reserved" entry instead of simply dangling with incorrect
 * counts.
 */
void hugetlb_fix_reserve_counts(struct inode *inode)
{
	struct hugepage_subpool *spool = subpool_inode(inode);
	long rsv_adjust;
	bool reserved = false;

	rsv_adjust = hugepage_subpool_get_pages(spool, 1);
	if (rsv_adjust > 0) {
		struct hstate *h = hstate_inode(inode);

		if (!hugetlb_acct_memory(h, 1))
			reserved = true;
	} else if (!rsv_adjust) {
		reserved = true;
	}

	if (!reserved)
		pr_warn("hugetlb: Huge Page Reserved count may go negative.\n");
}

/*
 * Count and return the number of huge pages in the reserve map
 * that intersect with the range [f, t).
 */
static long region_count(struct resv_map *resv, long f, long t)
{
	struct list_head *head = &resv->regions;
	struct file_region *rg;
	long chg = 0;

	spin_lock(&resv->lock);
	/* Locate each segment we overlap with, and count that overlap. */
	list_for_each_entry(rg, head, link) {
		long seg_from;
		long seg_to;

		if (rg->to <= f)
			continue;
		if (rg->from >= t)
			break;

		seg_from = max(rg->from, f);
		seg_to = min(rg->to, t);

		chg += seg_to - seg_from;
	}
	spin_unlock(&resv->lock);

	return chg;
}

/*
 * Convert the address within this vma to the page offset within
 * the mapping, huge page units here.
 */
static pgoff_t vma_hugecache_offset(struct hstate *h,
			struct vm_area_struct *vma, unsigned long address)
{
	return ((address - vma->vm_start) >> huge_page_shift(h)) +
			(vma->vm_pgoff >> huge_page_order(h));
}

/**
 * vma_kernel_pagesize - Page size granularity for this VMA.
 * @vma: The user mapping.
 *
 * Folios in this VMA will be aligned to, and at least the size of the
 * number of bytes returned by this function.
 *
 * Return: The default size of the folios allocated when backing a VMA.
 */
unsigned long vma_kernel_pagesize(struct vm_area_struct *vma)
{
	if (vma->vm_ops && vma->vm_ops->pagesize)
		return vma->vm_ops->pagesize(vma);
	return PAGE_SIZE;
}
EXPORT_SYMBOL_GPL(vma_kernel_pagesize);

/*
 * Return the page size being used by the MMU to back a VMA. In the majority
 * of cases, the page size used by the kernel matches the MMU size. On
 * architectures where it differs, an architecture-specific 'strong'
 * version of this symbol is required.
 */
__weak unsigned long vma_mmu_pagesize(struct vm_area_struct *vma)
{
	return vma_kernel_pagesize(vma);
}

/*
 * Flags for MAP_PRIVATE reservations.  These are stored in the bottom
 * bits of the reservation map pointer, which are always clear due to
 * alignment.
 */
#define HPAGE_RESV_OWNER    (1UL << 0)
#define HPAGE_RESV_UNMAPPED (1UL << 1)
#define HPAGE_RESV_MASK (HPAGE_RESV_OWNER | HPAGE_RESV_UNMAPPED)

/*
 * These helpers are used to track how many pages are reserved for
 * faults in a MAP_PRIVATE mapping. Only the process that called mmap()
 * is guaranteed to have their future faults succeed.
 *
 * With the exception of hugetlb_dup_vma_private() which is called at fork(),
 * the reserve counters are updated with the hugetlb_lock held. It is safe
 * to reset the VMA at fork() time as it is not in use yet and there is no
 * chance of the global counters getting corrupted as a result of the values.
 *
 * The private mapping reservation is represented in a subtly different
 * manner to a shared mapping.  A shared mapping has a region map associated
 * with the underlying file, this region map represents the backing file
 * pages which have ever had a reservation assigned which this persists even
 * after the page is instantiated.  A private mapping has a region map
 * associated with the original mmap which is attached to all VMAs which
 * reference it, this region map represents those offsets which have consumed
 * reservation ie. where pages have been instantiated.
 */
static unsigned long get_vma_private_data(struct vm_area_struct *vma)
{
	return (unsigned long)vma->vm_private_data;
}

static void set_vma_private_data(struct vm_area_struct *vma,
							unsigned long value)
{
	vma->vm_private_data = (void *)value;
}

static void
resv_map_set_hugetlb_cgroup_uncharge_info(struct resv_map *resv_map,
					  struct hugetlb_cgroup *h_cg,
					  struct hstate *h)
{
#ifdef CONFIG_CGROUP_HUGETLB
	if (!h_cg || !h) {
		resv_map->reservation_counter = NULL;
		resv_map->pages_per_hpage = 0;
		resv_map->css = NULL;
	} else {
		resv_map->reservation_counter =
			&h_cg->rsvd_hugepage[hstate_index(h)];
		resv_map->pages_per_hpage = pages_per_huge_page(h);
		resv_map->css = &h_cg->css;
	}
#endif
}

struct resv_map *resv_map_alloc(void)
{
	struct resv_map *resv_map = kmalloc(sizeof(*resv_map), GFP_KERNEL);
	struct file_region *rg = kmalloc(sizeof(*rg), GFP_KERNEL);

	if (!resv_map || !rg) {
		kfree(resv_map);
		kfree(rg);
		return NULL;
	}

	kref_init(&resv_map->refs);
	spin_lock_init(&resv_map->lock);
	INIT_LIST_HEAD(&resv_map->regions);
	init_rwsem(&resv_map->rw_sema);

	resv_map->adds_in_progress = 0;
	/*
	 * Initialize these to 0. On shared mappings, 0's here indicate these
	 * fields don't do cgroup accounting. On private mappings, these will be
	 * re-initialized to the proper values, to indicate that hugetlb cgroup
	 * reservations are to be un-charged from here.
	 */
	resv_map_set_hugetlb_cgroup_uncharge_info(resv_map, NULL, NULL);

	INIT_LIST_HEAD(&resv_map->region_cache);
	list_add(&rg->link, &resv_map->region_cache);
	resv_map->region_cache_count = 1;

	return resv_map;
}

void resv_map_release(struct kref *ref)
{
	struct resv_map *resv_map = container_of(ref, struct resv_map, refs);
	struct list_head *head = &resv_map->region_cache;
	struct file_region *rg, *trg;

	/* Clear out any active regions before we release the map. */
	region_del(resv_map, 0, LONG_MAX);

	/* ... and any entries left in the cache */
	list_for_each_entry_safe(rg, trg, head, link) {
		list_del(&rg->link);
		kfree(rg);
	}

	VM_BUG_ON(resv_map->adds_in_progress);

	kfree(resv_map);
}

static inline struct resv_map *inode_resv_map(struct inode *inode)
{
	/*
	 * At inode evict time, i_mapping may not point to the original
	 * address space within the inode.  This original address space
	 * contains the pointer to the resv_map.  So, always use the
	 * address space embedded within the inode.
	 * The VERY common case is inode->mapping == &inode->i_data but,
	 * this may not be true for device special inodes.
	 */
	return (struct resv_map *)(&inode->i_data)->i_private_data;
}

static struct resv_map *vma_resv_map(struct vm_area_struct *vma)
{
	VM_BUG_ON_VMA(!is_vm_hugetlb_page(vma), vma);
	if (vma->vm_flags & VM_MAYSHARE) {
		struct address_space *mapping = vma->vm_file->f_mapping;
		struct inode *inode = mapping->host;

		return inode_resv_map(inode);

	} else {
		return (struct resv_map *)(get_vma_private_data(vma) &
							~HPAGE_RESV_MASK);
	}
}

static void set_vma_resv_map(struct vm_area_struct *vma, struct resv_map *map)
{
	VM_BUG_ON_VMA(!is_vm_hugetlb_page(vma), vma);
	VM_BUG_ON_VMA(vma->vm_flags & VM_MAYSHARE, vma);

	set_vma_private_data(vma, (unsigned long)map);
}

static void set_vma_resv_flags(struct vm_area_struct *vma, unsigned long flags)
{
	VM_BUG_ON_VMA(!is_vm_hugetlb_page(vma), vma);
	VM_BUG_ON_VMA(vma->vm_flags & VM_MAYSHARE, vma);

	set_vma_private_data(vma, get_vma_private_data(vma) | flags);
}

static int is_vma_resv_set(struct vm_area_struct *vma, unsigned long flag)
{
	VM_BUG_ON_VMA(!is_vm_hugetlb_page(vma), vma);

	return (get_vma_private_data(vma) & flag) != 0;
}

bool __vma_private_lock(struct vm_area_struct *vma)
{
	return !(vma->vm_flags & VM_MAYSHARE) &&
		get_vma_private_data(vma) & ~HPAGE_RESV_MASK &&
		is_vma_resv_set(vma, HPAGE_RESV_OWNER);
}

void hugetlb_dup_vma_private(struct vm_area_struct *vma)
{
	VM_BUG_ON_VMA(!is_vm_hugetlb_page(vma), vma);
	/*
	 * Clear vm_private_data
	 * - For shared mappings this is a per-vma semaphore that may be
	 *   allocated in a subsequent call to hugetlb_vm_op_open.
	 *   Before clearing, make sure pointer is not associated with vma
	 *   as this will leak the structure.  This is the case when called
	 *   via clear_vma_resv_huge_pages() and hugetlb_vm_op_open has already
	 *   been called to allocate a new structure.
	 * - For MAP_PRIVATE mappings, this is the reserve map which does
	 *   not apply to children.  Faults generated by the children are
	 *   not guaranteed to succeed, even if read-only.
	 */
	if (vma->vm_flags & VM_MAYSHARE) {
		struct hugetlb_vma_lock *vma_lock = vma->vm_private_data;

		if (vma_lock && vma_lock->vma != vma)
			vma->vm_private_data = NULL;
	} else
		vma->vm_private_data = NULL;
}

/*
 * Reset and decrement one ref on hugepage private reservation.
 * Called with mm->mmap_lock writer semaphore held.
 * This function should be only used by move_vma() and operate on
 * same sized vma. It should never come here with last ref on the
 * reservation.
 */
void clear_vma_resv_huge_pages(struct vm_area_struct *vma)
{
	/*
	 * Clear the old hugetlb private page reservation.
	 * It has already been transferred to new_vma.
	 *
	 * During a mremap() operation of a hugetlb vma we call move_vma()
	 * which copies vma into new_vma and unmaps vma. After the copy
	 * operation both new_vma and vma share a reference to the resv_map
	 * struct, and at that point vma is about to be unmapped. We don't
	 * want to return the reservation to the pool at unmap of vma because
	 * the reservation still lives on in new_vma, so simply decrement the
	 * ref here and remove the resv_map reference from this vma.
	 */
	struct resv_map *reservations = vma_resv_map(vma);

	if (reservations && is_vma_resv_set(vma, HPAGE_RESV_OWNER)) {
		resv_map_put_hugetlb_cgroup_uncharge_info(reservations);
		kref_put(&reservations->refs, resv_map_release);
	}

	hugetlb_dup_vma_private(vma);
}

static void enqueue_hugetlb_folio(struct hstate *h, struct folio *folio)
{
	int nid = folio_nid(folio);

	lockdep_assert_held(&hugetlb_lock);
	VM_BUG_ON_FOLIO(folio_ref_count(folio), folio);

	list_move(&folio->lru, &h->hugepage_freelists[nid]);
	h->free_huge_pages++;
	h->free_huge_pages_node[nid]++;
	folio_set_hugetlb_freed(folio);
}

static struct folio *dequeue_hugetlb_folio_node_exact(struct hstate *h,
								int nid)
{
	struct folio *folio;
	bool pin = !!(current->flags & PF_MEMALLOC_PIN);

	lockdep_assert_held(&hugetlb_lock);
	list_for_each_entry(folio, &h->hugepage_freelists[nid], lru) {
		if (pin && !folio_is_longterm_pinnable(folio))
			continue;

		if (folio_test_hwpoison(folio))
			continue;

		if (is_migrate_isolate_page(&folio->page))
			continue;

		list_move(&folio->lru, &h->hugepage_activelist);
		folio_ref_unfreeze(folio, 1);
		folio_clear_hugetlb_freed(folio);
		h->free_huge_pages--;
		h->free_huge_pages_node[nid]--;
		return folio;
	}

	return NULL;
}

static struct folio *dequeue_hugetlb_folio_nodemask(struct hstate *h, gfp_t gfp_mask,
							int nid, nodemask_t *nmask)
{
	unsigned int cpuset_mems_cookie;
	struct zonelist *zonelist;
	struct zone *zone;
	struct zoneref *z;
	int node = NUMA_NO_NODE;

	/* 'nid' should not be NUMA_NO_NODE. Try to catch any misuse of it and rectifiy. */
	if (nid == NUMA_NO_NODE)
		nid = numa_node_id();

	zonelist = node_zonelist(nid, gfp_mask);

retry_cpuset:
	cpuset_mems_cookie = read_mems_allowed_begin();
	for_each_zone_zonelist_nodemask(zone, z, zonelist, gfp_zone(gfp_mask), nmask) {
		struct folio *folio;

		if (!cpuset_zone_allowed(zone, gfp_mask))
			continue;
		/*
		 * no need to ask again on the same node. Pool is node rather than
		 * zone aware
		 */
		if (zone_to_nid(zone) == node)
			continue;
		node = zone_to_nid(zone);

		folio = dequeue_hugetlb_folio_node_exact(h, node);
		if (folio)
			return folio;
	}
	if (unlikely(read_mems_allowed_retry(cpuset_mems_cookie)))
		goto retry_cpuset;

	return NULL;
}

static unsigned long available_huge_pages(struct hstate *h)
{
	return h->free_huge_pages - h->resv_huge_pages;
}

static struct folio *dequeue_hugetlb_folio_vma(struct hstate *h,
				struct vm_area_struct *vma,
				unsigned long address, long gbl_chg)
{
	struct folio *folio = NULL;
	struct mempolicy *mpol;
	gfp_t gfp_mask;
	nodemask_t *nodemask;
	int nid;

	/*
	 * gbl_chg==1 means the allocation requires a new page that was not
	 * reserved before.  Making sure there's at least one free page.
	 */
	if (gbl_chg && !available_huge_pages(h))
		goto err;

	gfp_mask = htlb_alloc_mask(h);
	nid = huge_node(vma, address, gfp_mask, &mpol, &nodemask);

	if (mpol_is_preferred_many(mpol)) {
		folio = dequeue_hugetlb_folio_nodemask(h, gfp_mask,
							nid, nodemask);

		/* Fallback to all nodes if page==NULL */
		nodemask = NULL;
	}

	if (!folio)
		folio = dequeue_hugetlb_folio_nodemask(h, gfp_mask,
							nid, nodemask);

	mpol_cond_put(mpol);
	return folio;

err:
	return NULL;
}

/*
 * common helper functions for hstate_next_node_to_{alloc|free}.
 * We may have allocated or freed a huge page based on a different
 * nodes_allowed previously, so h->next_node_to_{alloc|free} might
 * be outside of *nodes_allowed.  Ensure that we use an allowed
 * node for alloc or free.
 */
static int next_node_allowed(int nid, nodemask_t *nodes_allowed)
{
	nid = next_node_in(nid, *nodes_allowed);
	VM_BUG_ON(nid >= MAX_NUMNODES);

	return nid;
}

static int get_valid_node_allowed(int nid, nodemask_t *nodes_allowed)
{
	if (!node_isset(nid, *nodes_allowed))
		nid = next_node_allowed(nid, nodes_allowed);
	return nid;
}

/*
 * returns the previously saved node ["this node"] from which to
 * allocate a persistent huge page for the pool and advance the
 * next node from which to allocate, handling wrap at end of node
 * mask.
 */
static int hstate_next_node_to_alloc(int *next_node,
					nodemask_t *nodes_allowed)
{
	int nid;

	VM_BUG_ON(!nodes_allowed);

	nid = get_valid_node_allowed(*next_node, nodes_allowed);
	*next_node = next_node_allowed(nid, nodes_allowed);

	return nid;
}

/*
 * helper for remove_pool_hugetlb_folio() - return the previously saved
 * node ["this node"] from which to free a huge page.  Advance the
 * next node id whether or not we find a free huge page to free so
 * that the next attempt to free addresses the next node.
 */
static int hstate_next_node_to_free(struct hstate *h, nodemask_t *nodes_allowed)
{
	int nid;

	VM_BUG_ON(!nodes_allowed);

	nid = get_valid_node_allowed(h->next_nid_to_free, nodes_allowed);
	h->next_nid_to_free = next_node_allowed(nid, nodes_allowed);

	return nid;
}

#define for_each_node_mask_to_alloc(next_node, nr_nodes, node, mask)		\
	for (nr_nodes = nodes_weight(*mask);				\
		nr_nodes > 0 &&						\
		((node = hstate_next_node_to_alloc(next_node, mask)) || 1);	\
		nr_nodes--)

#define for_each_node_mask_to_free(hs, nr_nodes, node, mask)		\
	for (nr_nodes = nodes_weight(*mask);				\
		nr_nodes > 0 &&						\
		((node = hstate_next_node_to_free(hs, mask)) || 1);	\
		nr_nodes--)

#ifdef CONFIG_ARCH_HAS_GIGANTIC_PAGE
#ifdef CONFIG_CONTIG_ALLOC
static struct folio *alloc_gigantic_folio(struct hstate *h, gfp_t gfp_mask,
		int nid, nodemask_t *nodemask)
{
	struct folio *folio;
	int order = huge_page_order(h);
	bool retried = false;

	if (nid == NUMA_NO_NODE)
		nid = numa_mem_id();
retry:
	folio = NULL;
#ifdef CONFIG_CMA
	{
		int node;

		if (hugetlb_cma[nid])
			folio = cma_alloc_folio(hugetlb_cma[nid], order, gfp_mask);

		if (!folio && !(gfp_mask & __GFP_THISNODE)) {
			for_each_node_mask(node, *nodemask) {
				if (node == nid || !hugetlb_cma[node])
					continue;

				folio = cma_alloc_folio(hugetlb_cma[node], order, gfp_mask);
				if (folio)
					break;
			}
		}
	}
#endif
	if (!folio) {
		if (hugetlb_cma_only)
			return NULL;

		folio = folio_alloc_gigantic(order, gfp_mask, nid, nodemask);
		if (!folio)
			return NULL;
	}

	if (folio_ref_freeze(folio, 1))
		return folio;

	pr_warn("HugeTLB: unexpected refcount on PFN %lu\n", folio_pfn(folio));
	hugetlb_free_folio(folio);
	if (!retried) {
		retried = true;
		goto retry;
	}
	return NULL;
}

#else /* !CONFIG_CONTIG_ALLOC */
static struct folio *alloc_gigantic_folio(struct hstate *h, gfp_t gfp_mask,
					int nid, nodemask_t *nodemask)
{
	return NULL;
}
#endif /* CONFIG_CONTIG_ALLOC */

#else /* !CONFIG_ARCH_HAS_GIGANTIC_PAGE */
static struct folio *alloc_gigantic_folio(struct hstate *h, gfp_t gfp_mask,
					int nid, nodemask_t *nodemask)
{
	return NULL;
}
#endif

/*
 * Remove hugetlb folio from lists.
 * If vmemmap exists for the folio, clear the hugetlb flag so that the
 * folio appears as just a compound page.  Otherwise, wait until after
 * allocating vmemmap to clear the flag.
 *
 * Must be called with hugetlb lock held.
 */
static void remove_hugetlb_folio(struct hstate *h, struct folio *folio,
							bool adjust_surplus)
{
	int nid = folio_nid(folio);

	VM_BUG_ON_FOLIO(hugetlb_cgroup_from_folio(folio), folio);
	VM_BUG_ON_FOLIO(hugetlb_cgroup_from_folio_rsvd(folio), folio);

	lockdep_assert_held(&hugetlb_lock);
	if (hstate_is_gigantic(h) && !gigantic_page_runtime_supported())
		return;

	list_del(&folio->lru);

	if (folio_test_hugetlb_freed(folio)) {
		folio_clear_hugetlb_freed(folio);
		h->free_huge_pages--;
		h->free_huge_pages_node[nid]--;
	}
	if (adjust_surplus) {
		h->surplus_huge_pages--;
		h->surplus_huge_pages_node[nid]--;
	}

	/*
	 * We can only clear the hugetlb flag after allocating vmemmap
	 * pages.  Otherwise, someone (memory error handling) may try to write
	 * to tail struct pages.
	 */
	if (!folio_test_hugetlb_vmemmap_optimized(folio))
		__folio_clear_hugetlb(folio);

	h->nr_huge_pages--;
	h->nr_huge_pages_node[nid]--;
}

static void add_hugetlb_folio(struct hstate *h, struct folio *folio,
			     bool adjust_surplus)
{
	int nid = folio_nid(folio);

	VM_BUG_ON_FOLIO(!folio_test_hugetlb_vmemmap_optimized(folio), folio);

	lockdep_assert_held(&hugetlb_lock);

	INIT_LIST_HEAD(&folio->lru);
	h->nr_huge_pages++;
	h->nr_huge_pages_node[nid]++;

	if (adjust_surplus) {
		h->surplus_huge_pages++;
		h->surplus_huge_pages_node[nid]++;
	}

	__folio_set_hugetlb(folio);
	folio_change_private(folio, NULL);
	/*
	 * We have to set hugetlb_vmemmap_optimized again as above
	 * folio_change_private(folio, NULL) cleared it.
	 */
	folio_set_hugetlb_vmemmap_optimized(folio);

	arch_clear_hugetlb_flags(folio);
	enqueue_hugetlb_folio(h, folio);
}

static void __update_and_free_hugetlb_folio(struct hstate *h,
						struct folio *folio)
{
	bool clear_flag = folio_test_hugetlb_vmemmap_optimized(folio);

	if (hstate_is_gigantic(h) && !gigantic_page_runtime_supported())
		return;

	/*
	 * If we don't know which subpages are hwpoisoned, we can't free
	 * the hugepage, so it's leaked intentionally.
	 */
	if (folio_test_hugetlb_raw_hwp_unreliable(folio))
		return;

	/*
	 * If folio is not vmemmap optimized (!clear_flag), then the folio
	 * is no longer identified as a hugetlb page.  hugetlb_vmemmap_restore_folio
	 * can only be passed hugetlb pages and will BUG otherwise.
	 */
	if (clear_flag && hugetlb_vmemmap_restore_folio(h, folio)) {
		spin_lock_irq(&hugetlb_lock);
		/*
		 * If we cannot allocate vmemmap pages, just refuse to free the
		 * page and put the page back on the hugetlb free list and treat
		 * as a surplus page.
		 */
		add_hugetlb_folio(h, folio, true);
		spin_unlock_irq(&hugetlb_lock);
		return;
	}

	/*
	 * If vmemmap pages were allocated above, then we need to clear the
	 * hugetlb flag under the hugetlb lock.
	 */
	if (folio_test_hugetlb(folio)) {
		spin_lock_irq(&hugetlb_lock);
		__folio_clear_hugetlb(folio);
		spin_unlock_irq(&hugetlb_lock);
	}

	/*
	 * Move PageHWPoison flag from head page to the raw error pages,
	 * which makes any healthy subpages reusable.
	 */
	if (unlikely(folio_test_hwpoison(folio)))
		folio_clear_hugetlb_hwpoison(folio);

	folio_ref_unfreeze(folio, 1);

	INIT_LIST_HEAD(&folio->_deferred_list);
	hugetlb_free_folio(folio);
}

/*
 * As update_and_free_hugetlb_folio() can be called under any context, so we cannot
 * use GFP_KERNEL to allocate vmemmap pages. However, we can defer the
 * actual freeing in a workqueue to prevent from using GFP_ATOMIC to allocate
 * the vmemmap pages.
 *
 * free_hpage_workfn() locklessly retrieves the linked list of pages to be
 * freed and frees them one-by-one. As the page->mapping pointer is going
 * to be cleared in free_hpage_workfn() anyway, it is reused as the llist_node
 * structure of a lockless linked list of huge pages to be freed.
 */
static LLIST_HEAD(hpage_freelist);

static void free_hpage_workfn(struct work_struct *work)
{
	struct llist_node *node;

	node = llist_del_all(&hpage_freelist);

	while (node) {
		struct folio *folio;
		struct hstate *h;

		folio = container_of((struct address_space **)node,
				     struct folio, mapping);
		node = node->next;
		folio->mapping = NULL;
		/*
		 * The VM_BUG_ON_FOLIO(!folio_test_hugetlb(folio), folio) in
		 * folio_hstate() is going to trigger because a previous call to
		 * remove_hugetlb_folio() will clear the hugetlb bit, so do
		 * not use folio_hstate() directly.
		 */
		h = size_to_hstate(folio_size(folio));

		__update_and_free_hugetlb_folio(h, folio);

		cond_resched();
	}
}
static DECLARE_WORK(free_hpage_work, free_hpage_workfn);

static inline void flush_free_hpage_work(struct hstate *h)
{
	if (hugetlb_vmemmap_optimizable(h))
		flush_work(&free_hpage_work);
}

static void update_and_free_hugetlb_folio(struct hstate *h, struct folio *folio,
				 bool atomic)
{
	if (!folio_test_hugetlb_vmemmap_optimized(folio) || !atomic) {
		__update_and_free_hugetlb_folio(h, folio);
		return;
	}

	/*
	 * Defer freeing to avoid using GFP_ATOMIC to allocate vmemmap pages.
	 *
	 * Only call schedule_work() if hpage_freelist is previously
	 * empty. Otherwise, schedule_work() had been called but the workfn
	 * hasn't retrieved the list yet.
	 */
	if (llist_add((struct llist_node *)&folio->mapping, &hpage_freelist))
		schedule_work(&free_hpage_work);
}

static void bulk_vmemmap_restore_error(struct hstate *h,
					struct list_head *folio_list,
					struct list_head *non_hvo_folios)
{
	struct folio *folio, *t_folio;

	if (!list_empty(non_hvo_folios)) {
		/*
		 * Free any restored hugetlb pages so that restore of the
		 * entire list can be retried.
		 * The idea is that in the common case of ENOMEM errors freeing
		 * hugetlb pages with vmemmap we will free up memory so that we
		 * can allocate vmemmap for more hugetlb pages.
		 */
		list_for_each_entry_safe(folio, t_folio, non_hvo_folios, lru) {
			list_del(&folio->lru);
			spin_lock_irq(&hugetlb_lock);
			__folio_clear_hugetlb(folio);
			spin_unlock_irq(&hugetlb_lock);
			update_and_free_hugetlb_folio(h, folio, false);
			cond_resched();
		}
	} else {
		/*
		 * In the case where there are no folios which can be
		 * immediately freed, we loop through the list trying to restore
		 * vmemmap individually in the hope that someone elsewhere may
		 * have done something to cause success (such as freeing some
		 * memory).  If unable to restore a hugetlb page, the hugetlb
		 * page is made a surplus page and removed from the list.
		 * If are able to restore vmemmap and free one hugetlb page, we
		 * quit processing the list to retry the bulk operation.
		 */
		list_for_each_entry_safe(folio, t_folio, folio_list, lru)
			if (hugetlb_vmemmap_restore_folio(h, folio)) {
				list_del(&folio->lru);
				spin_lock_irq(&hugetlb_lock);
				add_hugetlb_folio(h, folio, true);
				spin_unlock_irq(&hugetlb_lock);
			} else {
				list_del(&folio->lru);
				spin_lock_irq(&hugetlb_lock);
				__folio_clear_hugetlb(folio);
				spin_unlock_irq(&hugetlb_lock);
				update_and_free_hugetlb_folio(h, folio, false);
				cond_resched();
				break;
			}
	}
}

static void update_and_free_pages_bulk(struct hstate *h,
						struct list_head *folio_list)
{
	long ret;
	struct folio *folio, *t_folio;
	LIST_HEAD(non_hvo_folios);

	/*
	 * First allocate required vmemmmap (if necessary) for all folios.
	 * Carefully handle errors and free up any available hugetlb pages
	 * in an effort to make forward progress.
	 */
retry:
	ret = hugetlb_vmemmap_restore_folios(h, folio_list, &non_hvo_folios);
	if (ret < 0) {
		bulk_vmemmap_restore_error(h, folio_list, &non_hvo_folios);
		goto retry;
	}

	/*
	 * At this point, list should be empty, ret should be >= 0 and there
	 * should only be pages on the non_hvo_folios list.
	 * Do note that the non_hvo_folios list could be empty.
	 * Without HVO enabled, ret will be 0 and there is no need to call
	 * __folio_clear_hugetlb as this was done previously.
	 */
	VM_WARN_ON(!list_empty(folio_list));
	VM_WARN_ON(ret < 0);
	if (!list_empty(&non_hvo_folios) && ret) {
		spin_lock_irq(&hugetlb_lock);
		list_for_each_entry(folio, &non_hvo_folios, lru)
			__folio_clear_hugetlb(folio);
		spin_unlock_irq(&hugetlb_lock);
	}

	list_for_each_entry_safe(folio, t_folio, &non_hvo_folios, lru) {
		update_and_free_hugetlb_folio(h, folio, false);
		cond_resched();
	}
}

struct hstate *size_to_hstate(unsigned long size)
{
	struct hstate *h;

	for_each_hstate(h) {
		if (huge_page_size(h) == size)
			return h;
	}
	return NULL;
}

void free_huge_folio(struct folio *folio)
{
	/*
	 * Can't pass hstate in here because it is called from the
	 * generic mm code.
	 */
	struct hstate *h = folio_hstate(folio);
	int nid = folio_nid(folio);
	struct hugepage_subpool *spool = hugetlb_folio_subpool(folio);
	bool restore_reserve;
	unsigned long flags;

	VM_BUG_ON_FOLIO(folio_ref_count(folio), folio);
	VM_BUG_ON_FOLIO(folio_mapcount(folio), folio);

	hugetlb_set_folio_subpool(folio, NULL);
	if (folio_test_anon(folio))
		__ClearPageAnonExclusive(&folio->page);
	folio->mapping = NULL;
	restore_reserve = folio_test_hugetlb_restore_reserve(folio);
	folio_clear_hugetlb_restore_reserve(folio);

	/*
	 * If HPageRestoreReserve was set on page, page allocation consumed a
	 * reservation.  If the page was associated with a subpool, there
	 * would have been a page reserved in the subpool before allocation
	 * via hugepage_subpool_get_pages().  Since we are 'restoring' the
	 * reservation, do not call hugepage_subpool_put_pages() as this will
	 * remove the reserved page from the subpool.
	 */
	if (!restore_reserve) {
		/*
		 * A return code of zero implies that the subpool will be
		 * under its minimum size if the reservation is not restored
		 * after page is free.  Therefore, force restore_reserve
		 * operation.
		 */
		if (hugepage_subpool_put_pages(spool, 1) == 0)
			restore_reserve = true;
	}

	spin_lock_irqsave(&hugetlb_lock, flags);
	folio_clear_hugetlb_migratable(folio);
	hugetlb_cgroup_uncharge_folio(hstate_index(h),
				     pages_per_huge_page(h), folio);
	hugetlb_cgroup_uncharge_folio_rsvd(hstate_index(h),
					  pages_per_huge_page(h), folio);
	lruvec_stat_mod_folio(folio, NR_HUGETLB, -pages_per_huge_page(h));
	mem_cgroup_uncharge(folio);
	if (restore_reserve)
		h->resv_huge_pages++;

	if (folio_test_hugetlb_temporary(folio)) {
		remove_hugetlb_folio(h, folio, false);
		spin_unlock_irqrestore(&hugetlb_lock, flags);
		update_and_free_hugetlb_folio(h, folio, true);
	} else if (h->surplus_huge_pages_node[nid]) {
		/* remove the page from active list */
		remove_hugetlb_folio(h, folio, true);
		spin_unlock_irqrestore(&hugetlb_lock, flags);
		update_and_free_hugetlb_folio(h, folio, true);
	} else {
		arch_clear_hugetlb_flags(folio);
		enqueue_hugetlb_folio(h, folio);
		spin_unlock_irqrestore(&hugetlb_lock, flags);
	}
}

/*
 * Must be called with the hugetlb lock held
 */
static void __prep_account_new_huge_page(struct hstate *h, int nid)
{
	lockdep_assert_held(&hugetlb_lock);
	h->nr_huge_pages++;
	h->nr_huge_pages_node[nid]++;
}

static void init_new_hugetlb_folio(struct hstate *h, struct folio *folio)
{
	__folio_set_hugetlb(folio);
	INIT_LIST_HEAD(&folio->lru);
	hugetlb_set_folio_subpool(folio, NULL);
	set_hugetlb_cgroup(folio, NULL);
	set_hugetlb_cgroup_rsvd(folio, NULL);
}

static void __prep_new_hugetlb_folio(struct hstate *h, struct folio *folio)
{
	init_new_hugetlb_folio(h, folio);
	hugetlb_vmemmap_optimize_folio(h, folio);
}

static void prep_new_hugetlb_folio(struct hstate *h, struct folio *folio, int nid)
{
	__prep_new_hugetlb_folio(h, folio);
	spin_lock_irq(&hugetlb_lock);
	__prep_account_new_huge_page(h, nid);
	spin_unlock_irq(&hugetlb_lock);
}

/*
 * Find and lock address space (mapping) in write mode.
 *
 * Upon entry, the folio is locked which means that folio_mapping() is
 * stable.  Due to locking order, we can only trylock_write.  If we can
 * not get the lock, simply return NULL to caller.
 */
struct address_space *hugetlb_folio_mapping_lock_write(struct folio *folio)
{
	struct address_space *mapping = folio_mapping(folio);

	if (!mapping)
		return mapping;

	if (i_mmap_trylock_write(mapping))
		return mapping;

	return NULL;
}

static struct folio *alloc_buddy_hugetlb_folio(struct hstate *h,
		gfp_t gfp_mask, int nid, nodemask_t *nmask,
		nodemask_t *node_alloc_noretry)
{
	int order = huge_page_order(h);
	struct folio *folio;
	bool alloc_try_hard = true;
	bool retry = true;

	/*
	 * By default we always try hard to allocate the folio with
	 * __GFP_RETRY_MAYFAIL flag.  However, if we are allocating folios in
	 * a loop (to adjust global huge page counts) and previous allocation
	 * failed, do not continue to try hard on the same node.  Use the
	 * node_alloc_noretry bitmap to manage this state information.
	 */
	if (node_alloc_noretry && node_isset(nid, *node_alloc_noretry))
		alloc_try_hard = false;
	if (alloc_try_hard)
		gfp_mask |= __GFP_RETRY_MAYFAIL;
	if (nid == NUMA_NO_NODE)
		nid = numa_mem_id();
retry:
	folio = __folio_alloc(gfp_mask, order, nid, nmask);
	/* Ensure hugetlb folio won't have large_rmappable flag set. */
	if (folio)
		folio_clear_large_rmappable(folio);

	if (folio && !folio_ref_freeze(folio, 1)) {
		folio_put(folio);
		if (retry) {	/* retry once */
			retry = false;
			goto retry;
		}
		/* WOW!  twice in a row. */
		pr_warn("HugeTLB unexpected inflated folio ref count\n");
		folio = NULL;
	}

	/*
	 * If we did not specify __GFP_RETRY_MAYFAIL, but still got a
	 * folio this indicates an overall state change.  Clear bit so
	 * that we resume normal 'try hard' allocations.
	 */
	if (node_alloc_noretry && folio && !alloc_try_hard)
		node_clear(nid, *node_alloc_noretry);

	/*
	 * If we tried hard to get a folio but failed, set bit so that
	 * subsequent attempts will not try as hard until there is an
	 * overall state change.
	 */
	if (node_alloc_noretry && !folio && alloc_try_hard)
		node_set(nid, *node_alloc_noretry);

	if (!folio) {
		__count_vm_event(HTLB_BUDDY_PGALLOC_FAIL);
		return NULL;
	}

	__count_vm_event(HTLB_BUDDY_PGALLOC);
	return folio;
}

static struct folio *only_alloc_fresh_hugetlb_folio(struct hstate *h,
		gfp_t gfp_mask, int nid, nodemask_t *nmask,
		nodemask_t *node_alloc_noretry)
{
	struct folio *folio;

	if (hstate_is_gigantic(h))
		folio = alloc_gigantic_folio(h, gfp_mask, nid, nmask);
	else
		folio = alloc_buddy_hugetlb_folio(h, gfp_mask, nid, nmask, node_alloc_noretry);
	if (folio)
		init_new_hugetlb_folio(h, folio);
	return folio;
}

/*
 * Common helper to allocate a fresh hugetlb page. All specific allocators
 * should use this function to get new hugetlb pages
 *
 * Note that returned page is 'frozen':  ref count of head page and all tail
 * pages is zero.
 */
static struct folio *alloc_fresh_hugetlb_folio(struct hstate *h,
		gfp_t gfp_mask, int nid, nodemask_t *nmask)
{
	struct folio *folio;

	if (hstate_is_gigantic(h))
		folio = alloc_gigantic_folio(h, gfp_mask, nid, nmask);
	else
		folio = alloc_buddy_hugetlb_folio(h, gfp_mask, nid, nmask, NULL);
	if (!folio)
		return NULL;

	prep_new_hugetlb_folio(h, folio, folio_nid(folio));
	return folio;
}

static void prep_and_add_allocated_folios(struct hstate *h,
					struct list_head *folio_list)
{
	unsigned long flags;
	struct folio *folio, *tmp_f;

	/* Send list for bulk vmemmap optimization processing */
	hugetlb_vmemmap_optimize_folios(h, folio_list);

	/* Add all new pool pages to free lists in one lock cycle */
	spin_lock_irqsave(&hugetlb_lock, flags);
	list_for_each_entry_safe(folio, tmp_f, folio_list, lru) {
		__prep_account_new_huge_page(h, folio_nid(folio));
		enqueue_hugetlb_folio(h, folio);
	}
	spin_unlock_irqrestore(&hugetlb_lock, flags);
}

/*
 * Allocates a fresh hugetlb page in a node interleaved manner.  The page
 * will later be added to the appropriate hugetlb pool.
 */
static struct folio *alloc_pool_huge_folio(struct hstate *h,
					nodemask_t *nodes_allowed,
					nodemask_t *node_alloc_noretry,
					int *next_node)
{
	gfp_t gfp_mask = htlb_alloc_mask(h) | __GFP_THISNODE;
	int nr_nodes, node;

	for_each_node_mask_to_alloc(next_node, nr_nodes, node, nodes_allowed) {
		struct folio *folio;

		folio = only_alloc_fresh_hugetlb_folio(h, gfp_mask, node,
					nodes_allowed, node_alloc_noretry);
		if (folio)
			return folio;
	}

	return NULL;
}

/*
 * Remove huge page from pool from next node to free.  Attempt to keep
 * persistent huge pages more or less balanced over allowed nodes.
 * This routine only 'removes' the hugetlb page.  The caller must make
 * an additional call to free the page to low level allocators.
 * Called with hugetlb_lock locked.
 */
static struct folio *remove_pool_hugetlb_folio(struct hstate *h,
		nodemask_t *nodes_allowed, bool acct_surplus)
{
	int nr_nodes, node;
	struct folio *folio = NULL;

	lockdep_assert_held(&hugetlb_lock);
	for_each_node_mask_to_free(h, nr_nodes, node, nodes_allowed) {
		/*
		 * If we're returning unused surplus pages, only examine
		 * nodes with surplus pages.
		 */
		if ((!acct_surplus || h->surplus_huge_pages_node[node]) &&
		    !list_empty(&h->hugepage_freelists[node])) {
			folio = list_entry(h->hugepage_freelists[node].next,
					  struct folio, lru);
			remove_hugetlb_folio(h, folio, acct_surplus);
			break;
		}
	}

	return folio;
}

/*
 * Dissolve a given free hugetlb folio into free buddy pages. This function
 * does nothing for in-use hugetlb folios and non-hugetlb folios.
 * This function returns values like below:
 *
 *  -ENOMEM: failed to allocate vmemmap pages to free the freed hugepages
 *           when the system is under memory pressure and the feature of
 *           freeing unused vmemmap pages associated with each hugetlb page
 *           is enabled.
 *  -EBUSY:  failed to dissolved free hugepages or the hugepage is in-use
 *           (allocated or reserved.)
 *       0:  successfully dissolved free hugepages or the page is not a
 *           hugepage (considered as already dissolved)
 */
int dissolve_free_hugetlb_folio(struct folio *folio)
{
	int rc = -EBUSY;

retry:
	/* Not to disrupt normal path by vainly holding hugetlb_lock */
	if (!folio_test_hugetlb(folio))
		return 0;

	spin_lock_irq(&hugetlb_lock);
	if (!folio_test_hugetlb(folio)) {
		rc = 0;
		goto out;
	}

	if (!folio_ref_count(folio)) {
		struct hstate *h = folio_hstate(folio);
		bool adjust_surplus = false;

		if (!available_huge_pages(h))
			goto out;

		/*
		 * We should make sure that the page is already on the free list
		 * when it is dissolved.
		 */
		if (unlikely(!folio_test_hugetlb_freed(folio))) {
			spin_unlock_irq(&hugetlb_lock);
			cond_resched();

			/*
			 * Theoretically, we should return -EBUSY when we
			 * encounter this race. In fact, we have a chance
			 * to successfully dissolve the page if we do a
			 * retry. Because the race window is quite small.
			 * If we seize this opportunity, it is an optimization
			 * for increasing the success rate of dissolving page.
			 */
			goto retry;
		}

		if (h->surplus_huge_pages_node[folio_nid(folio)])
			adjust_surplus = true;
		remove_hugetlb_folio(h, folio, adjust_surplus);
		h->max_huge_pages--;
		spin_unlock_irq(&hugetlb_lock);

		/*
		 * Normally update_and_free_hugtlb_folio will allocate required vmemmmap
		 * before freeing the page.  update_and_free_hugtlb_folio will fail to
		 * free the page if it can not allocate required vmemmap.  We
		 * need to adjust max_huge_pages if the page is not freed.
		 * Attempt to allocate vmemmmap here so that we can take
		 * appropriate action on failure.
		 *
		 * The folio_test_hugetlb check here is because
		 * remove_hugetlb_folio will clear hugetlb folio flag for
		 * non-vmemmap optimized hugetlb folios.
		 */
		if (folio_test_hugetlb(folio)) {
			rc = hugetlb_vmemmap_restore_folio(h, folio);
			if (rc) {
				spin_lock_irq(&hugetlb_lock);
				add_hugetlb_folio(h, folio, adjust_surplus);
				h->max_huge_pages++;
				goto out;
			}
		} else
			rc = 0;

		update_and_free_hugetlb_folio(h, folio, false);
		return rc;
	}
out:
	spin_unlock_irq(&hugetlb_lock);
	return rc;
}

/*
 * Dissolve free hugepages in a given pfn range. Used by memory hotplug to
 * make specified memory blocks removable from the system.
 * Note that this will dissolve a free gigantic hugepage completely, if any
 * part of it lies within the given range.
 * Also note that if dissolve_free_hugetlb_folio() returns with an error, all
 * free hugetlb folios that were dissolved before that error are lost.
 */
int dissolve_free_hugetlb_folios(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn;
	struct folio *folio;
	int rc = 0;
	unsigned int order;
	struct hstate *h;

	if (!hugepages_supported())
		return rc;

	order = huge_page_order(&default_hstate);
	for_each_hstate(h)
		order = min(order, huge_page_order(h));

	for (pfn = start_pfn; pfn < end_pfn; pfn += 1 << order) {
		folio = pfn_folio(pfn);
		rc = dissolve_free_hugetlb_folio(folio);
		if (rc)
			break;
	}

	return rc;
}

/*
 * Allocates a fresh surplus page from the page allocator.
 */
static struct folio *alloc_surplus_hugetlb_folio(struct hstate *h,
				gfp_t gfp_mask,	int nid, nodemask_t *nmask)
{
	struct folio *folio = NULL;

	if (hstate_is_gigantic(h))
		return NULL;

	spin_lock_irq(&hugetlb_lock);
	if (h->surplus_huge_pages >= h->nr_overcommit_huge_pages)
		goto out_unlock;
	spin_unlock_irq(&hugetlb_lock);

	folio = alloc_fresh_hugetlb_folio(h, gfp_mask, nid, nmask);
	if (!folio)
		return NULL;

	spin_lock_irq(&hugetlb_lock);
	/*
	 * We could have raced with the pool size change.
	 * Double check that and simply deallocate the new page
	 * if we would end up overcommiting the surpluses. Abuse
	 * temporary page to workaround the nasty free_huge_folio
	 * codeflow
	 */
	if (h->surplus_huge_pages >= h->nr_overcommit_huge_pages) {
		folio_set_hugetlb_temporary(folio);
		spin_unlock_irq(&hugetlb_lock);
		free_huge_folio(folio);
		return NULL;
	}

	h->surplus_huge_pages++;
	h->surplus_huge_pages_node[folio_nid(folio)]++;

out_unlock:
	spin_unlock_irq(&hugetlb_lock);

	return folio;
}

static struct folio *alloc_migrate_hugetlb_folio(struct hstate *h, gfp_t gfp_mask,
				     int nid, nodemask_t *nmask)
{
	struct folio *folio;

	if (hstate_is_gigantic(h))
		return NULL;

	folio = alloc_fresh_hugetlb_folio(h, gfp_mask, nid, nmask);
	if (!folio)
		return NULL;

	/* fresh huge pages are frozen */
	folio_ref_unfreeze(folio, 1);
	/*
	 * We do not account these pages as surplus because they are only
	 * temporary and will be released properly on the last reference
	 */
	folio_set_hugetlb_temporary(folio);

	return folio;
}

/*
 * Use the VMA's mpolicy to allocate a huge page from the buddy.
 */
static
struct folio *alloc_buddy_hugetlb_folio_with_mpol(struct hstate *h,
		struct vm_area_struct *vma, unsigned long addr)
{
	struct folio *folio = NULL;
	struct mempolicy *mpol;
	gfp_t gfp_mask = htlb_alloc_mask(h);
	int nid;
	nodemask_t *nodemask;

	nid = huge_node(vma, addr, gfp_mask, &mpol, &nodemask);
	if (mpol_is_preferred_many(mpol)) {
		gfp_t gfp = gfp_mask & ~(__GFP_DIRECT_RECLAIM | __GFP_NOFAIL);

		folio = alloc_surplus_hugetlb_folio(h, gfp, nid, nodemask);

		/* Fallback to all nodes if page==NULL */
		nodemask = NULL;
	}

	if (!folio)
		folio = alloc_surplus_hugetlb_folio(h, gfp_mask, nid, nodemask);
	mpol_cond_put(mpol);
	return folio;
}

struct folio *alloc_hugetlb_folio_reserve(struct hstate *h, int preferred_nid,
		nodemask_t *nmask, gfp_t gfp_mask)
{
	struct folio *folio;

	spin_lock_irq(&hugetlb_lock);
	folio = dequeue_hugetlb_folio_nodemask(h, gfp_mask, preferred_nid,
					       nmask);
	if (folio) {
		VM_BUG_ON(!h->resv_huge_pages);
		h->resv_huge_pages--;
	}

	spin_unlock_irq(&hugetlb_lock);
	return folio;
}

/* folio migration callback function */
struct folio *alloc_hugetlb_folio_nodemask(struct hstate *h, int preferred_nid,
		nodemask_t *nmask, gfp_t gfp_mask, bool allow_alloc_fallback)
{
	spin_lock_irq(&hugetlb_lock);
	if (available_huge_pages(h)) {
		struct folio *folio;

		folio = dequeue_hugetlb_folio_nodemask(h, gfp_mask,
						preferred_nid, nmask);
		if (folio) {
			spin_unlock_irq(&hugetlb_lock);
			return folio;
		}
	}
	spin_unlock_irq(&hugetlb_lock);

	/* We cannot fallback to other nodes, as we could break the per-node pool. */
	if (!allow_alloc_fallback)
		gfp_mask |= __GFP_THISNODE;

	return alloc_migrate_hugetlb_folio(h, gfp_mask, preferred_nid, nmask);
}

static nodemask_t *policy_mbind_nodemask(gfp_t gfp)
{
#ifdef CONFIG_NUMA
	struct mempolicy *mpol = get_task_policy(current);

	/*
	 * Only enforce MPOL_BIND policy which overlaps with cpuset policy
	 * (from policy_nodemask) specifically for hugetlb case
	 */
	if (mpol->mode == MPOL_BIND &&
		(apply_policy_zone(mpol, gfp_zone(gfp)) &&
		 cpuset_nodemask_valid_mems_allowed(&mpol->nodes)))
		return &mpol->nodes;
#endif
	return NULL;
}

/*
 * Increase the hugetlb pool such that it can accommodate a reservation
 * of size 'delta'.
 */
static int gather_surplus_pages(struct hstate *h, long delta)
	__must_hold(&hugetlb_lock)
{
	LIST_HEAD(surplus_list);
	struct folio *folio, *tmp;
	int ret;
	long i;
	long needed, allocated;
	bool alloc_ok = true;
	int node;
	nodemask_t *mbind_nodemask, alloc_nodemask;

	mbind_nodemask = policy_mbind_nodemask(htlb_alloc_mask(h));
	if (mbind_nodemask)
		nodes_and(alloc_nodemask, *mbind_nodemask, cpuset_current_mems_allowed);
	else
		alloc_nodemask = cpuset_current_mems_allowed;

	lockdep_assert_held(&hugetlb_lock);
	needed = (h->resv_huge_pages + delta) - h->free_huge_pages;
	if (needed <= 0) {
		h->resv_huge_pages += delta;
		return 0;
	}

	allocated = 0;

	ret = -ENOMEM;
retry:
	spin_unlock_irq(&hugetlb_lock);
	for (i = 0; i < needed; i++) {
		folio = NULL;

		/* Prioritize current node */
		if (node_isset(numa_mem_id(), alloc_nodemask))
			folio = alloc_surplus_hugetlb_folio(h, htlb_alloc_mask(h),
					numa_mem_id(), NULL);

		if (!folio) {
			for_each_node_mask(node, alloc_nodemask) {
				if (node == numa_mem_id())
					continue;
				folio = alloc_surplus_hugetlb_folio(h, htlb_alloc_mask(h),
						node, NULL);
				if (folio)
					break;
			}
		}
		if (!folio) {
			alloc_ok = false;
			break;
		}
		list_add(&folio->lru, &surplus_list);
		cond_resched();
	}
	allocated += i;

	/*
	 * After retaking hugetlb_lock, we need to recalculate 'needed'
	 * because either resv_huge_pages or free_huge_pages may have changed.
	 */
	spin_lock_irq(&hugetlb_lock);
	needed = (h->resv_huge_pages + delta) -
			(h->free_huge_pages + allocated);
	if (needed > 0) {
		if (alloc_ok)
			goto retry;
		/*
		 * We were not able to allocate enough pages to
		 * satisfy the entire reservation so we free what
		 * we've allocated so far.
		 */
		goto free;
	}
	/*
	 * The surplus_list now contains _at_least_ the number of extra pages
	 * needed to accommodate the reservation.  Add the appropriate number
	 * of pages to the hugetlb pool and free the extras back to the buddy
	 * allocator.  Commit the entire reservation here to prevent another
	 * process from stealing the pages as they are added to the pool but
	 * before they are reserved.
	 */
	needed += allocated;
	h->resv_huge_pages += delta;
	ret = 0;

	/* Free the needed pages to the hugetlb pool */
	list_for_each_entry_safe(folio, tmp, &surplus_list, lru) {
		if ((--needed) < 0)
			break;
		/* Add the page to the hugetlb allocator */
		enqueue_hugetlb_folio(h, folio);
	}
free:
	spin_unlock_irq(&hugetlb_lock);

	/*
	 * Free unnecessary surplus pages to the buddy allocator.
	 * Pages have no ref count, call free_huge_folio directly.
	 */
	list_for_each_entry_safe(folio, tmp, &surplus_list, lru)
		free_huge_folio(folio);
	spin_lock_irq(&hugetlb_lock);

	return ret;
}

/*
 * This routine has two main purposes:
 * 1) Decrement the reservation count (resv_huge_pages) by the value passed
 *    in unused_resv_pages.  This corresponds to the prior adjustments made
 *    to the associated reservation map.
 * 2) Free any unused surplus pages that may have been allocated to satisfy
 *    the reservation.  As many as unused_resv_pages may be freed.
 */
static void return_unused_surplus_pages(struct hstate *h,
					unsigned long unused_resv_pages)
{
	unsigned long nr_pages;
	LIST_HEAD(page_list);

	lockdep_assert_held(&hugetlb_lock);
	/* Uncommit the reservation */
	h->resv_huge_pages -= unused_resv_pages;

	if (hstate_is_gigantic(h) && !gigantic_page_runtime_supported())
		goto out;

	/*
	 * Part (or even all) of the reservation could have been backed
	 * by pre-allocated pages. Only free surplus pages.
	 */
	nr_pages = min(unused_resv_pages, h->surplus_huge_pages);

	/*
	 * We want to release as many surplus pages as possible, spread
	 * evenly across all nodes with memory. Iterate across these nodes
	 * until we can no longer free unreserved surplus pages. This occurs
	 * when the nodes with surplus pages have no free pages.
	 * remove_pool_hugetlb_folio() will balance the freed pages across the
	 * on-line nodes with memory and will handle the hstate accounting.
	 */
	while (nr_pages--) {
		struct folio *folio;

		folio = remove_pool_hugetlb_folio(h, &node_states[N_MEMORY], 1);
		if (!folio)
			goto out;

		list_add(&folio->lru, &page_list);
	}

out:
	spin_unlock_irq(&hugetlb_lock);
	update_and_free_pages_bulk(h, &page_list);
	spin_lock_irq(&hugetlb_lock);
}


/*
 * vma_needs_reservation, vma_commit_reservation and vma_end_reservation
 * are used by the huge page allocation routines to manage reservations.
 *
 * vma_needs_reservation is called to determine if the huge page at addr
 * within the vma has an associated reservation.  If a reservation is
 * needed, the value 1 is returned.  The caller is then responsible for
 * managing the global reservation and subpool usage counts.  After
 * the huge page has been allocated, vma_commit_reservation is called
 * to add the page to the reservation map.  If the page allocation fails,
 * the reservation must be ended instead of committed.  vma_end_reservation
 * is called in such cases.
 *
 * In the normal case, vma_commit_reservation returns the same value
 * as the preceding vma_needs_reservation call.  The only time this
 * is not the case is if a reserve map was changed between calls.  It
 * is the responsibility of the caller to notice the difference and
 * take appropriate action.
 *
 * vma_add_reservation is used in error paths where a reservation must
 * be restored when a newly allocated huge page must be freed.  It is
 * to be called after calling vma_needs_reservation to determine if a
 * reservation exists.
 *
 * vma_del_reservation is used in error paths where an entry in the reserve
 * map was created during huge page allocation and must be removed.  It is to
 * be called after calling vma_needs_reservation to determine if a reservation
 * exists.
 */
enum vma_resv_mode {
	VMA_NEEDS_RESV,
	VMA_COMMIT_RESV,
	VMA_END_RESV,
	VMA_ADD_RESV,
	VMA_DEL_RESV,
};
static long __vma_reservation_common(struct hstate *h,
				struct vm_area_struct *vma, unsigned long addr,
				enum vma_resv_mode mode)
{
	struct resv_map *resv;
	pgoff_t idx;
	long ret;
	long dummy_out_regions_needed;

	resv = vma_resv_map(vma);
	if (!resv)
		return 1;

	idx = vma_hugecache_offset(h, vma, addr);
	switch (mode) {
	case VMA_NEEDS_RESV:
		ret = region_chg(resv, idx, idx + 1, &dummy_out_regions_needed);
		/* We assume that vma_reservation_* routines always operate on
		 * 1 page, and that adding to resv map a 1 page entry can only
		 * ever require 1 region.
		 */
		VM_BUG_ON(dummy_out_regions_needed != 1);
		break;
	case VMA_COMMIT_RESV:
		ret = region_add(resv, idx, idx + 1, 1, NULL, NULL);
		/* region_add calls of range 1 should never fail. */
		VM_BUG_ON(ret < 0);
		break;
	case VMA_END_RESV:
		region_abort(resv, idx, idx + 1, 1);
		ret = 0;
		break;
	case VMA_ADD_RESV:
		if (vma->vm_flags & VM_MAYSHARE) {
			ret = region_add(resv, idx, idx + 1, 1, NULL, NULL);
			/* region_add calls of range 1 should never fail. */
			VM_BUG_ON(ret < 0);
		} else {
			region_abort(resv, idx, idx + 1, 1);
			ret = region_del(resv, idx, idx + 1);
		}
		break;
	case VMA_DEL_RESV:
		if (vma->vm_flags & VM_MAYSHARE) {
			region_abort(resv, idx, idx + 1, 1);
			ret = region_del(resv, idx, idx + 1);
		} else {
			ret = region_add(resv, idx, idx + 1, 1, NULL, NULL);
			/* region_add calls of range 1 should never fail. */
			VM_BUG_ON(ret < 0);
		}
		break;
	default:
		BUG();
	}

	if (vma->vm_flags & VM_MAYSHARE || mode == VMA_DEL_RESV)
		return ret;
	/*
	 * We know private mapping must have HPAGE_RESV_OWNER set.
	 *
	 * In most cases, reserves always exist for private mappings.
	 * However, a file associated with mapping could have been
	 * hole punched or truncated after reserves were consumed.
	 * As subsequent fault on such a range will not use reserves.
	 * Subtle - The reserve map for private mappings has the
	 * opposite meaning than that of shared mappings.  If NO
	 * entry is in the reserve map, it means a reservation exists.
	 * If an entry exists in the reserve map, it means the
	 * reservation has already been consumed.  As a result, the
	 * return value of this routine is the opposite of the
	 * value returned from reserve map manipulation routines above.
	 */
	if (ret > 0)
		return 0;
	if (ret == 0)
		return 1;
	return ret;
}

static long vma_needs_reservation(struct hstate *h,
			struct vm_area_struct *vma, unsigned long addr)
{
	return __vma_reservation_common(h, vma, addr, VMA_NEEDS_RESV);
}

static long vma_commit_reservation(struct hstate *h,
			struct vm_area_struct *vma, unsigned long addr)
{
	return __vma_reservation_common(h, vma, addr, VMA_COMMIT_RESV);
}

static void vma_end_reservation(struct hstate *h,
			struct vm_area_struct *vma, unsigned long addr)
{
	(void)__vma_reservation_common(h, vma, addr, VMA_END_RESV);
}

static long vma_add_reservation(struct hstate *h,
			struct vm_area_struct *vma, unsigned long addr)
{
	return __vma_reservation_common(h, vma, addr, VMA_ADD_RESV);
}

static long vma_del_reservation(struct hstate *h,
			struct vm_area_struct *vma, unsigned long addr)
{
	return __vma_reservation_common(h, vma, addr, VMA_DEL_RESV);
}

/*
 * This routine is called to restore reservation information on error paths.
 * It should ONLY be called for folios allocated via alloc_hugetlb_folio(),
 * and the hugetlb mutex should remain held when calling this routine.
 *
 * It handles two specific cases:
 * 1) A reservation was in place and the folio consumed the reservation.
 *    hugetlb_restore_reserve is set in the folio.
 * 2) No reservation was in place for the page, so hugetlb_restore_reserve is
 *    not set.  However, alloc_hugetlb_folio always updates the reserve map.
 *
 * In case 1, free_huge_folio later in the error path will increment the
 * global reserve count.  But, free_huge_folio does not have enough context
 * to adjust the reservation map.  This case deals primarily with private
 * mappings.  Adjust the reserve map here to be consistent with global
 * reserve count adjustments to be made by free_huge_folio.  Make sure the
 * reserve map indicates there is a reservation present.
 *
 * In case 2, simply undo reserve map modifications done by alloc_hugetlb_folio.
 */
void restore_reserve_on_error(struct hstate *h, struct vm_area_struct *vma,
			unsigned long address, struct folio *folio)
{
	long rc = vma_needs_reservation(h, vma, address);

	if (folio_test_hugetlb_restore_reserve(folio)) {
		if (unlikely(rc < 0))
			/*
			 * Rare out of memory condition in reserve map
			 * manipulation.  Clear hugetlb_restore_reserve so
			 * that global reserve count will not be incremented
			 * by free_huge_folio.  This will make it appear
			 * as though the reservation for this folio was
			 * consumed.  This may prevent the task from
			 * faulting in the folio at a later time.  This
			 * is better than inconsistent global huge page
			 * accounting of reserve counts.
			 */
			folio_clear_hugetlb_restore_reserve(folio);
		else if (rc)
			(void)vma_add_reservation(h, vma, address);
		else
			vma_end_reservation(h, vma, address);
	} else {
		if (!rc) {
			/*
			 * This indicates there is an entry in the reserve map
			 * not added by alloc_hugetlb_folio.  We know it was added
			 * before the alloc_hugetlb_folio call, otherwise
			 * hugetlb_restore_reserve would be set on the folio.
			 * Remove the entry so that a subsequent allocation
			 * does not consume a reservation.
			 */
			rc = vma_del_reservation(h, vma, address);
			if (rc < 0)
				/*
				 * VERY rare out of memory condition.  Since
				 * we can not delete the entry, set
				 * hugetlb_restore_reserve so that the reserve
				 * count will be incremented when the folio
				 * is freed.  This reserve will be consumed
				 * on a subsequent allocation.
				 */
				folio_set_hugetlb_restore_reserve(folio);
		} else if (rc < 0) {
			/*
			 * Rare out of memory condition from
			 * vma_needs_reservation call.  Memory allocation is
			 * only attempted if a new entry is needed.  Therefore,
			 * this implies there is not an entry in the
			 * reserve map.
			 *
			 * For shared mappings, no entry in the map indicates
			 * no reservation.  We are done.
			 */
			if (!(vma->vm_flags & VM_MAYSHARE))
				/*
				 * For private mappings, no entry indicates
				 * a reservation is present.  Since we can
				 * not add an entry, set hugetlb_restore_reserve
				 * on the folio so reserve count will be
				 * incremented when freed.  This reserve will
				 * be consumed on a subsequent allocation.
				 */
				folio_set_hugetlb_restore_reserve(folio);
		} else
			/*
			 * No reservation present, do nothing
			 */
			 vma_end_reservation(h, vma, address);
	}
}

/*
 * alloc_and_dissolve_hugetlb_folio - Allocate a new folio and dissolve
 * the old one
 * @h: struct hstate old page belongs to
 * @old_folio: Old folio to dissolve
 * @list: List to isolate the page in case we need to
 * Returns 0 on success, otherwise negated error.
 */
static int alloc_and_dissolve_hugetlb_folio(struct hstate *h,
			struct folio *old_folio, struct list_head *list)
{
	gfp_t gfp_mask = htlb_alloc_mask(h) | __GFP_THISNODE;
	int nid = folio_nid(old_folio);
	struct folio *new_folio = NULL;
	int ret = 0;

retry:
	spin_lock_irq(&hugetlb_lock);
	if (!folio_test_hugetlb(old_folio)) {
		/*
		 * Freed from under us. Drop new_folio too.
		 */
		goto free_new;
	} else if (folio_ref_count(old_folio)) {
		bool isolated;

		/*
		 * Someone has grabbed the folio, try to isolate it here.
		 * Fail with -EBUSY if not possible.
		 */
		spin_unlock_irq(&hugetlb_lock);
		isolated = folio_isolate_hugetlb(old_folio, list);
		ret = isolated ? 0 : -EBUSY;
		spin_lock_irq(&hugetlb_lock);
		goto free_new;
	} else if (!folio_test_hugetlb_freed(old_folio)) {
		/*
		 * Folio's refcount is 0 but it has not been enqueued in the
		 * freelist yet. Race window is small, so we can succeed here if
		 * we retry.
		 */
		spin_unlock_irq(&hugetlb_lock);
		cond_resched();
		goto retry;
	} else {
		if (!new_folio) {
			spin_unlock_irq(&hugetlb_lock);
			new_folio = alloc_buddy_hugetlb_folio(h, gfp_mask, nid,
							      NULL, NULL);
			if (!new_folio)
				return -ENOMEM;
			__prep_new_hugetlb_folio(h, new_folio);
			goto retry;
		}

		/*
		 * Ok, old_folio is still a genuine free hugepage. Remove it from
		 * the freelist and decrease the counters. These will be
		 * incremented again when calling __prep_account_new_huge_page()
		 * and enqueue_hugetlb_folio() for new_folio. The counters will
		 * remain stable since this happens under the lock.
		 */
		remove_hugetlb_folio(h, old_folio, false);

		/*
		 * Ref count on new_folio is already zero as it was dropped
		 * earlier.  It can be directly added to the pool free list.
		 */
		__prep_account_new_huge_page(h, nid);
		enqueue_hugetlb_folio(h, new_folio);

		/*
		 * Folio has been replaced, we can safely free the old one.
		 */
		spin_unlock_irq(&hugetlb_lock);
		update_and_free_hugetlb_folio(h, old_folio, false);
	}

	return ret;

free_new:
	spin_unlock_irq(&hugetlb_lock);
	if (new_folio)
		update_and_free_hugetlb_folio(h, new_folio, false);

	return ret;
}

int isolate_or_dissolve_huge_page(struct page *page, struct list_head *list)
{
	struct hstate *h;
	struct folio *folio = page_folio(page);
	int ret = -EBUSY;

	/*
	 * The page might have been dissolved from under our feet, so make sure
	 * to carefully check the state under the lock.
	 * Return success when racing as if we dissolved the page ourselves.
	 */
	spin_lock_irq(&hugetlb_lock);
	if (folio_test_hugetlb(folio)) {
		h = folio_hstate(folio);
	} else {
		spin_unlock_irq(&hugetlb_lock);
		return 0;
	}
	spin_unlock_irq(&hugetlb_lock);

	/*
	 * Fence off gigantic pages as there is a cyclic dependency between
	 * alloc_contig_range and them. Return -ENOMEM as this has the effect
	 * of bailing out right away without further retrying.
	 */
	if (hstate_is_gigantic(h))
		return -ENOMEM;

	if (folio_ref_count(folio) && folio_isolate_hugetlb(folio, list))
		ret = 0;
	else if (!folio_ref_count(folio))
		ret = alloc_and_dissolve_hugetlb_folio(h, folio, list);

	return ret;
}

/*
 *  replace_free_hugepage_folios - Replace free hugepage folios in a given pfn
 *  range with new folios.
 *  @start_pfn: start pfn of the given pfn range
 *  @end_pfn: end pfn of the given pfn range
 *  Returns 0 on success, otherwise negated error.
 */
int replace_free_hugepage_folios(unsigned long start_pfn, unsigned long end_pfn)
{
	struct hstate *h;
	struct folio *folio;
	int ret = 0;

	LIST_HEAD(isolate_list);

	while (start_pfn < end_pfn) {
		folio = pfn_folio(start_pfn);
		if (folio_test_hugetlb(folio)) {
			h = folio_hstate(folio);
		} else {
			start_pfn++;
			continue;
		}

		if (!folio_ref_count(folio)) {
			ret = alloc_and_dissolve_hugetlb_folio(h, folio,
							       &isolate_list);
			if (ret)
				break;

			putback_movable_pages(&isolate_list);
		}
		start_pfn++;
	}

	return ret;
}

void wait_for_freed_hugetlb_folios(void)
{
	if (llist_empty(&hpage_freelist))
		return;

	flush_work(&free_hpage_work);
}

typedef enum {
	/*
	 * For either 0/1: we checked the per-vma resv map, and one resv
	 * count either can be reused (0), or an extra needed (1).
	 */
	MAP_CHG_REUSE = 0,
	MAP_CHG_NEEDED = 1,
	/*
	 * Cannot use per-vma resv count can be used, hence a new resv
	 * count is enforced.
	 *
	 * NOTE: This is mostly identical to MAP_CHG_NEEDED, except
	 * that currently vma_needs_reservation() has an unwanted side
	 * effect to either use end() or commit() to complete the
	 * transaction.	 Hence it needs to differenciate from NEEDED.
	 */
	MAP_CHG_ENFORCED = 2,
} map_chg_state;

/*
 * NOTE! "cow_from_owner" represents a very hacky usage only used in CoW
 * faults of hugetlb private mappings on top of a non-page-cache folio (in
 * which case even if there's a private vma resv map it won't cover such
 * allocation).  New call sites should (probably) never set it to true!!
 * When it's set, the allocation will bypass all vma level reservations.
 */
struct folio *alloc_hugetlb_folio(struct vm_area_struct *vma,
				    unsigned long addr, bool cow_from_owner)
{
	struct hugepage_subpool *spool = subpool_vma(vma);
	struct hstate *h = hstate_vma(vma);
	struct folio *folio;
	long retval, gbl_chg;
	map_chg_state map_chg;
	int ret, idx;
	struct hugetlb_cgroup *h_cg = NULL;
	gfp_t gfp = htlb_alloc_mask(h) | __GFP_RETRY_MAYFAIL;

	idx = hstate_index(h);

	/* Whether we need a separate per-vma reservation? */
	if (cow_from_owner) {
		/*
		 * Special case!  Since it's a CoW on top of a reserved
		 * page, the private resv map doesn't count.  So it cannot
		 * consume the per-vma resv map even if it's reserved.
		 */
		map_chg = MAP_CHG_ENFORCED;
	} else {
		/*
		 * Examine the region/reserve map to determine if the process
		 * has a reservation for the page to be allocated.  A return
		 * code of zero indicates a reservation exists (no change).
		 */
		retval = vma_needs_reservation(h, vma, addr);
		if (retval < 0)
			return ERR_PTR(-ENOMEM);
		map_chg = retval ? MAP_CHG_NEEDED : MAP_CHG_REUSE;
	}

	/*
	 * Whether we need a separate global reservation?
	 *
	 * Processes that did not create the mapping will have no
	 * reserves as indicated by the region/reserve map. Check
	 * that the allocation will not exceed the subpool limit.
	 * Or if it can get one from the pool reservation directly.
	 */
	if (map_chg) {
		gbl_chg = hugepage_subpool_get_pages(spool, 1);
		if (gbl_chg < 0)
			goto out_end_reservation;
	} else {
		/*
		 * If we have the vma reservation ready, no need for extra
		 * global reservation.
		 */
		gbl_chg = 0;
	}

	/*
	 * If this allocation is not consuming a per-vma reservation,
	 * charge the hugetlb cgroup now.
	 */
	if (map_chg) {
		ret = hugetlb_cgroup_charge_cgroup_rsvd(
			idx, pages_per_huge_page(h), &h_cg);
		if (ret)
			goto out_subpool_put;
	}

	ret = hugetlb_cgroup_charge_cgroup(idx, pages_per_huge_page(h), &h_cg);
	if (ret)
		goto out_uncharge_cgroup_reservation;

	spin_lock_irq(&hugetlb_lock);
	/*
	 * glb_chg is passed to indicate whether or not a page must be taken
	 * from the global free pool (global change).  gbl_chg == 0 indicates
	 * a reservation exists for the allocation.
	 */
	folio = dequeue_hugetlb_folio_vma(h, vma, addr, gbl_chg);
	if (!folio) {
		spin_unlock_irq(&hugetlb_lock);
		folio = alloc_buddy_hugetlb_folio_with_mpol(h, vma, addr);
		if (!folio)
			goto out_uncharge_cgroup;
		spin_lock_irq(&hugetlb_lock);
		list_add(&folio->lru, &h->hugepage_activelist);
		folio_ref_unfreeze(folio, 1);
		/* Fall through */
	}

	/*
	 * Either dequeued or buddy-allocated folio needs to add special
	 * mark to the folio when it consumes a global reservation.
	 */
	if (!gbl_chg) {
		folio_set_hugetlb_restore_reserve(folio);
		h->resv_huge_pages--;
	}

	hugetlb_cgroup_commit_charge(idx, pages_per_huge_page(h), h_cg, folio);
	/* If allocation is not consuming a reservation, also store the
	 * hugetlb_cgroup pointer on the page.
	 */
	if (map_chg) {
		hugetlb_cgroup_commit_charge_rsvd(idx, pages_per_huge_page(h),
						  h_cg, folio);
	}

	spin_unlock_irq(&hugetlb_lock);

	hugetlb_set_folio_subpool(folio, spool);

	if (map_chg != MAP_CHG_ENFORCED) {
		/* commit() is only needed if the map_chg is not enforced */
		retval = vma_commit_reservation(h, vma, addr);
		/*
		 * Check for possible race conditions. When it happens..
		 * The page was added to the reservation map between
		 * vma_needs_reservation and vma_commit_reservation.
		 * This indicates a race with hugetlb_reserve_pages.
		 * Adjust for the subpool count incremented above AND
		 * in hugetlb_reserve_pages for the same page.	Also,
		 * the reservation count added in hugetlb_reserve_pages
		 * no longer applies.
		 */
		if (unlikely(map_chg == MAP_CHG_NEEDED && retval == 0)) {
			long rsv_adjust;

			rsv_adjust = hugepage_subpool_put_pages(spool, 1);
			hugetlb_acct_memory(h, -rsv_adjust);
			if (map_chg) {
				spin_lock_irq(&hugetlb_lock);
				hugetlb_cgroup_uncharge_folio_rsvd(
				    hstate_index(h), pages_per_huge_page(h),
				    folio);
				spin_unlock_irq(&hugetlb_lock);
			}
		}
	}

	ret = mem_cgroup_charge_hugetlb(folio, gfp);
	/*
	 * Unconditionally increment NR_HUGETLB here. If it turns out that
	 * mem_cgroup_charge_hugetlb failed, then immediately free the page and
	 * decrement NR_HUGETLB.
	 */
	lruvec_stat_mod_folio(folio, NR_HUGETLB, pages_per_huge_page(h));

	if (ret == -ENOMEM) {
		free_huge_folio(folio);
		return ERR_PTR(-ENOMEM);
	}

	return folio;

out_uncharge_cgroup:
	hugetlb_cgroup_uncharge_cgroup(idx, pages_per_huge_page(h), h_cg);
out_uncharge_cgroup_reservation:
	if (map_chg)
		hugetlb_cgroup_uncharge_cgroup_rsvd(idx, pages_per_huge_page(h),
						    h_cg);
out_subpool_put:
	if (map_chg)
		hugepage_subpool_put_pages(spool, 1);
out_end_reservation:
	if (map_chg != MAP_CHG_ENFORCED)
		vma_end_reservation(h, vma, addr);
	return ERR_PTR(-ENOSPC);
}

int alloc_bootmem_huge_page(struct hstate *h, int nid)
	__attribute__ ((weak, alias("__alloc_bootmem_huge_page")));
int __alloc_bootmem_huge_page(struct hstate *h, int nid)
{
	struct huge_bootmem_page *m = NULL; /* initialize for clang */
	int nr_nodes, node = nid;

	/* do node specific alloc */
	if (nid != NUMA_NO_NODE) {
		m = memblock_alloc_exact_nid_raw(huge_page_size(h), huge_page_size(h),
				0, MEMBLOCK_ALLOC_ACCESSIBLE, nid);
		if (!m)
			return 0;
		goto found;
	}
	/* allocate from next node when distributing huge pages */
	for_each_node_mask_to_alloc(&h->next_nid_to_alloc, nr_nodes, node, &node_states[N_ONLINE]) {
		m = memblock_alloc_try_nid_raw(
				huge_page_size(h), huge_page_size(h),
				0, MEMBLOCK_ALLOC_ACCESSIBLE, node);
		/*
		 * Use the beginning of the huge page to store the
		 * huge_bootmem_page struct (until gather_bootmem
		 * puts them into the mem_map).
		 */
		if (!m)
			return 0;
		goto found;
	}

found:

	/*
	 * Only initialize the head struct page in memmap_init_reserved_pages,
	 * rest of the struct pages will be initialized by the HugeTLB
	 * subsystem itself.
	 * The head struct page is used to get folio information by the HugeTLB
	 * subsystem like zone id and node id.
	 */
	memblock_reserved_mark_noinit(virt_to_phys((void *)m + PAGE_SIZE),
		huge_page_size(h) - PAGE_SIZE);

	/*
	 * Put them into a private list first because mem_map is not up yet.
	 *
	 * For pre-HVO to work correctly, pages need to be on the list for
	 * the node they were actually allocated from. That node may be
	 * different in the case of fallback by memblock_alloc_try_nid_raw.
	 * So, extract the actual node first.
	 */
	if (nid == NUMA_NO_NODE)
		node = early_pfn_to_nid(PHYS_PFN(virt_to_phys(m)));

	INIT_LIST_HEAD(&m->list);
	list_add(&m->list, &huge_boot_pages[node]);
	m->hstate = h;
	m->flags = 0;
	return 1;
}

/* Initialize [start_page:end_page_number] tail struct pages of a hugepage */
static void __init hugetlb_folio_init_tail_vmemmap(struct folio *folio,
					unsigned long start_page_number,
					unsigned long end_page_number)
{
	enum zone_type zone = zone_idx(folio_zone(folio));
	int nid = folio_nid(folio);
	unsigned long head_pfn = folio_pfn(folio);
	unsigned long pfn, end_pfn = head_pfn + end_page_number;
	int ret;

	for (pfn = head_pfn + start_page_number; pfn < end_pfn; pfn++) {
		struct page *page = pfn_to_page(pfn);

		__init_single_page(page, pfn, zone, nid);
		prep_compound_tail((struct page *)folio, pfn - head_pfn);
		ret = page_ref_freeze(page, 1);
		VM_BUG_ON(!ret);
	}
}

static void __init hugetlb_folio_init_vmemmap(struct folio *folio,
					      struct hstate *h,
					      unsigned long nr_pages)
{
	int ret;

	/* Prepare folio head */
	__folio_clear_reserved(folio);
	__folio_set_head(folio);
	ret = folio_ref_freeze(folio, 1);
	VM_BUG_ON(!ret);
	/* Initialize the necessary tail struct pages */
	hugetlb_folio_init_tail_vmemmap(folio, 1, nr_pages);
	prep_compound_head((struct page *)folio, huge_page_order(h));
}

/*
 * memblock-allocated pageblocks might not have the migrate type set
 * if marked with the 'noinit' flag. Set it to the default (MIGRATE_MOVABLE)
 * here.
 *
 * Note that this will not write the page struct, it is ok (and necessary)
 * to do this on vmemmap optimized folios.
 */
static void __init hugetlb_bootmem_init_migratetype(struct folio *folio,
							  struct hstate *h)
{
	unsigned long nr_pages = pages_per_huge_page(h), i;

	WARN_ON_ONCE(!pageblock_aligned(folio_pfn(folio)));

	for (i = 0; i < nr_pages; i += pageblock_nr_pages)
		set_pageblock_migratetype(folio_page(folio, i),
					  MIGRATE_MOVABLE);
}

static void __init prep_and_add_bootmem_folios(struct hstate *h,
					struct list_head *folio_list)
{
	unsigned long flags;
	struct folio *folio, *tmp_f;

	/* Send list for bulk vmemmap optimization processing */
	hugetlb_vmemmap_optimize_bootmem_folios(h, folio_list);

	list_for_each_entry_safe(folio, tmp_f, folio_list, lru) {
		if (!folio_test_hugetlb_vmemmap_optimized(folio)) {
			/*
			 * If HVO fails, initialize all tail struct pages
			 * We do not worry about potential long lock hold
			 * time as this is early in boot and there should
			 * be no contention.
			 */
			hugetlb_folio_init_tail_vmemmap(folio,
					HUGETLB_VMEMMAP_RESERVE_PAGES,
					pages_per_huge_page(h));
		}
		hugetlb_bootmem_init_migratetype(folio, h);
		/* Subdivide locks to achieve better parallel performance */
		spin_lock_irqsave(&hugetlb_lock, flags);
		__prep_account_new_huge_page(h, folio_nid(folio));
		enqueue_hugetlb_folio(h, folio);
		spin_unlock_irqrestore(&hugetlb_lock, flags);
	}
}

bool __init hugetlb_bootmem_page_zones_valid(int nid,
					     struct huge_bootmem_page *m)
{
	unsigned long start_pfn;
	bool valid;

	if (m->flags & HUGE_BOOTMEM_ZONES_VALID) {
		/*
		 * Already validated, skip check.
		 */
		return true;
	}

	start_pfn = virt_to_phys(m) >> PAGE_SHIFT;

	valid = !pfn_range_intersects_zones(nid, start_pfn,
			pages_per_huge_page(m->hstate));
	if (!valid)
		hstate_boot_nrinvalid[hstate_index(m->hstate)]++;

	return valid;
}

/*
 * Free a bootmem page that was found to be invalid (intersecting with
 * multiple zones).
 *
 * Since it intersects with multiple zones, we can't just do a free
 * operation on all pages at once, but instead have to walk all
 * pages, freeing them one by one.
 */
static void __init hugetlb_bootmem_free_invalid_page(int nid, struct page *page,
					     struct hstate *h)
{
	unsigned long npages = pages_per_huge_page(h);
	unsigned long pfn;

	while (npages--) {
		pfn = page_to_pfn(page);
		__init_reserved_page_zone(pfn, nid);
		free_reserved_page(page);
		page++;
	}
}

static bool __init hugetlb_bootmem_page_prehvo(struct huge_bootmem_page *m)
{
	return (m->flags & HUGE_BOOTMEM_HVO);
}

/*
 * Put bootmem huge pages into the standard lists after mem_map is up.
 * Note: This only applies to gigantic (order > MAX_PAGE_ORDER) pages.
 */
static void __init gather_bootmem_prealloc_node(unsigned long nid)
{
	LIST_HEAD(folio_list);
	struct huge_bootmem_page *m, *tm;
	struct hstate *h = NULL, *prev_h = NULL;

	list_for_each_entry_safe(m, tm, &huge_boot_pages[nid], list) {
		struct page *page = virt_to_page(m);
		struct folio *folio = (void *)page;

		h = m->hstate;
		if (!hugetlb_bootmem_page_zones_valid(nid, m)) {
			/*
			 * Can't use this page. Initialize the
			 * page structures if that hasn't already
			 * been done, and give them to the page
			 * allocator.
			 */
			hugetlb_bootmem_free_invalid_page(nid, page, h);
			continue;
		}

		/*
		 * It is possible to have multiple huge page sizes (hstates)
		 * in this list.  If so, process each size separately.
		 */
		if (h != prev_h && prev_h != NULL)
			prep_and_add_bootmem_folios(prev_h, &folio_list);
		prev_h = h;

		VM_BUG_ON(!hstate_is_gigantic(h));
		WARN_ON(folio_ref_count(folio) != 1);

		hugetlb_folio_init_vmemmap(folio, h,
					   HUGETLB_VMEMMAP_RESERVE_PAGES);
		init_new_hugetlb_folio(h, folio);

		if (hugetlb_bootmem_page_prehvo(m))
			/*
			 * If pre-HVO was done, just set the
			 * flag, the HVO code will then skip
			 * this folio.
			 */
			folio_set_hugetlb_vmemmap_optimized(folio);

		list_add(&folio->lru, &folio_list);

		/*
		 * We need to restore the 'stolen' pages to totalram_pages
		 * in order to fix confusing memory reports from free(1) and
		 * other side-effects, like CommitLimit going negative.
		 */
		adjust_managed_page_count(page, pages_per_huge_page(h));
		cond_resched();
	}

	prep_and_add_bootmem_folios(h, &folio_list);
}

static void __init gather_bootmem_prealloc_parallel(unsigned long start,
						    unsigned long end, void *arg)
{
	int nid;

	for (nid = start; nid < end; nid++)
		gather_bootmem_prealloc_node(nid);
}

static void __init gather_bootmem_prealloc(void)
{
	struct padata_mt_job job = {
		.thread_fn	= gather_bootmem_prealloc_parallel,
		.fn_arg		= NULL,
		.start		= 0,
		.size		= nr_node_ids,
		.align		= 1,
		.min_chunk	= 1,
		.max_threads	= num_node_state(N_MEMORY),
		.numa_aware	= true,
	};

	padata_do_multithreaded(&job);
}

static void __init hugetlb_hstate_alloc_pages_onenode(struct hstate *h, int nid)
{
	unsigned long i;
	char buf[32];
	LIST_HEAD(folio_list);

	for (i = 0; i < h->max_huge_pages_node[nid]; ++i) {
		if (hstate_is_gigantic(h)) {
			if (!alloc_bootmem_huge_page(h, nid))
				break;
		} else {
			struct folio *folio;
			gfp_t gfp_mask = htlb_alloc_mask(h) | __GFP_THISNODE;

			folio = only_alloc_fresh_hugetlb_folio(h, gfp_mask, nid,
					&node_states[N_MEMORY], NULL);
			if (!folio)
				break;
			list_add(&folio->lru, &folio_list);
		}
		cond_resched();
	}

	if (!list_empty(&folio_list))
		prep_and_add_allocated_folios(h, &folio_list);

	if (i == h->max_huge_pages_node[nid])
		return;

	string_get_size(huge_page_size(h), 1, STRING_UNITS_2, buf, 32);
	pr_warn("HugeTLB: allocating %u of page size %s failed node%d.  Only allocated %lu hugepages.\n",
		h->max_huge_pages_node[nid], buf, nid, i);
	h->max_huge_pages -= (h->max_huge_pages_node[nid] - i);
	h->max_huge_pages_node[nid] = i;
}

static bool __init hugetlb_hstate_alloc_pages_specific_nodes(struct hstate *h)
{
	int i;
	bool node_specific_alloc = false;

	for_each_online_node(i) {
		if (h->max_huge_pages_node[i] > 0) {
			hugetlb_hstate_alloc_pages_onenode(h, i);
			node_specific_alloc = true;
		}
	}

	return node_specific_alloc;
}

static void __init hugetlb_hstate_alloc_pages_errcheck(unsigned long allocated, struct hstate *h)
{
	if (allocated < h->max_huge_pages) {
		char buf[32];

		string_get_size(huge_page_size(h), 1, STRING_UNITS_2, buf, 32);
		pr_warn("HugeTLB: allocating %lu of page size %s failed.  Only allocated %lu hugepages.\n",
			h->max_huge_pages, buf, allocated);
		h->max_huge_pages = allocated;
	}
}

static void __init hugetlb_pages_alloc_boot_node(unsigned long start, unsigned long end, void *arg)
{
	struct hstate *h = (struct hstate *)arg;
	int i, num = end - start;
	nodemask_t node_alloc_noretry;
	LIST_HEAD(folio_list);
	int next_node = first_online_node;

	/* Bit mask controlling how hard we retry per-node allocations.*/
	nodes_clear(node_alloc_noretry);

	for (i = 0; i < num; ++i) {
		struct folio *folio = alloc_pool_huge_folio(h, &node_states[N_MEMORY],
						&node_alloc_noretry, &next_node);
		if (!folio)
			break;

		list_move(&folio->lru, &folio_list);
		cond_resched();
	}

	prep_and_add_allocated_folios(h, &folio_list);
}

static unsigned long __init hugetlb_gigantic_pages_alloc_boot(struct hstate *h)
{
	unsigned long i;

	for (i = 0; i < h->max_huge_pages; ++i) {
		if (!alloc_bootmem_huge_page(h, NUMA_NO_NODE))
			break;
		cond_resched();
	}

	return i;
}

static unsigned long __init hugetlb_pages_alloc_boot(struct hstate *h)
{
	struct padata_mt_job job = {
		.fn_arg		= h,
		.align		= 1,
		.numa_aware	= true
	};

	job.thread_fn	= hugetlb_pages_alloc_boot_node;
	job.start	= 0;
	job.size	= h->max_huge_pages;

	/*
	 * job.max_threads is twice the num_node_state(N_MEMORY),
	 *
	 * Tests below indicate that a multiplier of 2 significantly improves
	 * performance, and although larger values also provide improvements,
	 * the gains are marginal.
	 *
	 * Therefore, choosing 2 as the multiplier strikes a good balance between
	 * enhancing parallel processing capabilities and maintaining efficient
	 * resource management.
	 *
	 * +------------+-------+-------+-------+-------+-------+
	 * | multiplier |   1   |   2   |   3   |   4   |   5   |
	 * +------------+-------+-------+-------+-------+-------+
	 * | 256G 2node | 358ms | 215ms | 157ms | 134ms | 126ms |
	 * | 2T   4node | 979ms | 679ms | 543ms | 489ms | 481ms |
	 * | 50G  2node | 71ms  | 44ms  | 37ms  | 30ms  | 31ms  |
	 * +------------+-------+-------+-------+-------+-------+
	 */
	job.max_threads	= num_node_state(N_MEMORY) * 2;
	job.min_chunk	= h->max_huge_pages / num_node_state(N_MEMORY) / 2;
	padata_do_multithreaded(&job);

	return h->nr_huge_pages;
}

/*
 * NOTE: this routine is called in different contexts for gigantic and
 * non-gigantic pages.
 * - For gigantic pages, this is called early in the boot process and
 *   pages are allocated from memblock allocated or something similar.
 *   Gigantic pages are actually added to pools later with the routine
 *   gather_bootmem_prealloc.
 * - For non-gigantic pages, this is called later in the boot process after
 *   all of mm is up and functional.  Pages are allocated from buddy and
 *   then added to hugetlb pools.
 */
static void __init hugetlb_hstate_alloc_pages(struct hstate *h)
{
	unsigned long allocated;

	/* skip gigantic hugepages allocation if hugetlb_cma enabled */
	if (hstate_is_gigantic(h) && hugetlb_cma_size) {
		pr_warn_once("HugeTLB: hugetlb_cma is enabled, skip boot time allocation\n");
		return;
	}

	/* do node specific alloc */
	if (hugetlb_hstate_alloc_pages_specific_nodes(h))
		return;

	/* below will do all node balanced alloc */
	if (hstate_is_gigantic(h))
		allocated = hugetlb_gigantic_pages_alloc_boot(h);
	else
		allocated = hugetlb_pages_alloc_boot(h);

	hugetlb_hstate_alloc_pages_errcheck(allocated, h);
}

static void __init hugetlb_init_hstates(void)
{
	struct hstate *h, *h2;

	for_each_hstate(h) {
		/* oversize hugepages were init'ed in early boot */
		if (!hstate_is_gigantic(h))
			hugetlb_hstate_alloc_pages(h);

		/*
		 * Set demote order for each hstate.  Note that
		 * h->demote_order is initially 0.
		 * - We can not demote gigantic pages if runtime freeing
		 *   is not supported, so skip this.
		 * - If CMA allocation is possible, we can not demote
		 *   HUGETLB_PAGE_ORDER or smaller size pages.
		 */
		if (hstate_is_gigantic(h) && !gigantic_page_runtime_supported())
			continue;
		if (hugetlb_cma_size && h->order <= HUGETLB_PAGE_ORDER)
			continue;
		for_each_hstate(h2) {
			if (h2 == h)
				continue;
			if (h2->order < h->order &&
			    h2->order > h->demote_order)
				h->demote_order = h2->order;
		}
	}
}

static void __init report_hugepages(void)
{
	struct hstate *h;
	unsigned long nrinvalid;

	for_each_hstate(h) {
		char buf[32];

		nrinvalid = hstate_boot_nrinvalid[hstate_index(h)];
		h->max_huge_pages -= nrinvalid;

		string_get_size(huge_page_size(h), 1, STRING_UNITS_2, buf, 32);
		pr_info("HugeTLB: registered %s page size, pre-allocated %ld pages\n",
			buf, h->free_huge_pages);
		if (nrinvalid)
			pr_info("HugeTLB: %s page size: %lu invalid page%s discarded\n",
					buf, nrinvalid, nrinvalid > 1 ? "s" : "");
		pr_info("HugeTLB: %d KiB vmemmap can be freed for a %s page\n",
			hugetlb_vmemmap_optimizable_size(h) / SZ_1K, buf);
	}
}

#ifdef CONFIG_HIGHMEM
static void try_to_free_low(struct hstate *h, unsigned long count,
						nodemask_t *nodes_allowed)
{
	int i;
	LIST_HEAD(page_list);

	lockdep_assert_held(&hugetlb_lock);
	if (hstate_is_gigantic(h))
		return;

	/*
	 * Collect pages to be freed on a list, and free after dropping lock
	 */
	for_each_node_mask(i, *nodes_allowed) {
		struct folio *folio, *next;
		struct list_head *freel = &h->hugepage_freelists[i];
		list_for_each_entry_safe(folio, next, freel, lru) {
			if (count >= h->nr_huge_pages)
				goto out;
			if (folio_test_highmem(folio))
				continue;
			remove_hugetlb_folio(h, folio, false);
			list_add(&folio->lru, &page_list);
		}
	}

out:
	spin_unlock_irq(&hugetlb_lock);
	update_and_free_pages_bulk(h, &page_list);
	spin_lock_irq(&hugetlb_lock);
}
#else
static inline void try_to_free_low(struct hstate *h, unsigned long count,
						nodemask_t *nodes_allowed)
{
}
#endif

/*
 * Increment or decrement surplus_huge_pages.  Keep node-specific counters
 * balanced by operating on them in a round-robin fashion.
 * Returns 1 if an adjustment was made.
 */
static int adjust_pool_surplus(struct hstate *h, nodemask_t *nodes_allowed,
				int delta)
{
	int nr_nodes, node;

	lockdep_assert_held(&hugetlb_lock);
	VM_BUG_ON(delta != -1 && delta != 1);

	if (delta < 0) {
		for_each_node_mask_to_alloc(&h->next_nid_to_alloc, nr_nodes, node, nodes_allowed) {
			if (h->surplus_huge_pages_node[node])
				goto found;
		}
	} else {
		for_each_node_mask_to_free(h, nr_nodes, node, nodes_allowed) {
			if (h->surplus_huge_pages_node[node] <
					h->nr_huge_pages_node[node])
				goto found;
		}
	}
	return 0;

found:
	h->surplus_huge_pages += delta;
	h->surplus_huge_pages_node[node] += delta;
	return 1;
}

#define persistent_huge_pages(h) (h->nr_huge_pages - h->surplus_huge_pages)
static int set_max_huge_pages(struct hstate *h, unsigned long count, int nid,
			      nodemask_t *nodes_allowed)
{
	unsigned long min_count;
	unsigned long allocated;
	struct folio *folio;
	LIST_HEAD(page_list);
	NODEMASK_ALLOC(nodemask_t, node_alloc_noretry, GFP_KERNEL);

	/*
	 * Bit mask controlling how hard we retry per-node allocations.
	 * If we can not allocate the bit mask, do not attempt to allocate
	 * the requested huge pages.
	 */
	if (node_alloc_noretry)
		nodes_clear(*node_alloc_noretry);
	else
		return -ENOMEM;

	/*
	 * resize_lock mutex prevents concurrent adjustments to number of
	 * pages in hstate via the proc/sysfs interfaces.
	 */
	mutex_lock(&h->resize_lock);
	flush_free_hpage_work(h);
	spin_lock_irq(&hugetlb_lock);

	/*
	 * Check for a node specific request.
	 * Changing node specific huge page count may require a corresponding
	 * change to the global count.  In any case, the passed node mask
	 * (nodes_allowed) will restrict alloc/free to the specified node.
	 */
	if (nid != NUMA_NO_NODE) {
		unsigned long old_count = count;

		count += persistent_huge_pages(h) -
			 (h->nr_huge_pages_node[nid] -
			  h->surplus_huge_pages_node[nid]);
		/*
		 * User may have specified a large count value which caused the
		 * above calculation to overflow.  In this case, they wanted
		 * to allocate as many huge pages as possible.  Set count to
		 * largest possible value to align with their intention.
		 */
		if (count < old_count)
			count = ULONG_MAX;
	}

	/*
	 * Gigantic pages runtime allocation depend on the capability for large
	 * page range allocation.
	 * If the system does not provide this feature, return an error when
	 * the user tries to allocate gigantic pages but let the user free the
	 * boottime allocated gigantic pages.
	 */
	if (hstate_is_gigantic(h) && !IS_ENABLED(CONFIG_CONTIG_ALLOC)) {
		if (count > persistent_huge_pages(h)) {
			spin_unlock_irq(&hugetlb_lock);
			mutex_unlock(&h->resize_lock);
			NODEMASK_FREE(node_alloc_noretry);
			return -EINVAL;
		}
		/* Fall through to decrease pool */
	}

	/*
	 * Increase the pool size
	 * First take pages out of surplus state.  Then make up the
	 * remaining difference by allocating fresh huge pages.
	 *
	 * We might race with alloc_surplus_hugetlb_folio() here and be unable
	 * to convert a surplus huge page to a normal huge page. That is
	 * not critical, though, it just means the overall size of the
	 * pool might be one hugepage larger than it needs to be, but
	 * within all the constraints specified by the sysctls.
	 */
	while (h->surplus_huge_pages && count > persistent_huge_pages(h)) {
		if (!adjust_pool_surplus(h, nodes_allowed, -1))
			break;
	}

	allocated = 0;
	while (count > (persistent_huge_pages(h) + allocated)) {
		/*
		 * If this allocation races such that we no longer need the
		 * page, free_huge_folio will handle it by freeing the page
		 * and reducing the surplus.
		 */
		spin_unlock_irq(&hugetlb_lock);

		/* yield cpu to avoid soft lockup */
		cond_resched();

		folio = alloc_pool_huge_folio(h, nodes_allowed,
						node_alloc_noretry,
						&h->next_nid_to_alloc);
		if (!folio) {
			prep_and_add_allocated_folios(h, &page_list);
			spin_lock_irq(&hugetlb_lock);
			goto out;
		}

		list_add(&folio->lru, &page_list);
		allocated++;

		/* Bail for signals. Probably ctrl-c from user */
		if (signal_pending(current)) {
			prep_and_add_allocated_folios(h, &page_list);
			spin_lock_irq(&hugetlb_lock);
			goto out;
		}

		spin_lock_irq(&hugetlb_lock);
	}

	/* Add allocated pages to the pool */
	if (!list_empty(&page_list)) {
		spin_unlock_irq(&hugetlb_lock);
		prep_and_add_allocated_folios(h, &page_list);
		spin_lock_irq(&hugetlb_lock);
	}

	/*
	 * Decrease the pool size
	 * First return free pages to the buddy allocator (being careful
	 * to keep enough around to satisfy reservations).  Then place
	 * pages into surplus state as needed so the pool will shrink
	 * to the desired size as pages become free.
	 *
	 * By placing pages into the surplus state independent of the
	 * overcommit value, we are allowing the surplus pool size to
	 * exceed overcommit. There are few sane options here. Since
	 * alloc_surplus_hugetlb_folio() is checking the global counter,
	 * though, we'll note that we're not allowed to exceed surplus
	 * and won't grow the pool anywhere else. Not until one of the
	 * sysctls are changed, or the surplus pages go out of use.
	 */
	min_count = h->resv_huge_pages + h->nr_huge_pages - h->free_huge_pages;
	min_count = max(count, min_count);
	try_to_free_low(h, min_count, nodes_allowed);

	/*
	 * Collect pages to be removed on list without dropping lock
	 */
	while (min_count < persistent_huge_pages(h)) {
		folio = remove_pool_hugetlb_folio(h, nodes_allowed, 0);
		if (!folio)
			break;

		list_add(&folio->lru, &page_list);
	}
	/* free the pages after dropping lock */
	spin_unlock_irq(&hugetlb_lock);
	update_and_free_pages_bulk(h, &page_list);
	flush_free_hpage_work(h);
	spin_lock_irq(&hugetlb_lock);

	while (count < persistent_huge_pages(h)) {
		if (!adjust_pool_surplus(h, nodes_allowed, 1))
			break;
	}
out:
	h->max_huge_pages = persistent_huge_pages(h);
	spin_unlock_irq(&hugetlb_lock);
	mutex_unlock(&h->resize_lock);

	NODEMASK_FREE(node_alloc_noretry);

	return 0;
}

static long demote_free_hugetlb_folios(struct hstate *src, struct hstate *dst,
				       struct list_head *src_list)
{
	long rc;
	struct folio *folio, *next;
	LIST_HEAD(dst_list);
	LIST_HEAD(ret_list);

	rc = hugetlb_vmemmap_restore_folios(src, src_list, &ret_list);
	list_splice_init(&ret_list, src_list);

	/*
	 * Taking target hstate mutex synchronizes with set_max_huge_pages.
	 * Without the mutex, pages added to target hstate could be marked
	 * as surplus.
	 *
	 * Note that we already hold src->resize_lock.  To prevent deadlock,
	 * use the convention of always taking larger size hstate mutex first.
	 */
	mutex_lock(&dst->resize_lock);

	list_for_each_entry_safe(folio, next, src_list, lru) {
		int i;

		if (folio_test_hugetlb_vmemmap_optimized(folio))
			continue;

		list_del(&folio->lru);

		split_page_owner(&folio->page, huge_page_order(src), huge_page_order(dst));
		pgalloc_tag_split(folio, huge_page_order(src), huge_page_order(dst));

		for (i = 0; i < pages_per_huge_page(src); i += pages_per_huge_page(dst)) {
			struct page *page = folio_page(folio, i);
			/* Careful: see __split_huge_page_tail() */
			struct folio *new_folio = (struct folio *)page;

			clear_compound_head(page);
			prep_compound_page(page, dst->order);

			new_folio->mapping = NULL;
			init_new_hugetlb_folio(dst, new_folio);
			list_add(&new_folio->lru, &dst_list);
		}
	}

	prep_and_add_allocated_folios(dst, &dst_list);

	mutex_unlock(&dst->resize_lock);

	return rc;
}

static long demote_pool_huge_page(struct hstate *src, nodemask_t *nodes_allowed,
				  unsigned long nr_to_demote)
	__must_hold(&hugetlb_lock)
{
	int nr_nodes, node;
	struct hstate *dst;
	long rc = 0;
	long nr_demoted = 0;

	lockdep_assert_held(&hugetlb_lock);

	/* We should never get here if no demote order */
	if (!src->demote_order) {
		pr_warn("HugeTLB: NULL demote order passed to demote_pool_huge_page.\n");
		return -EINVAL;		/* internal error */
	}
	dst = size_to_hstate(PAGE_SIZE << src->demote_order);

	for_each_node_mask_to_free(src, nr_nodes, node, nodes_allowed) {
		LIST_HEAD(list);
		struct folio *folio, *next;

		list_for_each_entry_safe(folio, next, &src->hugepage_freelists[node], lru) {
			if (folio_test_hwpoison(folio))
				continue;

			remove_hugetlb_folio(src, folio, false);
			list_add(&folio->lru, &list);

			if (++nr_demoted == nr_to_demote)
				break;
		}

		spin_unlock_irq(&hugetlb_lock);

		rc = demote_free_hugetlb_folios(src, dst, &list);

		spin_lock_irq(&hugetlb_lock);

		list_for_each_entry_safe(folio, next, &list, lru) {
			list_del(&folio->lru);
			add_hugetlb_folio(src, folio, false);

			nr_demoted--;
		}

		if (rc < 0 || nr_demoted == nr_to_demote)
			break;
	}

	/*
	 * Not absolutely necessary, but for consistency update max_huge_pages
	 * based on pool changes for the demoted page.
	 */
	src->max_huge_pages -= nr_demoted;
	dst->max_huge_pages += nr_demoted << (huge_page_order(src) - huge_page_order(dst));

	if (rc < 0)
		return rc;

	if (nr_demoted)
		return nr_demoted;
	/*
	 * Only way to get here is if all pages on free lists are poisoned.
	 * Return -EBUSY so that caller will not retry.
	 */
	return -EBUSY;
}

#define HSTATE_ATTR_RO(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RO(_name)

#define HSTATE_ATTR_WO(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_WO(_name)

#define HSTATE_ATTR(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RW(_name)

static struct kobject *hugepages_kobj;
static struct kobject *hstate_kobjs[HUGE_MAX_HSTATE];

static struct hstate *kobj_to_node_hstate(struct kobject *kobj, int *nidp);

static struct hstate *kobj_to_hstate(struct kobject *kobj, int *nidp)
{
	int i;

	for (i = 0; i < HUGE_MAX_HSTATE; i++)
		if (hstate_kobjs[i] == kobj) {
			if (nidp)
				*nidp = NUMA_NO_NODE;
			return &hstates[i];
		}

	return kobj_to_node_hstate(kobj, nidp);
}

static ssize_t nr_hugepages_show_common(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct hstate *h;
	unsigned long nr_huge_pages;
	int nid;

	h = kobj_to_hstate(kobj, &nid);
	if (nid == NUMA_NO_NODE)
		nr_huge_pages = h->nr_huge_pages;
	else
		nr_huge_pages = h->nr_huge_pages_node[nid];

	return sysfs_emit(buf, "%lu\n", nr_huge_pages);
}

static ssize_t __nr_hugepages_store_common(bool obey_mempolicy,
					   struct hstate *h, int nid,
					   unsigned long count, size_t len)
{
	int err;
	nodemask_t nodes_allowed, *n_mask;

	if (hstate_is_gigantic(h) && !gigantic_page_runtime_supported())
		return -EINVAL;

	if (nid == NUMA_NO_NODE) {
		/*
		 * global hstate attribute
		 */
		if (!(obey_mempolicy &&
				init_nodemask_of_mempolicy(&nodes_allowed)))
			n_mask = &node_states[N_MEMORY];
		else
			n_mask = &nodes_allowed;
	} else {
		/*
		 * Node specific request.  count adjustment happens in
		 * set_max_huge_pages() after acquiring hugetlb_lock.
		 */
		init_nodemask_of_node(&nodes_allowed, nid);
		n_mask = &nodes_allowed;
	}

	err = set_max_huge_pages(h, count, nid, n_mask);

	return err ? err : len;
}

static ssize_t nr_hugepages_store_common(bool obey_mempolicy,
					 struct kobject *kobj, const char *buf,
					 size_t len)
{
	struct hstate *h;
	unsigned long count;
	int nid;
	int err;

	err = kstrtoul(buf, 10, &count);
	if (err)
		return err;

	h = kobj_to_hstate(kobj, &nid);
	return __nr_hugepages_store_common(obey_mempolicy, h, nid, count, len);
}

static ssize_t nr_hugepages_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	return nr_hugepages_show_common(kobj, attr, buf);
}

static ssize_t nr_hugepages_store(struct kobject *kobj,
	       struct kobj_attribute *attr, const char *buf, size_t len)
{
	return nr_hugepages_store_common(false, kobj, buf, len);
}
HSTATE_ATTR(nr_hugepages);

#ifdef CONFIG_NUMA

/*
 * hstate attribute for optionally mempolicy-based constraint on persistent
 * huge page alloc/free.
 */
static ssize_t nr_hugepages_mempolicy_show(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   char *buf)
{
	return nr_hugepages_show_common(kobj, attr, buf);
}

static ssize_t nr_hugepages_mempolicy_store(struct kobject *kobj,
	       struct kobj_attribute *attr, const char *buf, size_t len)
{
	return nr_hugepages_store_common(true, kobj, buf, len);
}
HSTATE_ATTR(nr_hugepages_mempolicy);
#endif


static ssize_t nr_overcommit_hugepages_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct hstate *h = kobj_to_hstate(kobj, NULL);
	return sysfs_emit(buf, "%lu\n", h->nr_overcommit_huge_pages);
}

static ssize_t nr_overcommit_hugepages_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long input;
	struct hstate *h = kobj_to_hstate(kobj, NULL);

	if (hstate_is_gigantic(h))
		return -EINVAL;

	err = kstrtoul(buf, 10, &input);
	if (err)
		return err;

	spin_lock_irq(&hugetlb_lock);
	h->nr_overcommit_huge_pages = input;
	spin_unlock_irq(&hugetlb_lock);

	return count;
}
HSTATE_ATTR(nr_overcommit_hugepages);

static ssize_t free_hugepages_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct hstate *h;
	unsigned long free_huge_pages;
	int nid;

	h = kobj_to_hstate(kobj, &nid);
	if (nid == NUMA_NO_NODE)
		free_huge_pages = h->free_huge_pages;
	else
		free_huge_pages = h->free_huge_pages_node[nid];

	return sysfs_emit(buf, "%lu\n", free_huge_pages);
}
HSTATE_ATTR_RO(free_hugepages);

static ssize_t resv_hugepages_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct hstate *h = kobj_to_hstate(kobj, NULL);
	return sysfs_emit(buf, "%lu\n", h->resv_huge_pages);
}
HSTATE_ATTR_RO(resv_hugepages);

static ssize_t surplus_hugepages_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct hstate *h;
	unsigned long surplus_huge_pages;
	int nid;

	h = kobj_to_hstate(kobj, &nid);
	if (nid == NUMA_NO_NODE)
		surplus_huge_pages = h->surplus_huge_pages;
	else
		surplus_huge_pages = h->surplus_huge_pages_node[nid];

	return sysfs_emit(buf, "%lu\n", surplus_huge_pages);
}
HSTATE_ATTR_RO(surplus_hugepages);

static ssize_t demote_store(struct kobject *kobj,
	       struct kobj_attribute *attr, const char *buf, size_t len)
{
	unsigned long nr_demote;
	unsigned long nr_available;
	nodemask_t nodes_allowed, *n_mask;
	struct hstate *h;
	int err;
	int nid;

	err = kstrtoul(buf, 10, &nr_demote);
	if (err)
		return err;
	h = kobj_to_hstate(kobj, &nid);

	if (nid != NUMA_NO_NODE) {
		init_nodemask_of_node(&nodes_allowed, nid);
		n_mask = &nodes_allowed;
	} else {
		n_mask = &node_states[N_MEMORY];
	}

	/* Synchronize with other sysfs operations modifying huge pages */
	mutex_lock(&h->resize_lock);
	spin_lock_irq(&hugetlb_lock);

	while (nr_demote) {
		long rc;

		/*
		 * Check for available pages to demote each time thorough the
		 * loop as demote_pool_huge_page will drop hugetlb_lock.
		 */
		if (nid != NUMA_NO_NODE)
			nr_available = h->free_huge_pages_node[nid];
		else
			nr_available = h->free_huge_pages;
		nr_available -= h->resv_huge_pages;
		if (!nr_available)
			break;

		rc = demote_pool_huge_page(h, n_mask, nr_demote);
		if (rc < 0) {
			err = rc;
			break;
		}

		nr_demote -= rc;
	}

	spin_unlock_irq(&hugetlb_lock);
	mutex_unlock(&h->resize_lock);

	if (err)
		return err;
	return len;
}
HSTATE_ATTR_WO(demote);

static ssize_t demote_size_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct hstate *h = kobj_to_hstate(kobj, NULL);
	unsigned long demote_size = (PAGE_SIZE << h->demote_order) / SZ_1K;

	return sysfs_emit(buf, "%lukB\n", demote_size);
}

static ssize_t demote_size_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	struct hstate *h, *demote_hstate;
	unsigned long demote_size;
	unsigned int demote_order;

	demote_size = (unsigned long)memparse(buf, NULL);

	demote_hstate = size_to_hstate(demote_size);
	if (!demote_hstate)
		return -EINVAL;
	demote_order = demote_hstate->order;
	if (demote_order < HUGETLB_PAGE_ORDER)
		return -EINVAL;

	/* demote order must be smaller than hstate order */
	h = kobj_to_hstate(kobj, NULL);
	if (demote_order >= h->order)
		return -EINVAL;

	/* resize_lock synchronizes access to demote size and writes */
	mutex_lock(&h->resize_lock);
	h->demote_order = demote_order;
	mutex_unlock(&h->resize_lock);

	return count;
}
HSTATE_ATTR(demote_size);

static struct attribute *hstate_attrs[] = {
	&nr_hugepages_attr.attr,
	&nr_overcommit_hugepages_attr.attr,
	&free_hugepages_attr.attr,
	&resv_hugepages_attr.attr,
	&surplus_hugepages_attr.attr,
#ifdef CONFIG_NUMA
	&nr_hugepages_mempolicy_attr.attr,
#endif
	NULL,
};

static const struct attribute_group hstate_attr_group = {
	.attrs = hstate_attrs,
};

static struct attribute *hstate_demote_attrs[] = {
	&demote_size_attr.attr,
	&demote_attr.attr,
	NULL,
};

static const struct attribute_group hstate_demote_attr_group = {
	.attrs = hstate_demote_attrs,
};

static int hugetlb_sysfs_add_hstate(struct hstate *h, struct kobject *parent,
				    struct kobject **hstate_kobjs,
				    const struct attribute_group *hstate_attr_group)
{
	int retval;
	int hi = hstate_index(h);

	hstate_kobjs[hi] = kobject_create_and_add(h->name, parent);
	if (!hstate_kobjs[hi])
		return -ENOMEM;

	retval = sysfs_create_group(hstate_kobjs[hi], hstate_attr_group);
	if (retval) {
		kobject_put(hstate_kobjs[hi]);
		hstate_kobjs[hi] = NULL;
		return retval;
	}

	if (h->demote_order) {
		retval = sysfs_create_group(hstate_kobjs[hi],
					    &hstate_demote_attr_group);
		if (retval) {
			pr_warn("HugeTLB unable to create demote interfaces for %s\n", h->name);
			sysfs_remove_group(hstate_kobjs[hi], hstate_attr_group);
			kobject_put(hstate_kobjs[hi]);
			hstate_kobjs[hi] = NULL;
			return retval;
		}
	}

	return 0;
}

#ifdef CONFIG_NUMA
static bool hugetlb_sysfs_initialized __ro_after_init;

/*
 * node_hstate/s - associate per node hstate attributes, via their kobjects,
 * with node devices in node_devices[] using a parallel array.  The array
 * index of a node device or _hstate == node id.
 * This is here to avoid any static dependency of the node device driver, in
 * the base kernel, on the hugetlb module.
 */
struct node_hstate {
	struct kobject		*hugepages_kobj;
	struct kobject		*hstate_kobjs[HUGE_MAX_HSTATE];
};
static struct node_hstate node_hstates[MAX_NUMNODES];

/*
 * A subset of global hstate attributes for node devices
 */
static struct attribute *per_node_hstate_attrs[] = {
	&nr_hugepages_attr.attr,
	&free_hugepages_attr.attr,
	&surplus_hugepages_attr.attr,
	NULL,
};

static const struct attribute_group per_node_hstate_attr_group = {
	.attrs = per_node_hstate_attrs,
};

/*
 * kobj_to_node_hstate - lookup global hstate for node device hstate attr kobj.
 * Returns node id via non-NULL nidp.
 */
static struct hstate *kobj_to_node_hstate(struct kobject *kobj, int *nidp)
{
	int nid;

	for (nid = 0; nid < nr_node_ids; nid++) {
		struct node_hstate *nhs = &node_hstates[nid];
		int i;
		for (i = 0; i < HUGE_MAX_HSTATE; i++)
			if (nhs->hstate_kobjs[i] == kobj) {
				if (nidp)
					*nidp = nid;
				return &hstates[i];
			}
	}

	BUG();
	return NULL;
}

/*
 * Unregister hstate attributes from a single node device.
 * No-op if no hstate attributes attached.
 */
void hugetlb_unregister_node(struct node *node)
{
	struct hstate *h;
	struct node_hstate *nhs = &node_hstates[node->dev.id];

	if (!nhs->hugepages_kobj)
		return;		/* no hstate attributes */

	for_each_hstate(h) {
		int idx = hstate_index(h);
		struct kobject *hstate_kobj = nhs->hstate_kobjs[idx];

		if (!hstate_kobj)
			continue;
		if (h->demote_order)
			sysfs_remove_group(hstate_kobj, &hstate_demote_attr_group);
		sysfs_remove_group(hstate_kobj, &per_node_hstate_attr_group);
		kobject_put(hstate_kobj);
		nhs->hstate_kobjs[idx] = NULL;
	}

	kobject_put(nhs->hugepages_kobj);
	nhs->hugepages_kobj = NULL;
}


/*
 * Register hstate attributes for a single node device.
 * No-op if attributes already registered.
 */
void hugetlb_register_node(struct node *node)
{
	struct hstate *h;
	struct node_hstate *nhs = &node_hstates[node->dev.id];
	int err;

	if (!hugetlb_sysfs_initialized)
		return;

	if (nhs->hugepages_kobj)
		return;		/* already allocated */

	nhs->hugepages_kobj = kobject_create_and_add("hugepages",
							&node->dev.kobj);
	if (!nhs->hugepages_kobj)
		return;

	for_each_hstate(h) {
		err = hugetlb_sysfs_add_hstate(h, nhs->hugepages_kobj,
						nhs->hstate_kobjs,
						&per_node_hstate_attr_group);
		if (err) {
			pr_err("HugeTLB: Unable to add hstate %s for node %d\n",
				h->name, node->dev.id);
			hugetlb_unregister_node(node);
			break;
		}
	}
}

/*
 * hugetlb init time:  register hstate attributes for all registered node
 * devices of nodes that have memory.  All on-line nodes should have
 * registered their associated device by this time.
 */
static void __init hugetlb_register_all_nodes(void)
{
	int nid;

	for_each_online_node(nid)
		hugetlb_register_node(node_devices[nid]);
}
#else	/* !CONFIG_NUMA */

static struct hstate *kobj_to_node_hstate(struct kobject *kobj, int *nidp)
{
	BUG();
	if (nidp)
		*nidp = -1;
	return NULL;
}

static void hugetlb_register_all_nodes(void) { }

#endif

#ifdef CONFIG_CMA
static void __init hugetlb_cma_check(void);
#else
static inline __init void hugetlb_cma_check(void)
{
}
#endif

static void __init hugetlb_sysfs_init(void)
{
	struct hstate *h;
	int err;

	hugepages_kobj = kobject_create_and_add("hugepages", mm_kobj);
	if (!hugepages_kobj)
		return;

	for_each_hstate(h) {
		err = hugetlb_sysfs_add_hstate(h, hugepages_kobj,
					 hstate_kobjs, &hstate_attr_group);
		if (err)
			pr_err("HugeTLB: Unable to add hstate %s", h->name);
	}

#ifdef CONFIG_NUMA
	hugetlb_sysfs_initialized = true;
#endif
	hugetlb_register_all_nodes();
}

#ifdef CONFIG_SYSCTL
static void hugetlb_sysctl_init(void);
#else
static inline void hugetlb_sysctl_init(void) { }
#endif

static int __init hugetlb_init(void)
{
	int i;

	BUILD_BUG_ON(sizeof_field(struct page, private) * BITS_PER_BYTE <
			__NR_HPAGEFLAGS);

	if (!hugepages_supported()) {
		if (hugetlb_max_hstate || default_hstate_max_huge_pages)
			pr_warn("HugeTLB: huge pages not supported, ignoring associated command-line parameters\n");
		return 0;
	}

	/*
	 * Make sure HPAGE_SIZE (HUGETLB_PAGE_ORDER) hstate exists.  Some
	 * architectures depend on setup being done here.
	 */
	hugetlb_add_hstate(HUGETLB_PAGE_ORDER);
	if (!parsed_default_hugepagesz) {
		/*
		 * If we did not parse a default huge page size, set
		 * default_hstate_idx to HPAGE_SIZE hstate. And, if the
		 * number of huge pages for this default size was implicitly
		 * specified, set that here as well.
		 * Note that the implicit setting will overwrite an explicit
		 * setting.  A warning will be printed in this case.
		 */
		default_hstate_idx = hstate_index(size_to_hstate(HPAGE_SIZE));
		if (default_hstate_max_huge_pages) {
			if (default_hstate.max_huge_pages) {
				char buf[32];

				string_get_size(huge_page_size(&default_hstate),
					1, STRING_UNITS_2, buf, 32);
				pr_warn("HugeTLB: Ignoring hugepages=%lu associated with %s page size\n",
					default_hstate.max_huge_pages, buf);
				pr_warn("HugeTLB: Using hugepages=%lu for number of default huge pages\n",
					default_hstate_max_huge_pages);
			}
			default_hstate.max_huge_pages =
				default_hstate_max_huge_pages;

			for_each_online_node(i)
				default_hstate.max_huge_pages_node[i] =
					default_hugepages_in_node[i];
		}
	}

	hugetlb_cma_check();
	hugetlb_init_hstates();
	gather_bootmem_prealloc();
	report_hugepages();

	hugetlb_sysfs_init();
	hugetlb_cgroup_file_init();
	hugetlb_sysctl_init();

#ifdef CONFIG_SMP
	num_fault_mutexes = roundup_pow_of_two(8 * num_possible_cpus());
#else
	num_fault_mutexes = 1;
#endif
	hugetlb_fault_mutex_table =
		kmalloc_array(num_fault_mutexes, sizeof(struct mutex),
			      GFP_KERNEL);
	BUG_ON(!hugetlb_fault_mutex_table);

	for (i = 0; i < num_fault_mutexes; i++)
		mutex_init(&hugetlb_fault_mutex_table[i]);
	return 0;
}
subsys_initcall(hugetlb_init);

/* Overwritten by architectures with more huge page sizes */
bool __init __attribute((weak)) arch_hugetlb_valid_size(unsigned long size)
{
	return size == HPAGE_SIZE;
}

void __init hugetlb_add_hstate(unsigned int order)
{
	struct hstate *h;
	unsigned long i;

	if (size_to_hstate(PAGE_SIZE << order)) {
		return;
	}
	BUG_ON(hugetlb_max_hstate >= HUGE_MAX_HSTATE);
	BUG_ON(order < order_base_2(__NR_USED_SUBPAGE));
	h = &hstates[hugetlb_max_hstate++];
	__mutex_init(&h->resize_lock, "resize mutex", &h->resize_key);
	h->order = order;
	h->mask = ~(huge_page_size(h) - 1);
	for (i = 0; i < MAX_NUMNODES; ++i)
		INIT_LIST_HEAD(&h->hugepage_freelists[i]);
	INIT_LIST_HEAD(&h->hugepage_activelist);
	snprintf(h->name, HSTATE_NAME_LEN, "hugepages-%lukB",
					huge_page_size(h)/SZ_1K);

	parsed_hstate = h;
}

bool __init __weak hugetlb_node_alloc_supported(void)
{
	return true;
}

static void __init hugepages_clear_pages_in_node(void)
{
	if (!hugetlb_max_hstate) {
		default_hstate_max_huge_pages = 0;
		memset(default_hugepages_in_node, 0,
			sizeof(default_hugepages_in_node));
	} else {
		parsed_hstate->max_huge_pages = 0;
		memset(parsed_hstate->max_huge_pages_node, 0,
			sizeof(parsed_hstate->max_huge_pages_node));
	}
}

static __init int hugetlb_add_param(char *s, int (*setup)(char *))
{
	size_t len;
	char *p;

	if (hugetlb_param_index >= HUGE_MAX_CMDLINE_ARGS)
		return -EINVAL;

	len = strlen(s) + 1;
	if (len + hstate_cmdline_index > sizeof(hstate_cmdline_buf))
		return -EINVAL;

	p = &hstate_cmdline_buf[hstate_cmdline_index];
	memcpy(p, s, len);
	hstate_cmdline_index += len;

	hugetlb_params[hugetlb_param_index].val = p;
	hugetlb_params[hugetlb_param_index].setup = setup;

	hugetlb_param_index++;

	return 0;
}

static __init void hugetlb_parse_params(void)
{
	int i;
	struct hugetlb_cmdline *hcp;

	for (i = 0; i < hugetlb_param_index; i++) {
		hcp = &hugetlb_params[i];

		hcp->setup(hcp->val);
	}

	if (!hugetlb_cma_size)
		hugetlb_cma_only = false;
}

/*
 * hugepages command line processing
 * hugepages normally follows a valid hugepagsz or default_hugepagsz
 * specification.  If not, ignore the hugepages value.  hugepages can also
 * be the first huge page command line  option in which case it implicitly
 * specifies the number of huge pages for the default size.
 */
static int __init hugepages_setup(char *s)
{
	unsigned long *mhp;
	static unsigned long *last_mhp;
	int node = NUMA_NO_NODE;
	int count;
	unsigned long tmp;
	char *p = s;

	if (!parsed_valid_hugepagesz) {
		pr_warn("HugeTLB: hugepages=%s does not follow a valid hugepagesz, ignoring\n", s);
		parsed_valid_hugepagesz = true;
		return -EINVAL;
	}

	/*
	 * !hugetlb_max_hstate means we haven't parsed a hugepagesz= parameter
	 * yet, so this hugepages= parameter goes to the "default hstate".
	 * Otherwise, it goes with the previously parsed hugepagesz or
	 * default_hugepagesz.
	 */
	else if (!hugetlb_max_hstate)
		mhp = &default_hstate_max_huge_pages;
	else
		mhp = &parsed_hstate->max_huge_pages;

	if (mhp == last_mhp) {
		pr_warn("HugeTLB: hugepages= specified twice without interleaving hugepagesz=, ignoring hugepages=%s\n", s);
		return 1;
	}

	while (*p) {
		count = 0;
		if (sscanf(p, "%lu%n", &tmp, &count) != 1)
			goto invalid;
		/* Parameter is node format */
		if (p[count] == ':') {
			if (!hugetlb_node_alloc_supported()) {
				pr_warn("HugeTLB: architecture can't support node specific alloc, ignoring!\n");
				return 1;
			}
			if (tmp >= MAX_NUMNODES || !node_online(tmp))
				goto invalid;
			node = array_index_nospec(tmp, MAX_NUMNODES);
			p += count + 1;
			/* Parse hugepages */
			if (sscanf(p, "%lu%n", &tmp, &count) != 1)
				goto invalid;
			if (!hugetlb_max_hstate)
				default_hugepages_in_node[node] = tmp;
			else
				parsed_hstate->max_huge_pages_node[node] = tmp;
			*mhp += tmp;
			/* Go to parse next node*/
			if (p[count] == ',')
				p += count + 1;
			else
				break;
		} else {
			if (p != s)
				goto invalid;
			*mhp = tmp;
			break;
		}
	}

	last_mhp = mhp;

	return 0;

invalid:
	pr_warn("HugeTLB: Invalid hugepages parameter %s\n", p);
	hugepages_clear_pages_in_node();
	return -EINVAL;
}
hugetlb_early_param("hugepages", hugepages_setup);

/*
 * hugepagesz command line processing
 * A specific huge page size can only be specified once with hugepagesz.
 * hugepagesz is followed by hugepages on the command line.  The global
 * variable 'parsed_valid_hugepagesz' is used to determine if prior
 * hugepagesz argument was valid.
 */
static int __init hugepagesz_setup(char *s)
{
	unsigned long size;
	struct hstate *h;

	parsed_valid_hugepagesz = false;
	size = (unsigned long)memparse(s, NULL);

	if (!arch_hugetlb_valid_size(size)) {
		pr_err("HugeTLB: unsupported hugepagesz=%s\n", s);
		return -EINVAL;
	}

	h = size_to_hstate(size);
	if (h) {
		/*
		 * hstate for this size already exists.  This is normally
		 * an error, but is allowed if the existing hstate is the
		 * default hstate.  More specifically, it is only allowed if
		 * the number of huge pages for the default hstate was not
		 * previously specified.
		 */
		if (!parsed_default_hugepagesz ||  h != &default_hstate ||
		    default_hstate.max_huge_pages) {
			pr_warn("HugeTLB: hugepagesz=%s specified twice, ignoring\n", s);
			return -EINVAL;
		}

		/*
		 * No need to call hugetlb_add_hstate() as hstate already
		 * exists.  But, do set parsed_hstate so that a following
		 * hugepages= parameter will be applied to this hstate.
		 */
		parsed_hstate = h;
		parsed_valid_hugepagesz = true;
		return 0;
	}

	hugetlb_add_hstate(ilog2(size) - PAGE_SHIFT);
	parsed_valid_hugepagesz = true;
	return 0;
}
hugetlb_early_param("hugepagesz", hugepagesz_setup);

/*
 * default_hugepagesz command line input
 * Only one instance of default_hugepagesz allowed on command line.
 */
static int __init default_hugepagesz_setup(char *s)
{
	unsigned long size;
	int i;

	parsed_valid_hugepagesz = false;
	if (parsed_default_hugepagesz) {
		pr_err("HugeTLB: default_hugepagesz previously specified, ignoring %s\n", s);
		return -EINVAL;
	}

	size = (unsigned long)memparse(s, NULL);

	if (!arch_hugetlb_valid_size(size)) {
		pr_err("HugeTLB: unsupported default_hugepagesz=%s\n", s);
		return -EINVAL;
	}

	hugetlb_add_hstate(ilog2(size) - PAGE_SHIFT);
	parsed_valid_hugepagesz = true;
	parsed_default_hugepagesz = true;
	default_hstate_idx = hstate_index(size_to_hstate(size));

	/*
	 * The number of default huge pages (for this size) could have been
	 * specified as the first hugetlb parameter: hugepages=X.  If so,
	 * then default_hstate_max_huge_pages is set.  If the default huge
	 * page size is gigantic (> MAX_PAGE_ORDER), then the pages must be
	 * allocated here from bootmem allocator.
	 */
	if (default_hstate_max_huge_pages) {
		default_hstate.max_huge_pages = default_hstate_max_huge_pages;
		/*
		 * Since this is an early parameter, we can't check
		 * NUMA node state yet, so loop through MAX_NUMNODES.
		 */
		for (i = 0; i < MAX_NUMNODES; i++) {
			if (default_hugepages_in_node[i] != 0)
				default_hstate.max_huge_pages_node[i] =
					default_hugepages_in_node[i];
		}
		default_hstate_max_huge_pages = 0;
	}

	return 0;
}
hugetlb_early_param("default_hugepagesz", default_hugepagesz_setup);

static bool __hugetlb_bootmem_allocated __initdata;

bool __init hugetlb_bootmem_allocated(void)
{
	return __hugetlb_bootmem_allocated;
}

void __init hugetlb_bootmem_alloc(void)
{
	struct hstate *h;
	int i;

	if (__hugetlb_bootmem_allocated)
		return;

	for (i = 0; i < MAX_NUMNODES; i++)
		INIT_LIST_HEAD(&huge_boot_pages[i]);

	hugetlb_parse_params();

	for_each_hstate(h) {
		h->next_nid_to_alloc = first_online_node;
		h->next_nid_to_free = first_online_node;

		if (hstate_is_gigantic(h))
			hugetlb_hstate_alloc_pages(h);
	}

	__hugetlb_bootmem_allocated = true;
}

static unsigned int allowed_mems_nr(struct hstate *h)
{
	int node;
	unsigned int nr = 0;
	nodemask_t *mbind_nodemask;
	unsigned int *array = h->free_huge_pages_node;
	gfp_t gfp_mask = htlb_alloc_mask(h);

	mbind_nodemask = policy_mbind_nodemask(gfp_mask);
	for_each_node_mask(node, cpuset_current_mems_allowed) {
		if (!mbind_nodemask || node_isset(node, *mbind_nodemask))
			nr += array[node];
	}

	return nr;
}

#ifdef CONFIG_SYSCTL
static int proc_hugetlb_doulongvec_minmax(const struct ctl_table *table, int write,
					  void *buffer, size_t *length,
					  loff_t *ppos, unsigned long *out)
{
	struct ctl_table dup_table;

	/*
	 * In order to avoid races with __do_proc_doulongvec_minmax(), we
	 * can duplicate the @table and alter the duplicate of it.
	 */
	dup_table = *table;
	dup_table.data = out;

	return proc_doulongvec_minmax(&dup_table, write, buffer, length, ppos);
}

static int hugetlb_sysctl_handler_common(bool obey_mempolicy,
			 const struct ctl_table *table, int write,
			 void *buffer, size_t *length, loff_t *ppos)
{
	struct hstate *h = &default_hstate;
	unsigned long tmp = h->max_huge_pages;
	int ret;

	if (!hugepages_supported())
		return -EOPNOTSUPP;

	ret = proc_hugetlb_doulongvec_minmax(table, write, buffer, length, ppos,
					     &tmp);
	if (ret)
		goto out;

	if (write)
		ret = __nr_hugepages_store_common(obey_mempolicy, h,
						  NUMA_NO_NODE, tmp, *length);
out:
	return ret;
}

static int hugetlb_sysctl_handler(const struct ctl_table *table, int write,
			  void *buffer, size_t *length, loff_t *ppos)
{

	return hugetlb_sysctl_handler_common(false, table, write,
							buffer, length, ppos);
}

#ifdef CONFIG_NUMA
static int hugetlb_mempolicy_sysctl_handler(const struct ctl_table *table, int write,
			  void *buffer, size_t *length, loff_t *ppos)
{
	return hugetlb_sysctl_handler_common(true, table, write,
							buffer, length, ppos);
}
#endif /* CONFIG_NUMA */

static int hugetlb_overcommit_handler(const struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	struct hstate *h = &default_hstate;
	unsigned long tmp;
	int ret;

	if (!hugepages_supported())
		return -EOPNOTSUPP;

	tmp = h->nr_overcommit_huge_pages;

	if (write && hstate_is_gigantic(h))
		return -EINVAL;

	ret = proc_hugetlb_doulongvec_minmax(table, write, buffer, length, ppos,
					     &tmp);
	if (ret)
		goto out;

	if (write) {
		spin_lock_irq(&hugetlb_lock);
		h->nr_overcommit_huge_pages = tmp;
		spin_unlock_irq(&hugetlb_lock);
	}
out:
	return ret;
}

static const struct ctl_table hugetlb_table[] = {
	{
		.procname	= "nr_hugepages",
		.data		= NULL,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= hugetlb_sysctl_handler,
	},
#ifdef CONFIG_NUMA
	{
		.procname       = "nr_hugepages_mempolicy",
		.data           = NULL,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = &hugetlb_mempolicy_sysctl_handler,
	},
#endif
	{
		.procname	= "hugetlb_shm_group",
		.data		= &sysctl_hugetlb_shm_group,
		.maxlen		= sizeof(gid_t),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "nr_overcommit_hugepages",
		.data		= NULL,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= hugetlb_overcommit_handler,
	},
};

static void hugetlb_sysctl_init(void)
{
	register_sysctl_init("vm", hugetlb_table);
}
#endif /* CONFIG_SYSCTL */

void hugetlb_report_meminfo(struct seq_file *m)
{
	struct hstate *h;
	unsigned long total = 0;

	if (!hugepages_supported())
		return;

	for_each_hstate(h) {
		unsigned long count = h->nr_huge_pages;

		total += huge_page_size(h) * count;

		if (h == &default_hstate)
			seq_printf(m,
				   "HugePages_Total:   %5lu\n"
				   "HugePages_Free:    %5lu\n"
				   "HugePages_Rsvd:    %5lu\n"
				   "HugePages_Surp:    %5lu\n"
				   "Hugepagesize:   %8lu kB\n",
				   count,
				   h->free_huge_pages,
				   h->resv_huge_pages,
				   h->surplus_huge_pages,
				   huge_page_size(h) / SZ_1K);
	}

	seq_printf(m, "Hugetlb:        %8lu kB\n", total / SZ_1K);
}

int hugetlb_report_node_meminfo(char *buf, int len, int nid)
{
	struct hstate *h = &default_hstate;

	if (!hugepages_supported())
		return 0;

	return sysfs_emit_at(buf, len,
			     "Node %d HugePages_Total: %5u\n"
			     "Node %d HugePages_Free:  %5u\n"
			     "Node %d HugePages_Surp:  %5u\n",
			     nid, h->nr_huge_pages_node[nid],
			     nid, h->free_huge_pages_node[nid],
			     nid, h->surplus_huge_pages_node[nid]);
}

void hugetlb_show_meminfo_node(int nid)
{
	struct hstate *h;

	if (!hugepages_supported())
		return;

	for_each_hstate(h)
		printk("Node %d hugepages_total=%u hugepages_free=%u hugepages_surp=%u hugepages_size=%lukB\n",
			nid,
			h->nr_huge_pages_node[nid],
			h->free_huge_pages_node[nid],
			h->surplus_huge_pages_node[nid],
			huge_page_size(h) / SZ_1K);
}

void hugetlb_report_usage(struct seq_file *m, struct mm_struct *mm)
{
	seq_printf(m, "HugetlbPages:\t%8lu kB\n",
		   K(atomic_long_read(&mm->hugetlb_usage)));
}

/* Return the number pages of memory we physically have, in PAGE_SIZE units. */
unsigned long hugetlb_total_pages(void)
{
	struct hstate *h;
	unsigned long nr_total_pages = 0;

	for_each_hstate(h)
		nr_total_pages += h->nr_huge_pages * pages_per_huge_page(h);
	return nr_total_pages;
}

static int hugetlb_acct_memory(struct hstate *h, long delta)
{
	int ret = -ENOMEM;

	if (!delta)
		return 0;

	spin_lock_irq(&hugetlb_lock);
	/*
	 * When cpuset is configured, it breaks the strict hugetlb page
	 * reservation as the accounting is done on a global variable. Such
	 * reservation is completely rubbish in the presence of cpuset because
	 * the reservation is not checked against page availability for the
	 * current cpuset. Application can still potentially OOM'ed by kernel
	 * with lack of free htlb page in cpuset that the task is in.
	 * Attempt to enforce strict accounting with cpuset is almost
	 * impossible (or too ugly) because cpuset is too fluid that
	 * task or memory node can be dynamically moved between cpusets.
	 *
	 * The change of semantics for shared hugetlb mapping with cpuset is
	 * undesirable. However, in order to preserve some of the semantics,
	 * we fall back to check against current free page availability as
	 * a best attempt and hopefully to minimize the impact of changing
	 * semantics that cpuset has.
	 *
	 * Apart from cpuset, we also have memory policy mechanism that
	 * also determines from which node the kernel will allocate memory
	 * in a NUMA system. So similar to cpuset, we also should consider
	 * the memory policy of the current task. Similar to the description
	 * above.
	 */
	if (delta > 0) {
		if (gather_surplus_pages(h, delta) < 0)
			goto out;

		if (delta > allowed_mems_nr(h)) {
			return_unused_surplus_pages(h, delta);
			goto out;
		}
	}

	ret = 0;
	if (delta < 0)
		return_unused_surplus_pages(h, (unsigned long) -delta);

out:
	spin_unlock_irq(&hugetlb_lock);
	return ret;
}

static void hugetlb_vm_op_open(struct vm_area_struct *vma)
{
	struct resv_map *resv = vma_resv_map(vma);

	/*
	 * HPAGE_RESV_OWNER indicates a private mapping.
	 * This new VMA should share its siblings reservation map if present.
	 * The VMA will only ever have a valid reservation map pointer where
	 * it is being copied for another still existing VMA.  As that VMA
	 * has a reference to the reservation map it cannot disappear until
	 * after this open call completes.  It is therefore safe to take a
	 * new reference here without additional locking.
	 */
	if (resv && is_vma_resv_set(vma, HPAGE_RESV_OWNER)) {
		resv_map_dup_hugetlb_cgroup_uncharge_info(resv);
		kref_get(&resv->refs);
	}

	/*
	 * vma_lock structure for sharable mappings is vma specific.
	 * Clear old pointer (if copied via vm_area_dup) and allocate
	 * new structure.  Before clearing, make sure vma_lock is not
	 * for this vma.
	 */
	if (vma->vm_flags & VM_MAYSHARE) {
		struct hugetlb_vma_lock *vma_lock = vma->vm_private_data;

		if (vma_lock) {
			if (vma_lock->vma != vma) {
				vma->vm_private_data = NULL;
				hugetlb_vma_lock_alloc(vma);
			} else
				pr_warn("HugeTLB: vma_lock already exists in %s.\n", __func__);
		} else
			hugetlb_vma_lock_alloc(vma);
	}
}

static void hugetlb_vm_op_close(struct vm_area_struct *vma)
{
	struct hstate *h = hstate_vma(vma);
	struct resv_map *resv;
	struct hugepage_subpool *spool = subpool_vma(vma);
	unsigned long reserve, start, end;
	long gbl_reserve;

	hugetlb_vma_lock_free(vma);

	resv = vma_resv_map(vma);
	if (!resv || !is_vma_resv_set(vma, HPAGE_RESV_OWNER))
		return;

	start = vma_hugecache_offset(h, vma, vma->vm_start);
	end = vma_hugecache_offset(h, vma, vma->vm_end);

	reserve = (end - start) - region_count(resv, start, end);
	hugetlb_cgroup_uncharge_counter(resv, start, end);
	if (reserve) {
		/*
		 * Decrement reserve counts.  The global reserve count may be
		 * adjusted if the subpool has a minimum size.
		 */
		gbl_reserve = hugepage_subpool_put_pages(spool, reserve);
		hugetlb_acct_memory(h, -gbl_reserve);
	}

	kref_put(&resv->refs, resv_map_release);
}

static int hugetlb_vm_op_split(struct vm_area_struct *vma, unsigned long addr)
{
	if (addr & ~(huge_page_mask(hstate_vma(vma))))
		return -EINVAL;

	/*
	 * PMD sharing is only possible for PUD_SIZE-aligned address ranges
	 * in HugeTLB VMAs. If we will lose PUD_SIZE alignment due to this
	 * split, unshare PMDs in the PUD_SIZE interval surrounding addr now.
	 */
	if (addr & ~PUD_MASK) {
		/*
		 * hugetlb_vm_op_split is called right before we attempt to
		 * split the VMA. We will need to unshare PMDs in the old and
		 * new VMAs, so let's unshare before we split.
		 */
		unsigned long floor = addr & PUD_MASK;
		unsigned long ceil = floor + PUD_SIZE;

		if (floor >= vma->vm_start && ceil <= vma->vm_end)
			hugetlb_unshare_pmds(vma, floor, ceil);
	}

	return 0;
}

static unsigned long hugetlb_vm_op_pagesize(struct vm_area_struct *vma)
{
	return huge_page_size(hstate_vma(vma));
}

/*
 * We cannot handle pagefaults against hugetlb pages at all.  They cause
 * handle_mm_fault() to try to instantiate regular-sized pages in the
 * hugepage VMA.  do_page_fault() is supposed to trap this, so BUG is we get
 * this far.
 */
static vm_fault_t hugetlb_vm_op_fault(struct vm_fault *vmf)
{
	BUG();
	return 0;
}

/*
 * When a new function is introduced to vm_operations_struct and added
 * to hugetlb_vm_ops, please consider adding the function to shm_vm_ops.
 * This is because under System V memory model, mappings created via
 * shmget/shmat with "huge page" specified are backed by hugetlbfs files,
 * their original vm_ops are overwritten with shm_vm_ops.
 */
const struct vm_operations_struct hugetlb_vm_ops = {
	.fault = hugetlb_vm_op_fault,
	.open = hugetlb_vm_op_open,
	.close = hugetlb_vm_op_close,
	.may_split = hugetlb_vm_op_split,
	.pagesize = hugetlb_vm_op_pagesize,
};

static pte_t make_huge_pte(struct vm_area_struct *vma, struct page *page,
		bool try_mkwrite)
{
	pte_t entry;
	unsigned int shift = huge_page_shift(hstate_vma(vma));

	if (try_mkwrite && (vma->vm_flags & VM_WRITE)) {
		entry = huge_pte_mkwrite(huge_pte_mkdirty(mk_huge_pte(page,
					 vma->vm_page_prot)));
	} else {
		entry = huge_pte_wrprotect(mk_huge_pte(page,
					   vma->vm_page_prot));
	}
	entry = pte_mkyoung(entry);
	entry = arch_make_huge_pte(entry, shift, vma->vm_flags);

	return entry;
}

static void set_huge_ptep_writable(struct vm_area_struct *vma,
				   unsigned long address, pte_t *ptep)
{
	pte_t entry;

	entry = huge_pte_mkwrite(huge_pte_mkdirty(huge_ptep_get(vma->vm_mm, address, ptep)));
	if (huge_ptep_set_access_flags(vma, address, ptep, entry, 1))
		update_mmu_cache(vma, address, ptep);
}

static void set_huge_ptep_maybe_writable(struct vm_area_struct *vma,
					 unsigned long address, pte_t *ptep)
{
	if (vma->vm_flags & VM_WRITE)
		set_huge_ptep_writable(vma, address, ptep);
}

bool is_hugetlb_entry_migration(pte_t pte)
{
	swp_entry_t swp;

	if (huge_pte_none(pte) || pte_present(pte))
		return false;
	swp = pte_to_swp_entry(pte);
	if (is_migration_entry(swp))
		return true;
	else
		return false;
}

bool is_hugetlb_entry_hwpoisoned(pte_t pte)
{
	swp_entry_t swp;

	if (huge_pte_none(pte) || pte_present(pte))
		return false;
	swp = pte_to_swp_entry(pte);
	if (is_hwpoison_entry(swp))
		return true;
	else
		return false;
}

static void
hugetlb_install_folio(struct vm_area_struct *vma, pte_t *ptep, unsigned long addr,
		      struct folio *new_folio, pte_t old, unsigned long sz)
{
	pte_t newpte = make_huge_pte(vma, &new_folio->page, true);

	__folio_mark_uptodate(new_folio);
	hugetlb_add_new_anon_rmap(new_folio, vma, addr);
	if (userfaultfd_wp(vma) && huge_pte_uffd_wp(old))
		newpte = huge_pte_mkuffd_wp(newpte);
	set_huge_pte_at(vma->vm_mm, addr, ptep, newpte, sz);
	hugetlb_count_add(pages_per_huge_page(hstate_vma(vma)), vma->vm_mm);
	folio_set_hugetlb_migratable(new_folio);
}

int copy_hugetlb_page_range(struct mm_struct *dst, struct mm_struct *src,
			    struct vm_area_struct *dst_vma,
			    struct vm_area_struct *src_vma)
{
	pte_t *src_pte, *dst_pte, entry;
	struct folio *pte_folio;
	unsigned long addr;
	bool cow = is_cow_mapping(src_vma->vm_flags);
	struct hstate *h = hstate_vma(src_vma);
	unsigned long sz = huge_page_size(h);
	unsigned long npages = pages_per_huge_page(h);
	struct mmu_notifier_range range;
	unsigned long last_addr_mask;
	int ret = 0;

	if (cow) {
		mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, src,
					src_vma->vm_start,
					src_vma->vm_end);
		mmu_notifier_invalidate_range_start(&range);
		vma_assert_write_locked(src_vma);
		raw_write_seqcount_begin(&src->write_protect_seq);
	} else {
		/*
		 * For shared mappings the vma lock must be held before
		 * calling hugetlb_walk() in the src vma. Otherwise, the
		 * returned ptep could go away if part of a shared pmd and
		 * another thread calls huge_pmd_unshare.
		 */
		hugetlb_vma_lock_read(src_vma);
	}

	last_addr_mask = hugetlb_mask_last_page(h);
	for (addr = src_vma->vm_start; addr < src_vma->vm_end; addr += sz) {
		spinlock_t *src_ptl, *dst_ptl;
		src_pte = hugetlb_walk(src_vma, addr, sz);
		if (!src_pte) {
			addr |= last_addr_mask;
			continue;
		}
		dst_pte = huge_pte_alloc(dst, dst_vma, addr, sz);
		if (!dst_pte) {
			ret = -ENOMEM;
			break;
		}

		/*
		 * If the pagetables are shared don't copy or take references.
		 *
		 * dst_pte == src_pte is the common case of src/dest sharing.
		 * However, src could have 'unshared' and dst shares with
		 * another vma. So page_count of ptep page is checked instead
		 * to reliably determine whether pte is shared.
		 */
		if (page_count(virt_to_page(dst_pte)) > 1) {
			addr |= last_addr_mask;
			continue;
		}

		dst_ptl = huge_pte_lock(h, dst, dst_pte);
		src_ptl = huge_pte_lockptr(h, src, src_pte);
		spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);
		entry = huge_ptep_get(src_vma->vm_mm, addr, src_pte);
again:
		if (huge_pte_none(entry)) {
			/*
			 * Skip if src entry none.
			 */
			;
		} else if (unlikely(is_hugetlb_entry_hwpoisoned(entry))) {
			if (!userfaultfd_wp(dst_vma))
				entry = huge_pte_clear_uffd_wp(entry);
			set_huge_pte_at(dst, addr, dst_pte, entry, sz);
		} else if (unlikely(is_hugetlb_entry_migration(entry))) {
			swp_entry_t swp_entry = pte_to_swp_entry(entry);
			bool uffd_wp = pte_swp_uffd_wp(entry);

			if (!is_readable_migration_entry(swp_entry) && cow) {
				/*
				 * COW mappings require pages in both
				 * parent and child to be set to read.
				 */
				swp_entry = make_readable_migration_entry(
							swp_offset(swp_entry));
				entry = swp_entry_to_pte(swp_entry);
				if (userfaultfd_wp(src_vma) && uffd_wp)
					entry = pte_swp_mkuffd_wp(entry);
				set_huge_pte_at(src, addr, src_pte, entry, sz);
			}
			if (!userfaultfd_wp(dst_vma))
				entry = huge_pte_clear_uffd_wp(entry);
			set_huge_pte_at(dst, addr, dst_pte, entry, sz);
		} else if (unlikely(is_pte_marker(entry))) {
			pte_marker marker = copy_pte_marker(
				pte_to_swp_entry(entry), dst_vma);

			if (marker)
				set_huge_pte_at(dst, addr, dst_pte,
						make_pte_marker(marker), sz);
		} else {
			entry = huge_ptep_get(src_vma->vm_mm, addr, src_pte);
			pte_folio = page_folio(pte_page(entry));
			folio_get(pte_folio);

			/*
			 * Failing to duplicate the anon rmap is a rare case
			 * where we see pinned hugetlb pages while they're
			 * prone to COW. We need to do the COW earlier during
			 * fork.
			 *
			 * When pre-allocating the page or copying data, we
			 * need to be without the pgtable locks since we could
			 * sleep during the process.
			 */
			if (!folio_test_anon(pte_folio)) {
				hugetlb_add_file_rmap(pte_folio);
			} else if (hugetlb_try_dup_anon_rmap(pte_folio, src_vma)) {
				pte_t src_pte_old = entry;
				struct folio *new_folio;

				spin_unlock(src_ptl);
				spin_unlock(dst_ptl);
				/* Do not use reserve as it's private owned */
				new_folio = alloc_hugetlb_folio(dst_vma, addr, false);
				if (IS_ERR(new_folio)) {
					folio_put(pte_folio);
					ret = PTR_ERR(new_folio);
					break;
				}
				ret = copy_user_large_folio(new_folio, pte_folio,
							    addr, dst_vma);
				folio_put(pte_folio);
				if (ret) {
					folio_put(new_folio);
					break;
				}

				/* Install the new hugetlb folio if src pte stable */
				dst_ptl = huge_pte_lock(h, dst, dst_pte);
				src_ptl = huge_pte_lockptr(h, src, src_pte);
				spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);
				entry = huge_ptep_get(src_vma->vm_mm, addr, src_pte);
				if (!pte_same(src_pte_old, entry)) {
					restore_reserve_on_error(h, dst_vma, addr,
								new_folio);
					folio_put(new_folio);
					/* huge_ptep of dst_pte won't change as in child */
					goto again;
				}
				hugetlb_install_folio(dst_vma, dst_pte, addr,
						      new_folio, src_pte_old, sz);
				spin_unlock(src_ptl);
				spin_unlock(dst_ptl);
				continue;
			}

			if (cow) {
				/*
				 * No need to notify as we are downgrading page
				 * table protection not changing it to point
				 * to a new page.
				 *
				 * See Documentation/mm/mmu_notifier.rst
				 */
				huge_ptep_set_wrprotect(src, addr, src_pte);
				entry = huge_pte_wrprotect(entry);
			}

			if (!userfaultfd_wp(dst_vma))
				entry = huge_pte_clear_uffd_wp(entry);

			set_huge_pte_at(dst, addr, dst_pte, entry, sz);
			hugetlb_count_add(npages, dst);
		}
		spin_unlock(src_ptl);
		spin_unlock(dst_ptl);
	}

	if (cow) {
		raw_write_seqcount_end(&src->write_protect_seq);
		mmu_notifier_invalidate_range_end(&range);
	} else {
		hugetlb_vma_unlock_read(src_vma);
	}

	return ret;
}

static void move_huge_pte(struct vm_area_struct *vma, unsigned long old_addr,
			  unsigned long new_addr, pte_t *src_pte, pte_t *dst_pte,
			  unsigned long sz)
{
	bool need_clear_uffd_wp = vma_has_uffd_without_event_remap(vma);
	struct hstate *h = hstate_vma(vma);
	struct mm_struct *mm = vma->vm_mm;
	spinlock_t *src_ptl, *dst_ptl;
	pte_t pte;

	dst_ptl = huge_pte_lock(h, mm, dst_pte);
	src_ptl = huge_pte_lockptr(h, mm, src_pte);

	/*
	 * We don't have to worry about the ordering of src and dst ptlocks
	 * because exclusive mmap_lock (or the i_mmap_lock) prevents deadlock.
	 */
	if (src_ptl != dst_ptl)
		spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);

	pte = huge_ptep_get_and_clear(mm, old_addr, src_pte, sz);

	if (need_clear_uffd_wp && pte_marker_uffd_wp(pte))
		huge_pte_clear(mm, new_addr, dst_pte, sz);
	else {
		if (need_clear_uffd_wp) {
			if (pte_present(pte))
				pte = huge_pte_clear_uffd_wp(pte);
			else if (is_swap_pte(pte))
				pte = pte_swp_clear_uffd_wp(pte);
		}
		set_huge_pte_at(mm, new_addr, dst_pte, pte, sz);
	}

	if (src_ptl != dst_ptl)
		spin_unlock(src_ptl);
	spin_unlock(dst_ptl);
}

int move_hugetlb_page_tables(struct vm_area_struct *vma,
			     struct vm_area_struct *new_vma,
			     unsigned long old_addr, unsigned long new_addr,
			     unsigned long len)
{
	struct hstate *h = hstate_vma(vma);
	struct address_space *mapping = vma->vm_file->f_mapping;
	unsigned long sz = huge_page_size(h);
	struct mm_struct *mm = vma->vm_mm;
	unsigned long old_end = old_addr + len;
	unsigned long last_addr_mask;
	pte_t *src_pte, *dst_pte;
	struct mmu_notifier_range range;
	bool shared_pmd = false;

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, mm, old_addr,
				old_end);
	adjust_range_if_pmd_sharing_possible(vma, &range.start, &range.end);
	/*
	 * In case of shared PMDs, we should cover the maximum possible
	 * range.
	 */
	flush_cache_range(vma, range.start, range.end);

	mmu_notifier_invalidate_range_start(&range);
	last_addr_mask = hugetlb_mask_last_page(h);
	/* Prevent race with file truncation */
	hugetlb_vma_lock_write(vma);
	i_mmap_lock_write(mapping);
	for (; old_addr < old_end; old_addr += sz, new_addr += sz) {
		src_pte = hugetlb_walk(vma, old_addr, sz);
		if (!src_pte) {
			old_addr |= last_addr_mask;
			new_addr |= last_addr_mask;
			continue;
		}
		if (huge_pte_none(huge_ptep_get(mm, old_addr, src_pte)))
			continue;

		if (huge_pmd_unshare(mm, vma, old_addr, src_pte)) {
			shared_pmd = true;
			old_addr |= last_addr_mask;
			new_addr |= last_addr_mask;
			continue;
		}

		dst_pte = huge_pte_alloc(mm, new_vma, new_addr, sz);
		if (!dst_pte)
			break;

		move_huge_pte(vma, old_addr, new_addr, src_pte, dst_pte, sz);
	}

	if (shared_pmd)
		flush_hugetlb_tlb_range(vma, range.start, range.end);
	else
		flush_hugetlb_tlb_range(vma, old_end - len, old_end);
	mmu_notifier_invalidate_range_end(&range);
	i_mmap_unlock_write(mapping);
	hugetlb_vma_unlock_write(vma);

	return len + old_addr - old_end;
}

void __unmap_hugepage_range(struct mmu_gather *tlb, struct vm_area_struct *vma,
			    unsigned long start, unsigned long end,
			    struct page *ref_page, zap_flags_t zap_flags)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address;
	pte_t *ptep;
	pte_t pte;
	spinlock_t *ptl;
	struct page *page;
	struct hstate *h = hstate_vma(vma);
	unsigned long sz = huge_page_size(h);
	bool adjust_reservation = false;
	unsigned long last_addr_mask;
	bool force_flush = false;

	WARN_ON(!is_vm_hugetlb_page(vma));
	BUG_ON(start & ~huge_page_mask(h));
	BUG_ON(end & ~huge_page_mask(h));

	/*
	 * This is a hugetlb vma, all the pte entries should point
	 * to huge page.
	 */
	tlb_change_page_size(tlb, sz);
	tlb_start_vma(tlb, vma);

	last_addr_mask = hugetlb_mask_last_page(h);
	address = start;
	for (; address < end; address += sz) {
		ptep = hugetlb_walk(vma, address, sz);
		if (!ptep) {
			address |= last_addr_mask;
			continue;
		}

		ptl = huge_pte_lock(h, mm, ptep);
		if (huge_pmd_unshare(mm, vma, address, ptep)) {
			spin_unlock(ptl);
			tlb_flush_pmd_range(tlb, address & PUD_MASK, PUD_SIZE);
			force_flush = true;
			address |= last_addr_mask;
			continue;
		}

		pte = huge_ptep_get(mm, address, ptep);
		if (huge_pte_none(pte)) {
			spin_unlock(ptl);
			continue;
		}

		/*
		 * Migrating hugepage or HWPoisoned hugepage is already
		 * unmapped and its refcount is dropped, so just clear pte here.
		 */
		if (unlikely(!pte_present(pte))) {
			/*
			 * If the pte was wr-protected by uffd-wp in any of the
			 * swap forms, meanwhile the caller does not want to
			 * drop the uffd-wp bit in this zap, then replace the
			 * pte with a marker.
			 */
			if (pte_swp_uffd_wp_any(pte) &&
			    !(zap_flags & ZAP_FLAG_DROP_MARKER))
				set_huge_pte_at(mm, address, ptep,
						make_pte_marker(PTE_MARKER_UFFD_WP),
						sz);
			else
				huge_pte_clear(mm, address, ptep, sz);
			spin_unlock(ptl);
			continue;
		}

		page = pte_page(pte);
		/*
		 * If a reference page is supplied, it is because a specific
		 * page is being unmapped, not a range. Ensure the page we
		 * are about to unmap is the actual page of interest.
		 */
		if (ref_page) {
			if (page != ref_page) {
				spin_unlock(ptl);
				continue;
			}
			/*
			 * Mark the VMA as having unmapped its page so that
			 * future faults in this VMA will fail rather than
			 * looking like data was lost
			 */
			set_vma_resv_flags(vma, HPAGE_RESV_UNMAPPED);
		}

		pte = huge_ptep_get_and_clear(mm, address, ptep, sz);
		tlb_remove_huge_tlb_entry(h, tlb, ptep, address);
		if (huge_pte_dirty(pte))
			set_page_dirty(page);
		/* Leave a uffd-wp pte marker if needed */
		if (huge_pte_uffd_wp(pte) &&
		    !(zap_flags & ZAP_FLAG_DROP_MARKER))
			set_huge_pte_at(mm, address, ptep,
					make_pte_marker(PTE_MARKER_UFFD_WP),
					sz);
		hugetlb_count_sub(pages_per_huge_page(h), mm);
		hugetlb_remove_rmap(page_folio(page));

		/*
		 * Restore the reservation for anonymous page, otherwise the
		 * backing page could be stolen by someone.
		 * If there we are freeing a surplus, do not set the restore
		 * reservation bit.
		 */
		if (!h->surplus_huge_pages && __vma_private_lock(vma) &&
		    folio_test_anon(page_folio(page))) {
			folio_set_hugetlb_restore_reserve(page_folio(page));
			/* Reservation to be adjusted after the spin lock */
			adjust_reservation = true;
		}

		spin_unlock(ptl);

		/*
		 * Adjust the reservation for the region that will have the
		 * reserve restored. Keep in mind that vma_needs_reservation() changes
		 * resv->adds_in_progress if it succeeds. If this is not done,
		 * do_exit() will not see it, and will keep the reservation
		 * forever.
		 */
		if (adjust_reservation) {
			int rc = vma_needs_reservation(h, vma, address);

			if (rc < 0)
				/* Pressumably allocate_file_region_entries failed
				 * to allocate a file_region struct. Clear
				 * hugetlb_restore_reserve so that global reserve
				 * count will not be incremented by free_huge_folio.
				 * Act as if we consumed the reservation.
				 */
				folio_clear_hugetlb_restore_reserve(page_folio(page));
			else if (rc)
				vma_add_reservation(h, vma, address);
		}

		tlb_remove_page_size(tlb, page, huge_page_size(h));
		/*
		 * Bail out after unmapping reference page if supplied
		 */
		if (ref_page)
			break;
	}
	tlb_end_vma(tlb, vma);

	/*
	 * If we unshared PMDs, the TLB flush was not recorded in mmu_gather. We
	 * could defer the flush until now, since by holding i_mmap_rwsem we
	 * guaranteed that the last refernece would not be dropped. But we must
	 * do the flushing before we return, as otherwise i_mmap_rwsem will be
	 * dropped and the last reference to the shared PMDs page might be
	 * dropped as well.
	 *
	 * In theory we could defer the freeing of the PMD pages as well, but
	 * huge_pmd_unshare() relies on the exact page_count for the PMD page to
	 * detect sharing, so we cannot defer the release of the page either.
	 * Instead, do flush now.
	 */
	if (force_flush)
		tlb_flush_mmu_tlbonly(tlb);
}

void __hugetlb_zap_begin(struct vm_area_struct *vma,
			 unsigned long *start, unsigned long *end)
{
	if (!vma->vm_file)	/* hugetlbfs_file_mmap error */
		return;

	adjust_range_if_pmd_sharing_possible(vma, start, end);
	hugetlb_vma_lock_write(vma);
	if (vma->vm_file)
		i_mmap_lock_write(vma->vm_file->f_mapping);
}

void __hugetlb_zap_end(struct vm_area_struct *vma,
		       struct zap_details *details)
{
	zap_flags_t zap_flags = details ? details->zap_flags : 0;

	if (!vma->vm_file)	/* hugetlbfs_file_mmap error */
		return;

	if (zap_flags & ZAP_FLAG_UNMAP) {	/* final unmap */
		/*
		 * Unlock and free the vma lock before releasing i_mmap_rwsem.
		 * When the vma_lock is freed, this makes the vma ineligible
		 * for pmd sharing.  And, i_mmap_rwsem is required to set up
		 * pmd sharing.  This is important as page tables for this
		 * unmapped range will be asynchrously deleted.  If the page
		 * tables are shared, there will be issues when accessed by
		 * someone else.
		 */
		__hugetlb_vma_unlock_write_free(vma);
	} else {
		hugetlb_vma_unlock_write(vma);
	}

	if (vma->vm_file)
		i_mmap_unlock_write(vma->vm_file->f_mapping);
}

void unmap_hugepage_range(struct vm_area_struct *vma, unsigned long start,
			  unsigned long end, struct page *ref_page,
			  zap_flags_t zap_flags)
{
	struct mmu_notifier_range range;
	struct mmu_gather tlb;

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma->vm_mm,
				start, end);
	adjust_range_if_pmd_sharing_possible(vma, &range.start, &range.end);
	mmu_notifier_invalidate_range_start(&range);
	tlb_gather_mmu(&tlb, vma->vm_mm);

	__unmap_hugepage_range(&tlb, vma, start, end, ref_page, zap_flags);

	mmu_notifier_invalidate_range_end(&range);
	tlb_finish_mmu(&tlb);
}

/*
 * This is called when the original mapper is failing to COW a MAP_PRIVATE
 * mapping it owns the reserve page for. The intention is to unmap the page
 * from other VMAs and let the children be SIGKILLed if they are faulting the
 * same region.
 */
static void unmap_ref_private(struct mm_struct *mm, struct vm_area_struct *vma,
			      struct page *page, unsigned long address)
{
	struct hstate *h = hstate_vma(vma);
	struct vm_area_struct *iter_vma;
	struct address_space *mapping;
	pgoff_t pgoff;

	/*
	 * vm_pgoff is in PAGE_SIZE units, hence the different calculation
	 * from page cache lookup which is in HPAGE_SIZE units.
	 */
	address = address & huge_page_mask(h);
	pgoff = ((address - vma->vm_start) >> PAGE_SHIFT) +
			vma->vm_pgoff;
	mapping = vma->vm_file->f_mapping;

	/*
	 * Take the mapping lock for the duration of the table walk. As
	 * this mapping should be shared between all the VMAs,
	 * __unmap_hugepage_range() is called as the lock is already held
	 */
	i_mmap_lock_write(mapping);
	vma_interval_tree_foreach(iter_vma, &mapping->i_mmap, pgoff, pgoff) {
		/* Do not unmap the current VMA */
		if (iter_vma == vma)
			continue;

		/*
		 * Shared VMAs have their own reserves and do not affect
		 * MAP_PRIVATE accounting but it is possible that a shared
		 * VMA is using the same page so check and skip such VMAs.
		 */
		if (iter_vma->vm_flags & VM_MAYSHARE)
			continue;

		/*
		 * Unmap the page from other VMAs without their own reserves.
		 * They get marked to be SIGKILLed if they fault in these
		 * areas. This is because a future no-page fault on this VMA
		 * could insert a zeroed page instead of the data existing
		 * from the time of fork. This would look like data corruption
		 */
		if (!is_vma_resv_set(iter_vma, HPAGE_RESV_OWNER))
			unmap_hugepage_range(iter_vma, address,
					     address + huge_page_size(h), page, 0);
	}
	i_mmap_unlock_write(mapping);
}

/*
 * hugetlb_wp() should be called with page lock of the original hugepage held.
 * Called with hugetlb_fault_mutex_table held and pte_page locked so we
 * cannot race with other handlers or page migration.
 * Keep the pte_same checks anyway to make transition from the mutex easier.
 */
static vm_fault_t hugetlb_wp(struct folio *pagecache_folio,
		       struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct mm_struct *mm = vma->vm_mm;
	const bool unshare = vmf->flags & FAULT_FLAG_UNSHARE;
	pte_t pte = huge_ptep_get(mm, vmf->address, vmf->pte);
	struct hstate *h = hstate_vma(vma);
	struct folio *old_folio;
	struct folio *new_folio;
	bool cow_from_owner = 0;
	vm_fault_t ret = 0;
	struct mmu_notifier_range range;

	/*
	 * Never handle CoW for uffd-wp protected pages.  It should be only
	 * handled when the uffd-wp protection is removed.
	 *
	 * Note that only the CoW optimization path (in hugetlb_no_page())
	 * can trigger this, because hugetlb_fault() will always resolve
	 * uffd-wp bit first.
	 */
	if (!unshare && huge_pte_uffd_wp(pte))
		return 0;

	/* Let's take out MAP_SHARED mappings first. */
	if (vma->vm_flags & VM_MAYSHARE) {
		set_huge_ptep_writable(vma, vmf->address, vmf->pte);
		return 0;
	}

	old_folio = page_folio(pte_page(pte));

	delayacct_wpcopy_start();

retry_avoidcopy:
	/*
	 * If no-one else is actually using this page, we're the exclusive
	 * owner and can reuse this page.
	 *
	 * Note that we don't rely on the (safer) folio refcount here, because
	 * copying the hugetlb folio when there are unexpected (temporary)
	 * folio references could harm simple fork()+exit() users when
	 * we run out of free hugetlb folios: we would have to kill processes
	 * in scenarios that used to work. As a side effect, there can still
	 * be leaks between processes, for example, with FOLL_GET users.
	 */
	if (folio_mapcount(old_folio) == 1 && folio_test_anon(old_folio)) {
		if (!PageAnonExclusive(&old_folio->page)) {
			folio_move_anon_rmap(old_folio, vma);
			SetPageAnonExclusive(&old_folio->page);
		}
		if (likely(!unshare))
			set_huge_ptep_maybe_writable(vma, vmf->address,
						     vmf->pte);

		delayacct_wpcopy_end();
		return 0;
	}
	VM_BUG_ON_PAGE(folio_test_anon(old_folio) &&
		       PageAnonExclusive(&old_folio->page), &old_folio->page);

	/*
	 * If the process that created a MAP_PRIVATE mapping is about to
	 * perform a COW due to a shared page count, attempt to satisfy
	 * the allocation without using the existing reserves. The pagecache
	 * page is used to determine if the reserve at this address was
	 * consumed or not. If reserves were used, a partial faulted mapping
	 * at the time of fork() could consume its reserves on COW instead
	 * of the full address range.
	 */
	if (is_vma_resv_set(vma, HPAGE_RESV_OWNER) &&
			old_folio != pagecache_folio)
		cow_from_owner = true;

	folio_get(old_folio);

	/*
	 * Drop page table lock as buddy allocator may be called. It will
	 * be acquired again before returning to the caller, as expected.
	 */
	spin_unlock(vmf->ptl);
	new_folio = alloc_hugetlb_folio(vma, vmf->address, cow_from_owner);

	if (IS_ERR(new_folio)) {
		/*
		 * If a process owning a MAP_PRIVATE mapping fails to COW,
		 * it is due to references held by a child and an insufficient
		 * huge page pool. To guarantee the original mappers
		 * reliability, unmap the page from child processes. The child
		 * may get SIGKILLed if it later faults.
		 */
		if (cow_from_owner) {
			struct address_space *mapping = vma->vm_file->f_mapping;
			pgoff_t idx;
			u32 hash;

			folio_put(old_folio);
			/*
			 * Drop hugetlb_fault_mutex and vma_lock before
			 * unmapping.  unmapping needs to hold vma_lock
			 * in write mode.  Dropping vma_lock in read mode
			 * here is OK as COW mappings do not interact with
			 * PMD sharing.
			 *
			 * Reacquire both after unmap operation.
			 */
			idx = vma_hugecache_offset(h, vma, vmf->address);
			hash = hugetlb_fault_mutex_hash(mapping, idx);
			hugetlb_vma_unlock_read(vma);
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);

			unmap_ref_private(mm, vma, &old_folio->page,
					vmf->address);

			mutex_lock(&hugetlb_fault_mutex_table[hash]);
			hugetlb_vma_lock_read(vma);
			spin_lock(vmf->ptl);
			vmf->pte = hugetlb_walk(vma, vmf->address,
					huge_page_size(h));
			if (likely(vmf->pte &&
				   pte_same(huge_ptep_get(mm, vmf->address, vmf->pte), pte)))
				goto retry_avoidcopy;
			/*
			 * race occurs while re-acquiring page table
			 * lock, and our job is done.
			 */
			delayacct_wpcopy_end();
			return 0;
		}

		ret = vmf_error(PTR_ERR(new_folio));
		goto out_release_old;
	}

	/*
	 * When the original hugepage is shared one, it does not have
	 * anon_vma prepared.
	 */
	ret = __vmf_anon_prepare(vmf);
	if (unlikely(ret))
		goto out_release_all;

	if (copy_user_large_folio(new_folio, old_folio, vmf->real_address, vma)) {
		ret = VM_FAULT_HWPOISON_LARGE | VM_FAULT_SET_HINDEX(hstate_index(h));
		goto out_release_all;
	}
	__folio_mark_uptodate(new_folio);

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, mm, vmf->address,
				vmf->address + huge_page_size(h));
	mmu_notifier_invalidate_range_start(&range);

	/*
	 * Retake the page table lock to check for racing updates
	 * before the page tables are altered
	 */
	spin_lock(vmf->ptl);
	vmf->pte = hugetlb_walk(vma, vmf->address, huge_page_size(h));
	if (likely(vmf->pte && pte_same(huge_ptep_get(mm, vmf->address, vmf->pte), pte))) {
		pte_t newpte = make_huge_pte(vma, &new_folio->page, !unshare);

		/* Break COW or unshare */
		huge_ptep_clear_flush(vma, vmf->address, vmf->pte);
		hugetlb_remove_rmap(old_folio);
		hugetlb_add_new_anon_rmap(new_folio, vma, vmf->address);
		if (huge_pte_uffd_wp(pte))
			newpte = huge_pte_mkuffd_wp(newpte);
		set_huge_pte_at(mm, vmf->address, vmf->pte, newpte,
				huge_page_size(h));
		folio_set_hugetlb_migratable(new_folio);
		/* Make the old page be freed below */
		new_folio = old_folio;
	}
	spin_unlock(vmf->ptl);
	mmu_notifier_invalidate_range_end(&range);
out_release_all:
	/*
	 * No restore in case of successful pagetable update (Break COW or
	 * unshare)
	 */
	if (new_folio != old_folio)
		restore_reserve_on_error(h, vma, vmf->address, new_folio);
	folio_put(new_folio);
out_release_old:
	folio_put(old_folio);

	spin_lock(vmf->ptl); /* Caller expects lock to be held */

	delayacct_wpcopy_end();
	return ret;
}

/*
 * Return whether there is a pagecache page to back given address within VMA.
 */
bool hugetlbfs_pagecache_present(struct hstate *h,
				 struct vm_area_struct *vma, unsigned long address)
{
	struct address_space *mapping = vma->vm_file->f_mapping;
	pgoff_t idx = linear_page_index(vma, address);
	struct folio *folio;

	folio = filemap_get_folio(mapping, idx);
	if (IS_ERR(folio))
		return false;
	folio_put(folio);
	return true;
}

int hugetlb_add_to_page_cache(struct folio *folio, struct address_space *mapping,
			   pgoff_t idx)
{
	struct inode *inode = mapping->host;
	struct hstate *h = hstate_inode(inode);
	int err;

	idx <<= huge_page_order(h);
	__folio_set_locked(folio);
	err = __filemap_add_folio(mapping, folio, idx, GFP_KERNEL, NULL);

	if (unlikely(err)) {
		__folio_clear_locked(folio);
		return err;
	}
	folio_clear_hugetlb_restore_reserve(folio);

	/*
	 * mark folio dirty so that it will not be removed from cache/file
	 * by non-hugetlbfs specific code paths.
	 */
	folio_mark_dirty(folio);

	spin_lock(&inode->i_lock);
	inode->i_blocks += blocks_per_huge_page(h);
	spin_unlock(&inode->i_lock);
	return 0;
}

static inline vm_fault_t hugetlb_handle_userfault(struct vm_fault *vmf,
						  struct address_space *mapping,
						  unsigned long reason)
{
	u32 hash;

	/*
	 * vma_lock and hugetlb_fault_mutex must be dropped before handling
	 * userfault. Also mmap_lock could be dropped due to handling
	 * userfault, any vma operation should be careful from here.
	 */
	hugetlb_vma_unlock_read(vmf->vma);
	hash = hugetlb_fault_mutex_hash(mapping, vmf->pgoff);
	mutex_unlock(&hugetlb_fault_mutex_table[hash]);
	return handle_userfault(vmf, reason);
}

/*
 * Recheck pte with pgtable lock.  Returns true if pte didn't change, or
 * false if pte changed or is changing.
 */
static bool hugetlb_pte_stable(struct hstate *h, struct mm_struct *mm, unsigned long addr,
			       pte_t *ptep, pte_t old_pte)
{
	spinlock_t *ptl;
	bool same;

	ptl = huge_pte_lock(h, mm, ptep);
	same = pte_same(huge_ptep_get(mm, addr, ptep), old_pte);
	spin_unlock(ptl);

	return same;
}

static vm_fault_t hugetlb_no_page(struct address_space *mapping,
			struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct mm_struct *mm = vma->vm_mm;
	struct hstate *h = hstate_vma(vma);
	vm_fault_t ret = VM_FAULT_SIGBUS;
	int anon_rmap = 0;
	unsigned long size;
	struct folio *folio;
	pte_t new_pte;
	bool new_folio, new_pagecache_folio = false;
	u32 hash = hugetlb_fault_mutex_hash(mapping, vmf->pgoff);

	/*
	 * Currently, we are forced to kill the process in the event the
	 * original mapper has unmapped pages from the child due to a failed
	 * COW/unsharing. Warn that such a situation has occurred as it may not
	 * be obvious.
	 */
	if (is_vma_resv_set(vma, HPAGE_RESV_UNMAPPED)) {
		pr_warn_ratelimited("PID %d killed due to inadequate hugepage pool\n",
			   current->pid);
		goto out;
	}

	/*
	 * Use page lock to guard against racing truncation
	 * before we get page_table_lock.
	 */
	new_folio = false;
	folio = filemap_lock_hugetlb_folio(h, mapping, vmf->pgoff);
	if (IS_ERR(folio)) {
		size = i_size_read(mapping->host) >> huge_page_shift(h);
		if (vmf->pgoff >= size)
			goto out;
		/* Check for page in userfault range */
		if (userfaultfd_missing(vma)) {
			/*
			 * Since hugetlb_no_page() was examining pte
			 * without pgtable lock, we need to re-test under
			 * lock because the pte may not be stable and could
			 * have changed from under us.  Try to detect
			 * either changed or during-changing ptes and retry
			 * properly when needed.
			 *
			 * Note that userfaultfd is actually fine with
			 * false positives (e.g. caused by pte changed),
			 * but not wrong logical events (e.g. caused by
			 * reading a pte during changing).  The latter can
			 * confuse the userspace, so the strictness is very
			 * much preferred.  E.g., MISSING event should
			 * never happen on the page after UFFDIO_COPY has
			 * correctly installed the page and returned.
			 */
			if (!hugetlb_pte_stable(h, mm, vmf->address, vmf->pte, vmf->orig_pte)) {
				ret = 0;
				goto out;
			}

			return hugetlb_handle_userfault(vmf, mapping,
							VM_UFFD_MISSING);
		}

		if (!(vma->vm_flags & VM_MAYSHARE)) {
			ret = __vmf_anon_prepare(vmf);
			if (unlikely(ret))
				goto out;
		}

		folio = alloc_hugetlb_folio(vma, vmf->address, false);
		if (IS_ERR(folio)) {
			/*
			 * Returning error will result in faulting task being
			 * sent SIGBUS.  The hugetlb fault mutex prevents two
			 * tasks from racing to fault in the same page which
			 * could result in false unable to allocate errors.
			 * Page migration does not take the fault mutex, but
			 * does a clear then write of pte's under page table
			 * lock.  Page fault code could race with migration,
			 * notice the clear pte and try to allocate a page
			 * here.  Before returning error, get ptl and make
			 * sure there really is no pte entry.
			 */
			if (hugetlb_pte_stable(h, mm, vmf->address, vmf->pte, vmf->orig_pte))
				ret = vmf_error(PTR_ERR(folio));
			else
				ret = 0;
			goto out;
		}
		folio_zero_user(folio, vmf->real_address);
		__folio_mark_uptodate(folio);
		new_folio = true;

		if (vma->vm_flags & VM_MAYSHARE) {
			int err = hugetlb_add_to_page_cache(folio, mapping,
							vmf->pgoff);
			if (err) {
				/*
				 * err can't be -EEXIST which implies someone
				 * else consumed the reservation since hugetlb
				 * fault mutex is held when add a hugetlb page
				 * to the page cache. So it's safe to call
				 * restore_reserve_on_error() here.
				 */
				restore_reserve_on_error(h, vma, vmf->address,
							folio);
				folio_put(folio);
				ret = VM_FAULT_SIGBUS;
				goto out;
			}
			new_pagecache_folio = true;
		} else {
			folio_lock(folio);
			anon_rmap = 1;
		}
	} else {
		/*
		 * If memory error occurs between mmap() and fault, some process
		 * don't have hwpoisoned swap entry for errored virtual address.
		 * So we need to block hugepage fault by PG_hwpoison bit check.
		 */
		if (unlikely(folio_test_hwpoison(folio))) {
			ret = VM_FAULT_HWPOISON_LARGE |
				VM_FAULT_SET_HINDEX(hstate_index(h));
			goto backout_unlocked;
		}

		/* Check for page in userfault range. */
		if (userfaultfd_minor(vma)) {
			folio_unlock(folio);
			folio_put(folio);
			/* See comment in userfaultfd_missing() block above */
			if (!hugetlb_pte_stable(h, mm, vmf->address, vmf->pte, vmf->orig_pte)) {
				ret = 0;
				goto out;
			}
			return hugetlb_handle_userfault(vmf, mapping,
							VM_UFFD_MINOR);
		}
	}

	/*
	 * If we are going to COW a private mapping later, we examine the
	 * pending reservations for this page now. This will ensure that
	 * any allocations necessary to record that reservation occur outside
	 * the spinlock.
	 */
	if ((vmf->flags & FAULT_FLAG_WRITE) && !(vma->vm_flags & VM_SHARED)) {
		if (vma_needs_reservation(h, vma, vmf->address) < 0) {
			ret = VM_FAULT_OOM;
			goto backout_unlocked;
		}
		/* Just decrements count, does not deallocate */
		vma_end_reservation(h, vma, vmf->address);
	}

	vmf->ptl = huge_pte_lock(h, mm, vmf->pte);
	ret = 0;
	/* If pte changed from under us, retry */
	if (!pte_same(huge_ptep_get(mm, vmf->address, vmf->pte), vmf->orig_pte))
		goto backout;

	if (anon_rmap)
		hugetlb_add_new_anon_rmap(folio, vma, vmf->address);
	else
		hugetlb_add_file_rmap(folio);
	new_pte = make_huge_pte(vma, &folio->page, vma->vm_flags & VM_SHARED);
	/*
	 * If this pte was previously wr-protected, keep it wr-protected even
	 * if populated.
	 */
	if (unlikely(pte_marker_uffd_wp(vmf->orig_pte)))
		new_pte = huge_pte_mkuffd_wp(new_pte);
	set_huge_pte_at(mm, vmf->address, vmf->pte, new_pte, huge_page_size(h));

	hugetlb_count_add(pages_per_huge_page(h), mm);
	if ((vmf->flags & FAULT_FLAG_WRITE) && !(vma->vm_flags & VM_SHARED)) {
		/* Optimization, do the COW without a second fault */
		ret = hugetlb_wp(folio, vmf);
	}

	spin_unlock(vmf->ptl);

	/*
	 * Only set hugetlb_migratable in newly allocated pages.  Existing pages
	 * found in the pagecache may not have hugetlb_migratable if they have
	 * been isolated for migration.
	 */
	if (new_folio)
		folio_set_hugetlb_migratable(folio);

	folio_unlock(folio);
out:
	hugetlb_vma_unlock_read(vma);

	/*
	 * We must check to release the per-VMA lock. __vmf_anon_prepare() is
	 * the only way ret can be set to VM_FAULT_RETRY.
	 */
	if (unlikely(ret & VM_FAULT_RETRY))
		vma_end_read(vma);

	mutex_unlock(&hugetlb_fault_mutex_table[hash]);
	return ret;

backout:
	spin_unlock(vmf->ptl);
backout_unlocked:
	if (new_folio && !new_pagecache_folio)
		restore_reserve_on_error(h, vma, vmf->address, folio);

	folio_unlock(folio);
	folio_put(folio);
	goto out;
}

#ifdef CONFIG_SMP
u32 hugetlb_fault_mutex_hash(struct address_space *mapping, pgoff_t idx)
{
	unsigned long key[2];
	u32 hash;

	key[0] = (unsigned long) mapping;
	key[1] = idx;

	hash = jhash2((u32 *)&key, sizeof(key)/(sizeof(u32)), 0);

	return hash & (num_fault_mutexes - 1);
}
#else
/*
 * For uniprocessor systems we always use a single mutex, so just
 * return 0 and avoid the hashing overhead.
 */
u32 hugetlb_fault_mutex_hash(struct address_space *mapping, pgoff_t idx)
{
	return 0;
}
#endif

vm_fault_t hugetlb_fault(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, unsigned int flags)
{
	vm_fault_t ret;
	u32 hash;
	struct folio *folio = NULL;
	struct folio *pagecache_folio = NULL;
	struct hstate *h = hstate_vma(vma);
	struct address_space *mapping;
	int need_wait_lock = 0;
	struct vm_fault vmf = {
		.vma = vma,
		.address = address & huge_page_mask(h),
		.real_address = address,
		.flags = flags,
		.pgoff = vma_hugecache_offset(h, vma,
				address & huge_page_mask(h)),
		/* TODO: Track hugetlb faults using vm_fault */

		/*
		 * Some fields may not be initialized, be careful as it may
		 * be hard to debug if called functions make assumptions
		 */
	};

	/*
	 * Serialize hugepage allocation and instantiation, so that we don't
	 * get spurious allocation failures if two CPUs race to instantiate
	 * the same page in the page cache.
	 */
	mapping = vma->vm_file->f_mapping;
	hash = hugetlb_fault_mutex_hash(mapping, vmf.pgoff);
	mutex_lock(&hugetlb_fault_mutex_table[hash]);

	/*
	 * Acquire vma lock before calling huge_pte_alloc and hold
	 * until finished with vmf.pte.  This prevents huge_pmd_unshare from
	 * being called elsewhere and making the vmf.pte no longer valid.
	 */
	hugetlb_vma_lock_read(vma);
	vmf.pte = huge_pte_alloc(mm, vma, vmf.address, huge_page_size(h));
	if (!vmf.pte) {
		hugetlb_vma_unlock_read(vma);
		mutex_unlock(&hugetlb_fault_mutex_table[hash]);
		return VM_FAULT_OOM;
	}

	vmf.orig_pte = huge_ptep_get(mm, vmf.address, vmf.pte);
	if (huge_pte_none_mostly(vmf.orig_pte)) {
		if (is_pte_marker(vmf.orig_pte)) {
			pte_marker marker =
				pte_marker_get(pte_to_swp_entry(vmf.orig_pte));

			if (marker & PTE_MARKER_POISONED) {
				ret = VM_FAULT_HWPOISON_LARGE |
				      VM_FAULT_SET_HINDEX(hstate_index(h));
				goto out_mutex;
			} else if (WARN_ON_ONCE(marker & PTE_MARKER_GUARD)) {
				/* This isn't supported in hugetlb. */
				ret = VM_FAULT_SIGSEGV;
				goto out_mutex;
			}
		}

		/*
		 * Other PTE markers should be handled the same way as none PTE.
		 *
		 * hugetlb_no_page will drop vma lock and hugetlb fault
		 * mutex internally, which make us return immediately.
		 */
		return hugetlb_no_page(mapping, &vmf);
	}

	ret = 0;

	/*
	 * vmf.orig_pte could be a migration/hwpoison vmf.orig_pte at this
	 * point, so this check prevents the kernel from going below assuming
	 * that we have an active hugepage in pagecache. This goto expects
	 * the 2nd page fault, and is_hugetlb_entry_(migration|hwpoisoned)
	 * check will properly handle it.
	 */
	if (!pte_present(vmf.orig_pte)) {
		if (unlikely(is_hugetlb_entry_migration(vmf.orig_pte))) {
			/*
			 * Release the hugetlb fault lock now, but retain
			 * the vma lock, because it is needed to guard the
			 * huge_pte_lockptr() later in
			 * migration_entry_wait_huge(). The vma lock will
			 * be released there.
			 */
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			migration_entry_wait_huge(vma, vmf.address, vmf.pte);
			return 0;
		} else if (unlikely(is_hugetlb_entry_hwpoisoned(vmf.orig_pte)))
			ret = VM_FAULT_HWPOISON_LARGE |
			    VM_FAULT_SET_HINDEX(hstate_index(h));
		goto out_mutex;
	}

	/*
	 * If we are going to COW/unshare the mapping later, we examine the
	 * pending reservations for this page now. This will ensure that any
	 * allocations necessary to record that reservation occur outside the
	 * spinlock. Also lookup the pagecache page now as it is used to
	 * determine if a reservation has been consumed.
	 */
	if ((flags & (FAULT_FLAG_WRITE|FAULT_FLAG_UNSHARE)) &&
	    !(vma->vm_flags & VM_MAYSHARE) && !huge_pte_write(vmf.orig_pte)) {
		if (vma_needs_reservation(h, vma, vmf.address) < 0) {
			ret = VM_FAULT_OOM;
			goto out_mutex;
		}
		/* Just decrements count, does not deallocate */
		vma_end_reservation(h, vma, vmf.address);

		pagecache_folio = filemap_lock_hugetlb_folio(h, mapping,
							     vmf.pgoff);
		if (IS_ERR(pagecache_folio))
			pagecache_folio = NULL;
	}

	vmf.ptl = huge_pte_lock(h, mm, vmf.pte);

	/* Check for a racing update before calling hugetlb_wp() */
	if (unlikely(!pte_same(vmf.orig_pte, huge_ptep_get(mm, vmf.address, vmf.pte))))
		goto out_ptl;

	/* Handle userfault-wp first, before trying to lock more pages */
	if (userfaultfd_wp(vma) && huge_pte_uffd_wp(huge_ptep_get(mm, vmf.address, vmf.pte)) &&
	    (flags & FAULT_FLAG_WRITE) && !huge_pte_write(vmf.orig_pte)) {
		if (!userfaultfd_wp_async(vma)) {
			spin_unlock(vmf.ptl);
			if (pagecache_folio) {
				folio_unlock(pagecache_folio);
				folio_put(pagecache_folio);
			}
			hugetlb_vma_unlock_read(vma);
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			return handle_userfault(&vmf, VM_UFFD_WP);
		}

		vmf.orig_pte = huge_pte_clear_uffd_wp(vmf.orig_pte);
		set_huge_pte_at(mm, vmf.address, vmf.pte, vmf.orig_pte,
				huge_page_size(hstate_vma(vma)));
		/* Fallthrough to CoW */
	}

	/*
	 * hugetlb_wp() requires page locks of pte_page(vmf.orig_pte) and
	 * pagecache_folio, so here we need take the former one
	 * when folio != pagecache_folio or !pagecache_folio.
	 */
	folio = page_folio(pte_page(vmf.orig_pte));
	if (folio != pagecache_folio)
		if (!folio_trylock(folio)) {
			need_wait_lock = 1;
			goto out_ptl;
		}

	folio_get(folio);

	if (flags & (FAULT_FLAG_WRITE|FAULT_FLAG_UNSHARE)) {
		if (!huge_pte_write(vmf.orig_pte)) {
			ret = hugetlb_wp(pagecache_folio, &vmf);
			goto out_put_page;
		} else if (likely(flags & FAULT_FLAG_WRITE)) {
			vmf.orig_pte = huge_pte_mkdirty(vmf.orig_pte);
		}
	}
	vmf.orig_pte = pte_mkyoung(vmf.orig_pte);
	if (huge_ptep_set_access_flags(vma, vmf.address, vmf.pte, vmf.orig_pte,
						flags & FAULT_FLAG_WRITE))
		update_mmu_cache(vma, vmf.address, vmf.pte);
out_put_page:
	if (folio != pagecache_folio)
		folio_unlock(folio);
	folio_put(folio);
out_ptl:
	spin_unlock(vmf.ptl);

	if (pagecache_folio) {
		folio_unlock(pagecache_folio);
		folio_put(pagecache_folio);
	}
out_mutex:
	hugetlb_vma_unlock_read(vma);

	/*
	 * We must check to release the per-VMA lock. __vmf_anon_prepare() in
	 * hugetlb_wp() is the only way ret can be set to VM_FAULT_RETRY.
	 */
	if (unlikely(ret & VM_FAULT_RETRY))
		vma_end_read(vma);

	mutex_unlock(&hugetlb_fault_mutex_table[hash]);
	/*
	 * Generally it's safe to hold refcount during waiting page lock. But
	 * here we just wait to defer the next page fault to avoid busy loop and
	 * the page is not used after unlocked before returning from the current
	 * page fault. So we are safe from accessing freed page, even if we wait
	 * here without taking refcount.
	 */
	if (need_wait_lock)
		folio_wait_locked(folio);
	return ret;
}

#ifdef CONFIG_USERFAULTFD
/*
 * Can probably be eliminated, but still used by hugetlb_mfill_atomic_pte().
 */
static struct folio *alloc_hugetlb_folio_vma(struct hstate *h,
		struct vm_area_struct *vma, unsigned long address)
{
	struct mempolicy *mpol;
	nodemask_t *nodemask;
	struct folio *folio;
	gfp_t gfp_mask;
	int node;

	gfp_mask = htlb_alloc_mask(h);
	node = huge_node(vma, address, gfp_mask, &mpol, &nodemask);
	/*
	 * This is used to allocate a temporary hugetlb to hold the copied
	 * content, which will then be copied again to the final hugetlb
	 * consuming a reservation. Set the alloc_fallback to false to indicate
	 * that breaking the per-node hugetlb pool is not allowed in this case.
	 */
	folio = alloc_hugetlb_folio_nodemask(h, node, nodemask, gfp_mask, false);
	mpol_cond_put(mpol);

	return folio;
}

/*
 * Used by userfaultfd UFFDIO_* ioctls. Based on userfaultfd's mfill_atomic_pte
 * with modifications for hugetlb pages.
 */
int hugetlb_mfill_atomic_pte(pte_t *dst_pte,
			     struct vm_area_struct *dst_vma,
			     unsigned long dst_addr,
			     unsigned long src_addr,
			     uffd_flags_t flags,
			     struct folio **foliop)
{
	struct mm_struct *dst_mm = dst_vma->vm_mm;
	bool is_continue = uffd_flags_mode_is(flags, MFILL_ATOMIC_CONTINUE);
	bool wp_enabled = (flags & MFILL_ATOMIC_WP);
	struct hstate *h = hstate_vma(dst_vma);
	struct address_space *mapping = dst_vma->vm_file->f_mapping;
	pgoff_t idx = vma_hugecache_offset(h, dst_vma, dst_addr);
	unsigned long size = huge_page_size(h);
	int vm_shared = dst_vma->vm_flags & VM_SHARED;
	pte_t _dst_pte;
	spinlock_t *ptl;
	int ret = -ENOMEM;
	struct folio *folio;
	bool folio_in_pagecache = false;

	if (uffd_flags_mode_is(flags, MFILL_ATOMIC_POISON)) {
		ptl = huge_pte_lock(h, dst_mm, dst_pte);

		/* Don't overwrite any existing PTEs (even markers) */
		if (!huge_pte_none(huge_ptep_get(dst_mm, dst_addr, dst_pte))) {
			spin_unlock(ptl);
			return -EEXIST;
		}

		_dst_pte = make_pte_marker(PTE_MARKER_POISONED);
		set_huge_pte_at(dst_mm, dst_addr, dst_pte, _dst_pte, size);

		/* No need to invalidate - it was non-present before */
		update_mmu_cache(dst_vma, dst_addr, dst_pte);

		spin_unlock(ptl);
		return 0;
	}

	if (is_continue) {
		ret = -EFAULT;
		folio = filemap_lock_hugetlb_folio(h, mapping, idx);
		if (IS_ERR(folio))
			goto out;
		folio_in_pagecache = true;
	} else if (!*foliop) {
		/* If a folio already exists, then it's UFFDIO_COPY for
		 * a non-missing case. Return -EEXIST.
		 */
		if (vm_shared &&
		    hugetlbfs_pagecache_present(h, dst_vma, dst_addr)) {
			ret = -EEXIST;
			goto out;
		}

		folio = alloc_hugetlb_folio(dst_vma, dst_addr, false);
		if (IS_ERR(folio)) {
			ret = -ENOMEM;
			goto out;
		}

		ret = copy_folio_from_user(folio, (const void __user *) src_addr,
					   false);

		/* fallback to copy_from_user outside mmap_lock */
		if (unlikely(ret)) {
			ret = -ENOENT;
			/* Free the allocated folio which may have
			 * consumed a reservation.
			 */
			restore_reserve_on_error(h, dst_vma, dst_addr, folio);
			folio_put(folio);

			/* Allocate a temporary folio to hold the copied
			 * contents.
			 */
			folio = alloc_hugetlb_folio_vma(h, dst_vma, dst_addr);
			if (!folio) {
				ret = -ENOMEM;
				goto out;
			}
			*foliop = folio;
			/* Set the outparam foliop and return to the caller to
			 * copy the contents outside the lock. Don't free the
			 * folio.
			 */
			goto out;
		}
	} else {
		if (vm_shared &&
		    hugetlbfs_pagecache_present(h, dst_vma, dst_addr)) {
			folio_put(*foliop);
			ret = -EEXIST;
			*foliop = NULL;
			goto out;
		}

		folio = alloc_hugetlb_folio(dst_vma, dst_addr, false);
		if (IS_ERR(folio)) {
			folio_put(*foliop);
			ret = -ENOMEM;
			*foliop = NULL;
			goto out;
		}
		ret = copy_user_large_folio(folio, *foliop, dst_addr, dst_vma);
		folio_put(*foliop);
		*foliop = NULL;
		if (ret) {
			folio_put(folio);
			goto out;
		}
	}

	/*
	 * If we just allocated a new page, we need a memory barrier to ensure
	 * that preceding stores to the page become visible before the
	 * set_pte_at() write. The memory barrier inside __folio_mark_uptodate
	 * is what we need.
	 *
	 * In the case where we have not allocated a new page (is_continue),
	 * the page must already be uptodate. UFFDIO_CONTINUE already includes
	 * an earlier smp_wmb() to ensure that prior stores will be visible
	 * before the set_pte_at() write.
	 */
	if (!is_continue)
		__folio_mark_uptodate(folio);
	else
		WARN_ON_ONCE(!folio_test_uptodate(folio));

	/* Add shared, newly allocated pages to the page cache. */
	if (vm_shared && !is_continue) {
		ret = -EFAULT;
		if (idx >= (i_size_read(mapping->host) >> huge_page_shift(h)))
			goto out_release_nounlock;

		/*
		 * Serialization between remove_inode_hugepages() and
		 * hugetlb_add_to_page_cache() below happens through the
		 * hugetlb_fault_mutex_table that here must be hold by
		 * the caller.
		 */
		ret = hugetlb_add_to_page_cache(folio, mapping, idx);
		if (ret)
			goto out_release_nounlock;
		folio_in_pagecache = true;
	}

	ptl = huge_pte_lock(h, dst_mm, dst_pte);

	ret = -EIO;
	if (folio_test_hwpoison(folio))
		goto out_release_unlock;

	/*
	 * We allow to overwrite a pte marker: consider when both MISSING|WP
	 * registered, we firstly wr-protect a none pte which has no page cache
	 * page backing it, then access the page.
	 */
	ret = -EEXIST;
	if (!huge_pte_none_mostly(huge_ptep_get(dst_mm, dst_addr, dst_pte)))
		goto out_release_unlock;

	if (folio_in_pagecache)
		hugetlb_add_file_rmap(folio);
	else
		hugetlb_add_new_anon_rmap(folio, dst_vma, dst_addr);

	/*
	 * For either: (1) CONTINUE on a non-shared VMA, or (2) UFFDIO_COPY
	 * with wp flag set, don't set pte write bit.
	 */
	_dst_pte = make_huge_pte(dst_vma, &folio->page,
				 !wp_enabled && !(is_continue && !vm_shared));
	/*
	 * Always mark UFFDIO_COPY page dirty; note that this may not be
	 * extremely important for hugetlbfs for now since swapping is not
	 * supported, but we should still be clear in that this page cannot be
	 * thrown away at will, even if write bit not set.
	 */
	_dst_pte = huge_pte_mkdirty(_dst_pte);
	_dst_pte = pte_mkyoung(_dst_pte);

	if (wp_enabled)
		_dst_pte = huge_pte_mkuffd_wp(_dst_pte);

	set_huge_pte_at(dst_mm, dst_addr, dst_pte, _dst_pte, size);

	hugetlb_count_add(pages_per_huge_page(h), dst_mm);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(dst_vma, dst_addr, dst_pte);

	spin_unlock(ptl);
	if (!is_continue)
		folio_set_hugetlb_migratable(folio);
	if (vm_shared || is_continue)
		folio_unlock(folio);
	ret = 0;
out:
	return ret;
out_release_unlock:
	spin_unlock(ptl);
	if (vm_shared || is_continue)
		folio_unlock(folio);
out_release_nounlock:
	if (!folio_in_pagecache)
		restore_reserve_on_error(h, dst_vma, dst_addr, folio);
	folio_put(folio);
	goto out;
}
#endif /* CONFIG_USERFAULTFD */

long hugetlb_change_protection(struct vm_area_struct *vma,
		unsigned long address, unsigned long end,
		pgprot_t newprot, unsigned long cp_flags)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long start = address;
	pte_t *ptep;
	pte_t pte;
	struct hstate *h = hstate_vma(vma);
	long pages = 0, psize = huge_page_size(h);
	bool shared_pmd = false;
	struct mmu_notifier_range range;
	unsigned long last_addr_mask;
	bool uffd_wp = cp_flags & MM_CP_UFFD_WP;
	bool uffd_wp_resolve = cp_flags & MM_CP_UFFD_WP_RESOLVE;

	/*
	 * In the case of shared PMDs, the area to flush could be beyond
	 * start/end.  Set range.start/range.end to cover the maximum possible
	 * range if PMD sharing is possible.
	 */
	mmu_notifier_range_init(&range, MMU_NOTIFY_PROTECTION_VMA,
				0, mm, start, end);
	adjust_range_if_pmd_sharing_possible(vma, &range.start, &range.end);

	BUG_ON(address >= end);
	flush_cache_range(vma, range.start, range.end);

	mmu_notifier_invalidate_range_start(&range);
	hugetlb_vma_lock_write(vma);
	i_mmap_lock_write(vma->vm_file->f_mapping);
	last_addr_mask = hugetlb_mask_last_page(h);
	for (; address < end; address += psize) {
		spinlock_t *ptl;
		ptep = hugetlb_walk(vma, address, psize);
		if (!ptep) {
			if (!uffd_wp) {
				address |= last_addr_mask;
				continue;
			}
			/*
			 * Userfaultfd wr-protect requires pgtable
			 * pre-allocations to install pte markers.
			 */
			ptep = huge_pte_alloc(mm, vma, address, psize);
			if (!ptep) {
				pages = -ENOMEM;
				break;
			}
		}
		ptl = huge_pte_lock(h, mm, ptep);
		if (huge_pmd_unshare(mm, vma, address, ptep)) {
			/*
			 * When uffd-wp is enabled on the vma, unshare
			 * shouldn't happen at all.  Warn about it if it
			 * happened due to some reason.
			 */
			WARN_ON_ONCE(uffd_wp || uffd_wp_resolve);
			pages++;
			spin_unlock(ptl);
			shared_pmd = true;
			address |= last_addr_mask;
			continue;
		}
		pte = huge_ptep_get(mm, address, ptep);
		if (unlikely(is_hugetlb_entry_hwpoisoned(pte))) {
			/* Nothing to do. */
		} else if (unlikely(is_hugetlb_entry_migration(pte))) {
			swp_entry_t entry = pte_to_swp_entry(pte);
			struct page *page = pfn_swap_entry_to_page(entry);
			pte_t newpte = pte;

			if (is_writable_migration_entry(entry)) {
				if (PageAnon(page))
					entry = make_readable_exclusive_migration_entry(
								swp_offset(entry));
				else
					entry = make_readable_migration_entry(
								swp_offset(entry));
				newpte = swp_entry_to_pte(entry);
				pages++;
			}

			if (uffd_wp)
				newpte = pte_swp_mkuffd_wp(newpte);
			else if (uffd_wp_resolve)
				newpte = pte_swp_clear_uffd_wp(newpte);
			if (!pte_same(pte, newpte))
				set_huge_pte_at(mm, address, ptep, newpte, psize);
		} else if (unlikely(is_pte_marker(pte))) {
			/*
			 * Do nothing on a poison marker; page is
			 * corrupted, permissons do not apply.  Here
			 * pte_marker_uffd_wp()==true implies !poison
			 * because they're mutual exclusive.
			 */
			if (pte_marker_uffd_wp(pte) && uffd_wp_resolve)
				/* Safe to modify directly (non-present->none). */
				huge_pte_clear(mm, address, ptep, psize);
		} else if (!huge_pte_none(pte)) {
			pte_t old_pte;
			unsigned int shift = huge_page_shift(hstate_vma(vma));

			old_pte = huge_ptep_modify_prot_start(vma, address, ptep);
			pte = huge_pte_modify(old_pte, newprot);
			pte = arch_make_huge_pte(pte, shift, vma->vm_flags);
			if (uffd_wp)
				pte = huge_pte_mkuffd_wp(pte);
			else if (uffd_wp_resolve)
				pte = huge_pte_clear_uffd_wp(pte);
			huge_ptep_modify_prot_commit(vma, address, ptep, old_pte, pte);
			pages++;
		} else {
			/* None pte */
			if (unlikely(uffd_wp))
				/* Safe to modify directly (none->non-present). */
				set_huge_pte_at(mm, address, ptep,
						make_pte_marker(PTE_MARKER_UFFD_WP),
						psize);
		}
		spin_unlock(ptl);
	}
	/*
	 * Must flush TLB before releasing i_mmap_rwsem: x86's huge_pmd_unshare
	 * may have cleared our pud entry and done put_page on the page table:
	 * once we release i_mmap_rwsem, another task can do the final put_page
	 * and that page table be reused and filled with junk.  If we actually
	 * did unshare a page of pmds, flush the range corresponding to the pud.
	 */
	if (shared_pmd)
		flush_hugetlb_tlb_range(vma, range.start, range.end);
	else
		flush_hugetlb_tlb_range(vma, start, end);
	/*
	 * No need to call mmu_notifier_arch_invalidate_secondary_tlbs() we are
	 * downgrading page table protection not changing it to point to a new
	 * page.
	 *
	 * See Documentation/mm/mmu_notifier.rst
	 */
	i_mmap_unlock_write(vma->vm_file->f_mapping);
	hugetlb_vma_unlock_write(vma);
	mmu_notifier_invalidate_range_end(&range);

	return pages > 0 ? (pages << h->order) : pages;
}

/* Return true if reservation was successful, false otherwise.  */
bool hugetlb_reserve_pages(struct inode *inode,
					long from, long to,
					struct vm_area_struct *vma,
					vm_flags_t vm_flags)
{
	long chg = -1, add = -1;
	struct hstate *h = hstate_inode(inode);
	struct hugepage_subpool *spool = subpool_inode(inode);
	struct resv_map *resv_map;
	struct hugetlb_cgroup *h_cg = NULL;
	long gbl_reserve, regions_needed = 0;

	/* This should never happen */
	if (from > to) {
		VM_WARN(1, "%s called with a negative range\n", __func__);
		return false;
	}

	/*
	 * vma specific semaphore used for pmd sharing and fault/truncation
	 * synchronization
	 */
	hugetlb_vma_lock_alloc(vma);

	/*
	 * Only apply hugepage reservation if asked. At fault time, an
	 * attempt will be made for VM_NORESERVE to allocate a page
	 * without using reserves
	 */
	if (vm_flags & VM_NORESERVE)
		return true;

	/*
	 * Shared mappings base their reservation on the number of pages that
	 * are already allocated on behalf of the file. Private mappings need
	 * to reserve the full area even if read-only as mprotect() may be
	 * called to make the mapping read-write. Assume !vma is a shm mapping
	 */
	if (!vma || vma->vm_flags & VM_MAYSHARE) {
		/*
		 * resv_map can not be NULL as hugetlb_reserve_pages is only
		 * called for inodes for which resv_maps were created (see
		 * hugetlbfs_get_inode).
		 */
		resv_map = inode_resv_map(inode);

		chg = region_chg(resv_map, from, to, &regions_needed);
	} else {
		/* Private mapping. */
		resv_map = resv_map_alloc();
		if (!resv_map)
			goto out_err;

		chg = to - from;

		set_vma_resv_map(vma, resv_map);
		set_vma_resv_flags(vma, HPAGE_RESV_OWNER);
	}

	if (chg < 0)
		goto out_err;

	if (hugetlb_cgroup_charge_cgroup_rsvd(hstate_index(h),
				chg * pages_per_huge_page(h), &h_cg) < 0)
		goto out_err;

	if (vma && !(vma->vm_flags & VM_MAYSHARE) && h_cg) {
		/* For private mappings, the hugetlb_cgroup uncharge info hangs
		 * of the resv_map.
		 */
		resv_map_set_hugetlb_cgroup_uncharge_info(resv_map, h_cg, h);
	}

	/*
	 * There must be enough pages in the subpool for the mapping. If
	 * the subpool has a minimum size, there may be some global
	 * reservations already in place (gbl_reserve).
	 */
	gbl_reserve = hugepage_subpool_get_pages(spool, chg);
	if (gbl_reserve < 0)
		goto out_uncharge_cgroup;

	/*
	 * Check enough hugepages are available for the reservation.
	 * Hand the pages back to the subpool if there are not
	 */
	if (hugetlb_acct_memory(h, gbl_reserve) < 0)
		goto out_put_pages;

	/*
	 * Account for the reservations made. Shared mappings record regions
	 * that have reservations as they are shared by multiple VMAs.
	 * When the last VMA disappears, the region map says how much
	 * the reservation was and the page cache tells how much of
	 * the reservation was consumed. Private mappings are per-VMA and
	 * only the consumed reservations are tracked. When the VMA
	 * disappears, the original reservation is the VMA size and the
	 * consumed reservations are stored in the map. Hence, nothing
	 * else has to be done for private mappings here
	 */
	if (!vma || vma->vm_flags & VM_MAYSHARE) {
		add = region_add(resv_map, from, to, regions_needed, h, h_cg);

		if (unlikely(add < 0)) {
			hugetlb_acct_memory(h, -gbl_reserve);
			goto out_put_pages;
		} else if (unlikely(chg > add)) {
			/*
			 * pages in this range were added to the reserve
			 * map between region_chg and region_add.  This
			 * indicates a race with alloc_hugetlb_folio.  Adjust
			 * the subpool and reserve counts modified above
			 * based on the difference.
			 */
			long rsv_adjust;

			/*
			 * hugetlb_cgroup_uncharge_cgroup_rsvd() will put the
			 * reference to h_cg->css. See comment below for detail.
			 */
			hugetlb_cgroup_uncharge_cgroup_rsvd(
				hstate_index(h),
				(chg - add) * pages_per_huge_page(h), h_cg);

			rsv_adjust = hugepage_subpool_put_pages(spool,
								chg - add);
			hugetlb_acct_memory(h, -rsv_adjust);
		} else if (h_cg) {
			/*
			 * The file_regions will hold their own reference to
			 * h_cg->css. So we should release the reference held
			 * via hugetlb_cgroup_charge_cgroup_rsvd() when we are
			 * done.
			 */
			hugetlb_cgroup_put_rsvd_cgroup(h_cg);
		}
	}
	return true;

out_put_pages:
	/* put back original number of pages, chg */
	(void)hugepage_subpool_put_pages(spool, chg);
out_uncharge_cgroup:
	hugetlb_cgroup_uncharge_cgroup_rsvd(hstate_index(h),
					    chg * pages_per_huge_page(h), h_cg);
out_err:
	hugetlb_vma_lock_free(vma);
	if (!vma || vma->vm_flags & VM_MAYSHARE)
		/* Only call region_abort if the region_chg succeeded but the
		 * region_add failed or didn't run.
		 */
		if (chg >= 0 && add < 0)
			region_abort(resv_map, from, to, regions_needed);
	if (vma && is_vma_resv_set(vma, HPAGE_RESV_OWNER)) {
		kref_put(&resv_map->refs, resv_map_release);
		set_vma_resv_map(vma, NULL);
	}
	return false;
}

long hugetlb_unreserve_pages(struct inode *inode, long start, long end,
								long freed)
{
	struct hstate *h = hstate_inode(inode);
	struct resv_map *resv_map = inode_resv_map(inode);
	long chg = 0;
	struct hugepage_subpool *spool = subpool_inode(inode);
	long gbl_reserve;

	/*
	 * Since this routine can be called in the evict inode path for all
	 * hugetlbfs inodes, resv_map could be NULL.
	 */
	if (resv_map) {
		chg = region_del(resv_map, start, end);
		/*
		 * region_del() can fail in the rare case where a region
		 * must be split and another region descriptor can not be
		 * allocated.  If end == LONG_MAX, it will not fail.
		 */
		if (chg < 0)
			return chg;
	}

	spin_lock(&inode->i_lock);
	inode->i_blocks -= (blocks_per_huge_page(h) * freed);
	spin_unlock(&inode->i_lock);

	/*
	 * If the subpool has a minimum size, the number of global
	 * reservations to be released may be adjusted.
	 *
	 * Note that !resv_map implies freed == 0. So (chg - freed)
	 * won't go negative.
	 */
	gbl_reserve = hugepage_subpool_put_pages(spool, (chg - freed));
	hugetlb_acct_memory(h, -gbl_reserve);

	return 0;
}

#ifdef CONFIG_HUGETLB_PMD_PAGE_TABLE_SHARING
static unsigned long page_table_shareable(struct vm_area_struct *svma,
				struct vm_area_struct *vma,
				unsigned long addr, pgoff_t idx)
{
	unsigned long saddr = ((idx - svma->vm_pgoff) << PAGE_SHIFT) +
				svma->vm_start;
	unsigned long sbase = saddr & PUD_MASK;
	unsigned long s_end = sbase + PUD_SIZE;

	/* Allow segments to share if only one is marked locked */
	unsigned long vm_flags = vma->vm_flags & ~VM_LOCKED_MASK;
	unsigned long svm_flags = svma->vm_flags & ~VM_LOCKED_MASK;

	/*
	 * match the virtual addresses, permission and the alignment of the
	 * page table page.
	 *
	 * Also, vma_lock (vm_private_data) is required for sharing.
	 */
	if (pmd_index(addr) != pmd_index(saddr) ||
	    vm_flags != svm_flags ||
	    !range_in_vma(svma, sbase, s_end) ||
	    !svma->vm_private_data)
		return 0;

	return saddr;
}

bool want_pmd_share(struct vm_area_struct *vma, unsigned long addr)
{
	unsigned long start = addr & PUD_MASK;
	unsigned long end = start + PUD_SIZE;

#ifdef CONFIG_USERFAULTFD
	if (uffd_disable_huge_pmd_share(vma))
		return false;
#endif
	/*
	 * check on proper vm_flags and page table alignment
	 */
	if (!(vma->vm_flags & VM_MAYSHARE))
		return false;
	if (!vma->vm_private_data)	/* vma lock required for sharing */
		return false;
	if (!range_in_vma(vma, start, end))
		return false;
	return true;
}

/*
 * Determine if start,end range within vma could be mapped by shared pmd.
 * If yes, adjust start and end to cover range associated with possible
 * shared pmd mappings.
 */
void adjust_range_if_pmd_sharing_possible(struct vm_area_struct *vma,
				unsigned long *start, unsigned long *end)
{
	unsigned long v_start = ALIGN(vma->vm_start, PUD_SIZE),
		v_end = ALIGN_DOWN(vma->vm_end, PUD_SIZE);

	/*
	 * vma needs to span at least one aligned PUD size, and the range
	 * must be at least partially within in.
	 */
	if (!(vma->vm_flags & VM_MAYSHARE) || !(v_end > v_start) ||
		(*end <= v_start) || (*start >= v_end))
		return;

	/* Extend the range to be PUD aligned for a worst case scenario */
	if (*start > v_start)
		*start = ALIGN_DOWN(*start, PUD_SIZE);

	if (*end < v_end)
		*end = ALIGN(*end, PUD_SIZE);
}

/*
 * Search for a shareable pmd page for hugetlb. In any case calls pmd_alloc()
 * and returns the corresponding pte. While this is not necessary for the
 * !shared pmd case because we can allocate the pmd later as well, it makes the
 * code much cleaner. pmd allocation is essential for the shared case because
 * pud has to be populated inside the same i_mmap_rwsem section - otherwise
 * racing tasks could either miss the sharing (see huge_pte_offset) or select a
 * bad pmd for sharing.
 */
pte_t *huge_pmd_share(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long addr, pud_t *pud)
{
	struct address_space *mapping = vma->vm_file->f_mapping;
	pgoff_t idx = ((addr - vma->vm_start) >> PAGE_SHIFT) +
			vma->vm_pgoff;
	struct vm_area_struct *svma;
	unsigned long saddr;
	pte_t *spte = NULL;
	pte_t *pte;

	i_mmap_lock_read(mapping);
	vma_interval_tree_foreach(svma, &mapping->i_mmap, idx, idx) {
		if (svma == vma)
			continue;

		saddr = page_table_shareable(svma, vma, addr, idx);
		if (saddr) {
			spte = hugetlb_walk(svma, saddr,
					    vma_mmu_pagesize(svma));
			if (spte) {
				ptdesc_pmd_pts_inc(virt_to_ptdesc(spte));
				break;
			}
		}
	}

	if (!spte)
		goto out;

	spin_lock(&mm->page_table_lock);
	if (pud_none(*pud)) {
		pud_populate(mm, pud,
				(pmd_t *)((unsigned long)spte & PAGE_MASK));
		mm_inc_nr_pmds(mm);
	} else {
		ptdesc_pmd_pts_dec(virt_to_ptdesc(spte));
	}
	spin_unlock(&mm->page_table_lock);
out:
	pte = (pte_t *)pmd_alloc(mm, pud, addr);
	i_mmap_unlock_read(mapping);
	return pte;
}

/*
 * unmap huge page backed by shared pte.
 *
 * Called with page table lock held.
 *
 * returns: 1 successfully unmapped a shared pte page
 *	    0 the underlying pte page is not shared, or it is the last user
 */
int huge_pmd_unshare(struct mm_struct *mm, struct vm_area_struct *vma,
					unsigned long addr, pte_t *ptep)
{
	unsigned long sz = huge_page_size(hstate_vma(vma));
	pgd_t *pgd = pgd_offset(mm, addr);
	p4d_t *p4d = p4d_offset(pgd, addr);
	pud_t *pud = pud_offset(p4d, addr);

	i_mmap_assert_write_locked(vma->vm_file->f_mapping);
	hugetlb_vma_assert_locked(vma);
	if (sz != PMD_SIZE)
		return 0;
	if (!ptdesc_pmd_pts_count(virt_to_ptdesc(ptep)))
		return 0;

	pud_clear(pud);
	ptdesc_pmd_pts_dec(virt_to_ptdesc(ptep));
	mm_dec_nr_pmds(mm);
	return 1;
}

#else /* !CONFIG_HUGETLB_PMD_PAGE_TABLE_SHARING */

pte_t *huge_pmd_share(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long addr, pud_t *pud)
{
	return NULL;
}

int huge_pmd_unshare(struct mm_struct *mm, struct vm_area_struct *vma,
				unsigned long addr, pte_t *ptep)
{
	return 0;
}

void adjust_range_if_pmd_sharing_possible(struct vm_area_struct *vma,
				unsigned long *start, unsigned long *end)
{
}

bool want_pmd_share(struct vm_area_struct *vma, unsigned long addr)
{
	return false;
}
#endif /* CONFIG_HUGETLB_PMD_PAGE_TABLE_SHARING */

#ifdef CONFIG_ARCH_WANT_GENERAL_HUGETLB
pte_t *huge_pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return NULL;
	pud = pud_alloc(mm, p4d, addr);
	if (pud) {
		if (sz == PUD_SIZE) {
			pte = (pte_t *)pud;
		} else {
			BUG_ON(sz != PMD_SIZE);
			if (want_pmd_share(vma, addr) && pud_none(*pud))
				pte = huge_pmd_share(mm, vma, addr, pud);
			else
				pte = (pte_t *)pmd_alloc(mm, pud, addr);
		}
	}

	if (pte) {
		pte_t pteval = ptep_get_lockless(pte);

		BUG_ON(pte_present(pteval) && !pte_huge(pteval));
	}

	return pte;
}

/*
 * huge_pte_offset() - Walk the page table to resolve the hugepage
 * entry at address @addr
 *
 * Return: Pointer to page table entry (PUD or PMD) for
 * address @addr, or NULL if a !p*d_present() entry is encountered and the
 * size @sz doesn't match the hugepage size at this level of the page
 * table.
 */
pte_t *huge_pte_offset(struct mm_struct *mm,
		       unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
		return NULL;
	p4d = p4d_offset(pgd, addr);
	if (!p4d_present(*p4d))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (sz == PUD_SIZE)
		/* must be pud huge, non-present or none */
		return (pte_t *)pud;
	if (!pud_present(*pud))
		return NULL;
	/* must have a valid entry and size to go further */

	pmd = pmd_offset(pud, addr);
	/* must be pmd huge, non-present or none */
	return (pte_t *)pmd;
}

/*
 * Return a mask that can be used to update an address to the last huge
 * page in a page table page mapping size.  Used to skip non-present
 * page table entries when linearly scanning address ranges.  Architectures
 * with unique huge page to page table relationships can define their own
 * version of this routine.
 */
unsigned long hugetlb_mask_last_page(struct hstate *h)
{
	unsigned long hp_size = huge_page_size(h);

	if (hp_size == PUD_SIZE)
		return P4D_SIZE - PUD_SIZE;
	else if (hp_size == PMD_SIZE)
		return PUD_SIZE - PMD_SIZE;
	else
		return 0UL;
}

#else

/* See description above.  Architectures can provide their own version. */
__weak unsigned long hugetlb_mask_last_page(struct hstate *h)
{
#ifdef CONFIG_HUGETLB_PMD_PAGE_TABLE_SHARING
	if (huge_page_size(h) == PMD_SIZE)
		return PUD_SIZE - PMD_SIZE;
#endif
	return 0UL;
}

#endif /* CONFIG_ARCH_WANT_GENERAL_HUGETLB */

/**
 * folio_isolate_hugetlb - try to isolate an allocated hugetlb folio
 * @folio: the folio to isolate
 * @list: the list to add the folio to on success
 *
 * Isolate an allocated (refcount > 0) hugetlb folio, marking it as
 * isolated/non-migratable, and moving it from the active list to the
 * given list.
 *
 * Isolation will fail if @folio is not an allocated hugetlb folio, or if
 * it is already isolated/non-migratable.
 *
 * On success, an additional folio reference is taken that must be dropped
 * using folio_putback_hugetlb() to undo the isolation.
 *
 * Return: True if isolation worked, otherwise False.
 */
bool folio_isolate_hugetlb(struct folio *folio, struct list_head *list)
{
	bool ret = true;

	spin_lock_irq(&hugetlb_lock);
	if (!folio_test_hugetlb(folio) ||
	    !folio_test_hugetlb_migratable(folio) ||
	    !folio_try_get(folio)) {
		ret = false;
		goto unlock;
	}
	folio_clear_hugetlb_migratable(folio);
	list_move_tail(&folio->lru, list);
unlock:
	spin_unlock_irq(&hugetlb_lock);
	return ret;
}

int get_hwpoison_hugetlb_folio(struct folio *folio, bool *hugetlb, bool unpoison)
{
	int ret = 0;

	*hugetlb = false;
	spin_lock_irq(&hugetlb_lock);
	if (folio_test_hugetlb(folio)) {
		*hugetlb = true;
		if (folio_test_hugetlb_freed(folio))
			ret = 0;
		else if (folio_test_hugetlb_migratable(folio) || unpoison)
			ret = folio_try_get(folio);
		else
			ret = -EBUSY;
	}
	spin_unlock_irq(&hugetlb_lock);
	return ret;
}

int get_huge_page_for_hwpoison(unsigned long pfn, int flags,
				bool *migratable_cleared)
{
	int ret;

	spin_lock_irq(&hugetlb_lock);
	ret = __get_huge_page_for_hwpoison(pfn, flags, migratable_cleared);
	spin_unlock_irq(&hugetlb_lock);
	return ret;
}

/**
 * folio_putback_hugetlb - unisolate a hugetlb folio
 * @folio: the isolated hugetlb folio
 *
 * Putback/un-isolate the hugetlb folio that was previous isolated using
 * folio_isolate_hugetlb(): marking it non-isolated/migratable and putting it
 * back onto the active list.
 *
 * Will drop the additional folio reference obtained through
 * folio_isolate_hugetlb().
 */
void folio_putback_hugetlb(struct folio *folio)
{
	spin_lock_irq(&hugetlb_lock);
	folio_set_hugetlb_migratable(folio);
	list_move_tail(&folio->lru, &(folio_hstate(folio))->hugepage_activelist);
	spin_unlock_irq(&hugetlb_lock);
	folio_put(folio);
}

void move_hugetlb_state(struct folio *old_folio, struct folio *new_folio, int reason)
{
	struct hstate *h = folio_hstate(old_folio);

	hugetlb_cgroup_migrate(old_folio, new_folio);
	set_page_owner_migrate_reason(&new_folio->page, reason);

	/*
	 * transfer temporary state of the new hugetlb folio. This is
	 * reverse to other transitions because the newpage is going to
	 * be final while the old one will be freed so it takes over
	 * the temporary status.
	 *
	 * Also note that we have to transfer the per-node surplus state
	 * here as well otherwise the global surplus count will not match
	 * the per-node's.
	 */
	if (folio_test_hugetlb_temporary(new_folio)) {
		int old_nid = folio_nid(old_folio);
		int new_nid = folio_nid(new_folio);

		folio_set_hugetlb_temporary(old_folio);
		folio_clear_hugetlb_temporary(new_folio);


		/*
		 * There is no need to transfer the per-node surplus state
		 * when we do not cross the node.
		 */
		if (new_nid == old_nid)
			return;
		spin_lock_irq(&hugetlb_lock);
		if (h->surplus_huge_pages_node[old_nid]) {
			h->surplus_huge_pages_node[old_nid]--;
			h->surplus_huge_pages_node[new_nid]++;
		}
		spin_unlock_irq(&hugetlb_lock);
	}

	/*
	 * Our old folio is isolated and has "migratable" cleared until it
	 * is putback. As migration succeeded, set the new folio "migratable"
	 * and add it to the active list.
	 */
	spin_lock_irq(&hugetlb_lock);
	folio_set_hugetlb_migratable(new_folio);
	list_move_tail(&new_folio->lru, &(folio_hstate(new_folio))->hugepage_activelist);
	spin_unlock_irq(&hugetlb_lock);
}

static void hugetlb_unshare_pmds(struct vm_area_struct *vma,
				   unsigned long start,
				   unsigned long end)
{
	struct hstate *h = hstate_vma(vma);
	unsigned long sz = huge_page_size(h);
	struct mm_struct *mm = vma->vm_mm;
	struct mmu_notifier_range range;
	unsigned long address;
	spinlock_t *ptl;
	pte_t *ptep;

	if (!(vma->vm_flags & VM_MAYSHARE))
		return;

	if (start >= end)
		return;

	flush_cache_range(vma, start, end);
	/*
	 * No need to call adjust_range_if_pmd_sharing_possible(), because
	 * we have already done the PUD_SIZE alignment.
	 */
	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, mm,
				start, end);
	mmu_notifier_invalidate_range_start(&range);
	hugetlb_vma_lock_write(vma);
	i_mmap_lock_write(vma->vm_file->f_mapping);
	for (address = start; address < end; address += PUD_SIZE) {
		ptep = hugetlb_walk(vma, address, sz);
		if (!ptep)
			continue;
		ptl = huge_pte_lock(h, mm, ptep);
		huge_pmd_unshare(mm, vma, address, ptep);
		spin_unlock(ptl);
	}
	flush_hugetlb_tlb_range(vma, start, end);
	i_mmap_unlock_write(vma->vm_file->f_mapping);
	hugetlb_vma_unlock_write(vma);
	/*
	 * No need to call mmu_notifier_arch_invalidate_secondary_tlbs(), see
	 * Documentation/mm/mmu_notifier.rst.
	 */
	mmu_notifier_invalidate_range_end(&range);
}

/*
 * This function will unconditionally remove all the shared pmd pgtable entries
 * within the specific vma for a hugetlbfs memory range.
 */
void hugetlb_unshare_all_pmds(struct vm_area_struct *vma)
{
	hugetlb_unshare_pmds(vma, ALIGN(vma->vm_start, PUD_SIZE),
			ALIGN_DOWN(vma->vm_end, PUD_SIZE));
}

#ifdef CONFIG_CMA
static bool cma_reserve_called __initdata;

static int __init cmdline_parse_hugetlb_cma(char *p)
{
	int nid, count = 0;
	unsigned long tmp;
	char *s = p;

	while (*s) {
		if (sscanf(s, "%lu%n", &tmp, &count) != 1)
			break;

		if (s[count] == ':') {
			if (tmp >= MAX_NUMNODES)
				break;
			nid = array_index_nospec(tmp, MAX_NUMNODES);

			s += count + 1;
			tmp = memparse(s, &s);
			hugetlb_cma_size_in_node[nid] = tmp;
			hugetlb_cma_size += tmp;

			/*
			 * Skip the separator if have one, otherwise
			 * break the parsing.
			 */
			if (*s == ',')
				s++;
			else
				break;
		} else {
			hugetlb_cma_size = memparse(p, &p);
			break;
		}
	}

	return 0;
}

early_param("hugetlb_cma", cmdline_parse_hugetlb_cma);

static int __init cmdline_parse_hugetlb_cma_only(char *p)
{
	return kstrtobool(p, &hugetlb_cma_only);
}

early_param("hugetlb_cma_only", cmdline_parse_hugetlb_cma_only);

void __init hugetlb_cma_reserve(int order)
{
	unsigned long size, reserved, per_node;
	bool node_specific_cma_alloc = false;
	int nid;

	/*
	 * HugeTLB CMA reservation is required for gigantic
	 * huge pages which could not be allocated via the
	 * page allocator. Just warn if there is any change
	 * breaking this assumption.
	 */
	VM_WARN_ON(order <= MAX_PAGE_ORDER);
	cma_reserve_called = true;

	if (!hugetlb_cma_size)
		return;

	for (nid = 0; nid < MAX_NUMNODES; nid++) {
		if (hugetlb_cma_size_in_node[nid] == 0)
			continue;

		if (!node_online(nid)) {
			pr_warn("hugetlb_cma: invalid node %d specified\n", nid);
			hugetlb_cma_size -= hugetlb_cma_size_in_node[nid];
			hugetlb_cma_size_in_node[nid] = 0;
			continue;
		}

		if (hugetlb_cma_size_in_node[nid] < (PAGE_SIZE << order)) {
			pr_warn("hugetlb_cma: cma area of node %d should be at least %lu MiB\n",
				nid, (PAGE_SIZE << order) / SZ_1M);
			hugetlb_cma_size -= hugetlb_cma_size_in_node[nid];
			hugetlb_cma_size_in_node[nid] = 0;
		} else {
			node_specific_cma_alloc = true;
		}
	}

	/* Validate the CMA size again in case some invalid nodes specified. */
	if (!hugetlb_cma_size)
		return;

	if (hugetlb_cma_size < (PAGE_SIZE << order)) {
		pr_warn("hugetlb_cma: cma area should be at least %lu MiB\n",
			(PAGE_SIZE << order) / SZ_1M);
		hugetlb_cma_size = 0;
		return;
	}

	if (!node_specific_cma_alloc) {
		/*
		 * If 3 GB area is requested on a machine with 4 numa nodes,
		 * let's allocate 1 GB on first three nodes and ignore the last one.
		 */
		per_node = DIV_ROUND_UP(hugetlb_cma_size, nr_online_nodes);
		pr_info("hugetlb_cma: reserve %lu MiB, up to %lu MiB per node\n",
			hugetlb_cma_size / SZ_1M, per_node / SZ_1M);
	}

	reserved = 0;
	for_each_online_node(nid) {
		int res;
		char name[CMA_MAX_NAME];

		if (node_specific_cma_alloc) {
			if (hugetlb_cma_size_in_node[nid] == 0)
				continue;

			size = hugetlb_cma_size_in_node[nid];
		} else {
			size = min(per_node, hugetlb_cma_size - reserved);
		}

		size = round_up(size, PAGE_SIZE << order);

		snprintf(name, sizeof(name), "hugetlb%d", nid);
		/*
		 * Note that 'order per bit' is based on smallest size that
		 * may be returned to CMA allocator in the case of
		 * huge page demotion.
		 */
		res = cma_declare_contiguous_multi(size, PAGE_SIZE << order,
					HUGETLB_PAGE_ORDER, name,
					&hugetlb_cma[nid], nid);
		if (res) {
			pr_warn("hugetlb_cma: reservation failed: err %d, node %d",
				res, nid);
			continue;
		}

		reserved += size;
		pr_info("hugetlb_cma: reserved %lu MiB on node %d\n",
			size / SZ_1M, nid);

		if (reserved >= hugetlb_cma_size)
			break;
	}

	if (!reserved)
		/*
		 * hugetlb_cma_size is used to determine if allocations from
		 * cma are possible.  Set to zero if no cma regions are set up.
		 */
		hugetlb_cma_size = 0;
}

static void __init hugetlb_cma_check(void)
{
	if (!hugetlb_cma_size || cma_reserve_called)
		return;

	pr_warn("hugetlb_cma: the option isn't supported by current arch\n");
}

#endif /* CONFIG_CMA */
