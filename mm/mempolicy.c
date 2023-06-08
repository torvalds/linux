// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simple NUMA memory policy for the Linux kernel.
 *
 * Copyright 2003,2004 Andi Kleen, SuSE Labs.
 * (C) Copyright 2005 Christoph Lameter, Silicon Graphics, Inc.
 *
 * NUMA policy allows the user to give hints in which node(s) memory should
 * be allocated.
 *
 * Support four policies per VMA and per process:
 *
 * The VMA policy has priority over the process policy for a page fault.
 *
 * interleave     Allocate memory interleaved over a set of nodes,
 *                with normal fallback if it fails.
 *                For VMA based allocations this interleaves based on the
 *                offset into the backing object or offset into the mapping
 *                for anonymous memory. For process policy an process counter
 *                is used.
 *
 * bind           Only allocate memory on a specific set of nodes,
 *                no fallback.
 *                FIXME: memory is allocated starting with the first node
 *                to the last. It would be better if bind would truly restrict
 *                the allocation to memory nodes instead
 *
 * preferred       Try a specific node first before normal fallback.
 *                As a special case NUMA_NO_NODE here means do the allocation
 *                on the local CPU. This is normally identical to default,
 *                but useful to set in a VMA when you have a non default
 *                process policy.
 *
 * preferred many Try a set of nodes first before normal fallback. This is
 *                similar to preferred without the special case.
 *
 * default        Allocate on the local node first, or when on a VMA
 *                use the process policy. This is what Linux always did
 *		  in a NUMA aware kernel and still does by, ahem, default.
 *
 * The process policy is applied for most non interrupt memory allocations
 * in that process' context. Interrupts ignore the policies and always
 * try to allocate on the local CPU. The VMA policy is only applied for memory
 * allocations for a VMA in the VM.
 *
 * Currently there are a few corner cases in swapping where the policy
 * is not applied, but the majority should be handled. When process policy
 * is used it is not remembered over swap outs/swap ins.
 *
 * Only the highest zone in the zone hierarchy gets policied. Allocations
 * requesting a lower zone just use default policy. This implies that
 * on systems with highmem kernel lowmem allocation don't get policied.
 * Same with GFP_DMA allocations.
 *
 * For shmfs/tmpfs/hugetlbfs shared memory the policy is shared between
 * all users and remembered even when nobody has memory mapped.
 */

/* Notebook:
   fix mmap readahead to honour policy and enable policy for any page cache
   object
   statistics for bigpages
   global policy for page cache? currently it uses process policy. Requires
   first item above.
   handle mremap for shared memory (currently ignored for the policy)
   grows down?
   make bind policy root only? It can trigger oom much faster and the
   kernel is not always grateful with that.
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
#include <linux/nodemask.h>
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
#include <linux/mmu_notifier.h>
#include <linux/printk.h>
#include <linux/swapops.h>

#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <linux/uaccess.h>

#include "internal.h"

/* Internal flags */
#define MPOL_MF_DISCONTIG_OK (MPOL_MF_INTERNAL << 0)	/* Skip checks for continuous vmas */
#define MPOL_MF_INVERT (MPOL_MF_INTERNAL << 1)		/* Invert check for nodemask */

static struct kmem_cache *policy_cache;
static struct kmem_cache *sn_cache;

/* Highest zone. An specific allocation for a zone below that is not
   policied. */
enum zone_type policy_zone = 0;

/*
 * run-time system-wide default policy => local allocation
 */
static struct mempolicy default_policy = {
	.refcnt = ATOMIC_INIT(1), /* never free it */
	.mode = MPOL_LOCAL,
};

static struct mempolicy preferred_node_policy[MAX_NUMNODES];

/**
 * numa_map_to_online_node - Find closest online node
 * @node: Node id to start the search
 *
 * Lookup the next closest node by distance if @nid is not online.
 *
 * Return: this @node if it is online, otherwise the closest node by distance
 */
int numa_map_to_online_node(int node)
{
	int min_dist = INT_MAX, dist, n, min_node;

	if (node == NUMA_NO_NODE || node_online(node))
		return node;

	min_node = node;
	for_each_online_node(n) {
		dist = node_distance(node, n);
		if (dist < min_dist) {
			min_dist = dist;
			min_node = n;
		}
	}

	return min_node;
}
EXPORT_SYMBOL_GPL(numa_map_to_online_node);

struct mempolicy *get_task_policy(struct task_struct *p)
{
	struct mempolicy *pol = p->mempolicy;
	int node;

	if (pol)
		return pol;

	node = numa_node_id();
	if (node != NUMA_NO_NODE) {
		pol = &preferred_node_policy[node];
		/* preferred_node_policy is not initialised early in boot */
		if (pol->mode)
			return pol;
	}

	return &default_policy;
}

static const struct mempolicy_operations {
	int (*create)(struct mempolicy *pol, const nodemask_t *nodes);
	void (*rebind)(struct mempolicy *pol, const nodemask_t *nodes);
} mpol_ops[MPOL_MAX];

static inline int mpol_store_user_nodemask(const struct mempolicy *pol)
{
	return pol->flags & MPOL_MODE_FLAGS;
}

static void mpol_relative_nodemask(nodemask_t *ret, const nodemask_t *orig,
				   const nodemask_t *rel)
{
	nodemask_t tmp;
	nodes_fold(tmp, *orig, nodes_weight(*rel));
	nodes_onto(*ret, tmp, *rel);
}

static int mpol_new_nodemask(struct mempolicy *pol, const nodemask_t *nodes)
{
	if (nodes_empty(*nodes))
		return -EINVAL;
	pol->nodes = *nodes;
	return 0;
}

static int mpol_new_preferred(struct mempolicy *pol, const nodemask_t *nodes)
{
	if (nodes_empty(*nodes))
		return -EINVAL;

	nodes_clear(pol->nodes);
	node_set(first_node(*nodes), pol->nodes);
	return 0;
}

/*
 * mpol_set_nodemask is called after mpol_new() to set up the nodemask, if
 * any, for the new policy.  mpol_new() has already validated the nodes
 * parameter with respect to the policy mode and flags.
 *
 * Must be called holding task's alloc_lock to protect task's mems_allowed
 * and mempolicy.  May also be called holding the mmap_lock for write.
 */
static int mpol_set_nodemask(struct mempolicy *pol,
		     const nodemask_t *nodes, struct nodemask_scratch *nsc)
{
	int ret;

	/*
	 * Default (pol==NULL) resp. local memory policies are not a
	 * subject of any remapping. They also do not need any special
	 * constructor.
	 */
	if (!pol || pol->mode == MPOL_LOCAL)
		return 0;

	/* Check N_MEMORY */
	nodes_and(nsc->mask1,
		  cpuset_current_mems_allowed, node_states[N_MEMORY]);

	VM_BUG_ON(!nodes);

	if (pol->flags & MPOL_F_RELATIVE_NODES)
		mpol_relative_nodemask(&nsc->mask2, nodes, &nsc->mask1);
	else
		nodes_and(nsc->mask2, *nodes, nsc->mask1);

	if (mpol_store_user_nodemask(pol))
		pol->w.user_nodemask = *nodes;
	else
		pol->w.cpuset_mems_allowed = cpuset_current_mems_allowed;

	ret = mpol_ops[pol->mode].create(pol, &nsc->mask2);
	return ret;
}

/*
 * This function just creates a new policy, does some check and simple
 * initialization. You must invoke mpol_set_nodemask() to set nodes.
 */
static struct mempolicy *mpol_new(unsigned short mode, unsigned short flags,
				  nodemask_t *nodes)
{
	struct mempolicy *policy;

	pr_debug("setting mode %d flags %d nodes[0] %lx\n",
		 mode, flags, nodes ? nodes_addr(*nodes)[0] : NUMA_NO_NODE);

	if (mode == MPOL_DEFAULT) {
		if (nodes && !nodes_empty(*nodes))
			return ERR_PTR(-EINVAL);
		return NULL;
	}
	VM_BUG_ON(!nodes);

	/*
	 * MPOL_PREFERRED cannot be used with MPOL_F_STATIC_NODES or
	 * MPOL_F_RELATIVE_NODES if the nodemask is empty (local allocation).
	 * All other modes require a valid pointer to a non-empty nodemask.
	 */
	if (mode == MPOL_PREFERRED) {
		if (nodes_empty(*nodes)) {
			if (((flags & MPOL_F_STATIC_NODES) ||
			     (flags & MPOL_F_RELATIVE_NODES)))
				return ERR_PTR(-EINVAL);

			mode = MPOL_LOCAL;
		}
	} else if (mode == MPOL_LOCAL) {
		if (!nodes_empty(*nodes) ||
		    (flags & MPOL_F_STATIC_NODES) ||
		    (flags & MPOL_F_RELATIVE_NODES))
			return ERR_PTR(-EINVAL);
	} else if (nodes_empty(*nodes))
		return ERR_PTR(-EINVAL);
	policy = kmem_cache_alloc(policy_cache, GFP_KERNEL);
	if (!policy)
		return ERR_PTR(-ENOMEM);
	atomic_set(&policy->refcnt, 1);
	policy->mode = mode;
	policy->flags = flags;
	policy->home_node = NUMA_NO_NODE;

	return policy;
}

/* Slow path of a mpol destructor. */
void __mpol_put(struct mempolicy *p)
{
	if (!atomic_dec_and_test(&p->refcnt))
		return;
	kmem_cache_free(policy_cache, p);
}

static void mpol_rebind_default(struct mempolicy *pol, const nodemask_t *nodes)
{
}

static void mpol_rebind_nodemask(struct mempolicy *pol, const nodemask_t *nodes)
{
	nodemask_t tmp;

	if (pol->flags & MPOL_F_STATIC_NODES)
		nodes_and(tmp, pol->w.user_nodemask, *nodes);
	else if (pol->flags & MPOL_F_RELATIVE_NODES)
		mpol_relative_nodemask(&tmp, &pol->w.user_nodemask, nodes);
	else {
		nodes_remap(tmp, pol->nodes, pol->w.cpuset_mems_allowed,
								*nodes);
		pol->w.cpuset_mems_allowed = *nodes;
	}

	if (nodes_empty(tmp))
		tmp = *nodes;

	pol->nodes = tmp;
}

static void mpol_rebind_preferred(struct mempolicy *pol,
						const nodemask_t *nodes)
{
	pol->w.cpuset_mems_allowed = *nodes;
}

/*
 * mpol_rebind_policy - Migrate a policy to a different set of nodes
 *
 * Per-vma policies are protected by mmap_lock. Allocations using per-task
 * policies are protected by task->mems_allowed_seq to prevent a premature
 * OOM/allocation failure due to parallel nodemask modification.
 */
static void mpol_rebind_policy(struct mempolicy *pol, const nodemask_t *newmask)
{
	if (!pol || pol->mode == MPOL_LOCAL)
		return;
	if (!mpol_store_user_nodemask(pol) &&
	    nodes_equal(pol->w.cpuset_mems_allowed, *newmask))
		return;

	mpol_ops[pol->mode].rebind(pol, newmask);
}

/*
 * Wrapper for mpol_rebind_policy() that just requires task
 * pointer, and updates task mempolicy.
 *
 * Called with task's alloc_lock held.
 */

void mpol_rebind_task(struct task_struct *tsk, const nodemask_t *new)
{
	mpol_rebind_policy(tsk->mempolicy, new);
}

/*
 * Rebind each vma in mm to new nodemask.
 *
 * Call holding a reference to mm.  Takes mm->mmap_lock during call.
 */

void mpol_rebind_mm(struct mm_struct *mm, nodemask_t *new)
{
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, mm, 0);

	mmap_write_lock(mm);
	for_each_vma(vmi, vma)
		mpol_rebind_policy(vma->vm_policy, new);
	mmap_write_unlock(mm);
}

static const struct mempolicy_operations mpol_ops[MPOL_MAX] = {
	[MPOL_DEFAULT] = {
		.rebind = mpol_rebind_default,
	},
	[MPOL_INTERLEAVE] = {
		.create = mpol_new_nodemask,
		.rebind = mpol_rebind_nodemask,
	},
	[MPOL_PREFERRED] = {
		.create = mpol_new_preferred,
		.rebind = mpol_rebind_preferred,
	},
	[MPOL_BIND] = {
		.create = mpol_new_nodemask,
		.rebind = mpol_rebind_nodemask,
	},
	[MPOL_LOCAL] = {
		.rebind = mpol_rebind_default,
	},
	[MPOL_PREFERRED_MANY] = {
		.create = mpol_new_nodemask,
		.rebind = mpol_rebind_preferred,
	},
};

static int migrate_page_add(struct page *page, struct list_head *pagelist,
				unsigned long flags);

struct queue_pages {
	struct list_head *pagelist;
	unsigned long flags;
	nodemask_t *nmask;
	unsigned long start;
	unsigned long end;
	struct vm_area_struct *first;
};

/*
 * Check if the page's nid is in qp->nmask.
 *
 * If MPOL_MF_INVERT is set in qp->flags, check if the nid is
 * in the invert of qp->nmask.
 */
static inline bool queue_pages_required(struct page *page,
					struct queue_pages *qp)
{
	int nid = page_to_nid(page);
	unsigned long flags = qp->flags;

	return node_isset(nid, *qp->nmask) == !(flags & MPOL_MF_INVERT);
}

/*
 * queue_pages_pmd() has three possible return values:
 * 0 - pages are placed on the right node or queued successfully, or
 *     special page is met, i.e. huge zero page.
 * 1 - there is unmovable page, and MPOL_MF_MOVE* & MPOL_MF_STRICT were
 *     specified.
 * -EIO - is migration entry or only MPOL_MF_STRICT was specified and an
 *        existing page was already on a node that does not follow the
 *        policy.
 */
static int queue_pages_pmd(pmd_t *pmd, spinlock_t *ptl, unsigned long addr,
				unsigned long end, struct mm_walk *walk)
	__releases(ptl)
{
	int ret = 0;
	struct page *page;
	struct queue_pages *qp = walk->private;
	unsigned long flags;

	if (unlikely(is_pmd_migration_entry(*pmd))) {
		ret = -EIO;
		goto unlock;
	}
	page = pmd_page(*pmd);
	if (is_huge_zero_page(page)) {
		walk->action = ACTION_CONTINUE;
		goto unlock;
	}
	if (!queue_pages_required(page, qp))
		goto unlock;

	flags = qp->flags;
	/* go to thp migration */
	if (flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)) {
		if (!vma_migratable(walk->vma) ||
		    migrate_page_add(page, qp->pagelist, flags)) {
			ret = 1;
			goto unlock;
		}
	} else
		ret = -EIO;
unlock:
	spin_unlock(ptl);
	return ret;
}

/*
 * Scan through pages checking if pages follow certain conditions,
 * and move them to the pagelist if they do.
 *
 * queue_pages_pte_range() has three possible return values:
 * 0 - pages are placed on the right node or queued successfully, or
 *     special page is met, i.e. zero page.
 * 1 - there is unmovable page, and MPOL_MF_MOVE* & MPOL_MF_STRICT were
 *     specified.
 * -EIO - only MPOL_MF_STRICT was specified and an existing page was already
 *        on a node that does not follow the policy.
 */
static int queue_pages_pte_range(pmd_t *pmd, unsigned long addr,
			unsigned long end, struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	struct page *page;
	struct queue_pages *qp = walk->private;
	unsigned long flags = qp->flags;
	bool has_unmovable = false;
	pte_t *pte, *mapped_pte;
	spinlock_t *ptl;

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (ptl)
		return queue_pages_pmd(pmd, ptl, addr, end, walk);

	if (pmd_trans_unstable(pmd))
		return 0;

	mapped_pte = pte = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
	for (; addr != end; pte++, addr += PAGE_SIZE) {
		if (!pte_present(*pte))
			continue;
		page = vm_normal_page(vma, addr, *pte);
		if (!page || is_zone_device_page(page))
			continue;
		/*
		 * vm_normal_page() filters out zero pages, but there might
		 * still be PageReserved pages to skip, perhaps in a VDSO.
		 */
		if (PageReserved(page))
			continue;
		if (!queue_pages_required(page, qp))
			continue;
		if (flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)) {
			/* MPOL_MF_STRICT must be specified if we get here */
			if (!vma_migratable(vma)) {
				has_unmovable = true;
				break;
			}

			/*
			 * Do not abort immediately since there may be
			 * temporary off LRU pages in the range.  Still
			 * need migrate other LRU pages.
			 */
			if (migrate_page_add(page, qp->pagelist, flags))
				has_unmovable = true;
		} else
			break;
	}
	pte_unmap_unlock(mapped_pte, ptl);
	cond_resched();

	if (has_unmovable)
		return 1;

	return addr != end ? -EIO : 0;
}

static int queue_pages_hugetlb(pte_t *pte, unsigned long hmask,
			       unsigned long addr, unsigned long end,
			       struct mm_walk *walk)
{
	int ret = 0;
#ifdef CONFIG_HUGETLB_PAGE
	struct queue_pages *qp = walk->private;
	unsigned long flags = (qp->flags & MPOL_MF_VALID);
	struct page *page;
	spinlock_t *ptl;
	pte_t entry;

	ptl = huge_pte_lock(hstate_vma(walk->vma), walk->mm, pte);
	entry = huge_ptep_get(pte);
	if (!pte_present(entry))
		goto unlock;
	page = pte_page(entry);
	if (!queue_pages_required(page, qp))
		goto unlock;

	if (flags == MPOL_MF_STRICT) {
		/*
		 * STRICT alone means only detecting misplaced page and no
		 * need to further check other vma.
		 */
		ret = -EIO;
		goto unlock;
	}

	if (!vma_migratable(walk->vma)) {
		/*
		 * Must be STRICT with MOVE*, otherwise .test_walk() have
		 * stopped walking current vma.
		 * Detecting misplaced page but allow migrating pages which
		 * have been queued.
		 */
		ret = 1;
		goto unlock;
	}

	/* With MPOL_MF_MOVE, we migrate only unshared hugepage. */
	if (flags & (MPOL_MF_MOVE_ALL) ||
	    (flags & MPOL_MF_MOVE && page_mapcount(page) == 1 &&
	     !hugetlb_pmd_shared(pte))) {
		if (isolate_hugetlb(page, qp->pagelist) &&
			(flags & MPOL_MF_STRICT))
			/*
			 * Failed to isolate page but allow migrating pages
			 * which have been queued.
			 */
			ret = 1;
	}
unlock:
	spin_unlock(ptl);
#else
	BUG();
#endif
	return ret;
}

#ifdef CONFIG_NUMA_BALANCING
/*
 * This is used to mark a range of virtual addresses to be inaccessible.
 * These are later cleared by a NUMA hinting fault. Depending on these
 * faults, pages may be migrated for better NUMA placement.
 *
 * This is assuming that NUMA faults are handled using PROT_NONE. If
 * an architecture makes a different choice, it will need further
 * changes to the core.
 */
unsigned long change_prot_numa(struct vm_area_struct *vma,
			unsigned long addr, unsigned long end)
{
	struct mmu_gather tlb;
	int nr_updated;

	tlb_gather_mmu(&tlb, vma->vm_mm);

	nr_updated = change_protection(&tlb, vma, addr, end, PAGE_NONE,
				       MM_CP_PROT_NUMA);
	if (nr_updated)
		count_vm_numa_events(NUMA_PTE_UPDATES, nr_updated);

	tlb_finish_mmu(&tlb);

	return nr_updated;
}
#else
static unsigned long change_prot_numa(struct vm_area_struct *vma,
			unsigned long addr, unsigned long end)
{
	return 0;
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

	if (flags & MPOL_MF_LAZY) {
		/* Similar to task_numa_work, skip inaccessible VMAs */
		if (!is_vm_hugetlb_page(vma) && vma_is_accessible(vma) &&
			!(vma->vm_flags & VM_MIXEDMAP))
			change_prot_numa(vma, start, endvma);
		return 1;
	}

	/* queue pages from current vma */
	if (flags & MPOL_MF_VALID)
		return 0;
	return 1;
}

static const struct mm_walk_ops queue_pages_walk_ops = {
	.hugetlb_entry		= queue_pages_hugetlb,
	.pmd_entry		= queue_pages_pte_range,
	.test_walk		= queue_pages_test_walk,
};

/*
 * Walk through page tables and collect pages to be migrated.
 *
 * If pages found in a given range are on a set of nodes (determined by
 * @nodes and @flags,) it's isolated and queued to the pagelist which is
 * passed via @private.
 *
 * queue_pages_range() has three possible return values:
 * 1 - there is unmovable page, but MPOL_MF_MOVE* & MPOL_MF_STRICT were
 *     specified.
 * 0 - queue pages successfully or no misplaced page.
 * errno - i.e. misplaced pages with MPOL_MF_STRICT specified (-EIO) or
 *         memory range specified by nodemask and maxnode points outside
 *         your accessible address space (-EFAULT)
 */
static int
queue_pages_range(struct mm_struct *mm, unsigned long start, unsigned long end,
		nodemask_t *nodes, unsigned long flags,
		struct list_head *pagelist)
{
	int err;
	struct queue_pages qp = {
		.pagelist = pagelist,
		.flags = flags,
		.nmask = nodes,
		.start = start,
		.end = end,
		.first = NULL,
	};

	err = walk_page_range(mm, start, end, &queue_pages_walk_ops, &qp);

	if (!qp.first)
		/* whole range in hole */
		err = -EFAULT;

	return err;
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

	pr_debug("vma %lx-%lx/%lx vm_ops %p vm_file %p set_policy %p\n",
		 vma->vm_start, vma->vm_end, vma->vm_pgoff,
		 vma->vm_ops, vma->vm_file,
		 vma->vm_ops ? vma->vm_ops->set_policy : NULL);

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
	struct vm_area_struct *merged;
	unsigned long vmstart, vmend;
	pgoff_t pgoff;
	int err;

	vmend = min(end, vma->vm_end);
	if (start > vma->vm_start) {
		*prev = vma;
		vmstart = start;
	} else {
		vmstart = vma->vm_start;
	}

	if (mpol_equal(vma_policy(vma), new_pol))
		return 0;

	pgoff = vma->vm_pgoff + ((vmstart - vma->vm_start) >> PAGE_SHIFT);
	merged = vma_merge(vma->vm_mm, *prev, vmstart, vmend, vma->vm_flags,
			   vma->anon_vma, vma->vm_file, pgoff, new_pol,
			   vma->vm_userfaultfd_ctx, anon_vma_name(vma));
	if (merged) {
		*prev = merged;
		/* vma_merge() invalidated the mas */
		mas_pause(&vmi->mas);
		return vma_replace_policy(merged, new_pol);
	}

	if (vma->vm_start != vmstart) {
		err = split_vma(vma->vm_mm, vma, vmstart, 1);
		if (err)
			return err;
		/* split_vma() invalidated the mas */
		mas_pause(&vmi->mas);
	}

	if (vma->vm_end != vmend) {
		err = split_vma(vma->vm_mm, vma, vmend, 0);
		if (err)
			return err;
		/* split_vma() invalidated the mas */
		mas_pause(&vmi->mas);
	}

	*prev = vma;
	return vma_replace_policy(vma, new_pol);
}

/* Set the process memory policy */
static long do_set_mempolicy(unsigned short mode, unsigned short flags,
			     nodemask_t *nodes)
{
	struct mempolicy *new, *old;
	NODEMASK_SCRATCH(scratch);
	int ret;

	if (!scratch)
		return -ENOMEM;

	new = mpol_new(mode, flags, nodes);
	if (IS_ERR(new)) {
		ret = PTR_ERR(new);
		goto out;
	}

	task_lock(current);
	ret = mpol_set_nodemask(new, nodes, scratch);
	if (ret) {
		task_unlock(current);
		mpol_put(new);
		goto out;
	}

	old = current->mempolicy;
	current->mempolicy = new;
	if (new && new->mode == MPOL_INTERLEAVE)
		current->il_prev = MAX_NUMNODES-1;
	task_unlock(current);
	mpol_put(old);
	ret = 0;
out:
	NODEMASK_SCRATCH_FREE(scratch);
	return ret;
}

/*
 * Return nodemask for policy for get_mempolicy() query
 *
 * Called with task's alloc_lock held
 */
static void get_policy_nodemask(struct mempolicy *p, nodemask_t *nodes)
{
	nodes_clear(*nodes);
	if (p == &default_policy)
		return;

	switch (p->mode) {
	case MPOL_BIND:
	case MPOL_INTERLEAVE:
	case MPOL_PREFERRED:
	case MPOL_PREFERRED_MANY:
		*nodes = p->nodes;
		break;
	case MPOL_LOCAL:
		/* return empty node mask for local allocation */
		break;
	default:
		BUG();
	}
}

static int lookup_node(struct mm_struct *mm, unsigned long addr)
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
static long do_get_mempolicy(int *policy, nodemask_t *nmask,
			     unsigned long addr, unsigned long flags)
{
	int err;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = NULL;
	struct mempolicy *pol = current->mempolicy, *pol_refcount = NULL;

	if (flags &
		~(unsigned long)(MPOL_F_NODE|MPOL_F_ADDR|MPOL_F_MEMS_ALLOWED))
		return -EINVAL;

	if (flags & MPOL_F_MEMS_ALLOWED) {
		if (flags & (MPOL_F_NODE|MPOL_F_ADDR))
			return -EINVAL;
		*policy = 0;	/* just so it's initialized */
		task_lock(current);
		*nmask  = cpuset_current_mems_allowed;
		task_unlock(current);
		return 0;
	}

	if (flags & MPOL_F_ADDR) {
		/*
		 * Do NOT fall back to task policy if the
		 * vma/shared policy at addr is NULL.  We
		 * want to return MPOL_DEFAULT in this case.
		 */
		mmap_read_lock(mm);
		vma = vma_lookup(mm, addr);
		if (!vma) {
			mmap_read_unlock(mm);
			return -EFAULT;
		}
		if (vma->vm_ops && vma->vm_ops->get_policy)
			pol = vma->vm_ops->get_policy(vma, addr);
		else
			pol = vma->vm_policy;
	} else if (addr)
		return -EINVAL;

	if (!pol)
		pol = &default_policy;	/* indicates default behavior */

	if (flags & MPOL_F_NODE) {
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
			err = lookup_node(mm, addr);
			if (err < 0)
				goto out;
			*policy = err;
		} else if (pol == current->mempolicy &&
				pol->mode == MPOL_INTERLEAVE) {
			*policy = next_node_in(current->il_prev, pol->nodes);
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
		if (mpol_store_user_nodemask(pol)) {
			*nmask = pol->w.user_nodemask;
		} else {
			task_lock(current);
			get_policy_nodemask(pol, nmask);
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
/*
 * page migration, thp tail pages can be passed.
 */
static int migrate_page_add(struct page *page, struct list_head *pagelist,
				unsigned long flags)
{
	struct page *head = compound_head(page);
	/*
	 * Avoid migrating a page that is shared with others.
	 */
	if ((flags & MPOL_MF_MOVE_ALL) || page_mapcount(head) == 1) {
		if (!isolate_lru_page(head)) {
			list_add_tail(&head->lru, pagelist);
			mod_node_page_state(page_pgdat(head),
				NR_ISOLATED_ANON + page_is_file_lru(head),
				thp_nr_pages(head));
		} else if (flags & MPOL_MF_STRICT) {
			/*
			 * Non-movable page may reach here.  And, there may be
			 * temporary off LRU pages or non-LRU movable pages.
			 * Treat them as unmovable pages since they can't be
			 * isolated, so they can't be moved at the moment.  It
			 * should return -EIO for this case too.
			 */
			return -EIO;
		}
	}

	return 0;
}

/*
 * Migrate pages from one node to a target node.
 * Returns error or the number of pages not migrated.
 */
static int migrate_to_node(struct mm_struct *mm, int source, int dest,
			   int flags)
{
	nodemask_t nmask;
	struct vm_area_struct *vma;
	LIST_HEAD(pagelist);
	int err = 0;
	struct migration_target_control mtc = {
		.nid = dest,
		.gfp_mask = GFP_HIGHUSER_MOVABLE | __GFP_THISNODE,
	};

	nodes_clear(nmask);
	node_set(source, nmask);

	/*
	 * This does not "check" the range but isolates all pages that
	 * need migration.  Between passing in the full user address
	 * space range and MPOL_MF_DISCONTIG_OK, this call can not fail.
	 */
	vma = find_vma(mm, 0);
	VM_BUG_ON(!(flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)));
	queue_pages_range(mm, vma->vm_start, mm->task_size, &nmask,
			flags | MPOL_MF_DISCONTIG_OK, &pagelist);

	if (!list_empty(&pagelist)) {
		err = migrate_pages(&pagelist, alloc_migration_target, NULL,
				(unsigned long)&mtc, MIGRATE_SYNC, MR_SYSCALL, NULL);
		if (err)
			putback_movable_pages(&pagelist);
	}

	return err;
}

/*
 * Move pages between the two nodesets so as to preserve the physical
 * layout as much as possible.
 *
 * Returns the number of page that could not be moved.
 */
int do_migrate_pages(struct mm_struct *mm, const nodemask_t *from,
		     const nodemask_t *to, int flags)
{
	int busy = 0;
	int err = 0;
	nodemask_t tmp;

	lru_cache_disable();

	mmap_read_lock(mm);

	/*
	 * Find a 'source' bit set in 'tmp' whose corresponding 'dest'
	 * bit in 'to' is not also set in 'tmp'.  Clear the found 'source'
	 * bit in 'tmp', and return that <source, dest> pair for migration.
	 * The pair of nodemasks 'to' and 'from' define the map.
	 *
	 * If no pair of bits is found that way, fallback to picking some
	 * pair of 'source' and 'dest' bits that are not the same.  If the
	 * 'source' and 'dest' bits are the same, this represents a node
	 * that will be migrating to itself, so no pages need move.
	 *
	 * If no bits are left in 'tmp', or if all remaining bits left
	 * in 'tmp' correspond to the same bit in 'to', return false
	 * (nothing left to migrate).
	 *
	 * This lets us pick a pair of nodes to migrate between, such that
	 * if possible the dest node is not already occupied by some other
	 * source node, minimizing the risk of overloading the memory on a
	 * node that would happen if we migrated incoming memory to a node
	 * before migrating outgoing memory source that same node.
	 *
	 * A single scan of tmp is sufficient.  As we go, we remember the
	 * most recent <s, d> pair that moved (s != d).  If we find a pair
	 * that not only moved, but what's better, moved to an empty slot
	 * (d is not set in tmp), then we break out then, with that pair.
	 * Otherwise when we finish scanning from_tmp, we at least have the
	 * most recent <s, d> pair that moved.  If we get all the way through
	 * the scan of tmp without finding any node that moved, much less
	 * moved to an empty node, then there is nothing left worth migrating.
	 */

	tmp = *from;
	while (!nodes_empty(tmp)) {
		int s, d;
		int source = NUMA_NO_NODE;
		int dest = 0;

		for_each_node_mask(s, tmp) {

			/*
			 * do_migrate_pages() tries to maintain the relative
			 * node relationship of the pages established between
			 * threads and memory areas.
                         *
			 * However if the number of source nodes is not equal to
			 * the number of destination nodes we can not preserve
			 * this node relative relationship.  In that case, skip
			 * copying memory from a node that is in the destination
			 * mask.
			 *
			 * Example: [2,3,4] -> [3,4,5] moves everything.
			 *          [0-7] - > [3,4,5] moves only 0,1,2,6,7.
			 */

			if ((nodes_weight(*from) != nodes_weight(*to)) &&
						(node_isset(s, *to)))
				continue;

			d = node_remap(s, *from, *to);
			if (s == d)
				continue;

			source = s;	/* Node moved. Memorize */
			dest = d;

			/* dest not in remaining from nodes? */
			if (!node_isset(dest, tmp))
				break;
		}
		if (source == NUMA_NO_NODE)
			break;

		node_clear(source, tmp);
		err = migrate_to_node(mm, source, dest, flags);
		if (err > 0)
			busy += err;
		if (err < 0)
			break;
	}
	mmap_read_unlock(mm);

	lru_cache_enable();
	if (err < 0)
		return err;
	return busy;

}

/*
 * Allocate a new page for page migration based on vma policy.
 * Start by assuming the page is mapped by the same vma as contains @start.
 * Search forward from there, if not.  N.B., this assumes that the
 * list of pages handed to migrate_pages()--which is how we get here--
 * is in virtual address order.
 */
static struct page *new_page(struct page *page, unsigned long start)
{
	struct folio *dst, *src = page_folio(page);
	struct vm_area_struct *vma;
	unsigned long address;
	VMA_ITERATOR(vmi, current->mm, start);
	gfp_t gfp = GFP_HIGHUSER_MOVABLE | __GFP_RETRY_MAYFAIL;

	for_each_vma(vmi, vma) {
		address = page_address_in_vma(page, vma);
		if (address != -EFAULT)
			break;
	}

	if (folio_test_hugetlb(src))
		return alloc_huge_page_vma(page_hstate(&src->page),
				vma, address);

	if (folio_test_large(src))
		gfp = GFP_TRANSHUGE;

	/*
	 * if !vma, vma_alloc_folio() will use task or system default policy
	 */
	dst = vma_alloc_folio(gfp, folio_order(src), vma, address,
			folio_test_large(src));
	return &dst->page;
}
#else

static int migrate_page_add(struct page *page, struct list_head *pagelist,
				unsigned long flags)
{
	return -EIO;
}

int do_migrate_pages(struct mm_struct *mm, const nodemask_t *from,
		     const nodemask_t *to, int flags)
{
	return -ENOSYS;
}

static struct page *new_page(struct page *page, unsigned long start)
{
	return NULL;
}
#endif

static long do_mbind(unsigned long start, unsigned long len,
		     unsigned short mode, unsigned short mode_flags,
		     nodemask_t *nmask, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	struct vma_iterator vmi;
	struct mempolicy *new;
	unsigned long end;
	int err;
	int ret;
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

	if (flags & MPOL_MF_LAZY)
		new->flags |= MPOL_F_MOF;

	/*
	 * If we are using the default policy then operation
	 * on discontinuous address spaces is okay after all
	 */
	if (!new)
		flags |= MPOL_MF_DISCONTIG_OK;

	pr_debug("mbind %lx-%lx mode:%d flags:%d nodes:%lx\n",
		 start, start + len, mode, mode_flags,
		 nmask ? nodes_addr(*nmask)[0] : NUMA_NO_NODE);

	if (flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)) {

		lru_cache_disable();
	}
	{
		NODEMASK_SCRATCH(scratch);
		if (scratch) {
			mmap_write_lock(mm);
			err = mpol_set_nodemask(new, nmask, scratch);
			if (err)
				mmap_write_unlock(mm);
		} else
			err = -ENOMEM;
		NODEMASK_SCRATCH_FREE(scratch);
	}
	if (err)
		goto mpol_out;

	ret = queue_pages_range(mm, start, end, nmask,
			  flags | MPOL_MF_INVERT, &pagelist);

	if (ret < 0) {
		err = ret;
		goto up_out;
	}

	vma_iter_init(&vmi, mm, start);
	prev = vma_prev(&vmi);
	for_each_vma_range(vmi, vma, end) {
		err = mbind_range(&vmi, vma, &prev, start, end, new);
		if (err)
			break;
	}

	if (!err) {
		int nr_failed = 0;

		if (!list_empty(&pagelist)) {
			WARN_ON_ONCE(flags & MPOL_MF_LAZY);
			nr_failed = migrate_pages(&pagelist, new_page, NULL,
				start, MIGRATE_SYNC, MR_MEMPOLICY_MBIND, NULL);
			if (nr_failed)
				putback_movable_pages(&pagelist);
		}

		if ((ret > 0) || (nr_failed && (flags & MPOL_MF_STRICT)))
			err = -EIO;
	} else {
up_out:
		if (!list_empty(&pagelist))
			putback_movable_pages(&pagelist);
	}

	mmap_write_unlock(mm);
mpol_out:
	mpol_put(new);
	if (flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL))
		lru_cache_enable();
	return err;
}

/*
 * User space interface with variable sized bitmaps for nodelists.
 */
static int get_bitmap(unsigned long *mask, const unsigned long __user *nmask,
		      unsigned long maxnode)
{
	unsigned long nlongs = BITS_TO_LONGS(maxnode);
	int ret;

	if (in_compat_syscall())
		ret = compat_get_bitmap(mask,
					(const compat_ulong_t __user *)nmask,
					maxnode);
	else
		ret = copy_from_user(mask, nmask,
				     nlongs * sizeof(unsigned long));

	if (ret)
		return -EFAULT;

	if (maxnode % BITS_PER_LONG)
		mask[nlongs - 1] &= (1UL << (maxnode % BITS_PER_LONG)) - 1;

	return 0;
}

/* Copy a node mask from user space. */
static int get_nodes(nodemask_t *nodes, const unsigned long __user *nmask,
		     unsigned long maxnode)
{
	--maxnode;
	nodes_clear(*nodes);
	if (maxnode == 0 || !nmask)
		return 0;
	if (maxnode > PAGE_SIZE*BITS_PER_BYTE)
		return -EINVAL;

	/*
	 * When the user specified more nodes than supported just check
	 * if the non supported part is all zero, one word at a time,
	 * starting at the end.
	 */
	while (maxnode > MAX_NUMNODES) {
		unsigned long bits = min_t(unsigned long, maxnode, BITS_PER_LONG);
		unsigned long t;

		if (get_bitmap(&t, &nmask[(maxnode - 1) / BITS_PER_LONG], bits))
			return -EFAULT;

		if (maxnode - bits >= MAX_NUMNODES) {
			maxnode -= bits;
		} else {
			maxnode = MAX_NUMNODES;
			t &= ~((1UL << (MAX_NUMNODES % BITS_PER_LONG)) - 1);
		}
		if (t)
			return -EINVAL;
	}

	return get_bitmap(nodes_addr(*nodes), nmask, maxnode);
}

/* Copy a kernel node mask to user space */
static int copy_nodes_to_user(unsigned long __user *mask, unsigned long maxnode,
			      nodemask_t *nodes)
{
	unsigned long copy = ALIGN(maxnode-1, 64) / 8;
	unsigned int nbytes = BITS_TO_LONGS(nr_node_ids) * sizeof(long);
	bool compat = in_compat_syscall();

	if (compat)
		nbytes = BITS_TO_COMPAT_LONGS(nr_node_ids) * sizeof(compat_long_t);

	if (copy > nbytes) {
		if (copy > PAGE_SIZE)
			return -EINVAL;
		if (clear_user((char __user *)mask + nbytes, copy - nbytes))
			return -EFAULT;
		copy = nbytes;
		maxnode = nr_node_ids;
	}

	if (compat)
		return compat_put_bitmap((compat_ulong_t __user *)mask,
					 nodes_addr(*nodes), maxnode);

	return copy_to_user(mask, nodes_addr(*nodes), copy) ? -EFAULT : 0;
}

/* Basic parameter sanity check used by both mbind() and set_mempolicy() */
static inline int sanitize_mpol_flags(int *mode, unsigned short *flags)
{
	*flags = *mode & MPOL_MODE_FLAGS;
	*mode &= ~MPOL_MODE_FLAGS;

	if ((unsigned int)(*mode) >=  MPOL_MAX)
		return -EINVAL;
	if ((*flags & MPOL_F_STATIC_NODES) && (*flags & MPOL_F_RELATIVE_NODES))
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
			 unsigned long maxnode, unsigned int flags)
{
	unsigned short mode_flags;
	nodemask_t nodes;
	int lmode = mode;
	int err;

	start = untagged_addr(start);
	err = sanitize_mpol_flags(&lmode, &mode_flags);
	if (err)
		return err;

	err = get_nodes(&nodes, nmask, maxnode);
	if (err)
		return err;

	return do_mbind(start, len, lmode, mode_flags, &nodes, flags);
}

SYSCALL_DEFINE4(set_mempolicy_home_node, unsigned long, start, unsigned long, len,
		unsigned long, home_node, unsigned long, flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	struct mempolicy *new;
	unsigned long end;
	int err = -ENOENT;
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
	 * Check home_node is online to avoid accessing uninitialized
	 * NODE_DATA.
	 */
	if (home_node >= MAX_NUMNODES || !node_online(home_node))
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
		new = mpol_dup(vma_policy(vma));
		if (IS_ERR(new)) {
			err = PTR_ERR(new);
			break;
		}
		/*
		 * Only update home node if there is an existing vma policy
		 */
		if (!new)
			continue;

		/*
		 * If any vma in the range got policy other than MPOL_BIND
		 * or MPOL_PREFERRED_MANY we return error. We don't reset
		 * the home node for vmas we already updated before.
		 */
		if (new->mode != MPOL_BIND && new->mode != MPOL_PREFERRED_MANY) {
			mpol_put(new);
			err = -EOPNOTSUPP;
			break;
		}

		new->home_node = home_node;
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
		unsigned long, maxnode, unsigned int, flags)
{
	return kernel_mbind(start, len, mode, nmask, maxnode, flags);
}

/* Set the process memory policy */
static long kernel_set_mempolicy(int mode, const unsigned long __user *nmask,
				 unsigned long maxnode)
{
	unsigned short mode_flags;
	nodemask_t nodes;
	int lmode = mode;
	int err;

	err = sanitize_mpol_flags(&lmode, &mode_flags);
	if (err)
		return err;

	err = get_nodes(&nodes, nmask, maxnode);
	if (err)
		return err;

	return do_set_mempolicy(lmode, mode_flags, &nodes);
}

SYSCALL_DEFINE3(set_mempolicy, int, mode, const unsigned long __user *, nmask,
		unsigned long, maxnode)
{
	return kernel_set_mempolicy(mode, nmask, maxnode);
}

static int kernel_migrate_pages(pid_t pid, unsigned long maxnode,
				const unsigned long __user *old_nodes,
				const unsigned long __user *new_nodes)
{
	struct mm_struct *mm = NULL;
	struct task_struct *task;
	nodemask_t task_nodes;
	int err;
	nodemask_t *old;
	nodemask_t *new;
	NODEMASK_SCRATCH(scratch);

	if (!scratch)
		return -ENOMEM;

	old = &scratch->mask1;
	new = &scratch->mask2;

	err = get_nodes(old, old_nodes, maxnode);
	if (err)
		goto out;

	err = get_nodes(new, new_nodes, maxnode);
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

	task_nodes = cpuset_mems_allowed(task);
	/* Is the user allowed to access the target nodes? */
	if (!nodes_subset(*new, task_nodes) && !capable(CAP_SYS_NICE)) {
		err = -EPERM;
		goto out_put;
	}

	task_nodes = cpuset_mems_allowed(current);
	nodes_and(*new, *new, task_nodes);
	if (nodes_empty(*new))
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
	NODEMASK_SCRATCH_FREE(scratch);

	return err;

out_put:
	put_task_struct(task);
	goto out;

}

SYSCALL_DEFINE4(migrate_pages, pid_t, pid, unsigned long, maxnode,
		const unsigned long __user *, old_nodes,
		const unsigned long __user *, new_nodes)
{
	return kernel_migrate_pages(pid, maxnode, old_nodes, new_nodes);
}


/* Retrieve NUMA policy */
static int kernel_get_mempolicy(int __user *policy,
				unsigned long __user *nmask,
				unsigned long maxnode,
				unsigned long addr,
				unsigned long flags)
{
	int err;
	int pval;
	nodemask_t nodes;

	if (nmask != NULL && maxnode < nr_node_ids)
		return -EINVAL;

	addr = untagged_addr(addr);

	err = do_get_mempolicy(&pval, &nodes, addr, flags);

	if (err)
		return err;

	if (policy && put_user(pval, policy))
		return -EFAULT;

	if (nmask)
		err = copy_nodes_to_user(nmask, maxnode, &nodes);

	return err;
}

SYSCALL_DEFINE5(get_mempolicy, int __user *, policy,
		unsigned long __user *, nmask, unsigned long, maxnode,
		unsigned long, addr, unsigned long, flags)
{
	return kernel_get_mempolicy(policy, nmask, maxnode, addr, flags);
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
	 * Migration allocates pages in the highest zone. If we cannot
	 * do so then migration (at least from node to node) is not
	 * possible.
	 */
	if (vma->vm_file &&
		gfp_zone(mapping_gfp_mask(vma->vm_file->f_mapping))
			< policy_zone)
		return false;
	return true;
}

struct mempolicy *__get_vma_policy(struct vm_area_struct *vma,
						unsigned long addr)
{
	struct mempolicy *pol = NULL;

	if (vma) {
		if (vma->vm_ops && vma->vm_ops->get_policy) {
			pol = vma->vm_ops->get_policy(vma, addr);
		} else if (vma->vm_policy) {
			pol = vma->vm_policy;

			/*
			 * shmem_alloc_page() passes MPOL_F_SHARED policy with
			 * a pseudo vma whose vma->vm_ops=NULL. Take a reference
			 * count on these policies which will be dropped by
			 * mpol_cond_put() later
			 */
			if (mpol_needs_cond_ref(pol))
				mpol_get(pol);
		}
	}

	return pol;
}

/*
 * get_vma_policy(@vma, @addr)
 * @vma: virtual memory area whose policy is sought
 * @addr: address in @vma for shared policy lookup
 *
 * Returns effective policy for a VMA at specified address.
 * Falls back to current->mempolicy or system default policy, as necessary.
 * Shared policies [those marked as MPOL_F_SHARED] require an extra reference
 * count--added by the get_policy() vm_op, as appropriate--to protect against
 * freeing by another task.  It is the caller's responsibility to free the
 * extra reference for shared policies.
 */
static struct mempolicy *get_vma_policy(struct vm_area_struct *vma,
						unsigned long addr)
{
	struct mempolicy *pol = __get_vma_policy(vma, addr);

	if (!pol)
		pol = get_task_policy(current);

	return pol;
}

bool vma_policy_mof(struct vm_area_struct *vma)
{
	struct mempolicy *pol;

	if (vma->vm_ops && vma->vm_ops->get_policy) {
		bool ret = false;

		pol = vma->vm_ops->get_policy(vma, vma->vm_start);
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
	 * if policy->nodes has movable memory only,
	 * we apply policy when gfp_zone(gfp) = ZONE_MOVABLE only.
	 *
	 * policy->nodes is intersect with node_states[N_MEMORY].
	 * so if the following test fails, it implies
	 * policy->nodes has movable memory only.
	 */
	if (!nodes_intersects(policy->nodes, node_states[N_HIGH_MEMORY]))
		dynamic_policy_zone = ZONE_MOVABLE;

	return zone >= dynamic_policy_zone;
}

/*
 * Return a nodemask representing a mempolicy for filtering nodes for
 * page allocation
 */
nodemask_t *policy_nodemask(gfp_t gfp, struct mempolicy *policy)
{
	int mode = policy->mode;

	/* Lower zones don't get a nodemask applied for MPOL_BIND */
	if (unlikely(mode == MPOL_BIND) &&
		apply_policy_zone(policy, gfp_zone(gfp)) &&
		cpuset_nodemask_valid_mems_allowed(&policy->nodes))
		return &policy->nodes;

	if (mode == MPOL_PREFERRED_MANY)
		return &policy->nodes;

	return NULL;
}

/*
 * Return the  preferred node id for 'prefer' mempolicy, and return
 * the given id for all other policies.
 *
 * policy_node() is always coupled with policy_nodemask(), which
 * secures the nodemask limit for 'bind' and 'prefer-many' policy.
 */
static int policy_node(gfp_t gfp, struct mempolicy *policy, int nd)
{
	if (policy->mode == MPOL_PREFERRED) {
		nd = first_node(policy->nodes);
	} else {
		/*
		 * __GFP_THISNODE shouldn't even be used with the bind policy
		 * because we might easily break the expectation to stay on the
		 * requested node and not break the policy.
		 */
		WARN_ON_ONCE(policy->mode == MPOL_BIND && (gfp & __GFP_THISNODE));
	}

	if ((policy->mode == MPOL_BIND ||
	     policy->mode == MPOL_PREFERRED_MANY) &&
	    policy->home_node != NUMA_NO_NODE)
		return policy->home_node;

	return nd;
}

/* Do dynamic interleaving for a process */
static unsigned interleave_nodes(struct mempolicy *policy)
{
	unsigned next;
	struct task_struct *me = current;

	next = next_node_in(me->il_prev, policy->nodes);
	if (next < MAX_NUMNODES)
		me->il_prev = next;
	return next;
}

/*
 * Depending on the memory policy provide a node from which to allocate the
 * next slab entry.
 */
unsigned int mempolicy_slab_node(void)
{
	struct mempolicy *policy;
	int node = numa_mem_id();

	if (!in_task())
		return node;

	policy = current->mempolicy;
	if (!policy)
		return node;

	switch (policy->mode) {
	case MPOL_PREFERRED:
		return first_node(policy->nodes);

	case MPOL_INTERLEAVE:
		return interleave_nodes(policy);

	case MPOL_BIND:
	case MPOL_PREFERRED_MANY:
	{
		struct zoneref *z;

		/*
		 * Follow bind policy behavior and start allocation at the
		 * first node.
		 */
		struct zonelist *zonelist;
		enum zone_type highest_zoneidx = gfp_zone(GFP_KERNEL);
		zonelist = &NODE_DATA(node)->node_zonelists[ZONELIST_FALLBACK];
		z = first_zones_zonelist(zonelist, highest_zoneidx,
							&policy->nodes);
		return z->zone ? zone_to_nid(z->zone) : node;
	}
	case MPOL_LOCAL:
		return node;

	default:
		BUG();
	}
}

/*
 * Do static interleaving for a VMA with known offset @n.  Returns the n'th
 * node in pol->nodes (starting from n=0), wrapping around if n exceeds the
 * number of present nodes.
 */
static unsigned offset_il_node(struct mempolicy *pol, unsigned long n)
{
	nodemask_t nodemask = pol->nodes;
	unsigned int target, nnodes;
	int i;
	int nid;
	/*
	 * The barrier will stabilize the nodemask in a register or on
	 * the stack so that it will stop changing under the code.
	 *
	 * Between first_node() and next_node(), pol->nodes could be changed
	 * by other threads. So we put pol->nodes in a local stack.
	 */
	barrier();

	nnodes = nodes_weight(nodemask);
	if (!nnodes)
		return numa_node_id();
	target = (unsigned int)n % nnodes;
	nid = first_node(nodemask);
	for (i = 0; i < target; i++)
		nid = next_node(nid, nodemask);
	return nid;
}

/* Determine a node number for interleave */
static inline unsigned interleave_nid(struct mempolicy *pol,
		 struct vm_area_struct *vma, unsigned long addr, int shift)
{
	if (vma) {
		unsigned long off;

		/*
		 * for small pages, there is no difference between
		 * shift and PAGE_SHIFT, so the bit-shift is safe.
		 * for huge pages, since vm_pgoff is in units of small
		 * pages, we need to shift off the always 0 bits to get
		 * a useful offset.
		 */
		BUG_ON(shift < PAGE_SHIFT);
		off = vma->vm_pgoff >> (shift - PAGE_SHIFT);
		off += (addr - vma->vm_start) >> shift;
		return offset_il_node(pol, off);
	} else
		return interleave_nodes(pol);
}

#ifdef CONFIG_HUGETLBFS
/*
 * huge_node(@vma, @addr, @gfp_flags, @mpol)
 * @vma: virtual memory area whose policy is sought
 * @addr: address in @vma for shared policy lookup and interleave policy
 * @gfp_flags: for requested zone
 * @mpol: pointer to mempolicy pointer for reference counted mempolicy
 * @nodemask: pointer to nodemask pointer for 'bind' and 'prefer-many' policy
 *
 * Returns a nid suitable for a huge page allocation and a pointer
 * to the struct mempolicy for conditional unref after allocation.
 * If the effective policy is 'bind' or 'prefer-many', returns a pointer
 * to the mempolicy's @nodemask for filtering the zonelist.
 *
 * Must be protected by read_mems_allowed_begin()
 */
int huge_node(struct vm_area_struct *vma, unsigned long addr, gfp_t gfp_flags,
				struct mempolicy **mpol, nodemask_t **nodemask)
{
	int nid;
	int mode;

	*mpol = get_vma_policy(vma, addr);
	*nodemask = NULL;
	mode = (*mpol)->mode;

	if (unlikely(mode == MPOL_INTERLEAVE)) {
		nid = interleave_nid(*mpol, vma, addr,
					huge_page_shift(hstate_vma(vma)));
	} else {
		nid = policy_node(gfp_flags, *mpol, numa_node_id());
		if (mode == MPOL_BIND || mode == MPOL_PREFERRED_MANY)
			*nodemask = &(*mpol)->nodes;
	}
	return nid;
}

/*
 * init_nodemask_of_mempolicy
 *
 * If the current task's mempolicy is "default" [NULL], return 'false'
 * to indicate default policy.  Otherwise, extract the policy nodemask
 * for 'bind' or 'interleave' policy into the argument nodemask, or
 * initialize the argument nodemask to contain the single node for
 * 'preferred' or 'local' policy and return 'true' to indicate presence
 * of non-default mempolicy.
 *
 * We don't bother with reference counting the mempolicy [mpol_get/put]
 * because the current task is examining it's own mempolicy and a task's
 * mempolicy is only ever changed by the task itself.
 *
 * N.B., it is the caller's responsibility to free a returned nodemask.
 */
bool init_nodemask_of_mempolicy(nodemask_t *mask)
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
		*mask = mempolicy->nodes;
		break;

	case MPOL_LOCAL:
		init_nodemask_of_node(mask, numa_node_id());
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
 * the policy nodemask. Otherwise, return true for all other policies
 * including "interleave", as a tsk with "interleave" policy may have
 * memory allocated from all nodes in system.
 *
 * Takes task_lock(tsk) to prevent freeing of its mempolicy.
 */
bool mempolicy_in_oom_domain(struct task_struct *tsk,
					const nodemask_t *mask)
{
	struct mempolicy *mempolicy;
	bool ret = true;

	if (!mask)
		return ret;

	task_lock(tsk);
	mempolicy = tsk->mempolicy;
	if (mempolicy && mempolicy->mode == MPOL_BIND)
		ret = nodes_intersects(mempolicy->nodes, *mask);
	task_unlock(tsk);

	return ret;
}

/* Allocate a page in interleaved policy.
   Own path because it needs to do special accounting. */
static struct page *alloc_page_interleave(gfp_t gfp, unsigned order,
					unsigned nid)
{
	struct page *page;

	page = __alloc_pages(gfp, order, nid, NULL);
	/* skip NUMA_INTERLEAVE_HIT counter update if numa stats is disabled */
	if (!static_branch_likely(&vm_numa_stat_key))
		return page;
	if (page && page_to_nid(page) == nid) {
		preempt_disable();
		__count_numa_event(page_zone(page), NUMA_INTERLEAVE_HIT);
		preempt_enable();
	}
	return page;
}

static struct page *alloc_pages_preferred_many(gfp_t gfp, unsigned int order,
						int nid, struct mempolicy *pol)
{
	struct page *page;
	gfp_t preferred_gfp;

	/*
	 * This is a two pass approach. The first pass will only try the
	 * preferred nodes but skip the direct reclaim and allow the
	 * allocation to fail, while the second pass will try all the
	 * nodes in system.
	 */
	preferred_gfp = gfp | __GFP_NOWARN;
	preferred_gfp &= ~(__GFP_DIRECT_RECLAIM | __GFP_NOFAIL);
	page = __alloc_pages(preferred_gfp, order, nid, &pol->nodes);
	if (!page)
		page = __alloc_pages(gfp, order, nid, NULL);

	return page;
}

/**
 * vma_alloc_folio - Allocate a folio for a VMA.
 * @gfp: GFP flags.
 * @order: Order of the folio.
 * @vma: Pointer to VMA or NULL if not available.
 * @addr: Virtual address of the allocation.  Must be inside @vma.
 * @hugepage: For hugepages try only the preferred node if possible.
 *
 * Allocate a folio for a specific address in @vma, using the appropriate
 * NUMA policy.  When @vma is not NULL the caller must hold the mmap_lock
 * of the mm_struct of the VMA to prevent it from going away.  Should be
 * used for all allocations for folios that will be mapped into user space.
 *
 * Return: The folio on success or NULL if allocation fails.
 */
struct folio *vma_alloc_folio(gfp_t gfp, int order, struct vm_area_struct *vma,
		unsigned long addr, bool hugepage)
{
	struct mempolicy *pol;
	int node = numa_node_id();
	struct folio *folio;
	int preferred_nid;
	nodemask_t *nmask;

	pol = get_vma_policy(vma, addr);

	if (pol->mode == MPOL_INTERLEAVE) {
		struct page *page;
		unsigned nid;

		nid = interleave_nid(pol, vma, addr, PAGE_SHIFT + order);
		mpol_cond_put(pol);
		gfp |= __GFP_COMP;
		page = alloc_page_interleave(gfp, order, nid);
		if (page && order > 1)
			prep_transhuge_page(page);
		folio = (struct folio *)page;
		goto out;
	}

	if (pol->mode == MPOL_PREFERRED_MANY) {
		struct page *page;

		node = policy_node(gfp, pol, node);
		gfp |= __GFP_COMP;
		page = alloc_pages_preferred_many(gfp, order, node, pol);
		mpol_cond_put(pol);
		if (page && order > 1)
			prep_transhuge_page(page);
		folio = (struct folio *)page;
		goto out;
	}

	if (unlikely(IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) && hugepage)) {
		int hpage_node = node;

		/*
		 * For hugepage allocation and non-interleave policy which
		 * allows the current node (or other explicitly preferred
		 * node) we only try to allocate from the current/preferred
		 * node and don't fall back to other nodes, as the cost of
		 * remote accesses would likely offset THP benefits.
		 *
		 * If the policy is interleave or does not allow the current
		 * node in its nodemask, we allocate the standard way.
		 */
		if (pol->mode == MPOL_PREFERRED)
			hpage_node = first_node(pol->nodes);

		nmask = policy_nodemask(gfp, pol);
		if (!nmask || node_isset(hpage_node, *nmask)) {
			mpol_cond_put(pol);
			/*
			 * First, try to allocate THP only on local node, but
			 * don't reclaim unnecessarily, just compact.
			 */
			folio = __folio_alloc_node(gfp | __GFP_THISNODE |
					__GFP_NORETRY, order, hpage_node);

			/*
			 * If hugepage allocations are configured to always
			 * synchronous compact or the vma has been madvised
			 * to prefer hugepage backing, retry allowing remote
			 * memory with both reclaim and compact as well.
			 */
			if (!folio && (gfp & __GFP_DIRECT_RECLAIM))
				folio = __folio_alloc(gfp, order, hpage_node,
						      nmask);

			goto out;
		}
	}

	nmask = policy_nodemask(gfp, pol);
	preferred_nid = policy_node(gfp, pol, node);
	folio = __folio_alloc(gfp, order, preferred_nid, nmask);
	mpol_cond_put(pol);
out:
	return folio;
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
 * process is honoured when in process context.
 *
 * Context: Can be called from any context, providing the appropriate GFP
 * flags are used.
 * Return: The page on success or NULL if allocation fails.
 */
struct page *alloc_pages(gfp_t gfp, unsigned order)
{
	struct mempolicy *pol = &default_policy;
	struct page *page;

	if (!in_interrupt() && !(gfp & __GFP_THISNODE))
		pol = get_task_policy(current);

	/*
	 * No reference counting needed for current->mempolicy
	 * nor system default_policy
	 */
	if (pol->mode == MPOL_INTERLEAVE)
		page = alloc_page_interleave(gfp, order, interleave_nodes(pol));
	else if (pol->mode == MPOL_PREFERRED_MANY)
		page = alloc_pages_preferred_many(gfp, order,
				  policy_node(gfp, pol, numa_node_id()), pol);
	else
		page = __alloc_pages(gfp, order,
				policy_node(gfp, pol, numa_node_id()),
				policy_nodemask(gfp, pol));

	return page;
}
EXPORT_SYMBOL(alloc_pages);

struct folio *folio_alloc(gfp_t gfp, unsigned order)
{
	struct page *page = alloc_pages(gfp | __GFP_COMP, order);

	if (page && order > 1)
		prep_transhuge_page(page);
	return (struct folio *)page;
}
EXPORT_SYMBOL(folio_alloc);

static unsigned long alloc_pages_bulk_array_interleave(gfp_t gfp,
		struct mempolicy *pol, unsigned long nr_pages,
		struct page **page_array)
{
	int nodes;
	unsigned long nr_pages_per_node;
	int delta;
	int i;
	unsigned long nr_allocated;
	unsigned long total_allocated = 0;

	nodes = nodes_weight(pol->nodes);
	nr_pages_per_node = nr_pages / nodes;
	delta = nr_pages - nodes * nr_pages_per_node;

	for (i = 0; i < nodes; i++) {
		if (delta) {
			nr_allocated = __alloc_pages_bulk(gfp,
					interleave_nodes(pol), NULL,
					nr_pages_per_node + 1, NULL,
					page_array);
			delta--;
		} else {
			nr_allocated = __alloc_pages_bulk(gfp,
					interleave_nodes(pol), NULL,
					nr_pages_per_node, NULL, page_array);
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

	preferred_gfp = gfp | __GFP_NOWARN;
	preferred_gfp &= ~(__GFP_DIRECT_RECLAIM | __GFP_NOFAIL);

	nr_allocated  = __alloc_pages_bulk(preferred_gfp, nid, &pol->nodes,
					   nr_pages, NULL, page_array);

	if (nr_allocated < nr_pages)
		nr_allocated += __alloc_pages_bulk(gfp, numa_node_id(), NULL,
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

	if (!in_interrupt() && !(gfp & __GFP_THISNODE))
		pol = get_task_policy(current);

	if (pol->mode == MPOL_INTERLEAVE)
		return alloc_pages_bulk_array_interleave(gfp, pol,
							 nr_pages, page_array);

	if (pol->mode == MPOL_PREFERRED_MANY)
		return alloc_pages_bulk_array_preferred_many(gfp,
				numa_node_id(), pol, nr_pages, page_array);

	return __alloc_pages_bulk(gfp, policy_node(gfp, pol, numa_node_id()),
				  policy_nodemask(gfp, pol), nr_pages, NULL,
				  page_array);
}

int vma_dup_policy(struct vm_area_struct *src, struct vm_area_struct *dst)
{
	struct mempolicy *pol = mpol_dup(vma_policy(src));

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
 * further kernel/cpuset.c update_nodemask().
 *
 * current's mempolicy may be rebinded by the other task(the task that changes
 * cpuset's mems), so we needn't do rebind work for current task.
 */

/* Slow path of a mempolicy duplicate */
struct mempolicy *__mpol_dup(struct mempolicy *old)
{
	struct mempolicy *new = kmem_cache_alloc(policy_cache, GFP_KERNEL);

	if (!new)
		return ERR_PTR(-ENOMEM);

	/* task's mempolicy is protected by alloc_lock */
	if (old == current->mempolicy) {
		task_lock(current);
		*new = *old;
		task_unlock(current);
	} else
		*new = *old;

	if (current_cpuset_is_being_rebound()) {
		nodemask_t mems = cpuset_mems_allowed(current);
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
	if (a->home_node != b->home_node)
		return false;
	if (mpol_store_user_nodemask(a))
		if (!nodes_equal(a->w.user_nodemask, b->w.user_nodemask))
			return false;

	switch (a->mode) {
	case MPOL_BIND:
	case MPOL_INTERLEAVE:
	case MPOL_PREFERRED:
	case MPOL_PREFERRED_MANY:
		return !!nodes_equal(a->nodes, b->nodes);
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
 * Remember policies even when nobody has shared memory mapped.
 * The policies are kept in Red-Black tree linked from the inode.
 * They are protected by the sp->lock rwlock, which should be held
 * for any accesses to the tree.
 */

/*
 * lookup first element intersecting start-end.  Caller holds sp->lock for
 * reading or for writing
 */
static struct sp_node *
sp_lookup(struct shared_policy *sp, unsigned long start, unsigned long end)
{
	struct rb_node *n = sp->root.rb_node;

	while (n) {
		struct sp_node *p = rb_entry(n, struct sp_node, nd);

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
		struct sp_node *w = NULL;
		struct rb_node *prev = rb_prev(n);
		if (!prev)
			break;
		w = rb_entry(prev, struct sp_node, nd);
		if (w->end <= start)
			break;
		n = prev;
	}
	return rb_entry(n, struct sp_node, nd);
}

/*
 * Insert a new shared policy into the list.  Caller holds sp->lock for
 * writing.
 */
static void sp_insert(struct shared_policy *sp, struct sp_node *new)
{
	struct rb_node **p = &sp->root.rb_node;
	struct rb_node *parent = NULL;
	struct sp_node *nd;

	while (*p) {
		parent = *p;
		nd = rb_entry(parent, struct sp_node, nd);
		if (new->start < nd->start)
			p = &(*p)->rb_left;
		else if (new->end > nd->end)
			p = &(*p)->rb_right;
		else
			BUG();
	}
	rb_link_node(&new->nd, parent, p);
	rb_insert_color(&new->nd, &sp->root);
	pr_debug("inserting %lx-%lx: %d\n", new->start, new->end,
		 new->policy ? new->policy->mode : 0);
}

/* Find shared policy intersecting idx */
struct mempolicy *
mpol_shared_policy_lookup(struct shared_policy *sp, unsigned long idx)
{
	struct mempolicy *pol = NULL;
	struct sp_node *sn;

	if (!sp->root.rb_node)
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

static void sp_free(struct sp_node *n)
{
	mpol_put(n->policy);
	kmem_cache_free(sn_cache, n);
}

/**
 * mpol_misplaced - check whether current page node is valid in policy
 *
 * @page: page to be checked
 * @vma: vm area where page mapped
 * @addr: virtual address where page mapped
 *
 * Lookup current policy node id for vma,addr and "compare to" page's
 * node id.  Policy determination "mimics" alloc_page_vma().
 * Called from fault path where we know the vma and faulting address.
 *
 * Return: NUMA_NO_NODE if the page is in a node that is valid for this
 * policy, or a suitable node ID to allocate a replacement page from.
 */
int mpol_misplaced(struct page *page, struct vm_area_struct *vma, unsigned long addr)
{
	struct mempolicy *pol;
	struct zoneref *z;
	int curnid = page_to_nid(page);
	unsigned long pgoff;
	int thiscpu = raw_smp_processor_id();
	int thisnid = cpu_to_node(thiscpu);
	int polnid = NUMA_NO_NODE;
	int ret = NUMA_NO_NODE;

	pol = get_vma_policy(vma, addr);
	if (!(pol->flags & MPOL_F_MOF))
		goto out;

	switch (pol->mode) {
	case MPOL_INTERLEAVE:
		pgoff = vma->vm_pgoff;
		pgoff += (addr - vma->vm_start) >> PAGE_SHIFT;
		polnid = offset_il_node(pol, pgoff);
		break;

	case MPOL_PREFERRED:
		if (node_isset(curnid, pol->nodes))
			goto out;
		polnid = first_node(pol->nodes);
		break;

	case MPOL_LOCAL:
		polnid = numa_node_id();
		break;

	case MPOL_BIND:
		/* Optimize placement among multiple nodes via NUMA balancing */
		if (pol->flags & MPOL_F_MORON) {
			if (node_isset(thisnid, pol->nodes))
				break;
			goto out;
		}
		fallthrough;

	case MPOL_PREFERRED_MANY:
		/*
		 * use current page if in policy nodemask,
		 * else select nearest allowed node, if any.
		 * If no allowed nodes, use current [!misplaced].
		 */
		if (node_isset(curnid, pol->nodes))
			goto out;
		z = first_zones_zonelist(
				node_zonelist(numa_node_id(), GFP_HIGHUSER),
				gfp_zone(GFP_HIGHUSER),
				&pol->nodes);
		polnid = zone_to_nid(z->zone);
		break;

	default:
		BUG();
	}

	/* Migrate the page towards the node whose CPU is referencing it */
	if (pol->flags & MPOL_F_MORON) {
		polnid = thisnid;

		if (!should_numa_migrate_memory(current, page, curnid, thiscpu))
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

static void sp_delete(struct shared_policy *sp, struct sp_node *n)
{
	pr_debug("deleting %lx-l%lx\n", n->start, n->end);
	rb_erase(&n->nd, &sp->root);
	sp_free(n);
}

static void sp_node_init(struct sp_node *node, unsigned long start,
			unsigned long end, struct mempolicy *pol)
{
	node->start = start;
	node->end = end;
	node->policy = pol;
}

static struct sp_node *sp_alloc(unsigned long start, unsigned long end,
				struct mempolicy *pol)
{
	struct sp_node *n;
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
	sp_node_init(n, start, end, newpol);

	return n;
}

/* Replace a policy range. */
static int shared_policy_replace(struct shared_policy *sp, unsigned long start,
				 unsigned long end, struct sp_node *new)
{
	struct sp_node *n;
	struct sp_node *n_new = NULL;
	struct mempolicy *mpol_new = NULL;
	int ret = 0;

restart:
	write_lock(&sp->lock);
	n = sp_lookup(sp, start, end);
	/* Take care of old policies in the same range. */
	while (n && n->start < end) {
		struct rb_node *next = rb_next(&n->nd);
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
				sp_node_init(n_new, end, n->end, mpol_new);
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
		n = rb_entry(next, struct sp_node, nd);
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
	ret = -ENOMEM;
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
 * mpol_shared_policy_init - initialize shared policy for inode
 * @sp: pointer to inode shared policy
 * @mpol:  struct mempolicy to install
 *
 * Install non-NULL @mpol in inode's shared policy rb-tree.
 * On entry, the current task has a reference on a non-NULL @mpol.
 * This must be released on exit.
 * This is called at get_inode() calls and we can use GFP_KERNEL.
 */
void mpol_shared_policy_init(struct shared_policy *sp, struct mempolicy *mpol)
{
	int ret;

	sp->root = RB_ROOT;		/* empty tree == default mempolicy */
	rwlock_init(&sp->lock);

	if (mpol) {
		struct vm_area_struct pvma;
		struct mempolicy *new;
		NODEMASK_SCRATCH(scratch);

		if (!scratch)
			goto put_mpol;
		/* contextualize the tmpfs mount point mempolicy */
		new = mpol_new(mpol->mode, mpol->flags, &mpol->w.user_nodemask);
		if (IS_ERR(new))
			goto free_scratch; /* no valid nodemask intersection */

		task_lock(current);
		ret = mpol_set_nodemask(new, &mpol->w.user_nodemask, scratch);
		task_unlock(current);
		if (ret)
			goto put_new;

		/* Create pseudo-vma that contains just the policy */
		vma_init(&pvma, NULL);
		pvma.vm_end = TASK_SIZE;	/* policy covers entire file */
		mpol_set_shared_policy(sp, &pvma, new); /* adds ref */

put_new:
		mpol_put(new);			/* drop initial ref */
free_scratch:
		NODEMASK_SCRATCH_FREE(scratch);
put_mpol:
		mpol_put(mpol);	/* drop our incoming ref on sb mpol */
	}
}

int mpol_set_shared_policy(struct shared_policy *info,
			struct vm_area_struct *vma, struct mempolicy *npol)
{
	int err;
	struct sp_node *new = NULL;
	unsigned long sz = vma_pages(vma);

	pr_debug("set_shared_policy %lx sz %lu %d %d %lx\n",
		 vma->vm_pgoff,
		 sz, npol ? npol->mode : -1,
		 npol ? npol->flags : -1,
		 npol ? nodes_addr(npol->nodes)[0] : NUMA_NO_NODE);

	if (npol) {
		new = sp_alloc(vma->vm_pgoff, vma->vm_pgoff + sz, npol);
		if (!new)
			return -ENOMEM;
	}
	err = shared_policy_replace(info, vma->vm_pgoff, vma->vm_pgoff+sz, new);
	if (err && new)
		sp_free(new);
	return err;
}

/* Free a backing policy store on inode delete. */
void mpol_free_shared_policy(struct shared_policy *p)
{
	struct sp_node *n;
	struct rb_node *next;

	if (!p->root.rb_node)
		return;
	write_lock(&p->lock);
	next = rb_first(&p->root);
	while (next) {
		n = rb_entry(next, struct sp_node, nd);
		next = rb_next(&n->nd);
		sp_delete(p, n);
	}
	write_unlock(&p->lock);
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

	if (num_online_nodes() > 1 && !numabalancing_override) {
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

/* assumes fs == KERNEL_DS */
void __init numa_policy_init(void)
{
	nodemask_t interleave_nodes;
	unsigned long largest = 0;
	int nid, prefer = 0;

	policy_cache = kmem_cache_create("numa_policy",
					 sizeof(struct mempolicy),
					 0, SLAB_PANIC, NULL);

	sn_cache = kmem_cache_create("shared_policy_node",
				     sizeof(struct sp_node),
				     0, SLAB_PANIC, NULL);

	for_each_node(nid) {
		preferred_node_policy[nid] = (struct mempolicy) {
			.refcnt = ATOMIC_INIT(1),
			.mode = MPOL_PREFERRED,
			.flags = MPOL_F_MOF | MPOL_F_MORON,
			.nodes = nodemask_of_node(nid),
		};
	}

	/*
	 * Set interleaving policy for system init. Interleaving is only
	 * enabled across suitably sized nodes (default is >= 16MB), or
	 * fall back to the largest node if they're all smaller.
	 */
	nodes_clear(interleave_nodes);
	for_each_node_state(nid, N_MEMORY) {
		unsigned long total_pages = node_present_pages(nid);

		/* Preserve the largest node */
		if (largest < total_pages) {
			largest = total_pages;
			prefer = nid;
		}

		/* Interleave this node? */
		if ((total_pages << PAGE_SHIFT) >= (16 << 20))
			node_set(nid, interleave_nodes);
	}

	/* All too small, use the largest */
	if (unlikely(nodes_empty(interleave_nodes)))
		node_set(prefer, interleave_nodes);

	if (do_set_mempolicy(MPOL_INTERLEAVE, 0, &interleave_nodes))
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
 *	<mode>[=<flags>][:<nodelist>]
 *
 * Return: %0 on success, else %1
 */
int mpol_parse_str(char *str, struct mempolicy **mpol)
{
	struct mempolicy *new = NULL;
	unsigned short mode_flags;
	nodemask_t nodes;
	char *nodelist = strchr(str, ':');
	char *flags = strchr(str, '=');
	int err = 1, mode;

	if (flags)
		*flags++ = '\0';	/* terminate mode string */

	if (nodelist) {
		/* NUL-terminate mode or flags string */
		*nodelist++ = '\0';
		if (nodelist_parse(nodelist, nodes))
			goto out;
		if (!nodes_subset(nodes, node_states[N_MEMORY]))
			goto out;
	} else
		nodes_clear(nodes);

	mode = match_string(policy_modes, MPOL_MAX, str);
	if (mode < 0)
		goto out;

	switch (mode) {
	case MPOL_PREFERRED:
		/*
		 * Insist on a nodelist of one node only, although later
		 * we use first_node(nodes) to grab a single node, so here
		 * nodelist (or nodes) cannot be empty.
		 */
		if (nodelist) {
			char *rest = nodelist;
			while (isdigit(*rest))
				rest++;
			if (*rest)
				goto out;
			if (nodes_empty(nodes))
				goto out;
		}
		break;
	case MPOL_INTERLEAVE:
		/*
		 * Default to online nodes with memory if no nodelist
		 */
		if (!nodelist)
			nodes = node_states[N_MEMORY];
		break;
	case MPOL_LOCAL:
		/*
		 * Don't allow a nodelist;  mpol_new() checks flags
		 */
		if (nodelist)
			goto out;
		break;
	case MPOL_DEFAULT:
		/*
		 * Insist on a empty nodelist
		 */
		if (!nodelist)
			err = 0;
		goto out;
	case MPOL_PREFERRED_MANY:
	case MPOL_BIND:
		/*
		 * Insist on a nodelist
		 */
		if (!nodelist)
			goto out;
	}

	mode_flags = 0;
	if (flags) {
		/*
		 * Currently, we only support two mutually exclusive
		 * mode flags.
		 */
		if (!strcmp(flags, "static"))
			mode_flags |= MPOL_F_STATIC_NODES;
		else if (!strcmp(flags, "relative"))
			mode_flags |= MPOL_F_RELATIVE_NODES;
		else
			goto out;
	}

	new = mpol_new(mode, mode_flags, &nodes);
	if (IS_ERR(new))
		goto out;

	/*
	 * Save nodes for mpol_to_str() to show the tmpfs mount options
	 * for /proc/mounts, /proc/pid/mounts and /proc/pid/mountinfo.
	 */
	if (mode != MPOL_PREFERRED) {
		new->nodes = nodes;
	} else if (nodelist) {
		nodes_clear(new->nodes);
		node_set(first_node(nodes), new->nodes);
	} else {
		new->mode = MPOL_LOCAL;
	}

	/*
	 * Save nodes for contextualization: this will be used to "clone"
	 * the mempolicy in a specific context [cpuset] at a later time.
	 */
	new->w.user_nodemask = nodes;

	err = 0;

out:
	/* Restore string for error message */
	if (nodelist)
		*--nodelist = ':';
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
 * longest flag, "relative", and to display at least a few node ids.
 */
void mpol_to_str(char *buffer, int maxlen, struct mempolicy *pol)
{
	char *p = buffer;
	nodemask_t nodes = NODE_MASK_NONE;
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
		nodes = pol->nodes;
		break;
	default:
		WARN_ON_ONCE(1);
		snprintf(p, maxlen, "unknown");
		return;
	}

	p += snprintf(p, maxlen, "%s", policy_modes[mode]);

	if (flags & MPOL_MODE_FLAGS) {
		p += snprintf(p, buffer + maxlen - p, "=");

		/*
		 * Currently, the only defined flags are mutually exclusive
		 */
		if (flags & MPOL_F_STATIC_NODES)
			p += snprintf(p, buffer + maxlen - p, "static");
		else if (flags & MPOL_F_RELATIVE_NODES)
			p += snprintf(p, buffer + maxlen - p, "relative");
	}

	if (!nodes_empty(nodes))
		p += scnprintf(p, buffer + maxlen - p, ":%*pbl",
			       nodemask_pr_args(&nodes));
}
