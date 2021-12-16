/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMU_NOTIFIER_H
#define _LINUX_MMU_NOTIFIER_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mm_types.h>
#include <linux/mmap_lock.h>
#include <linux/percpu-rwsem.h>
#include <linux/slab.h>
#include <linux/srcu.h>
#include <linux/interval_tree.h>
#include <linux/android_kabi.h>

struct mmu_notifier_subscriptions;
struct mmu_notifier;
struct mmu_notifier_range;
struct mmu_interval_notifier;

struct mmu_notifier_subscriptions_hdr {
	bool valid;
#ifdef CONFIG_SPECULATIVE_PAGE_FAULT
	struct percpu_rw_semaphore_atomic *mmu_notifier_lock;
#endif
};

/**
 * enum mmu_notifier_event - reason for the mmu notifier callback
 * @MMU_NOTIFY_UNMAP: either munmap() that unmap the range or a mremap() that
 * move the range
 *
 * @MMU_NOTIFY_CLEAR: clear page table entry (many reasons for this like
 * madvise() or replacing a page by another one, ...).
 *
 * @MMU_NOTIFY_PROTECTION_VMA: update is due to protection change for the range
 * ie using the vma access permission (vm_page_prot) to update the whole range
 * is enough no need to inspect changes to the CPU page table (mprotect()
 * syscall)
 *
 * @MMU_NOTIFY_PROTECTION_PAGE: update is due to change in read/write flag for
 * pages in the range so to mirror those changes the user must inspect the CPU
 * page table (from the end callback).
 *
 * @MMU_NOTIFY_SOFT_DIRTY: soft dirty accounting (still same page and same
 * access flags). User should soft dirty the page in the end callback to make
 * sure that anyone relying on soft dirtyness catch pages that might be written
 * through non CPU mappings.
 *
 * @MMU_NOTIFY_RELEASE: used during mmu_interval_notifier invalidate to signal
 * that the mm refcount is zero and the range is no longer accessible.
 *
 * @MMU_NOTIFY_MIGRATE: used during migrate_vma_collect() invalidate to signal
 * a device driver to possibly ignore the invalidation if the
 * migrate_pgmap_owner field matches the driver's device private pgmap owner.
 */
enum mmu_notifier_event {
	MMU_NOTIFY_UNMAP = 0,
	MMU_NOTIFY_CLEAR,
	MMU_NOTIFY_PROTECTION_VMA,
	MMU_NOTIFY_PROTECTION_PAGE,
	MMU_NOTIFY_SOFT_DIRTY,
	MMU_NOTIFY_RELEASE,
	MMU_NOTIFY_MIGRATE,
};

#define MMU_NOTIFIER_RANGE_BLOCKABLE (1 << 0)

struct mmu_notifier_ops {
	/*
	 * Called either by mmu_notifier_unregister or when the mm is
	 * being destroyed by exit_mmap, always before all pages are
	 * freed. This can run concurrently with other mmu notifier
	 * methods (the ones invoked outside the mm context) and it
	 * should tear down all secondary mmu mappings and freeze the
	 * secondary mmu. If this method isn't implemented you've to
	 * be sure that nothing could possibly write to the pages
	 * through the secondary mmu by the time the last thread with
	 * tsk->mm == mm exits.
	 *
	 * As side note: the pages freed after ->release returns could
	 * be immediately reallocated by the gart at an alias physical
	 * address with a different cache model, so if ->release isn't
	 * implemented because all _software_ driven memory accesses
	 * through the secondary mmu are terminated by the time the
	 * last thread of this mm quits, you've also to be sure that
	 * speculative _hardware_ operations can't allocate dirty
	 * cachelines in the cpu that could not be snooped and made
	 * coherent with the other read and write operations happening
	 * through the gart alias address, so leading to memory
	 * corruption.
	 */
	void (*release)(struct mmu_notifier *subscription,
			struct mm_struct *mm);

	/*
	 * clear_flush_young is called after the VM is
	 * test-and-clearing the young/accessed bitflag in the
	 * pte. This way the VM will provide proper aging to the
	 * accesses to the page through the secondary MMUs and not
	 * only to the ones through the Linux pte.
	 * Start-end is necessary in case the secondary MMU is mapping the page
	 * at a smaller granularity than the primary MMU.
	 */
	int (*clear_flush_young)(struct mmu_notifier *subscription,
				 struct mm_struct *mm,
				 unsigned long start,
				 unsigned long end);

	/*
	 * clear_young is a lightweight version of clear_flush_young. Like the
	 * latter, it is supposed to test-and-clear the young/accessed bitflag
	 * in the secondary pte, but it may omit flushing the secondary tlb.
	 */
	int (*clear_young)(struct mmu_notifier *subscription,
			   struct mm_struct *mm,
			   unsigned long start,
			   unsigned long end);

	/*
	 * test_young is called to check the young/accessed bitflag in
	 * the secondary pte. This is used to know if the page is
	 * frequently used without actually clearing the flag or tearing
	 * down the secondary mapping on the page.
	 */
	int (*test_young)(struct mmu_notifier *subscription,
			  struct mm_struct *mm,
			  unsigned long address);

	/*
	 * change_pte is called in cases that pte mapping to page is changed:
	 * for example, when ksm remaps pte to point to a new shared page.
	 */
	void (*change_pte)(struct mmu_notifier *subscription,
			   struct mm_struct *mm,
			   unsigned long address,
			   pte_t pte);

	/*
	 * invalidate_range_start() and invalidate_range_end() must be
	 * paired and are called only when the mmap_lock and/or the
	 * locks protecting the reverse maps are held. If the subsystem
	 * can't guarantee that no additional references are taken to
	 * the pages in the range, it has to implement the
	 * invalidate_range() notifier to remove any references taken
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
	 * invalidate_range_end(). If the page must not be freed
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
	 * droppped on invalidate_range_end() then the driver itself
	 * will drop the last refcount but it must take care to flush
	 * any secondary tlb before doing the final free on the
	 * page. Pages will no longer be referenced by the linux
	 * address space but may still be referenced by sptes until
	 * the last refcount is dropped.
	 *
	 * If blockable argument is set to false then the callback cannot
	 * sleep and has to return with -EAGAIN if sleeping would be required.
	 * 0 should be returned otherwise. Please note that notifiers that can
	 * fail invalidate_range_start are not allowed to implement
	 * invalidate_range_end, as there is no mechanism for informing the
	 * notifier that its start failed.
	 */
	int (*invalidate_range_start)(struct mmu_notifier *subscription,
				      const struct mmu_notifier_range *range);
	void (*invalidate_range_end)(struct mmu_notifier *subscription,
				     const struct mmu_notifier_range *range);

	/*
	 * invalidate_range() is either called between
	 * invalidate_range_start() and invalidate_range_end() when the
	 * VM has to free pages that where unmapped, but before the
	 * pages are actually freed, or outside of _start()/_end() when
	 * a (remote) TLB is necessary.
	 *
	 * If invalidate_range() is used to manage a non-CPU TLB with
	 * shared page-tables, it not necessary to implement the
	 * invalidate_range_start()/end() notifiers, as
	 * invalidate_range() alread catches the points in time when an
	 * external TLB range needs to be flushed. For more in depth
	 * discussion on this see Documentation/vm/mmu_notifier.rst
	 *
	 * Note that this function might be called with just a sub-range
	 * of what was passed to invalidate_range_start()/end(), if
	 * called between those functions.
	 */
	void (*invalidate_range)(struct mmu_notifier *subscription,
				 struct mm_struct *mm,
				 unsigned long start,
				 unsigned long end);

	/*
	 * These callbacks are used with the get/put interface to manage the
	 * lifetime of the mmu_notifier memory. alloc_notifier() returns a new
	 * notifier for use with the mm.
	 *
	 * free_notifier() is only called after the mmu_notifier has been
	 * fully put, calls to any ops callback are prevented and no ops
	 * callbacks are currently running. It is called from a SRCU callback
	 * and cannot sleep.
	 */
	struct mmu_notifier *(*alloc_notifier)(struct mm_struct *mm);
	void (*free_notifier)(struct mmu_notifier *subscription);

	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
	ANDROID_KABI_RESERVE(3);
	ANDROID_KABI_RESERVE(4);
};

/*
 * The notifier chains are protected by mmap_lock and/or the reverse map
 * semaphores. Notifier chains are only changed when all reverse maps and
 * the mmap_lock locks are taken.
 *
 * Therefore notifier chains can only be traversed when either
 *
 * 1. mmap_lock is held.
 * 2. One of the reverse map locks is held (i_mmap_rwsem or anon_vma->rwsem).
 * 3. No other concurrent thread can access the list (release)
 */
struct mmu_notifier {
	struct hlist_node hlist;
	const struct mmu_notifier_ops *ops;
	struct mm_struct *mm;
	struct rcu_head rcu;
	unsigned int users;

	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
};

/**
 * struct mmu_interval_notifier_ops
 * @invalidate: Upon return the caller must stop using any SPTEs within this
 *              range. This function can sleep. Return false only if sleeping
 *              was required but mmu_notifier_range_blockable(range) is false.
 */
struct mmu_interval_notifier_ops {
	bool (*invalidate)(struct mmu_interval_notifier *interval_sub,
			   const struct mmu_notifier_range *range,
			   unsigned long cur_seq);
};

struct mmu_interval_notifier {
	struct interval_tree_node interval_tree;
	const struct mmu_interval_notifier_ops *ops;
	struct mm_struct *mm;
	struct hlist_node deferred_item;
	unsigned long invalidate_seq;
};

#ifdef CONFIG_MMU_NOTIFIER

#ifdef CONFIG_LOCKDEP
extern struct lockdep_map __mmu_notifier_invalidate_range_start_map;
#endif

struct mmu_notifier_range {
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	unsigned long start;
	unsigned long end;
	unsigned flags;
	enum mmu_notifier_event event;
	void *migrate_pgmap_owner;
};

static inline
struct mmu_notifier_subscriptions_hdr *get_notifier_subscriptions_hdr(
							struct mm_struct *mm)
{
	/*
	 * container_of() can't be used here because mmu_notifier_subscriptions
	 * struct should be kept invisible to mm_struct, otherwise it
	 * introduces KMI CRC breakage. Therefore the callers don't know what
	 * members struct mmu_notifier_subscriptions contains and can't call
	 * container_of(), which requires a member name.
	 *
	 * WARNING: For this typecasting to work, mmu_notifier_subscriptions_hdr
	 * should be the first member of struct mmu_notifier_subscriptions.
	 */
	return (struct mmu_notifier_subscriptions_hdr *)mm->notifier_subscriptions;
}

static inline int mm_has_notifiers(struct mm_struct *mm)
{
#ifdef CONFIG_SPECULATIVE_PAGE_FAULT
	return unlikely(get_notifier_subscriptions_hdr(mm)->valid);
#else
	return unlikely(mm->notifier_subscriptions);
#endif
}

struct mmu_notifier *mmu_notifier_get_locked(const struct mmu_notifier_ops *ops,
					     struct mm_struct *mm);
static inline struct mmu_notifier *
mmu_notifier_get(const struct mmu_notifier_ops *ops, struct mm_struct *mm)
{
	struct mmu_notifier *ret;

	mmap_write_lock(mm);
	ret = mmu_notifier_get_locked(ops, mm);
	mmap_write_unlock(mm);
	return ret;
}
void mmu_notifier_put(struct mmu_notifier *subscription);
void mmu_notifier_synchronize(void);

extern int mmu_notifier_register(struct mmu_notifier *subscription,
				 struct mm_struct *mm);
extern int __mmu_notifier_register(struct mmu_notifier *subscription,
				   struct mm_struct *mm);
extern void mmu_notifier_unregister(struct mmu_notifier *subscription,
				    struct mm_struct *mm);

unsigned long
mmu_interval_read_begin(struct mmu_interval_notifier *interval_sub);
int mmu_interval_notifier_insert(struct mmu_interval_notifier *interval_sub,
				 struct mm_struct *mm, unsigned long start,
				 unsigned long length,
				 const struct mmu_interval_notifier_ops *ops);
int mmu_interval_notifier_insert_locked(
	struct mmu_interval_notifier *interval_sub, struct mm_struct *mm,
	unsigned long start, unsigned long length,
	const struct mmu_interval_notifier_ops *ops);
void mmu_interval_notifier_remove(struct mmu_interval_notifier *interval_sub);

/**
 * mmu_interval_set_seq - Save the invalidation sequence
 * @interval_sub - The subscription passed to invalidate
 * @cur_seq - The cur_seq passed to the invalidate() callback
 *
 * This must be called unconditionally from the invalidate callback of a
 * struct mmu_interval_notifier_ops under the same lock that is used to call
 * mmu_interval_read_retry(). It updates the sequence number for later use by
 * mmu_interval_read_retry(). The provided cur_seq will always be odd.
 *
 * If the caller does not call mmu_interval_read_begin() or
 * mmu_interval_read_retry() then this call is not required.
 */
static inline void
mmu_interval_set_seq(struct mmu_interval_notifier *interval_sub,
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
mmu_interval_read_retry(struct mmu_interval_notifier *interval_sub,
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
 * False is not reliable and only suggests a collision may not have
 * occured. It can be called many times and does not have to hold the user
 * provided lock.
 *
 * This call can be used as part of loops and other expensive operations to
 * expedite a retry.
 */
static inline bool
mmu_interval_check_retry(struct mmu_interval_notifier *interval_sub,
			 unsigned long seq)
{
	/* Pairs with the WRITE_ONCE in mmu_interval_set_seq() */
	return READ_ONCE(interval_sub->invalidate_seq) != seq;
}

extern void __mmu_notifier_subscriptions_destroy(struct mm_struct *mm);
extern void __mmu_notifier_release(struct mm_struct *mm);
extern int __mmu_notifier_clear_flush_young(struct mm_struct *mm,
					  unsigned long start,
					  unsigned long end);
extern int __mmu_notifier_clear_young(struct mm_struct *mm,
				      unsigned long start,
				      unsigned long end);
extern int __mmu_notifier_test_young(struct mm_struct *mm,
				     unsigned long address);
extern void __mmu_notifier_change_pte(struct mm_struct *mm,
				      unsigned long address, pte_t pte);
extern int __mmu_notifier_invalidate_range_start(struct mmu_notifier_range *r);
extern void __mmu_notifier_invalidate_range_end(struct mmu_notifier_range *r,
				  bool only_end);
extern void __mmu_notifier_invalidate_range(struct mm_struct *mm,
				  unsigned long start, unsigned long end);
extern bool
mmu_notifier_range_update_to_read_only(const struct mmu_notifier_range *range);

static inline bool
mmu_notifier_range_blockable(const struct mmu_notifier_range *range)
{
	return (range->flags & MMU_NOTIFIER_RANGE_BLOCKABLE);
}

static inline void mmu_notifier_release(struct mm_struct *mm)
{
	if (mm_has_notifiers(mm))
		__mmu_notifier_release(mm);
}

static inline int mmu_notifier_clear_flush_young(struct mm_struct *mm,
					  unsigned long start,
					  unsigned long end)
{
	if (mm_has_notifiers(mm))
		return __mmu_notifier_clear_flush_young(mm, start, end);
	return 0;
}

static inline int mmu_notifier_clear_young(struct mm_struct *mm,
					   unsigned long start,
					   unsigned long end)
{
	if (mm_has_notifiers(mm))
		return __mmu_notifier_clear_young(mm, start, end);
	return 0;
}

static inline int mmu_notifier_test_young(struct mm_struct *mm,
					  unsigned long address)
{
	if (mm_has_notifiers(mm))
		return __mmu_notifier_test_young(mm, address);
	return 0;
}

static inline void mmu_notifier_change_pte(struct mm_struct *mm,
					   unsigned long address, pte_t pte)
{
	if (mm_has_notifiers(mm))
		__mmu_notifier_change_pte(mm, address, pte);
}

static inline void
mmu_notifier_invalidate_range_start(struct mmu_notifier_range *range)
{
	might_sleep();

	lock_map_acquire(&__mmu_notifier_invalidate_range_start_map);
	if (mm_has_notifiers(range->mm)) {
		range->flags |= MMU_NOTIFIER_RANGE_BLOCKABLE;
		__mmu_notifier_invalidate_range_start(range);
	}
	lock_map_release(&__mmu_notifier_invalidate_range_start_map);
}

static inline int
mmu_notifier_invalidate_range_start_nonblock(struct mmu_notifier_range *range)
{
	int ret = 0;

	lock_map_acquire(&__mmu_notifier_invalidate_range_start_map);
	if (mm_has_notifiers(range->mm)) {
		range->flags &= ~MMU_NOTIFIER_RANGE_BLOCKABLE;
		ret = __mmu_notifier_invalidate_range_start(range);
	}
	lock_map_release(&__mmu_notifier_invalidate_range_start_map);
	return ret;
}

static inline void
mmu_notifier_invalidate_range_end(struct mmu_notifier_range *range)
{
	if (mmu_notifier_range_blockable(range))
		might_sleep();

	if (mm_has_notifiers(range->mm))
		__mmu_notifier_invalidate_range_end(range, false);
}

static inline void
mmu_notifier_invalidate_range_only_end(struct mmu_notifier_range *range)
{
	if (mm_has_notifiers(range->mm))
		__mmu_notifier_invalidate_range_end(range, true);
}

static inline void mmu_notifier_invalidate_range(struct mm_struct *mm,
				  unsigned long start, unsigned long end)
{
	if (mm_has_notifiers(mm))
		__mmu_notifier_invalidate_range(mm, start, end);
}

#ifdef CONFIG_SPECULATIVE_PAGE_FAULT

extern bool mmu_notifier_subscriptions_init(struct mm_struct *mm);
extern void mmu_notifier_subscriptions_destroy(struct mm_struct *mm);

static inline bool mmu_notifier_trylock(struct mm_struct *mm)
{
	return percpu_down_read_trylock(
		&get_notifier_subscriptions_hdr(mm)->mmu_notifier_lock->rw_sem);
}

static inline void mmu_notifier_unlock(struct mm_struct *mm)
{
	percpu_up_read(
		&get_notifier_subscriptions_hdr(mm)->mmu_notifier_lock->rw_sem);
}

#else /* CONFIG_SPECULATIVE_PAGE_FAULT */

static inline bool mmu_notifier_subscriptions_init(struct mm_struct *mm)
{
	mm->notifier_subscriptions = NULL;
	return true;
}

static inline void mmu_notifier_subscriptions_destroy(struct mm_struct *mm)
{
	if (mm_has_notifiers(mm))
		__mmu_notifier_subscriptions_destroy(mm);
}

static inline bool mmu_notifier_trylock(struct mm_struct *mm)
{
	return true;
}

static inline void mmu_notifier_unlock(struct mm_struct *mm)
{
}

#endif /* CONFIG_SPECULATIVE_PAGE_FAULT */

static inline void mmu_notifier_range_init(struct mmu_notifier_range *range,
					   enum mmu_notifier_event event,
					   unsigned flags,
					   struct vm_area_struct *vma,
					   struct mm_struct *mm,
					   unsigned long start,
					   unsigned long end)
{
	range->vma = vma;
	range->event = event;
	range->mm = mm;
	range->start = start;
	range->end = end;
	range->flags = flags;
}

static inline void mmu_notifier_range_init_migrate(
			struct mmu_notifier_range *range, unsigned int flags,
			struct vm_area_struct *vma, struct mm_struct *mm,
			unsigned long start, unsigned long end, void *pgmap)
{
	mmu_notifier_range_init(range, MMU_NOTIFY_MIGRATE, flags, vma, mm,
				start, end);
	range->migrate_pgmap_owner = pgmap;
}

#define ptep_clear_flush_young_notify(__vma, __address, __ptep)		\
({									\
	int __young;							\
	struct vm_area_struct *___vma = __vma;				\
	unsigned long ___address = __address;				\
	__young = ptep_clear_flush_young(___vma, ___address, __ptep);	\
	__young |= mmu_notifier_clear_flush_young(___vma->vm_mm,	\
						  ___address,		\
						  ___address +		\
							PAGE_SIZE);	\
	__young;							\
})

#define pmdp_clear_flush_young_notify(__vma, __address, __pmdp)		\
({									\
	int __young;							\
	struct vm_area_struct *___vma = __vma;				\
	unsigned long ___address = __address;				\
	__young = pmdp_clear_flush_young(___vma, ___address, __pmdp);	\
	__young |= mmu_notifier_clear_flush_young(___vma->vm_mm,	\
						  ___address,		\
						  ___address +		\
							PMD_SIZE);	\
	__young;							\
})

#define ptep_clear_young_notify(__vma, __address, __ptep)		\
({									\
	int __young;							\
	struct vm_area_struct *___vma = __vma;				\
	unsigned long ___address = __address;				\
	__young = ptep_test_and_clear_young(___vma, ___address, __ptep);\
	__young |= mmu_notifier_clear_young(___vma->vm_mm, ___address,	\
					    ___address + PAGE_SIZE);	\
	__young;							\
})

#define pmdp_clear_young_notify(__vma, __address, __pmdp)		\
({									\
	int __young;							\
	struct vm_area_struct *___vma = __vma;				\
	unsigned long ___address = __address;				\
	__young = pmdp_test_and_clear_young(___vma, ___address, __pmdp);\
	__young |= mmu_notifier_clear_young(___vma->vm_mm, ___address,	\
					    ___address + PMD_SIZE);	\
	__young;							\
})

#define	ptep_clear_flush_notify(__vma, __address, __ptep)		\
({									\
	unsigned long ___addr = __address & PAGE_MASK;			\
	struct mm_struct *___mm = (__vma)->vm_mm;			\
	pte_t ___pte;							\
									\
	___pte = ptep_clear_flush(__vma, __address, __ptep);		\
	mmu_notifier_invalidate_range(___mm, ___addr,			\
					___addr + PAGE_SIZE);		\
									\
	___pte;								\
})

#define pmdp_huge_clear_flush_notify(__vma, __haddr, __pmd)		\
({									\
	unsigned long ___haddr = __haddr & HPAGE_PMD_MASK;		\
	struct mm_struct *___mm = (__vma)->vm_mm;			\
	pmd_t ___pmd;							\
									\
	___pmd = pmdp_huge_clear_flush(__vma, __haddr, __pmd);		\
	mmu_notifier_invalidate_range(___mm, ___haddr,			\
				      ___haddr + HPAGE_PMD_SIZE);	\
									\
	___pmd;								\
})

#define pudp_huge_clear_flush_notify(__vma, __haddr, __pud)		\
({									\
	unsigned long ___haddr = __haddr & HPAGE_PUD_MASK;		\
	struct mm_struct *___mm = (__vma)->vm_mm;			\
	pud_t ___pud;							\
									\
	___pud = pudp_huge_clear_flush(__vma, __haddr, __pud);		\
	mmu_notifier_invalidate_range(___mm, ___haddr,			\
				      ___haddr + HPAGE_PUD_SIZE);	\
									\
	___pud;								\
})

/*
 * set_pte_at_notify() sets the pte _after_ running the notifier.
 * This is safe to start by updating the secondary MMUs, because the primary MMU
 * pte invalidate must have already happened with a ptep_clear_flush() before
 * set_pte_at_notify() has been invoked.  Updating the secondary MMUs first is
 * required when we change both the protection of the mapping from read-only to
 * read-write and the pfn (like during copy on write page faults). Otherwise the
 * old page would remain mapped readonly in the secondary MMUs after the new
 * page is already writable by some CPU through the primary MMU.
 */
#define set_pte_at_notify(__mm, __address, __ptep, __pte)		\
({									\
	struct mm_struct *___mm = __mm;					\
	unsigned long ___address = __address;				\
	pte_t ___pte = __pte;						\
									\
	mmu_notifier_change_pte(___mm, ___address, ___pte);		\
	set_pte_at(___mm, ___address, __ptep, ___pte);			\
})

#else /* CONFIG_MMU_NOTIFIER */

struct mmu_notifier_range {
	unsigned long start;
	unsigned long end;
};

static inline void _mmu_notifier_range_init(struct mmu_notifier_range *range,
					    unsigned long start,
					    unsigned long end)
{
	range->start = start;
	range->end = end;
}

#define mmu_notifier_range_init(range,event,flags,vma,mm,start,end)  \
	_mmu_notifier_range_init(range, start, end)
#define mmu_notifier_range_init_migrate(range, flags, vma, mm, start, end, \
					pgmap) \
	_mmu_notifier_range_init(range, start, end)

static inline bool
mmu_notifier_range_blockable(const struct mmu_notifier_range *range)
{
	return true;
}

static inline int mm_has_notifiers(struct mm_struct *mm)
{
	return 0;
}

static inline void mmu_notifier_release(struct mm_struct *mm)
{
}

static inline int mmu_notifier_clear_flush_young(struct mm_struct *mm,
					  unsigned long start,
					  unsigned long end)
{
	return 0;
}

static inline int mmu_notifier_test_young(struct mm_struct *mm,
					  unsigned long address)
{
	return 0;
}

static inline void mmu_notifier_change_pte(struct mm_struct *mm,
					   unsigned long address, pte_t pte)
{
}

static inline void
mmu_notifier_invalidate_range_start(struct mmu_notifier_range *range)
{
}

static inline int
mmu_notifier_invalidate_range_start_nonblock(struct mmu_notifier_range *range)
{
	return 0;
}

static inline
void mmu_notifier_invalidate_range_end(struct mmu_notifier_range *range)
{
}

static inline void
mmu_notifier_invalidate_range_only_end(struct mmu_notifier_range *range)
{
}

static inline void mmu_notifier_invalidate_range(struct mm_struct *mm,
				  unsigned long start, unsigned long end)
{
}

static inline bool mmu_notifier_subscriptions_init(struct mm_struct *mm)
{
	return true;
}

static inline void mmu_notifier_subscriptions_destroy(struct mm_struct *mm)
{
}

static inline bool mmu_notifier_trylock(struct mm_struct *mm)
{
	return true;
}

static inline void mmu_notifier_unlock(struct mm_struct *mm)
{
}

#define mmu_notifier_range_update_to_read_only(r) false

#define ptep_clear_flush_young_notify ptep_clear_flush_young
#define pmdp_clear_flush_young_notify pmdp_clear_flush_young
#define ptep_clear_young_notify ptep_test_and_clear_young
#define pmdp_clear_young_notify pmdp_test_and_clear_young
#define	ptep_clear_flush_notify ptep_clear_flush
#define pmdp_huge_clear_flush_notify pmdp_huge_clear_flush
#define pudp_huge_clear_flush_notify pudp_huge_clear_flush
#define set_pte_at_notify set_pte_at

static inline void mmu_notifier_synchronize(void)
{
}

#endif /* CONFIG_MMU_NOTIFIER */

#endif /* _LINUX_MMU_NOTIFIER_H */
