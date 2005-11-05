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

#ifdef __KERNEL__

#include <linux/config.h>
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
 * Hugetlb policy. i386 hugetlb so far works with node numbers
 * instead of zone lists, so give it special interfaces for now.
 */
extern int mpol_first_node(struct vm_area_struct *vma, unsigned long addr);
extern int mpol_node_valid(int nid, struct vm_area_struct *vma,
			unsigned long addr);

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

static inline void mpol_shared_policy_init(struct shared_policy *info)
{
	info->root = RB_ROOT;
	spin_lock_init(&info->lock);
}

int mpol_set_shared_policy(struct shared_policy *info,
				struct vm_area_struct *vma,
				struct mempolicy *new);
void mpol_free_shared_policy(struct shared_policy *p);
struct mempolicy *mpol_shared_policy_lookup(struct shared_policy *sp,
					    unsigned long idx);

struct mempolicy *get_vma_policy(struct task_struct *task,
			struct vm_area_struct *vma, unsigned long addr);

extern void numa_default_policy(void);
extern void numa_policy_init(void);
extern void numa_policy_rebind(const nodemask_t *old, const nodemask_t *new);
extern struct mempolicy default_policy;

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

static inline int mpol_first_node(struct vm_area_struct *vma, unsigned long a)
{
	return numa_node_id();
}

static inline int
mpol_node_valid(int nid, struct vm_area_struct *vma, unsigned long a)
{
	return 1;
}

struct shared_policy {};

static inline int mpol_set_shared_policy(struct shared_policy *info,
					struct vm_area_struct *vma,
					struct mempolicy *new)
{
	return -EINVAL;
}

static inline void mpol_shared_policy_init(struct shared_policy *info)
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

static inline void numa_policy_rebind(const nodemask_t *old,
					const nodemask_t *new)
{
}

#endif /* CONFIG_NUMA */
#endif /* __KERNEL__ */

#endif
