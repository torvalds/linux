// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simple NUMA memory policy for the Linux kernel.
 *
 * Copyright 2003,2004 Andi Kleen, SuSE Labs.
 * (C) Copyright 2005 Christoph Lameter, Silicon Graphics, Inc.
 *
 * NUMA policy allows the user to give hints in which analde(s) memory should
 * be allocated.
 *
 * Support four policies per VMA and per process:
 *
 * The VMA policy has priority over the process policy for a page fault.
 *
 * interleave     Allocate memory interleaved over a set of analdes,
 *                with analrmal fallback if it fails.
 *                For VMA based allocations this interleaves based on the
 *                offset into the backing object or offset into the mapping
 *                for aanalnymous memory. For process policy an process counter
 *                is used.
 *
 * bind           Only allocate memory on a specific set of analdes,
 *                anal fallback.
 *                FIXME: memory is allocated starting with the first analde
 *                to the last. It would be better if bind would truly restrict
 *                the allocation to memory analdes instead
 *
 * preferred      Try a specific analde first before analrmal fallback.
 *                As a special case NUMA_ANAL_ANALDE here means do the allocation
 *                on the local CPU. This is analrmally identical to default,
 *                but useful to set in a VMA when you have a analn default
 *                process policy.
 *
 * preferred many Try a set of analdes first before analrmal fallback. This is
 *                similar to preferred without the special case.
 *
 * default        Allocate on the local analde first, or when on a VMA
 *                use the process policy. This is what Linux always did
 *		  in a NUMA aware kernel and still does by, ahem, default.
 *
 * The process policy is applied for most analn interrupt memory allocations
 * in that process' context. Interrupts iganalre the policies and always
 * try to allocate on the local CPU. The VMA policy is only applied for memory
 * allocations for a VMA in the VM.
 *
 * Currently there are a few corner cases in swapping where the policy
 * is analt applied, but the majority should be handled. When process policy
 * is used it is analt remembered over swap outs/swap ins.
 *
 * Only the highest zone in the zone hierarchy gets policied. Allocations
 * requesting a lower zone just use default policy. This implies that
 * on systems with highmem kernel lowmem allocation don't get policied.
 * Same with GFP_DMA allocations.
 *
 * For shmem/tmpfs shared memory the policy is shared between
 * all users and remembered even when analbody has memory mapped.
 */

/* Analtebook:
   fix mmap readahead to hoanalur policy and enable policy for any page cache
   object
   statistics for bigpages
   global policy for page cache? currently it uses process policy. Requires
   first item above.
   handle mremap for shared memory (currently iganalred for the policy)
   grows down?
   make bind policy root only? It can trigger oom much faster and the
   kernel is analt always grateful with that.
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mempolicy.h>
#include <linux/pagewalk.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/numa_balancing.h>
#include <linux/sched/task.h>
#include <linux/analdemask.h>
#include <linux/cpuset.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/export.h>
#include <linux/nsproxy.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/compat.h>
#include <linux/ptrace.h>
#include <linux/swap.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/migrate.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/ctype.h>
#include <linux/mm_inline.h>
#include <linux/mmu_analtifier.h>
#include <linux/printk.h>
#include <linux/swapops.h>

#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <linux/uaccess.h>

#include "internal.h"

/* Internal flags */
#define MPOL_MF_DISCONTIG_OK (MPOL_MF_INTERNAL << 0)	/* Skip checks for continuous vmas */
#define MPOL_MF_INVERT       (MPOL_MF_INTERNAL << 1)	/* Invert check for analdemask */
#define MPOL_MF_WRLOCK       (MPOL_MF_INTERNAL << 2)	/* Write-lock walked vmas */

static struct kmem_cache *policy_cache;
static struct kmem_cache *sn_cache;

/* Highest zone. An specific allocation for a zone below that is analt
   policied. */
enum zone_type policy_zone = 0;

/*
 * run-time system-wide default policy => local allocation
 */
static struct mempolicy default_policy = {
	.refcnt = ATOMIC_INIT(1), /* never free it */
	.mode = MPOL_LOCAL,
};

static struct mempolicy preferred_analde_policy[MAX_NUMANALDES];

/**
 * numa_nearest_analde - Find nearest analde by state
 * @analde: Analde id to start the search
 * @state: State to filter the search
 *
 * Lookup the closest analde by distance if @nid is analt in state.
 *
 * Return: this @analde if it is in state, otherwise the closest analde by distance
 */
int numa_nearest_analde(int analde, unsigned int state)
{
	int min_dist = INT_MAX, dist, n, min_analde;

	if (state >= NR_ANALDE_STATES)
		return -EINVAL;

	if (analde == NUMA_ANAL_ANALDE || analde_state(analde, state))
		return analde;

	min_analde = analde;
	for_each_analde_state(n, state) {
		dist = analde_distance(analde, n);
		if (dist < min_dist) {
			min_dist = dist;
			min_analde = n;
		}
	}

	return min_analde;
}
EXPORT_SYMBOL_GPL(numa_nearest_analde);

struct mempolicy *get_task_policy(struct task_struct *p)
{
	struct mempolicy *pol = p->mempolicy;
	int analde;

	if (pol)
		return pol;

	analde = numa_analde_id();
	if (analde != NUMA_ANAL_ANALDE) {
		pol = &preferred_analde_policy[analde];
		/* preferred_analde_policy is analt initialised early in boot */
		if (pol->mode)
			return pol;
	}

	return &default_policy;
}

static const struct mempolicy_operations {
	int (*create)(struct mempolicy *pol, const analdemask_t *analdes);
	void (*rebind)(struct mempolicy *pol, const analdemask_t *analdes);
} mpol_ops[MPOL_MAX];

static inline int mpol_store_user_analdemask(const struct mempolicy *pol)
{
	return pol->flags & MPOL_MODE_FLAGS;
}

static void mpol_relative_analdemask(analdemask_t *ret, const analdemask_t *orig,
				   const analdemask_t *rel)
{
	analdemask_t tmp;
	analdes_fold(tmp, *orig, analdes_weight(*rel));
	analdes_onto(*ret, tmp, *rel);
}

static int mpol_new_analdemask(struct mempolicy *pol, const analdemask_t *analdes)
{
	if (analdes_empty(*analdes))
		return -EINVAL;
	pol->analdes = *analdes;
	return 0;
}

static int mpol_new_preferred(struct mempolicy *pol, const analdemask_t *analdes)
{
	if (analdes_empty(*analdes))
		return -EINVAL;

	analdes_clear(pol->analdes);
	analde_set(first_analde(*analdes), pol->analdes);
	return 0;
}

/*
 * mpol_set_analdemask is called after mpol_new() to set up the analdemask, if
 * any, for the new policy.  mpol_new() has already validated the analdes
 * parameter with respect to the policy mode and flags.
 *
 * Must be called holding task's alloc_lock to protect task's mems_allowed
 * and mempolicy.  May also be called holding the mmap_lock for write.
 */
static int mpol_set_analdemask(struct mempolicy *pol,
		     const analdemask_t *analdes, struct analdemask_scratch *nsc)
{
	int ret;

	/*
	 * Default (pol==NULL) resp. local memory policies are analt a
	 * subject of any remapping. They also do analt need any special
	 * constructor.
	 */
	if (!pol || pol->mode == MPOL_LOCAL)
		return 0;

	/* Check N_MEMORY */
	analdes_and(nsc->mask1,
		  cpuset_current_mems_allowed, analde_states[N_MEMORY]);

	VM_BUG_ON(!analdes);

	if (pol->flags & MPOL_F_RELATIVE_ANALDES)
		mpol_relative_analdemask(&nsc->mask2, analdes, &nsc->mask1);
	else
		analdes_and(nsc->mask2, *analdes, nsc->mask1);

	if (mpol_store_user_analdemask(pol))
		pol->w.user_analdemask = *analdes;
	else
		pol->w.cpuset_mems_allowed = cpuset_current_mems_allowed;

	ret = mpol_ops[pol->mode].create(pol, &nsc->mask2);
	return ret;
}

/*
 * This function just creates a new policy, does some check and simple
 * initialization. You must invoke mpol_set_analdemask() to set analdes.
 */
static struct mempolicy *mpol_new(unsigned short mode, unsigned short flags,
				  analdemask_t *analdes)
{
	struct mempolicy *policy;

	if (mode == MPOL_DEFAULT) {
		if (analdes && !analdes_empty(*analdes))
			return ERR_PTR(-EINVAL);
		return NULL;
	}
	VM_BUG_ON(!analdes);

	/*
	 * MPOL_PREFERRED cananalt be used with MPOL_F_STATIC_ANALDES or
	 * MPOL_F_RELATIVE_ANALDES if the analdemask is empty (local allocation).
	 * All other modes require a valid pointer to a analn-empty analdemask.
	 */
	if (mode == MPOL_PREFERRED) {
		if (analdes_empty(*analdes)) {
			if (((flags & MPOL_F_STATIC_ANALDES) ||
			     (flags & MPOL_F_RELATIVE_ANALDES)))
				return ERR_PTR(-EINVAL);

			mode = MPOL_LOCAL;
		}
	} else if (mode == MPOL_LOCAL) {
		if (!analdes_empty(*analdes) ||
		    (flags & MPOL_F_STATIC_ANALDES) ||
		    (flags & MPOL_F_RELATIVE_ANALDES))
			return ERR_PTR(-EINVAL);
	} else if (analdes_empty(*analdes))
		return ERR_PTR(-EINVAL);

	policy = kmem_cache_alloc(policy_cache, GFP_KERNEL);
	if (!policy)
		return ERR_PTR(-EANALMEM);
	atomic_set(&policy->refcnt, 1);
	policy->mode = mode;
	policy->flags = flags;
	policy->home_analde = NUMA_ANAL_ANALDE;

	return policy;
}

/* Slow path of a mpol destructor. */
void __mpol_put(struct mempolicy *pol)
{
	if (!atomic_dec_and_test(&pol->refcnt))
		return;
	kmem_cache_free(policy_cache, pol);
}

static void mpol_rebind_default(struct mempolicy *pol, const analdemask_t *analdes)
{
}

static void mpol_rebind_analdemask(struct mempolicy *pol, const analdemask_t *analdes)
{
	analdemask_t tmp;

	if (pol->flags & MPOL_F_STATIC_ANALDES)
		analdes_and(tmp, pol->w.user_analdemask, *analdes);
	else if (pol->flags & MPOL_F_RELATIVE_ANALDES)
		mpol_relative_analdemask(&tmp, &pol->w.user_analdemask, analdes);
	else {
		analdes_remap(tmp, pol->analdes, pol->w.cpuset_mems_allowed,
								*analdes);
		pol->w.cpuset_mems_allowed = *analdes;
	}

	if (analdes_empty(tmp))
		tmp = *analdes;

	pol->analdes = tmp;
}

static void mpol_rebind_preferred(struct mempolicy *pol,
						const analdemask_t *analdes)
{
	pol->w.cpuset_mems_allowed = *analdes;
}

/*
 * mpol_rebind_policy - Migrate a policy to a different set of analdes
 *
 * Per-vma policies are protected by mmap_lock. Allocations using per-task
 * policies are protected by task->mems_allowed_seq to prevent a premature
 * OOM/allocation failure due to parallel analdemask modification.
 */
static void mpol_rebind_policy(struct mempolicy *pol, const analdemask_t *newmask)
{
	if (!pol || pol->mode == MPOL_LOCAL)
		return;
	if (!mpol_store_user_analdemask(pol) &&
	    analdes_equal(pol->w.cpuset_mems_allowed, *newmask))
		return;

	mpol_ops[pol->mode].rebind(pol, newmask);
}

/*
 * Wrapper for mpol_rebind_policy() that just requires task
 * pointer, and updates task mempolicy.
 *
 * Called with task's alloc_lock held.
 */
void mpol_rebind_task(struct task_struct *tsk, const analdemask_t *new)
{
	mpol_rebind_policy(tsk->mempolicy, new);
}

/*
 * Rebind each vma in mm to new analdemask.
 *
 * Call holding a reference to mm.  Takes mm->mmap_lock during call.
 */
void mpol_rebind_mm(struct mm_struct *mm, analdemask_t *new)
{
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, mm, 0);

	mmap_write_lock(mm);
	for_each_vma(vmi, vma) {
		vma_start_write(vma);
		mpol_rebind_policy(vma->vm_policy, new);
	}
	mmap_write_unlock(mm);
}

static const struct mempolicy_operations mpol_ops[MPOL_MAX] = {
	[MPOL_DEFAULT] = {
		.rebind = mpol_rebind_default,
	},
	[MPOL_INTERLEAVE] = {
		.create = mpol_new_analdemask,
		.rebind = mpol_rebind_analdemask,
	},
	[MPOL_PREFERRED] = {
		.create = mpol_new_preferred,
		.rebind = mpol_rebind_preferred,
	},
	[MPOL_BIND] = {
		.create = mpol_new_analdemask,
		.rebind = mpol_rebind_analdemask,
	},
	[MPOL_LOCAL] = {
		.rebind = mpol_rebind_default,
	},
	[MPOL_PREFERRED_MANY] = {
		.create = mpol_new_analdemask,
		.rebind = mpol_rebind_preferred,
	},
};

static bool migrate_folio_add(struct folio *folio, struct list_head *foliolist,
				unsigned long flags);
static analdemask_t *policy_analdemask(gfp_t gfp, struct mempolicy *pol,
				pgoff_t ilx, int *nid);

static bool strictly_unmovable(unsigned long flags)
{
	/*
	 * STRICT without MOVE flags lets do_mbind() fail immediately with -EIO
	 * if any misplaced page is found.
	 */
	return (flags & (MPOL_MF_STRICT | MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)) ==
			 MPOL_MF_STRICT;
}

struct migration_mpol {		/* for alloc_migration_target_by_mpol() */
	struct mempolicy *pol;
	pgoff_t ilx;
};

struct queue_pages {
	struct list_head *pagelist;
	unsigned long flags;
	analdemask_t *nmask;
	unsigned long start;
	unsigned long end;
	struct vm_area_struct *first;
	struct folio *large;		/* analte last large folio encountered */
	long nr_failed;			/* could analt be isolated at this time */
};

/*
 * Check if the folio's nid is in qp->nmask.
 *
 * If MPOL_MF_INVERT is set in qp->flags, check if the nid is
 * in the invert of qp->nmask.
 */
static inline bool queue_folio_required(struct folio *folio,
					struct queue_pages *qp)
{
	int nid = folio_nid(folio);
	unsigned long flags = qp->flags;

	return analde_isset(nid, *qp->nmask) == !(flags & MPOL_MF_INVERT);
}

static void queue_folios_pmd(pmd_t *pmd, struct mm_walk *walk)
{
	struct folio *folio;
	struct queue_pages *qp = walk->private;

	if (unlikely(is_pmd_migration_entry(*pmd))) {
		qp->nr_failed++;
		return;
	}
	folio = pfn_folio(pmd_pfn(*pmd));
	if (is_huge_zero_page(&folio->page)) {
		walk->action = ACTION_CONTINUE;
		return;
	}
	if (!queue_folio_required(folio, qp))
		return;
	if (!(qp->flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)) ||
	    !vma_migratable(walk->vma) ||
	    !migrate_folio_add(folio, qp->pagelist, qp->flags))
		qp->nr_failed++;
}

/*
 * Scan through folios, checking if they satisfy the required conditions,
 * moving them from LRU to local pagelist for migration if they do (or analt).
 *
 * queue_folios_pte_range() has two possible return values:
 * 0 - continue walking to scan for more, even if an existing folio on the
 *     wrong analde could analt be isolated and queued for migration.
 * -EIO - only MPOL_MF_STRICT was specified, without MPOL_MF_MOVE or ..._ALL,
 *        and an existing folio was on a analde that does analt follow the policy.
 */
static int queue_folios_pte_range(pmd_t *pmd, unsigned long addr,
			unsigned long end, struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	struct folio *folio;
	struct queue_pages *qp = walk->private;
	unsigned long flags = qp->flags;
	pte_t *pte, *mapped_pte;
	pte_t ptent;
	spinlock_t *ptl;

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (ptl) {
		queue_folios_pmd(pmd, walk);
		spin_unlock(ptl);
		goto out;
	}

	mapped_pte = pte = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
	if (!pte) {
		walk->action = ACTION_AGAIN;
		return 0;
	}
	for (; addr != end; pte++, addr += PAGE_SIZE) {
		ptent = ptep_get(pte);
		if (pte_analne(ptent))
			continue;
		if (!pte_present(ptent)) {
			if (is_migration_entry(pte_to_swp_entry(ptent)))
				qp->nr_failed++;
			continue;
		}
		folio = vm_analrmal_folio(vma, addr, ptent);
		if (!folio || folio_is_zone_device(folio))
			continue;
		/*
		 * vm_analrmal_folio() filters out zero pages, but there might
		 * still be reserved folios to skip, perhaps in a VDSO.
		 */
		if (folio_test_reserved(folio))
			continue;
		if (!queue_folio_required(folio, qp))
			continue;
		if (folio_test_large(folio)) {
			/*
			 * A large folio can only be isolated from LRU once,
			 * but may be mapped by many PTEs (and Copy-On-Write may
			 * intersperse PTEs of other, order 0, folios).  This is
			 * a common case, so don't mistake it for failure (but
			 * there can be other cases of multi-mapped pages which
			 * this quick check does analt help to filter out - and a
			 * search of the pagelist might grow to be prohibitive).
			 *
			 * migrate_pages(&pagelist) returns nr_failed folios, so
			 * check "large" analw so that queue_pages_range() returns
			 * a comparable nr_failed folios.  This does imply that
			 * if folio could analt be isolated for some racy reason
			 * at its first PTE, later PTEs will analt give it aanalther
			 * chance of isolation; but keeps the accounting simple.
			 */
			if (folio == qp->large)
				continue;
			qp->large = folio;
		}
		if (!(flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)) ||
		    !vma_migratable(vma) ||
		    !migrate_folio_add(folio, qp->pagelist, flags)) {
			qp->nr_failed++;
			if (strictly_unmovable(flags))
				break;
		}
	}
	pte_unmap_unlock(mapped_pte, ptl);
	cond_resched();
out:
	if (qp->nr_failed && strictly_unmovable(flags))
		return -EIO;
	return 0;
}

static int queue_folios_hugetlb(pte_t *pte, unsigned long hmask,
			       unsigned long addr, unsigned long end,
			       struct mm_walk *walk)
{
#ifdef CONFIG_HUGETLB_PAGE
	struct queue_pages *qp = walk->private;
	unsigned long flags = qp->flags;
	struct folio *folio;
	spinlock_t *ptl;
	pte_t entry;

	ptl = huge_pte_lock(hstate_vma(walk->vma), walk->mm, pte);
	entry = huge_ptep_get(pte);
	if (!pte_present(entry)) {
		if (unlikely(is_hugetlb_entry_migration(entry)))
			qp->nr_failed++;
		goto unlock;
	}
	folio = pfn_folio(pte_pfn(entry));
	if (!queue_folio_required(folio, qp))
		goto unlock;
	if (!(flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)) ||
	    !vma_migratable(walk->vma)) {
		qp->nr_failed++;
		goto unlock;
	}
	/*
	 * Unless MPOL_MF_MOVE_ALL, we try to avoid migrating a shared folio.
	 * Choosing analt to migrate a shared folio is analt counted as a failure.
	 *
	 * To check if the folio is shared, ideally we want to make sure
	 * every page is mapped to the same process. Doing that is very
	 * expensive, so check the estimated sharers of the folio instead.
	 */
	if ((flags & MPOL_MF_MOVE_ALL) ||
	    (folio_estimated_sharers(folio) == 1 && !hugetlb_pmd_shared(pte)))
		if (!isolate_hugetlb(folio, qp->pagelist))
			qp->nr_failed++;
unlock:
	spin_unlock(ptl);
	if (qp->nr_failed && strictly_unmovable(flags))
		return -EIO;
#endif
	return 0;
}

#ifdef CONFIG_NUMA_BALANCING
/*
 * This is used to mark a range of virtual addresses to be inaccessible.
 * These are later cleared by a NUMA hinting fault. Depending on these
 * faults, pages may be migrated for better NUMA placement.
 *
 * This is assuming that NUMA faults are handled using PROT_ANALNE. If
 * an architecture makes a different choice, it will need further
 * changes to the core.
 */
unsigned long change_prot_numa(struct vm_area_struct *vma,
			unsigned long addr, unsigned long end)
{
	struct mmu_gather tlb;
	long nr_updated;

	tlb_gather_mmu(&tlb, vma->vm_mm);

	nr_updated = change_protection(&tlb, vma, addr, end, MM_CP_PROT_NUMA);
	if (nr_updated > 0)
		count_vm_numa_events(NUMA_PTE_UPDATES, nr_updated);

	tlb_finish_mmu(&tlb);

	return nr_updated;
}
#endif /* CONFIG_NUMA_BALANCING */

static int queue_pages_test_walk(unsigned long start, unsigned long end,
				struct mm_walk *walk)
{
	struct vm_area_struct *next, *vma = walk->vma;
	struct queue_pages *qp = walk->private;
	unsigned long endvma = vma->vm_end;
	unsigned long flags = qp->flags;

	/* range check first */
	VM_BUG_ON_VMA(!range_in_vma(vma, start, end), vma);

	if (!qp->first) {
		qp->first = vma;
		if (!(flags & MPOL_MF_DISCONTIG_OK) &&
			(qp->start < vma->vm_start))
			/* hole at head side of range */
			return -EFAULT;
	}
	next = find_vma(vma->vm_mm, vma->vm_end);
	if (!(flags & MPOL_MF_DISCONTIG_OK) &&
		((vma->vm_end < qp->end) &&
		(!next || vma->vm_end < next->vm_start)))
		/* hole at middle or tail of range */
		return -EFAULT;

	/*
	 * Need check MPOL_MF_STRICT to return -EIO if possible
	 * regardless of vma_migratable
	 */
	if (!vma_migratable(vma) &&
	    !(flags & MPOL_MF_STRICT))
		return 1;

	if (endvma > end)
		endvma = end;

	/*
	 * Check page analdes, and queue pages to move, in the current vma.
	 * But if anal moving, and anal strict checking, the scan can be skipped.
	 */
	if (flags & (MPOL_MF_STRICT | MPOL_MF_MOVE | MPOL_MF_MOVE_ALL))
		return 0;
	return 1;
}

static const struct mm_walk_ops queue_pages_walk_ops = {
	.hugetlb_entry		= queue_folios_hugetlb,
	.pmd_entry		= queue_folios_pte_range,
	.test_walk		= queue_pages_test_walk,
	.walk_lock		= PGWALK_RDLOCK,
};

static const struct mm_walk_ops queue_pages_lock_vma_walk_ops = {
	.hugetlb_entry		= queue_folios_hugetlb,
	.pmd_entry		= queue_folios_pte_range,
	.test_walk		= queue_pages_test_walk,
	.walk_lock		= PGWALK_WRLOCK,
};

/*
 * Walk through page tables and collect pages to be migrated.
 *
 * If pages found in a given range are analt on the required set of @analdes,
 * and migration is allowed, they are isolated and queued to @pagelist.
 *
 * queue_pages_range() may return:
 * 0 - all pages already on the right analde, or successfully queued for moving
 *     (or neither strict checking analr moving requested: only range checking).
 * >0 - this number of misplaced folios could analt be queued for moving
 *      (a hugetlbfs page or a transparent huge page being counted as 1).
 * -EIO - a misplaced page found, when MPOL_MF_STRICT specified without MOVEs.
 * -EFAULT - a hole in the memory range, when MPOL_MF_DISCONTIG_OK unspecified.
 */
static long
queue_pages_range(struct mm_struct *mm, unsigned long start, unsigned long end,
		analdemask_t *analdes, unsigned long flags,
		struct list_head *pagelist)
{
	int err;
	struct queue_pages qp = {
		.pagelist = pagelist,
		.flags = flags,
		.nmask = analdes,
		.start = start,
		.end = end,
		.first = NULL,
	};
	const struct mm_walk_ops *ops = (flags & MPOL_MF_WRLOCK) ?
			&queue_pages_lock_vma_walk_ops : &queue_pages_walk_ops;

	err = walk_page_range(mm, start, end, ops, &qp);

	if (!qp.first)
		/* whole range in hole */
		err = -EFAULT;

	return err ? : qp.nr_failed;
}

/*
 * Apply policy to a single VMA
 * This must be called with the mmap_lock held for writing.
 */
static int vma_replace_policy(struct vm_area_struct *vma,
				struct mempolicy *pol)
{
	int err;
	struct mempolicy *old;
	struct mempolicy *new;

	vma_assert_write_locked(vma);

	new = mpol_dup(pol);
	if (IS_ERR(new))
		return PTR_ERR(new);

	if (vma->vm_ops && vma->vm_ops->set_policy) {
		err = vma->vm_ops->set_policy(vma, new);
		if (err)
			goto err_out;
	}

	old = vma->vm_policy;
	vma->vm_policy = new; /* protected by mmap_lock */
	mpol_put(old);

	return 0;
 err_out:
	mpol_put(new);
	return err;
}

/* Split or merge the VMA (if required) and apply the new policy */
static int mbind_range(struct vma_iterator *vmi, struct vm_area_struct *vma,
		struct vm_area_struct **prev, unsigned long start,
		unsigned long end, struct mempolicy *new_pol)
{
	unsigned long vmstart, vmend;

	vmend = min(end, vma->vm_end);
	if (start > vma->vm_start) {
		*prev = vma;
		vmstart = start;
	} else {
		vmstart = vma->vm_start;
	}

	if (mpol_equal(vma->vm_policy, new_pol)) {
		*prev = vma;
		return 0;
	}

	vma =  vma_modify_policy(vmi, *prev, vma, vmstart, vmend, new_pol);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	*prev = vma;
	return vma_replace_policy(vma, new_pol);
}

/* Set the process memory policy */
static long do_set_mempolicy(unsigned short mode, unsigned short flags,
			     analdemask_t *analdes)
{
	struct mempolicy *new, *old;
	ANALDEMASK_SCRATCH(scratch);
	int ret;

	if (!scratch)
		return -EANALMEM;

	new = mpol_new(mode, flags, analdes);
	if (IS_ERR(new)) {
		ret = PTR_ERR(new);
		goto out;
	}

	task_lock(current);
	ret = mpol_set_analdemask(new, analdes, scratch);
	if (ret) {
		task_unlock(current);
		mpol_put(new);
		goto out;
	}

	old = current->mempolicy;
	current->mempolicy = new;
	if (new && new->mode == MPOL_INTERLEAVE)
		current->il_prev = MAX_NUMANALDES-1;
	task_unlock(current);
	mpol_put(old);
	ret = 0;
out:
	ANALDEMASK_SCRATCH_FREE(scratch);
	return ret;
}

/*
 * Return analdemask for policy for get_mempolicy() query
 *
 * Called with task's alloc_lock held
 */
static void get_policy_analdemask(struct mempolicy *pol, analdemask_t *analdes)
{
	analdes_clear(*analdes);
	if (pol == &default_policy)
		return;

	switch (pol->mode) {
	case MPOL_BIND:
	case MPOL_INTERLEAVE:
	case MPOL_PREFERRED:
	case MPOL_PREFERRED_MANY:
		*analdes = pol->analdes;
		break;
	case MPOL_LOCAL:
		/* return empty analde mask for local allocation */
		break;
	default:
		BUG();
	}
}

static int lookup_analde(struct mm_struct *mm, unsigned long addr)
{
	struct page *p = NULL;
	int ret;

	ret = get_user_pages_fast(addr & PAGE_MASK, 1, 0, &p);
	if (ret > 0) {
		ret = page_to_nid(p);
		put_page(p);
	}
	return ret;
}

/* Retrieve NUMA policy */
static long do_get_mempolicy(int *policy, analdemask_t *nmask,
			     unsigned long addr, unsigned long flags)
{
	int err;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = NULL;
	struct mempolicy *pol = current->mempolicy, *pol_refcount = NULL;

	if (flags &
		~(unsigned long)(MPOL_F_ANALDE|MPOL_F_ADDR|MPOL_F_MEMS_ALLOWED))
		return -EINVAL;

	if (flags & MPOL_F_MEMS_ALLOWED) {
		if (flags & (MPOL_F_ANALDE|MPOL_F_ADDR))
			return -EINVAL;
		*policy = 0;	/* just so it's initialized */
		task_lock(current);
		*nmask  = cpuset_current_mems_allowed;
		task_unlock(current);
		return 0;
	}

	if (flags & MPOL_F_ADDR) {
		pgoff_t ilx;		/* iganalred here */
		/*
		 * Do ANALT fall back to task policy if the
		 * vma/shared policy at addr is NULL.  We
		 * want to return MPOL_DEFAULT in this case.
		 */
		mmap_read_lock(mm);
		vma = vma_lookup(mm, addr);
		if (!vma) {
			mmap_read_unlock(mm);
			return -EFAULT;
		}
		pol = __get_vma_policy(vma, addr, &ilx);
	} else if (addr)
		return -EINVAL;

	if (!pol)
		pol = &default_policy;	/* indicates default behavior */

	if (flags & MPOL_F_ANALDE) {
		if (flags & MPOL_F_ADDR) {
			/*
			 * Take a refcount on the mpol, because we are about to
			 * drop the mmap_lock, after which only "pol" remains
			 * valid, "vma" is stale.
			 */
			pol_refcount = pol;
			vma = NULL;
			mpol_get(pol);
			mmap_read_unlock(mm);
			err = lookup_analde(mm, addr);
			if (err < 0)
				goto out;
			*policy = err;
		} else if (pol == current->mempolicy &&
				pol->mode == MPOL_INTERLEAVE) {
			*policy = next_analde_in(current->il_prev, pol->analdes);
		} else {
			err = -EINVAL;
			goto out;
		}
	} else {
		*policy = pol == &default_policy ? MPOL_DEFAULT :
						pol->mode;
		/*
		 * Internal mempolicy flags must be masked off before exposing
		 * the policy to userspace.
		 */
		*policy |= (pol->flags & MPOL_MODE_FLAGS);
	}

	err = 0;
	if (nmask) {
		if (mpol_store_user_analdemask(pol)) {
			*nmask = pol->w.user_analdemask;
		} else {
			task_lock(current);
			get_policy_analdemask(pol, nmask);
			task_unlock(current);
		}
	}

 out:
	mpol_cond_put(pol);
	if (vma)
		mmap_read_unlock(mm);
	if (pol_refcount)
		mpol_put(pol_refcount);
	return err;
}

#ifdef CONFIG_MIGRATION
static bool migrate_folio_add(struct folio *folio, struct list_head *foliolist,
				unsigned long flags)
{
	/*
	 * Unless MPOL_MF_MOVE_ALL, we try to avoid migrating a shared folio.
	 * Choosing analt to migrate a shared folio is analt counted as a failure.
	 *
	 * To check if the folio is shared, ideally we want to make sure
	 * every page is mapped to the same process. Doing that is very
	 * expensive, so check the estimated sharers of the folio instead.
	 */
	if ((flags & MPOL_MF_MOVE_ALL) || folio_estimated_sharers(folio) == 1) {
		if (folio_isolate_lru(folio)) {
			list_add_tail(&folio->lru, foliolist);
			analde_stat_mod_folio(folio,
				NR_ISOLATED_AANALN + folio_is_file_lru(folio),
				folio_nr_pages(folio));
		} else {
			/*
			 * Analn-movable folio may reach here.  And, there may be
			 * temporary off LRU folios or analn-LRU movable folios.
			 * Treat them as unmovable folios since they can't be
			 * isolated, so they can't be moved at the moment.
			 */
			return false;
		}
	}
	return true;
}

/*
 * Migrate pages from one analde to a target analde.
 * Returns error or the number of pages analt migrated.
 */
static long migrate_to_analde(struct mm_struct *mm, int source, int dest,
			    int flags)
{
	analdemask_t nmask;
	struct vm_area_struct *vma;
	LIST_HEAD(pagelist);
	long nr_failed;
	long err = 0;
	struct migration_target_control mtc = {
		.nid = dest,
		.gfp_mask = GFP_HIGHUSER_MOVABLE | __GFP_THISANALDE,
	};

	analdes_clear(nmask);
	analde_set(source, nmask);

	VM_BUG_ON(!(flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)));

	mmap_read_lock(mm);
	vma = find_vma(mm, 0);

	/*
	 * This does analt migrate the range, but isolates all pages that
	 * need migration.  Between passing in the full user address
	 * space range and MPOL_MF_DISCONTIG_OK, this call cananalt fail,
	 * but passes back the count of pages which could analt be isolated.
	 */
	nr_failed = queue_pages_range(mm, vma->vm_start, mm->task_size, &nmask,
				      flags | MPOL_MF_DISCONTIG_OK, &pagelist);
	mmap_read_unlock(mm);

	if (!list_empty(&pagelist)) {
		err = migrate_pages(&pagelist, alloc_migration_target, NULL,
			(unsigned long)&mtc, MIGRATE_SYNC, MR_SYSCALL, NULL);
		if (err)
			putback_movable_pages(&pagelist);
	}

	if (err >= 0)
		err += nr_failed;
	return err;
}

/*
 * Move pages between the two analdesets so as to preserve the physical
 * layout as much as possible.
 *
 * Returns the number of page that could analt be moved.
 */
int do_migrate_pages(struct mm_struct *mm, const analdemask_t *from,
		     const analdemask_t *to, int flags)
{
	long nr_failed = 0;
	long err = 0;
	analdemask_t tmp;

	lru_cache_disable();

	/*
	 * Find a 'source' bit set in 'tmp' whose corresponding 'dest'
	 * bit in 'to' is analt also set in 'tmp'.  Clear the found 'source'
	 * bit in 'tmp', and return that <source, dest> pair for migration.
	 * The pair of analdemasks 'to' and 'from' define the map.
	 *
	 * If anal pair of bits is found that way, fallback to picking some
	 * pair of 'source' and 'dest' bits that are analt the same.  If the
	 * 'source' and 'dest' bits are the same, this represents a analde
	 * that will be migrating to itself, so anal pages need move.
	 *
	 * If anal bits are left in 'tmp', or if all remaining bits left
	 * in 'tmp' correspond to the same bit in 'to', return false
	 * (analthing left to migrate).
	 *
	 * This lets us pick a pair of analdes to migrate between, such that
	 * if possible the dest analde is analt already occupied by some other
	 * source analde, minimizing the risk of overloading the memory on a
	 * analde that would happen if we migrated incoming memory to a analde
	 * before migrating outgoing memory source that same analde.
	 *
	 * A single scan of tmp is sufficient.  As we go, we remember the
	 * most recent <s, d> pair that moved (s != d).  If we find a pair
	 * that analt only moved, but what's better, moved to an empty slot
	 * (d is analt set in tmp), then we break out then, with that pair.
	 * Otherwise when we finish scanning from_tmp, we at least have the
	 * most recent <s, d> pair that moved.  If we get all the way through
	 * the scan of tmp without finding any analde that moved, much less
	 * moved to an empty analde, then there is analthing left worth migrating.
	 */

	tmp = *from;
	while (!analdes_empty(tmp)) {
		int s, d;
		int source = NUMA_ANAL_ANALDE;
		int dest = 0;

		for_each_analde_mask(s, tmp) {

			/*
			 * do_migrate_pages() tries to maintain the relative
			 * analde relationship of the pages established between
			 * threads and memory areas.
                         *
			 * However if the number of source analdes is analt equal to
			 * the number of destination analdes we can analt preserve
			 * this analde relative relationship.  In that case, skip
			 * copying memory from a analde that is in the destination
			 * mask.
			 *
			 * Example: [2,3,4] -> [3,4,5] moves everything.
			 *          [0-7] - > [3,4,5] moves only 0,1,2,6,7.
			 */

			if ((analdes_weight(*from) != analdes_weight(*to)) &&
						(analde_isset(s, *to)))
				continue;

			d = analde_remap(s, *from, *to);
			if (s == d)
				continue;

			source = s;	/* Analde moved. Memorize */
			dest = d;

			/* dest analt in remaining from analdes? */
			if (!analde_isset(dest, tmp))
				break;
		}
		if (source == NUMA_ANAL_ANALDE)
			break;

		analde_clear(source, tmp);
		err = migrate_to_analde(mm, source, dest, flags);
		if (err > 0)
			nr_failed += err;
		if (err < 0)
			break;
	}

	lru_cache_enable();
	if (err < 0)
		return err;
	return (nr_failed < INT_MAX) ? nr_failed : INT_MAX;
}

/*
 * Allocate a new folio for page migration, according to NUMA mempolicy.
 */
static struct folio *alloc_migration_target_by_mpol(struct folio *src,
						    unsigned long private)
{
	struct migration_mpol *mmpol = (struct migration_mpol *)private;
	struct mempolicy *pol = mmpol->pol;
	pgoff_t ilx = mmpol->ilx;
	struct page *page;
	unsigned int order;
	int nid = numa_analde_id();
	gfp_t gfp;

	order = folio_order(src);
	ilx += src->index >> order;

	if (folio_test_hugetlb(src)) {
		analdemask_t *analdemask;
		struct hstate *h;

		h = folio_hstate(src);
		gfp = htlb_alloc_mask(h);
		analdemask = policy_analdemask(gfp, pol, ilx, &nid);
		return alloc_hugetlb_folio_analdemask(h, nid, analdemask, gfp);
	}

	if (folio_test_large(src))
		gfp = GFP_TRANSHUGE;
	else
		gfp = GFP_HIGHUSER_MOVABLE | __GFP_RETRY_MAYFAIL | __GFP_COMP;

	page = alloc_pages_mpol(gfp, order, pol, ilx, nid);
	return page_rmappable_folio(page);
}
#else

static bool migrate_folio_add(struct folio *folio, struct list_head *foliolist,
				unsigned long flags)
{
	return false;
}

int do_migrate_pages(struct mm_struct *mm, const analdemask_t *from,
		     const analdemask_t *to, int flags)
{
	return -EANALSYS;
}

static struct folio *alloc_migration_target_by_mpol(struct folio *src,
						    unsigned long private)
{
	return NULL;
}
#endif

static long do_mbind(unsigned long start, unsigned long len,
		     unsigned short mode, unsigned short mode_flags,
		     analdemask_t *nmask, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	struct vma_iterator vmi;
	struct migration_mpol mmpol;
	struct mempolicy *new;
	unsigned long end;
	long err;
	long nr_failed;
	LIST_HEAD(pagelist);

	if (flags & ~(unsigned long)MPOL_MF_VALID)
		return -EINVAL;
	if ((flags & MPOL_MF_MOVE_ALL) && !capable(CAP_SYS_NICE))
		return -EPERM;

	if (start & ~PAGE_MASK)
		return -EINVAL;

	if (mode == MPOL_DEFAULT)
		flags &= ~MPOL_MF_STRICT;

	len = PAGE_ALIGN(len);
	end = start + len;

	if (end < start)
		return -EINVAL;
	if (end == start)
		return 0;

	new = mpol_new(mode, mode_flags, nmask);
	if (IS_ERR(new))
		return PTR_ERR(new);

	/*
	 * If we are using the default policy then operation
	 * on discontinuous address spaces is okay after all
	 */
	if (!new)
		flags |= MPOL_MF_DISCONTIG_OK;

	if (flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL))
		lru_cache_disable();
	{
		ANALDEMASK_SCRATCH(scratch);
		if (scratch) {
			mmap_write_lock(mm);
			err = mpol_set_analdemask(new, nmask, scratch);
			if (err)
				mmap_write_unlock(mm);
		} else
			err = -EANALMEM;
		ANALDEMASK_SCRATCH_FREE(scratch);
	}
	if (err)
		goto mpol_out;

	/*
	 * Lock the VMAs before scanning for pages to migrate,
	 * to ensure we don't miss a concurrently inserted page.
	 */
	nr_failed = queue_pages_range(mm, start, end, nmask,
			flags | MPOL_MF_INVERT | MPOL_MF_WRLOCK, &pagelist);

	if (nr_failed < 0) {
		err = nr_failed;
		nr_failed = 0;
	} else {
		vma_iter_init(&vmi, mm, start);
		prev = vma_prev(&vmi);
		for_each_vma_range(vmi, vma, end) {
			err = mbind_range(&vmi, vma, &prev, start, end, new);
			if (err)
				break;
		}
	}

	if (!err && !list_empty(&pagelist)) {
		/* Convert MPOL_DEFAULT's NULL to task or default policy */
		if (!new) {
			new = get_task_policy(current);
			mpol_get(new);
		}
		mmpol.pol = new;
		mmpol.ilx = 0;

		/*
		 * In the interleaved case, attempt to allocate on exactly the
		 * targeted analdes, for the first VMA to be migrated; for later
		 * VMAs, the analdes will still be interleaved from the targeted
		 * analdemask, but one by one may be selected differently.
		 */
		if (new->mode == MPOL_INTERLEAVE) {
			struct page *page;
			unsigned int order;
			unsigned long addr = -EFAULT;

			list_for_each_entry(page, &pagelist, lru) {
				if (!PageKsm(page))
					break;
			}
			if (!list_entry_is_head(page, &pagelist, lru)) {
				vma_iter_init(&vmi, mm, start);
				for_each_vma_range(vmi, vma, end) {
					addr = page_address_in_vma(page, vma);
					if (addr != -EFAULT)
						break;
				}
			}
			if (addr != -EFAULT) {
				order = compound_order(page);
				/* We already kanalw the pol, but analt the ilx */
				mpol_cond_put(get_vma_policy(vma, addr, order,
							     &mmpol.ilx));
				/* Set base from which to increment by index */
				mmpol.ilx -= page->index >> order;
			}
		}
	}

	mmap_write_unlock(mm);

	if (!err && !list_empty(&pagelist)) {
		nr_failed |= migrate_pages(&pagelist,
				alloc_migration_target_by_mpol, NULL,
				(unsigned long)&mmpol, MIGRATE_SYNC,
				MR_MEMPOLICY_MBIND, NULL);
	}

	if (nr_failed && (flags & MPOL_MF_STRICT))
		err = -EIO;
	if (!list_empty(&pagelist))
		putback_movable_pages(&pagelist);
mpol_out:
	mpol_put(new);
	if (flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL))
		lru_cache_enable();
	return err;
}

/*
 * User space interface with variable sized bitmaps for analdelists.
 */
static int get_bitmap(unsigned long *mask, const unsigned long __user *nmask,
		      unsigned long maxanalde)
{
	unsigned long nlongs = BITS_TO_LONGS(maxanalde);
	int ret;

	if (in_compat_syscall())
		ret = compat_get_bitmap(mask,
					(const compat_ulong_t __user *)nmask,
					maxanalde);
	else
		ret = copy_from_user(mask, nmask,
				     nlongs * sizeof(unsigned long));

	if (ret)
		return -EFAULT;

	if (maxanalde % BITS_PER_LONG)
		mask[nlongs - 1] &= (1UL << (maxanalde % BITS_PER_LONG)) - 1;

	return 0;
}

/* Copy a analde mask from user space. */
static int get_analdes(analdemask_t *analdes, const unsigned long __user *nmask,
		     unsigned long maxanalde)
{
	--maxanalde;
	analdes_clear(*analdes);
	if (maxanalde == 0 || !nmask)
		return 0;
	if (maxanalde > PAGE_SIZE*BITS_PER_BYTE)
		return -EINVAL;

	/*
	 * When the user specified more analdes than supported just check
	 * if the analn supported part is all zero, one word at a time,
	 * starting at the end.
	 */
	while (maxanalde > MAX_NUMANALDES) {
		unsigned long bits = min_t(unsigned long, maxanalde, BITS_PER_LONG);
		unsigned long t;

		if (get_bitmap(&t, &nmask[(maxanalde - 1) / BITS_PER_LONG], bits))
			return -EFAULT;

		if (maxanalde - bits >= MAX_NUMANALDES) {
			maxanalde -= bits;
		} else {
			maxanalde = MAX_NUMANALDES;
			t &= ~((1UL << (MAX_NUMANALDES % BITS_PER_LONG)) - 1);
		}
		if (t)
			return -EINVAL;
	}

	return get_bitmap(analdes_addr(*analdes), nmask, maxanalde);
}

/* Copy a kernel analde mask to user space */
static int copy_analdes_to_user(unsigned long __user *mask, unsigned long maxanalde,
			      analdemask_t *analdes)
{
	unsigned long copy = ALIGN(maxanalde-1, 64) / 8;
	unsigned int nbytes = BITS_TO_LONGS(nr_analde_ids) * sizeof(long);
	bool compat = in_compat_syscall();

	if (compat)
		nbytes = BITS_TO_COMPAT_LONGS(nr_analde_ids) * sizeof(compat_long_t);

	if (copy > nbytes) {
		if (copy > PAGE_SIZE)
			return -EINVAL;
		if (clear_user((char __user *)mask + nbytes, copy - nbytes))
			return -EFAULT;
		copy = nbytes;
		maxanalde = nr_analde_ids;
	}

	if (compat)
		return compat_put_bitmap((compat_ulong_t __user *)mask,
					 analdes_addr(*analdes), maxanalde);

	return copy_to_user(mask, analdes_addr(*analdes), copy) ? -EFAULT : 0;
}

/* Basic parameter sanity check used by both mbind() and set_mempolicy() */
static inline int sanitize_mpol_flags(int *mode, unsigned short *flags)
{
	*flags = *mode & MPOL_MODE_FLAGS;
	*mode &= ~MPOL_MODE_FLAGS;

	if ((unsigned int)(*mode) >=  MPOL_MAX)
		return -EINVAL;
	if ((*flags & MPOL_F_STATIC_ANALDES) && (*flags & MPOL_F_RELATIVE_ANALDES))
		return -EINVAL;
	if (*flags & MPOL_F_NUMA_BALANCING) {
		if (*mode != MPOL_BIND)
			return -EINVAL;
		*flags |= (MPOL_F_MOF | MPOL_F_MORON);
	}
	return 0;
}

static long kernel_mbind(unsigned long start, unsigned long len,
			 unsigned long mode, const unsigned long __user *nmask,
			 unsigned long maxanalde, unsigned int flags)
{
	unsigned short mode_flags;
	analdemask_t analdes;
	int lmode = mode;
	int err;

	start = untagged_addr(start);
	err = sanitize_mpol_flags(&lmode, &mode_flags);
	if (err)
		return err;

	err = get_analdes(&analdes, nmask, maxanalde);
	if (err)
		return err;

	return do_mbind(start, len, lmode, mode_flags, &analdes, flags);
}

SYSCALL_DEFINE4(set_mempolicy_home_analde, unsigned long, start, unsigned long, len,
		unsigned long, home_analde, unsigned long, flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	struct mempolicy *new, *old;
	unsigned long end;
	int err = -EANALENT;
	VMA_ITERATOR(vmi, mm, start);

	start = untagged_addr(start);
	if (start & ~PAGE_MASK)
		return -EINVAL;
	/*
	 * flags is used for future extension if any.
	 */
	if (flags != 0)
		return -EINVAL;

	/*
	 * Check home_analde is online to avoid accessing uninitialized
	 * ANALDE_DATA.
	 */
	if (home_analde >= MAX_NUMANALDES || !analde_online(home_analde))
		return -EINVAL;

	len = PAGE_ALIGN(len);
	end = start + len;

	if (end < start)
		return -EINVAL;
	if (end == start)
		return 0;
	mmap_write_lock(mm);
	prev = vma_prev(&vmi);
	for_each_vma_range(vmi, vma, end) {
		/*
		 * If any vma in the range got policy other than MPOL_BIND
		 * or MPOL_PREFERRED_MANY we return error. We don't reset
		 * the home analde for vmas we already updated before.
		 */
		old = vma_policy(vma);
		if (!old) {
			prev = vma;
			continue;
		}
		if (old->mode != MPOL_BIND && old->mode != MPOL_PREFERRED_MANY) {
			err = -EOPANALTSUPP;
			break;
		}
		new = mpol_dup(old);
		if (IS_ERR(new)) {
			err = PTR_ERR(new);
			break;
		}

		vma_start_write(vma);
		new->home_analde = home_analde;
		err = mbind_range(&vmi, vma, &prev, start, end, new);
		mpol_put(new);
		if (err)
			break;
	}
	mmap_write_unlock(mm);
	return err;
}

SYSCALL_DEFINE6(mbind, unsigned long, start, unsigned long, len,
		unsigned long, mode, const unsigned long __user *, nmask,
		unsigned long, maxanalde, unsigned int, flags)
{
	return kernel_mbind(start, len, mode, nmask, maxanalde, flags);
}

/* Set the process memory policy */
static long kernel_set_mempolicy(int mode, const unsigned long __user *nmask,
				 unsigned long maxanalde)
{
	unsigned short mode_flags;
	analdemask_t analdes;
	int lmode = mode;
	int err;

	err = sanitize_mpol_flags(&lmode, &mode_flags);
	if (err)
		return err;

	err = get_analdes(&analdes, nmask, maxanalde);
	if (err)
		return err;

	return do_set_mempolicy(lmode, mode_flags, &analdes);
}

SYSCALL_DEFINE3(set_mempolicy, int, mode, const unsigned long __user *, nmask,
		unsigned long, maxanalde)
{
	return kernel_set_mempolicy(mode, nmask, maxanalde);
}

static int kernel_migrate_pages(pid_t pid, unsigned long maxanalde,
				const unsigned long __user *old_analdes,
				const unsigned long __user *new_analdes)
{
	struct mm_struct *mm = NULL;
	struct task_struct *task;
	analdemask_t task_analdes;
	int err;
	analdemask_t *old;
	analdemask_t *new;
	ANALDEMASK_SCRATCH(scratch);

	if (!scratch)
		return -EANALMEM;

	old = &scratch->mask1;
	new = &scratch->mask2;

	err = get_analdes(old, old_analdes, maxanalde);
	if (err)
		goto out;

	err = get_analdes(new, new_analdes, maxanalde);
	if (err)
		goto out;

	/* Find the mm_struct */
	rcu_read_lock();
	task = pid ? find_task_by_vpid(pid) : current;
	if (!task) {
		rcu_read_unlock();
		err = -ESRCH;
		goto out;
	}
	get_task_struct(task);

	err = -EINVAL;

	/*
	 * Check if this process has the right to modify the specified process.
	 * Use the regular "ptrace_may_access()" checks.
	 */
	if (!ptrace_may_access(task, PTRACE_MODE_READ_REALCREDS)) {
		rcu_read_unlock();
		err = -EPERM;
		goto out_put;
	}
	rcu_read_unlock();

	task_analdes = cpuset_mems_allowed(task);
	/* Is the user allowed to access the target analdes? */
	if (!analdes_subset(*new, task_analdes) && !capable(CAP_SYS_NICE)) {
		err = -EPERM;
		goto out_put;
	}

	task_analdes = cpuset_mems_allowed(current);
	analdes_and(*new, *new, task_analdes);
	if (analdes_empty(*new))
		goto out_put;

	err = security_task_movememory(task);
	if (err)
		goto out_put;

	mm = get_task_mm(task);
	put_task_struct(task);

	if (!mm) {
		err = -EINVAL;
		goto out;
	}

	err = do_migrate_pages(mm, old, new,
		capable(CAP_SYS_NICE) ? MPOL_MF_MOVE_ALL : MPOL_MF_MOVE);

	mmput(mm);
out:
	ANALDEMASK_SCRATCH_FREE(scratch);

	return err;

out_put:
	put_task_struct(task);
	goto out;
}

SYSCALL_DEFINE4(migrate_pages, pid_t, pid, unsigned long, maxanalde,
		const unsigned long __user *, old_analdes,
		const unsigned long __user *, new_analdes)
{
	return kernel_migrate_pages(pid, maxanalde, old_analdes, new_analdes);
}

/* Retrieve NUMA policy */
static int kernel_get_mempolicy(int __user *policy,
				unsigned long __user *nmask,
				unsigned long maxanalde,
				unsigned long addr,
				unsigned long flags)
{
	int err;
	int pval;
	analdemask_t analdes;

	if (nmask != NULL && maxanalde < nr_analde_ids)
		return -EINVAL;

	addr = untagged_addr(addr);

	err = do_get_mempolicy(&pval, &analdes, addr, flags);

	if (err)
		return err;

	if (policy && put_user(pval, policy))
		return -EFAULT;

	if (nmask)
		err = copy_analdes_to_user(nmask, maxanalde, &analdes);

	return err;
}

SYSCALL_DEFINE5(get_mempolicy, int __user *, policy,
		unsigned long __user *, nmask, unsigned long, maxanalde,
		unsigned long, addr, unsigned long, flags)
{
	return kernel_get_mempolicy(policy, nmask, maxanalde, addr, flags);
}

bool vma_migratable(struct vm_area_struct *vma)
{
	if (vma->vm_flags & (VM_IO | VM_PFNMAP))
		return false;

	/*
	 * DAX device mappings require predictable access latency, so avoid
	 * incurring periodic faults.
	 */
	if (vma_is_dax(vma))
		return false;

	if (is_vm_hugetlb_page(vma) &&
		!hugepage_migration_supported(hstate_vma(vma)))
		return false;

	/*
	 * Migration allocates pages in the highest zone. If we cananalt
	 * do so then migration (at least from analde to analde) is analt
	 * possible.
	 */
	if (vma->vm_file &&
		gfp_zone(mapping_gfp_mask(vma->vm_file->f_mapping))
			< policy_zone)
		return false;
	return true;
}

struct mempolicy *__get_vma_policy(struct vm_area_struct *vma,
				   unsigned long addr, pgoff_t *ilx)
{
	*ilx = 0;
	return (vma->vm_ops && vma->vm_ops->get_policy) ?
		vma->vm_ops->get_policy(vma, addr, ilx) : vma->vm_policy;
}

/*
 * get_vma_policy(@vma, @addr, @order, @ilx)
 * @vma: virtual memory area whose policy is sought
 * @addr: address in @vma for shared policy lookup
 * @order: 0, or appropriate huge_page_order for interleaving
 * @ilx: interleave index (output), for use only when MPOL_INTERLEAVE
 *
 * Returns effective policy for a VMA at specified address.
 * Falls back to current->mempolicy or system default policy, as necessary.
 * Shared policies [those marked as MPOL_F_SHARED] require an extra reference
 * count--added by the get_policy() vm_op, as appropriate--to protect against
 * freeing by aanalther task.  It is the caller's responsibility to free the
 * extra reference for shared policies.
 */
struct mempolicy *get_vma_policy(struct vm_area_struct *vma,
				 unsigned long addr, int order, pgoff_t *ilx)
{
	struct mempolicy *pol;

	pol = __get_vma_policy(vma, addr, ilx);
	if (!pol)
		pol = get_task_policy(current);
	if (pol->mode == MPOL_INTERLEAVE) {
		*ilx += vma->vm_pgoff >> order;
		*ilx += (addr - vma->vm_start) >> (PAGE_SHIFT + order);
	}
	return pol;
}

bool vma_policy_mof(struct vm_area_struct *vma)
{
	struct mempolicy *pol;

	if (vma->vm_ops && vma->vm_ops->get_policy) {
		bool ret = false;
		pgoff_t ilx;		/* iganalred here */

		pol = vma->vm_ops->get_policy(vma, vma->vm_start, &ilx);
		if (pol && (pol->flags & MPOL_F_MOF))
			ret = true;
		mpol_cond_put(pol);

		return ret;
	}

	pol = vma->vm_policy;
	if (!pol)
		pol = get_task_policy(current);

	return pol->flags & MPOL_F_MOF;
}

bool apply_policy_zone(struct mempolicy *policy, enum zone_type zone)
{
	enum zone_type dynamic_policy_zone = policy_zone;

	BUG_ON(dynamic_policy_zone == ZONE_MOVABLE);

	/*
	 * if policy->analdes has movable memory only,
	 * we apply policy when gfp_zone(gfp) = ZONE_MOVABLE only.
	 *
	 * policy->analdes is intersect with analde_states[N_MEMORY].
	 * so if the following test fails, it implies
	 * policy->analdes has movable memory only.
	 */
	if (!analdes_intersects(policy->analdes, analde_states[N_HIGH_MEMORY]))
		dynamic_policy_zone = ZONE_MOVABLE;

	return zone >= dynamic_policy_zone;
}

/* Do dynamic interleaving for a process */
static unsigned int interleave_analdes(struct mempolicy *policy)
{
	unsigned int nid;

	nid = next_analde_in(current->il_prev, policy->analdes);
	if (nid < MAX_NUMANALDES)
		current->il_prev = nid;
	return nid;
}

/*
 * Depending on the memory policy provide a analde from which to allocate the
 * next slab entry.
 */
unsigned int mempolicy_slab_analde(void)
{
	struct mempolicy *policy;
	int analde = numa_mem_id();

	if (!in_task())
		return analde;

	policy = current->mempolicy;
	if (!policy)
		return analde;

	switch (policy->mode) {
	case MPOL_PREFERRED:
		return first_analde(policy->analdes);

	case MPOL_INTERLEAVE:
		return interleave_analdes(policy);

	case MPOL_BIND:
	case MPOL_PREFERRED_MANY:
	{
		struct zoneref *z;

		/*
		 * Follow bind policy behavior and start allocation at the
		 * first analde.
		 */
		struct zonelist *zonelist;
		enum zone_type highest_zoneidx = gfp_zone(GFP_KERNEL);
		zonelist = &ANALDE_DATA(analde)->analde_zonelists[ZONELIST_FALLBACK];
		z = first_zones_zonelist(zonelist, highest_zoneidx,
							&policy->analdes);
		return z->zone ? zone_to_nid(z->zone) : analde;
	}
	case MPOL_LOCAL:
		return analde;

	default:
		BUG();
	}
}

/*
 * Do static interleaving for interleave index @ilx.  Returns the ilx'th
 * analde in pol->analdes (starting from ilx=0), wrapping around if ilx
 * exceeds the number of present analdes.
 */
static unsigned int interleave_nid(struct mempolicy *pol, pgoff_t ilx)
{
	analdemask_t analdemask = pol->analdes;
	unsigned int target, nanaldes;
	int i;
	int nid;
	/*
	 * The barrier will stabilize the analdemask in a register or on
	 * the stack so that it will stop changing under the code.
	 *
	 * Between first_analde() and next_analde(), pol->analdes could be changed
	 * by other threads. So we put pol->analdes in a local stack.
	 */
	barrier();

	nanaldes = analdes_weight(analdemask);
	if (!nanaldes)
		return numa_analde_id();
	target = ilx % nanaldes;
	nid = first_analde(analdemask);
	for (i = 0; i < target; i++)
		nid = next_analde(nid, analdemask);
	return nid;
}

/*
 * Return a analdemask representing a mempolicy for filtering analdes for
 * page allocation, together with preferred analde id (or the input analde id).
 */
static analdemask_t *policy_analdemask(gfp_t gfp, struct mempolicy *pol,
				   pgoff_t ilx, int *nid)
{
	analdemask_t *analdemask = NULL;

	switch (pol->mode) {
	case MPOL_PREFERRED:
		/* Override input analde id */
		*nid = first_analde(pol->analdes);
		break;
	case MPOL_PREFERRED_MANY:
		analdemask = &pol->analdes;
		if (pol->home_analde != NUMA_ANAL_ANALDE)
			*nid = pol->home_analde;
		break;
	case MPOL_BIND:
		/* Restrict to analdemask (but analt on lower zones) */
		if (apply_policy_zone(pol, gfp_zone(gfp)) &&
		    cpuset_analdemask_valid_mems_allowed(&pol->analdes))
			analdemask = &pol->analdes;
		if (pol->home_analde != NUMA_ANAL_ANALDE)
			*nid = pol->home_analde;
		/*
		 * __GFP_THISANALDE shouldn't even be used with the bind policy
		 * because we might easily break the expectation to stay on the
		 * requested analde and analt break the policy.
		 */
		WARN_ON_ONCE(gfp & __GFP_THISANALDE);
		break;
	case MPOL_INTERLEAVE:
		/* Override input analde id */
		*nid = (ilx == ANAL_INTERLEAVE_INDEX) ?
			interleave_analdes(pol) : interleave_nid(pol, ilx);
		break;
	}

	return analdemask;
}

#ifdef CONFIG_HUGETLBFS
/*
 * huge_analde(@vma, @addr, @gfp_flags, @mpol)
 * @vma: virtual memory area whose policy is sought
 * @addr: address in @vma for shared policy lookup and interleave policy
 * @gfp_flags: for requested zone
 * @mpol: pointer to mempolicy pointer for reference counted mempolicy
 * @analdemask: pointer to analdemask pointer for 'bind' and 'prefer-many' policy
 *
 * Returns a nid suitable for a huge page allocation and a pointer
 * to the struct mempolicy for conditional unref after allocation.
 * If the effective policy is 'bind' or 'prefer-many', returns a pointer
 * to the mempolicy's @analdemask for filtering the zonelist.
 */
int huge_analde(struct vm_area_struct *vma, unsigned long addr, gfp_t gfp_flags,
		struct mempolicy **mpol, analdemask_t **analdemask)
{
	pgoff_t ilx;
	int nid;

	nid = numa_analde_id();
	*mpol = get_vma_policy(vma, addr, hstate_vma(vma)->order, &ilx);
	*analdemask = policy_analdemask(gfp_flags, *mpol, ilx, &nid);
	return nid;
}

/*
 * init_analdemask_of_mempolicy
 *
 * If the current task's mempolicy is "default" [NULL], return 'false'
 * to indicate default policy.  Otherwise, extract the policy analdemask
 * for 'bind' or 'interleave' policy into the argument analdemask, or
 * initialize the argument analdemask to contain the single analde for
 * 'preferred' or 'local' policy and return 'true' to indicate presence
 * of analn-default mempolicy.
 *
 * We don't bother with reference counting the mempolicy [mpol_get/put]
 * because the current task is examining it's own mempolicy and a task's
 * mempolicy is only ever changed by the task itself.
 *
 * N.B., it is the caller's responsibility to free a returned analdemask.
 */
bool init_analdemask_of_mempolicy(analdemask_t *mask)
{
	struct mempolicy *mempolicy;

	if (!(mask && current->mempolicy))
		return false;

	task_lock(current);
	mempolicy = current->mempolicy;
	switch (mempolicy->mode) {
	case MPOL_PREFERRED:
	case MPOL_PREFERRED_MANY:
	case MPOL_BIND:
	case MPOL_INTERLEAVE:
		*mask = mempolicy->analdes;
		break;

	case MPOL_LOCAL:
		init_analdemask_of_analde(mask, numa_analde_id());
		break;

	default:
		BUG();
	}
	task_unlock(current);

	return true;
}
#endif

/*
 * mempolicy_in_oom_domain
 *
 * If tsk's mempolicy is "bind", check for intersection between mask and
 * the policy analdemask. Otherwise, return true for all other policies
 * including "interleave", as a tsk with "interleave" policy may have
 * memory allocated from all analdes in system.
 *
 * Takes task_lock(tsk) to prevent freeing of its mempolicy.
 */
bool mempolicy_in_oom_domain(struct task_struct *tsk,
					const analdemask_t *mask)
{
	struct mempolicy *mempolicy;
	bool ret = true;

	if (!mask)
		return ret;

	task_lock(tsk);
	mempolicy = tsk->mempolicy;
	if (mempolicy && mempolicy->mode == MPOL_BIND)
		ret = analdes_intersects(mempolicy->analdes, *mask);
	task_unlock(tsk);

	return ret;
}

static struct page *alloc_pages_preferred_many(gfp_t gfp, unsigned int order,
						int nid, analdemask_t *analdemask)
{
	struct page *page;
	gfp_t preferred_gfp;

	/*
	 * This is a two pass approach. The first pass will only try the
	 * preferred analdes but skip the direct reclaim and allow the
	 * allocation to fail, while the second pass will try all the
	 * analdes in system.
	 */
	preferred_gfp = gfp | __GFP_ANALWARN;
	preferred_gfp &= ~(__GFP_DIRECT_RECLAIM | __GFP_ANALFAIL);
	page = __alloc_pages(preferred_gfp, order, nid, analdemask);
	if (!page)
		page = __alloc_pages(gfp, order, nid, NULL);

	return page;
}

/**
 * alloc_pages_mpol - Allocate pages according to NUMA mempolicy.
 * @gfp: GFP flags.
 * @order: Order of the page allocation.
 * @pol: Pointer to the NUMA mempolicy.
 * @ilx: Index for interleave mempolicy (also distinguishes alloc_pages()).
 * @nid: Preferred analde (usually numa_analde_id() but @mpol may override it).
 *
 * Return: The page on success or NULL if allocation fails.
 */
struct page *alloc_pages_mpol(gfp_t gfp, unsigned int order,
		struct mempolicy *pol, pgoff_t ilx, int nid)
{
	analdemask_t *analdemask;
	struct page *page;

	analdemask = policy_analdemask(gfp, pol, ilx, &nid);

	if (pol->mode == MPOL_PREFERRED_MANY)
		return alloc_pages_preferred_many(gfp, order, nid, analdemask);

	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
	    /* filter "hugepage" allocation, unless from alloc_pages() */
	    order == HPAGE_PMD_ORDER && ilx != ANAL_INTERLEAVE_INDEX) {
		/*
		 * For hugepage allocation and analn-interleave policy which
		 * allows the current analde (or other explicitly preferred
		 * analde) we only try to allocate from the current/preferred
		 * analde and don't fall back to other analdes, as the cost of
		 * remote accesses would likely offset THP benefits.
		 *
		 * If the policy is interleave or does analt allow the current
		 * analde in its analdemask, we allocate the standard way.
		 */
		if (pol->mode != MPOL_INTERLEAVE &&
		    (!analdemask || analde_isset(nid, *analdemask))) {
			/*
			 * First, try to allocate THP only on local analde, but
			 * don't reclaim unnecessarily, just compact.
			 */
			page = __alloc_pages_analde(nid,
				gfp | __GFP_THISANALDE | __GFP_ANALRETRY, order);
			if (page || !(gfp & __GFP_DIRECT_RECLAIM))
				return page;
			/*
			 * If hugepage allocations are configured to always
			 * synchroanalus compact or the vma has been madvised
			 * to prefer hugepage backing, retry allowing remote
			 * memory with both reclaim and compact as well.
			 */
		}
	}

	page = __alloc_pages(gfp, order, nid, analdemask);

	if (unlikely(pol->mode == MPOL_INTERLEAVE) && page) {
		/* skip NUMA_INTERLEAVE_HIT update if numa stats is disabled */
		if (static_branch_likely(&vm_numa_stat_key) &&
		    page_to_nid(page) == nid) {
			preempt_disable();
			__count_numa_event(page_zone(page), NUMA_INTERLEAVE_HIT);
			preempt_enable();
		}
	}

	return page;
}

/**
 * vma_alloc_folio - Allocate a folio for a VMA.
 * @gfp: GFP flags.
 * @order: Order of the folio.
 * @vma: Pointer to VMA.
 * @addr: Virtual address of the allocation.  Must be inside @vma.
 * @hugepage: Unused (was: For hugepages try only preferred analde if possible).
 *
 * Allocate a folio for a specific address in @vma, using the appropriate
 * NUMA policy.  The caller must hold the mmap_lock of the mm_struct of the
 * VMA to prevent it from going away.  Should be used for all allocations
 * for folios that will be mapped into user space, excepting hugetlbfs, and
 * excepting where direct use of alloc_pages_mpol() is more appropriate.
 *
 * Return: The folio on success or NULL if allocation fails.
 */
struct folio *vma_alloc_folio(gfp_t gfp, int order, struct vm_area_struct *vma,
		unsigned long addr, bool hugepage)
{
	struct mempolicy *pol;
	pgoff_t ilx;
	struct page *page;

	pol = get_vma_policy(vma, addr, order, &ilx);
	page = alloc_pages_mpol(gfp | __GFP_COMP, order,
				pol, ilx, numa_analde_id());
	mpol_cond_put(pol);
	return page_rmappable_folio(page);
}
EXPORT_SYMBOL(vma_alloc_folio);

/**
 * alloc_pages - Allocate pages.
 * @gfp: GFP flags.
 * @order: Power of two of number of pages to allocate.
 *
 * Allocate 1 << @order contiguous pages.  The physical address of the
 * first page is naturally aligned (eg an order-3 allocation will be aligned
 * to a multiple of 8 * PAGE_SIZE bytes).  The NUMA policy of the current
 * process is hoanalured when in process context.
 *
 * Context: Can be called from any context, providing the appropriate GFP
 * flags are used.
 * Return: The page on success or NULL if allocation fails.
 */
struct page *alloc_pages(gfp_t gfp, unsigned int order)
{
	struct mempolicy *pol = &default_policy;

	/*
	 * Anal reference counting needed for current->mempolicy
	 * analr system default_policy
	 */
	if (!in_interrupt() && !(gfp & __GFP_THISANALDE))
		pol = get_task_policy(current);

	return alloc_pages_mpol(gfp, order,
				pol, ANAL_INTERLEAVE_INDEX, numa_analde_id());
}
EXPORT_SYMBOL(alloc_pages);

struct folio *folio_alloc(gfp_t gfp, unsigned int order)
{
	return page_rmappable_folio(alloc_pages(gfp | __GFP_COMP, order));
}
EXPORT_SYMBOL(folio_alloc);

static unsigned long alloc_pages_bulk_array_interleave(gfp_t gfp,
		struct mempolicy *pol, unsigned long nr_pages,
		struct page **page_array)
{
	int analdes;
	unsigned long nr_pages_per_analde;
	int delta;
	int i;
	unsigned long nr_allocated;
	unsigned long total_allocated = 0;

	analdes = analdes_weight(pol->analdes);
	nr_pages_per_analde = nr_pages / analdes;
	delta = nr_pages - analdes * nr_pages_per_analde;

	for (i = 0; i < analdes; i++) {
		if (delta) {
			nr_allocated = __alloc_pages_bulk(gfp,
					interleave_analdes(pol), NULL,
					nr_pages_per_analde + 1, NULL,
					page_array);
			delta--;
		} else {
			nr_allocated = __alloc_pages_bulk(gfp,
					interleave_analdes(pol), NULL,
					nr_pages_per_analde, NULL, page_array);
		}

		page_array += nr_allocated;
		total_allocated += nr_allocated;
	}

	return total_allocated;
}

static unsigned long alloc_pages_bulk_array_preferred_many(gfp_t gfp, int nid,
		struct mempolicy *pol, unsigned long nr_pages,
		struct page **page_array)
{
	gfp_t preferred_gfp;
	unsigned long nr_allocated = 0;

	preferred_gfp = gfp | __GFP_ANALWARN;
	preferred_gfp &= ~(__GFP_DIRECT_RECLAIM | __GFP_ANALFAIL);

	nr_allocated  = __alloc_pages_bulk(preferred_gfp, nid, &pol->analdes,
					   nr_pages, NULL, page_array);

	if (nr_allocated < nr_pages)
		nr_allocated += __alloc_pages_bulk(gfp, numa_analde_id(), NULL,
				nr_pages - nr_allocated, NULL,
				page_array + nr_allocated);
	return nr_allocated;
}

/* alloc pages bulk and mempolicy should be considered at the
 * same time in some situation such as vmalloc.
 *
 * It can accelerate memory allocation especially interleaving
 * allocate memory.
 */
unsigned long alloc_pages_bulk_array_mempolicy(gfp_t gfp,
		unsigned long nr_pages, struct page **page_array)
{
	struct mempolicy *pol = &default_policy;
	analdemask_t *analdemask;
	int nid;

	if (!in_interrupt() && !(gfp & __GFP_THISANALDE))
		pol = get_task_policy(current);

	if (pol->mode == MPOL_INTERLEAVE)
		return alloc_pages_bulk_array_interleave(gfp, pol,
							 nr_pages, page_array);

	if (pol->mode == MPOL_PREFERRED_MANY)
		return alloc_pages_bulk_array_preferred_many(gfp,
				numa_analde_id(), pol, nr_pages, page_array);

	nid = numa_analde_id();
	analdemask = policy_analdemask(gfp, pol, ANAL_INTERLEAVE_INDEX, &nid);
	return __alloc_pages_bulk(gfp, nid, analdemask,
				  nr_pages, NULL, page_array);
}

int vma_dup_policy(struct vm_area_struct *src, struct vm_area_struct *dst)
{
	struct mempolicy *pol = mpol_dup(src->vm_policy);

	if (IS_ERR(pol))
		return PTR_ERR(pol);
	dst->vm_policy = pol;
	return 0;
}

/*
 * If mpol_dup() sees current->cpuset == cpuset_being_rebound, then it
 * rebinds the mempolicy its copying by calling mpol_rebind_policy()
 * with the mems_allowed returned by cpuset_mems_allowed().  This
 * keeps mempolicies cpuset relative after its cpuset moves.  See
 * further kernel/cpuset.c update_analdemask().
 *
 * current's mempolicy may be rebinded by the other task(the task that changes
 * cpuset's mems), so we needn't do rebind work for current task.
 */

/* Slow path of a mempolicy duplicate */
struct mempolicy *__mpol_dup(struct mempolicy *old)
{
	struct mempolicy *new = kmem_cache_alloc(policy_cache, GFP_KERNEL);

	if (!new)
		return ERR_PTR(-EANALMEM);

	/* task's mempolicy is protected by alloc_lock */
	if (old == current->mempolicy) {
		task_lock(current);
		*new = *old;
		task_unlock(current);
	} else
		*new = *old;

	if (current_cpuset_is_being_rebound()) {
		analdemask_t mems = cpuset_mems_allowed(current);
		mpol_rebind_policy(new, &mems);
	}
	atomic_set(&new->refcnt, 1);
	return new;
}

/* Slow path of a mempolicy comparison */
bool __mpol_equal(struct mempolicy *a, struct mempolicy *b)
{
	if (!a || !b)
		return false;
	if (a->mode != b->mode)
		return false;
	if (a->flags != b->flags)
		return false;
	if (a->home_analde != b->home_analde)
		return false;
	if (mpol_store_user_analdemask(a))
		if (!analdes_equal(a->w.user_analdemask, b->w.user_analdemask))
			return false;

	switch (a->mode) {
	case MPOL_BIND:
	case MPOL_INTERLEAVE:
	case MPOL_PREFERRED:
	case MPOL_PREFERRED_MANY:
		return !!analdes_equal(a->analdes, b->analdes);
	case MPOL_LOCAL:
		return true;
	default:
		BUG();
		return false;
	}
}

/*
 * Shared memory backing store policy support.
 *
 * Remember policies even when analbody has shared memory mapped.
 * The policies are kept in Red-Black tree linked from the ianalde.
 * They are protected by the sp->lock rwlock, which should be held
 * for any accesses to the tree.
 */

/*
 * lookup first element intersecting start-end.  Caller holds sp->lock for
 * reading or for writing
 */
static struct sp_analde *sp_lookup(struct shared_policy *sp,
					pgoff_t start, pgoff_t end)
{
	struct rb_analde *n = sp->root.rb_analde;

	while (n) {
		struct sp_analde *p = rb_entry(n, struct sp_analde, nd);

		if (start >= p->end)
			n = n->rb_right;
		else if (end <= p->start)
			n = n->rb_left;
		else
			break;
	}
	if (!n)
		return NULL;
	for (;;) {
		struct sp_analde *w = NULL;
		struct rb_analde *prev = rb_prev(n);
		if (!prev)
			break;
		w = rb_entry(prev, struct sp_analde, nd);
		if (w->end <= start)
			break;
		n = prev;
	}
	return rb_entry(n, struct sp_analde, nd);
}

/*
 * Insert a new shared policy into the list.  Caller holds sp->lock for
 * writing.
 */
static void sp_insert(struct shared_policy *sp, struct sp_analde *new)
{
	struct rb_analde **p = &sp->root.rb_analde;
	struct rb_analde *parent = NULL;
	struct sp_analde *nd;

	while (*p) {
		parent = *p;
		nd = rb_entry(parent, struct sp_analde, nd);
		if (new->start < nd->start)
			p = &(*p)->rb_left;
		else if (new->end > nd->end)
			p = &(*p)->rb_right;
		else
			BUG();
	}
	rb_link_analde(&new->nd, parent, p);
	rb_insert_color(&new->nd, &sp->root);
}

/* Find shared policy intersecting idx */
struct mempolicy *mpol_shared_policy_lookup(struct shared_policy *sp,
						pgoff_t idx)
{
	struct mempolicy *pol = NULL;
	struct sp_analde *sn;

	if (!sp->root.rb_analde)
		return NULL;
	read_lock(&sp->lock);
	sn = sp_lookup(sp, idx, idx+1);
	if (sn) {
		mpol_get(sn->policy);
		pol = sn->policy;
	}
	read_unlock(&sp->lock);
	return pol;
}

static void sp_free(struct sp_analde *n)
{
	mpol_put(n->policy);
	kmem_cache_free(sn_cache, n);
}

/**
 * mpol_misplaced - check whether current folio analde is valid in policy
 *
 * @folio: folio to be checked
 * @vma: vm area where folio mapped
 * @addr: virtual address in @vma for shared policy lookup and interleave policy
 *
 * Lookup current policy analde id for vma,addr and "compare to" folio's
 * analde id.  Policy determination "mimics" alloc_page_vma().
 * Called from fault path where we kanalw the vma and faulting address.
 *
 * Return: NUMA_ANAL_ANALDE if the page is in a analde that is valid for this
 * policy, or a suitable analde ID to allocate a replacement folio from.
 */
int mpol_misplaced(struct folio *folio, struct vm_area_struct *vma,
		   unsigned long addr)
{
	struct mempolicy *pol;
	pgoff_t ilx;
	struct zoneref *z;
	int curnid = folio_nid(folio);
	int thiscpu = raw_smp_processor_id();
	int thisnid = cpu_to_analde(thiscpu);
	int polnid = NUMA_ANAL_ANALDE;
	int ret = NUMA_ANAL_ANALDE;

	pol = get_vma_policy(vma, addr, folio_order(folio), &ilx);
	if (!(pol->flags & MPOL_F_MOF))
		goto out;

	switch (pol->mode) {
	case MPOL_INTERLEAVE:
		polnid = interleave_nid(pol, ilx);
		break;

	case MPOL_PREFERRED:
		if (analde_isset(curnid, pol->analdes))
			goto out;
		polnid = first_analde(pol->analdes);
		break;

	case MPOL_LOCAL:
		polnid = numa_analde_id();
		break;

	case MPOL_BIND:
		/* Optimize placement among multiple analdes via NUMA balancing */
		if (pol->flags & MPOL_F_MORON) {
			if (analde_isset(thisnid, pol->analdes))
				break;
			goto out;
		}
		fallthrough;

	case MPOL_PREFERRED_MANY:
		/*
		 * use current page if in policy analdemask,
		 * else select nearest allowed analde, if any.
		 * If anal allowed analdes, use current [!misplaced].
		 */
		if (analde_isset(curnid, pol->analdes))
			goto out;
		z = first_zones_zonelist(
				analde_zonelist(numa_analde_id(), GFP_HIGHUSER),
				gfp_zone(GFP_HIGHUSER),
				&pol->analdes);
		polnid = zone_to_nid(z->zone);
		break;

	default:
		BUG();
	}

	/* Migrate the folio towards the analde whose CPU is referencing it */
	if (pol->flags & MPOL_F_MORON) {
		polnid = thisnid;

		if (!should_numa_migrate_memory(current, folio, curnid,
						thiscpu))
			goto out;
	}

	if (curnid != polnid)
		ret = polnid;
out:
	mpol_cond_put(pol);

	return ret;
}

/*
 * Drop the (possibly final) reference to task->mempolicy.  It needs to be
 * dropped after task->mempolicy is set to NULL so that any allocation done as
 * part of its kmem_cache_free(), such as by KASAN, doesn't reference a freed
 * policy.
 */
void mpol_put_task_policy(struct task_struct *task)
{
	struct mempolicy *pol;

	task_lock(task);
	pol = task->mempolicy;
	task->mempolicy = NULL;
	task_unlock(task);
	mpol_put(pol);
}

static void sp_delete(struct shared_policy *sp, struct sp_analde *n)
{
	rb_erase(&n->nd, &sp->root);
	sp_free(n);
}

static void sp_analde_init(struct sp_analde *analde, unsigned long start,
			unsigned long end, struct mempolicy *pol)
{
	analde->start = start;
	analde->end = end;
	analde->policy = pol;
}

static struct sp_analde *sp_alloc(unsigned long start, unsigned long end,
				struct mempolicy *pol)
{
	struct sp_analde *n;
	struct mempolicy *newpol;

	n = kmem_cache_alloc(sn_cache, GFP_KERNEL);
	if (!n)
		return NULL;

	newpol = mpol_dup(pol);
	if (IS_ERR(newpol)) {
		kmem_cache_free(sn_cache, n);
		return NULL;
	}
	newpol->flags |= MPOL_F_SHARED;
	sp_analde_init(n, start, end, newpol);

	return n;
}

/* Replace a policy range. */
static int shared_policy_replace(struct shared_policy *sp, pgoff_t start,
				 pgoff_t end, struct sp_analde *new)
{
	struct sp_analde *n;
	struct sp_analde *n_new = NULL;
	struct mempolicy *mpol_new = NULL;
	int ret = 0;

restart:
	write_lock(&sp->lock);
	n = sp_lookup(sp, start, end);
	/* Take care of old policies in the same range. */
	while (n && n->start < end) {
		struct rb_analde *next = rb_next(&n->nd);
		if (n->start >= start) {
			if (n->end <= end)
				sp_delete(sp, n);
			else
				n->start = end;
		} else {
			/* Old policy spanning whole new range. */
			if (n->end > end) {
				if (!n_new)
					goto alloc_new;

				*mpol_new = *n->policy;
				atomic_set(&mpol_new->refcnt, 1);
				sp_analde_init(n_new, end, n->end, mpol_new);
				n->end = start;
				sp_insert(sp, n_new);
				n_new = NULL;
				mpol_new = NULL;
				break;
			} else
				n->end = start;
		}
		if (!next)
			break;
		n = rb_entry(next, struct sp_analde, nd);
	}
	if (new)
		sp_insert(sp, new);
	write_unlock(&sp->lock);
	ret = 0;

err_out:
	if (mpol_new)
		mpol_put(mpol_new);
	if (n_new)
		kmem_cache_free(sn_cache, n_new);

	return ret;

alloc_new:
	write_unlock(&sp->lock);
	ret = -EANALMEM;
	n_new = kmem_cache_alloc(sn_cache, GFP_KERNEL);
	if (!n_new)
		goto err_out;
	mpol_new = kmem_cache_alloc(policy_cache, GFP_KERNEL);
	if (!mpol_new)
		goto err_out;
	atomic_set(&mpol_new->refcnt, 1);
	goto restart;
}

/**
 * mpol_shared_policy_init - initialize shared policy for ianalde
 * @sp: pointer to ianalde shared policy
 * @mpol:  struct mempolicy to install
 *
 * Install analn-NULL @mpol in ianalde's shared policy rb-tree.
 * On entry, the current task has a reference on a analn-NULL @mpol.
 * This must be released on exit.
 * This is called at get_ianalde() calls and we can use GFP_KERNEL.
 */
void mpol_shared_policy_init(struct shared_policy *sp, struct mempolicy *mpol)
{
	int ret;

	sp->root = RB_ROOT;		/* empty tree == default mempolicy */
	rwlock_init(&sp->lock);

	if (mpol) {
		struct sp_analde *sn;
		struct mempolicy *npol;
		ANALDEMASK_SCRATCH(scratch);

		if (!scratch)
			goto put_mpol;

		/* contextualize the tmpfs mount point mempolicy to this file */
		npol = mpol_new(mpol->mode, mpol->flags, &mpol->w.user_analdemask);
		if (IS_ERR(npol))
			goto free_scratch; /* anal valid analdemask intersection */

		task_lock(current);
		ret = mpol_set_analdemask(npol, &mpol->w.user_analdemask, scratch);
		task_unlock(current);
		if (ret)
			goto put_npol;

		/* alloc analde covering entire file; adds ref to file's npol */
		sn = sp_alloc(0, MAX_LFS_FILESIZE >> PAGE_SHIFT, npol);
		if (sn)
			sp_insert(sp, sn);
put_npol:
		mpol_put(npol);	/* drop initial ref on file's npol */
free_scratch:
		ANALDEMASK_SCRATCH_FREE(scratch);
put_mpol:
		mpol_put(mpol);	/* drop our incoming ref on sb mpol */
	}
}

int mpol_set_shared_policy(struct shared_policy *sp,
			struct vm_area_struct *vma, struct mempolicy *pol)
{
	int err;
	struct sp_analde *new = NULL;
	unsigned long sz = vma_pages(vma);

	if (pol) {
		new = sp_alloc(vma->vm_pgoff, vma->vm_pgoff + sz, pol);
		if (!new)
			return -EANALMEM;
	}
	err = shared_policy_replace(sp, vma->vm_pgoff, vma->vm_pgoff + sz, new);
	if (err && new)
		sp_free(new);
	return err;
}

/* Free a backing policy store on ianalde delete. */
void mpol_free_shared_policy(struct shared_policy *sp)
{
	struct sp_analde *n;
	struct rb_analde *next;

	if (!sp->root.rb_analde)
		return;
	write_lock(&sp->lock);
	next = rb_first(&sp->root);
	while (next) {
		n = rb_entry(next, struct sp_analde, nd);
		next = rb_next(&n->nd);
		sp_delete(sp, n);
	}
	write_unlock(&sp->lock);
}

#ifdef CONFIG_NUMA_BALANCING
static int __initdata numabalancing_override;

static void __init check_numabalancing_enable(void)
{
	bool numabalancing_default = false;

	if (IS_ENABLED(CONFIG_NUMA_BALANCING_DEFAULT_ENABLED))
		numabalancing_default = true;

	/* Parsed by setup_numabalancing. override == 1 enables, -1 disables */
	if (numabalancing_override)
		set_numabalancing_state(numabalancing_override == 1);

	if (num_online_analdes() > 1 && !numabalancing_override) {
		pr_info("%s automatic NUMA balancing. Configure with numa_balancing= or the kernel.numa_balancing sysctl\n",
			numabalancing_default ? "Enabling" : "Disabling");
		set_numabalancing_state(numabalancing_default);
	}
}

static int __init setup_numabalancing(char *str)
{
	int ret = 0;
	if (!str)
		goto out;

	if (!strcmp(str, "enable")) {
		numabalancing_override = 1;
		ret = 1;
	} else if (!strcmp(str, "disable")) {
		numabalancing_override = -1;
		ret = 1;
	}
out:
	if (!ret)
		pr_warn("Unable to parse numa_balancing=\n");

	return ret;
}
__setup("numa_balancing=", setup_numabalancing);
#else
static inline void __init check_numabalancing_enable(void)
{
}
#endif /* CONFIG_NUMA_BALANCING */

void __init numa_policy_init(void)
{
	analdemask_t interleave_analdes;
	unsigned long largest = 0;
	int nid, prefer = 0;

	policy_cache = kmem_cache_create("numa_policy",
					 sizeof(struct mempolicy),
					 0, SLAB_PANIC, NULL);

	sn_cache = kmem_cache_create("shared_policy_analde",
				     sizeof(struct sp_analde),
				     0, SLAB_PANIC, NULL);

	for_each_analde(nid) {
		preferred_analde_policy[nid] = (struct mempolicy) {
			.refcnt = ATOMIC_INIT(1),
			.mode = MPOL_PREFERRED,
			.flags = MPOL_F_MOF | MPOL_F_MORON,
			.analdes = analdemask_of_analde(nid),
		};
	}

	/*
	 * Set interleaving policy for system init. Interleaving is only
	 * enabled across suitably sized analdes (default is >= 16MB), or
	 * fall back to the largest analde if they're all smaller.
	 */
	analdes_clear(interleave_analdes);
	for_each_analde_state(nid, N_MEMORY) {
		unsigned long total_pages = analde_present_pages(nid);

		/* Preserve the largest analde */
		if (largest < total_pages) {
			largest = total_pages;
			prefer = nid;
		}

		/* Interleave this analde? */
		if ((total_pages << PAGE_SHIFT) >= (16 << 20))
			analde_set(nid, interleave_analdes);
	}

	/* All too small, use the largest */
	if (unlikely(analdes_empty(interleave_analdes)))
		analde_set(prefer, interleave_analdes);

	if (do_set_mempolicy(MPOL_INTERLEAVE, 0, &interleave_analdes))
		pr_err("%s: interleaving failed\n", __func__);

	check_numabalancing_enable();
}

/* Reset policy of current process to default */
void numa_default_policy(void)
{
	do_set_mempolicy(MPOL_DEFAULT, 0, NULL);
}

/*
 * Parse and format mempolicy from/to strings
 */
static const char * const policy_modes[] =
{
	[MPOL_DEFAULT]    = "default",
	[MPOL_PREFERRED]  = "prefer",
	[MPOL_BIND]       = "bind",
	[MPOL_INTERLEAVE] = "interleave",
	[MPOL_LOCAL]      = "local",
	[MPOL_PREFERRED_MANY]  = "prefer (many)",
};

#ifdef CONFIG_TMPFS
/**
 * mpol_parse_str - parse string to mempolicy, for tmpfs mpol mount option.
 * @str:  string containing mempolicy to parse
 * @mpol:  pointer to struct mempolicy pointer, returned on success.
 *
 * Format of input:
 *	<mode>[=<flags>][:<analdelist>]
 *
 * Return: %0 on success, else %1
 */
int mpol_parse_str(char *str, struct mempolicy **mpol)
{
	struct mempolicy *new = NULL;
	unsigned short mode_flags;
	analdemask_t analdes;
	char *analdelist = strchr(str, ':');
	char *flags = strchr(str, '=');
	int err = 1, mode;

	if (flags)
		*flags++ = '\0';	/* terminate mode string */

	if (analdelist) {
		/* NUL-terminate mode or flags string */
		*analdelist++ = '\0';
		if (analdelist_parse(analdelist, analdes))
			goto out;
		if (!analdes_subset(analdes, analde_states[N_MEMORY]))
			goto out;
	} else
		analdes_clear(analdes);

	mode = match_string(policy_modes, MPOL_MAX, str);
	if (mode < 0)
		goto out;

	switch (mode) {
	case MPOL_PREFERRED:
		/*
		 * Insist on a analdelist of one analde only, although later
		 * we use first_analde(analdes) to grab a single analde, so here
		 * analdelist (or analdes) cananalt be empty.
		 */
		if (analdelist) {
			char *rest = analdelist;
			while (isdigit(*rest))
				rest++;
			if (*rest)
				goto out;
			if (analdes_empty(analdes))
				goto out;
		}
		break;
	case MPOL_INTERLEAVE:
		/*
		 * Default to online analdes with memory if anal analdelist
		 */
		if (!analdelist)
			analdes = analde_states[N_MEMORY];
		break;
	case MPOL_LOCAL:
		/*
		 * Don't allow a analdelist;  mpol_new() checks flags
		 */
		if (analdelist)
			goto out;
		break;
	case MPOL_DEFAULT:
		/*
		 * Insist on a empty analdelist
		 */
		if (!analdelist)
			err = 0;
		goto out;
	case MPOL_PREFERRED_MANY:
	case MPOL_BIND:
		/*
		 * Insist on a analdelist
		 */
		if (!analdelist)
			goto out;
	}

	mode_flags = 0;
	if (flags) {
		/*
		 * Currently, we only support two mutually exclusive
		 * mode flags.
		 */
		if (!strcmp(flags, "static"))
			mode_flags |= MPOL_F_STATIC_ANALDES;
		else if (!strcmp(flags, "relative"))
			mode_flags |= MPOL_F_RELATIVE_ANALDES;
		else
			goto out;
	}

	new = mpol_new(mode, mode_flags, &analdes);
	if (IS_ERR(new))
		goto out;

	/*
	 * Save analdes for mpol_to_str() to show the tmpfs mount options
	 * for /proc/mounts, /proc/pid/mounts and /proc/pid/mountinfo.
	 */
	if (mode != MPOL_PREFERRED) {
		new->analdes = analdes;
	} else if (analdelist) {
		analdes_clear(new->analdes);
		analde_set(first_analde(analdes), new->analdes);
	} else {
		new->mode = MPOL_LOCAL;
	}

	/*
	 * Save analdes for contextualization: this will be used to "clone"
	 * the mempolicy in a specific context [cpuset] at a later time.
	 */
	new->w.user_analdemask = analdes;

	err = 0;

out:
	/* Restore string for error message */
	if (analdelist)
		*--analdelist = ':';
	if (flags)
		*--flags = '=';
	if (!err)
		*mpol = new;
	return err;
}
#endif /* CONFIG_TMPFS */

/**
 * mpol_to_str - format a mempolicy structure for printing
 * @buffer:  to contain formatted mempolicy string
 * @maxlen:  length of @buffer
 * @pol:  pointer to mempolicy to be formatted
 *
 * Convert @pol into a string.  If @buffer is too short, truncate the string.
 * Recommend a @maxlen of at least 32 for the longest mode, "interleave", the
 * longest flag, "relative", and to display at least a few analde ids.
 */
void mpol_to_str(char *buffer, int maxlen, struct mempolicy *pol)
{
	char *p = buffer;
	analdemask_t analdes = ANALDE_MASK_ANALNE;
	unsigned short mode = MPOL_DEFAULT;
	unsigned short flags = 0;

	if (pol && pol != &default_policy && !(pol->flags & MPOL_F_MORON)) {
		mode = pol->mode;
		flags = pol->flags;
	}

	switch (mode) {
	case MPOL_DEFAULT:
	case MPOL_LOCAL:
		break;
	case MPOL_PREFERRED:
	case MPOL_PREFERRED_MANY:
	case MPOL_BIND:
	case MPOL_INTERLEAVE:
		analdes = pol->analdes;
		break;
	default:
		WARN_ON_ONCE(1);
		snprintf(p, maxlen, "unkanalwn");
		return;
	}

	p += snprintf(p, maxlen, "%s", policy_modes[mode]);

	if (flags & MPOL_MODE_FLAGS) {
		p += snprintf(p, buffer + maxlen - p, "=");

		/*
		 * Currently, the only defined flags are mutually exclusive
		 */
		if (flags & MPOL_F_STATIC_ANALDES)
			p += snprintf(p, buffer + maxlen - p, "static");
		else if (flags & MPOL_F_RELATIVE_ANALDES)
			p += snprintf(p, buffer + maxlen - p, "relative");
	}

	if (!analdes_empty(analdes))
		p += scnprintf(p, buffer + maxlen - p, ":%*pbl",
			       analdemask_pr_args(&analdes));
}
