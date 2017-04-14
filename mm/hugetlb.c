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
#include <linux/bootmem.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/page-isolation.h>
#include <linux/jhash.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>

#include <linux/io.h>
#include <linux/hugetlb.h>
#include <linux/hugetlb_cgroup.h>
#include <linux/node.h>
#include <linux/userfaultfd_k.h>
#include "internal.h"

int hugepages_treat_as_movable;

int hugetlb_max_hstate __read_mostly;
unsigned int default_hstate_idx;
struct hstate hstates[HUGE_MAX_HSTATE];
/*
 * Minimum page order among possible hugepage sizes, set to a proper value
 * at boot time.
 */
static unsigned int minimum_order __read_mostly = UINT_MAX;

__initdata LIST_HEAD(huge_boot_pages);

/* for command line parsing */
static struct hstate * __initdata parsed_hstate;
static unsigned long __initdata default_hstate_max_huge_pages;
static unsigned long __initdata default_hstate_size;
static bool __initdata parsed_valid_hugepagesz = true;

/*
 * Protects updates to hugepage_freelists, hugepage_activelist, nr_huge_pages,
 * free_huge_pages, and surplus_huge_pages.
 */
DEFINE_SPINLOCK(hugetlb_lock);

/*
 * Serializes faults on the same logical page.  This is used to
 * prevent spurious OOMs when the hugepage pool is fully utilized.
 */
static int num_fault_mutexes;
struct mutex *hugetlb_fault_mutex_table ____cacheline_aligned_in_smp;

/* Forward declaration */
static int hugetlb_acct_memory(struct hstate *h, long delta);

static inline void unlock_or_release_subpool(struct hugepage_subpool *spool)
{
	bool free = (spool->count == 0) && (spool->used_hpages == 0);

	spin_unlock(&spool->lock);

	/* If no pages are used, and no other handles to the subpool
	 * remain, give up any reservations mased on minimum size and
	 * free the subpool */
	if (free) {
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
	spin_lock(&spool->lock);
	BUG_ON(!spool->count);
	spool->count--;
	unlock_or_release_subpool(spool);
}

/*
 * Subpool accounting for allocating and reserving pages.
 * Return -ENOMEM if there are not enough resources to satisfy the
 * the request.  Otherwise, return the number of pages by which the
 * global pools must be adjusted (upward).  The returned value may
 * only be different than the passed value (delta) in the case where
 * a subpool minimum size must be manitained.
 */
static long hugepage_subpool_get_pages(struct hugepage_subpool *spool,
				      long delta)
{
	long ret = delta;

	if (!spool)
		return ret;

	spin_lock(&spool->lock);

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
	spin_unlock(&spool->lock);
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

	if (!spool)
		return delta;

	spin_lock(&spool->lock);

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
	unlock_or_release_subpool(spool);

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
 * Region tracking -- allows tracking of reservations and instantiated pages
 *                    across the pages in a mapping.
 *
 * The region data structures are embedded into a resv_map and protected
 * by a resv_map's lock.  The set of regions within the resv_map represent
 * reservations for huge pages, or huge pages that have already been
 * instantiated within the map.  The from and to elements are huge page
 * indicies into the associated mapping.  from indicates the starting index
 * of the region.  to represents the first index past the end of  the region.
 *
 * For example, a file region structure with from == 0 and to == 4 represents
 * four huge pages in a mapping.  It is important to note that the to element
 * represents the first element past the end of the region. This is used in
 * arithmetic as 4(to) - 0(from) = 4 huge pages in the region.
 *
 * Interval notation of the form [from, to) will be used to indicate that
 * the endpoint from is inclusive and to is exclusive.
 */
struct file_region {
	struct list_head link;
	long from;
	long to;
};

/*
 * Add the huge page range represented by [f, t) to the reserve
 * map.  In the normal case, existing regions will be expanded
 * to accommodate the specified range.  Sufficient regions should
 * exist for expansion due to the previous call to region_chg
 * with the same range.  However, it is possible that region_del
 * could have been called after region_chg and modifed the map
 * in such a way that no region exists to be expanded.  In this
 * case, pull a region descriptor from the cache associated with
 * the map and use that for the new range.
 *
 * Return the number of new huge pages added to the map.  This
 * number is greater than or equal to zero.
 */
static long region_add(struct resv_map *resv, long f, long t)
{
	struct list_head *head = &resv->regions;
	struct file_region *rg, *nrg, *trg;
	long add = 0;

	spin_lock(&resv->lock);
	/* Locate the region we are either in or before. */
	list_for_each_entry(rg, head, link)
		if (f <= rg->to)
			break;

	/*
	 * If no region exists which can be expanded to include the
	 * specified range, the list must have been modified by an
	 * interleving call to region_del().  Pull a region descriptor
	 * from the cache and use it for this range.
	 */
	if (&rg->link == head || t < rg->from) {
		VM_BUG_ON(resv->region_cache_count <= 0);

		resv->region_cache_count--;
		nrg = list_first_entry(&resv->region_cache, struct file_region,
					link);
		list_del(&nrg->link);

		nrg->from = f;
		nrg->to = t;
		list_add(&nrg->link, rg->link.prev);

		add += t - f;
		goto out_locked;
	}

	/* Round our left edge to the current segment if it encloses us. */
	if (f > rg->from)
		f = rg->from;

	/* Check for and consume any regions we now overlap with. */
	nrg = rg;
	list_for_each_entry_safe(rg, trg, rg->link.prev, link) {
		if (&rg->link == head)
			break;
		if (rg->from > t)
			break;

		/* If this area reaches higher then extend our area to
		 * include it completely.  If this is not the first area
		 * which we intend to reuse, free it. */
		if (rg->to > t)
			t = rg->to;
		if (rg != nrg) {
			/* Decrement return value by the deleted range.
			 * Another range will span this area so that by
			 * end of routine add will be >= zero
			 */
			add -= (rg->to - rg->from);
			list_del(&rg->link);
			kfree(rg);
		}
	}

	add += (nrg->from - f);		/* Added to beginning of region */
	nrg->from = f;
	add += t - nrg->to;		/* Added to end of region */
	nrg->to = t;

out_locked:
	resv->adds_in_progress--;
	spin_unlock(&resv->lock);
	VM_BUG_ON(add < 0);
	return add;
}

/*
 * Examine the existing reserve map and determine how many
 * huge pages in the specified range [f, t) are NOT currently
 * represented.  This routine is called before a subsequent
 * call to region_add that will actually modify the reserve
 * map to add the specified range [f, t).  region_chg does
 * not change the number of huge pages represented by the
 * map.  However, if the existing regions in the map can not
 * be expanded to represent the new range, a new file_region
 * structure is added to the map as a placeholder.  This is
 * so that the subsequent region_add call will have all the
 * regions it needs and will not fail.
 *
 * Upon entry, region_chg will also examine the cache of region descriptors
 * associated with the map.  If there are not enough descriptors cached, one
 * will be allocated for the in progress add operation.
 *
 * Returns the number of huge pages that need to be added to the existing
 * reservation map for the range [f, t).  This number is greater or equal to
 * zero.  -ENOMEM is returned if a new file_region structure or cache entry
 * is needed and can not be allocated.
 */
static long region_chg(struct resv_map *resv, long f, long t)
{
	struct list_head *head = &resv->regions;
	struct file_region *rg, *nrg = NULL;
	long chg = 0;

retry:
	spin_lock(&resv->lock);
retry_locked:
	resv->adds_in_progress++;

	/*
	 * Check for sufficient descriptors in the cache to accommodate
	 * the number of in progress add operations.
	 */
	if (resv->adds_in_progress > resv->region_cache_count) {
		struct file_region *trg;

		VM_BUG_ON(resv->adds_in_progress - resv->region_cache_count > 1);
		/* Must drop lock to allocate a new descriptor. */
		resv->adds_in_progress--;
		spin_unlock(&resv->lock);

		trg = kmalloc(sizeof(*trg), GFP_KERNEL);
		if (!trg) {
			kfree(nrg);
			return -ENOMEM;
		}

		spin_lock(&resv->lock);
		list_add(&trg->link, &resv->region_cache);
		resv->region_cache_count++;
		goto retry_locked;
	}

	/* Locate the region we are before or in. */
	list_for_each_entry(rg, head, link)
		if (f <= rg->to)
			break;

	/* If we are below the current region then a new region is required.
	 * Subtle, allocate a new region at the position but make it zero
	 * size such that we can guarantee to record the reservation. */
	if (&rg->link == head || t < rg->from) {
		if (!nrg) {
			resv->adds_in_progress--;
			spin_unlock(&resv->lock);
			nrg = kmalloc(sizeof(*nrg), GFP_KERNEL);
			if (!nrg)
				return -ENOMEM;

			nrg->from = f;
			nrg->to   = f;
			INIT_LIST_HEAD(&nrg->link);
			goto retry;
		}

		list_add(&nrg->link, rg->link.prev);
		chg = t - f;
		goto out_nrg;
	}

	/* Round our left edge to the current segment if it encloses us. */
	if (f > rg->from)
		f = rg->from;
	chg = t - f;

	/* Check for and consume any regions we now overlap with. */
	list_for_each_entry(rg, rg->link.prev, link) {
		if (&rg->link == head)
			break;
		if (rg->from > t)
			goto out;

		/* We overlap with this area, if it extends further than
		 * us then we must extend ourselves.  Account for its
		 * existing reservation. */
		if (rg->to > t) {
			chg += rg->to - t;
			t = rg->to;
		}
		chg -= rg->to - rg->from;
	}

out:
	spin_unlock(&resv->lock);
	/*  We already know we raced and no longer need the new region */
	kfree(nrg);
	return chg;
out_nrg:
	spin_unlock(&resv->lock);
	return chg;
}

/*
 * Abort the in progress add operation.  The adds_in_progress field
 * of the resv_map keeps track of the operations in progress between
 * calls to region_chg and region_add.  Operations are sometimes
 * aborted after the call to region_chg.  In such cases, region_abort
 * is called to decrement the adds_in_progress counter.
 *
 * NOTE: The range arguments [f, t) are not needed or used in this
 * routine.  They are kept to make reading the calling code easier as
 * arguments will match the associated region_chg call.
 */
static void region_abort(struct resv_map *resv, long f, long t)
{
	spin_lock(&resv->lock);
	VM_BUG_ON(!resv->region_cache_count);
	resv->adds_in_progress--;
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

			/* New entry for end of split region */
			nrg->from = t;
			nrg->to = rg->to;
			INIT_LIST_HEAD(&nrg->link);

			/* Original entry is trimmed */
			rg->to = f;

			list_add(&nrg->link, &rg->link);
			nrg = NULL;
			break;
		}

		if (f <= rg->from && t >= rg->to) { /* Remove entire region */
			del += rg->to - rg->from;
			list_del(&rg->link);
			kfree(rg);
			continue;
		}

		if (f <= rg->from) {	/* Trim beginning of region */
			del += t - rg->from;
			rg->from = t;
		} else {		/* Trim end of region */
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

	rsv_adjust = hugepage_subpool_get_pages(spool, 1);
	if (rsv_adjust) {
		struct hstate *h = hstate_inode(inode);

		hugetlb_acct_memory(h, 1);
	}
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
 * the mapping, in pagecache page units; huge pages here.
 */
static pgoff_t vma_hugecache_offset(struct hstate *h,
			struct vm_area_struct *vma, unsigned long address)
{
	return ((address - vma->vm_start) >> huge_page_shift(h)) +
			(vma->vm_pgoff >> huge_page_order(h));
}

pgoff_t linear_hugepage_index(struct vm_area_struct *vma,
				     unsigned long address)
{
	return vma_hugecache_offset(hstate_vma(vma), vma, address);
}
EXPORT_SYMBOL_GPL(linear_hugepage_index);

/*
 * Return the size of the pages allocated when backing a VMA. In the majority
 * cases this will be same size as used by the page table entries.
 */
unsigned long vma_kernel_pagesize(struct vm_area_struct *vma)
{
	struct hstate *hstate;

	if (!is_vm_hugetlb_page(vma))
		return PAGE_SIZE;

	hstate = hstate_vma(vma);

	return 1UL << huge_page_shift(hstate);
}
EXPORT_SYMBOL_GPL(vma_kernel_pagesize);

/*
 * Return the page size being used by the MMU to back a VMA. In the majority
 * of cases, the page size used by the kernel matches the MMU size. On
 * architectures where it differs, an architecture-specific version of this
 * function is required.
 */
#ifndef vma_mmu_pagesize
unsigned long vma_mmu_pagesize(struct vm_area_struct *vma)
{
	return vma_kernel_pagesize(vma);
}
#endif

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
 * With the exception of reset_vma_resv_huge_pages() which is called at fork(),
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

	resv_map->adds_in_progress = 0;

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
	return inode->i_mapping->private_data;
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

	set_vma_private_data(vma, (get_vma_private_data(vma) &
				HPAGE_RESV_MASK) | (unsigned long)map);
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

/* Reset counters to 0 and clear all HPAGE_RESV_* flags */
void reset_vma_resv_huge_pages(struct vm_area_struct *vma)
{
	VM_BUG_ON_VMA(!is_vm_hugetlb_page(vma), vma);
	if (!(vma->vm_flags & VM_MAYSHARE))
		vma->vm_private_data = (void *)0;
}

/* Returns true if the VMA has associated reserve pages */
static bool vma_has_reserves(struct vm_area_struct *vma, long chg)
{
	if (vma->vm_flags & VM_NORESERVE) {
		/*
		 * This address is already reserved by other process(chg == 0),
		 * so, we should decrement reserved count. Without decrementing,
		 * reserve count remains after releasing inode, because this
		 * allocated page will go into page cache and is regarded as
		 * coming from reserved pool in releasing step.  Currently, we
		 * don't have any other solution to deal with this situation
		 * properly, so add work-around here.
		 */
		if (vma->vm_flags & VM_MAYSHARE && chg == 0)
			return true;
		else
			return false;
	}

	/* Shared mappings always use reserves */
	if (vma->vm_flags & VM_MAYSHARE) {
		/*
		 * We know VM_NORESERVE is not set.  Therefore, there SHOULD
		 * be a region map for all pages.  The only situation where
		 * there is no region map is if a hole was punched via
		 * fallocate.  In this case, there really are no reverves to
		 * use.  This situation is indicated if chg != 0.
		 */
		if (chg)
			return false;
		else
			return true;
	}

	/*
	 * Only the process that called mmap() has reserves for
	 * private mappings.
	 */
	if (is_vma_resv_set(vma, HPAGE_RESV_OWNER)) {
		/*
		 * Like the shared case above, a hole punch or truncate
		 * could have been performed on the private mapping.
		 * Examine the value of chg to determine if reserves
		 * actually exist or were previously consumed.
		 * Very Subtle - The value of chg comes from a previous
		 * call to vma_needs_reserves().  The reserve map for
		 * private mappings has different (opposite) semantics
		 * than that of shared mappings.  vma_needs_reserves()
		 * has already taken this difference in semantics into
		 * account.  Therefore, the meaning of chg is the same
		 * as in the shared case above.  Code could easily be
		 * combined, but keeping it separate draws attention to
		 * subtle differences.
		 */
		if (chg)
			return false;
		else
			return true;
	}

	return false;
}

static void enqueue_huge_page(struct hstate *h, struct page *page)
{
	int nid = page_to_nid(page);
	list_move(&page->lru, &h->hugepage_freelists[nid]);
	h->free_huge_pages++;
	h->free_huge_pages_node[nid]++;
}

static struct page *dequeue_huge_page_node(struct hstate *h, int nid)
{
	struct page *page;

	list_for_each_entry(page, &h->hugepage_freelists[nid], lru)
		if (!is_migrate_isolate_page(page))
			break;
	/*
	 * if 'non-isolated free hugepage' not found on the list,
	 * the allocation fails.
	 */
	if (&h->hugepage_freelists[nid] == &page->lru)
		return NULL;
	list_move(&page->lru, &h->hugepage_activelist);
	set_page_refcounted(page);
	h->free_huge_pages--;
	h->free_huge_pages_node[nid]--;
	return page;
}

/* Movability of hugepages depends on migration support. */
static inline gfp_t htlb_alloc_mask(struct hstate *h)
{
	if (hugepages_treat_as_movable || hugepage_migration_supported(h))
		return GFP_HIGHUSER_MOVABLE;
	else
		return GFP_HIGHUSER;
}

static struct page *dequeue_huge_page_vma(struct hstate *h,
				struct vm_area_struct *vma,
				unsigned long address, int avoid_reserve,
				long chg)
{
	struct page *page = NULL;
	struct mempolicy *mpol;
	nodemask_t *nodemask;
	struct zonelist *zonelist;
	struct zone *zone;
	struct zoneref *z;
	unsigned int cpuset_mems_cookie;

	/*
	 * A child process with MAP_PRIVATE mappings created by their parent
	 * have no page reserves. This check ensures that reservations are
	 * not "stolen". The child may still get SIGKILLed
	 */
	if (!vma_has_reserves(vma, chg) &&
			h->free_huge_pages - h->resv_huge_pages == 0)
		goto err;

	/* If reserves cannot be used, ensure enough pages are in the pool */
	if (avoid_reserve && h->free_huge_pages - h->resv_huge_pages == 0)
		goto err;

retry_cpuset:
	cpuset_mems_cookie = read_mems_allowed_begin();
	zonelist = huge_zonelist(vma, address,
					htlb_alloc_mask(h), &mpol, &nodemask);

	for_each_zone_zonelist_nodemask(zone, z, zonelist,
						MAX_NR_ZONES - 1, nodemask) {
		if (cpuset_zone_allowed(zone, htlb_alloc_mask(h))) {
			page = dequeue_huge_page_node(h, zone_to_nid(zone));
			if (page) {
				if (avoid_reserve)
					break;
				if (!vma_has_reserves(vma, chg))
					break;

				SetPagePrivate(page);
				h->resv_huge_pages--;
				break;
			}
		}
	}

	mpol_cond_put(mpol);
	if (unlikely(!page && read_mems_allowed_retry(cpuset_mems_cookie)))
		goto retry_cpuset;
	return page;

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
static int hstate_next_node_to_alloc(struct hstate *h,
					nodemask_t *nodes_allowed)
{
	int nid;

	VM_BUG_ON(!nodes_allowed);

	nid = get_valid_node_allowed(h->next_nid_to_alloc, nodes_allowed);
	h->next_nid_to_alloc = next_node_allowed(nid, nodes_allowed);

	return nid;
}

/*
 * helper for free_pool_huge_page() - return the previously saved
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

#define for_each_node_mask_to_alloc(hs, nr_nodes, node, mask)		\
	for (nr_nodes = nodes_weight(*mask);				\
		nr_nodes > 0 &&						\
		((node = hstate_next_node_to_alloc(hs, mask)) || 1);	\
		nr_nodes--)

#define for_each_node_mask_to_free(hs, nr_nodes, node, mask)		\
	for (nr_nodes = nodes_weight(*mask);				\
		nr_nodes > 0 &&						\
		((node = hstate_next_node_to_free(hs, mask)) || 1);	\
		nr_nodes--)

#if defined(CONFIG_ARCH_HAS_GIGANTIC_PAGE) && \
	((defined(CONFIG_MEMORY_ISOLATION) && defined(CONFIG_COMPACTION)) || \
	defined(CONFIG_CMA))
static void destroy_compound_gigantic_page(struct page *page,
					unsigned int order)
{
	int i;
	int nr_pages = 1 << order;
	struct page *p = page + 1;

	atomic_set(compound_mapcount_ptr(page), 0);
	for (i = 1; i < nr_pages; i++, p = mem_map_next(p, page, i)) {
		clear_compound_head(p);
		set_page_refcounted(p);
	}

	set_compound_order(page, 0);
	__ClearPageHead(page);
}

static void free_gigantic_page(struct page *page, unsigned int order)
{
	free_contig_range(page_to_pfn(page), 1 << order);
}

static int __alloc_gigantic_page(unsigned long start_pfn,
				unsigned long nr_pages)
{
	unsigned long end_pfn = start_pfn + nr_pages;
	return alloc_contig_range(start_pfn, end_pfn, MIGRATE_MOVABLE,
				  GFP_KERNEL);
}

static bool pfn_range_valid_gigantic(struct zone *z,
			unsigned long start_pfn, unsigned long nr_pages)
{
	unsigned long i, end_pfn = start_pfn + nr_pages;
	struct page *page;

	for (i = start_pfn; i < end_pfn; i++) {
		if (!pfn_valid(i))
			return false;

		page = pfn_to_page(i);

		if (page_zone(page) != z)
			return false;

		if (PageReserved(page))
			return false;

		if (page_count(page) > 0)
			return false;

		if (PageHuge(page))
			return false;
	}

	return true;
}

static bool zone_spans_last_pfn(const struct zone *zone,
			unsigned long start_pfn, unsigned long nr_pages)
{
	unsigned long last_pfn = start_pfn + nr_pages - 1;
	return zone_spans_pfn(zone, last_pfn);
}

static struct page *alloc_gigantic_page(int nid, unsigned int order)
{
	unsigned long nr_pages = 1 << order;
	unsigned long ret, pfn, flags;
	struct zone *z;

	z = NODE_DATA(nid)->node_zones;
	for (; z - NODE_DATA(nid)->node_zones < MAX_NR_ZONES; z++) {
		spin_lock_irqsave(&z->lock, flags);

		pfn = ALIGN(z->zone_start_pfn, nr_pages);
		while (zone_spans_last_pfn(z, pfn, nr_pages)) {
			if (pfn_range_valid_gigantic(z, pfn, nr_pages)) {
				/*
				 * We release the zone lock here because
				 * alloc_contig_range() will also lock the zone
				 * at some point. If there's an allocation
				 * spinning on this lock, it may win the race
				 * and cause alloc_contig_range() to fail...
				 */
				spin_unlock_irqrestore(&z->lock, flags);
				ret = __alloc_gigantic_page(pfn, nr_pages);
				if (!ret)
					return pfn_to_page(pfn);
				spin_lock_irqsave(&z->lock, flags);
			}
			pfn += nr_pages;
		}

		spin_unlock_irqrestore(&z->lock, flags);
	}

	return NULL;
}

static void prep_new_huge_page(struct hstate *h, struct page *page, int nid);
static void prep_compound_gigantic_page(struct page *page, unsigned int order);

static struct page *alloc_fresh_gigantic_page_node(struct hstate *h, int nid)
{
	struct page *page;

	page = alloc_gigantic_page(nid, huge_page_order(h));
	if (page) {
		prep_compound_gigantic_page(page, huge_page_order(h));
		prep_new_huge_page(h, page, nid);
	}

	return page;
}

static int alloc_fresh_gigantic_page(struct hstate *h,
				nodemask_t *nodes_allowed)
{
	struct page *page = NULL;
	int nr_nodes, node;

	for_each_node_mask_to_alloc(h, nr_nodes, node, nodes_allowed) {
		page = alloc_fresh_gigantic_page_node(h, node);
		if (page)
			return 1;
	}

	return 0;
}

static inline bool gigantic_page_supported(void) { return true; }
#else
static inline bool gigantic_page_supported(void) { return false; }
static inline void free_gigantic_page(struct page *page, unsigned int order) { }
static inline void destroy_compound_gigantic_page(struct page *page,
						unsigned int order) { }
static inline int alloc_fresh_gigantic_page(struct hstate *h,
					nodemask_t *nodes_allowed) { return 0; }
#endif

static void update_and_free_page(struct hstate *h, struct page *page)
{
	int i;

	if (hstate_is_gigantic(h) && !gigantic_page_supported())
		return;

	h->nr_huge_pages--;
	h->nr_huge_pages_node[page_to_nid(page)]--;
	for (i = 0; i < pages_per_huge_page(h); i++) {
		page[i].flags &= ~(1 << PG_locked | 1 << PG_error |
				1 << PG_referenced | 1 << PG_dirty |
				1 << PG_active | 1 << PG_private |
				1 << PG_writeback);
	}
	VM_BUG_ON_PAGE(hugetlb_cgroup_from_page(page), page);
	set_compound_page_dtor(page, NULL_COMPOUND_DTOR);
	set_page_refcounted(page);
	if (hstate_is_gigantic(h)) {
		destroy_compound_gigantic_page(page, huge_page_order(h));
		free_gigantic_page(page, huge_page_order(h));
	} else {
		__free_pages(page, huge_page_order(h));
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

/*
 * Test to determine whether the hugepage is "active/in-use" (i.e. being linked
 * to hstate->hugepage_activelist.)
 *
 * This function can be called for tail pages, but never returns true for them.
 */
bool page_huge_active(struct page *page)
{
	VM_BUG_ON_PAGE(!PageHuge(page), page);
	return PageHead(page) && PagePrivate(&page[1]);
}

/* never called for tail page */
static void set_page_huge_active(struct page *page)
{
	VM_BUG_ON_PAGE(!PageHeadHuge(page), page);
	SetPagePrivate(&page[1]);
}

static void clear_page_huge_active(struct page *page)
{
	VM_BUG_ON_PAGE(!PageHeadHuge(page), page);
	ClearPagePrivate(&page[1]);
}

void free_huge_page(struct page *page)
{
	/*
	 * Can't pass hstate in here because it is called from the
	 * compound page destructor.
	 */
	struct hstate *h = page_hstate(page);
	int nid = page_to_nid(page);
	struct hugepage_subpool *spool =
		(struct hugepage_subpool *)page_private(page);
	bool restore_reserve;

	set_page_private(page, 0);
	page->mapping = NULL;
	VM_BUG_ON_PAGE(page_count(page), page);
	VM_BUG_ON_PAGE(page_mapcount(page), page);
	restore_reserve = PagePrivate(page);
	ClearPagePrivate(page);

	/*
	 * A return code of zero implies that the subpool will be under its
	 * minimum size if the reservation is not restored after page is free.
	 * Therefore, force restore_reserve operation.
	 */
	if (hugepage_subpool_put_pages(spool, 1) == 0)
		restore_reserve = true;

	spin_lock(&hugetlb_lock);
	clear_page_huge_active(page);
	hugetlb_cgroup_uncharge_page(hstate_index(h),
				     pages_per_huge_page(h), page);
	if (restore_reserve)
		h->resv_huge_pages++;

	if (h->surplus_huge_pages_node[nid]) {
		/* remove the page from active list */
		list_del(&page->lru);
		update_and_free_page(h, page);
		h->surplus_huge_pages--;
		h->surplus_huge_pages_node[nid]--;
	} else {
		arch_clear_hugepage_flags(page);
		enqueue_huge_page(h, page);
	}
	spin_unlock(&hugetlb_lock);
}

static void prep_new_huge_page(struct hstate *h, struct page *page, int nid)
{
	INIT_LIST_HEAD(&page->lru);
	set_compound_page_dtor(page, HUGETLB_PAGE_DTOR);
	spin_lock(&hugetlb_lock);
	set_hugetlb_cgroup(page, NULL);
	h->nr_huge_pages++;
	h->nr_huge_pages_node[nid]++;
	spin_unlock(&hugetlb_lock);
	put_page(page); /* free it into the hugepage allocator */
}

static void prep_compound_gigantic_page(struct page *page, unsigned int order)
{
	int i;
	int nr_pages = 1 << order;
	struct page *p = page + 1;

	/* we rely on prep_new_huge_page to set the destructor */
	set_compound_order(page, order);
	__ClearPageReserved(page);
	__SetPageHead(page);
	for (i = 1; i < nr_pages; i++, p = mem_map_next(p, page, i)) {
		/*
		 * For gigantic hugepages allocated through bootmem at
		 * boot, it's safer to be consistent with the not-gigantic
		 * hugepages and clear the PG_reserved bit from all tail pages
		 * too.  Otherwse drivers using get_user_pages() to access tail
		 * pages may get the reference counting wrong if they see
		 * PG_reserved set on a tail page (despite the head page not
		 * having PG_reserved set).  Enforcing this consistency between
		 * head and tail pages allows drivers to optimize away a check
		 * on the head page when they need know if put_page() is needed
		 * after get_user_pages().
		 */
		__ClearPageReserved(p);
		set_page_count(p, 0);
		set_compound_head(p, page);
	}
	atomic_set(compound_mapcount_ptr(page), -1);
}

/*
 * PageHuge() only returns true for hugetlbfs pages, but not for normal or
 * transparent huge pages.  See the PageTransHuge() documentation for more
 * details.
 */
int PageHuge(struct page *page)
{
	if (!PageCompound(page))
		return 0;

	page = compound_head(page);
	return page[1].compound_dtor == HUGETLB_PAGE_DTOR;
}
EXPORT_SYMBOL_GPL(PageHuge);

/*
 * PageHeadHuge() only returns true for hugetlbfs head page, but not for
 * normal or transparent huge pages.
 */
int PageHeadHuge(struct page *page_head)
{
	if (!PageHead(page_head))
		return 0;

	return get_compound_page_dtor(page_head) == free_huge_page;
}

pgoff_t __basepage_index(struct page *page)
{
	struct page *page_head = compound_head(page);
	pgoff_t index = page_index(page_head);
	unsigned long compound_idx;

	if (!PageHuge(page_head))
		return page_index(page);

	if (compound_order(page_head) >= MAX_ORDER)
		compound_idx = page_to_pfn(page) - page_to_pfn(page_head);
	else
		compound_idx = page - page_head;

	return (index << compound_order(page_head)) + compound_idx;
}

static struct page *alloc_fresh_huge_page_node(struct hstate *h, int nid)
{
	struct page *page;

	page = __alloc_pages_node(nid,
		htlb_alloc_mask(h)|__GFP_COMP|__GFP_THISNODE|
						__GFP_REPEAT|__GFP_NOWARN,
		huge_page_order(h));
	if (page) {
		prep_new_huge_page(h, page, nid);
	}

	return page;
}

static int alloc_fresh_huge_page(struct hstate *h, nodemask_t *nodes_allowed)
{
	struct page *page;
	int nr_nodes, node;
	int ret = 0;

	for_each_node_mask_to_alloc(h, nr_nodes, node, nodes_allowed) {
		page = alloc_fresh_huge_page_node(h, node);
		if (page) {
			ret = 1;
			break;
		}
	}

	if (ret)
		count_vm_event(HTLB_BUDDY_PGALLOC);
	else
		count_vm_event(HTLB_BUDDY_PGALLOC_FAIL);

	return ret;
}

/*
 * Free huge page from pool from next node to free.
 * Attempt to keep persistent huge pages more or less
 * balanced over allowed nodes.
 * Called with hugetlb_lock locked.
 */
static int free_pool_huge_page(struct hstate *h, nodemask_t *nodes_allowed,
							 bool acct_surplus)
{
	int nr_nodes, node;
	int ret = 0;

	for_each_node_mask_to_free(h, nr_nodes, node, nodes_allowed) {
		/*
		 * If we're returning unused surplus pages, only examine
		 * nodes with surplus pages.
		 */
		if ((!acct_surplus || h->surplus_huge_pages_node[node]) &&
		    !list_empty(&h->hugepage_freelists[node])) {
			struct page *page =
				list_entry(h->hugepage_freelists[node].next,
					  struct page, lru);
			list_del(&page->lru);
			h->free_huge_pages--;
			h->free_huge_pages_node[node]--;
			if (acct_surplus) {
				h->surplus_huge_pages--;
				h->surplus_huge_pages_node[node]--;
			}
			update_and_free_page(h, page);
			ret = 1;
			break;
		}
	}

	return ret;
}

/*
 * Dissolve a given free hugepage into free buddy pages. This function does
 * nothing for in-use (including surplus) hugepages. Returns -EBUSY if the
 * number of free hugepages would be reduced below the number of reserved
 * hugepages.
 */
static int dissolve_free_huge_page(struct page *page)
{
	int rc = 0;

	spin_lock(&hugetlb_lock);
	if (PageHuge(page) && !page_count(page)) {
		struct page *head = compound_head(page);
		struct hstate *h = page_hstate(head);
		int nid = page_to_nid(head);
		if (h->free_huge_pages - h->resv_huge_pages == 0) {
			rc = -EBUSY;
			goto out;
		}
		list_del(&head->lru);
		h->free_huge_pages--;
		h->free_huge_pages_node[nid]--;
		h->max_huge_pages--;
		update_and_free_page(h, head);
	}
out:
	spin_unlock(&hugetlb_lock);
	return rc;
}

/*
 * Dissolve free hugepages in a given pfn range. Used by memory hotplug to
 * make specified memory blocks removable from the system.
 * Note that this will dissolve a free gigantic hugepage completely, if any
 * part of it lies within the given range.
 * Also note that if dissolve_free_huge_page() returns with an error, all
 * free hugepages that were dissolved before that error are lost.
 */
int dissolve_free_huge_pages(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn;
	struct page *page;
	int rc = 0;

	if (!hugepages_supported())
		return rc;

	for (pfn = start_pfn; pfn < end_pfn; pfn += 1 << minimum_order) {
		page = pfn_to_page(pfn);
		if (PageHuge(page) && !page_count(page)) {
			rc = dissolve_free_huge_page(page);
			if (rc)
				break;
		}
	}

	return rc;
}

/*
 * There are 3 ways this can get called:
 * 1. With vma+addr: we use the VMA's memory policy
 * 2. With !vma, but nid=NUMA_NO_NODE:  We try to allocate a huge
 *    page from any node, and let the buddy allocator itself figure
 *    it out.
 * 3. With !vma, but nid!=NUMA_NO_NODE.  We allocate a huge page
 *    strictly from 'nid'
 */
static struct page *__hugetlb_alloc_buddy_huge_page(struct hstate *h,
		struct vm_area_struct *vma, unsigned long addr, int nid)
{
	int order = huge_page_order(h);
	gfp_t gfp = htlb_alloc_mask(h)|__GFP_COMP|__GFP_REPEAT|__GFP_NOWARN;
	unsigned int cpuset_mems_cookie;

	/*
	 * We need a VMA to get a memory policy.  If we do not
	 * have one, we use the 'nid' argument.
	 *
	 * The mempolicy stuff below has some non-inlined bits
	 * and calls ->vm_ops.  That makes it hard to optimize at
	 * compile-time, even when NUMA is off and it does
	 * nothing.  This helps the compiler optimize it out.
	 */
	if (!IS_ENABLED(CONFIG_NUMA) || !vma) {
		/*
		 * If a specific node is requested, make sure to
		 * get memory from there, but only when a node
		 * is explicitly specified.
		 */
		if (nid != NUMA_NO_NODE)
			gfp |= __GFP_THISNODE;
		/*
		 * Make sure to call something that can handle
		 * nid=NUMA_NO_NODE
		 */
		return alloc_pages_node(nid, gfp, order);
	}

	/*
	 * OK, so we have a VMA.  Fetch the mempolicy and try to
	 * allocate a huge page with it.  We will only reach this
	 * when CONFIG_NUMA=y.
	 */
	do {
		struct page *page;
		struct mempolicy *mpol;
		struct zonelist *zl;
		nodemask_t *nodemask;

		cpuset_mems_cookie = read_mems_allowed_begin();
		zl = huge_zonelist(vma, addr, gfp, &mpol, &nodemask);
		mpol_cond_put(mpol);
		page = __alloc_pages_nodemask(gfp, order, zl, nodemask);
		if (page)
			return page;
	} while (read_mems_allowed_retry(cpuset_mems_cookie));

	return NULL;
}

/*
 * There are two ways to allocate a huge page:
 * 1. When you have a VMA and an address (like a fault)
 * 2. When you have no VMA (like when setting /proc/.../nr_hugepages)
 *
 * 'vma' and 'addr' are only for (1).  'nid' is always NUMA_NO_NODE in
 * this case which signifies that the allocation should be done with
 * respect for the VMA's memory policy.
 *
 * For (2), we ignore 'vma' and 'addr' and use 'nid' exclusively. This
 * implies that memory policies will not be taken in to account.
 */
static struct page *__alloc_buddy_huge_page(struct hstate *h,
		struct vm_area_struct *vma, unsigned long addr, int nid)
{
	struct page *page;
	unsigned int r_nid;

	if (hstate_is_gigantic(h))
		return NULL;

	/*
	 * Make sure that anyone specifying 'nid' is not also specifying a VMA.
	 * This makes sure the caller is picking _one_ of the modes with which
	 * we can call this function, not both.
	 */
	if (vma || (addr != -1)) {
		VM_WARN_ON_ONCE(addr == -1);
		VM_WARN_ON_ONCE(nid != NUMA_NO_NODE);
	}
	/*
	 * Assume we will successfully allocate the surplus page to
	 * prevent racing processes from causing the surplus to exceed
	 * overcommit
	 *
	 * This however introduces a different race, where a process B
	 * tries to grow the static hugepage pool while alloc_pages() is
	 * called by process A. B will only examine the per-node
	 * counters in determining if surplus huge pages can be
	 * converted to normal huge pages in adjust_pool_surplus(). A
	 * won't be able to increment the per-node counter, until the
	 * lock is dropped by B, but B doesn't drop hugetlb_lock until
	 * no more huge pages can be converted from surplus to normal
	 * state (and doesn't try to convert again). Thus, we have a
	 * case where a surplus huge page exists, the pool is grown, and
	 * the surplus huge page still exists after, even though it
	 * should just have been converted to a normal huge page. This
	 * does not leak memory, though, as the hugepage will be freed
	 * once it is out of use. It also does not allow the counters to
	 * go out of whack in adjust_pool_surplus() as we don't modify
	 * the node values until we've gotten the hugepage and only the
	 * per-node value is checked there.
	 */
	spin_lock(&hugetlb_lock);
	if (h->surplus_huge_pages >= h->nr_overcommit_huge_pages) {
		spin_unlock(&hugetlb_lock);
		return NULL;
	} else {
		h->nr_huge_pages++;
		h->surplus_huge_pages++;
	}
	spin_unlock(&hugetlb_lock);

	page = __hugetlb_alloc_buddy_huge_page(h, vma, addr, nid);

	spin_lock(&hugetlb_lock);
	if (page) {
		INIT_LIST_HEAD(&page->lru);
		r_nid = page_to_nid(page);
		set_compound_page_dtor(page, HUGETLB_PAGE_DTOR);
		set_hugetlb_cgroup(page, NULL);
		/*
		 * We incremented the global counters already
		 */
		h->nr_huge_pages_node[r_nid]++;
		h->surplus_huge_pages_node[r_nid]++;
		__count_vm_event(HTLB_BUDDY_PGALLOC);
	} else {
		h->nr_huge_pages--;
		h->surplus_huge_pages--;
		__count_vm_event(HTLB_BUDDY_PGALLOC_FAIL);
	}
	spin_unlock(&hugetlb_lock);

	return page;
}

/*
 * Allocate a huge page from 'nid'.  Note, 'nid' may be
 * NUMA_NO_NODE, which means that it may be allocated
 * anywhere.
 */
static
struct page *__alloc_buddy_huge_page_no_mpol(struct hstate *h, int nid)
{
	unsigned long addr = -1;

	return __alloc_buddy_huge_page(h, NULL, addr, nid);
}

/*
 * Use the VMA's mpolicy to allocate a huge page from the buddy.
 */
static
struct page *__alloc_buddy_huge_page_with_mpol(struct hstate *h,
		struct vm_area_struct *vma, unsigned long addr)
{
	return __alloc_buddy_huge_page(h, vma, addr, NUMA_NO_NODE);
}

/*
 * This allocation function is useful in the context where vma is irrelevant.
 * E.g. soft-offlining uses this function because it only cares physical
 * address of error page.
 */
struct page *alloc_huge_page_node(struct hstate *h, int nid)
{
	struct page *page = NULL;

	spin_lock(&hugetlb_lock);
	if (h->free_huge_pages - h->resv_huge_pages > 0)
		page = dequeue_huge_page_node(h, nid);
	spin_unlock(&hugetlb_lock);

	if (!page)
		page = __alloc_buddy_huge_page_no_mpol(h, nid);

	return page;
}

/*
 * Increase the hugetlb pool such that it can accommodate a reservation
 * of size 'delta'.
 */
static int gather_surplus_pages(struct hstate *h, int delta)
{
	struct list_head surplus_list;
	struct page *page, *tmp;
	int ret, i;
	int needed, allocated;
	bool alloc_ok = true;

	needed = (h->resv_huge_pages + delta) - h->free_huge_pages;
	if (needed <= 0) {
		h->resv_huge_pages += delta;
		return 0;
	}

	allocated = 0;
	INIT_LIST_HEAD(&surplus_list);

	ret = -ENOMEM;
retry:
	spin_unlock(&hugetlb_lock);
	for (i = 0; i < needed; i++) {
		page = __alloc_buddy_huge_page_no_mpol(h, NUMA_NO_NODE);
		if (!page) {
			alloc_ok = false;
			break;
		}
		list_add(&page->lru, &surplus_list);
	}
	allocated += i;

	/*
	 * After retaking hugetlb_lock, we need to recalculate 'needed'
	 * because either resv_huge_pages or free_huge_pages may have changed.
	 */
	spin_lock(&hugetlb_lock);
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
	list_for_each_entry_safe(page, tmp, &surplus_list, lru) {
		if ((--needed) < 0)
			break;
		/*
		 * This page is now managed by the hugetlb allocator and has
		 * no users -- drop the buddy allocator's reference.
		 */
		put_page_testzero(page);
		VM_BUG_ON_PAGE(page_count(page), page);
		enqueue_huge_page(h, page);
	}
free:
	spin_unlock(&hugetlb_lock);

	/* Free unnecessary surplus pages to the buddy allocator */
	list_for_each_entry_safe(page, tmp, &surplus_list, lru)
		put_page(page);
	spin_lock(&hugetlb_lock);

	return ret;
}

/*
 * This routine has two main purposes:
 * 1) Decrement the reservation count (resv_huge_pages) by the value passed
 *    in unused_resv_pages.  This corresponds to the prior adjustments made
 *    to the associated reservation map.
 * 2) Free any unused surplus pages that may have been allocated to satisfy
 *    the reservation.  As many as unused_resv_pages may be freed.
 *
 * Called with hugetlb_lock held.  However, the lock could be dropped (and
 * reacquired) during calls to cond_resched_lock.  Whenever dropping the lock,
 * we must make sure nobody else can claim pages we are in the process of
 * freeing.  Do this by ensuring resv_huge_page always is greater than the
 * number of huge pages we plan to free when dropping the lock.
 */
static void return_unused_surplus_pages(struct hstate *h,
					unsigned long unused_resv_pages)
{
	unsigned long nr_pages;

	/* Cannot return gigantic pages currently */
	if (hstate_is_gigantic(h))
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
	 * free_pool_huge_page() will balance the the freed pages across the
	 * on-line nodes with memory and will handle the hstate accounting.
	 *
	 * Note that we decrement resv_huge_pages as we free the pages.  If
	 * we drop the lock, resv_huge_pages will still be sufficiently large
	 * to cover subsequent pages we may free.
	 */
	while (nr_pages--) {
		h->resv_huge_pages--;
		unused_resv_pages--;
		if (!free_pool_huge_page(h, &node_states[N_MEMORY], 1))
			goto out;
		cond_resched_lock(&hugetlb_lock);
	}

out:
	/* Fully uncommit the reservation */
	h->resv_huge_pages -= unused_resv_pages;
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
 */
enum vma_resv_mode {
	VMA_NEEDS_RESV,
	VMA_COMMIT_RESV,
	VMA_END_RESV,
	VMA_ADD_RESV,
};
static long __vma_reservation_common(struct hstate *h,
				struct vm_area_struct *vma, unsigned long addr,
				enum vma_resv_mode mode)
{
	struct resv_map *resv;
	pgoff_t idx;
	long ret;

	resv = vma_resv_map(vma);
	if (!resv)
		return 1;

	idx = vma_hugecache_offset(h, vma, addr);
	switch (mode) {
	case VMA_NEEDS_RESV:
		ret = region_chg(resv, idx, idx + 1);
		break;
	case VMA_COMMIT_RESV:
		ret = region_add(resv, idx, idx + 1);
		break;
	case VMA_END_RESV:
		region_abort(resv, idx, idx + 1);
		ret = 0;
		break;
	case VMA_ADD_RESV:
		if (vma->vm_flags & VM_MAYSHARE)
			ret = region_add(resv, idx, idx + 1);
		else {
			region_abort(resv, idx, idx + 1);
			ret = region_del(resv, idx, idx + 1);
		}
		break;
	default:
		BUG();
	}

	if (vma->vm_flags & VM_MAYSHARE)
		return ret;
	else if (is_vma_resv_set(vma, HPAGE_RESV_OWNER) && ret >= 0) {
		/*
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
		if (ret)
			return 0;
		else
			return 1;
	}
	else
		return ret < 0 ? ret : 0;
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

/*
 * This routine is called to restore a reservation on error paths.  In the
 * specific error paths, a huge page was allocated (via alloc_huge_page)
 * and is about to be freed.  If a reservation for the page existed,
 * alloc_huge_page would have consumed the reservation and set PagePrivate
 * in the newly allocated page.  When the page is freed via free_huge_page,
 * the global reservation count will be incremented if PagePrivate is set.
 * However, free_huge_page can not adjust the reserve map.  Adjust the
 * reserve map here to be consistent with global reserve count adjustments
 * to be made by free_huge_page.
 */
static void restore_reserve_on_error(struct hstate *h,
			struct vm_area_struct *vma, unsigned long address,
			struct page *page)
{
	if (unlikely(PagePrivate(page))) {
		long rc = vma_needs_reservation(h, vma, address);

		if (unlikely(rc < 0)) {
			/*
			 * Rare out of memory condition in reserve map
			 * manipulation.  Clear PagePrivate so that
			 * global reserve count will not be incremented
			 * by free_huge_page.  This will make it appear
			 * as though the reservation for this page was
			 * consumed.  This may prevent the task from
			 * faulting in the page at a later time.  This
			 * is better than inconsistent global huge page
			 * accounting of reserve counts.
			 */
			ClearPagePrivate(page);
		} else if (rc) {
			rc = vma_add_reservation(h, vma, address);
			if (unlikely(rc < 0))
				/*
				 * See above comment about rare out of
				 * memory condition.
				 */
				ClearPagePrivate(page);
		} else
			vma_end_reservation(h, vma, address);
	}
}

struct page *alloc_huge_page(struct vm_area_struct *vma,
				    unsigned long addr, int avoid_reserve)
{
	struct hugepage_subpool *spool = subpool_vma(vma);
	struct hstate *h = hstate_vma(vma);
	struct page *page;
	long map_chg, map_commit;
	long gbl_chg;
	int ret, idx;
	struct hugetlb_cgroup *h_cg;

	idx = hstate_index(h);
	/*
	 * Examine the region/reserve map to determine if the process
	 * has a reservation for the page to be allocated.  A return
	 * code of zero indicates a reservation exists (no change).
	 */
	map_chg = gbl_chg = vma_needs_reservation(h, vma, addr);
	if (map_chg < 0)
		return ERR_PTR(-ENOMEM);

	/*
	 * Processes that did not create the mapping will have no
	 * reserves as indicated by the region/reserve map. Check
	 * that the allocation will not exceed the subpool limit.
	 * Allocations for MAP_NORESERVE mappings also need to be
	 * checked against any subpool limit.
	 */
	if (map_chg || avoid_reserve) {
		gbl_chg = hugepage_subpool_get_pages(spool, 1);
		if (gbl_chg < 0) {
			vma_end_reservation(h, vma, addr);
			return ERR_PTR(-ENOSPC);
		}

		/*
		 * Even though there was no reservation in the region/reserve
		 * map, there could be reservations associated with the
		 * subpool that can be used.  This would be indicated if the
		 * return value of hugepage_subpool_get_pages() is zero.
		 * However, if avoid_reserve is specified we still avoid even
		 * the subpool reservations.
		 */
		if (avoid_reserve)
			gbl_chg = 1;
	}

	ret = hugetlb_cgroup_charge_cgroup(idx, pages_per_huge_page(h), &h_cg);
	if (ret)
		goto out_subpool_put;

	spin_lock(&hugetlb_lock);
	/*
	 * glb_chg is passed to indicate whether or not a page must be taken
	 * from the global free pool (global change).  gbl_chg == 0 indicates
	 * a reservation exists for the allocation.
	 */
	page = dequeue_huge_page_vma(h, vma, addr, avoid_reserve, gbl_chg);
	if (!page) {
		spin_unlock(&hugetlb_lock);
		page = __alloc_buddy_huge_page_with_mpol(h, vma, addr);
		if (!page)
			goto out_uncharge_cgroup;
		if (!avoid_reserve && vma_has_reserves(vma, gbl_chg)) {
			SetPagePrivate(page);
			h->resv_huge_pages--;
		}
		spin_lock(&hugetlb_lock);
		list_move(&page->lru, &h->hugepage_activelist);
		/* Fall through */
	}
	hugetlb_cgroup_commit_charge(idx, pages_per_huge_page(h), h_cg, page);
	spin_unlock(&hugetlb_lock);

	set_page_private(page, (unsigned long)spool);

	map_commit = vma_commit_reservation(h, vma, addr);
	if (unlikely(map_chg > map_commit)) {
		/*
		 * The page was added to the reservation map between
		 * vma_needs_reservation and vma_commit_reservation.
		 * This indicates a race with hugetlb_reserve_pages.
		 * Adjust for the subpool count incremented above AND
		 * in hugetlb_reserve_pages for the same page.  Also,
		 * the reservation count added in hugetlb_reserve_pages
		 * no longer applies.
		 */
		long rsv_adjust;

		rsv_adjust = hugepage_subpool_put_pages(spool, 1);
		hugetlb_acct_memory(h, -rsv_adjust);
	}
	return page;

out_uncharge_cgroup:
	hugetlb_cgroup_uncharge_cgroup(idx, pages_per_huge_page(h), h_cg);
out_subpool_put:
	if (map_chg || avoid_reserve)
		hugepage_subpool_put_pages(spool, 1);
	vma_end_reservation(h, vma, addr);
	return ERR_PTR(-ENOSPC);
}

/*
 * alloc_huge_page()'s wrapper which simply returns the page if allocation
 * succeeds, otherwise NULL. This function is called from new_vma_page(),
 * where no ERR_VALUE is expected to be returned.
 */
struct page *alloc_huge_page_noerr(struct vm_area_struct *vma,
				unsigned long addr, int avoid_reserve)
{
	struct page *page = alloc_huge_page(vma, addr, avoid_reserve);
	if (IS_ERR(page))
		page = NULL;
	return page;
}

int __weak alloc_bootmem_huge_page(struct hstate *h)
{
	struct huge_bootmem_page *m;
	int nr_nodes, node;

	for_each_node_mask_to_alloc(h, nr_nodes, node, &node_states[N_MEMORY]) {
		void *addr;

		addr = memblock_virt_alloc_try_nid_nopanic(
				huge_page_size(h), huge_page_size(h),
				0, BOOTMEM_ALLOC_ACCESSIBLE, node);
		if (addr) {
			/*
			 * Use the beginning of the huge page to store the
			 * huge_bootmem_page struct (until gather_bootmem
			 * puts them into the mem_map).
			 */
			m = addr;
			goto found;
		}
	}
	return 0;

found:
	BUG_ON(!IS_ALIGNED(virt_to_phys(m), huge_page_size(h)));
	/* Put them into a private list first because mem_map is not up yet */
	list_add(&m->list, &huge_boot_pages);
	m->hstate = h;
	return 1;
}

static void __init prep_compound_huge_page(struct page *page,
		unsigned int order)
{
	if (unlikely(order > (MAX_ORDER - 1)))
		prep_compound_gigantic_page(page, order);
	else
		prep_compound_page(page, order);
}

/* Put bootmem huge pages into the standard lists after mem_map is up */
static void __init gather_bootmem_prealloc(void)
{
	struct huge_bootmem_page *m;

	list_for_each_entry(m, &huge_boot_pages, list) {
		struct hstate *h = m->hstate;
		struct page *page;

#ifdef CONFIG_HIGHMEM
		page = pfn_to_page(m->phys >> PAGE_SHIFT);
		memblock_free_late(__pa(m),
				   sizeof(struct huge_bootmem_page));
#else
		page = virt_to_page(m);
#endif
		WARN_ON(page_count(page) != 1);
		prep_compound_huge_page(page, h->order);
		WARN_ON(PageReserved(page));
		prep_new_huge_page(h, page, page_to_nid(page));
		/*
		 * If we had gigantic hugepages allocated at boot time, we need
		 * to restore the 'stolen' pages to totalram_pages in order to
		 * fix confusing memory reports from free(1) and another
		 * side-effects, like CommitLimit going negative.
		 */
		if (hstate_is_gigantic(h))
			adjust_managed_page_count(page, 1 << h->order);
	}
}

static void __init hugetlb_hstate_alloc_pages(struct hstate *h)
{
	unsigned long i;

	for (i = 0; i < h->max_huge_pages; ++i) {
		if (hstate_is_gigantic(h)) {
			if (!alloc_bootmem_huge_page(h))
				break;
		} else if (!alloc_fresh_huge_page(h,
					 &node_states[N_MEMORY]))
			break;
	}
	h->max_huge_pages = i;
}

static void __init hugetlb_init_hstates(void)
{
	struct hstate *h;

	for_each_hstate(h) {
		if (minimum_order > huge_page_order(h))
			minimum_order = huge_page_order(h);

		/* oversize hugepages were init'ed in early boot */
		if (!hstate_is_gigantic(h))
			hugetlb_hstate_alloc_pages(h);
	}
	VM_BUG_ON(minimum_order == UINT_MAX);
}

static char * __init memfmt(char *buf, unsigned long n)
{
	if (n >= (1UL << 30))
		sprintf(buf, "%lu GB", n >> 30);
	else if (n >= (1UL << 20))
		sprintf(buf, "%lu MB", n >> 20);
	else
		sprintf(buf, "%lu KB", n >> 10);
	return buf;
}

static void __init report_hugepages(void)
{
	struct hstate *h;

	for_each_hstate(h) {
		char buf[32];
		pr_info("HugeTLB registered %s page size, pre-allocated %ld pages\n",
			memfmt(buf, huge_page_size(h)),
			h->free_huge_pages);
	}
}

#ifdef CONFIG_HIGHMEM
static void try_to_free_low(struct hstate *h, unsigned long count,
						nodemask_t *nodes_allowed)
{
	int i;

	if (hstate_is_gigantic(h))
		return;

	for_each_node_mask(i, *nodes_allowed) {
		struct page *page, *next;
		struct list_head *freel = &h->hugepage_freelists[i];
		list_for_each_entry_safe(page, next, freel, lru) {
			if (count >= h->nr_huge_pages)
				return;
			if (PageHighMem(page))
				continue;
			list_del(&page->lru);
			update_and_free_page(h, page);
			h->free_huge_pages--;
			h->free_huge_pages_node[page_to_nid(page)]--;
		}
	}
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

	VM_BUG_ON(delta != -1 && delta != 1);

	if (delta < 0) {
		for_each_node_mask_to_alloc(h, nr_nodes, node, nodes_allowed) {
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
static unsigned long set_max_huge_pages(struct hstate *h, unsigned long count,
						nodemask_t *nodes_allowed)
{
	unsigned long min_count, ret;

	if (hstate_is_gigantic(h) && !gigantic_page_supported())
		return h->max_huge_pages;

	/*
	 * Increase the pool size
	 * First take pages out of surplus state.  Then make up the
	 * remaining difference by allocating fresh huge pages.
	 *
	 * We might race with __alloc_buddy_huge_page() here and be unable
	 * to convert a surplus huge page to a normal huge page. That is
	 * not critical, though, it just means the overall size of the
	 * pool might be one hugepage larger than it needs to be, but
	 * within all the constraints specified by the sysctls.
	 */
	spin_lock(&hugetlb_lock);
	while (h->surplus_huge_pages && count > persistent_huge_pages(h)) {
		if (!adjust_pool_surplus(h, nodes_allowed, -1))
			break;
	}

	while (count > persistent_huge_pages(h)) {
		/*
		 * If this allocation races such that we no longer need the
		 * page, free_huge_page will handle it by freeing the page
		 * and reducing the surplus.
		 */
		spin_unlock(&hugetlb_lock);

		/* yield cpu to avoid soft lockup */
		cond_resched();

		if (hstate_is_gigantic(h))
			ret = alloc_fresh_gigantic_page(h, nodes_allowed);
		else
			ret = alloc_fresh_huge_page(h, nodes_allowed);
		spin_lock(&hugetlb_lock);
		if (!ret)
			goto out;

		/* Bail for signals. Probably ctrl-c from user */
		if (signal_pending(current))
			goto out;
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
	 * __alloc_buddy_huge_page() is checking the global counter,
	 * though, we'll note that we're not allowed to exceed surplus
	 * and won't grow the pool anywhere else. Not until one of the
	 * sysctls are changed, or the surplus pages go out of use.
	 */
	min_count = h->resv_huge_pages + h->nr_huge_pages - h->free_huge_pages;
	min_count = max(count, min_count);
	try_to_free_low(h, min_count, nodes_allowed);
	while (min_count < persistent_huge_pages(h)) {
		if (!free_pool_huge_page(h, nodes_allowed, 0))
			break;
		cond_resched_lock(&hugetlb_lock);
	}
	while (count < persistent_huge_pages(h)) {
		if (!adjust_pool_surplus(h, nodes_allowed, 1))
			break;
	}
out:
	ret = persistent_huge_pages(h);
	spin_unlock(&hugetlb_lock);
	return ret;
}

#define HSTATE_ATTR_RO(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RO(_name)

#define HSTATE_ATTR(_name) \
	static struct kobj_attribute _name##_attr = \
		__ATTR(_name, 0644, _name##_show, _name##_store)

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

	return sprintf(buf, "%lu\n", nr_huge_pages);
}

static ssize_t __nr_hugepages_store_common(bool obey_mempolicy,
					   struct hstate *h, int nid,
					   unsigned long count, size_t len)
{
	int err;
	NODEMASK_ALLOC(nodemask_t, nodes_allowed, GFP_KERNEL | __GFP_NORETRY);

	if (hstate_is_gigantic(h) && !gigantic_page_supported()) {
		err = -EINVAL;
		goto out;
	}

	if (nid == NUMA_NO_NODE) {
		/*
		 * global hstate attribute
		 */
		if (!(obey_mempolicy &&
				init_nodemask_of_mempolicy(nodes_allowed))) {
			NODEMASK_FREE(nodes_allowed);
			nodes_allowed = &node_states[N_MEMORY];
		}
	} else if (nodes_allowed) {
		/*
		 * per node hstate attribute: adjust count to global,
		 * but restrict alloc/free to the specified node.
		 */
		count += h->nr_huge_pages - h->nr_huge_pages_node[nid];
		init_nodemask_of_node(nodes_allowed, nid);
	} else
		nodes_allowed = &node_states[N_MEMORY];

	h->max_huge_pages = set_max_huge_pages(h, count, nodes_allowed);

	if (nodes_allowed != &node_states[N_MEMORY])
		NODEMASK_FREE(nodes_allowed);

	return len;
out:
	NODEMASK_FREE(nodes_allowed);
	return err;
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
				       struct kobj_attribute *attr, char *buf)
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
	return sprintf(buf, "%lu\n", h->nr_overcommit_huge_pages);
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

	spin_lock(&hugetlb_lock);
	h->nr_overcommit_huge_pages = input;
	spin_unlock(&hugetlb_lock);

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

	return sprintf(buf, "%lu\n", free_huge_pages);
}
HSTATE_ATTR_RO(free_hugepages);

static ssize_t resv_hugepages_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct hstate *h = kobj_to_hstate(kobj, NULL);
	return sprintf(buf, "%lu\n", h->resv_huge_pages);
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

	return sprintf(buf, "%lu\n", surplus_huge_pages);
}
HSTATE_ATTR_RO(surplus_hugepages);

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

static struct attribute_group hstate_attr_group = {
	.attrs = hstate_attrs,
};

static int hugetlb_sysfs_add_hstate(struct hstate *h, struct kobject *parent,
				    struct kobject **hstate_kobjs,
				    struct attribute_group *hstate_attr_group)
{
	int retval;
	int hi = hstate_index(h);

	hstate_kobjs[hi] = kobject_create_and_add(h->name, parent);
	if (!hstate_kobjs[hi])
		return -ENOMEM;

	retval = sysfs_create_group(hstate_kobjs[hi], hstate_attr_group);
	if (retval)
		kobject_put(hstate_kobjs[hi]);

	return retval;
}

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
			pr_err("Hugetlb: Unable to add hstate %s", h->name);
	}
}

#ifdef CONFIG_NUMA

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

static struct attribute_group per_node_hstate_attr_group = {
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
static void hugetlb_unregister_node(struct node *node)
{
	struct hstate *h;
	struct node_hstate *nhs = &node_hstates[node->dev.id];

	if (!nhs->hugepages_kobj)
		return;		/* no hstate attributes */

	for_each_hstate(h) {
		int idx = hstate_index(h);
		if (nhs->hstate_kobjs[idx]) {
			kobject_put(nhs->hstate_kobjs[idx]);
			nhs->hstate_kobjs[idx] = NULL;
		}
	}

	kobject_put(nhs->hugepages_kobj);
	nhs->hugepages_kobj = NULL;
}


/*
 * Register hstate attributes for a single node device.
 * No-op if attributes already registered.
 */
static void hugetlb_register_node(struct node *node)
{
	struct hstate *h;
	struct node_hstate *nhs = &node_hstates[node->dev.id];
	int err;

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
			pr_err("Hugetlb: Unable to add hstate %s for node %d\n",
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

	for_each_node_state(nid, N_MEMORY) {
		struct node *node = node_devices[nid];
		if (node->dev.id == nid)
			hugetlb_register_node(node);
	}

	/*
	 * Let the node device driver know we're here so it can
	 * [un]register hstate attributes on node hotplug.
	 */
	register_hugetlbfs_with_node(hugetlb_register_node,
				     hugetlb_unregister_node);
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

static int __init hugetlb_init(void)
{
	int i;

	if (!hugepages_supported())
		return 0;

	if (!size_to_hstate(default_hstate_size)) {
		default_hstate_size = HPAGE_SIZE;
		if (!size_to_hstate(default_hstate_size))
			hugetlb_add_hstate(HUGETLB_PAGE_ORDER);
	}
	default_hstate_idx = hstate_index(size_to_hstate(default_hstate_size));
	if (default_hstate_max_huge_pages) {
		if (!default_hstate.max_huge_pages)
			default_hstate.max_huge_pages = default_hstate_max_huge_pages;
	}

	hugetlb_init_hstates();
	gather_bootmem_prealloc();
	report_hugepages();

	hugetlb_sysfs_init();
	hugetlb_register_all_nodes();
	hugetlb_cgroup_file_init();

#ifdef CONFIG_SMP
	num_fault_mutexes = roundup_pow_of_two(8 * num_possible_cpus());
#else
	num_fault_mutexes = 1;
#endif
	hugetlb_fault_mutex_table =
		kmalloc(sizeof(struct mutex) * num_fault_mutexes, GFP_KERNEL);
	BUG_ON(!hugetlb_fault_mutex_table);

	for (i = 0; i < num_fault_mutexes; i++)
		mutex_init(&hugetlb_fault_mutex_table[i]);
	return 0;
}
subsys_initcall(hugetlb_init);

/* Should be called on processing a hugepagesz=... option */
void __init hugetlb_bad_size(void)
{
	parsed_valid_hugepagesz = false;
}

void __init hugetlb_add_hstate(unsigned int order)
{
	struct hstate *h;
	unsigned long i;

	if (size_to_hstate(PAGE_SIZE << order)) {
		pr_warn("hugepagesz= specified twice, ignoring\n");
		return;
	}
	BUG_ON(hugetlb_max_hstate >= HUGE_MAX_HSTATE);
	BUG_ON(order == 0);
	h = &hstates[hugetlb_max_hstate++];
	h->order = order;
	h->mask = ~((1ULL << (order + PAGE_SHIFT)) - 1);
	h->nr_huge_pages = 0;
	h->free_huge_pages = 0;
	for (i = 0; i < MAX_NUMNODES; ++i)
		INIT_LIST_HEAD(&h->hugepage_freelists[i]);
	INIT_LIST_HEAD(&h->hugepage_activelist);
	h->next_nid_to_alloc = first_memory_node;
	h->next_nid_to_free = first_memory_node;
	snprintf(h->name, HSTATE_NAME_LEN, "hugepages-%lukB",
					huge_page_size(h)/1024);

	parsed_hstate = h;
}

static int __init hugetlb_nrpages_setup(char *s)
{
	unsigned long *mhp;
	static unsigned long *last_mhp;

	if (!parsed_valid_hugepagesz) {
		pr_warn("hugepages = %s preceded by "
			"an unsupported hugepagesz, ignoring\n", s);
		parsed_valid_hugepagesz = true;
		return 1;
	}
	/*
	 * !hugetlb_max_hstate means we haven't parsed a hugepagesz= parameter yet,
	 * so this hugepages= parameter goes to the "default hstate".
	 */
	else if (!hugetlb_max_hstate)
		mhp = &default_hstate_max_huge_pages;
	else
		mhp = &parsed_hstate->max_huge_pages;

	if (mhp == last_mhp) {
		pr_warn("hugepages= specified twice without interleaving hugepagesz=, ignoring\n");
		return 1;
	}

	if (sscanf(s, "%lu", mhp) <= 0)
		*mhp = 0;

	/*
	 * Global state is always initialized later in hugetlb_init.
	 * But we need to allocate >= MAX_ORDER hstates here early to still
	 * use the bootmem allocator.
	 */
	if (hugetlb_max_hstate && parsed_hstate->order >= MAX_ORDER)
		hugetlb_hstate_alloc_pages(parsed_hstate);

	last_mhp = mhp;

	return 1;
}
__setup("hugepages=", hugetlb_nrpages_setup);

static int __init hugetlb_default_setup(char *s)
{
	default_hstate_size = memparse(s, &s);
	return 1;
}
__setup("default_hugepagesz=", hugetlb_default_setup);

static unsigned int cpuset_mems_nr(unsigned int *array)
{
	int node;
	unsigned int nr = 0;

	for_each_node_mask(node, cpuset_current_mems_allowed)
		nr += array[node];

	return nr;
}

#ifdef CONFIG_SYSCTL
static int hugetlb_sysctl_handler_common(bool obey_mempolicy,
			 struct ctl_table *table, int write,
			 void __user *buffer, size_t *length, loff_t *ppos)
{
	struct hstate *h = &default_hstate;
	unsigned long tmp = h->max_huge_pages;
	int ret;

	if (!hugepages_supported())
		return -EOPNOTSUPP;

	table->data = &tmp;
	table->maxlen = sizeof(unsigned long);
	ret = proc_doulongvec_minmax(table, write, buffer, length, ppos);
	if (ret)
		goto out;

	if (write)
		ret = __nr_hugepages_store_common(obey_mempolicy, h,
						  NUMA_NO_NODE, tmp, *length);
out:
	return ret;
}

int hugetlb_sysctl_handler(struct ctl_table *table, int write,
			  void __user *buffer, size_t *length, loff_t *ppos)
{

	return hugetlb_sysctl_handler_common(false, table, write,
							buffer, length, ppos);
}

#ifdef CONFIG_NUMA
int hugetlb_mempolicy_sysctl_handler(struct ctl_table *table, int write,
			  void __user *buffer, size_t *length, loff_t *ppos)
{
	return hugetlb_sysctl_handler_common(true, table, write,
							buffer, length, ppos);
}
#endif /* CONFIG_NUMA */

int hugetlb_overcommit_handler(struct ctl_table *table, int write,
			void __user *buffer,
			size_t *length, loff_t *ppos)
{
	struct hstate *h = &default_hstate;
	unsigned long tmp;
	int ret;

	if (!hugepages_supported())
		return -EOPNOTSUPP;

	tmp = h->nr_overcommit_huge_pages;

	if (write && hstate_is_gigantic(h))
		return -EINVAL;

	table->data = &tmp;
	table->maxlen = sizeof(unsigned long);
	ret = proc_doulongvec_minmax(table, write, buffer, length, ppos);
	if (ret)
		goto out;

	if (write) {
		spin_lock(&hugetlb_lock);
		h->nr_overcommit_huge_pages = tmp;
		spin_unlock(&hugetlb_lock);
	}
out:
	return ret;
}

#endif /* CONFIG_SYSCTL */

void hugetlb_report_meminfo(struct seq_file *m)
{
	struct hstate *h = &default_hstate;
	if (!hugepages_supported())
		return;
	seq_printf(m,
			"HugePages_Total:   %5lu\n"
			"HugePages_Free:    %5lu\n"
			"HugePages_Rsvd:    %5lu\n"
			"HugePages_Surp:    %5lu\n"
			"Hugepagesize:   %8lu kB\n",
			h->nr_huge_pages,
			h->free_huge_pages,
			h->resv_huge_pages,
			h->surplus_huge_pages,
			1UL << (huge_page_order(h) + PAGE_SHIFT - 10));
}

int hugetlb_report_node_meminfo(int nid, char *buf)
{
	struct hstate *h = &default_hstate;
	if (!hugepages_supported())
		return 0;
	return sprintf(buf,
		"Node %d HugePages_Total: %5u\n"
		"Node %d HugePages_Free:  %5u\n"
		"Node %d HugePages_Surp:  %5u\n",
		nid, h->nr_huge_pages_node[nid],
		nid, h->free_huge_pages_node[nid],
		nid, h->surplus_huge_pages_node[nid]);
}

void hugetlb_show_meminfo(void)
{
	struct hstate *h;
	int nid;

	if (!hugepages_supported())
		return;

	for_each_node_state(nid, N_MEMORY)
		for_each_hstate(h)
			pr_info("Node %d hugepages_total=%u hugepages_free=%u hugepages_surp=%u hugepages_size=%lukB\n",
				nid,
				h->nr_huge_pages_node[nid],
				h->free_huge_pages_node[nid],
				h->surplus_huge_pages_node[nid],
				1UL << (huge_page_order(h) + PAGE_SHIFT - 10));
}

void hugetlb_report_usage(struct seq_file *m, struct mm_struct *mm)
{
	seq_printf(m, "HugetlbPages:\t%8lu kB\n",
		   atomic_long_read(&mm->hugetlb_usage) << (PAGE_SHIFT - 10));
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

	spin_lock(&hugetlb_lock);
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
	 */
	if (delta > 0) {
		if (gather_surplus_pages(h, delta) < 0)
			goto out;

		if (delta > cpuset_mems_nr(h->free_huge_pages_node)) {
			return_unused_surplus_pages(h, delta);
			goto out;
		}
	}

	ret = 0;
	if (delta < 0)
		return_unused_surplus_pages(h, (unsigned long) -delta);

out:
	spin_unlock(&hugetlb_lock);
	return ret;
}

static void hugetlb_vm_op_open(struct vm_area_struct *vma)
{
	struct resv_map *resv = vma_resv_map(vma);

	/*
	 * This new VMA should share its siblings reservation map if present.
	 * The VMA will only ever have a valid reservation map pointer where
	 * it is being copied for another still existing VMA.  As that VMA
	 * has a reference to the reservation map it cannot disappear until
	 * after this open call completes.  It is therefore safe to take a
	 * new reference here without additional locking.
	 */
	if (resv && is_vma_resv_set(vma, HPAGE_RESV_OWNER))
		kref_get(&resv->refs);
}

static void hugetlb_vm_op_close(struct vm_area_struct *vma)
{
	struct hstate *h = hstate_vma(vma);
	struct resv_map *resv = vma_resv_map(vma);
	struct hugepage_subpool *spool = subpool_vma(vma);
	unsigned long reserve, start, end;
	long gbl_reserve;

	if (!resv || !is_vma_resv_set(vma, HPAGE_RESV_OWNER))
		return;

	start = vma_hugecache_offset(h, vma, vma->vm_start);
	end = vma_hugecache_offset(h, vma, vma->vm_end);

	reserve = (end - start) - region_count(resv, start, end);

	kref_put(&resv->refs, resv_map_release);

	if (reserve) {
		/*
		 * Decrement reserve counts.  The global reserve count may be
		 * adjusted if the subpool has a minimum size.
		 */
		gbl_reserve = hugepage_subpool_put_pages(spool, reserve);
		hugetlb_acct_memory(h, -gbl_reserve);
	}
}

/*
 * We cannot handle pagefaults against hugetlb pages at all.  They cause
 * handle_mm_fault() to try to instantiate regular-sized pages in the
 * hugegpage VMA.  do_page_fault() is supposed to trap this, so BUG is we get
 * this far.
 */
static int hugetlb_vm_op_fault(struct vm_fault *vmf)
{
	BUG();
	return 0;
}

const struct vm_operations_struct hugetlb_vm_ops = {
	.fault = hugetlb_vm_op_fault,
	.open = hugetlb_vm_op_open,
	.close = hugetlb_vm_op_close,
};

static pte_t make_huge_pte(struct vm_area_struct *vma, struct page *page,
				int writable)
{
	pte_t entry;

	if (writable) {
		entry = huge_pte_mkwrite(huge_pte_mkdirty(mk_huge_pte(page,
					 vma->vm_page_prot)));
	} else {
		entry = huge_pte_wrprotect(mk_huge_pte(page,
					   vma->vm_page_prot));
	}
	entry = pte_mkyoung(entry);
	entry = pte_mkhuge(entry);
	entry = arch_make_huge_pte(entry, vma, page, writable);

	return entry;
}

static void set_huge_ptep_writable(struct vm_area_struct *vma,
				   unsigned long address, pte_t *ptep)
{
	pte_t entry;

	entry = huge_pte_mkwrite(huge_pte_mkdirty(huge_ptep_get(ptep)));
	if (huge_ptep_set_access_flags(vma, address, ptep, entry, 1))
		update_mmu_cache(vma, address, ptep);
}

static int is_hugetlb_entry_migration(pte_t pte)
{
	swp_entry_t swp;

	if (huge_pte_none(pte) || pte_present(pte))
		return 0;
	swp = pte_to_swp_entry(pte);
	if (non_swap_entry(swp) && is_migration_entry(swp))
		return 1;
	else
		return 0;
}

static int is_hugetlb_entry_hwpoisoned(pte_t pte)
{
	swp_entry_t swp;

	if (huge_pte_none(pte) || pte_present(pte))
		return 0;
	swp = pte_to_swp_entry(pte);
	if (non_swap_entry(swp) && is_hwpoison_entry(swp))
		return 1;
	else
		return 0;
}

int copy_hugetlb_page_range(struct mm_struct *dst, struct mm_struct *src,
			    struct vm_area_struct *vma)
{
	pte_t *src_pte, *dst_pte, entry;
	struct page *ptepage;
	unsigned long addr;
	int cow;
	struct hstate *h = hstate_vma(vma);
	unsigned long sz = huge_page_size(h);
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */
	int ret = 0;

	cow = (vma->vm_flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;

	mmun_start = vma->vm_start;
	mmun_end = vma->vm_end;
	if (cow)
		mmu_notifier_invalidate_range_start(src, mmun_start, mmun_end);

	for (addr = vma->vm_start; addr < vma->vm_end; addr += sz) {
		spinlock_t *src_ptl, *dst_ptl;
		src_pte = huge_pte_offset(src, addr);
		if (!src_pte)
			continue;
		dst_pte = huge_pte_alloc(dst, addr, sz);
		if (!dst_pte) {
			ret = -ENOMEM;
			break;
		}

		/* If the pagetables are shared don't copy or take references */
		if (dst_pte == src_pte)
			continue;

		dst_ptl = huge_pte_lock(h, dst, dst_pte);
		src_ptl = huge_pte_lockptr(h, src, src_pte);
		spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);
		entry = huge_ptep_get(src_pte);
		if (huge_pte_none(entry)) { /* skip none entry */
			;
		} else if (unlikely(is_hugetlb_entry_migration(entry) ||
				    is_hugetlb_entry_hwpoisoned(entry))) {
			swp_entry_t swp_entry = pte_to_swp_entry(entry);

			if (is_write_migration_entry(swp_entry) && cow) {
				/*
				 * COW mappings require pages in both
				 * parent and child to be set to read.
				 */
				make_migration_entry_read(&swp_entry);
				entry = swp_entry_to_pte(swp_entry);
				set_huge_pte_at(src, addr, src_pte, entry);
			}
			set_huge_pte_at(dst, addr, dst_pte, entry);
		} else {
			if (cow) {
				huge_ptep_set_wrprotect(src, addr, src_pte);
				mmu_notifier_invalidate_range(src, mmun_start,
								   mmun_end);
			}
			entry = huge_ptep_get(src_pte);
			ptepage = pte_page(entry);
			get_page(ptepage);
			page_dup_rmap(ptepage, true);
			set_huge_pte_at(dst, addr, dst_pte, entry);
			hugetlb_count_add(pages_per_huge_page(h), dst);
		}
		spin_unlock(src_ptl);
		spin_unlock(dst_ptl);
	}

	if (cow)
		mmu_notifier_invalidate_range_end(src, mmun_start, mmun_end);

	return ret;
}

void __unmap_hugepage_range(struct mmu_gather *tlb, struct vm_area_struct *vma,
			    unsigned long start, unsigned long end,
			    struct page *ref_page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address;
	pte_t *ptep;
	pte_t pte;
	spinlock_t *ptl;
	struct page *page;
	struct hstate *h = hstate_vma(vma);
	unsigned long sz = huge_page_size(h);
	const unsigned long mmun_start = start;	/* For mmu_notifiers */
	const unsigned long mmun_end   = end;	/* For mmu_notifiers */

	WARN_ON(!is_vm_hugetlb_page(vma));
	BUG_ON(start & ~huge_page_mask(h));
	BUG_ON(end & ~huge_page_mask(h));

	/*
	 * This is a hugetlb vma, all the pte entries should point
	 * to huge page.
	 */
	tlb_remove_check_page_size_change(tlb, sz);
	tlb_start_vma(tlb, vma);
	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);
	address = start;
	for (; address < end; address += sz) {
		ptep = huge_pte_offset(mm, address);
		if (!ptep)
			continue;

		ptl = huge_pte_lock(h, mm, ptep);
		if (huge_pmd_unshare(mm, &address, ptep)) {
			spin_unlock(ptl);
			continue;
		}

		pte = huge_ptep_get(ptep);
		if (huge_pte_none(pte)) {
			spin_unlock(ptl);
			continue;
		}

		/*
		 * Migrating hugepage or HWPoisoned hugepage is already
		 * unmapped and its refcount is dropped, so just clear pte here.
		 */
		if (unlikely(!pte_present(pte))) {
			huge_pte_clear(mm, address, ptep);
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

		pte = huge_ptep_get_and_clear(mm, address, ptep);
		tlb_remove_huge_tlb_entry(h, tlb, ptep, address);
		if (huge_pte_dirty(pte))
			set_page_dirty(page);

		hugetlb_count_sub(pages_per_huge_page(h), mm);
		page_remove_rmap(page, true);

		spin_unlock(ptl);
		tlb_remove_page_size(tlb, page, huge_page_size(h));
		/*
		 * Bail out after unmapping reference page if supplied
		 */
		if (ref_page)
			break;
	}
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
	tlb_end_vma(tlb, vma);
}

void __unmap_hugepage_range_final(struct mmu_gather *tlb,
			  struct vm_area_struct *vma, unsigned long start,
			  unsigned long end, struct page *ref_page)
{
	__unmap_hugepage_range(tlb, vma, start, end, ref_page);

	/*
	 * Clear this flag so that x86's huge_pmd_share page_table_shareable
	 * test will fail on a vma being torn down, and not grab a page table
	 * on its way out.  We're lucky that the flag has such an appropriate
	 * name, and can in fact be safely cleared here. We could clear it
	 * before the __unmap_hugepage_range above, but all that's necessary
	 * is to clear it before releasing the i_mmap_rwsem. This works
	 * because in the context this is called, the VMA is about to be
	 * destroyed and the i_mmap_rwsem is held.
	 */
	vma->vm_flags &= ~VM_MAYSHARE;
}

void unmap_hugepage_range(struct vm_area_struct *vma, unsigned long start,
			  unsigned long end, struct page *ref_page)
{
	struct mm_struct *mm;
	struct mmu_gather tlb;

	mm = vma->vm_mm;

	tlb_gather_mmu(&tlb, mm, start, end);
	__unmap_hugepage_range(&tlb, vma, start, end, ref_page);
	tlb_finish_mmu(&tlb, start, end);
}

/*
 * This is called when the original mapper is failing to COW a MAP_PRIVATE
 * mappping it owns the reserve page for. The intention is to unmap the page
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
					     address + huge_page_size(h), page);
	}
	i_mmap_unlock_write(mapping);
}

/*
 * Hugetlb_cow() should be called with page lock of the original hugepage held.
 * Called with hugetlb_instantiation_mutex held and pte_page locked so we
 * cannot race with other handlers or page migration.
 * Keep the pte_same checks anyway to make transition from the mutex easier.
 */
static int hugetlb_cow(struct mm_struct *mm, struct vm_area_struct *vma,
		       unsigned long address, pte_t *ptep,
		       struct page *pagecache_page, spinlock_t *ptl)
{
	pte_t pte;
	struct hstate *h = hstate_vma(vma);
	struct page *old_page, *new_page;
	int ret = 0, outside_reserve = 0;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */

	pte = huge_ptep_get(ptep);
	old_page = pte_page(pte);

retry_avoidcopy:
	/* If no-one else is actually using this page, avoid the copy
	 * and just make the page writable */
	if (page_mapcount(old_page) == 1 && PageAnon(old_page)) {
		page_move_anon_rmap(old_page, vma);
		set_huge_ptep_writable(vma, address, ptep);
		return 0;
	}

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
			old_page != pagecache_page)
		outside_reserve = 1;

	get_page(old_page);

	/*
	 * Drop page table lock as buddy allocator may be called. It will
	 * be acquired again before returning to the caller, as expected.
	 */
	spin_unlock(ptl);
	new_page = alloc_huge_page(vma, address, outside_reserve);

	if (IS_ERR(new_page)) {
		/*
		 * If a process owning a MAP_PRIVATE mapping fails to COW,
		 * it is due to references held by a child and an insufficient
		 * huge page pool. To guarantee the original mappers
		 * reliability, unmap the page from child processes. The child
		 * may get SIGKILLed if it later faults.
		 */
		if (outside_reserve) {
			put_page(old_page);
			BUG_ON(huge_pte_none(pte));
			unmap_ref_private(mm, vma, old_page, address);
			BUG_ON(huge_pte_none(pte));
			spin_lock(ptl);
			ptep = huge_pte_offset(mm, address & huge_page_mask(h));
			if (likely(ptep &&
				   pte_same(huge_ptep_get(ptep), pte)))
				goto retry_avoidcopy;
			/*
			 * race occurs while re-acquiring page table
			 * lock, and our job is done.
			 */
			return 0;
		}

		ret = (PTR_ERR(new_page) == -ENOMEM) ?
			VM_FAULT_OOM : VM_FAULT_SIGBUS;
		goto out_release_old;
	}

	/*
	 * When the original hugepage is shared one, it does not have
	 * anon_vma prepared.
	 */
	if (unlikely(anon_vma_prepare(vma))) {
		ret = VM_FAULT_OOM;
		goto out_release_all;
	}

	copy_user_huge_page(new_page, old_page, address, vma,
			    pages_per_huge_page(h));
	__SetPageUptodate(new_page);
	set_page_huge_active(new_page);

	mmun_start = address & huge_page_mask(h);
	mmun_end = mmun_start + huge_page_size(h);
	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);

	/*
	 * Retake the page table lock to check for racing updates
	 * before the page tables are altered
	 */
	spin_lock(ptl);
	ptep = huge_pte_offset(mm, address & huge_page_mask(h));
	if (likely(ptep && pte_same(huge_ptep_get(ptep), pte))) {
		ClearPagePrivate(new_page);

		/* Break COW */
		huge_ptep_clear_flush(vma, address, ptep);
		mmu_notifier_invalidate_range(mm, mmun_start, mmun_end);
		set_huge_pte_at(mm, address, ptep,
				make_huge_pte(vma, new_page, 1));
		page_remove_rmap(old_page, true);
		hugepage_add_new_anon_rmap(new_page, vma, address);
		/* Make the old page be freed below */
		new_page = old_page;
	}
	spin_unlock(ptl);
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
out_release_all:
	restore_reserve_on_error(h, vma, address, new_page);
	put_page(new_page);
out_release_old:
	put_page(old_page);

	spin_lock(ptl); /* Caller expects lock to be held */
	return ret;
}

/* Return the pagecache page at a given address within a VMA */
static struct page *hugetlbfs_pagecache_page(struct hstate *h,
			struct vm_area_struct *vma, unsigned long address)
{
	struct address_space *mapping;
	pgoff_t idx;

	mapping = vma->vm_file->f_mapping;
	idx = vma_hugecache_offset(h, vma, address);

	return find_lock_page(mapping, idx);
}

/*
 * Return whether there is a pagecache page to back given address within VMA.
 * Caller follow_hugetlb_page() holds page_table_lock so we cannot lock_page.
 */
static bool hugetlbfs_pagecache_present(struct hstate *h,
			struct vm_area_struct *vma, unsigned long address)
{
	struct address_space *mapping;
	pgoff_t idx;
	struct page *page;

	mapping = vma->vm_file->f_mapping;
	idx = vma_hugecache_offset(h, vma, address);

	page = find_get_page(mapping, idx);
	if (page)
		put_page(page);
	return page != NULL;
}

int huge_add_to_page_cache(struct page *page, struct address_space *mapping,
			   pgoff_t idx)
{
	struct inode *inode = mapping->host;
	struct hstate *h = hstate_inode(inode);
	int err = add_to_page_cache(page, mapping, idx, GFP_KERNEL);

	if (err)
		return err;
	ClearPagePrivate(page);

	spin_lock(&inode->i_lock);
	inode->i_blocks += blocks_per_huge_page(h);
	spin_unlock(&inode->i_lock);
	return 0;
}

static int hugetlb_no_page(struct mm_struct *mm, struct vm_area_struct *vma,
			   struct address_space *mapping, pgoff_t idx,
			   unsigned long address, pte_t *ptep, unsigned int flags)
{
	struct hstate *h = hstate_vma(vma);
	int ret = VM_FAULT_SIGBUS;
	int anon_rmap = 0;
	unsigned long size;
	struct page *page;
	pte_t new_pte;
	spinlock_t *ptl;

	/*
	 * Currently, we are forced to kill the process in the event the
	 * original mapper has unmapped pages from the child due to a failed
	 * COW. Warn that such a situation has occurred as it may not be obvious
	 */
	if (is_vma_resv_set(vma, HPAGE_RESV_UNMAPPED)) {
		pr_warn_ratelimited("PID %d killed due to inadequate hugepage pool\n",
			   current->pid);
		return ret;
	}

	/*
	 * Use page lock to guard against racing truncation
	 * before we get page_table_lock.
	 */
retry:
	page = find_lock_page(mapping, idx);
	if (!page) {
		size = i_size_read(mapping->host) >> huge_page_shift(h);
		if (idx >= size)
			goto out;

		/*
		 * Check for page in userfault range
		 */
		if (userfaultfd_missing(vma)) {
			u32 hash;
			struct vm_fault vmf = {
				.vma = vma,
				.address = address,
				.flags = flags,
				/*
				 * Hard to debug if it ends up being
				 * used by a callee that assumes
				 * something about the other
				 * uninitialized fields... same as in
				 * memory.c
				 */
			};

			/*
			 * hugetlb_fault_mutex must be dropped before
			 * handling userfault.  Reacquire after handling
			 * fault to make calling code simpler.
			 */
			hash = hugetlb_fault_mutex_hash(h, mm, vma, mapping,
							idx, address);
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			ret = handle_userfault(&vmf, VM_UFFD_MISSING);
			mutex_lock(&hugetlb_fault_mutex_table[hash]);
			goto out;
		}

		page = alloc_huge_page(vma, address, 0);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			if (ret == -ENOMEM)
				ret = VM_FAULT_OOM;
			else
				ret = VM_FAULT_SIGBUS;
			goto out;
		}
		clear_huge_page(page, address, pages_per_huge_page(h));
		__SetPageUptodate(page);
		set_page_huge_active(page);

		if (vma->vm_flags & VM_MAYSHARE) {
			int err = huge_add_to_page_cache(page, mapping, idx);
			if (err) {
				put_page(page);
				if (err == -EEXIST)
					goto retry;
				goto out;
			}
		} else {
			lock_page(page);
			if (unlikely(anon_vma_prepare(vma))) {
				ret = VM_FAULT_OOM;
				goto backout_unlocked;
			}
			anon_rmap = 1;
		}
	} else {
		/*
		 * If memory error occurs between mmap() and fault, some process
		 * don't have hwpoisoned swap entry for errored virtual address.
		 * So we need to block hugepage fault by PG_hwpoison bit check.
		 */
		if (unlikely(PageHWPoison(page))) {
			ret = VM_FAULT_HWPOISON |
				VM_FAULT_SET_HINDEX(hstate_index(h));
			goto backout_unlocked;
		}
	}

	/*
	 * If we are going to COW a private mapping later, we examine the
	 * pending reservations for this page now. This will ensure that
	 * any allocations necessary to record that reservation occur outside
	 * the spinlock.
	 */
	if ((flags & FAULT_FLAG_WRITE) && !(vma->vm_flags & VM_SHARED)) {
		if (vma_needs_reservation(h, vma, address) < 0) {
			ret = VM_FAULT_OOM;
			goto backout_unlocked;
		}
		/* Just decrements count, does not deallocate */
		vma_end_reservation(h, vma, address);
	}

	ptl = huge_pte_lock(h, mm, ptep);
	size = i_size_read(mapping->host) >> huge_page_shift(h);
	if (idx >= size)
		goto backout;

	ret = 0;
	if (!huge_pte_none(huge_ptep_get(ptep)))
		goto backout;

	if (anon_rmap) {
		ClearPagePrivate(page);
		hugepage_add_new_anon_rmap(page, vma, address);
	} else
		page_dup_rmap(page, true);
	new_pte = make_huge_pte(vma, page, ((vma->vm_flags & VM_WRITE)
				&& (vma->vm_flags & VM_SHARED)));
	set_huge_pte_at(mm, address, ptep, new_pte);

	hugetlb_count_add(pages_per_huge_page(h), mm);
	if ((flags & FAULT_FLAG_WRITE) && !(vma->vm_flags & VM_SHARED)) {
		/* Optimization, do the COW without a second fault */
		ret = hugetlb_cow(mm, vma, address, ptep, page, ptl);
	}

	spin_unlock(ptl);
	unlock_page(page);
out:
	return ret;

backout:
	spin_unlock(ptl);
backout_unlocked:
	unlock_page(page);
	restore_reserve_on_error(h, vma, address, page);
	put_page(page);
	goto out;
}

#ifdef CONFIG_SMP
u32 hugetlb_fault_mutex_hash(struct hstate *h, struct mm_struct *mm,
			    struct vm_area_struct *vma,
			    struct address_space *mapping,
			    pgoff_t idx, unsigned long address)
{
	unsigned long key[2];
	u32 hash;

	if (vma->vm_flags & VM_SHARED) {
		key[0] = (unsigned long) mapping;
		key[1] = idx;
	} else {
		key[0] = (unsigned long) mm;
		key[1] = address >> huge_page_shift(h);
	}

	hash = jhash2((u32 *)&key, sizeof(key)/sizeof(u32), 0);

	return hash & (num_fault_mutexes - 1);
}
#else
/*
 * For uniprocesor systems we always use a single mutex, so just
 * return 0 and avoid the hashing overhead.
 */
u32 hugetlb_fault_mutex_hash(struct hstate *h, struct mm_struct *mm,
			    struct vm_area_struct *vma,
			    struct address_space *mapping,
			    pgoff_t idx, unsigned long address)
{
	return 0;
}
#endif

int hugetlb_fault(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, unsigned int flags)
{
	pte_t *ptep, entry;
	spinlock_t *ptl;
	int ret;
	u32 hash;
	pgoff_t idx;
	struct page *page = NULL;
	struct page *pagecache_page = NULL;
	struct hstate *h = hstate_vma(vma);
	struct address_space *mapping;
	int need_wait_lock = 0;

	address &= huge_page_mask(h);

	ptep = huge_pte_offset(mm, address);
	if (ptep) {
		entry = huge_ptep_get(ptep);
		if (unlikely(is_hugetlb_entry_migration(entry))) {
			migration_entry_wait_huge(vma, mm, ptep);
			return 0;
		} else if (unlikely(is_hugetlb_entry_hwpoisoned(entry)))
			return VM_FAULT_HWPOISON_LARGE |
				VM_FAULT_SET_HINDEX(hstate_index(h));
	} else {
		ptep = huge_pte_alloc(mm, address, huge_page_size(h));
		if (!ptep)
			return VM_FAULT_OOM;
	}

	mapping = vma->vm_file->f_mapping;
	idx = vma_hugecache_offset(h, vma, address);

	/*
	 * Serialize hugepage allocation and instantiation, so that we don't
	 * get spurious allocation failures if two CPUs race to instantiate
	 * the same page in the page cache.
	 */
	hash = hugetlb_fault_mutex_hash(h, mm, vma, mapping, idx, address);
	mutex_lock(&hugetlb_fault_mutex_table[hash]);

	entry = huge_ptep_get(ptep);
	if (huge_pte_none(entry)) {
		ret = hugetlb_no_page(mm, vma, mapping, idx, address, ptep, flags);
		goto out_mutex;
	}

	ret = 0;

	/*
	 * entry could be a migration/hwpoison entry at this point, so this
	 * check prevents the kernel from going below assuming that we have
	 * a active hugepage in pagecache. This goto expects the 2nd page fault,
	 * and is_hugetlb_entry_(migration|hwpoisoned) check will properly
	 * handle it.
	 */
	if (!pte_present(entry))
		goto out_mutex;

	/*
	 * If we are going to COW the mapping later, we examine the pending
	 * reservations for this page now. This will ensure that any
	 * allocations necessary to record that reservation occur outside the
	 * spinlock. For private mappings, we also lookup the pagecache
	 * page now as it is used to determine if a reservation has been
	 * consumed.
	 */
	if ((flags & FAULT_FLAG_WRITE) && !huge_pte_write(entry)) {
		if (vma_needs_reservation(h, vma, address) < 0) {
			ret = VM_FAULT_OOM;
			goto out_mutex;
		}
		/* Just decrements count, does not deallocate */
		vma_end_reservation(h, vma, address);

		if (!(vma->vm_flags & VM_MAYSHARE))
			pagecache_page = hugetlbfs_pagecache_page(h,
								vma, address);
	}

	ptl = huge_pte_lock(h, mm, ptep);

	/* Check for a racing update before calling hugetlb_cow */
	if (unlikely(!pte_same(entry, huge_ptep_get(ptep))))
		goto out_ptl;

	/*
	 * hugetlb_cow() requires page locks of pte_page(entry) and
	 * pagecache_page, so here we need take the former one
	 * when page != pagecache_page or !pagecache_page.
	 */
	page = pte_page(entry);
	if (page != pagecache_page)
		if (!trylock_page(page)) {
			need_wait_lock = 1;
			goto out_ptl;
		}

	get_page(page);

	if (flags & FAULT_FLAG_WRITE) {
		if (!huge_pte_write(entry)) {
			ret = hugetlb_cow(mm, vma, address, ptep,
					  pagecache_page, ptl);
			goto out_put_page;
		}
		entry = huge_pte_mkdirty(entry);
	}
	entry = pte_mkyoung(entry);
	if (huge_ptep_set_access_flags(vma, address, ptep, entry,
						flags & FAULT_FLAG_WRITE))
		update_mmu_cache(vma, address, ptep);
out_put_page:
	if (page != pagecache_page)
		unlock_page(page);
	put_page(page);
out_ptl:
	spin_unlock(ptl);

	if (pagecache_page) {
		unlock_page(pagecache_page);
		put_page(pagecache_page);
	}
out_mutex:
	mutex_unlock(&hugetlb_fault_mutex_table[hash]);
	/*
	 * Generally it's safe to hold refcount during waiting page lock. But
	 * here we just wait to defer the next page fault to avoid busy loop and
	 * the page is not used after unlocked before returning from the current
	 * page fault. So we are safe from accessing freed page, even if we wait
	 * here without taking refcount.
	 */
	if (need_wait_lock)
		wait_on_page_locked(page);
	return ret;
}

/*
 * Used by userfaultfd UFFDIO_COPY.  Based on mcopy_atomic_pte with
 * modifications for huge pages.
 */
int hugetlb_mcopy_atomic_pte(struct mm_struct *dst_mm,
			    pte_t *dst_pte,
			    struct vm_area_struct *dst_vma,
			    unsigned long dst_addr,
			    unsigned long src_addr,
			    struct page **pagep)
{
	int vm_shared = dst_vma->vm_flags & VM_SHARED;
	struct hstate *h = hstate_vma(dst_vma);
	pte_t _dst_pte;
	spinlock_t *ptl;
	int ret;
	struct page *page;

	if (!*pagep) {
		ret = -ENOMEM;
		page = alloc_huge_page(dst_vma, dst_addr, 0);
		if (IS_ERR(page))
			goto out;

		ret = copy_huge_page_from_user(page,
						(const void __user *) src_addr,
						pages_per_huge_page(h), false);

		/* fallback to copy_from_user outside mmap_sem */
		if (unlikely(ret)) {
			ret = -EFAULT;
			*pagep = page;
			/* don't free the page */
			goto out;
		}
	} else {
		page = *pagep;
		*pagep = NULL;
	}

	/*
	 * The memory barrier inside __SetPageUptodate makes sure that
	 * preceding stores to the page contents become visible before
	 * the set_pte_at() write.
	 */
	__SetPageUptodate(page);
	set_page_huge_active(page);

	/*
	 * If shared, add to page cache
	 */
	if (vm_shared) {
		struct address_space *mapping = dst_vma->vm_file->f_mapping;
		pgoff_t idx = vma_hugecache_offset(h, dst_vma, dst_addr);

		ret = huge_add_to_page_cache(page, mapping, idx);
		if (ret)
			goto out_release_nounlock;
	}

	ptl = huge_pte_lockptr(h, dst_mm, dst_pte);
	spin_lock(ptl);

	ret = -EEXIST;
	if (!huge_pte_none(huge_ptep_get(dst_pte)))
		goto out_release_unlock;

	if (vm_shared) {
		page_dup_rmap(page, true);
	} else {
		ClearPagePrivate(page);
		hugepage_add_new_anon_rmap(page, dst_vma, dst_addr);
	}

	_dst_pte = make_huge_pte(dst_vma, page, dst_vma->vm_flags & VM_WRITE);
	if (dst_vma->vm_flags & VM_WRITE)
		_dst_pte = huge_pte_mkdirty(_dst_pte);
	_dst_pte = pte_mkyoung(_dst_pte);

	set_huge_pte_at(dst_mm, dst_addr, dst_pte, _dst_pte);

	(void)huge_ptep_set_access_flags(dst_vma, dst_addr, dst_pte, _dst_pte,
					dst_vma->vm_flags & VM_WRITE);
	hugetlb_count_add(pages_per_huge_page(h), dst_mm);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(dst_vma, dst_addr, dst_pte);

	spin_unlock(ptl);
	if (vm_shared)
		unlock_page(page);
	ret = 0;
out:
	return ret;
out_release_unlock:
	spin_unlock(ptl);
out_release_nounlock:
	if (vm_shared)
		unlock_page(page);
	put_page(page);
	goto out;
}

long follow_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma,
			 struct page **pages, struct vm_area_struct **vmas,
			 unsigned long *position, unsigned long *nr_pages,
			 long i, unsigned int flags, int *nonblocking)
{
	unsigned long pfn_offset;
	unsigned long vaddr = *position;
	unsigned long remainder = *nr_pages;
	struct hstate *h = hstate_vma(vma);

	while (vaddr < vma->vm_end && remainder) {
		pte_t *pte;
		spinlock_t *ptl = NULL;
		int absent;
		struct page *page;

		/*
		 * If we have a pending SIGKILL, don't keep faulting pages and
		 * potentially allocating memory.
		 */
		if (unlikely(fatal_signal_pending(current))) {
			remainder = 0;
			break;
		}

		/*
		 * Some archs (sparc64, sh*) have multiple pte_ts to
		 * each hugepage.  We have to make sure we get the
		 * first, for the page indexing below to work.
		 *
		 * Note that page table lock is not held when pte is null.
		 */
		pte = huge_pte_offset(mm, vaddr & huge_page_mask(h));
		if (pte)
			ptl = huge_pte_lock(h, mm, pte);
		absent = !pte || huge_pte_none(huge_ptep_get(pte));

		/*
		 * When coredumping, it suits get_dump_page if we just return
		 * an error where there's an empty slot with no huge pagecache
		 * to back it.  This way, we avoid allocating a hugepage, and
		 * the sparse dumpfile avoids allocating disk blocks, but its
		 * huge holes still show up with zeroes where they need to be.
		 */
		if (absent && (flags & FOLL_DUMP) &&
		    !hugetlbfs_pagecache_present(h, vma, vaddr)) {
			if (pte)
				spin_unlock(ptl);
			remainder = 0;
			break;
		}

		/*
		 * We need call hugetlb_fault for both hugepages under migration
		 * (in which case hugetlb_fault waits for the migration,) and
		 * hwpoisoned hugepages (in which case we need to prevent the
		 * caller from accessing to them.) In order to do this, we use
		 * here is_swap_pte instead of is_hugetlb_entry_migration and
		 * is_hugetlb_entry_hwpoisoned. This is because it simply covers
		 * both cases, and because we can't follow correct pages
		 * directly from any kind of swap entries.
		 */
		if (absent || is_swap_pte(huge_ptep_get(pte)) ||
		    ((flags & FOLL_WRITE) &&
		      !huge_pte_write(huge_ptep_get(pte)))) {
			int ret;
			unsigned int fault_flags = 0;

			if (pte)
				spin_unlock(ptl);
			if (flags & FOLL_WRITE)
				fault_flags |= FAULT_FLAG_WRITE;
			if (nonblocking)
				fault_flags |= FAULT_FLAG_ALLOW_RETRY;
			if (flags & FOLL_NOWAIT)
				fault_flags |= FAULT_FLAG_ALLOW_RETRY |
					FAULT_FLAG_RETRY_NOWAIT;
			if (flags & FOLL_TRIED) {
				VM_WARN_ON_ONCE(fault_flags &
						FAULT_FLAG_ALLOW_RETRY);
				fault_flags |= FAULT_FLAG_TRIED;
			}
			ret = hugetlb_fault(mm, vma, vaddr, fault_flags);
			if (ret & VM_FAULT_ERROR) {
				remainder = 0;
				break;
			}
			if (ret & VM_FAULT_RETRY) {
				if (nonblocking)
					*nonblocking = 0;
				*nr_pages = 0;
				/*
				 * VM_FAULT_RETRY must not return an
				 * error, it will return zero
				 * instead.
				 *
				 * No need to update "position" as the
				 * caller will not check it after
				 * *nr_pages is set to 0.
				 */
				return i;
			}
			continue;
		}

		pfn_offset = (vaddr & ~huge_page_mask(h)) >> PAGE_SHIFT;
		page = pte_page(huge_ptep_get(pte));
same_page:
		if (pages) {
			pages[i] = mem_map_offset(page, pfn_offset);
			get_page(pages[i]);
		}

		if (vmas)
			vmas[i] = vma;

		vaddr += PAGE_SIZE;
		++pfn_offset;
		--remainder;
		++i;
		if (vaddr < vma->vm_end && remainder &&
				pfn_offset < pages_per_huge_page(h)) {
			/*
			 * We use pfn_offset to avoid touching the pageframes
			 * of this compound page.
			 */
			goto same_page;
		}
		spin_unlock(ptl);
	}
	*nr_pages = remainder;
	/*
	 * setting position is actually required only if remainder is
	 * not zero but it's faster not to add a "if (remainder)"
	 * branch.
	 */
	*position = vaddr;

	return i ? i : -EFAULT;
}

#ifndef __HAVE_ARCH_FLUSH_HUGETLB_TLB_RANGE
/*
 * ARCHes with special requirements for evicting HUGETLB backing TLB entries can
 * implement this.
 */
#define flush_hugetlb_tlb_range(vma, addr, end)	flush_tlb_range(vma, addr, end)
#endif

unsigned long hugetlb_change_protection(struct vm_area_struct *vma,
		unsigned long address, unsigned long end, pgprot_t newprot)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long start = address;
	pte_t *ptep;
	pte_t pte;
	struct hstate *h = hstate_vma(vma);
	unsigned long pages = 0;

	BUG_ON(address >= end);
	flush_cache_range(vma, address, end);

	mmu_notifier_invalidate_range_start(mm, start, end);
	i_mmap_lock_write(vma->vm_file->f_mapping);
	for (; address < end; address += huge_page_size(h)) {
		spinlock_t *ptl;
		ptep = huge_pte_offset(mm, address);
		if (!ptep)
			continue;
		ptl = huge_pte_lock(h, mm, ptep);
		if (huge_pmd_unshare(mm, &address, ptep)) {
			pages++;
			spin_unlock(ptl);
			continue;
		}
		pte = huge_ptep_get(ptep);
		if (unlikely(is_hugetlb_entry_hwpoisoned(pte))) {
			spin_unlock(ptl);
			continue;
		}
		if (unlikely(is_hugetlb_entry_migration(pte))) {
			swp_entry_t entry = pte_to_swp_entry(pte);

			if (is_write_migration_entry(entry)) {
				pte_t newpte;

				make_migration_entry_read(&entry);
				newpte = swp_entry_to_pte(entry);
				set_huge_pte_at(mm, address, ptep, newpte);
				pages++;
			}
			spin_unlock(ptl);
			continue;
		}
		if (!huge_pte_none(pte)) {
			pte = huge_ptep_get_and_clear(mm, address, ptep);
			pte = pte_mkhuge(huge_pte_modify(pte, newprot));
			pte = arch_make_huge_pte(pte, vma, NULL, 0);
			set_huge_pte_at(mm, address, ptep, pte);
			pages++;
		}
		spin_unlock(ptl);
	}
	/*
	 * Must flush TLB before releasing i_mmap_rwsem: x86's huge_pmd_unshare
	 * may have cleared our pud entry and done put_page on the page table:
	 * once we release i_mmap_rwsem, another task can do the final put_page
	 * and that page table be reused and filled with junk.
	 */
	flush_hugetlb_tlb_range(vma, start, end);
	mmu_notifier_invalidate_range(mm, start, end);
	i_mmap_unlock_write(vma->vm_file->f_mapping);
	mmu_notifier_invalidate_range_end(mm, start, end);

	return pages << h->order;
}

int hugetlb_reserve_pages(struct inode *inode,
					long from, long to,
					struct vm_area_struct *vma,
					vm_flags_t vm_flags)
{
	long ret, chg;
	struct hstate *h = hstate_inode(inode);
	struct hugepage_subpool *spool = subpool_inode(inode);
	struct resv_map *resv_map;
	long gbl_reserve;

	/*
	 * Only apply hugepage reservation if asked. At fault time, an
	 * attempt will be made for VM_NORESERVE to allocate a page
	 * without using reserves
	 */
	if (vm_flags & VM_NORESERVE)
		return 0;

	/*
	 * Shared mappings base their reservation on the number of pages that
	 * are already allocated on behalf of the file. Private mappings need
	 * to reserve the full area even if read-only as mprotect() may be
	 * called to make the mapping read-write. Assume !vma is a shm mapping
	 */
	if (!vma || vma->vm_flags & VM_MAYSHARE) {
		resv_map = inode_resv_map(inode);

		chg = region_chg(resv_map, from, to);

	} else {
		resv_map = resv_map_alloc();
		if (!resv_map)
			return -ENOMEM;

		chg = to - from;

		set_vma_resv_map(vma, resv_map);
		set_vma_resv_flags(vma, HPAGE_RESV_OWNER);
	}

	if (chg < 0) {
		ret = chg;
		goto out_err;
	}

	/*
	 * There must be enough pages in the subpool for the mapping. If
	 * the subpool has a minimum size, there may be some global
	 * reservations already in place (gbl_reserve).
	 */
	gbl_reserve = hugepage_subpool_get_pages(spool, chg);
	if (gbl_reserve < 0) {
		ret = -ENOSPC;
		goto out_err;
	}

	/*
	 * Check enough hugepages are available for the reservation.
	 * Hand the pages back to the subpool if there are not
	 */
	ret = hugetlb_acct_memory(h, gbl_reserve);
	if (ret < 0) {
		/* put back original number of pages, chg */
		(void)hugepage_subpool_put_pages(spool, chg);
		goto out_err;
	}

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
		long add = region_add(resv_map, from, to);

		if (unlikely(chg > add)) {
			/*
			 * pages in this range were added to the reserve
			 * map between region_chg and region_add.  This
			 * indicates a race with alloc_huge_page.  Adjust
			 * the subpool and reserve counts modified above
			 * based on the difference.
			 */
			long rsv_adjust;

			rsv_adjust = hugepage_subpool_put_pages(spool,
								chg - add);
			hugetlb_acct_memory(h, -rsv_adjust);
		}
	}
	return 0;
out_err:
	if (!vma || vma->vm_flags & VM_MAYSHARE)
		/* Don't call region_abort if region_chg failed */
		if (chg >= 0)
			region_abort(resv_map, from, to);
	if (vma && is_vma_resv_set(vma, HPAGE_RESV_OWNER))
		kref_put(&resv_map->refs, resv_map_release);
	return ret;
}

long hugetlb_unreserve_pages(struct inode *inode, long start, long end,
								long freed)
{
	struct hstate *h = hstate_inode(inode);
	struct resv_map *resv_map = inode_resv_map(inode);
	long chg = 0;
	struct hugepage_subpool *spool = subpool_inode(inode);
	long gbl_reserve;

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
	 */
	gbl_reserve = hugepage_subpool_put_pages(spool, (chg - freed));
	hugetlb_acct_memory(h, -gbl_reserve);

	return 0;
}

#ifdef CONFIG_ARCH_WANT_HUGE_PMD_SHARE
static unsigned long page_table_shareable(struct vm_area_struct *svma,
				struct vm_area_struct *vma,
				unsigned long addr, pgoff_t idx)
{
	unsigned long saddr = ((idx - svma->vm_pgoff) << PAGE_SHIFT) +
				svma->vm_start;
	unsigned long sbase = saddr & PUD_MASK;
	unsigned long s_end = sbase + PUD_SIZE;

	/* Allow segments to share if only one is marked locked */
	unsigned long vm_flags = vma->vm_flags & VM_LOCKED_CLEAR_MASK;
	unsigned long svm_flags = svma->vm_flags & VM_LOCKED_CLEAR_MASK;

	/*
	 * match the virtual addresses, permission and the alignment of the
	 * page table page.
	 */
	if (pmd_index(addr) != pmd_index(saddr) ||
	    vm_flags != svm_flags ||
	    sbase < svma->vm_start || svma->vm_end < s_end)
		return 0;

	return saddr;
}

static bool vma_shareable(struct vm_area_struct *vma, unsigned long addr)
{
	unsigned long base = addr & PUD_MASK;
	unsigned long end = base + PUD_SIZE;

	/*
	 * check on proper vm_flags and page table alignment
	 */
	if (vma->vm_flags & VM_MAYSHARE &&
	    vma->vm_start <= base && end <= vma->vm_end)
		return true;
	return false;
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
pte_t *huge_pmd_share(struct mm_struct *mm, unsigned long addr, pud_t *pud)
{
	struct vm_area_struct *vma = find_vma(mm, addr);
	struct address_space *mapping = vma->vm_file->f_mapping;
	pgoff_t idx = ((addr - vma->vm_start) >> PAGE_SHIFT) +
			vma->vm_pgoff;
	struct vm_area_struct *svma;
	unsigned long saddr;
	pte_t *spte = NULL;
	pte_t *pte;
	spinlock_t *ptl;

	if (!vma_shareable(vma, addr))
		return (pte_t *)pmd_alloc(mm, pud, addr);

	i_mmap_lock_write(mapping);
	vma_interval_tree_foreach(svma, &mapping->i_mmap, idx, idx) {
		if (svma == vma)
			continue;

		saddr = page_table_shareable(svma, vma, addr, idx);
		if (saddr) {
			spte = huge_pte_offset(svma->vm_mm, saddr);
			if (spte) {
				get_page(virt_to_page(spte));
				break;
			}
		}
	}

	if (!spte)
		goto out;

	ptl = huge_pte_lock(hstate_vma(vma), mm, spte);
	if (pud_none(*pud)) {
		pud_populate(mm, pud,
				(pmd_t *)((unsigned long)spte & PAGE_MASK));
		mm_inc_nr_pmds(mm);
	} else {
		put_page(virt_to_page(spte));
	}
	spin_unlock(ptl);
out:
	pte = (pte_t *)pmd_alloc(mm, pud, addr);
	i_mmap_unlock_write(mapping);
	return pte;
}

/*
 * unmap huge page backed by shared pte.
 *
 * Hugetlb pte page is ref counted at the time of mapping.  If pte is shared
 * indicated by page_count > 1, unmap is achieved by clearing pud and
 * decrementing the ref count. If count == 1, the pte page is not shared.
 *
 * called with page table lock held.
 *
 * returns: 1 successfully unmapped a shared pte page
 *	    0 the underlying pte page is not shared, or it is the last user
 */
int huge_pmd_unshare(struct mm_struct *mm, unsigned long *addr, pte_t *ptep)
{
	pgd_t *pgd = pgd_offset(mm, *addr);
	p4d_t *p4d = p4d_offset(pgd, *addr);
	pud_t *pud = pud_offset(p4d, *addr);

	BUG_ON(page_count(virt_to_page(ptep)) == 0);
	if (page_count(virt_to_page(ptep)) == 1)
		return 0;

	pud_clear(pud);
	put_page(virt_to_page(ptep));
	mm_dec_nr_pmds(mm);
	*addr = ALIGN(*addr, HPAGE_SIZE * PTRS_PER_PTE) - HPAGE_SIZE;
	return 1;
}
#define want_pmd_share()	(1)
#else /* !CONFIG_ARCH_WANT_HUGE_PMD_SHARE */
pte_t *huge_pmd_share(struct mm_struct *mm, unsigned long addr, pud_t *pud)
{
	return NULL;
}

int huge_pmd_unshare(struct mm_struct *mm, unsigned long *addr, pte_t *ptep)
{
	return 0;
}
#define want_pmd_share()	(0)
#endif /* CONFIG_ARCH_WANT_HUGE_PMD_SHARE */

#ifdef CONFIG_ARCH_WANT_GENERAL_HUGETLB
pte_t *huge_pte_alloc(struct mm_struct *mm,
			unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_offset(pgd, addr);
	pud = pud_alloc(mm, p4d, addr);
	if (pud) {
		if (sz == PUD_SIZE) {
			pte = (pte_t *)pud;
		} else {
			BUG_ON(sz != PMD_SIZE);
			if (want_pmd_share() && pud_none(*pud))
				pte = huge_pmd_share(mm, addr, pud);
			else
				pte = (pte_t *)pmd_alloc(mm, pud, addr);
		}
	}
	BUG_ON(pte && pte_present(*pte) && !pte_huge(*pte));

	return pte;
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
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
	if (!pud_present(*pud))
		return NULL;
	if (pud_huge(*pud))
		return (pte_t *)pud;
	pmd = pmd_offset(pud, addr);
	return (pte_t *) pmd;
}

#endif /* CONFIG_ARCH_WANT_GENERAL_HUGETLB */

/*
 * These functions are overwritable if your architecture needs its own
 * behavior.
 */
struct page * __weak
follow_huge_addr(struct mm_struct *mm, unsigned long address,
			      int write)
{
	return ERR_PTR(-EINVAL);
}

struct page * __weak
follow_huge_pmd(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd, int flags)
{
	struct page *page = NULL;
	spinlock_t *ptl;
	pte_t pte;
retry:
	ptl = pmd_lockptr(mm, pmd);
	spin_lock(ptl);
	/*
	 * make sure that the address range covered by this pmd is not
	 * unmapped from other threads.
	 */
	if (!pmd_huge(*pmd))
		goto out;
	pte = huge_ptep_get((pte_t *)pmd);
	if (pte_present(pte)) {
		page = pmd_page(*pmd) + ((address & ~PMD_MASK) >> PAGE_SHIFT);
		if (flags & FOLL_GET)
			get_page(page);
	} else {
		if (is_hugetlb_entry_migration(pte)) {
			spin_unlock(ptl);
			__migration_entry_wait(mm, (pte_t *)pmd, ptl);
			goto retry;
		}
		/*
		 * hwpoisoned entry is treated as no_page_table in
		 * follow_page_mask().
		 */
	}
out:
	spin_unlock(ptl);
	return page;
}

struct page * __weak
follow_huge_pud(struct mm_struct *mm, unsigned long address,
		pud_t *pud, int flags)
{
	if (flags & FOLL_GET)
		return NULL;

	return pte_page(*(pte_t *)pud) + ((address & ~PUD_MASK) >> PAGE_SHIFT);
}

#ifdef CONFIG_MEMORY_FAILURE

/*
 * This function is called from memory failure code.
 */
int dequeue_hwpoisoned_huge_page(struct page *hpage)
{
	struct hstate *h = page_hstate(hpage);
	int nid = page_to_nid(hpage);
	int ret = -EBUSY;

	spin_lock(&hugetlb_lock);
	/*
	 * Just checking !page_huge_active is not enough, because that could be
	 * an isolated/hwpoisoned hugepage (which have >0 refcount).
	 */
	if (!page_huge_active(hpage) && !page_count(hpage)) {
		/*
		 * Hwpoisoned hugepage isn't linked to activelist or freelist,
		 * but dangling hpage->lru can trigger list-debug warnings
		 * (this happens when we call unpoison_memory() on it),
		 * so let it point to itself with list_del_init().
		 */
		list_del_init(&hpage->lru);
		set_page_refcounted(hpage);
		h->free_huge_pages--;
		h->free_huge_pages_node[nid]--;
		ret = 0;
	}
	spin_unlock(&hugetlb_lock);
	return ret;
}
#endif

bool isolate_huge_page(struct page *page, struct list_head *list)
{
	bool ret = true;

	VM_BUG_ON_PAGE(!PageHead(page), page);
	spin_lock(&hugetlb_lock);
	if (!page_huge_active(page) || !get_page_unless_zero(page)) {
		ret = false;
		goto unlock;
	}
	clear_page_huge_active(page);
	list_move_tail(&page->lru, list);
unlock:
	spin_unlock(&hugetlb_lock);
	return ret;
}

void putback_active_hugepage(struct page *page)
{
	VM_BUG_ON_PAGE(!PageHead(page), page);
	spin_lock(&hugetlb_lock);
	set_page_huge_active(page);
	list_move_tail(&page->lru, &(page_hstate(page))->hugepage_activelist);
	spin_unlock(&hugetlb_lock);
	put_page(page);
}
