#ifndef _LINUX_MEMPOLICY_H
#define _LINUX_MEMPOLICY_H 1

#include <linux/errno.h>

/*
 * NUMA memory policies for Linux.
 * Copyright 2003,2004 Andi Kleen SuSE Labs
 */

/* Policies */
#define MPOL_DEFAULT	0
#define MPOL_PREFERRED	1
#define MPOL_BIND	2
#define MPOL_INTERLEAVE	3

#define MPOL_MAX MPOL_INTERLEAVE

/* Flags for get_mem_policy */
#define MPOL_F_NODE	(1<<0)	/* return next IL mode instead of node mask */
#define MPOL_F_ADDR	(1<<1)	/* look up vma using address */

/* Flags for mbind */
#define MPOL_MF_STRICT	(1<<0)	/* Verify existing pages in the mapping */
#define MPOL_MF_MOVE	(1<<1)	/* Move pages owned by this process to conform to mapping */
#define MPOL_MF_MOVE_ALL (1<<2)	/* Move every page to conform to mapping */
#define MPOL_MF_INTERNAL (1<<3)	/* Internal flags start here */

#ifdef __KERNEL__

#include <linux/mmzone.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/nodemask.h>

struct vm_area_struct;

#ifdef CONFIG_NUMA

/*
 * Describe a memory policy.
 *
 * A mempolicy can be either associated with a process or with a VMA.
 * For VMA related allocations the VMA policy is preferred, otherwise
 * the process policy is used. Interrupts ignore the memory policy
 * of the current process.
 *
 * Locking policy for interlave:
 * In process context there is no locking because only the process accesses
 * its own state. All vma manipulation is somewhat protected by a down_read on
 * mmap_sem.
 *
 * Freeing policy:
 * When policy is MPOL_BIND v.zonelist is kmalloc'ed and must be kfree'd.
 * All other policies don't have any external state. mpol_free() handles this.
 *
 * Copying policy objects:
 * For MPOL_BIND the zonelist must be always duplicated. mpol_clone() does this.
 */
struct mempolicy {
	atomic_t refcnt;
	short policy; 	/* See MPOL_* above */
	union {
		struct zonelist  *zonelist;	/* bind */
		short 		 preferred_node; /* preferred */
		nodemask_t	 nodes;		/* interleave */
		/* undefined for default */
	} v;
	nodemask_t cpuset_mems_allowed;	/* mempolicy relative to these nodes */
};

/*
 * Support for managing mempolicy data objects (clone, copy, destroy)
 * The default fast path of a NULL MPOL_DEFAULT policy is always inlined.
 */

extern void __mpol_free(struct mempolicy *pol);
static inline void mpol_free(struct mempolicy *pol)
{
	if (pol)
		__mpol_free(pol);
}

extern struct mempolicy *__mpol_copy(struct mempolicy *pol);
static inline struct mempolicy *mpol_copy(struct mempolicy *pol)
{
	if (pol)
		pol = __mpol_copy(pol);
	return pol;
}

#define vma_policy(vma) ((vma)->vm_policy)
#define vma_set_policy(vma, pol) ((vma)->vm_policy = (pol))

static inline void mpol_get(struct mempolicy *pol)
{
	if (pol)
		atomic_inc(&pol->refcnt);
}

extern int __mpol_equal(struct mempolicy *a, struct mempolicy *b);
static inline int mpol_equal(struct mempolicy *a, struct mempolicy *b)
{
	if (a == b)
		return 1;
	return __mpol_equal(a, b);
}
#define vma_mpol_equal(a,b) mpol_equal(vma_policy(a), vma_policy(b))

/* Could later add inheritance of the process policy here. */

#define mpol_set_vma_default(vma) ((vma)->vm_policy = NULL)

/*
 * Tree of shared policies for a shared memory region.
 * Maintain the policies in a pseudo mm that contains vmas. The vmas
 * carry the policy. As a special twist the pseudo mm is indexed in pages, not
 * bytes, so that we can work with shared memory segments bigger than
 * unsigned long.
 */

struct sp_node {
	struct rb_node nd;
	unsigned long start, end;
	struct mempolicy *policy;
};

struct shared_policy {
	struct rb_root root;
	spinlock_t lock;
};

void mpol_shared_policy_init(struct shared_policy *info, int policy,
				nodemask_t *nodes);
int mpol_set_shared_policy(struct shared_policy *info,
				struct vm_area_struct *vma,
				struct mempolicy *new);
void mpol_free_shared_policy(struct shared_policy *p);
struct mempolicy *mpol_shared_policy_lookup(struct shared_policy *sp,
					    unsigned long idx);

extern void numa_default_policy(void);
extern void numa_policy_init(void);
extern void mpol_rebind_policy(struct mempolicy *pol, const nodemask_t *new);
extern void mpol_rebind_task(struct task_struct *tsk,
					const nodemask_t *new);
extern void mpol_rebind_mm(struct mm_struct *mm, nodemask_t *new);
extern void mpol_fix_fork_child_flag(struct task_struct *p);
#define set_cpuset_being_rebound(x) (cpuset_being_rebound = (x))

#ifdef CONFIG_CPUSET
#define current_cpuset_is_being_rebound() \
				(cpuset_being_rebound == current->cpuset)
#else
#define current_cpuset_is_being_rebound() 0
#endif

extern struct mempolicy default_policy;
extern struct zonelist *huge_zonelist(struct vm_area_struct *vma,
		unsigned long addr);
extern unsigned slab_node(struct mempolicy *policy);

extern int policy_zone;

static inline void check_highest_zone(int k)
{
	if (k > policy_zone)
		policy_zone = k;
}

int do_migrate_pages(struct mm_struct *mm,
	const nodemask_t *from_nodes, const nodemask_t *to_nodes, int flags);

extern void *cpuset_being_rebound;	/* Trigger mpol_copy vma rebind */

#else

struct mempolicy {};

static inline int mpol_equal(struct mempolicy *a, struct mempolicy *b)
{
	return 1;
}
#define vma_mpol_equal(a,b) 1

#define mpol_set_vma_default(vma) do {} while(0)

static inline void mpol_free(struct mempolicy *p)
{
}

static inline void mpol_get(struct mempolicy *pol)
{
}

static inline struct mempolicy *mpol_copy(struct mempolicy *old)
{
	return NULL;
}

struct shared_policy {};

static inline int mpol_set_shared_policy(struct shared_policy *info,
					struct vm_area_struct *vma,
					struct mempolicy *new)
{
	return -EINVAL;
}

static inline void mpol_shared_policy_init(struct shared_policy *info,
					int policy, nodemask_t *nodes)
{
}

static inline void mpol_free_shared_policy(struct shared_policy *p)
{
}

static inline struct mempolicy *
mpol_shared_policy_lookup(struct shared_policy *sp, unsigned long idx)
{
	return NULL;
}

#define vma_policy(vma) NULL
#define vma_set_policy(vma, pol) do {} while(0)

static inline void numa_policy_init(void)
{
}

static inline void numa_default_policy(void)
{
}

static inline void mpol_rebind_policy(struct mempolicy *pol,
					const nodemask_t *new)
{
}

static inline void mpol_rebind_task(struct task_struct *tsk,
					const nodemask_t *new)
{
}

static inline void mpol_rebind_mm(struct mm_struct *mm, nodemask_t *new)
{
}

static inline void mpol_fix_fork_child_flag(struct task_struct *p)
{
}

#define set_cpuset_being_rebound(x) do {} while (0)

static inline struct zonelist *huge_zonelist(struct vm_area_struct *vma,
		unsigned long addr)
{
	return NODE_DATA(0)->node_zonelists + gfp_zone(GFP_HIGHUSER);
}

static inline int do_migrate_pages(struct mm_struct *mm,
			const nodemask_t *from_nodes,
			const nodemask_t *to_nodes, int flags)
{
	return 0;
}

static inline void check_highest_zone(int k)
{
}
#endif /* CONFIG_NUMA */
#endif /* __KERNEL__ */

#endif
