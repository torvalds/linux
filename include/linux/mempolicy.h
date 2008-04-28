#ifndef _LINUX_MEMPOLICY_H
#define _LINUX_MEMPOLICY_H 1

#include <linux/errno.h>

/*
 * NUMA memory policies for Linux.
 * Copyright 2003,2004 Andi Kleen SuSE Labs
 */

/*
 * Both the MPOL_* mempolicy mode and the MPOL_F_* optional mode flags are
 * passed by the user to either set_mempolicy() or mbind() in an 'int' actual.
 * The MPOL_MODE_FLAGS macro determines the legal set of optional mode flags.
 */

/* Policies */
enum {
	MPOL_DEFAULT,
	MPOL_PREFERRED,
	MPOL_BIND,
	MPOL_INTERLEAVE,
	MPOL_MAX,	/* always last member of enum */
};

/* Flags for set_mempolicy */
#define MPOL_F_STATIC_NODES	(1 << 15)
#define MPOL_F_RELATIVE_NODES	(1 << 14)

/*
 * MPOL_MODE_FLAGS is the union of all possible optional mode flags passed to
 * either set_mempolicy() or mbind().
 */
#define MPOL_MODE_FLAGS	(MPOL_F_STATIC_NODES | MPOL_F_RELATIVE_NODES)

/* Flags for get_mempolicy */
#define MPOL_F_NODE	(1<<0)	/* return next IL mode instead of node mask */
#define MPOL_F_ADDR	(1<<1)	/* look up vma using address */
#define MPOL_F_MEMS_ALLOWED (1<<2) /* return allowed memories */

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

struct mm_struct;

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
 * Mempolicy objects are reference counted.  A mempolicy will be freed when
 * mpol_free() decrements the reference count to zero.
 *
 * Copying policy objects:
 * mpol_copy() allocates a new mempolicy and copies the specified mempolicy
 * to the new storage.  The reference count of the new object is initialized
 * to 1, representing the caller of mpol_copy().
 */
struct mempolicy {
	atomic_t refcnt;
	unsigned short policy; 	/* See MPOL_* above */
	unsigned short flags;	/* See set_mempolicy() MPOL_F_* above */
	union {
		short 		 preferred_node; /* preferred */
		nodemask_t	 nodes;		/* interleave/bind */
		/* undefined for default */
	} v;
	union {
		nodemask_t cpuset_mems_allowed;	/* relative to these nodes */
		nodemask_t user_nodemask;	/* nodemask passed by user */
	} w;
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

void mpol_shared_policy_init(struct shared_policy *info, unsigned short policy,
				unsigned short flags, nodemask_t *nodes);
int mpol_set_shared_policy(struct shared_policy *info,
				struct vm_area_struct *vma,
				struct mempolicy *new);
void mpol_free_shared_policy(struct shared_policy *p);
struct mempolicy *mpol_shared_policy_lookup(struct shared_policy *sp,
					    unsigned long idx);

extern void numa_default_policy(void);
extern void numa_policy_init(void);
extern void mpol_rebind_task(struct task_struct *tsk,
					const nodemask_t *new);
extern void mpol_rebind_mm(struct mm_struct *mm, nodemask_t *new);
extern void mpol_fix_fork_child_flag(struct task_struct *p);

extern struct zonelist *huge_zonelist(struct vm_area_struct *vma,
				unsigned long addr, gfp_t gfp_flags,
				struct mempolicy **mpol, nodemask_t **nodemask);
extern unsigned slab_node(struct mempolicy *policy);

extern enum zone_type policy_zone;

static inline void check_highest_zone(enum zone_type k)
{
	if (k > policy_zone && k != ZONE_MOVABLE)
		policy_zone = k;
}

int do_migrate_pages(struct mm_struct *mm,
	const nodemask_t *from_nodes, const nodemask_t *to_nodes, int flags);

#else

struct mempolicy {};

static inline int mpol_equal(struct mempolicy *a, struct mempolicy *b)
{
	return 1;
}

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
		unsigned short policy, unsigned short flags, nodemask_t *nodes)
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

static inline struct zonelist *huge_zonelist(struct vm_area_struct *vma,
				unsigned long addr, gfp_t gfp_flags,
				struct mempolicy **mpol, nodemask_t **nodemask)
{
	*mpol = NULL;
	*nodemask = NULL;
	return node_zonelist(0, gfp_flags);
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
