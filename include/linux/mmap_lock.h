/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMAP_LOCK_H
#define _LINUX_MMAP_LOCK_H

/* Avoid a dependency loop by declaring here. */
extern int rcuwait_wake_up(struct rcuwait *w);

#include <linux/lockdep.h>
#include <linux/mm_types.h>
#include <linux/mmdebug.h>
#include <linux/rwsem.h>
#include <linux/tracepoint-defs.h>
#include <linux/types.h>
#include <linux/cleanup.h>
#include <linux/sched/mm.h>

#define MMAP_LOCK_INITIALIZER(name) \
	.mmap_lock = __RWSEM_INITIALIZER((name).mmap_lock),

DECLARE_TRACEPOINT(mmap_lock_start_locking);
DECLARE_TRACEPOINT(mmap_lock_acquire_returned);
DECLARE_TRACEPOINT(mmap_lock_released);

#ifdef CONFIG_TRACING

void __mmap_lock_do_trace_start_locking(struct mm_struct *mm, bool write);
void __mmap_lock_do_trace_acquire_returned(struct mm_struct *mm, bool write,
					   bool success);
void __mmap_lock_do_trace_released(struct mm_struct *mm, bool write);

static inline void __mmap_lock_trace_start_locking(struct mm_struct *mm,
						   bool write)
{
	if (tracepoint_enabled(mmap_lock_start_locking))
		__mmap_lock_do_trace_start_locking(mm, write);
}

static inline void __mmap_lock_trace_acquire_returned(struct mm_struct *mm,
						      bool write, bool success)
{
	if (tracepoint_enabled(mmap_lock_acquire_returned))
		__mmap_lock_do_trace_acquire_returned(mm, write, success);
}

static inline void __mmap_lock_trace_released(struct mm_struct *mm, bool write)
{
	if (tracepoint_enabled(mmap_lock_released))
		__mmap_lock_do_trace_released(mm, write);
}

#else /* !CONFIG_TRACING */

static inline void __mmap_lock_trace_start_locking(struct mm_struct *mm,
						   bool write)
{
}

static inline void __mmap_lock_trace_acquire_returned(struct mm_struct *mm,
						      bool write, bool success)
{
}

static inline void __mmap_lock_trace_released(struct mm_struct *mm, bool write)
{
}

#endif /* CONFIG_TRACING */

static inline void mmap_assert_locked(const struct mm_struct *mm)
{
	rwsem_assert_held(&mm->mmap_lock);
}

static inline void mmap_assert_write_locked(const struct mm_struct *mm)
{
	rwsem_assert_held_write(&mm->mmap_lock);
}

#ifdef CONFIG_PER_VMA_LOCK

#ifdef CONFIG_LOCKDEP
#define __vma_lockdep_map(vma) (&vma->vmlock_dep_map)
#else
#define __vma_lockdep_map(vma) NULL
#endif

/*
 * VMA locks do not behave like most ordinary locks found in the kernel, so we
 * cannot quite have full lockdep tracking in the way we would ideally prefer.
 *
 * Read locks act as shared locks which exclude an exclusive lock being
 * taken. We therefore mark these accordingly on read lock acquire/release.
 *
 * Write locks are acquired exclusively per-VMA, but released in a shared
 * fashion, that is upon vma_end_write_all(), we update the mmap's seqcount such
 * that write lock is released.
 *
 * We therefore cannot track write locks per-VMA, nor do we try. Mitigating this
 * is the fact that, of course, we do lockdep-track the mmap lock rwsem which
 * must be held when taking a VMA write lock.
 *
 * We do, however, want to indicate that during either acquisition of a VMA
 * write lock or detachment of a VMA that we require the lock held be exclusive,
 * so we utilise lockdep to do so.
 */
#define __vma_lockdep_acquire_read(vma) \
	lock_acquire_shared(__vma_lockdep_map(vma), 0, 1, NULL, _RET_IP_)
#define __vma_lockdep_release_read(vma) \
	lock_release(__vma_lockdep_map(vma), _RET_IP_)
#define __vma_lockdep_acquire_exclusive(vma) \
	lock_acquire_exclusive(__vma_lockdep_map(vma), 0, 0, NULL, _RET_IP_)
#define __vma_lockdep_release_exclusive(vma) \
	lock_release(__vma_lockdep_map(vma), _RET_IP_)
/* Only meaningful if CONFIG_LOCK_STAT is defined. */
#define __vma_lockdep_stat_mark_acquired(vma) \
	lock_acquired(__vma_lockdep_map(vma), _RET_IP_)

static inline void mm_lock_seqcount_init(struct mm_struct *mm)
{
	seqcount_init(&mm->mm_lock_seq);
}

static inline void mm_lock_seqcount_begin(struct mm_struct *mm)
{
	do_raw_write_seqcount_begin(&mm->mm_lock_seq);
}

static inline void mm_lock_seqcount_end(struct mm_struct *mm)
{
	ASSERT_EXCLUSIVE_WRITER(mm->mm_lock_seq);
	do_raw_write_seqcount_end(&mm->mm_lock_seq);
}

static inline bool mmap_lock_speculate_try_begin(struct mm_struct *mm, unsigned int *seq)
{
	/*
	 * Since mmap_lock is a sleeping lock, and waiting for it to become
	 * unlocked is more or less equivalent with taking it ourselves, don't
	 * bother with the speculative path if mmap_lock is already write-locked
	 * and take the slow path, which takes the lock.
	 */
	return raw_seqcount_try_begin(&mm->mm_lock_seq, *seq);
}

static inline bool mmap_lock_speculate_retry(struct mm_struct *mm, unsigned int seq)
{
	return read_seqcount_retry(&mm->mm_lock_seq, seq);
}

static inline void vma_lock_init(struct vm_area_struct *vma, bool reset_refcnt)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	static struct lock_class_key lockdep_key;

	lockdep_init_map(__vma_lockdep_map(vma), "vm_lock", &lockdep_key, 0);
#endif
	if (reset_refcnt)
		refcount_set(&vma->vm_refcnt, 0);
	vma->vm_lock_seq = UINT_MAX;
}

/*
 * This function determines whether the input VMA reference count describes a
 * VMA which has excluded all VMA read locks.
 *
 * In the case of a detached VMA, we may incorrectly indicate that readers are
 * excluded when one remains, because in that scenario we target a refcount of
 * VM_REFCNT_EXCLUDE_READERS_FLAG, rather than the attached target of
 * VM_REFCNT_EXCLUDE_READERS_FLAG + 1.
 *
 * However, the race window for that is very small so it is unlikely.
 *
 * Returns: true if readers are excluded, false otherwise.
 */
static inline bool __vma_are_readers_excluded(int refcnt)
{
	/*
	 * See the comment describing the vm_area_struct->vm_refcnt field for
	 * details of possible refcnt values.
	 */
	return (refcnt & VM_REFCNT_EXCLUDE_READERS_FLAG) &&
		refcnt <= VM_REFCNT_EXCLUDE_READERS_FLAG + 1;
}

/*
 * Actually decrement the VMA reference count.
 *
 * The function returns the reference count as it was immediately after the
 * decrement took place. If it returns zero, the VMA is now detached.
 */
static inline __must_check unsigned int
__vma_refcount_put_return(struct vm_area_struct *vma)
{
	int oldcnt;

	if (__refcount_dec_and_test(&vma->vm_refcnt, &oldcnt))
		return 0;

	return oldcnt - 1;
}

/**
 * vma_refcount_put() - Drop reference count in VMA vm_refcnt field due to a
 * read-lock being dropped.
 * @vma: The VMA whose reference count we wish to decrement.
 *
 * If we were the last reader, wake up threads waiting to obtain an exclusive
 * lock.
 */
static inline void vma_refcount_put(struct vm_area_struct *vma)
{
	/* Use a copy of vm_mm in case vma is freed after we drop vm_refcnt. */
	struct mm_struct *mm = vma->vm_mm;
	int newcnt;

	__vma_lockdep_release_read(vma);
	newcnt = __vma_refcount_put_return(vma);

	/*
	 * __vma_start_exclude_readers() may be sleeping waiting for readers to
	 * drop their reference count, so wake it up if we were the last reader
	 * blocking it from being acquired.
	 *
	 * We may be raced by other readers temporarily incrementing the
	 * reference count, though the race window is very small, this might
	 * cause spurious wakeups.
	 */
	if (newcnt && __vma_are_readers_excluded(newcnt))
		rcuwait_wake_up(&mm->vma_writer_wait);
}

/*
 * Use only while holding mmap read lock which guarantees that locking will not
 * fail (nobody can concurrently write-lock the vma). vma_start_read() should
 * not be used in such cases because it might fail due to mm_lock_seq overflow.
 * This functionality is used to obtain vma read lock and drop the mmap read lock.
 */
static inline bool vma_start_read_locked_nested(struct vm_area_struct *vma, int subclass)
{
	int oldcnt;

	mmap_assert_locked(vma->vm_mm);
	if (unlikely(!__refcount_inc_not_zero_limited_acquire(&vma->vm_refcnt, &oldcnt,
							      VM_REFCNT_LIMIT)))
		return false;

	__vma_lockdep_acquire_read(vma);
	return true;
}

/*
 * Use only while holding mmap read lock which guarantees that locking will not
 * fail (nobody can concurrently write-lock the vma). vma_start_read() should
 * not be used in such cases because it might fail due to mm_lock_seq overflow.
 * This functionality is used to obtain vma read lock and drop the mmap read lock.
 */
static inline bool vma_start_read_locked(struct vm_area_struct *vma)
{
	return vma_start_read_locked_nested(vma, 0);
}

static inline void vma_end_read(struct vm_area_struct *vma)
{
	vma_refcount_put(vma);
}

static inline unsigned int __vma_raw_mm_seqnum(struct vm_area_struct *vma)
{
	const struct mm_struct *mm = vma->vm_mm;

	/* We must hold an exclusive write lock for this access to be valid. */
	mmap_assert_write_locked(vma->vm_mm);
	return mm->mm_lock_seq.sequence;
}

/*
 * Determine whether a VMA is write-locked. Must be invoked ONLY if the mmap
 * write lock is held.
 *
 * Returns true if write-locked, otherwise false.
 */
static inline bool __is_vma_write_locked(struct vm_area_struct *vma)
{
	/*
	 * current task is holding mmap_write_lock, both vma->vm_lock_seq and
	 * mm->mm_lock_seq can't be concurrently modified.
	 */
	return vma->vm_lock_seq == __vma_raw_mm_seqnum(vma);
}

int __vma_start_write(struct vm_area_struct *vma, int state);

/*
 * Begin writing to a VMA.
 * Exclude concurrent readers under the per-VMA lock until the currently
 * write-locked mmap_lock is dropped or downgraded.
 */
static inline void vma_start_write(struct vm_area_struct *vma)
{
	if (__is_vma_write_locked(vma))
		return;

	__vma_start_write(vma, TASK_UNINTERRUPTIBLE);
}

/**
 * vma_start_write_killable - Begin writing to a VMA.
 * @vma: The VMA we are going to modify.
 *
 * Exclude concurrent readers under the per-VMA lock until the currently
 * write-locked mmap_lock is dropped or downgraded.
 *
 * Context: May sleep while waiting for readers to drop the vma read lock.
 * Caller must already hold the mmap_lock for write.
 *
 * Return: 0 for a successful acquisition.  -EINTR if a fatal signal was
 * received.
 */
static inline __must_check
int vma_start_write_killable(struct vm_area_struct *vma)
{
	if (__is_vma_write_locked(vma))
		return 0;

	return __vma_start_write(vma, TASK_KILLABLE);
}

/**
 * vma_assert_write_locked() - assert that @vma holds a VMA write lock.
 * @vma: The VMA to assert.
 */
static inline void vma_assert_write_locked(struct vm_area_struct *vma)
{
	VM_WARN_ON_ONCE_VMA(!__is_vma_write_locked(vma), vma);
}

/**
 * vma_assert_locked() - assert that @vma holds either a VMA read or a VMA write
 * lock and is not detached.
 * @vma: The VMA to assert.
 */
static inline void vma_assert_locked(struct vm_area_struct *vma)
{
	unsigned int refcnt;

	if (IS_ENABLED(CONFIG_LOCKDEP)) {
		if (!lock_is_held(__vma_lockdep_map(vma)))
			vma_assert_write_locked(vma);
		return;
	}

	/*
	 * See the comment describing the vm_area_struct->vm_refcnt field for
	 * details of possible refcnt values.
	 */
	refcnt = refcount_read(&vma->vm_refcnt);

	/*
	 * In this case we're either read-locked, write-locked with temporary
	 * readers, or in the midst of excluding readers, all of which means
	 * we're locked.
	 */
	if (refcnt > 1)
		return;

	/* It is a bug for the VMA to be detached here. */
	VM_WARN_ON_ONCE_VMA(!refcnt, vma);

	/*
	 * OK, the VMA has a reference count of 1 which means it is either
	 * unlocked and attached or write-locked, so assert that it is
	 * write-locked.
	 */
	vma_assert_write_locked(vma);
}

/**
 * vma_assert_stabilised() - assert that this VMA cannot be changed from
 * underneath us either by having a VMA or mmap lock held.
 * @vma: The VMA whose stability we wish to assess.
 *
 * If lockdep is enabled we can precisely ensure stability via either an mmap
 * lock owned by us or a specific VMA lock.
 *
 * With lockdep disabled we may sometimes race with other threads acquiring the
 * mmap read lock simultaneous with our VMA read lock.
 */
static inline void vma_assert_stabilised(struct vm_area_struct *vma)
{
	/*
	 * If another thread owns an mmap lock, it may go away at any time, and
	 * thus is no guarantee of stability.
	 *
	 * If lockdep is enabled we can accurately determine if an mmap lock is
	 * held and owned by us. Otherwise we must approximate.
	 *
	 * It doesn't necessarily mean we are not stabilised however, as we may
	 * hold a VMA read lock (not a write lock as this would require an owned
	 * mmap lock).
	 *
	 * If (assuming lockdep is not enabled) we were to assert a VMA read
	 * lock first we may also run into issues, as other threads can hold VMA
	 * read locks simlutaneous to us.
	 *
	 * Therefore if lockdep is not enabled we risk a false negative (i.e. no
	 * assert fired). If accurate checking is required, enable lockdep.
	 */
	if (IS_ENABLED(CONFIG_LOCKDEP)) {
		if (lockdep_is_held(&vma->vm_mm->mmap_lock))
			return;
	} else {
		if (rwsem_is_locked(&vma->vm_mm->mmap_lock))
			return;
	}

	/*
	 * We're not stabilised by the mmap lock, so assert that we're
	 * stabilised by a VMA lock.
	 */
	vma_assert_locked(vma);
}

static inline bool vma_is_attached(struct vm_area_struct *vma)
{
	return refcount_read(&vma->vm_refcnt);
}

/*
 * WARNING: to avoid racing with vma_mark_attached()/vma_mark_detached(), these
 * assertions should be made either under mmap_write_lock or when the object
 * has been isolated under mmap_write_lock, ensuring no competing writers.
 */
static inline void vma_assert_attached(struct vm_area_struct *vma)
{
	WARN_ON_ONCE(!vma_is_attached(vma));
}

static inline void vma_assert_detached(struct vm_area_struct *vma)
{
	WARN_ON_ONCE(vma_is_attached(vma));
}

static inline void vma_mark_attached(struct vm_area_struct *vma)
{
	vma_assert_write_locked(vma);
	vma_assert_detached(vma);
	refcount_set_release(&vma->vm_refcnt, 1);
}

void __vma_exclude_readers_for_detach(struct vm_area_struct *vma);

static inline void vma_mark_detached(struct vm_area_struct *vma)
{
	vma_assert_write_locked(vma);
	vma_assert_attached(vma);

	/*
	 * The VMA still being attached (refcnt > 0) - is unlikely, because the
	 * vma has been already write-locked and readers can increment vm_refcnt
	 * only temporarily before they check vm_lock_seq, realize the vma is
	 * locked and drop back the vm_refcnt. That is a narrow window for
	 * observing a raised vm_refcnt.
	 *
	 * See the comment describing the vm_area_struct->vm_refcnt field for
	 * details of possible refcnt values.
	 */
	if (likely(!__vma_refcount_put_return(vma)))
		return;

	__vma_exclude_readers_for_detach(vma);
}

struct vm_area_struct *lock_vma_under_rcu(struct mm_struct *mm,
					  unsigned long address);

/*
 * Locks next vma pointed by the iterator. Confirms the locked vma has not
 * been modified and will retry under mmap_lock protection if modification
 * was detected. Should be called from read RCU section.
 * Returns either a valid locked VMA, NULL if no more VMAs or -EINTR if the
 * process was interrupted.
 */
struct vm_area_struct *lock_next_vma(struct mm_struct *mm,
				     struct vma_iterator *iter,
				     unsigned long address);

#else /* CONFIG_PER_VMA_LOCK */

static inline void mm_lock_seqcount_init(struct mm_struct *mm) {}
static inline void mm_lock_seqcount_begin(struct mm_struct *mm) {}
static inline void mm_lock_seqcount_end(struct mm_struct *mm) {}

static inline bool mmap_lock_speculate_try_begin(struct mm_struct *mm, unsigned int *seq)
{
	return false;
}

static inline bool mmap_lock_speculate_retry(struct mm_struct *mm, unsigned int seq)
{
	return true;
}
static inline void vma_lock_init(struct vm_area_struct *vma, bool reset_refcnt) {}
static inline void vma_end_read(struct vm_area_struct *vma) {}
static inline void vma_start_write(struct vm_area_struct *vma) {}
static inline __must_check
int vma_start_write_killable(struct vm_area_struct *vma) { return 0; }
static inline void vma_assert_write_locked(struct vm_area_struct *vma)
		{ mmap_assert_write_locked(vma->vm_mm); }
static inline void vma_assert_attached(struct vm_area_struct *vma) {}
static inline void vma_assert_detached(struct vm_area_struct *vma) {}
static inline void vma_mark_attached(struct vm_area_struct *vma) {}
static inline void vma_mark_detached(struct vm_area_struct *vma) {}

static inline struct vm_area_struct *lock_vma_under_rcu(struct mm_struct *mm,
		unsigned long address)
{
	return NULL;
}

static inline void vma_assert_locked(struct vm_area_struct *vma)
{
	mmap_assert_locked(vma->vm_mm);
}

static inline void vma_assert_stabilised(struct vm_area_struct *vma)
{
	/* If no VMA locks, then either mmap lock suffices to stabilise. */
	mmap_assert_locked(vma->vm_mm);
}

#endif /* CONFIG_PER_VMA_LOCK */

static inline void mmap_write_lock(struct mm_struct *mm)
{
	__mmap_lock_trace_start_locking(mm, true);
	down_write(&mm->mmap_lock);
	mm_lock_seqcount_begin(mm);
	__mmap_lock_trace_acquire_returned(mm, true, true);
}

static inline void mmap_write_lock_nested(struct mm_struct *mm, int subclass)
{
	__mmap_lock_trace_start_locking(mm, true);
	down_write_nested(&mm->mmap_lock, subclass);
	mm_lock_seqcount_begin(mm);
	__mmap_lock_trace_acquire_returned(mm, true, true);
}

static inline int mmap_write_lock_killable(struct mm_struct *mm)
{
	int ret;

	__mmap_lock_trace_start_locking(mm, true);
	ret = down_write_killable(&mm->mmap_lock);
	if (!ret)
		mm_lock_seqcount_begin(mm);
	__mmap_lock_trace_acquire_returned(mm, true, ret == 0);
	return ret;
}

/*
 * Drop all currently-held per-VMA locks.
 * This is called from the mmap_lock implementation directly before releasing
 * a write-locked mmap_lock (or downgrading it to read-locked).
 * This should normally NOT be called manually from other places.
 * If you want to call this manually anyway, keep in mind that this will release
 * *all* VMA write locks, including ones from further up the stack.
 */
static inline void vma_end_write_all(struct mm_struct *mm)
{
	mmap_assert_write_locked(mm);
	mm_lock_seqcount_end(mm);
}

static inline void mmap_write_unlock(struct mm_struct *mm)
{
	__mmap_lock_trace_released(mm, true);
	vma_end_write_all(mm);
	up_write(&mm->mmap_lock);
}

static inline void mmap_write_downgrade(struct mm_struct *mm)
{
	__mmap_lock_trace_acquire_returned(mm, false, true);
	vma_end_write_all(mm);
	downgrade_write(&mm->mmap_lock);
}

static inline void mmap_read_lock(struct mm_struct *mm)
{
	__mmap_lock_trace_start_locking(mm, false);
	down_read(&mm->mmap_lock);
	__mmap_lock_trace_acquire_returned(mm, false, true);
}

static inline int mmap_read_lock_killable(struct mm_struct *mm)
{
	int ret;

	__mmap_lock_trace_start_locking(mm, false);
	ret = down_read_killable(&mm->mmap_lock);
	__mmap_lock_trace_acquire_returned(mm, false, ret == 0);
	return ret;
}

static inline bool mmap_read_trylock(struct mm_struct *mm)
{
	bool ret;

	__mmap_lock_trace_start_locking(mm, false);
	ret = down_read_trylock(&mm->mmap_lock) != 0;
	__mmap_lock_trace_acquire_returned(mm, false, ret);
	return ret;
}

static inline void mmap_read_unlock(struct mm_struct *mm)
{
	__mmap_lock_trace_released(mm, false);
	up_read(&mm->mmap_lock);
}

DEFINE_GUARD(mmap_read_lock, struct mm_struct *,
	     mmap_read_lock(_T), mmap_read_unlock(_T))

static inline void mmap_read_unlock_non_owner(struct mm_struct *mm)
{
	__mmap_lock_trace_released(mm, false);
	up_read_non_owner(&mm->mmap_lock);
}

static inline int mmap_lock_is_contended(struct mm_struct *mm)
{
	return rwsem_is_contended(&mm->mmap_lock);
}

#endif /* _LINUX_MMAP_LOCK_H */
