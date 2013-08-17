#ifndef _LINUX_BLOCKGROUP_LOCK_H
#define _LINUX_BLOCKGROUP_LOCK_H
/*
 * Per-blockgroup locking for ext2 and ext3.
 *
 * Simple hashed spinlocking.
 */

#include <linux/spinlock.h>
#include <linux/cache.h>

#ifdef CONFIG_SMP

/*
 * We want a power-of-two.  Is there a better way than this?
 */

#if NR_CPUS >= 32
#define NR_BG_LOCKS	128
#elif NR_CPUS >= 16
#define NR_BG_LOCKS	64
#elif NR_CPUS >= 8
#define NR_BG_LOCKS	32
#elif NR_CPUS >= 4
#define NR_BG_LOCKS	16
#elif NR_CPUS >= 2
#define NR_BG_LOCKS	8
#else
#define NR_BG_LOCKS	4
#endif

#else	/* CONFIG_SMP */
#define NR_BG_LOCKS	1
#endif	/* CONFIG_SMP */

struct bgl_lock {
	spinlock_t lock;
} ____cacheline_aligned_in_smp;

struct blockgroup_lock {
	struct bgl_lock locks[NR_BG_LOCKS];
};

static inline void bgl_lock_init(struct blockgroup_lock *bgl)
{
	int i;

	for (i = 0; i < NR_BG_LOCKS; i++)
		spin_lock_init(&bgl->locks[i].lock);
}

/*
 * The accessor is a macro so we can embed a blockgroup_lock into different
 * superblock types
 */
static inline spinlock_t *
bgl_lock_ptr(struct blockgroup_lock *bgl, unsigned int block_group)
{
	return &bgl->locks[(block_group) & (NR_BG_LOCKS-1)].lock;
}

#endif
