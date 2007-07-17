/*
 * Simple NUMA memory policy for the Linux kernel.
 *
 * Copyright 2003,2004 Andi Kleen, SuSE Labs.
 * (C) Copyright 2005 Christoph Lameter, Silicon Graphics, Inc.
 * Subject to the GNU Public License, version 2.
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
 *                As a special case node -1 here means do the allocation
 *                on the local CPU. This is normally identical to default,
 *                but useful to set in a VMA when you have a non default
 *                process policy.
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
   could replace all the switch()es with a mempolicy_ops structure.
*/

#include <linux/mempolicy.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/nodemask.h>
#include <linux/cpuset.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/compat.h>
#include <linux/mempolicy.h>
#include <linux/swap.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/migrate.h>
#include <linux/rmap.h>
#include <linux/security.h>

#include <asm/tlbflush.h>
#include <asm/uaccess.h>

/* Internal flags */
#define MPOL_MF_DISCONTIG_OK (MPOL_MF_INTERNAL << 0)	/* Skip checks for continuous vmas */
#define MPOL_MF_INVERT (MPOL_MF_INTERNAL << 1)		/* Invert check for nodemask */
#define MPOL_MF_STATS (MPOL_MF_INTERNAL << 2)		/* Gather statistics */

static struct kmem_cache *policy_cache;
static struct kmem_cache *sn_cache;

/* Highest zone. An specific allocation for a zone below that is not
   policied. */
enum zone_type policy_zone = 0;

struct mempolicy default_policy = {
	.refcnt = ATOMIC_INIT(1), /* never free it */
	.policy = MPOL_DEFAULT,
};

/* Do sanity checking on a policy */
static int mpol_check_policy(int mode, nodemask_t *nodes)
{
	int empty = nodes_empty(*nodes);

	switch (mode) {
	case MPOL_DEFAULT:
		if (!empty)
			return -EINVAL;
		break;
	case MPOL_BIND:
	case MPOL_INTERLEAVE:
		/* Preferred will only use the first bit, but allow
		   more for now. */
		if (empty)
			return -EINVAL;
		break;
	}
	return nodes_subset(*nodes, node_online_map) ? 0 : -EINVAL;
}

/* Generate a custom zonelist for the BIND policy. */
static struct zonelist *bind_zonelist(nodemask_t *nodes)
{
	struct zonelist *zl;
	int num, max, nd;
	enum zone_type k;

	max = 1 + MAX_NR_ZONES * nodes_weight(*nodes);
	max++;			/* space for zlcache_ptr (see mmzone.h) */
	zl = kmalloc(sizeof(struct zone *) * max, GFP_KERNEL);
	if (!zl)
		return ERR_PTR(-ENOMEM);
	zl->zlcache_ptr = NULL;
	num = 0;
	/* First put in the highest zones from all nodes, then all the next 
	   lower zones etc. Avoid empty zones because the memory allocator
	   doesn't like them. If you implement node hot removal you
	   have to fix that. */
	k = policy_zone;
	while (1) {
		for_each_node_mask(nd, *nodes) { 
			struct zone *z = &NODE_DATA(nd)->node_zones[k];
			if (z->present_pages > 0) 
				zl->zones[num++] = z;
		}
		if (k == 0)
			break;
		k--;
	}
	if (num == 0) {
		kfree(zl);
		return ERR_PTR(-EINVAL);
	}
	zl->zones[num] = NULL;
	return zl;
}

/* Create a new policy */
static struct mempolicy *mpol_new(int mode, nodemask_t *nodes)
{
	struct mempolicy *policy;

	pr_debug("setting mode %d nodes[0] %lx\n",
		 mode, nodes ? nodes_addr(*nodes)[0] : -1);

	if (mode == MPOL_DEFAULT)
		return NULL;
	policy = kmem_cache_alloc(policy_cache, GFP_KERNEL);
	if (!policy)
		return ERR_PTR(-ENOMEM);
	atomic_set(&policy->refcnt, 1);
	switch (mode) {
	case MPOL_INTERLEAVE:
		policy->v.nodes = *nodes;
		if (nodes_weight(*nodes) == 0) {
			kmem_cache_free(policy_cache, policy);
			return ERR_PTR(-EINVAL);
		}
		break;
	case MPOL_PREFERRED:
		policy->v.preferred_node = first_node(*nodes);
		if (policy->v.preferred_node >= MAX_NUMNODES)
			policy->v.preferred_node = -1;
		break;
	case MPOL_BIND:
		policy->v.zonelist = bind_zonelist(nodes);
		if (IS_ERR(policy->v.zonelist)) {
			void *error_code = policy->v.zonelist;
			kmem_cache_free(policy_cache, policy);
			return error_code;
		}
		break;
	}
	policy->policy = mode;
	policy->cpuset_mems_allowed = cpuset_mems_allowed(current);
	return policy;
}

static void gather_stats(struct page *, void *, int pte_dirty);
static void migrate_page_add(struct page *page, struct list_head *pagelist,
				unsigned long flags);

/* Scan through pages checking if pages follow certain conditions. */
static int check_pte_range(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long addr, unsigned long end,
		const nodemask_t *nodes, unsigned long flags,
		void *private)
{
	pte_t *orig_pte;
	pte_t *pte;
	spinlock_t *ptl;

	orig_pte = pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	do {
		struct page *page;
		int nid;

		if (!pte_present(*pte))
			continue;
		page = vm_normal_page(vma, addr, *pte);
		if (!page)
			continue;
		/*
		 * The check for PageReserved here is important to avoid
		 * handling zero pages and other pages that may have been
		 * marked special by the system.
		 *
		 * If the PageReserved would not be checked here then f.e.
		 * the location of the zero page could have an influence
		 * on MPOL_MF_STRICT, zero pages would be counted for
		 * the per node stats, and there would be useless attempts
		 * to put zero pages on the migration list.
		 */
		if (PageReserved(page))
			continue;
		nid = page_to_nid(page);
		if (node_isset(nid, *nodes) == !!(flags & MPOL_MF_INVERT))
			continue;

		if (flags & MPOL_MF_STATS)
			gather_stats(page, private, pte_dirty(*pte));
		else if (flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL))
			migrate_page_add(page, private, flags);
		else
			break;
	} while (pte++, addr += PAGE_SIZE, addr != end);
	pte_unmap_unlock(orig_pte, ptl);
	return addr != end;
}

static inline int check_pmd_range(struct vm_area_struct *vma, pud_t *pud,
		unsigned long addr, unsigned long end,
		const nodemask_t *nodes, unsigned long flags,
		void *private)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		if (check_pte_range(vma, pmd, addr, next, nodes,
				    flags, private))
			return -EIO;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static inline int check_pud_range(struct vm_area_struct *vma, pgd_t *pgd,
		unsigned long addr, unsigned long end,
		const nodemask_t *nodes, unsigned long flags,
		void *private)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		if (check_pmd_range(vma, pud, addr, next, nodes,
				    flags, private))
			return -EIO;
	} while (pud++, addr = next, addr != end);
	return 0;
}

static inline int check_pgd_range(struct vm_area_struct *vma,
		unsigned long addr, unsigned long end,
		const nodemask_t *nodes, unsigned long flags,
		void *private)
{
	pgd_t *pgd;
	unsigned long next;

	pgd = pgd_offset(vma->vm_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		if (check_pud_range(vma, pgd, addr, next, nodes,
				    flags, private))
			return -EIO;
	} while (pgd++, addr = next, addr != end);
	return 0;
}

/*
 * Check if all pages in a range are on a set of nodes.
 * If pagelist != NULL then isolate pages from the LRU and
 * put them on the pagelist.
 */
static struct vm_area_struct *
check_range(struct mm_struct *mm, unsigned long start, unsigned long end,
		const nodemask_t *nodes, unsigned long flags, void *private)
{
	int err;
	struct vm_area_struct *first, *vma, *prev;

	if (flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)) {

		err = migrate_prep();
		if (err)
			return ERR_PTR(err);
	}

	first = find_vma(mm, start);
	if (!first)
		return ERR_PTR(-EFAULT);
	prev = NULL;
	for (vma = first; vma && vma->vm_start < end; vma = vma->vm_next) {
		if (!(flags & MPOL_MF_DISCONTIG_OK)) {
			if (!vma->vm_next && vma->vm_end < end)
				return ERR_PTR(-EFAULT);
			if (prev && prev->vm_end < vma->vm_start)
				return ERR_PTR(-EFAULT);
		}
		if (!is_vm_hugetlb_page(vma) &&
		    ((flags & MPOL_MF_STRICT) ||
		     ((flags & (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)) &&
				vma_migratable(vma)))) {
			unsigned long endvma = vma->vm_end;

			if (endvma > end)
				endvma = end;
			if (vma->vm_start > start)
				start = vma->vm_start;
			err = check_pgd_range(vma, start, endvma, nodes,
						flags, private);
			if (err) {
				first = ERR_PTR(err);
				break;
			}
		}
		prev = vma;
	}
	return first;
}

/* Apply policy to a single VMA */
static int policy_vma(struct vm_area_struct *vma, struct mempolicy *new)
{
	int err = 0;
	struct mempolicy *old = vma->vm_policy;

	pr_debug("vma %lx-%lx/%lx vm_ops %p vm_file %p set_policy %p\n",
		 vma->vm_start, vma->vm_end, vma->vm_pgoff,
		 vma->vm_ops, vma->vm_file,
		 vma->vm_ops ? vma->vm_ops->set_policy : NULL);

	if (vma->vm_ops && vma->vm_ops->set_policy)
		err = vma->vm_ops->set_policy(vma, new);
	if (!err) {
		mpol_get(new);
		vma->vm_policy = new;
		mpol_free(old);
	}
	return err;
}

/* Step 2: apply policy to a range and do splits. */
static int mbind_range(struct vm_area_struct *vma, unsigned long start,
		       unsigned long end, struct mempolicy *new)
{
	struct vm_area_struct *next;
	int err;

	err = 0;
	for (; vma && vma->vm_start < end; vma = next) {
		next = vma->vm_next;
		if (vma->vm_start < start)
			err = split_vma(vma->vm_mm, vma, start, 1);
		if (!err && vma->vm_end > end)
			err = split_vma(vma->vm_mm, vma, end, 0);
		if (!err)
			err = policy_vma(vma, new);
		if (err)
			break;
	}
	return err;
}

static int contextualize_policy(int mode, nodemask_t *nodes)
{
	if (!nodes)
		return 0;

	cpuset_update_task_memory_state();
	if (!cpuset_nodes_subset_current_mems_allowed(*nodes))
		return -EINVAL;
	return mpol_check_policy(mode, nodes);
}


/*
 * Update task->flags PF_MEMPOLICY bit: set iff non-default
 * mempolicy.  Allows more rapid checking of this (combined perhaps
 * with other PF_* flag bits) on memory allocation hot code paths.
 *
 * If called from outside this file, the task 'p' should -only- be
 * a newly forked child not yet visible on the task list, because
 * manipulating the task flags of a visible task is not safe.
 *
 * The above limitation is why this routine has the funny name
 * mpol_fix_fork_child_flag().
 *
 * It is also safe to call this with a task pointer of current,
 * which the static wrapper mpol_set_task_struct_flag() does,
 * for use within this file.
 */

void mpol_fix_fork_child_flag(struct task_struct *p)
{
	if (p->mempolicy)
		p->flags |= PF_MEMPOLICY;
	else
		p->flags &= ~PF_MEMPOLICY;
}

static void mpol_set_task_struct_flag(void)
{
	mpol_fix_fork_child_flag(current);
}

/* Set the process memory policy */
long do_set_mempolicy(int mode, nodemask_t *nodes)
{
	struct mempolicy *new;

	if (contextualize_policy(mode, nodes))
		return -EINVAL;
	new = mpol_new(mode, nodes);
	if (IS_ERR(new))
		return PTR_ERR(new);
	mpol_free(current->mempolicy);
	current->mempolicy = new;
	mpol_set_task_struct_flag();
	if (new && new->policy == MPOL_INTERLEAVE)
		current->il_next = first_node(new->v.nodes);
	return 0;
}

/* Fill a zone bitmap for a policy */
static void get_zonemask(struct mempolicy *p, nodemask_t *nodes)
{
	int i;

	nodes_clear(*nodes);
	switch (p->policy) {
	case MPOL_BIND:
		for (i = 0; p->v.zonelist->zones[i]; i++)
			node_set(zone_to_nid(p->v.zonelist->zones[i]),
				*nodes);
		break;
	case MPOL_DEFAULT:
		break;
	case MPOL_INTERLEAVE:
		*nodes = p->v.nodes;
		break;
	case MPOL_PREFERRED:
		/* or use current node instead of online map? */
		if (p->v.preferred_node < 0)
			*nodes = node_online_map;
		else
			node_set(p->v.preferred_node, *nodes);
		break;
	default:
		BUG();
	}
}

static int lookup_node(struct mm_struct *mm, unsigned long addr)
{
	struct page *p;
	int err;

	err = get_user_pages(current, mm, addr & PAGE_MASK, 1, 0, 0, &p, NULL);
	if (err >= 0) {
		err = page_to_nid(p);
		put_page(p);
	}
	return err;
}

/* Retrieve NUMA policy */
long do_get_mempolicy(int *policy, nodemask_t *nmask,
			unsigned long addr, unsigned long flags)
{
	int err;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = NULL;
	struct mempolicy *pol = current->mempolicy;

	cpuset_update_task_memory_state();
	if (flags & ~(unsigned long)(MPOL_F_NODE|MPOL_F_ADDR))
		return -EINVAL;
	if (flags & MPOL_F_ADDR) {
		down_read(&mm->mmap_sem);
		vma = find_vma_intersection(mm, addr, addr+1);
		if (!vma) {
			up_read(&mm->mmap_sem);
			return -EFAULT;
		}
		if (vma->vm_ops && vma->vm_ops->get_policy)
			pol = vma->vm_ops->get_policy(vma, addr);
		else
			pol = vma->vm_policy;
	} else if (addr)
		return -EINVAL;

	if (!pol)
		pol = &default_policy;

	if (flags & MPOL_F_NODE) {
		if (flags & MPOL_F_ADDR) {
			err = lookup_node(mm, addr);
			if (err < 0)
				goto out;
			*policy = err;
		} else if (pol == current->mempolicy &&
				pol->policy == MPOL_INTERLEAVE) {
			*policy = current->il_next;
		} else {
			err = -EINVAL;
			goto out;
		}
	} else
		*policy = pol->policy;

	if (vma) {
		up_read(&current->mm->mmap_sem);
		vma = NULL;
	}

	err = 0;
	if (nmask)
		get_zonemask(pol, nmask);

 out:
	if (vma)
		up_read(&current->mm->mmap_sem);
	return err;
}

#ifdef CONFIG_MIGRATION
/*
 * page migration
 */
static void migrate_page_add(struct page *page, struct list_head *pagelist,
				unsigned long flags)
{
	/*
	 * Avoid migrating a page that is shared with others.
	 */
	if ((flags & MPOL_MF_MOVE_ALL) || page_mapcount(page) == 1)
		isolate_lru_page(page, pagelist);
}

static struct page *new_node_page(struct page *page, unsigned long node, int **x)
{
	return alloc_pages_node(node, GFP_HIGHUSER_MOVABLE, 0);
}

/*
 * Migrate pages from one node to a target node.
 * Returns error or the number of pages not migrated.
 */
int migrate_to_node(struct mm_struct *mm, int source, int dest, int flags)
{
	nodemask_t nmask;
	LIST_HEAD(pagelist);
	int err = 0;

	nodes_clear(nmask);
	node_set(source, nmask);

	check_range(mm, mm->mmap->vm_start, TASK_SIZE, &nmask,
			flags | MPOL_MF_DISCONTIG_OK, &pagelist);

	if (!list_empty(&pagelist))
		err = migrate_pages(&pagelist, new_node_page, dest);

	return err;
}

/*
 * Move pages between the two nodesets so as to preserve the physical
 * layout as much as possible.
 *
 * Returns the number of page that could not be moved.
 */
int do_migrate_pages(struct mm_struct *mm,
	const nodemask_t *from_nodes, const nodemask_t *to_nodes, int flags)
{
	LIST_HEAD(pagelist);
	int busy = 0;
	int err = 0;
	nodemask_t tmp;

  	down_read(&mm->mmap_sem);

	err = migrate_vmas(mm, from_nodes, to_nodes, flags);
	if (err)
		goto out;

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
 * Otherwise when we finish scannng from_tmp, we at least have the
 * most recent <s, d> pair that moved.  If we get all the way through
 * the scan of tmp without finding any node that moved, much less
 * moved to an empty node, then there is nothing left worth migrating.
 */

	tmp = *from_nodes;
	while (!nodes_empty(tmp)) {
		int s,d;
		int source = -1;
		int dest = 0;

		for_each_node_mask(s, tmp) {
			d = node_remap(s, *from_nodes, *to_nodes);
			if (s == d)
				continue;

			source = s;	/* Node moved. Memorize */
			dest = d;

			/* dest not in remaining from nodes? */
			if (!node_isset(dest, tmp))
				break;
		}
		if (source == -1)
			break;

		node_clear(source, tmp);
		err = migrate_to_node(mm, source, dest, flags);
		if (err > 0)
			busy += err;
		if (err < 0)
			break;
	}
out:
	up_read(&mm->mmap_sem);
	if (err < 0)
		return err;
	return busy;

}

static struct page *new_vma_page(struct page *page, unsigned long private, int **x)
{
	struct vm_area_struct *vma = (struct vm_area_struct *)private;

	return alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma,
					page_address_in_vma(page, vma));
}
#else

static void migrate_page_add(struct page *page, struct list_head *pagelist,
				unsigned long flags)
{
}

int do_migrate_pages(struct mm_struct *mm,
	const nodemask_t *from_nodes, const nodemask_t *to_nodes, int flags)
{
	return -ENOSYS;
}

static struct page *new_vma_page(struct page *page, unsigned long private, int **x)
{
	return NULL;
}
#endif

long do_mbind(unsigned long start, unsigned long len,
		unsigned long mode, nodemask_t *nmask, unsigned long flags)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	struct mempolicy *new;
	unsigned long end;
	int err;
	LIST_HEAD(pagelist);

	if ((flags & ~(unsigned long)(MPOL_MF_STRICT |
				      MPOL_MF_MOVE | MPOL_MF_MOVE_ALL))
	    || mode > MPOL_MAX)
		return -EINVAL;
	if ((flags & MPOL_MF_MOVE_ALL) && !capable(CAP_SYS_NICE))
		return -EPERM;

	if (start & ~PAGE_MASK)
		return -EINVAL;

	if (mode == MPOL_DEFAULT)
		flags &= ~MPOL_MF_STRICT;

	len = (len + PAGE_SIZE - 1) & PAGE_MASK;
	end = start + len;

	if (end < start)
		return -EINVAL;
	if (end == start)
		return 0;

	if (mpol_check_policy(mode, nmask))
		return -EINVAL;

	new = mpol_new(mode, nmask);
	if (IS_ERR(new))
		return PTR_ERR(new);

	/*
	 * If we are using the default policy then operation
	 * on discontinuous address spaces is okay after all
	 */
	if (!new)
		flags |= MPOL_MF_DISCONTIG_OK;

	pr_debug("mbind %lx-%lx mode:%ld nodes:%lx\n",start,start+len,
		 mode, nmask ? nodes_addr(*nmask)[0] : -1);

	down_write(&mm->mmap_sem);
	vma = check_range(mm, start, end, nmask,
			  flags | MPOL_MF_INVERT, &pagelist);

	err = PTR_ERR(vma);
	if (!IS_ERR(vma)) {
		int nr_failed = 0;

		err = mbind_range(vma, start, end, new);

		if (!list_empty(&pagelist))
			nr_failed = migrate_pages(&pagelist, new_vma_page,
						(unsigned long)vma);

		if (!err && nr_failed && (flags & MPOL_MF_STRICT))
			err = -EIO;
	}

	up_write(&mm->mmap_sem);
	mpol_free(new);
	return err;
}

/*
 * User space interface with variable sized bitmaps for nodelists.
 */

/* Copy a node mask from user space. */
static int get_nodes(nodemask_t *nodes, const unsigned long __user *nmask,
		     unsigned long maxnode)
{
	unsigned long k;
	unsigned long nlongs;
	unsigned long endmask;

	--maxnode;
	nodes_clear(*nodes);
	if (maxnode == 0 || !nmask)
		return 0;
	if (maxnode > PAGE_SIZE*BITS_PER_BYTE)
		return -EINVAL;

	nlongs = BITS_TO_LONGS(maxnode);
	if ((maxnode % BITS_PER_LONG) == 0)
		endmask = ~0UL;
	else
		endmask = (1UL << (maxnode % BITS_PER_LONG)) - 1;

	/* When the user specified more nodes than supported just check
	   if the non supported part is all zero. */
	if (nlongs > BITS_TO_LONGS(MAX_NUMNODES)) {
		if (nlongs > PAGE_SIZE/sizeof(long))
			return -EINVAL;
		for (k = BITS_TO_LONGS(MAX_NUMNODES); k < nlongs; k++) {
			unsigned long t;
			if (get_user(t, nmask + k))
				return -EFAULT;
			if (k == nlongs - 1) {
				if (t & endmask)
					return -EINVAL;
			} else if (t)
				return -EINVAL;
		}
		nlongs = BITS_TO_LONGS(MAX_NUMNODES);
		endmask = ~0UL;
	}

	if (copy_from_user(nodes_addr(*nodes), nmask, nlongs*sizeof(unsigned long)))
		return -EFAULT;
	nodes_addr(*nodes)[nlongs-1] &= endmask;
	return 0;
}

/* Copy a kernel node mask to user space */
static int copy_nodes_to_user(unsigned long __user *mask, unsigned long maxnode,
			      nodemask_t *nodes)
{
	unsigned long copy = ALIGN(maxnode-1, 64) / 8;
	const int nbytes = BITS_TO_LONGS(MAX_NUMNODES) * sizeof(long);

	if (copy > nbytes) {
		if (copy > PAGE_SIZE)
			return -EINVAL;
		if (clear_user((char __user *)mask + nbytes, copy - nbytes))
			return -EFAULT;
		copy = nbytes;
	}
	return copy_to_user(mask, nodes_addr(*nodes), copy) ? -EFAULT : 0;
}

asmlinkage long sys_mbind(unsigned long start, unsigned long len,
			unsigned long mode,
			unsigned long __user *nmask, unsigned long maxnode,
			unsigned flags)
{
	nodemask_t nodes;
	int err;

	err = get_nodes(&nodes, nmask, maxnode);
	if (err)
		return err;
#ifdef CONFIG_CPUSETS
	/* Restrict the nodes to the allowed nodes in the cpuset */
	nodes_and(nodes, nodes, current->mems_allowed);
#endif
	return do_mbind(start, len, mode, &nodes, flags);
}

/* Set the process memory policy */
asmlinkage long sys_set_mempolicy(int mode, unsigned long __user *nmask,
		unsigned long maxnode)
{
	int err;
	nodemask_t nodes;

	if (mode < 0 || mode > MPOL_MAX)
		return -EINVAL;
	err = get_nodes(&nodes, nmask, maxnode);
	if (err)
		return err;
	return do_set_mempolicy(mode, &nodes);
}

asmlinkage long sys_migrate_pages(pid_t pid, unsigned long maxnode,
		const unsigned long __user *old_nodes,
		const unsigned long __user *new_nodes)
{
	struct mm_struct *mm;
	struct task_struct *task;
	nodemask_t old;
	nodemask_t new;
	nodemask_t task_nodes;
	int err;

	err = get_nodes(&old, old_nodes, maxnode);
	if (err)
		return err;

	err = get_nodes(&new, new_nodes, maxnode);
	if (err)
		return err;

	/* Find the mm_struct */
	read_lock(&tasklist_lock);
	task = pid ? find_task_by_pid(pid) : current;
	if (!task) {
		read_unlock(&tasklist_lock);
		return -ESRCH;
	}
	mm = get_task_mm(task);
	read_unlock(&tasklist_lock);

	if (!mm)
		return -EINVAL;

	/*
	 * Check if this process has the right to modify the specified
	 * process. The right exists if the process has administrative
	 * capabilities, superuser privileges or the same
	 * userid as the target process.
	 */
	if ((current->euid != task->suid) && (current->euid != task->uid) &&
	    (current->uid != task->suid) && (current->uid != task->uid) &&
	    !capable(CAP_SYS_NICE)) {
		err = -EPERM;
		goto out;
	}

	task_nodes = cpuset_mems_allowed(task);
	/* Is the user allowed to access the target nodes? */
	if (!nodes_subset(new, task_nodes) && !capable(CAP_SYS_NICE)) {
		err = -EPERM;
		goto out;
	}

	err = security_task_movememory(task);
	if (err)
		goto out;

	err = do_migrate_pages(mm, &old, &new,
		capable(CAP_SYS_NICE) ? MPOL_MF_MOVE_ALL : MPOL_MF_MOVE);
out:
	mmput(mm);
	return err;
}


/* Retrieve NUMA policy */
asmlinkage long sys_get_mempolicy(int __user *policy,
				unsigned long __user *nmask,
				unsigned long maxnode,
				unsigned long addr, unsigned long flags)
{
	int err, pval;
	nodemask_t nodes;

	if (nmask != NULL && maxnode < MAX_NUMNODES)
		return -EINVAL;

	err = do_get_mempolicy(&pval, &nodes, addr, flags);

	if (err)
		return err;

	if (policy && put_user(pval, policy))
		return -EFAULT;

	if (nmask)
		err = copy_nodes_to_user(nmask, maxnode, &nodes);

	return err;
}

#ifdef CONFIG_COMPAT

asmlinkage long compat_sys_get_mempolicy(int __user *policy,
				     compat_ulong_t __user *nmask,
				     compat_ulong_t maxnode,
				     compat_ulong_t addr, compat_ulong_t flags)
{
	long err;
	unsigned long __user *nm = NULL;
	unsigned long nr_bits, alloc_size;
	DECLARE_BITMAP(bm, MAX_NUMNODES);

	nr_bits = min_t(unsigned long, maxnode-1, MAX_NUMNODES);
	alloc_size = ALIGN(nr_bits, BITS_PER_LONG) / 8;

	if (nmask)
		nm = compat_alloc_user_space(alloc_size);

	err = sys_get_mempolicy(policy, nm, nr_bits+1, addr, flags);

	if (!err && nmask) {
		err = copy_from_user(bm, nm, alloc_size);
		/* ensure entire bitmap is zeroed */
		err |= clear_user(nmask, ALIGN(maxnode-1, 8) / 8);
		err |= compat_put_bitmap(nmask, bm, nr_bits);
	}

	return err;
}

asmlinkage long compat_sys_set_mempolicy(int mode, compat_ulong_t __user *nmask,
				     compat_ulong_t maxnode)
{
	long err = 0;
	unsigned long __user *nm = NULL;
	unsigned long nr_bits, alloc_size;
	DECLARE_BITMAP(bm, MAX_NUMNODES);

	nr_bits = min_t(unsigned long, maxnode-1, MAX_NUMNODES);
	alloc_size = ALIGN(nr_bits, BITS_PER_LONG) / 8;

	if (nmask) {
		err = compat_get_bitmap(bm, nmask, nr_bits);
		nm = compat_alloc_user_space(alloc_size);
		err |= copy_to_user(nm, bm, alloc_size);
	}

	if (err)
		return -EFAULT;

	return sys_set_mempolicy(mode, nm, nr_bits+1);
}

asmlinkage long compat_sys_mbind(compat_ulong_t start, compat_ulong_t len,
			     compat_ulong_t mode, compat_ulong_t __user *nmask,
			     compat_ulong_t maxnode, compat_ulong_t flags)
{
	long err = 0;
	unsigned long __user *nm = NULL;
	unsigned long nr_bits, alloc_size;
	nodemask_t bm;

	nr_bits = min_t(unsigned long, maxnode-1, MAX_NUMNODES);
	alloc_size = ALIGN(nr_bits, BITS_PER_LONG) / 8;

	if (nmask) {
		err = compat_get_bitmap(nodes_addr(bm), nmask, nr_bits);
		nm = compat_alloc_user_space(alloc_size);
		err |= copy_to_user(nm, nodes_addr(bm), alloc_size);
	}

	if (err)
		return -EFAULT;

	return sys_mbind(start, len, mode, nm, nr_bits+1, flags);
}

#endif

/* Return effective policy for a VMA */
static struct mempolicy * get_vma_policy(struct task_struct *task,
		struct vm_area_struct *vma, unsigned long addr)
{
	struct mempolicy *pol = task->mempolicy;

	if (vma) {
		if (vma->vm_ops && vma->vm_ops->get_policy)
			pol = vma->vm_ops->get_policy(vma, addr);
		else if (vma->vm_policy &&
				vma->vm_policy->policy != MPOL_DEFAULT)
			pol = vma->vm_policy;
	}
	if (!pol)
		pol = &default_policy;
	return pol;
}

/* Return a zonelist representing a mempolicy */
static struct zonelist *zonelist_policy(gfp_t gfp, struct mempolicy *policy)
{
	int nd;

	switch (policy->policy) {
	case MPOL_PREFERRED:
		nd = policy->v.preferred_node;
		if (nd < 0)
			nd = numa_node_id();
		break;
	case MPOL_BIND:
		/* Lower zones don't get a policy applied */
		/* Careful: current->mems_allowed might have moved */
		if (gfp_zone(gfp) >= policy_zone)
			if (cpuset_zonelist_valid_mems_allowed(policy->v.zonelist))
				return policy->v.zonelist;
		/*FALL THROUGH*/
	case MPOL_INTERLEAVE: /* should not happen */
	case MPOL_DEFAULT:
		nd = numa_node_id();
		break;
	default:
		nd = 0;
		BUG();
	}
	return NODE_DATA(nd)->node_zonelists + gfp_zone(gfp);
}

/* Do dynamic interleaving for a process */
static unsigned interleave_nodes(struct mempolicy *policy)
{
	unsigned nid, next;
	struct task_struct *me = current;

	nid = me->il_next;
	next = next_node(nid, policy->v.nodes);
	if (next >= MAX_NUMNODES)
		next = first_node(policy->v.nodes);
	me->il_next = next;
	return nid;
}

/*
 * Depending on the memory policy provide a node from which to allocate the
 * next slab entry.
 */
unsigned slab_node(struct mempolicy *policy)
{
	int pol = policy ? policy->policy : MPOL_DEFAULT;

	switch (pol) {
	case MPOL_INTERLEAVE:
		return interleave_nodes(policy);

	case MPOL_BIND:
		/*
		 * Follow bind policy behavior and start allocation at the
		 * first node.
		 */
		return zone_to_nid(policy->v.zonelist->zones[0]);

	case MPOL_PREFERRED:
		if (policy->v.preferred_node >= 0)
			return policy->v.preferred_node;
		/* Fall through */

	default:
		return numa_node_id();
	}
}

/* Do static interleaving for a VMA with known offset. */
static unsigned offset_il_node(struct mempolicy *pol,
		struct vm_area_struct *vma, unsigned long off)
{
	unsigned nnodes = nodes_weight(pol->v.nodes);
	unsigned target = (unsigned)off % nnodes;
	int c;
	int nid = -1;

	c = 0;
	do {
		nid = next_node(nid, pol->v.nodes);
		c++;
	} while (c <= target);
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
		return offset_il_node(pol, vma, off);
	} else
		return interleave_nodes(pol);
}

#ifdef CONFIG_HUGETLBFS
/* Return a zonelist suitable for a huge page allocation. */
struct zonelist *huge_zonelist(struct vm_area_struct *vma, unsigned long addr)
{
	struct mempolicy *pol = get_vma_policy(current, vma, addr);

	if (pol->policy == MPOL_INTERLEAVE) {
		unsigned nid;

		nid = interleave_nid(pol, vma, addr, HPAGE_SHIFT);
		return NODE_DATA(nid)->node_zonelists + gfp_zone(GFP_HIGHUSER);
	}
	return zonelist_policy(GFP_HIGHUSER, pol);
}
#endif

/* Allocate a page in interleaved policy.
   Own path because it needs to do special accounting. */
static struct page *alloc_page_interleave(gfp_t gfp, unsigned order,
					unsigned nid)
{
	struct zonelist *zl;
	struct page *page;

	zl = NODE_DATA(nid)->node_zonelists + gfp_zone(gfp);
	page = __alloc_pages(gfp, order, zl);
	if (page && page_zone(page) == zl->zones[0])
		inc_zone_page_state(page, NUMA_INTERLEAVE_HIT);
	return page;
}

/**
 * 	alloc_page_vma	- Allocate a page for a VMA.
 *
 * 	@gfp:
 *      %GFP_USER    user allocation.
 *      %GFP_KERNEL  kernel allocations,
 *      %GFP_HIGHMEM highmem/user allocations,
 *      %GFP_FS      allocation should not call back into a file system.
 *      %GFP_ATOMIC  don't sleep.
 *
 * 	@vma:  Pointer to VMA or NULL if not available.
 *	@addr: Virtual Address of the allocation. Must be inside the VMA.
 *
 * 	This function allocates a page from the kernel page pool and applies
 *	a NUMA policy associated with the VMA or the current process.
 *	When VMA is not NULL caller must hold down_read on the mmap_sem of the
 *	mm_struct of the VMA to prevent it from going away. Should be used for
 *	all allocations for pages that will be mapped into
 * 	user space. Returns NULL when no page can be allocated.
 *
 *	Should be called with the mm_sem of the vma hold.
 */
struct page *
alloc_page_vma(gfp_t gfp, struct vm_area_struct *vma, unsigned long addr)
{
	struct mempolicy *pol = get_vma_policy(current, vma, addr);

	cpuset_update_task_memory_state();

	if (unlikely(pol->policy == MPOL_INTERLEAVE)) {
		unsigned nid;

		nid = interleave_nid(pol, vma, addr, PAGE_SHIFT);
		return alloc_page_interleave(gfp, 0, nid);
	}
	return __alloc_pages(gfp, 0, zonelist_policy(gfp, pol));
}

/**
 * 	alloc_pages_current - Allocate pages.
 *
 *	@gfp:
 *		%GFP_USER   user allocation,
 *      	%GFP_KERNEL kernel allocation,
 *      	%GFP_HIGHMEM highmem allocation,
 *      	%GFP_FS     don't call back into a file system.
 *      	%GFP_ATOMIC don't sleep.
 *	@order: Power of two of allocation size in pages. 0 is a single page.
 *
 *	Allocate a page from the kernel page pool.  When not in
 *	interrupt context and apply the current process NUMA policy.
 *	Returns NULL when no page can be allocated.
 *
 *	Don't call cpuset_update_task_memory_state() unless
 *	1) it's ok to take cpuset_sem (can WAIT), and
 *	2) allocating for current task (not interrupt).
 */
struct page *alloc_pages_current(gfp_t gfp, unsigned order)
{
	struct mempolicy *pol = current->mempolicy;

	if ((gfp & __GFP_WAIT) && !in_interrupt())
		cpuset_update_task_memory_state();
	if (!pol || in_interrupt() || (gfp & __GFP_THISNODE))
		pol = &default_policy;
	if (pol->policy == MPOL_INTERLEAVE)
		return alloc_page_interleave(gfp, order, interleave_nodes(pol));
	return __alloc_pages(gfp, order, zonelist_policy(gfp, pol));
}
EXPORT_SYMBOL(alloc_pages_current);

/*
 * If mpol_copy() sees current->cpuset == cpuset_being_rebound, then it
 * rebinds the mempolicy its copying by calling mpol_rebind_policy()
 * with the mems_allowed returned by cpuset_mems_allowed().  This
 * keeps mempolicies cpuset relative after its cpuset moves.  See
 * further kernel/cpuset.c update_nodemask().
 */
void *cpuset_being_rebound;

/* Slow path of a mempolicy copy */
struct mempolicy *__mpol_copy(struct mempolicy *old)
{
	struct mempolicy *new = kmem_cache_alloc(policy_cache, GFP_KERNEL);

	if (!new)
		return ERR_PTR(-ENOMEM);
	if (current_cpuset_is_being_rebound()) {
		nodemask_t mems = cpuset_mems_allowed(current);
		mpol_rebind_policy(old, &mems);
	}
	*new = *old;
	atomic_set(&new->refcnt, 1);
	if (new->policy == MPOL_BIND) {
		int sz = ksize(old->v.zonelist);
		new->v.zonelist = kmemdup(old->v.zonelist, sz, GFP_KERNEL);
		if (!new->v.zonelist) {
			kmem_cache_free(policy_cache, new);
			return ERR_PTR(-ENOMEM);
		}
	}
	return new;
}

/* Slow path of a mempolicy comparison */
int __mpol_equal(struct mempolicy *a, struct mempolicy *b)
{
	if (!a || !b)
		return 0;
	if (a->policy != b->policy)
		return 0;
	switch (a->policy) {
	case MPOL_DEFAULT:
		return 1;
	case MPOL_INTERLEAVE:
		return nodes_equal(a->v.nodes, b->v.nodes);
	case MPOL_PREFERRED:
		return a->v.preferred_node == b->v.preferred_node;
	case MPOL_BIND: {
		int i;
		for (i = 0; a->v.zonelist->zones[i]; i++)
			if (a->v.zonelist->zones[i] != b->v.zonelist->zones[i])
				return 0;
		return b->v.zonelist->zones[i] == NULL;
	}
	default:
		BUG();
		return 0;
	}
}

/* Slow path of a mpol destructor. */
void __mpol_free(struct mempolicy *p)
{
	if (!atomic_dec_and_test(&p->refcnt))
		return;
	if (p->policy == MPOL_BIND)
		kfree(p->v.zonelist);
	p->policy = MPOL_DEFAULT;
	kmem_cache_free(policy_cache, p);
}

/*
 * Shared memory backing store policy support.
 *
 * Remember policies even when nobody has shared memory mapped.
 * The policies are kept in Red-Black tree linked from the inode.
 * They are protected by the sp->lock spinlock, which should be held
 * for any accesses to the tree.
 */

/* lookup first element intersecting start-end */
/* Caller holds sp->lock */
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

/* Insert a new shared policy into the list. */
/* Caller holds sp->lock */
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
		 new->policy ? new->policy->policy : 0);
}

/* Find shared policy intersecting idx */
struct mempolicy *
mpol_shared_policy_lookup(struct shared_policy *sp, unsigned long idx)
{
	struct mempolicy *pol = NULL;
	struct sp_node *sn;

	if (!sp->root.rb_node)
		return NULL;
	spin_lock(&sp->lock);
	sn = sp_lookup(sp, idx, idx+1);
	if (sn) {
		mpol_get(sn->policy);
		pol = sn->policy;
	}
	spin_unlock(&sp->lock);
	return pol;
}

static void sp_delete(struct shared_policy *sp, struct sp_node *n)
{
	pr_debug("deleting %lx-l%lx\n", n->start, n->end);
	rb_erase(&n->nd, &sp->root);
	mpol_free(n->policy);
	kmem_cache_free(sn_cache, n);
}

struct sp_node *
sp_alloc(unsigned long start, unsigned long end, struct mempolicy *pol)
{
	struct sp_node *n = kmem_cache_alloc(sn_cache, GFP_KERNEL);

	if (!n)
		return NULL;
	n->start = start;
	n->end = end;
	mpol_get(pol);
	n->policy = pol;
	return n;
}

/* Replace a policy range. */
static int shared_policy_replace(struct shared_policy *sp, unsigned long start,
				 unsigned long end, struct sp_node *new)
{
	struct sp_node *n, *new2 = NULL;

restart:
	spin_lock(&sp->lock);
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
				if (!new2) {
					spin_unlock(&sp->lock);
					new2 = sp_alloc(end, n->end, n->policy);
					if (!new2)
						return -ENOMEM;
					goto restart;
				}
				n->end = start;
				sp_insert(sp, new2);
				new2 = NULL;
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
	spin_unlock(&sp->lock);
	if (new2) {
		mpol_free(new2->policy);
		kmem_cache_free(sn_cache, new2);
	}
	return 0;
}

void mpol_shared_policy_init(struct shared_policy *info, int policy,
				nodemask_t *policy_nodes)
{
	info->root = RB_ROOT;
	spin_lock_init(&info->lock);

	if (policy != MPOL_DEFAULT) {
		struct mempolicy *newpol;

		/* Falls back to MPOL_DEFAULT on any error */
		newpol = mpol_new(policy, policy_nodes);
		if (!IS_ERR(newpol)) {
			/* Create pseudo-vma that contains just the policy */
			struct vm_area_struct pvma;

			memset(&pvma, 0, sizeof(struct vm_area_struct));
			/* Policy covers entire file */
			pvma.vm_end = TASK_SIZE;
			mpol_set_shared_policy(info, &pvma, newpol);
			mpol_free(newpol);
		}
	}
}

int mpol_set_shared_policy(struct shared_policy *info,
			struct vm_area_struct *vma, struct mempolicy *npol)
{
	int err;
	struct sp_node *new = NULL;
	unsigned long sz = vma_pages(vma);

	pr_debug("set_shared_policy %lx sz %lu %d %lx\n",
		 vma->vm_pgoff,
		 sz, npol? npol->policy : -1,
		 npol ? nodes_addr(npol->v.nodes)[0] : -1);

	if (npol) {
		new = sp_alloc(vma->vm_pgoff, vma->vm_pgoff + sz, npol);
		if (!new)
			return -ENOMEM;
	}
	err = shared_policy_replace(info, vma->vm_pgoff, vma->vm_pgoff+sz, new);
	if (err && new)
		kmem_cache_free(sn_cache, new);
	return err;
}

/* Free a backing policy store on inode delete. */
void mpol_free_shared_policy(struct shared_policy *p)
{
	struct sp_node *n;
	struct rb_node *next;

	if (!p->root.rb_node)
		return;
	spin_lock(&p->lock);
	next = rb_first(&p->root);
	while (next) {
		n = rb_entry(next, struct sp_node, nd);
		next = rb_next(&n->nd);
		rb_erase(&n->nd, &p->root);
		mpol_free(n->policy);
		kmem_cache_free(sn_cache, n);
	}
	spin_unlock(&p->lock);
}

/* assumes fs == KERNEL_DS */
void __init numa_policy_init(void)
{
	nodemask_t interleave_nodes;
	unsigned long largest = 0;
	int nid, prefer = 0;

	policy_cache = kmem_cache_create("numa_policy",
					 sizeof(struct mempolicy),
					 0, SLAB_PANIC, NULL, NULL);

	sn_cache = kmem_cache_create("shared_policy_node",
				     sizeof(struct sp_node),
				     0, SLAB_PANIC, NULL, NULL);

	/*
	 * Set interleaving policy for system init. Interleaving is only
	 * enabled across suitably sized nodes (default is >= 16MB), or
	 * fall back to the largest node if they're all smaller.
	 */
	nodes_clear(interleave_nodes);
	for_each_online_node(nid) {
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

	if (do_set_mempolicy(MPOL_INTERLEAVE, &interleave_nodes))
		printk("numa_policy_init: interleaving failed\n");
}

/* Reset policy of current process to default */
void numa_default_policy(void)
{
	do_set_mempolicy(MPOL_DEFAULT, NULL);
}

/* Migrate a policy to a different set of nodes */
void mpol_rebind_policy(struct mempolicy *pol, const nodemask_t *newmask)
{
	nodemask_t *mpolmask;
	nodemask_t tmp;

	if (!pol)
		return;
	mpolmask = &pol->cpuset_mems_allowed;
	if (nodes_equal(*mpolmask, *newmask))
		return;

	switch (pol->policy) {
	case MPOL_DEFAULT:
		break;
	case MPOL_INTERLEAVE:
		nodes_remap(tmp, pol->v.nodes, *mpolmask, *newmask);
		pol->v.nodes = tmp;
		*mpolmask = *newmask;
		current->il_next = node_remap(current->il_next,
						*mpolmask, *newmask);
		break;
	case MPOL_PREFERRED:
		pol->v.preferred_node = node_remap(pol->v.preferred_node,
						*mpolmask, *newmask);
		*mpolmask = *newmask;
		break;
	case MPOL_BIND: {
		nodemask_t nodes;
		struct zone **z;
		struct zonelist *zonelist;

		nodes_clear(nodes);
		for (z = pol->v.zonelist->zones; *z; z++)
			node_set(zone_to_nid(*z), nodes);
		nodes_remap(tmp, nodes, *mpolmask, *newmask);
		nodes = tmp;

		zonelist = bind_zonelist(&nodes);

		/* If no mem, then zonelist is NULL and we keep old zonelist.
		 * If that old zonelist has no remaining mems_allowed nodes,
		 * then zonelist_policy() will "FALL THROUGH" to MPOL_DEFAULT.
		 */

		if (!IS_ERR(zonelist)) {
			/* Good - got mem - substitute new zonelist */
			kfree(pol->v.zonelist);
			pol->v.zonelist = zonelist;
		}
		*mpolmask = *newmask;
		break;
	}
	default:
		BUG();
		break;
	}
}

/*
 * Wrapper for mpol_rebind_policy() that just requires task
 * pointer, and updates task mempolicy.
 */

void mpol_rebind_task(struct task_struct *tsk, const nodemask_t *new)
{
	mpol_rebind_policy(tsk->mempolicy, new);
}

/*
 * Rebind each vma in mm to new nodemask.
 *
 * Call holding a reference to mm.  Takes mm->mmap_sem during call.
 */

void mpol_rebind_mm(struct mm_struct *mm, nodemask_t *new)
{
	struct vm_area_struct *vma;

	down_write(&mm->mmap_sem);
	for (vma = mm->mmap; vma; vma = vma->vm_next)
		mpol_rebind_policy(vma->vm_policy, new);
	up_write(&mm->mmap_sem);
}

/*
 * Display pages allocated per node and memory policy via /proc.
 */

static const char * const policy_types[] =
	{ "default", "prefer", "bind", "interleave" };

/*
 * Convert a mempolicy into a string.
 * Returns the number of characters in buffer (if positive)
 * or an error (negative)
 */
static inline int mpol_to_str(char *buffer, int maxlen, struct mempolicy *pol)
{
	char *p = buffer;
	int l;
	nodemask_t nodes;
	int mode = pol ? pol->policy : MPOL_DEFAULT;

	switch (mode) {
	case MPOL_DEFAULT:
		nodes_clear(nodes);
		break;

	case MPOL_PREFERRED:
		nodes_clear(nodes);
		node_set(pol->v.preferred_node, nodes);
		break;

	case MPOL_BIND:
		get_zonemask(pol, &nodes);
		break;

	case MPOL_INTERLEAVE:
		nodes = pol->v.nodes;
		break;

	default:
		BUG();
		return -EFAULT;
	}

	l = strlen(policy_types[mode]);
 	if (buffer + maxlen < p + l + 1)
 		return -ENOSPC;

	strcpy(p, policy_types[mode]);
	p += l;

	if (!nodes_empty(nodes)) {
		if (buffer + maxlen < p + 2)
			return -ENOSPC;
		*p++ = '=';
	 	p += nodelist_scnprintf(p, buffer + maxlen - p, nodes);
	}
	return p - buffer;
}

struct numa_maps {
	unsigned long pages;
	unsigned long anon;
	unsigned long active;
	unsigned long writeback;
	unsigned long mapcount_max;
	unsigned long dirty;
	unsigned long swapcache;
	unsigned long node[MAX_NUMNODES];
};

static void gather_stats(struct page *page, void *private, int pte_dirty)
{
	struct numa_maps *md = private;
	int count = page_mapcount(page);

	md->pages++;
	if (pte_dirty || PageDirty(page))
		md->dirty++;

	if (PageSwapCache(page))
		md->swapcache++;

	if (PageActive(page))
		md->active++;

	if (PageWriteback(page))
		md->writeback++;

	if (PageAnon(page))
		md->anon++;

	if (count > md->mapcount_max)
		md->mapcount_max = count;

	md->node[page_to_nid(page)]++;
}

#ifdef CONFIG_HUGETLB_PAGE
static void check_huge_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end,
		struct numa_maps *md)
{
	unsigned long addr;
	struct page *page;

	for (addr = start; addr < end; addr += HPAGE_SIZE) {
		pte_t *ptep = huge_pte_offset(vma->vm_mm, addr & HPAGE_MASK);
		pte_t pte;

		if (!ptep)
			continue;

		pte = *ptep;
		if (pte_none(pte))
			continue;

		page = pte_page(pte);
		if (!page)
			continue;

		gather_stats(page, md, pte_dirty(*ptep));
	}
}
#else
static inline void check_huge_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end,
		struct numa_maps *md)
{
}
#endif

int show_numa_map(struct seq_file *m, void *v)
{
	struct proc_maps_private *priv = m->private;
	struct vm_area_struct *vma = v;
	struct numa_maps *md;
	struct file *file = vma->vm_file;
	struct mm_struct *mm = vma->vm_mm;
	int n;
	char buffer[50];

	if (!mm)
		return 0;

	md = kzalloc(sizeof(struct numa_maps), GFP_KERNEL);
	if (!md)
		return 0;

	mpol_to_str(buffer, sizeof(buffer),
			    get_vma_policy(priv->task, vma, vma->vm_start));

	seq_printf(m, "%08lx %s", vma->vm_start, buffer);

	if (file) {
		seq_printf(m, " file=");
		seq_path(m, file->f_path.mnt, file->f_path.dentry, "\n\t= ");
	} else if (vma->vm_start <= mm->brk && vma->vm_end >= mm->start_brk) {
		seq_printf(m, " heap");
	} else if (vma->vm_start <= mm->start_stack &&
			vma->vm_end >= mm->start_stack) {
		seq_printf(m, " stack");
	}

	if (is_vm_hugetlb_page(vma)) {
		check_huge_range(vma, vma->vm_start, vma->vm_end, md);
		seq_printf(m, " huge");
	} else {
		check_pgd_range(vma, vma->vm_start, vma->vm_end,
				&node_online_map, MPOL_MF_STATS, md);
	}

	if (!md->pages)
		goto out;

	if (md->anon)
		seq_printf(m," anon=%lu",md->anon);

	if (md->dirty)
		seq_printf(m," dirty=%lu",md->dirty);

	if (md->pages != md->anon && md->pages != md->dirty)
		seq_printf(m, " mapped=%lu", md->pages);

	if (md->mapcount_max > 1)
		seq_printf(m, " mapmax=%lu", md->mapcount_max);

	if (md->swapcache)
		seq_printf(m," swapcache=%lu", md->swapcache);

	if (md->active < md->pages && !is_vm_hugetlb_page(vma))
		seq_printf(m," active=%lu", md->active);

	if (md->writeback)
		seq_printf(m," writeback=%lu", md->writeback);

	for_each_online_node(n)
		if (md->node[n])
			seq_printf(m, " N%d=%lu", n, md->node[n]);
out:
	seq_putc(m, '\n');
	kfree(md);

	if (m->count < m->size)
		m->version = (vma != priv->tail_vma) ? vma->vm_start : 0;
	return 0;
}

