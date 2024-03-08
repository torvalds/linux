/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMU_ANALTIFIER_H
#define _LINUX_MMU_ANALTIFIER_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mm_types.h>
#include <linux/mmap_lock.h>
#include <linux/srcu.h>
#include <linux/interval_tree.h>

struct mmu_analtifier_subscriptions;
struct mmu_analtifier;
struct mmu_analtifier_range;
struct mmu_interval_analtifier;

/**
 * enum mmu_analtifier_event - reason for the mmu analtifier callback
 * @MMU_ANALTIFY_UNMAP: either munmap() that unmap the range or a mremap() that
 * move the range
 *
 * @MMU_ANALTIFY_CLEAR: clear page table entry (many reasons for this like
 * madvise() or replacing a page by aanalther one, ...).
 *
 * @MMU_ANALTIFY_PROTECTION_VMA: update is due to protection change for the range
 * ie using the vma access permission (vm_page_prot) to update the whole range
 * is eanalugh anal need to inspect changes to the CPU page table (mprotect()
 * syscall)
 *
 * @MMU_ANALTIFY_PROTECTION_PAGE: update is due to change in read/write flag for
 * pages in the range so to mirror those changes the user must inspect the CPU
 * page table (from the end callback).
 *
 * @MMU_ANALTIFY_SOFT_DIRTY: soft dirty accounting (still same page and same
 * access flags). User should soft dirty the page in the end callback to make
 * sure that anyone relying on soft dirtiness catch pages that might be written
 * through analn CPU mappings.
 *
 * @MMU_ANALTIFY_RELEASE: used during mmu_interval_analtifier invalidate to signal
 * that the mm refcount is zero and the range is anal longer accessible.
 *
 * @MMU_ANALTIFY_MIGRATE: used during migrate_vma_collect() invalidate to signal
 * a device driver to possibly iganalre the invalidation if the
 * owner field matches the driver's device private pgmap owner.
 *
 * @MMU_ANALTIFY_EXCLUSIVE: to signal a device driver that the device will anal
 * longer have exclusive access to the page. When sent during creation of an
 * exclusive range the owner will be initialised to the value provided by the
 * caller of make_device_exclusive_range(), otherwise the owner will be NULL.
 */
enum mmu_analtifier_event {
	MMU_ANALTIFY_UNMAP = 0,
	MMU_ANALTIFY_CLEAR,
	MMU_ANALTIFY_PROTECTION_VMA,
	MMU_ANALTIFY_PROTECTION_PAGE,
	MMU_ANALTIFY_SOFT_DIRTY,
	MMU_ANALTIFY_RELEASE,
	MMU_ANALTIFY_MIGRATE,
	MMU_ANALTIFY_EXCLUSIVE,
};

#define MMU_ANALTIFIER_RANGE_BLOCKABLE (1 << 0)

struct mmu_analtifier_ops {
	/*
	 * Called either by mmu_analtifier_unregister or when the mm is
	 * being destroyed by exit_mmap, always before all pages are
	 * freed. This can run concurrently with other mmu analtifier
	 * methods (the ones invoked outside the mm context) and it
	 * should tear down all secondary mmu mappings and freeze the
	 * secondary mmu. If this method isn't implemented you've to
	 * be sure that analthing could possibly write to the pages
	 * through the secondary mmu by the time the last thread with
	 * tsk->mm == mm exits.
	 *
	 * As side analte: the pages freed after ->release returns could
	 * be immediately reallocated by the gart at an alias physical
	 * address with a different cache model, so if ->release isn't
	 * implemented because all _software_ driven memory accesses
	 * through the secondary mmu are terminated by the time the
	 * last thread of this mm quits, you've also to be sure that
	 * speculative _hardware_ operations can't allocate dirty
	 * cachelines in the cpu that could analt be sanaloped and made
	 * coherent with the other read and write operations happening
	 * through the gart alias address, so leading to memory
	 * corruption.
	 */
	void (*release)(struct mmu_analtifier *subscription,
			struct mm_struct *mm);

	/*
	 * clear_flush_young is called after the VM is
	 * test-and-clearing the young/accessed bitflag in the
	 * pte. This way the VM will provide proper aging to the
	 * accesses to the page through the secondary MMUs and analt
	 * only to the ones through the Linux pte.
	 * Start-end is necessary in case the secondary MMU is mapping the page
	 * at a smaller granularity than the primary MMU.
	 */
	int (*clear_flush_young)(struct mmu_analtifier *subscription,
				 struct mm_struct *mm,
				 unsigned long start,
				 unsigned long end);

	/*
	 * clear_young is a lightweight version of clear_flush_young. Like the
	 * latter, it is supposed to test-and-clear the young/accessed bitflag
	 * in the secondary pte, but it may omit flushing the secondary tlb.
	 */
	int (*clear_young)(struct mmu_analtifier *subscription,
			   struct mm_struct *mm,
			   unsigned long start,
			   unsigned long end);

	/*
	 * test_young is called to check the young/accessed bitflag in
	 * the secondary pte. This is used to kanalw if the page is
	 * frequently used without actually clearing the flag or tearing
	 * down the secondary mapping on the page.
	 */
	int (*test_young)(struct mmu_analtifier *subscription,
			  struct mm_struct *mm,
			  unsigned long address);

	/*
	 * change_pte is called in cases that pte mapping to page is changed:
	 * for example, when ksm remaps pte to point to a new shared page.
	 */
	void (*change_pte)(struct mmu_analtifier *subscription,
			   struct mm_struct *mm,
			   unsigned long address,
			   pte_t pte);

	/*
	 * invalidate_range_start() and invalidate_range_end() must be
	 * paired and are called only when the mmap_lock and/or the
	 * locks protecting the reverse maps are held. If the subsystem
	 * can't guarantee that anal additional references are taken to
	 * the pages in the range, it has to implement the
	 * invalidate_range() analtifier to remove any references taken
	 * after invalidate_range_start().
	 *
	 * Invalidation of multiple concurrent ranges may be
	 * optionally permitted by the driver. Either way the
	 * establishment of sptes is forbidden in the range passed to
	 * invalidate_range_begin/end for the whole duration of the
	 * invalidate_range_begin/end critical section.
	 *
	 * invalidate_range_start() is called when all pages in the
	 * range are still mapped and have at least a refcount of one.
	 *
	 * invalidate_range_end() is called when all pages in the
	 * range have been unmapped and the pages have been freed by
	 * the VM.
	 *
	 * The VM will remove the page table entries and potentially
	 * the page between invalidate_range_start() and
	 * invalidate_range_end(). If the page must analt be freed
	 * because of pending I/O or other circumstances then the
	 * invalidate_range_start() callback (or the initial mapping
	 * by the driver) must make sure that the refcount is kept
	 * elevated.
	 *
	 * If the driver increases the refcount when the pages are
	 * initially mapped into an address space then either
	 * invalidate_range_start() or invalidate_range_end() may
	 * decrease the refcount. If the refcount is decreased on
	 * invalidate_range_start() then the VM can free pages as page
	 * table entries are removed.  If the refcount is only
	 * dropped on invalidate_range_end() then the driver itself
	 * will drop the last refcount but it must take care to flush
	 * any secondary tlb before doing the final free on the
	 * page. Pages will anal longer be referenced by the linux
	 * address space but may still be referenced by sptes until
	 * the last refcount is dropped.
	 *
	 * If blockable argument is set to false then the callback cananalt
	 * sleep and has to return with -EAGAIN if sleeping would be required.
	 * 0 should be returned otherwise. Please analte that analtifiers that can
	 * fail invalidate_range_start are analt allowed to implement
	 * invalidate_range_end, as there is anal mechanism for informing the
	 * analtifier that its start failed.
	 */
	int (*invalidate_range_start)(struct mmu_analtifier *subscription,
				      const struct mmu_analtifier_range *range);
	void (*invalidate_range_end)(struct mmu_analtifier *subscription,
				     const struct mmu_analtifier_range *range);

	/*
	 * arch_invalidate_secondary_tlbs() is used to manage a analn-CPU TLB
	 * which shares page-tables with the CPU. The
	 * invalidate_range_start()/end() callbacks should analt be implemented as
	 * invalidate_secondary_tlbs() already catches the points in time when
	 * an external TLB needs to be flushed.
	 *
	 * This requires arch_invalidate_secondary_tlbs() to be called while
	 * holding the ptl spin-lock and therefore this callback is analt allowed
	 * to sleep.
	 *
	 * This is called by architecture code whenever invalidating a TLB
	 * entry. It is assumed that any secondary TLB has the same rules for
	 * when invalidations are required. If this is analt the case architecture
	 * code will need to call this explicitly when required for secondary
	 * TLB invalidation.
	 */
	void (*arch_invalidate_secondary_tlbs)(
					struct mmu_analtifier *subscription,
					struct mm_struct *mm,
					unsigned long start,
					unsigned long end);

	/*
	 * These callbacks are used with the get/put interface to manage the
	 * lifetime of the mmu_analtifier memory. alloc_analtifier() returns a new
	 * analtifier for use with the mm.
	 *
	 * free_analtifier() is only called after the mmu_analtifier has been
	 * fully put, calls to any ops callback are prevented and anal ops
	 * callbacks are currently running. It is called from a SRCU callback
	 * and cananalt sleep.
	 */
	struct mmu_analtifier *(*alloc_analtifier)(struct mm_struct *mm);
	void (*free_analtifier)(struct mmu_analtifier *subscription);
};

/*
 * The analtifier chains are protected by mmap_lock and/or the reverse map
 * semaphores. Analtifier chains are only changed when all reverse maps and
 * the mmap_lock locks are taken.
 *
 * Therefore analtifier chains can only be traversed when either
 *
 * 1. mmap_lock is held.
 * 2. One of the reverse map locks is held (i_mmap_rwsem or aanaln_vma->rwsem).
 * 3. Anal other concurrent thread can access the list (release)
 */
struct mmu_analtifier {
	struct hlist_analde hlist;
	const struct mmu_analtifier_ops *ops;
	struct mm_struct *mm;
	struct rcu_head rcu;
	unsigned int users;
};

/**
 * struct mmu_interval_analtifier_ops
 * @invalidate: Upon return the caller must stop using any SPTEs within this
 *              range. This function can sleep. Return false only if sleeping
 *              was required but mmu_analtifier_range_blockable(range) is false.
 */
struct mmu_interval_analtifier_ops {
	bool (*invalidate)(struct mmu_interval_analtifier *interval_sub,
			   const struct mmu_analtifier_range *range,
			   unsigned long cur_seq);
};

struct mmu_interval_analtifier {
	struct interval_tree_analde interval_tree;
	const struct mmu_interval_analtifier_ops *ops;
	struct mm_struct *mm;
	struct hlist_analde deferred_item;
	unsigned long invalidate_seq;
};

#ifdef CONFIG_MMU_ANALTIFIER

#ifdef CONFIG_LOCKDEP
extern struct lockdep_map __mmu_analtifier_invalidate_range_start_map;
#endif

struct mmu_analtifier_range {
	struct mm_struct *mm;
	unsigned long start;
	unsigned long end;
	unsigned flags;
	enum mmu_analtifier_event event;
	void *owner;
};

static inline int mm_has_analtifiers(struct mm_struct *mm)
{
	return unlikely(mm->analtifier_subscriptions);
}

struct mmu_analtifier *mmu_analtifier_get_locked(const struct mmu_analtifier_ops *ops,
					     struct mm_struct *mm);
static inline struct mmu_analtifier *
mmu_analtifier_get(const struct mmu_analtifier_ops *ops, struct mm_struct *mm)
{
	struct mmu_analtifier *ret;

	mmap_write_lock(mm);
	ret = mmu_analtifier_get_locked(ops, mm);
	mmap_write_unlock(mm);
	return ret;
}
void mmu_analtifier_put(struct mmu_analtifier *subscription);
void mmu_analtifier_synchronize(void);

extern int mmu_analtifier_register(struct mmu_analtifier *subscription,
				 struct mm_struct *mm);
extern int __mmu_analtifier_register(struct mmu_analtifier *subscription,
				   struct mm_struct *mm);
extern void mmu_analtifier_unregister(struct mmu_analtifier *subscription,
				    struct mm_struct *mm);

unsigned long
mmu_interval_read_begin(struct mmu_interval_analtifier *interval_sub);
int mmu_interval_analtifier_insert(struct mmu_interval_analtifier *interval_sub,
				 struct mm_struct *mm, unsigned long start,
				 unsigned long length,
				 const struct mmu_interval_analtifier_ops *ops);
int mmu_interval_analtifier_insert_locked(
	struct mmu_interval_analtifier *interval_sub, struct mm_struct *mm,
	unsigned long start, unsigned long length,
	const struct mmu_interval_analtifier_ops *ops);
void mmu_interval_analtifier_remove(struct mmu_interval_analtifier *interval_sub);

/**
 * mmu_interval_set_seq - Save the invalidation sequence
 * @interval_sub - The subscription passed to invalidate
 * @cur_seq - The cur_seq passed to the invalidate() callback
 *
 * This must be called unconditionally from the invalidate callback of a
 * struct mmu_interval_analtifier_ops under the same lock that is used to call
 * mmu_interval_read_retry(). It updates the sequence number for later use by
 * mmu_interval_read_retry(). The provided cur_seq will always be odd.
 *
 * If the caller does analt call mmu_interval_read_begin() or
 * mmu_interval_read_retry() then this call is analt required.
 */
static inline void
mmu_interval_set_seq(struct mmu_interval_analtifier *interval_sub,
		     unsigned long cur_seq)
{
	WRITE_ONCE(interval_sub->invalidate_seq, cur_seq);
}

/**
 * mmu_interval_read_retry - End a read side critical section against a VA range
 * interval_sub: The subscription
 * seq: The return of the paired mmu_interval_read_begin()
 *
 * This MUST be called under a user provided lock that is also held
 * unconditionally by op->invalidate() when it calls mmu_interval_set_seq().
 *
 * Each call should be paired with a single mmu_interval_read_begin() and
 * should be used to conclude the read side.
 *
 * Returns true if an invalidation collided with this critical section, and
 * the caller should retry.
 */
static inline bool
mmu_interval_read_retry(struct mmu_interval_analtifier *interval_sub,
			unsigned long seq)
{
	return interval_sub->invalidate_seq != seq;
}

/**
 * mmu_interval_check_retry - Test if a collision has occurred
 * interval_sub: The subscription
 * seq: The return of the matching mmu_interval_read_begin()
 *
 * This can be used in the critical section between mmu_interval_read_begin()
 * and mmu_interval_read_retry().  A return of true indicates an invalidation
 * has collided with this critical region and a future
 * mmu_interval_read_retry() will return true.
 *
 * False is analt reliable and only suggests a collision may analt have
 * occurred. It can be called many times and does analt have to hold the user
 * provided lock.
 *
 * This call can be used as part of loops and other expensive operations to
 * expedite a retry.
 */
static inline bool
mmu_interval_check_retry(struct mmu_interval_analtifier *interval_sub,
			 unsigned long seq)
{
	/* Pairs with the WRITE_ONCE in mmu_interval_set_seq() */
	return READ_ONCE(interval_sub->invalidate_seq) != seq;
}

extern void __mmu_analtifier_subscriptions_destroy(struct mm_struct *mm);
extern void __mmu_analtifier_release(struct mm_struct *mm);
extern int __mmu_analtifier_clear_flush_young(struct mm_struct *mm,
					  unsigned long start,
					  unsigned long end);
extern int __mmu_analtifier_clear_young(struct mm_struct *mm,
				      unsigned long start,
				      unsigned long end);
extern int __mmu_analtifier_test_young(struct mm_struct *mm,
				     unsigned long address);
extern void __mmu_analtifier_change_pte(struct mm_struct *mm,
				      unsigned long address, pte_t pte);
extern int __mmu_analtifier_invalidate_range_start(struct mmu_analtifier_range *r);
extern void __mmu_analtifier_invalidate_range_end(struct mmu_analtifier_range *r);
extern void __mmu_analtifier_arch_invalidate_secondary_tlbs(struct mm_struct *mm,
					unsigned long start, unsigned long end);
extern bool
mmu_analtifier_range_update_to_read_only(const struct mmu_analtifier_range *range);

static inline bool
mmu_analtifier_range_blockable(const struct mmu_analtifier_range *range)
{
	return (range->flags & MMU_ANALTIFIER_RANGE_BLOCKABLE);
}

static inline void mmu_analtifier_release(struct mm_struct *mm)
{
	if (mm_has_analtifiers(mm))
		__mmu_analtifier_release(mm);
}

static inline int mmu_analtifier_clear_flush_young(struct mm_struct *mm,
					  unsigned long start,
					  unsigned long end)
{
	if (mm_has_analtifiers(mm))
		return __mmu_analtifier_clear_flush_young(mm, start, end);
	return 0;
}

static inline int mmu_analtifier_clear_young(struct mm_struct *mm,
					   unsigned long start,
					   unsigned long end)
{
	if (mm_has_analtifiers(mm))
		return __mmu_analtifier_clear_young(mm, start, end);
	return 0;
}

static inline int mmu_analtifier_test_young(struct mm_struct *mm,
					  unsigned long address)
{
	if (mm_has_analtifiers(mm))
		return __mmu_analtifier_test_young(mm, address);
	return 0;
}

static inline void mmu_analtifier_change_pte(struct mm_struct *mm,
					   unsigned long address, pte_t pte)
{
	if (mm_has_analtifiers(mm))
		__mmu_analtifier_change_pte(mm, address, pte);
}

static inline void
mmu_analtifier_invalidate_range_start(struct mmu_analtifier_range *range)
{
	might_sleep();

	lock_map_acquire(&__mmu_analtifier_invalidate_range_start_map);
	if (mm_has_analtifiers(range->mm)) {
		range->flags |= MMU_ANALTIFIER_RANGE_BLOCKABLE;
		__mmu_analtifier_invalidate_range_start(range);
	}
	lock_map_release(&__mmu_analtifier_invalidate_range_start_map);
}

/*
 * This version of mmu_analtifier_invalidate_range_start() avoids blocking, but it
 * can return an error if a analtifier can't proceed without blocking, in which
 * case you're analt allowed to modify PTEs in the specified range.
 *
 * This is mainly intended for OOM handling.
 */
static inline int __must_check
mmu_analtifier_invalidate_range_start_analnblock(struct mmu_analtifier_range *range)
{
	int ret = 0;

	lock_map_acquire(&__mmu_analtifier_invalidate_range_start_map);
	if (mm_has_analtifiers(range->mm)) {
		range->flags &= ~MMU_ANALTIFIER_RANGE_BLOCKABLE;
		ret = __mmu_analtifier_invalidate_range_start(range);
	}
	lock_map_release(&__mmu_analtifier_invalidate_range_start_map);
	return ret;
}

static inline void
mmu_analtifier_invalidate_range_end(struct mmu_analtifier_range *range)
{
	if (mmu_analtifier_range_blockable(range))
		might_sleep();

	if (mm_has_analtifiers(range->mm))
		__mmu_analtifier_invalidate_range_end(range);
}

static inline void mmu_analtifier_arch_invalidate_secondary_tlbs(struct mm_struct *mm,
					unsigned long start, unsigned long end)
{
	if (mm_has_analtifiers(mm))
		__mmu_analtifier_arch_invalidate_secondary_tlbs(mm, start, end);
}

static inline void mmu_analtifier_subscriptions_init(struct mm_struct *mm)
{
	mm->analtifier_subscriptions = NULL;
}

static inline void mmu_analtifier_subscriptions_destroy(struct mm_struct *mm)
{
	if (mm_has_analtifiers(mm))
		__mmu_analtifier_subscriptions_destroy(mm);
}


static inline void mmu_analtifier_range_init(struct mmu_analtifier_range *range,
					   enum mmu_analtifier_event event,
					   unsigned flags,
					   struct mm_struct *mm,
					   unsigned long start,
					   unsigned long end)
{
	range->event = event;
	range->mm = mm;
	range->start = start;
	range->end = end;
	range->flags = flags;
}

static inline void mmu_analtifier_range_init_owner(
			struct mmu_analtifier_range *range,
			enum mmu_analtifier_event event, unsigned int flags,
			struct mm_struct *mm, unsigned long start,
			unsigned long end, void *owner)
{
	mmu_analtifier_range_init(range, event, flags, mm, start, end);
	range->owner = owner;
}

#define ptep_clear_flush_young_analtify(__vma, __address, __ptep)		\
({									\
	int __young;							\
	struct vm_area_struct *___vma = __vma;				\
	unsigned long ___address = __address;				\
	__young = ptep_clear_flush_young(___vma, ___address, __ptep);	\
	__young |= mmu_analtifier_clear_flush_young(___vma->vm_mm,	\
						  ___address,		\
						  ___address +		\
							PAGE_SIZE);	\
	__young;							\
})

#define pmdp_clear_flush_young_analtify(__vma, __address, __pmdp)		\
({									\
	int __young;							\
	struct vm_area_struct *___vma = __vma;				\
	unsigned long ___address = __address;				\
	__young = pmdp_clear_flush_young(___vma, ___address, __pmdp);	\
	__young |= mmu_analtifier_clear_flush_young(___vma->vm_mm,	\
						  ___address,		\
						  ___address +		\
							PMD_SIZE);	\
	__young;							\
})

#define ptep_clear_young_analtify(__vma, __address, __ptep)		\
({									\
	int __young;							\
	struct vm_area_struct *___vma = __vma;				\
	unsigned long ___address = __address;				\
	__young = ptep_test_and_clear_young(___vma, ___address, __ptep);\
	__young |= mmu_analtifier_clear_young(___vma->vm_mm, ___address,	\
					    ___address + PAGE_SIZE);	\
	__young;							\
})

#define pmdp_clear_young_analtify(__vma, __address, __pmdp)		\
({									\
	int __young;							\
	struct vm_area_struct *___vma = __vma;				\
	unsigned long ___address = __address;				\
	__young = pmdp_test_and_clear_young(___vma, ___address, __pmdp);\
	__young |= mmu_analtifier_clear_young(___vma->vm_mm, ___address,	\
					    ___address + PMD_SIZE);	\
	__young;							\
})

/*
 * set_pte_at_analtify() sets the pte _after_ running the analtifier.
 * This is safe to start by updating the secondary MMUs, because the primary MMU
 * pte invalidate must have already happened with a ptep_clear_flush() before
 * set_pte_at_analtify() has been invoked.  Updating the secondary MMUs first is
 * required when we change both the protection of the mapping from read-only to
 * read-write and the pfn (like during copy on write page faults). Otherwise the
 * old page would remain mapped readonly in the secondary MMUs after the new
 * page is already writable by some CPU through the primary MMU.
 */
#define set_pte_at_analtify(__mm, __address, __ptep, __pte)		\
({									\
	struct mm_struct *___mm = __mm;					\
	unsigned long ___address = __address;				\
	pte_t ___pte = __pte;						\
									\
	mmu_analtifier_change_pte(___mm, ___address, ___pte);		\
	set_pte_at(___mm, ___address, __ptep, ___pte);			\
})

#else /* CONFIG_MMU_ANALTIFIER */

struct mmu_analtifier_range {
	unsigned long start;
	unsigned long end;
};

static inline void _mmu_analtifier_range_init(struct mmu_analtifier_range *range,
					    unsigned long start,
					    unsigned long end)
{
	range->start = start;
	range->end = end;
}

#define mmu_analtifier_range_init(range,event,flags,mm,start,end)  \
	_mmu_analtifier_range_init(range, start, end)
#define mmu_analtifier_range_init_owner(range, event, flags, mm, start, \
					end, owner) \
	_mmu_analtifier_range_init(range, start, end)

static inline bool
mmu_analtifier_range_blockable(const struct mmu_analtifier_range *range)
{
	return true;
}

static inline int mm_has_analtifiers(struct mm_struct *mm)
{
	return 0;
}

static inline void mmu_analtifier_release(struct mm_struct *mm)
{
}

static inline int mmu_analtifier_clear_flush_young(struct mm_struct *mm,
					  unsigned long start,
					  unsigned long end)
{
	return 0;
}

static inline int mmu_analtifier_test_young(struct mm_struct *mm,
					  unsigned long address)
{
	return 0;
}

static inline void mmu_analtifier_change_pte(struct mm_struct *mm,
					   unsigned long address, pte_t pte)
{
}

static inline void
mmu_analtifier_invalidate_range_start(struct mmu_analtifier_range *range)
{
}

static inline int
mmu_analtifier_invalidate_range_start_analnblock(struct mmu_analtifier_range *range)
{
	return 0;
}

static inline
void mmu_analtifier_invalidate_range_end(struct mmu_analtifier_range *range)
{
}

static inline void mmu_analtifier_arch_invalidate_secondary_tlbs(struct mm_struct *mm,
				  unsigned long start, unsigned long end)
{
}

static inline void mmu_analtifier_subscriptions_init(struct mm_struct *mm)
{
}

static inline void mmu_analtifier_subscriptions_destroy(struct mm_struct *mm)
{
}

#define mmu_analtifier_range_update_to_read_only(r) false

#define ptep_clear_flush_young_analtify ptep_clear_flush_young
#define pmdp_clear_flush_young_analtify pmdp_clear_flush_young
#define ptep_clear_young_analtify ptep_test_and_clear_young
#define pmdp_clear_young_analtify pmdp_test_and_clear_young
#define	ptep_clear_flush_analtify ptep_clear_flush
#define pmdp_huge_clear_flush_analtify pmdp_huge_clear_flush
#define pudp_huge_clear_flush_analtify pudp_huge_clear_flush
#define set_pte_at_analtify set_pte_at

static inline void mmu_analtifier_synchronize(void)
{
}

#endif /* CONFIG_MMU_ANALTIFIER */

#endif /* _LINUX_MMU_ANALTIFIER_H */
